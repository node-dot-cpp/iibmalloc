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

struct ListItem
{
	ListItem* next;
	ListItem* prev;

	void listInitializeNull()
	{
		next = nullptr;
		prev = nullptr;
	}

	void listInitializeEmpty()
	{
		next = this;
		prev = this;
	}

	void listInsertNext(ListItem* other)
	{
		assert(isInList());
		assert(!other->isInList());

		other->next = this->next;
		other->prev = this;
		next->prev = other;
		next = other;
	}

	ListItem* listGetNext()
	{
		return next;
	}

	
	ListItem* listGetPrev()
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

struct Chunk :public ListItem
{
	typedef size_t SizeT; //todo
//	static constexpr SizeT INUSE_FLAG = 1;
	SizeT size;
	//SizeT sizeBefore;
	uint8_t sizeIndex;

	//TODO consider merging all in a single byte
	uint8_t inUse;
//	uint8_t last;

	enum BucketKinds :uint8_t
	{
		Free = 0,
		NoBucket,
		SmallBucket,
		MediumBucket
	} bucketKind;


	void initialize(SizeT sz, uint8_t szIndex)
	{
		size = sz;
		prev = nullptr;
		next = nullptr;
		sizeIndex = szIndex;
		bucketKind = Free;
	}

	SizeT getSize() const
	{
		return size;
	}

	void setSize(SizeT sz, uint8_t szIx) 
	{
		size = sz;
		sizeIndex = szIx;
	}

	bool isFree() const {return bucketKind == Free;}
	void setFree() { bucketKind = Free;}
	void setInUse() { bucketKind = NoBucket;}


	BucketKinds getBucketKind() const { return bucketKind; }
	void setBucketKind(BucketKinds kind) { bucketKind = kind; }

	//bool noFlags() const { return isFree() && getBucketKind() == NoBucket; }
	//void clearFlags() { clearInUse(); setBucketKind(NoBucket); }

	uint8_t getSizeIndex() const { return sizeIndex; }

	//void updateAfterSize(size_t sz)
	//{
	//	uintptr_t ptr = reinterpret_cast<uintptr_t>(this) + sz;
	//	reinterpret_cast<Chunk*>(ptr)->setSizeBefore(sz);
	//}
	

	
	//Chunk* getBefore()
	//{
	//	uintptr_t ptr = reinterpret_cast<uintptr_t>(this) - sizeBefore;
	//	return sizeBefore != 0 ? reinterpret_cast<Chunk*>(ptr) : nullptr;
	//}

	//Chunk* getAfter()
	//{
	//	uintptr_t ptr = reinterpret_cast<uintptr_t>(this) + getSize();
	//	return reinterpret_cast<Chunk*>(ptr);
	//}

};


class ChunkList
{
private:
	uint32_t count = 0;
	ListItem lst;
public:
	
	ChunkList()
	{
		lst.listInitializeEmpty();
	}

	FORCE_INLINE
	bool empty() const { return count == 0; }
	FORCE_INLINE
		uint32_t size() const { return count; }
	FORCE_INLINE
		bool isEnd(ListItem* item) const { return item == &lst; }

	FORCE_INLINE
	ListItem* front()

	{
		return lst.listGetNext();
	}

	FORCE_INLINE
	void pushFront(ListItem* chk)
	{
		lst.listInsertNext(chk);
		++count;
	}

	FORCE_INLINE
	ListItem* popFront()
	{
		assert(!empty());

		ListItem* chk = lst.listGetNext();
		chk->removeFromList();
		--count;

		return chk;
	}

	FORCE_INLINE
		ListItem* popBack()
	{
		assert(!empty());

		ListItem* chk = lst.listGetPrev();
		chk->removeFromList();
		--count;

		return chk;
	}

	FORCE_INLINE
	void remove(ListItem* chk)
	{
		chk->removeFromList();
		--count;
	}

	size_t getCount() const { return count; }
};

struct BlockStats
{
	//alloc / dealloc ops
	unsigned long allocCount = 0;
	unsigned long allocSize = 0;
	unsigned long deallocCount = 0;
	unsigned long deallocSize = 0;

	void printStats()
	{
		printf("Allocs %lu (%lu), ", allocCount, allocSize);
		printf("Deallocs %lu (%lu), ", deallocCount, deallocSize);

		long ct = allocCount - deallocCount;
		long sz = allocSize - deallocSize;

		printf("Diff %ld (%ld)\n\n", ct, sz);
	}


	void allocate(size_t sz)
	{
		allocSize += sz;
		++allocCount;
	}
	void deallocate(size_t sz)
	{
		deallocSize += sz;
		++deallocCount;
	}
};

constexpr size_t max_cached_size = 256; // # of pages
constexpr size_t single_page_cache_size = 4;
constexpr size_t multi_page_cache_size = 2;


struct FreeChunks
{
//	Chunk* topChunk = nullptr;
	std::array<ChunkList, max_cached_size+1> freeBlocks;

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

	Chunk* getFreeBlock(size_t sz)
	{
		assert(isAlignedExp(sz, blockSizeExp));

		size_t ix = (sz >> blockSizeExp)-1;
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				Chunk* chk = static_cast<Chunk*>(freeBlocks[ix].popFront());
				chk->initialize(sz, ix);
//				assert(chk->isFree());
//				chk->setInUse();
				return chk;
			}
		}

		void* ptr = VirtualMemory::allocate(sz);
		stats.allocate(sz);
		if (ptr)
		{
			Chunk* chk = static_cast<Chunk*>(ptr);
			chk->initialize(sz, ix);
			return chk;
		}
		//todo enlarge top chunk

		throw std::bad_alloc();
	}


	void freeChunk(Chunk* chk)
	{
		assert(!chk->isFree());
		assert(!chk->isInList());

		size_t ix = chk->getSizeIndex();
		if ( ix == 0 ) // quite likely case (all bucket chunks)
		{
			if ( freeBlocks[ix].getCount() < single_page_cache_size )
			{
				chk->setFree();
				freeBlocks[ix].pushFront(chk);
				return;
			}
		}
		else if ( ix < max_cached_size && freeBlocks[ix].getCount() < multi_page_cache_size )
		{
			chk->setFree();
			freeBlocks[ix].pushFront(chk);
			return;
		}

		stats.deallocate(chk->getSize());
		VirtualMemory::deallocate(chk, chk->getSize());
	}

	void printStats()
	{
		stats.printStats();
	}
};


#endif //PAGE_ALLOCATOR_H
