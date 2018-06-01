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

#include <cstdlib>
#include <cstring>
#include <limits>

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


//#define USE_ITEM_HEADER
#define USE_SOUNDING_PAGE_ADDRESS

#ifdef USE_SOUNDING_PAGE_ADDRESS
template<class BasePageAllocator, size_t total_cnt_exp>
class SoundingAddressPageAllocator : public BasePageAllocator
{
	static constexpr size_t total_cnt = 1 << total_cnt_exp;

	struct MemoryBlockHeader
	{
		MemoryBlockListItem block;
		MemoryBlockHeader* next;
	};
	MemoryBlockHeader* memoryBlockHead = nullptr;
	
	struct PageBlockDescriptor
	{
		PageBlockDescriptor* next = nullptr;
		void* blockAddress = nullptr;
		size_t usageMask = 0;
		static_assert( sizeof( size_t ) * 8 >= total_cnt, "revise implementation" );
	};
	PageBlockDescriptor pageBlockListStart;
	PageBlockDescriptor* pageBlockListCurrent;
	PageBlockDescriptor* indexHead[total_cnt];

	void* getNextBlock()
	{
		void* pages = getFreeBlockNoCache( BLOCK_SIZE * total_cnt ); // TODO: replace by reservation, if possible
		MemoryBlockHeader* h = reinterpret_cast<MemoryBlockHeader*>( pages );
		h->next = memoryBlockHead;
		memoryBlockHead = h;
		return pages;
	}

	void* createNextBlockAndGetPage( size_t reasonIdx )
	{
		assert( reasonIdx < total_cnt );
		PageBlockDescriptor* pb = new PageBlockDescriptor; // TODO: consider using our own allocator
		pb->blockAddress = getNextBlock();
		pb->usageMask = ((size_t)1) << reasonIdx;
		pb->next = nullptr;
		pageBlockListCurrent->next = pb;
		pageBlockListCurrent = pb;
		return idxToPageAddr( pb->blockAddress, reasonIdx );
	}

public:
	static constexpr size_t reserverdSizeAtPageStart() { return sizeof( MemoryBlockHeader ); }

public:
//	SoundingAddressPageAllocator( BasePageAllocator& pageAllocator_ ) : pageAllocator( pageAllocator_ ) {}
	SoundingAddressPageAllocator() {}

	static FORCE_INLINE size_t addressToIdx( void* ptr ) { return ( (uintptr_t)(ptr) >> BLOCK_SIZE_EXP ) & ( total_cnt - 1 ); }
	static FORCE_INLINE void* idxToPageAddr( void* blockptr, size_t idx ) 
	{ 
		assert( idx < total_cnt );
		uintptr_t startAsIdx = addressToIdx( blockptr );
		void* ret = (void*)( ( ( ( (uintptr_t)(blockptr) >> ( BLOCK_SIZE_EXP + total_cnt_exp ) ) << total_cnt_exp ) + idx + (( idx < startAsIdx ) << total_cnt_exp) ) << BLOCK_SIZE_EXP );
		assert( addressToIdx( ret ) == idx );
		return ret;
	}
	static FORCE_INLINE size_t getOffsetInPage( void * ptr ) { return (uintptr_t)(ptr) & BLOCK_SIZE_MASK; }
	static FORCE_INLINE void* ptrToPageStart( void * ptr ) { return (void*)( ( (uintptr_t)(ptr) >> BLOCK_SIZE_EXP ) << BLOCK_SIZE_EXP ); }

	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );

		pageBlockListStart.blockAddress = nullptr;
		pageBlockListStart.usageMask = 0;
		pageBlockListStart.next = nullptr;

		pageBlockListCurrent = &pageBlockListStart;
		for ( size_t i=0; i<total_cnt; ++i )
			indexHead[i] = pageBlockListCurrent;
	}

	void* getPage( size_t idx )
	{
		assert( idx < total_cnt );
		assert( indexHead[idx] );
		if ( indexHead[idx]->next == nullptr )
		{
			assert( indexHead[idx] == pageBlockListCurrent );
			void* ret = createNextBlockAndGetPage( idx );
			indexHead[idx] = pageBlockListCurrent;
			assert( indexHead[idx]->next == nullptr );
			return ret;
		}
		else
		{
			indexHead[idx] = indexHead[idx]->next;
			assert( indexHead[idx]->blockAddress );
			assert( ( indexHead[idx]->usageMask & ( ((size_t)1) << idx ) ) == 0 );
			indexHead[idx]->usageMask |= ((size_t)1) << idx;
			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx );
			// TODO: commit page
			return ret;
		}
	}

	void freePage( MemoryBlockListItem* chk )
	{
		assert( false );
		// TODO: decommit
	}

	void deinitialize()
	{
		PageBlockDescriptor* next = pageBlockListStart.next;
		while( next )
		{
			assert( next->blockAddress );
			freeChunkNoCache( reinterpret_cast<MemoryBlockListItem*>( next->blockAddress ), BLOCK_SIZE * total_cnt );
			PageBlockDescriptor* tmp = next->next;
			delete next;
			next = tmp;
		}
		BasePageAllocator::deinitialize();
	}
};
#endif


