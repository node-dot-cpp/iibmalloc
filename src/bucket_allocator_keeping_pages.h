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
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H
#define SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H

#define NOMINMAX


#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>

#include "bucket_allocator_common.h"
#include "page_allocator.h"



class SerializableAllocatorBase;
extern thread_local SerializableAllocatorBase g_AllocManager;



constexpr size_t ALIGNMENT = 2 * sizeof(uint64_t);
constexpr uint8_t ALIGNMENT_EXP = sizeToExp(ALIGNMENT);
static_assert( ( 1 << ALIGNMENT_EXP ) == ALIGNMENT, "" );
constexpr size_t ALIGNMENT_MASK = expToMask(ALIGNMENT_EXP);
static_assert( 1 + ALIGNMENT_MASK == ALIGNMENT, "" );

constexpr size_t BLOCK_SIZE = 4 * 1024;
constexpr uint8_t BLOCK_SIZE_EXP = sizeToExp(BLOCK_SIZE);
constexpr size_t BLOCK_SIZE_MASK = expToMask(BLOCK_SIZE_EXP);
static_assert( ( 1 << BLOCK_SIZE_EXP ) == BLOCK_SIZE, "" );
static_assert( 1 + BLOCK_SIZE_MASK == BLOCK_SIZE, "" );

constexpr size_t MAX_BUCKET_SIZE = 1 * 1024;
constexpr uint8_t MAX_BUCKET_SIZE_EXP = sizeToExp(MAX_BUCKET_SIZE);
constexpr size_t MAX_BUCKET_SIZE_MASK = expToMask(MAX_BUCKET_SIZE_EXP);
static_assert( ( 1 << MAX_BUCKET_SIZE_EXP ) == MAX_BUCKET_SIZE, "" );
static_assert( 1 + MAX_BUCKET_SIZE_MASK == MAX_BUCKET_SIZE, "" );


constexpr size_t KEEP_EMPTY_BUCKETS = 4;
constexpr size_t KEEP_FREE_CHUNKS = 100;
constexpr size_t COMMIT_CHUNK_MULTIPLIER = 1;
static_assert(COMMIT_CHUNK_MULTIPLIER >= 1, "");

constexpr size_t FIRST_BUCKET_ALIGNMENT = 64;
constexpr uint8_t FIRST_BUCKET_ALIGNMENT_EXP = sizeToExp(FIRST_BUCKET_ALIGNMENT);



static constexpr size_t MAX_SMALL_BUCKET_SIZE = 32;


class SerializableAllocatorBase
{
protected:
	PageAllocator pageAllocator;

	static constexpr size_t MaxBucketSize = BLOCK_SIZE / 2;
	static constexpr size_t BucketCount = 16;
	void* buckets[BucketCount];
	static constexpr size_t large_block_idx = 0xFF;

	struct ChunkHeader
	{
		MemoryBlockListItem block;
		size_t idx;
		ChunkHeader* next;
	};
	
	ChunkHeader* nextPage = nullptr;

protected:
	static constexpr
	FORCE_INLINE size_t indexToBucketSize(uint8_t ix)
	{
		return 1ULL << (ix + 3);
	}

	FORCE_INLINE
	ChunkHeader* getChunkFromUsrPtr(void* ptr)
	{
		return reinterpret_cast<ChunkHeader*>(alignDownExp(reinterpret_cast<uintptr_t>(ptr), BLOCK_SIZE_EXP));
	}

#if defined(_MSC_VER)
#if defined(_M_IX86)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint32_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
#elif defined(_M_X64)
	static
	FORCE_INLINE uint8_t sizeToIndex(uint64_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
#else
#error Unknown 32/64 bits architecture
#endif

#elif defined(__GNUC__)
#if defined(__i386__)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint32_t sz)
	{
		uint32_t ix = __builtin_clzl(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(29ul - ix);
	}
#elif defined(__x86_64__)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint64_t sz)
	{
		uint64_t ix = __builtin_clzll(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
	}
#else
#error Unknown 32/64 bits architecture
#endif	

#else
#error Unknown compiler
#endif
	
public:
	SerializableAllocatorBase() { initialize(); }
	SerializableAllocatorBase(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase(SerializableAllocatorBase&&) = default;
	SerializableAllocatorBase& operator=(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase& operator=(SerializableAllocatorBase&&) = default;

	void enable() {}
	void disable() {}


	FORCE_INLINE void* allocateInCaseNoFreeBucket( size_t sz, uint8_t szidx )
	{
		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getFreeBlock( BLOCK_SIZE ) );
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
		ChunkHeader* h = reinterpret_cast<ChunkHeader*>( block );
		h->idx = szidx;
		h->next = nextPage;
		nextPage = h;
		uint8_t* mem = block + memStart;
		size_t bucketSz = indexToBucketSize( szidx ); // TODO: rework
		assert( bucketSz >= sizeof( void* ) );
		size_t itemCnt = (BLOCK_SIZE - memStart) / bucketSz;
		assert( itemCnt );
		for ( size_t i=bucketSz; i<(itemCnt-1)*bucketSz; i+=bucketSz )
			*reinterpret_cast<void**>(mem + i) = mem + i + bucketSz;
		*reinterpret_cast<void**>(mem + (itemCnt-1)*bucketSz) = nullptr;
		buckets[szidx] = mem + bucketSz;
		return mem;
	}

	NOINLINE void* allocateInCaseTooLargeForBucket(size_t sz)
	{
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
		size_t fullSz = alignUpExp( sz + memStart, BLOCK_SIZE_EXP );
		MemoryBlockListItem* block = pageAllocator.getFreeBlock( fullSz );
		ChunkHeader* h = reinterpret_cast<ChunkHeader*>( block );
		h->idx = large_block_idx;

//		usedNonBuckets.pushFront(chk);
		return reinterpret_cast<uint8_t*>(block) + memStart;
	}

	FORCE_INLINE void* allocate(size_t sz)
	{
		if ( sz <= MaxBucketSize )
		{
			uint8_t szidx = sizeToIndex( sz );
			assert( szidx < BucketCount );
			if ( buckets[szidx] )
			{
				void* ret = buckets[szidx];
				buckets[szidx] = *reinterpret_cast<void**>(buckets[szidx]);
				return ret;
			}
			else
				return allocateInCaseNoFreeBucket( sz, szidx );
		}
		else
			return allocateInCaseTooLargeForBucket( sz );

		return nullptr;
	}

	FORCE_INLINE void deallocate(void* ptr)
	{
		if(ptr)
		{
			ChunkHeader* h = getChunkFromUsrPtr( ptr );
			if ( h->idx != large_block_idx )
			{
				*reinterpret_cast<void**>( ptr ) = buckets[h->idx];
				buckets[h->idx] = ptr;
			}
			else
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(h) );
		}
	}
	
	const BlockStats& getStats() const { return pageAllocator.getStats(); }
	
	void printStats()
	{
//		heap.printStats();
	}

	void initialize(size_t size)
	{
		initialize();
	}

	void initialize()
	{
		memset( buckets, 0, sizeof( void* ) * BucketCount );
	}

	void deinitialize()
	{
		while ( nextPage )
		{
			ChunkHeader* next = nextPage->next;
			pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(nextPage) );
			nextPage = next;
		}
	}

	~SerializableAllocatorBase()
	{
		deinitialize();
	}
};

#endif //SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H
