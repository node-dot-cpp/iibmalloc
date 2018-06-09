/* -------------------------------------------------------------------------------
 * Copyright (c) 2018, OLogN Technologies AG
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * -------------------------------------------------------------------------------
 * 
 * Per-thread bucket allocator
 * Page Allocator: 
 *     - returns a requested number of allocated (or previously cached) pages
 *     - accepts a pointer to pages to be deallocated (pointer to and number of
 *       pages must be that from one of requests for allocation)
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef PAGE_ALLOCATOR_H
#define PAGE_ALLOCATOR_H

#include "bucket_allocator_common.h"

#define GET_PERF_DATA

#ifdef GET_PERF_DATA
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif // GET_PERF_DATA

constexpr uint8_t CACHE_LINE_ALIGNMENT_EXP = sizeToExp(64);


/* OS specific implementations */
class VirtualMemory
{
public:
	static size_t getPageSize();
	static size_t getAllocGranularity();

	static unsigned char* reserve(void* addr, size_t size);
	static void commit(uintptr_t addr, size_t size);
	static void decommit(uintptr_t addr, size_t size);

	static void* allocate(size_t size);
	static void deallocate(void* ptr, size_t size);
//	static void release(void* addr);
};

struct MemoryBlockListItem
{
	MemoryBlockListItem* next;
	MemoryBlockListItem* prev;
	typedef size_t SizeT; //todo
//	static constexpr SizeT INUSE_FLAG = 1;
	SizeT size;
	SizeT sizeIndex;

	enum BucketKinds :uint8_t
	{
		NoBucket = 0,
		SmallBucket,
		MediumBucket
	} bucketKind = NoBucket;


	BucketKinds getBucketKind() const { return bucketKind; }
	void setBucketKind(BucketKinds kind) { bucketKind = kind; }


	void initialize(SizeT sz, SizeT szIndex)
	{
		size = sz;
		prev = nullptr;
		next = nullptr;
		sizeIndex = szIndex;
		bucketKind = NoBucket;
	}

	void listInitializeEmpty()
	{
		next = this;
		prev = this;
	}
	SizeT getSizeIndex() const { return sizeIndex; }
	SizeT getSize() const {	return size; }

	void listInsertNext(MemoryBlockListItem* other)
	{
		assert(isInList());
		assert(!other->isInList());

		other->next = this->next;
		other->prev = this;
		next->prev = other;
		next = other;
	}

	MemoryBlockListItem* listGetNext()
	{
		return next;
	}
	
	MemoryBlockListItem* listGetPrev()
	{
		return prev;
	}

	bool isInList() const
	{
		if (prev == nullptr && next == nullptr)
			return false;
		else
		{
			assert(prev != nullptr);
			assert(next != nullptr);
			return true;
		}
	}

	void removeFromList()
	{
		assert(prev != nullptr);
		assert(next != nullptr);

		prev->next = next;
		next->prev = prev;

		next = nullptr;
		prev = nullptr;
	}


};


class MemoryBlockList
{
private:
	uint32_t count = 0;
	MemoryBlockListItem lst;
public:
	
	MemoryBlockList()
	{
		lst.listInitializeEmpty();
	}

	FORCE_INLINE
	bool empty() const { return count == 0; }
	FORCE_INLINE
		uint32_t size() const { return count; }
	FORCE_INLINE
		bool isEnd(MemoryBlockListItem* item) const { return item == &lst; }

	FORCE_INLINE
	MemoryBlockListItem* front()

	{
		return lst.listGetNext();
	}

	FORCE_INLINE
	void pushFront(MemoryBlockListItem* chk)
	{
		lst.listInsertNext(chk);
		++count;
	}

	FORCE_INLINE
	MemoryBlockListItem* popFront()
	{
		assert(!empty());

		MemoryBlockListItem* chk = lst.listGetNext();
		chk->removeFromList();
		--count;

		return chk;
	}

	FORCE_INLINE
		MemoryBlockListItem* popBack()
	{
		assert(!empty());

		MemoryBlockListItem* chk = lst.listGetPrev();
		chk->removeFromList();
		--count;

		return chk;
	}

	FORCE_INLINE
	void remove(MemoryBlockListItem* chk)
	{
		chk->removeFromList();
		--count;
	}

	size_t getCount() const { return count; }
};

struct BlockStats
{
	//alloc / dealloc ops
	uint64_t sysAllocCount = 0;
	uint64_t sysAllocSize = 0;
	uint64_t rdtscSysAllocSpent = 0;

	uint64_t sysDeallocCount = 0;
	uint64_t sysDeallocSize = 0;
	uint64_t rdtscSysDeallocSpent = 0;

	uint64_t allocRequestCount;
	uint64_t allocRequestSize;

	uint64_t deallocRequestCount;
	uint64_t deallocRequestSize;

	void printStats()
	{
		printf("Allocs %zd (%zd), ", sysAllocCount, sysAllocSize);
		printf("Deallocs %zd (%zd), ", sysDeallocCount, sysDeallocSize);

		uint64_t ct = sysAllocCount - sysDeallocCount;
		uint64_t sz = sysAllocSize - sysDeallocSize;

		printf("Diff %zd (%zd)\n\n", ct, sz);
	}

	void registerAllocRequest( size_t sz )
	{
		allocRequestSize += sz;
		++allocRequestCount;
	}
	void registerDeallocRequest( size_t sz )
	{
		deallocRequestSize += sz;
		++deallocRequestCount;
	}

	void registerSysAlloc( size_t sz, uint64_t rdtscSpent )
	{
		sysAllocSize += sz;
		rdtscSysAllocSpent += rdtscSpent;
		++sysAllocCount;
	}
	void registerSysDealloc( size_t sz, uint64_t rdtscSpent )
	{
		sysDeallocSize += sz;
		rdtscSysDeallocSpent += rdtscSpent;
		++sysDeallocCount;
	}
};