class SerializableAllocatorBase
{
protected:
	static constexpr size_t MaxBucketSize = BLOCK_SIZE / 4;
	static constexpr size_t BucketCountExp = 4;
	static constexpr size_t BucketCount = 1 << BucketCountExp;
	void* buckets[BucketCount];
	static constexpr size_t large_block_idx = 0xFF;

	struct ChunkHeader
	{
		MemoryBlockListItem block;
		ChunkHeader* next;
		size_t idx;
	};
	
#ifdef USE_SOUNDING_PAGE_ADDRESS
	SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp> pageAllocator;
#else
	PageAllocatorWithCaching pageAllocator;

	ChunkHeader* nextPage = nullptr;

	FORCE_INLINE
	ChunkHeader* getChunkFromUsrPtr(void* ptr)
	{
		return reinterpret_cast<ChunkHeader*>(alignDownExp(reinterpret_cast<uintptr_t>(ptr), BLOCK_SIZE_EXP));
	}
#endif

#ifdef USE_ITEM_HEADER
	struct ItemHeader
	{
		uint8_t idx;
		uint8_t reserved[7];
	};
	static_assert( sizeof( ItemHeader ) == 8, "" );
#endif // USE_ITEM_HEADER

protected:
	static constexpr
	FORCE_INLINE size_t indexToBucketSize(uint8_t ix)
	{
		return 1ULL << (ix + 3);
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
#ifdef USE_SOUNDING_PAGE_ADDRESS
		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getPage( szidx ) );
		constexpr size_t memStart = alignUpExp( SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp>::reserverdSizeAtPageStart(), ALIGNMENT_EXP );
#else
		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getFreeBlock( BLOCK_SIZE ) );
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
		ChunkHeader* h = reinterpret_cast<ChunkHeader*>( block );
		h->idx = szidx;
		h->next = nextPage;
		nextPage = h;
#endif
		uint8_t* mem = block + memStart;
		size_t bucketSz = indexToBucketSize( szidx ); // TODO: rework
		assert( bucketSz >= sizeof( void* ) );
#ifdef USE_ITEM_HEADER
		bucketSz += sizeof(ItemHeader);
#endif // USE_ITEM_HEADER
		size_t itemCnt = (BLOCK_SIZE - memStart) / bucketSz;
		assert( itemCnt );
		for ( size_t i=bucketSz; i<(itemCnt-1)*bucketSz; i+=bucketSz )
			*reinterpret_cast<void**>(mem + i) = mem + i + bucketSz;
		*reinterpret_cast<void**>(mem + (itemCnt-1)*bucketSz) = nullptr;
		buckets[szidx] = mem + bucketSz;
#ifdef USE_ITEM_HEADER
		reinterpret_cast<ItemHeader*>( mem )->idx = szidx;
		return mem + sizeof( ItemHeader );
