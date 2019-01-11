
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
 *     * Neither the name of the OLogN Technologies AG nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL OLogN Technologies AG BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. * 
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

#include "iibmalloc_common.h"

namespace nodecpp::iibmalloc
{

#define GET_PERF_DATA

#ifdef GET_PERF_DATA
#ifdef NODECPP_MSVC
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif // GET_PERF_DATA


/* OS specific implementations */
class VirtualMemory
{
public:
	static size_t getPageSize();
	static size_t getAllocGranularity();

	static unsigned char* reserve(void* addr, size_t size);
	static void commit(uintptr_t addr, size_t size); // TODO: revise necessity (duplicates might beavailable)
	static void decommit(uintptr_t addr, size_t size); // TODO: revise necessity (duplicates might be available)

	static void* allocate(size_t size);
	static void deallocate(void* ptr, size_t size);

	static void* AllocateAddressSpace(size_t size);
	static void* CommitMemory(void* addr, size_t size);
	static void DecommitMemory(void* addr, size_t size);
	static void FreeAddressSpace(void* addr, size_t size);
};

struct MemoryBlockListItem
{
	MemoryBlockListItem* next;
	MemoryBlockListItem* prev;
	typedef size_t SizeT; //todo
	SizeT size;
	SizeT sizeIndex;

	void initialize(SizeT sz, SizeT szIndex)
	{
		size = sz;
		prev = nullptr;
		next = nullptr;
		sizeIndex = szIndex;
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
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, isInList() );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !other->isInList() );

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
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev != nullptr);
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next != nullptr);
			return true;
		}
	}

	void removeFromList()
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev != nullptr);
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next != nullptr);

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
		initialize();
	}

	void initialize()
	{
		count = 0;
		lst.listInitializeEmpty();
	}

	NODECPP_FORCEINLINE
	bool empty() const { return count == 0; }
	NODECPP_FORCEINLINE
		uint32_t size() const { return count; }
	NODECPP_FORCEINLINE
		bool isEnd(MemoryBlockListItem* item) const { return item == &lst; }

	NODECPP_FORCEINLINE
	MemoryBlockListItem* front()

	{
		return lst.listGetNext();
	}

	NODECPP_FORCEINLINE
	void pushFront(MemoryBlockListItem* chk)
	{
		lst.listInsertNext(chk);
		++count;
	}

	NODECPP_FORCEINLINE
	MemoryBlockListItem* popFront()
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !empty());

		MemoryBlockListItem* chk = lst.listGetNext();
		chk->removeFromList();
		--count;

		return chk;
	}

	NODECPP_FORCEINLINE
		MemoryBlockListItem* popBack()
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !empty());

		MemoryBlockListItem* chk = lst.listGetPrev();
		chk->removeFromList();
		--count;

		return chk;
	}

	NODECPP_FORCEINLINE
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

	uint64_t allocRequestCount = 0;
	uint64_t allocRequestSize = 0;

	uint64_t deallocRequestCount = 0;
	uint64_t deallocRequestSize = 0;

	void printStats() const
	{
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "Allocs {} ({}), ", sysAllocCount, sysAllocSize);
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "Deallocs {} ({}), ", sysDeallocCount, sysDeallocSize);

		uint64_t ct = sysAllocCount - sysDeallocCount;
		uint64_t sz = sysAllocSize - sysDeallocSize;

		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "Diff {} ({})\n", ct, sz);
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
	BlockStats stats;
	uint8_t blockSizeExp = 0;

public:

	void initialize(uint8_t blockSizeExp)
	{
		this->blockSizeExp = blockSizeExp;
	}

	MemoryBlockListItem* getFreeBlock(size_t sz)
	{
		stats.registerAllocRequest( sz );

		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, isAlignedExp(sz, blockSizeExp));

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


	void freeChunk( MemoryBlockListItem* chk )
	{
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

	void printStats()
	{
		stats.printStats();
	}

	void* AllocateAddressSpace(size_t size)
	{
		return VirtualMemory::AllocateAddressSpace( size );
	}
	void* CommitMemory(void* addr, size_t size)
	{
		return VirtualMemory::CommitMemory( addr, size);
	}
	void DecommitMemory(void* addr, size_t size)
	{
		VirtualMemory::DecommitMemory( addr, size );
	}
	void FreeAddressSpace(void* addr, size_t size)
	{
		VirtualMemory::FreeAddressSpace( addr, size );
	}
};