struct PageAllocator // rather a proof of concept
{
	static constexpr size_t OVERHEAD = alignUpExp(sizeof(MemoryBlockListItem), CACHE_LINE_ALIGNMENT_EXP);

	BlockStats stats;
	uint8_t blockSizeExp = 0;

public:

	void initialize(uint8_t blockSizeExp)
	{
		this->blockSizeExp = blockSizeExp;
	}

	MemoryBlockListItem* getFreeBlockInt(size_t sz)
	{
		stats.registerAllocRequest( sz );

		assert(isAlignedExp(sz, blockSizeExp));

		uint64_t start = __rdtsc();
		void* ptr = VirtualMemory::allocate(sz);
		uint64_t end = __rdtsc();

		stats.registerSysAlloc( sz, end - start );

		if (ptr)
		{
			MemoryBlockListItem* chk = static_cast<MemoryBlockListItem*>(ptr);
			chk->initialize(sz, 0);
			return chk;
		}

		throw std::bad_alloc();
	}

	void* getFreeBlock(size_t sz)
	{
		size_t chkSz = alignUpExp(sz + OVERHEAD, blockSizeExp);

		MemoryBlockListItem* chk = getFreeBlockInt(chkSz);

		return reinterpret_cast<uint8_t*>(chk) + OVERHEAD;
	}

	MemoryBlockListItem* getBucketBlock(size_t sz)
	{
		return getFreeBlockInt(sz);
	}

	void freeChunk(void* ptr)
	{
		MemoryBlockListItem* chk = reinterpret_cast<MemoryBlockListItem*>(ptr);
		size_t ix = chk->getSizeIndex();
		assert ( ix == 0 );

		size_t sz = chk->getSize();
		stats.registerDeallocRequest( sz );
		uint64_t start = __rdtsc();
		VirtualMemory::deallocate(chk, sz );
		uint64_t end = __rdtsc();
		stats.registerSysDealloc( sz, end - start );

	}

	const BlockStats& getStats() const { return stats; }

	void doHouseKeeping() {}

	void printStats()
	{
		stats.printStats();
	}
};


constexpr size_t max_cached_size = 256; // # of pages
constexpr size_t single_page_cache_size = 4;
constexpr size_t multi_page_cache_size = 2;

struct PageAllocatorWithCaching // to be further developed for practical purposes
{
	static constexpr size_t OVERHEAD = alignUpExp(sizeof(MemoryBlockListItem), CACHE_LINE_ALIGNMENT_EXP);
	//	Chunk* topChunk = nullptr;
	std::array<MemoryBlockList, max_cached_size+1> freeBlocks;

	BlockStats stats;
	//uintptr_t blocksBegin = 0;
	//uintptr_t uninitializedBlocksBegin = 0;
	//uintptr_t blocksEnd = 0;
	uint8_t blockSizeExp = 0;

public:

	void initialize(uint8_t blockSizeExp)
	{
		this->blockSizeExp = blockSizeExp;
	}

	MemoryBlockListItem* getFreeBlockInt(size_t sz)
	{
		stats.registerAllocRequest( sz );

		assert(isAlignedExp(sz, blockSizeExp));

		size_t ix = (sz >> blockSizeExp)-1;
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				MemoryBlockListItem* chk = static_cast<MemoryBlockListItem*>(freeBlocks[ix].popFront());
				chk->initialize(sz, ix);
//				assert(chk->isFree());
//				chk->setInUse();
				return chk;
			}
		}

		uint64_t start = __rdtsc();
		void* ptr = VirtualMemory::allocate(sz);
		uint64_t end = __rdtsc();
		stats.registerSysAlloc( sz, end - start );

		if (ptr)
		{
			MemoryBlockListItem* chk = static_cast<MemoryBlockListItem*>(ptr);
			chk->initialize(sz, ix);
			return chk;
		}
		//todo enlarge top chunk

		throw std::bad_alloc();
	}

	void* getFreeBlock(size_t sz)
	{
		size_t chkSz = alignUpExp(sz + OVERHEAD, blockSizeExp);

		MemoryBlockListItem* chk = getFreeBlockInt(chkSz);

		return reinterpret_cast<uint8_t*>(chk) + OVERHEAD;
	}

	MemoryBlockListItem* getBucketBlock(size_t sz)
	{
		return getFreeBlockInt(sz);
	}


	void freeChunk( void* ptr )
	{
//		assert(!chk->isFree());
//		assert(!chk->isInList());

		MemoryBlockListItem* chk = reinterpret_cast<MemoryBlockListItem*>(ptr);
		size_t sz = chk->getSize();
		stats.registerDeallocRequest( sz );
		size_t ix = chk->getSizeIndex();
		if ( ix == 0 ) // quite likely case (all bucket chunks)
		{
			if ( freeBlocks[ix].getCount() < single_page_cache_size )
			{
//				chk->setFree();
				freeBlocks[ix].pushFront(chk);
				return;
			}
		}
		else if ( ix < max_cached_size && freeBlocks[ix].getCount() < multi_page_cache_size )
		{
//			chk->setFree();
			freeBlocks[ix].pushFront(chk);
			return;
		}

		uint64_t start = __rdtsc();
		VirtualMemory::deallocate(chk, sz );
		uint64_t end = __rdtsc();
		stats.registerSysDealloc( sz, end - start );
	}

	const BlockStats& getStats() const { return stats; }

	void doHouseKeeping() {}

	void printStats()
	{
		stats.printStats();
	}
};


#endif //PAGE_ALLOCATOR_H