#else
		return mem;
#endif // USE_ITEM_HEADER
	}

	NOINLINE void* allocateInCaseTooLargeForBucket(size_t sz)
	{
#ifdef USE_ITEM_HEADER
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ) + sizeof( ItemHeader ), ALIGNMENT_EXP );
#elif defined USE_SOUNDING_PAGE_ADDRESS
		constexpr size_t memStart = alignUpExp( sizeof( size_t ), ALIGNMENT_EXP );
#else
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
#endif // USE_ITEM_HEADER
		size_t fullSz = alignUpExp( sz + memStart, BLOCK_SIZE_EXP );
		MemoryBlockListItem* block = pageAllocator.getFreeBlock( fullSz );

#ifdef USE_ITEM_HEADER
#else
		size_t* h = reinterpret_cast<size_t*>( block );
		*h = sz;
#endif

//		usedNonBuckets.pushFront(chk);
#ifdef USE_ITEM_HEADER
		( reinterpret_cast<ItemHeader*>( reinterpret_cast<uint8_t*>(block) + memStart ) - 1 )->idx = large_block_idx;
#endif // USE_ITEM_HEADER
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
#ifdef USE_ITEM_HEADER
				reinterpret_cast<ItemHeader*>( ret )->idx = szidx;
				return reinterpret_cast<uint8_t*>(ret) + sizeof( ItemHeader );
#else
				return ret;
#endif // USE_ITEM_HEADER
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
#ifdef USE_ITEM_HEADER
			ItemHeader* ih = reinterpret_cast<ItemHeader*>(ptr) - 1;
			if ( ih->idx != large_block_idx )
			{
				uint8_t idx = ih->idx;
				*reinterpret_cast<void**>( ih ) = buckets[idx];
				buckets[idx] = ih;
			}
			else
			{
				ChunkHeader* ch = getChunkFromUsrPtr( ptr );
//				assert( reinterpret_cast<uint8_t*>(ch) == reinterpret_cast<uint8_t*>(ih) );
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(ch) );
			}
#elif defined USE_SOUNDING_PAGE_ADDRESS
			size_t offsetInPage = SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp>::getOffsetInPage( ptr );
			if ( offsetInPage > sizeof( size_t ) )
			{
				uint8_t idx = SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp>::addressToIdx( ptr );
				*reinterpret_cast<void**>( ptr ) = buckets[idx];
				buckets[idx] = ptr;
			}
			else
			{
				void* pageStart = SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp>::ptrToPageStart( ptr );
				MemoryBlockListItem* h = reinterpret_cast<MemoryBlockListItem*>(pageStart);
				h->size = *reinterpret_cast<size_t*>(pageStart);
				h->sizeIndex = 0xFFFFFFFF; // TODO: address properly!!!
				h->prev = nullptr;
				h->next = nullptr;
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(h) );
			}
#else
			ChunkHeader* h = getChunkFromUsrPtr( ptr );
			if ( h->idx != large_block_idx )
			{
				*reinterpret_cast<void**>( ptr ) = buckets[h->idx];
				buckets[h->idx] = ptr;
			}
			else
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(h) );
#endif // USE_ITEM_HEADER
		}
	}
	
	const BlockStats& getStats() const { return pageAllocator.getStats(); }
	
	void printStats()
	{
		pageAllocator.printStats();
	}

	void initialize(size_t size)
	{
		initialize();
	}

	void initialize()
	{
		memset( buckets, 0, sizeof( void* ) * BucketCount );
		pageAllocator.initialize( BLOCK_SIZE_EXP );
	}

	void deinitialize()
	{
#ifdef USE_SOUNDING_PAGE_ADDRESS
		// ...
#else
		while ( nextPage )
		{
			ChunkHeader* next = nextPage->next;
			pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(nextPage) );
			nextPage = next;
		}
#endif // USE_SOUNDING_PAGE_ADDRESS
		pageAllocator.deinitialize();
	}

	~SerializableAllocatorBase()
	{
		deinitialize();
	}
};

#endif //SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H