constexpr size_t max_cached_size = 20; // # of pages
constexpr size_t single_page_cache_size = 32;
constexpr size_t multi_page_cache_size = 4;

struct PageAllocatorWithCaching // to be further developed for practical purposes
{
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
		for ( size_t ix=0; ix<=max_cached_size; ++ ix )
			freeBlocks[ix].initialize();
	}

	void deinitialize()
	{
		for ( size_t ix=0; ix<=max_cached_size; ++ ix )
		{
			while ( !freeBlocks[ix].empty() )
			{
				MemoryBlockListItem* chk = static_cast<MemoryBlockListItem*>(freeBlocks[ix].popFront());
				size_t sz = chk->getSize();
				uint64_t start = __rdtsc();
				VirtualMemory::deallocate(chk, sz );
				uint64_t end = __rdtsc();
				stats.registerSysDealloc( sz, end - start );
			}
		}
	}

	MemoryBlockListItem* getFreeBlock(size_t sz)
	{
		stats.registerAllocRequest( sz );

		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, isAlignedExp(sz, blockSizeExp));

		size_t ix = (sz >> blockSizeExp)-1;
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				MemoryBlockListItem* chk = static_cast<MemoryBlockListItem*>(freeBlocks[ix].popFront());
				chk->initialize(sz, ix);
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

	void* getFreeBlockNoCache(size_t sz)
	{
		stats.registerAllocRequest( sz );

		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, isAlignedExp(sz, blockSizeExp));

		uint64_t start = __rdtsc();
		void* ptr = VirtualMemory::allocate(sz);
		uint64_t end = __rdtsc();
		stats.registerSysAlloc( sz, end - start );

		if (ptr)
			return ptr;

		throw std::bad_alloc();
	}
	
	void freeChunk( MemoryBlockListItem* chk )
	{
		size_t sz = chk->getSize();
		stats.registerDeallocRequest( sz );
		size_t ix = (sz >> blockSizeExp)-1;
		if ( ix == 0 ) // quite likely case (all bucket chunks)
		{
			if ( freeBlocks[ix].getCount() < single_page_cache_size )
			{
				freeBlocks[ix].pushFront(chk);
				return;
			}
		}
		else if ( ix < max_cached_size && freeBlocks[ix].getCount() < multi_page_cache_size )
		{
			freeBlocks[ix].pushFront(chk);
			return;
		}

		uint64_t start = __rdtsc();
		VirtualMemory::deallocate(chk, sz );
		uint64_t end = __rdtsc();
		stats.registerSysDealloc( sz, end - start );
	}

	void freeChunkNoCache( void* block, size_t sz )
	{
		stats.registerDeallocRequest( sz );

		uint64_t start = __rdtsc();
		VirtualMemory::deallocate( block, sz );
		uint64_t end = __rdtsc();
		stats.registerSysDealloc( sz, end - start );
	}

	const BlockStats& getStats() const { return stats; }

	void printStats() const
	{
		stats.printStats();
	}

	void* AllocateAddressSpace(size_t size)
	{
		return VirtualMemory::AllocateAddressSpace( size );
	}
	void* CommitMemory(void* addr, size_t size)
	{
		stats.registerAllocRequest( size );
		void* ret = VirtualMemory::CommitMemory( addr, size);
		if (ret == (void*)(-1))
		{
			nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "Committing failed at {} ({:x}) (0x{:x} bytes in total)", stats.allocRequestCount, stats.allocRequestCount, stats.allocRequestSize );
		}
		return ret;
	}
	void DecommitMemory(void* addr, size_t size)
	{
		VirtualMemory::DecommitMemory( addr, size );
	}
	void FreeAddressSpace(void* addr, size_t size)
	{
		VirtualMemory::FreeAddressSpace( addr, size );
	}
};

} // namespace nodecpp::iibmalloc

#endif //PAGE_ALLOCATOR_H
