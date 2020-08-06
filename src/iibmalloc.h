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
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * -------------------------------------------------------------------------------
 * 
 * iibmalloc allocator
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef IIBMALLOC_H
#define IIBMALLOC_H


#include "iibmalloc_common.h"
#include "page_management.h"

#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
#include <map>
#endif


namespace nodecpp::iibmalloc
{

constexpr size_t ALIGNMENT = 2 * sizeof(uint64_t);
constexpr uint8_t ALIGNMENT_EXP = sizeToExp(ALIGNMENT);
static_assert( ( 1 << ALIGNMENT_EXP ) == ALIGNMENT, "" );
constexpr size_t ALIGNMENT_MASK = expToMask(ALIGNMENT_EXP);
static_assert( 1 + ALIGNMENT_MASK == ALIGNMENT, "" );

constexpr size_t PAGE_SIZE_BYTES = 4 * 1024;
constexpr uint8_t PAGE_SIZE_EXP = sizeToExp(PAGE_SIZE_BYTES);
constexpr size_t PAGE_SIZE_MASK = expToMask(PAGE_SIZE_EXP);
static_assert( ( 1 << PAGE_SIZE_EXP ) == PAGE_SIZE_BYTES, "" );
static_assert( 1 + PAGE_SIZE_MASK == PAGE_SIZE_BYTES, "" );


template<class BasePageAllocator, class ItemT>
class CollectionInPages : public BasePageAllocator
{
	struct ListItem
	{
		ItemT item;
		ListItem* next;
	};
	ListItem* head;
	ListItem* freeList;
	size_t pageCnt;
	void collectPageStarts( ListItem** pageStartHead, ListItem* fromList )
	{
		ListItem* curr = fromList;
		while (curr)
		{
			if ( ((uintptr_t)curr & PAGE_SIZE_MASK) == 0 )
			{
				ListItem* tmp = curr->next;
				curr->next = *pageStartHead;
				*pageStartHead = curr;
				curr = tmp;
			}
			else
				curr = curr->next;
		}
	}
public:
	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );
		head = nullptr;
		freeList = nullptr;
		pageCnt = 0;
	}
	ItemT* createNew()
	{
		if ( freeList == nullptr )
		{
			freeList = reinterpret_cast<ListItem*>( this->getFreeBlockNoCache( PAGE_SIZE_BYTES ) );
			++pageCnt;
			ListItem* item = freeList;
			size_t itemCnt = PAGE_SIZE_BYTES / sizeof( ListItem );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, itemCnt != 0 );
			for ( size_t i=0; i<itemCnt-1; ++i )
			{
				item->next = item + 1;
				item = item->next;
			}
			item->next = nullptr;
		}
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeList != nullptr );
		ListItem* tmp = head;
		ListItem* nextFree = freeList->next;
		head = freeList;
		head->next = tmp;
		freeList = nextFree;
//		freeList->next = head;
//		head->next = freeList;
		return &(head->item);
	}
	template<class Functor>
	void doForEach(Functor& f)
	{
		ListItem* curr = head;
		while (curr)
		{
			f.f( curr->item );
			curr = curr->next;
		}
	}
	void deinitialize()
	{
		ListItem* pageStartHead = nullptr;
		collectPageStarts( &pageStartHead, head );
		collectPageStarts( &pageStartHead, freeList );
		while (pageStartHead)
		{
			ListItem* tmp = pageStartHead->next;
			this->freeChunkNoCache( pageStartHead, PAGE_SIZE_BYTES );
			--pageCnt;
			pageStartHead = tmp;
		}
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, pageCnt == 0 );
		head = nullptr;
		freeList = nullptr;
	}
};

template<class BasePageAllocator, size_t bucket_cnt_exp, size_t reservation_size_exp, size_t commit_page_cnt_exp, size_t multipage_page_cnt_exp>
class SoundingAddressPageAllocator : public BasePageAllocator
{
	static constexpr size_t reservation_size = (1 << reservation_size_exp);
	static constexpr size_t bucket_cnt = (1 << bucket_cnt_exp);
	static_assert( reservation_size_exp >= bucket_cnt_exp + PAGE_SIZE_EXP, "revise implementation" );
	static_assert( commit_page_cnt_exp >= multipage_page_cnt_exp );
	static constexpr size_t multipage_page_cnt = 1 << multipage_page_cnt_exp;
	static constexpr size_t pages_per_bucket_exp = reservation_size_exp - bucket_cnt_exp - PAGE_SIZE_EXP;
	static constexpr size_t pages_in_single_commit_exp = (pages_per_bucket_exp >= 1 ? pages_per_bucket_exp - 1 : 0);
	static constexpr size_t pages_in_single_commit = (1 << pages_in_single_commit_exp);
	static constexpr size_t pages_per_bucket = (1 << pages_per_bucket_exp);
	static constexpr size_t commit_page_cnt = (1 << commit_page_cnt_exp);
	static constexpr size_t commit_size = (1 << (commit_page_cnt_exp + PAGE_SIZE_EXP));
	static_assert( commit_page_cnt_exp <= reservation_size_exp - bucket_cnt_exp - PAGE_SIZE_EXP, "value mismatch" );

	struct MemoryBlockHeader
	{
		MemoryBlockListItem block;
		MemoryBlockHeader* next;
	};
	
	struct PageBlockDescriptor
	{
		PageBlockDescriptor* next = nullptr;
		void* blockAddress = nullptr;
		uint16_t nextToUse[ bucket_cnt ];
		uint16_t nextToCommit[ bucket_cnt ];
		static_assert( UINT16_MAX > pages_per_bucket , "revise implementation" );
	};
	CollectionInPages<BasePageAllocator,PageBlockDescriptor> pageBlockDescriptors;
	PageBlockDescriptor pageBlockListStart;
	PageBlockDescriptor* pageBlockListCurrent;
	PageBlockDescriptor* indexHead[bucket_cnt];

	void* getNextBlock()
	{
		void* pages = this->AllocateAddressSpace( reservation_size );
		return pages;
	}

	void* createNextBlockAndGetPage( size_t reasonIdx )
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, reasonIdx < bucket_cnt );
//		PageBlockDescriptor* pb = new PageBlockDescriptor; // TODO: consider using our own allocator
		PageBlockDescriptor* pb = pageBlockDescriptors.createNew();
		pb->blockAddress = getNextBlock();
//nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "createNextBlockAndGetPage(): descriptor allocated at 0x{:x}; block = 0x{:x}", (size_t)(pb), (size_t)(pb->blockAddress) );
		memset( pb->nextToUse, 0, sizeof( uint16_t) * bucket_cnt );
		memset( pb->nextToCommit, 0, sizeof( uint16_t) * bucket_cnt );
		pb->next = nullptr;
		pageBlockListCurrent->next = pb;
		pageBlockListCurrent = pb;
//		void* ret = idxToPageAddr( pb->blockAddress, reasonIdx );
		void* ret = idxToPageAddr( pb->blockAddress, reasonIdx, 0 );
//	nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "createNextBlockAndGetPage(): before commit, {}, 0x{:x} -> 0x{:x}", reasonIdx, (size_t)(pb->blockAddress), (size_t)(ret) );
//		void* ret2 = this->CommitMemory( ret, PAGE_SIZE_BYTES );
//		this->CommitMemory( ret, PAGE_SIZE_BYTES );
		commitRangeOfPageIndexes( pb->blockAddress, reasonIdx, 0, commit_page_cnt );
		pb->nextToUse[ reasonIdx ] = 1;
		static_assert( commit_page_cnt <= UINT16_MAX, "" );
		pb->nextToCommit[ reasonIdx ] = (uint16_t)commit_page_cnt;
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
//	nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "createNextBlockAndGetPage(): after commit 0x{:x}", (size_t)(ret2) );
		return ret;
	}

	void resetLists()
	{
		pageBlockListStart.blockAddress = nullptr;
		for ( size_t i=0; i<bucket_cnt; ++i )
			pageBlockListStart.nextToUse[i] = pages_per_bucket; // thus triggering switching to a next block whatever bucket is selected
		for ( size_t i=0; i<bucket_cnt; ++i )
			pageBlockListStart.nextToCommit[i] = pages_per_bucket; // thus triggering switching to a next block whatever bucket is selected
		pageBlockListStart.next = nullptr;

		pageBlockListCurrent = &pageBlockListStart;
		for ( size_t i=0; i<bucket_cnt; ++i )
			indexHead[i] = pageBlockListCurrent;
	}

public:
//	static constexpr size_t reservedSizeAtPageStart() { return sizeof( MemoryBlockHeader ); }

	struct MultipageData
	{
		void* ptr1;
		size_t sz1;
		void* ptr2;
		size_t sz2;
	};

public:
	SoundingAddressPageAllocator() {}

	static NODECPP_FORCEINLINE size_t addressToIdx( void* ptr ) 
	{ 
		// TODO: make sure computations are optimal
		uintptr_t padr = (uintptr_t)(ptr) >> PAGE_SIZE_EXP;
		constexpr uintptr_t meaningfulBitsMask = ( 1 << (bucket_cnt_exp + pages_per_bucket_exp) ) - 1;
		uintptr_t meaningfulBits = padr & meaningfulBitsMask;
		return meaningfulBits >> pages_per_bucket_exp;
	}
	static NODECPP_FORCEINLINE void* idxToPageAddr( void* blockptr, size_t idx, size_t pagesUsed ) 
	{ 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, idx < bucket_cnt );
		uintptr_t startAsIdx = addressToIdx( blockptr );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(blockptr) & PAGE_SIZE_MASK ) == 0 );
		uintptr_t startingPage =  (uintptr_t)(blockptr) >> PAGE_SIZE_EXP;
		uintptr_t basePage =  ( startingPage >> (reservation_size_exp - PAGE_SIZE_EXP) ) << (reservation_size_exp - PAGE_SIZE_EXP);
		uintptr_t baseOffset = startingPage - basePage;
		bool below = (idx << pages_per_bucket_exp) + pagesUsed < baseOffset;
		uintptr_t ret = basePage + (idx << pages_per_bucket_exp) + pagesUsed + (below << (pages_per_bucket_exp + bucket_cnt_exp));
		ret <<= PAGE_SIZE_EXP;
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, addressToIdx( (void*)( ret ) ) == idx );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, (uint8_t*)blockptr <= (uint8_t*)ret && (uint8_t*)ret < (uint8_t*)blockptr + reservation_size );
		return (void*)( ret );
	}
	static NODECPP_FORCEINLINE size_t getOffsetInPage( void * ptr ) { return (uintptr_t)(ptr) & PAGE_SIZE_MASK; }
	static NODECPP_FORCEINLINE void* ptrToPageStart( void * ptr ) { return (void*)( ( (uintptr_t)(ptr) >> PAGE_SIZE_EXP ) << PAGE_SIZE_EXP ); }

	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );
		pageBlockDescriptors.initialize(PAGE_SIZE_EXP);
		resetLists();
	}

	void commitRangeOfPageIndexes( void* blockptr, size_t bucketIdx, size_t pageIdx, size_t rangeSize )
	{
		uint8_t* start = reinterpret_cast<uint8_t*>( idxToPageAddr( blockptr, bucketIdx, pageIdx ) );
		uint8_t* prevNext = start;
		uint8_t* next;
		for ( size_t i=1; i<rangeSize; ++i )
		{
			next = reinterpret_cast<uint8_t*>( idxToPageAddr( blockptr, bucketIdx, pageIdx + i ) );
			if ( next - prevNext == PAGE_SIZE_BYTES )
			{
				prevNext = next;
				continue;
			}
			else
			{
				this->CommitMemory( start, prevNext - start + PAGE_SIZE_BYTES );
				start = next;
				prevNext = next;
			}
		}
		this->CommitMemory( start, prevNext - start + PAGE_SIZE_BYTES );
	}

	void* getPage( size_t idx )
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, idx < bucket_cnt );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx] );
		if ( indexHead[idx]->nextToUse[idx] < pages_per_bucket )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
			if ( indexHead[idx]->nextToUse[idx] == indexHead[idx]->nextToCommit[idx] )
			{
				commitRangeOfPageIndexes( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToCommit[idx], commit_page_cnt );
				indexHead[idx]->nextToCommit[ idx ] += commit_page_cnt;
			}
			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToUse[idx] );
			++(indexHead[idx]->nextToUse[idx]);
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
//			this->CommitMemory( ret, PAGE_SIZE_BYTES );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
		else if ( indexHead[idx]->next == nullptr ) // next block is to be created
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx] == pageBlockListCurrent );
			void* ret = createNextBlockAndGetPage( idx );
			indexHead[idx] = pageBlockListCurrent;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->next == nullptr );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
		else // next block is just to be used first time
		{
			indexHead[idx] = indexHead[idx]->next;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->blockAddress );
//			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( indexHead[idx]->usageMask & ( ((size_t)1) << idx ) ) == 0 );
//			indexHead[idx]->usageMask |= ((size_t)1) << idx;
//			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->nextToUse[idx] == 0 );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->nextToCommit[idx] == 0 );
			if ( indexHead[idx]->nextToUse[idx] == indexHead[idx]->nextToCommit[idx] )
			commitRangeOfPageIndexes( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToCommit[idx], commit_page_cnt );
			indexHead[idx]->nextToCommit[idx] = commit_page_cnt;
			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToUse[idx] );
			indexHead[idx]->nextToUse[idx] = 1;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
//	nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "getPage(): before commit, {}, 0x{:x} -> 0x{:x}", idx, (size_t)(indexHead[idx]->blockAddress), (size_t)(ret) );
//			void* ret2 = this->CommitMemory( ret, PAGE_SIZE_BYTES );
//	nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "getPage(): after commit 0x{:x}", (size_t)(ret2) );
//			this->CommitMemory( ret, PAGE_SIZE_BYTES );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
	}

	void getMultipage( size_t idx, MultipageData& mpData )
	{
		// NOTE: current implementation just sits over repeated calls to getPage()
		//       it is reasonably assumed that returned pages are within at most two connected segments
		// TODO: it's possible to make it more optimal just by writing fram scratches by analogy with getPage() and calls from it
		mpData.ptr1 = getPage( idx );
		if constexpr ( multipage_page_cnt == 1 )
		{
			asserrt( mpData.ptr1 );
			mpData.sz1 = PAGE_SIZE_BYTES;
			mpData.ptr2 = nullptr;
			mpData.sz2 = 0;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, mpData.sz1 + mpData.sz2 == ( multipage_page_cnt << PAGE_SIZE_EXP ) );
			return;
		}

		void* nextPage;
		size_t i=1;
		mpData.sz1 = PAGE_SIZE_BYTES;
		for ( ; i<multipage_page_cnt; ++i )
		{
			nextPage = getPage( idx );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, nextPage );
			if ( reinterpret_cast<uint8_t*>(mpData.ptr1) + mpData.sz1 == reinterpret_cast<uint8_t*>(nextPage) )
				mpData.sz1 += PAGE_SIZE_BYTES;
			else break;
		}
		if ( i == multipage_page_cnt )
		{
			mpData.ptr2 = nullptr;
			mpData.sz2 = 0;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, mpData.sz1 + mpData.sz2 == ( multipage_page_cnt << PAGE_SIZE_EXP ) );
			return;
		}
		mpData.ptr2 = nextPage;
		mpData.sz2 = PAGE_SIZE_BYTES;
		++i;
		for ( ; i<multipage_page_cnt; ++i )
		{
			nextPage = getPage( idx );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, nextPage );
			if ( reinterpret_cast<uint8_t*>(mpData.ptr2) + mpData.sz2 == reinterpret_cast<uint8_t*>(nextPage) )
				mpData.sz2 += PAGE_SIZE_BYTES;
			else break;
		}
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, i == multipage_page_cnt );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, mpData.sz1 + mpData.sz2 == ( multipage_page_cnt << PAGE_SIZE_EXP ) );
	}

	void freePage( MemoryBlockListItem* chk )
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false );
		// TODO: decommit
	}

	void deinitialize()
	{
		PageBlockDescriptor* next = pageBlockListStart.next;
		while( next )
		{
//nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "in block 0x{:x} about to delete 0x{:x} of size 0x{:x}", (size_t)( next ), (size_t)( next->blockAddress ), PAGE_SIZE_BYTES * bucket_cnt );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next->blockAddress );
			this->freeChunkNoCache( reinterpret_cast<MemoryBlockListItem*>( next->blockAddress ), reservation_size );
			PageBlockDescriptor* tmp = next->next;
//			delete next;
			next = tmp;
		}
//		class F { private: BasePageAllocator* alloc; public: F(BasePageAllocator*alloc_) {alloc = alloc_;} void f(PageBlockDescriptor& h) {NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h.blockAddress != nullptr ); alloc->freeChunkNoCache( h.blockAddress, reservation_size ); } }; F f(this);
//		pageBlockDescriptors.doForEach(f);
		pageBlockDescriptors.deinitialize();
		resetLists();
		BasePageAllocator::deinitialize();
	}
};


//#define BULKALLOCATOR_HEAVY_DEBUG

template<class BasePageAllocator, size_t commited_block_size, uint16_t max_pages>
class BulkAllocator : public BasePageAllocator
{
	static_assert( ( commited_block_size >> PAGE_SIZE_EXP ) > 0 );
	static_assert( ( commited_block_size & PAGE_SIZE_MASK ) == 0 );
	static_assert( max_pages < PAGE_SIZE_BYTES );
	static constexpr size_t pagesPerAllocatedBlock = commited_block_size >> PAGE_SIZE_EXP;

public:
	struct AnyChunkHeader
	{
	private:
		uintptr_t prev;
		uintptr_t next;
	public:
		AnyChunkHeader* prevInBlock() {return (AnyChunkHeader*)( prev & ~((uintptr_t)(PAGE_SIZE_MASK)) ); }
		const AnyChunkHeader* prevInBlock() const {return (const AnyChunkHeader*)( prev & ~((uintptr_t)(PAGE_SIZE_MASK)) ); }
		AnyChunkHeader* nextInBlock() {return (AnyChunkHeader*)( next & ~((uintptr_t)(PAGE_SIZE_MASK) ) ); }
		const AnyChunkHeader* nextInBlock() const {return (const AnyChunkHeader*)( next & ~((uintptr_t)(PAGE_SIZE_MASK) ) ); }
		void setPrevInBlock( AnyChunkHeader* prev_ ) { NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ((uintptr_t)prev_ & PAGE_SIZE_MASK) == 0 ); prev = ( (uintptr_t)prev_ & ~(uintptr_t)(PAGE_SIZE_MASK) ) + (prev & ((uintptr_t)(PAGE_SIZE_MASK))); }
		uint16_t getPageCount() const { return prev & ((uintptr_t)(PAGE_SIZE_MASK)); }
		bool isFree() const { return next & ((uintptr_t)(PAGE_SIZE_MASK)); }
		void set( AnyChunkHeader* prevInBlock_, AnyChunkHeader* nextInBlock_, uint16_t pageCount, bool isFree )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ((uintptr_t)prevInBlock_ & PAGE_SIZE_MASK) == 0 );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ((uintptr_t)nextInBlock_ & PAGE_SIZE_MASK) == 0 );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, pageCount <= (commited_block_size>>PAGE_SIZE_EXP) );
			prev = ((uintptr_t)prevInBlock_) + pageCount;
			next = ((uintptr_t)nextInBlock_) + isFree;
		}
	};

	constexpr size_t maxAllocatableSize() {return ((size_t)max_pages) << PAGE_SIZE_EXP; }
	static constexpr size_t reservedSizeAtPageStart() { return std::max( sizeof( AnyChunkHeader ), (size_t)(NODECPP_GUARANTEED_IIBMALLOC_ALIGNMENT) ); }

private:
//	std::vector<AnyChunkHeader*> blockList;
	CollectionInPages<BasePageAllocator,AnyChunkHeader*> blocks;

	struct FreeChunkHeader : public AnyChunkHeader
	{
		FreeChunkHeader* prevFree;
		FreeChunkHeader* nextFree;
	};
	FreeChunkHeader* freeListBegin[ max_pages + 1 ];

	void removeFromFreeList( FreeChunkHeader* item )
	{
		if ( item->prevFree )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, item->getPageCount() == item->prevFree->getPageCount() || ( item->getPageCount() > max_pages && item->prevFree->getPageCount() > max_pages ));
			item->prevFree->nextFree = item->nextFree;
		}
		else
		{
			uint16_t idx = item->getPageCount() - 1;
			if ( idx >= max_pages )
				idx = max_pages;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[idx] == item );
			freeListBegin[idx] = item->nextFree;
			if ( freeListBegin[idx] != nullptr )
				freeListBegin[idx]->prevFree = nullptr;
		}
		if ( item->nextFree )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, item->getPageCount() == item->nextFree->getPageCount() || ( item->getPageCount() > max_pages && item->nextFree->getPageCount() > max_pages ));
			item->nextFree->prevFree = item->prevFree;
		}
	}

	void dbgValidateBlock( const AnyChunkHeader* h )
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h != nullptr );
		size_t szTotal = 0;
		const AnyChunkHeader* curr = h;
		while ( curr )
		{
			szTotal += curr->getPageCount();
			const AnyChunkHeader* next = curr->nextInBlock();
			if ( next )
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next > curr );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !(curr->isFree() && next->isFree()) );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next->prevInBlock() == curr );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, reinterpret_cast<const uint8_t*>(curr) + (curr->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<const uint8_t*>( next ) );
			}
			curr = next;
		}
		curr = h->prevInBlock();
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, curr == nullptr || ( curr->isFree() != h->isFree() ) );
		while ( curr )
		{
			szTotal += curr->getPageCount();
			const AnyChunkHeader* prev = curr->prevInBlock();
			if ( prev )
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev < curr );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !(prev->isFree() && curr->isFree()) );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev->nextInBlock() == curr );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, reinterpret_cast<const uint8_t*>(prev) + (prev->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<const uint8_t*>( curr ) );
			}
			curr = prev;
		}
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, (szTotal << PAGE_SIZE_EXP) == commited_block_size );
	}

#ifdef BULKALLOCATOR_HEAVY_DEBUG
	void dbgValidateAllBlocks()
	{
		class F { private: BulkAllocator<BasePageAllocator, commited_block_size, max_pages>* me; public: F(BulkAllocator<BasePageAllocator, commited_block_size, max_pages>*me_) {me = me_;} void f(AnyChunkHeader* h) {NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, blockList[i] != nullptr ); me->dbgValidateBlock( h ); } }; F f(this);
		blocks.doForEachBlock( f );
/*		for ( size_t i=0; i<blockList.size(); ++i )
		{
			AnyChunkHeader* start = reinterpret_cast<AnyChunkHeader*>( blockList[i] );
			dbgValidateBlock( start );
		}*/
	}
#endif

	void dbgValidateFreeList( const FreeChunkHeader* h, const uint16_t pageCnt )
	{
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h != nullptr );
		const FreeChunkHeader* curr = h;
		while ( curr )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, curr->getPageCount() == pageCnt || ( pageCnt > max_pages && curr->getPageCount() >= pageCnt ) );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, curr->isFree() );
			const FreeChunkHeader* next = curr->nextFree;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next == nullptr || next->prevFree == curr );
			curr = next;
		}
		curr = h;
		while ( curr )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, curr->getPageCount() == pageCnt || ( pageCnt > max_pages && curr->getPageCount() >= pageCnt ) );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, curr->isFree() );
			const FreeChunkHeader* prev = curr->prevFree;
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev == nullptr || prev->nextFree == curr );
			curr = prev;
		}
	}

	void dbgValidateAllFreeLists()
	{
		for ( uint16_t i=0; i<=max_pages; ++i )
		{
			FreeChunkHeader* h = freeListBegin[i];
			if ( h !=nullptr )
				dbgValidateFreeList( h, i + 1 );
		}
	}

public:
	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );
		for ( size_t i=0; i<=max_pages; ++i )
			freeListBegin[i] = nullptr;
//		new ( &blockList ) std::vector<AnyChunkHeader*>;
		blocks.initialize( PAGE_SIZE_EXP );
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
	}

	AnyChunkHeader* allocate( size_t szIncludingHeader )
	{
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

		AnyChunkHeader* ret = nullptr;

		size_t pageCount = ((uintptr_t)(-((intptr_t)((((uintptr_t)(-((intptr_t)szIncludingHeader))))) >> PAGE_SIZE_EXP )));

		if ( pageCount <= max_pages )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, pageCount <= UINT16_MAX );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, pageCount <= max_pages );

			if ( freeListBegin[pageCount - 1] == nullptr )
			{
				if ( freeListBegin[ max_pages ] == nullptr )
				{
					FreeChunkHeader* h = reinterpret_cast<FreeChunkHeader*>( this->getFreeBlockNoCache( commited_block_size ) );
					NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h!= nullptr );
//					blockList.push_back( h );
					*(blocks.createNew()) = h;
					freeListBegin[ max_pages ] = h;
					freeListBegin[ max_pages ]->set( nullptr, nullptr, pagesPerAllocatedBlock, true );
					freeListBegin[ max_pages ]->nextFree = nullptr;
					freeListBegin[ max_pages ]->prevFree = nullptr;
				}

				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[ max_pages ] != nullptr );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[ max_pages ]->getPageCount() > max_pages );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[ max_pages ]->prevFree == nullptr );

				ret = freeListBegin[ max_pages ];
				freeListBegin[ max_pages ] = freeListBegin[ max_pages ]->nextFree; // pop
				if ( freeListBegin[ max_pages ] != nullptr )
					freeListBegin[ max_pages ]->prevFree = nullptr;
				FreeChunkHeader* updatedBegin = reinterpret_cast<FreeChunkHeader*>( reinterpret_cast<uint8_t*>(ret) + (pageCount << PAGE_SIZE_EXP) );
				updatedBegin->set( ret, ret->nextInBlock(), ret->getPageCount() - (uint16_t)pageCount, true );
				updatedBegin->prevFree = nullptr;
				updatedBegin->nextFree = nullptr;

				ret->set( ret->prevInBlock(), updatedBegin, (uint16_t)pageCount, false );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[ max_pages ] != updatedBegin );

				uint16_t remainingPageCnt = updatedBegin->getPageCount();
				if ( remainingPageCnt > max_pages )
				{
					updatedBegin->nextFree = freeListBegin[ max_pages ];
					if ( freeListBegin[ max_pages ] != nullptr )
						freeListBegin[ max_pages ]->prevFree = updatedBegin;
					freeListBegin[ max_pages ] = updatedBegin;
				}
				else
				{
					updatedBegin->nextFree = freeListBegin[ remainingPageCnt - 1 ];
					if ( freeListBegin[ remainingPageCnt - 1 ] != nullptr )
						freeListBegin[ remainingPageCnt - 1 ]->prevFree = updatedBegin;
					freeListBegin[ remainingPageCnt - 1 ] = updatedBegin;
				}
			}
			else
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[pageCount - 1]->nextFree == nullptr || freeListBegin[pageCount - 1]->nextFree->prevFree == freeListBegin[pageCount - 1] );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, freeListBegin[pageCount - 1]->prevFree == nullptr );
				ret = freeListBegin[pageCount - 1];
				freeListBegin[pageCount - 1] = freeListBegin[pageCount - 1]->nextFree;
				if ( freeListBegin[pageCount - 1] != nullptr )
					freeListBegin[pageCount - 1]->prevFree = nullptr;
			}
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ret->getPageCount() <= max_pages );
		}
		else
		{
			ret = reinterpret_cast<FreeChunkHeader*>( this->getFreeBlockNoCache( pageCount << PAGE_SIZE_EXP ) );
			ret->set( (FreeChunkHeader*)(void*)(pageCount<<PAGE_SIZE_EXP), nullptr, 0, false );
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ret->getPageCount() == 0 );
		}


#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

		return ret;
	}

	void deallocate( void* ptr )
	{
		AnyChunkHeader* h = reinterpret_cast<AnyChunkHeader*>( ptr );
		if ( h->getPageCount() != 0 )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h->getPageCount() <= max_pages );
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

			AnyChunkHeader* prev = h->prevInBlock();
			if ( prev && prev->isFree() )
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev->prevInBlock() == nullptr || !prev->prevInBlock()->isFree() );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, prev->nextInBlock() == h );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, reinterpret_cast<uint8_t*>(prev->prevInBlock()) + (prev->prevInBlock()->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<uint8_t*>( h ) );
				removeFromFreeList( static_cast<FreeChunkHeader*>(prev) );
				prev->set( prev->prevInBlock(), h->nextInBlock(), prev->getPageCount() + h->getPageCount(), true );
				h = prev;
			}
			AnyChunkHeader* next = h->nextInBlock();
			if ( next && next->isFree() )
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next->nextInBlock() == nullptr || !next->nextInBlock()->isFree() );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, next->prevInBlock() == h );
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, reinterpret_cast<uint8_t*>(h) + (h->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<uint8_t*>( next ) );
				removeFromFreeList( static_cast<FreeChunkHeader*>(next) );
				h->set( h->prevInBlock(), next->nextInBlock(), h->getPageCount() + next->getPageCount(), true );
			}

			FreeChunkHeader* hfree = static_cast<FreeChunkHeader*>(h);
			uint16_t idx = hfree->getPageCount() - 1;
			if ( idx >= max_pages )
				idx = max_pages;
			hfree->prevFree = nullptr;
			hfree->nextFree = freeListBegin[idx];
			if ( freeListBegin[idx] != nullptr )
				freeListBegin[idx]->prevFree = hfree;
			freeListBegin[idx] = hfree;

#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
		}
		else
		{
			size_t deallocSize = (size_t)(h->prevInBlock());
			this->freeChunkNoCache( ptr, deallocSize );
		}

	}

	size_t getAllocatedSize( void* ptr )
	{
		AnyChunkHeader* h = reinterpret_cast<AnyChunkHeader*>( ptr );
		if ( h->getPageCount() != 0 )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h->getPageCount() <= max_pages );
			return h->getPageCount() << PAGE_SIZE_EXP;
		}
		else
		{
			return (size_t)(h->prevInBlock());
		}

	}

	void deinitialize()
	{
		class F { private: BasePageAllocator* alloc; public: F(BasePageAllocator*alloc_) {alloc = alloc_;} void f(AnyChunkHeader* h) {NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, h != nullptr ); alloc->freeChunkNoCache( h, commited_block_size ); } }; F f(this);
		blocks.doForEach(f);
		blocks.deinitialize();
/*		for ( size_t i=0; i<blockList.size(); ++i )
		{
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, blockList[i] != nullptr );
			this->freeChunkNoCache( blockList[i], commited_block_size );
		}
		blockList.clear();*/
		for ( size_t i=0; i<=max_pages; ++i )
			freeListBegin[i] = nullptr;
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
	}
};

//#define USE_EXP_BUCKET_SIZES
#define USE_HALF_EXP_BUCKET_SIZES
//#define USE_QUAD_EXP_BUCKET_SIZES

class IibAllocatorBase
{
protected:
	static constexpr size_t MaxBucketSize = PAGE_SIZE_BYTES * 2;
	static constexpr size_t BucketCountExp = 6;
	static constexpr size_t BucketCount = 1 << BucketCountExp;
	void* buckets[BucketCount];

	static constexpr size_t reservation_size_exp = 23;
	typedef BulkAllocator<PageAllocatorWithCaching, 1 << reservation_size_exp, 32> BulkAllocatorT;
	BulkAllocatorT bulkAllocator;

	typedef SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp, reservation_size_exp, 4, 3> PageAllocatorT;
	PageAllocatorT pageAllocator;

public:
#ifdef USE_EXP_BUCKET_SIZES
	static constexpr
	NODECPP_FORCEINLINE size_t indexToBucketSize(uint8_t ix) // Note: currently is used once per page formatting
	{
		return 1ULL << (ix + 3);
	}
#elif defined USE_HALF_EXP_BUCKET_SIZES
	static constexpr
	NODECPP_FORCEINLINE size_t indexToBucketSizeHalfExp(uint8_t ix) // Note: currently is used once per page formatting
	{
		size_t ret = ( 1ULL << ((ix>>1) + 3) ) + ( ( ( ( ix + 1 ) & 1 ) - 1 ) & ( 1ULL << ((ix>>1) + 2) ) );
		return alignUpExp( ret, 3 ); // this is because of case ix = 1, ret = 12 (keeping 8-byte alignment)
	}
#elif defined USE_QUAD_EXP_BUCKET_SIZES
	static constexpr
	NODECPP_FORCEINLINE size_t indexToBucketSizeQuarterExp(uint8_t ix) // Note: currently is used once per page formatting
	{
		ix += 3;
		size_t ret = ( 4ULL << ((ix>>2)) ) + ((ix&3)+1) * (1ULL << ((ix>>2)));
//		size_t ret = ( 4ULL << ((ix>>2)) ) + ( ( ( ( ix+1) & 1 ) - 1 ) & (1ULL << ((ix>>2))) ) + ( ( ( ((ix>>1)+1) & 1 ) - 1 ) & (2ULL << ((ix>>2))) ) + (1ULL << ((ix>>2)));
		return alignUpExp( ret, 3 ); // this is because of case ix = 1, ret = 12 (keeping 8-byte alignment), etc
	}
#else
#error "Undefined bucket size schema"
#endif

#if defined NODECPP_MSVC
#if defined NODECPP_X86
#ifdef USE_EXP_BUCKET_SIZES
	static
		NODECPP_FORCEINLINE uint8_t sizeToIndex(uint32_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
#elif defined USE_HALF_EXP_BUCKET_SIZES
#error "not implemented"
#elif defined USE_QUAD_EXP_BUCKET_SIZES
#error "not implemented"
#else
#error Undefined bucket size schema
#endif
#elif defined NODECPP_X64
#ifdef USE_EXP_BUCKET_SIZES
	static
	NODECPP_FORCEINLINE uint8_t sizeToIndex(uint64_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
#elif defined USE_HALF_EXP_BUCKET_SIZES
	static
	NODECPP_FORCEINLINE uint8_t sizeToIndexHalfExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz);
//		nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "ix = {}", ix );
		uint8_t addition = 1 & ( sz >> (ix-1) );
		ix = ((ix-2)<<1) + addition - 1;
		return static_cast<uint8_t>(ix);
	}
#elif defined USE_QUAD_EXP_BUCKET_SIZES
	static
	NODECPP_FORCEINLINE uint8_t sizeToIndexQuarterExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz);
//		nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "ix = {}", ix );
		uint8_t addition = 3 & ( sz >> (ix-2) );
		ix = ((ix-2)<<2) + addition - 3;
		return static_cast<uint8_t>(ix);
	}
#else
#error Undefined bucket size schema
#endif
#else
#error Unknown 32/64 bits architecture
#endif

#elif (defined NODECPP_CLANG) || (defined NODECPP_GCC)
#if defined NODECPP_X86
#ifdef USE_EXP_BUCKET_SIZES
	static
		NODECPP_FORCEINLINE uint8_t sizeToIndex(uint32_t sz)
	{
		uint32_t ix = __builtin_clzl(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(29ul - ix);
	}
#elif defined USE_HALF_EXP_BUCKET_SIZES
#error "not implemented"
#elif defined USE_QUAD_EXP_BUCKET_SIZES
#error "not implemented"
#else
#error Undefined bucket size schema
#endif
#elif defined NODECPP_X64
#ifdef USE_EXP_BUCKET_SIZES
	static
		NODECPP_FORCEINLINE uint8_t sizeToIndex(uint64_t sz)
	{
		uint64_t ix = __builtin_clzll(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
	}
#elif defined USE_HALF_EXP_BUCKET_SIZES
	static
		NODECPP_FORCEINLINE uint8_t sizeToIndexHalfExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
//		uint64_t ix = __builtin_clzll(sz - 1);
//		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
		uint64_t ix = __builtin_clzll(sz);
		ix = 63ull - ix;
//		nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "ix = {}", ix );
		uint8_t addition = 1ull & ( sz >> (ix-1) );
		ix = ((ix-2)<<1) + addition - 1;
		return static_cast<uint8_t>(ix);
	}
#elif defined USE_QUAD_EXP_BUCKET_SIZES
	static
		NODECPP_FORCEINLINE uint8_t sizeToIndexQuarterExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
//		uint64_t ix = __builtin_clzll(sz - 1);
//		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
		uint64_t ix = __builtin_clzll(sz);
		ix = 63ull - ix;
//		nodecpp::log::default_log::info( nodecpp::log::ModuleID(nodecpp::iibmalloc_module_id), "ix = {}", ix );
		uint8_t addition = 3ull & ( sz >> (ix-2) );
		ix = ((ix-2)<<2) + addition - 3;
		return static_cast<uint8_t>(ix);
	}
#else
#error Undefined bucket size schema
#endif
#else
#error Unknown 32/64 bits architecture
#endif	

#else
#error Unknown compiler
#endif
	
public:
	IibAllocatorBase() { initialize(); }
	IibAllocatorBase(const IibAllocatorBase&) = delete;
	IibAllocatorBase(IibAllocatorBase&&) = default;
	IibAllocatorBase& operator=(const IibAllocatorBase&) = delete;
	IibAllocatorBase& operator=(IibAllocatorBase&&) = default;

	static constexpr size_t maximalSupportedAlignment = std::max( 32, NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW );

	bool formatAllocatedPageAlignedBlock( uint8_t* block, size_t blockSz, size_t bucketSz, uint8_t bucketidx )
	{
		constexpr size_t memForbidden = alignUpExp( BulkAllocatorT::reservedSizeAtPageStart(), ALIGNMENT_EXP );
		if ( block == nullptr )
			return false;
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( ((uintptr_t)block) & PAGE_SIZE_MASK ) == 0 );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( blockSz & PAGE_SIZE_MASK ) == 0 );
		if( ( bucketSz & (memForbidden*2-1) ) == 0 ) // that is, (K * bucketSz) % PAGE_SIZE_BYTES != memForbidden for any K
		{
			size_t itemCnt = blockSz / bucketSz;
			if ( itemCnt )
			{
				for ( size_t i=0; i<(itemCnt-1)*bucketSz; i+=bucketSz )
				{
					NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ((i + bucketSz) & PAGE_SIZE_MASK) != memForbidden );
					*reinterpret_cast<void**>(block + i) = block + i + bucketSz;
				}
				*reinterpret_cast<void**>(block + (itemCnt-1)*bucketSz) = nullptr;
				buckets[bucketidx] = block;
				return true;
			}
			else
				return false;
		}
		else
		{
			size_t itemCnt = blockSz / bucketSz;
			if ( itemCnt )
			{
				for ( size_t i=0; i<(itemCnt-1)*bucketSz; i+=bucketSz )
				{
					if ( ((i + bucketSz) & PAGE_SIZE_MASK) != memForbidden )
						*reinterpret_cast<void**>(block + i) = block + i + bucketSz;
					else
					{ 
						NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, i != itemCnt - 2 ); // for small buckets such a bucket could not happen at the end anyway
						*reinterpret_cast<void**>(block + i) = block + i + bucketSz + bucketSz;
						i += bucketSz;
					}
				}
				*reinterpret_cast<void**>(block + (itemCnt-1)*bucketSz) = nullptr;
				buckets[bucketidx] = block;
				return true;
			}
			else
				return false;
		}
	}

	NODECPP_NOINLINE void* allocateInCaseNoFreeBucket( size_t sz, uint8_t szidx )
	{
#ifdef USE_EXP_BUCKET_SIZES
		size_t bucketSz = indexToBucketSize( szidx );
#elif defined USE_HALF_EXP_BUCKET_SIZES
		size_t bucketSz = indexToBucketSizeHalfExp( szidx );
#elif defined USE_QUAD_EXP_BUCKET_SIZES
		size_t bucketSz = indexToBucketSizeQuarterExp( szidx );
#else
#error Undefined bucket size schema
#endif
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, bucketSz >= sizeof( void* ) );
		PageAllocatorT::MultipageData mpData;
//		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getPage( szidx ) );
		pageAllocator.getMultipage( szidx, mpData );
		formatAllocatedPageAlignedBlock( reinterpret_cast<uint8_t*>( mpData.ptr1 ), mpData.sz1, bucketSz, szidx );
		formatAllocatedPageAlignedBlock( reinterpret_cast<uint8_t*>( mpData.ptr2 ), mpData.sz2, bucketSz, szidx );
		void* ret = buckets[szidx];
		buckets[szidx] = *reinterpret_cast<void**>(buckets[szidx]);
		return ret;
	}

	NODECPP_NOINLINE void* allocateInCaseTooLargeForBucket(size_t sz)
	{
		constexpr size_t memStart = alignUpExp( BulkAllocatorT::reservedSizeAtPageStart(), ALIGNMENT_EXP );
		void* block = bulkAllocator.allocate( sz + memStart );

		return reinterpret_cast<uint8_t*>(block) + memStart;
	}

	NODECPP_FORCEINLINE void* allocate(size_t sz)
	{
		if ( sz <= MaxBucketSize )
		{
#ifdef USE_EXP_BUCKET_SIZES
			uint8_t szidx = sizeToIndex( sz );
#elif defined USE_HALF_EXP_BUCKET_SIZES
			uint8_t szidx = sizeToIndexHalfExp( sz );
#elif defined USE_QUAD_EXP_BUCKET_SIZES
			uint8_t szidx = sizeToIndexQuarterExp( sz );
#else
#error Undefined bucket size schema
#endif
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, szidx < BucketCount );
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

	template<size_t sizeLowBound, size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned(size_t sz)
	{
		static_assert( alignment <= maximalSupportedAlignment );
		static_assert( sizeLowBound >= alignment );
		void* ret = nullptr;
#ifdef USE_EXP_BUCKET_SIZES
		ret = allocate( sz );
#elif defined USE_HALF_EXP_BUCKET_SIZES
		if constexpr ( alignment <= 8 ) 
			ret = allocate( sz );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, sz >= sizeLowBound, "{} vs. {}", sz, sizeLowBound );
		if constexpr ( sizeLowBound > 16 && sizeLowBound <= 24 )
			ret = allocate( sz > 24 ? sz : 25 );
		else if constexpr ( alignment <= 16 ) 
			ret = allocate( sz );
		else if constexpr ( sizeLowBound > 33 && sizeLowBound <= 48 )
			ret = allocate( sz > 48 ? sz : 49 );
		else
			ret = allocate( sz );
#elif defined USE_QUAD_EXP_BUCKET_SIZES
#error Not implemented
#else
#error Undefined bucket size schema
#endif
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, ((uintptr_t)ret & (alignment - 1)) == 0, "ret = 0x{:x}, alignment = {}", (uintptr_t)ret, alignment );
		return ret;
	}

	template<size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned(size_t sz)
	{
		static_assert( alignment <= maximalSupportedAlignment );
		void* ret = nullptr;
#ifdef USE_EXP_BUCKET_SIZES
		if constexpr ( alignment <= 8 ) 
			ret = allocate( sz );
		else
			ret = allocate( sz >= alignment ? sz : alignment );
#elif defined USE_HALF_EXP_BUCKET_SIZES
		if constexpr ( alignment <= 8 ) 
			ret = allocate( sz );
		else if constexpr ( alignment == 16 ) 
			ret = allocate( sz > 24 ? sz : 25 );
		else if constexpr ( alignment == 32 ) 
			ret = allocate( sz > 48 ? sz : 49 );
#elif defined USE_QUAD_EXP_BUCKET_SIZES
#error Not implemented
#else
#error Undefined bucket size schema
#endif
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, ((uintptr_t)ret & (alignment - 1)) == 0, "ret = 0x{:x}, alignment = {}", (uintptr_t)ret, alignment );
		return ret;
	}

	template<size_t sz, size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned()
	{
		static_assert( alignment <= maximalSupportedAlignment );
		static_assert( sz >= alignment );
		void* ret = nullptr;
#ifdef USE_EXP_BUCKET_SIZES
		ret = allocate( sz );
#elif defined USE_HALF_EXP_BUCKET_SIZES
		if constexpr ( alignment <= 8 ) 
			ret = allocate( sz );
		if constexpr ( sz > 16 && sz <= 24 )
			ret = allocate( 32 );
		else if constexpr ( alignment <= 16 ) 
			ret = allocate( sz );
		else if constexpr ( sz > 33 && sz <= 48 )
			ret = allocate( 64 );
		else
			ret = allocate( sz );
#elif defined USE_QUAD_EXP_BUCKET_SIZES
#error Not implemented
#else
#error Undefined bucket size schema
#endif
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, ((uintptr_t)ret & (alignment - 1)) == 0, "ret = 0x{:x}, alignment = {}", (uintptr_t)ret, alignment );
		return ret;
	}

	NODECPP_FORCEINLINE void deallocate(void* ptr)
	{
		if(ptr)
		{
			size_t offsetInPage = PageAllocatorT::getOffsetInPage( ptr );
			constexpr size_t memForbidden = alignUpExp( BulkAllocatorT::reservedSizeAtPageStart(), ALIGNMENT_EXP );
			if ( offsetInPage != memForbidden )
			{
				size_t idx = PageAllocatorT::addressToIdx( ptr );
				*reinterpret_cast<void**>( ptr ) = buckets[idx];
				buckets[idx] = ptr;
			}
			else
			{
				void* pageStart = PageAllocatorT::ptrToPageStart( ptr );
				bulkAllocator.deallocate( pageStart );
			}
		}
	}

	NODECPP_FORCEINLINE size_t getAllocatedSize(void* ptr)
	{
		if(ptr)
		{
			size_t offsetInPage = PageAllocatorT::getOffsetInPage( ptr );
			constexpr size_t memForbidden = alignUpExp( BulkAllocatorT::reservedSizeAtPageStart(), ALIGNMENT_EXP );
			if ( offsetInPage != memForbidden )
			{
				size_t idx = PageAllocatorT::addressToIdx( ptr );
#ifdef USE_EXP_BUCKET_SIZES
				return indexToBucketSize(idx);
#elif defined USE_HALF_EXP_BUCKET_SIZES
				return indexToBucketSizeHalfExp(idx);
#elif defined USE_QUAD_EXP_BUCKET_SIZES
				return indexToBucketSizeQuarterExp(idx);
#else
#error Undefined bucket size schema
#endif
			}
			else
			{
				void* pageStart = PageAllocatorT::ptrToPageStart( ptr );
				return bulkAllocator.getAllocatedSize( pageStart );
			}
		}
		else
			return 0;
	}
	
	const BlockStats& getStats() const { return pageAllocator.getStats(); }
	
	void printStats() const 
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
		pageAllocator.initialize( PAGE_SIZE_EXP );
		bulkAllocator.initialize( PAGE_SIZE_EXP );
	}

	void deinitialize()
	{
		pageAllocator.deinitialize();
		bulkAllocator.deinitialize();
	}

	~IibAllocatorBase()
	{
		deinitialize();
	}
};


#ifndef NODECPP_DISABLE_SAFE_ALLOCATION_MEANS

constexpr size_t guaranteed_prefix_size = 8;

class SafeIibAllocator : protected IibAllocatorBase
{
	static_assert( guaranteed_prefix_size >= sizeof(void*) ); // required to keep zombie list item pointer 'next' inside a block
protected:
	void** zombieBucketsFirst[BucketCount];
	void** zombieBucketsLast[BucketCount];
	void* zombieLargeChunks;

#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	std::map<uint8_t*, size_t, std::greater<uint8_t*>> zombieMap; // TODO: consider using thread-local allocator
	bool doZombieEarlyDetection_ = true;
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	
public:
	SafeIibAllocator() { initialize(); }
	SafeIibAllocator(const SafeIibAllocator&) = delete;
	SafeIibAllocator(SafeIibAllocator&&) = default;
	SafeIibAllocator& operator=(const SafeIibAllocator&) = delete;
	SafeIibAllocator& operator=(SafeIibAllocator&&) = default;

//	void enable() {}
//	void disable() {}

	bool doZombieEarlyDetection( bool doIt = true )
	{
#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, zombieMap.empty(), "to (re)set doZombieEarlyDetection() zombieMap must be empty" );
		bool ret = doZombieEarlyDetection_;
		doZombieEarlyDetection_ = doIt;
		return ret;
#else
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false, "to doZombieEarlyDetection() first define NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION" );
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	}


	NODECPP_FORCEINLINE void* allocate(size_t sz)
	{
		return IibAllocatorBase::allocate( sz );
	}

	template<size_t sizeLowBound, size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned(size_t sz)
	{
		return IibAllocatorBase::allocateAligned<sizeLowBound, alignment>( sz );
	}

	template<size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned(size_t sz)
	{
		return IibAllocatorBase::allocateAligned<alignment>( sz );
	}

	template<size_t sz, size_t alignment>
	NODECPP_FORCEINLINE void* allocateAligned()
	{
		return IibAllocatorBase::allocateAligned<sz, alignment>();
	}

	NODECPP_FORCEINLINE void deallocate(void* ptr )
	{
		IibAllocatorBase::deallocate( ptr );
	}

	NODECPP_FORCEINLINE size_t isPointerInBlock(void* allocatedPtr, void* ptr )
	{
		return ptr >= allocatedPtr && reinterpret_cast<uint8_t*>(ptr) < reinterpret_cast<uint8_t*>(allocatedPtr) + IibAllocatorBase::getAllocatedSize( ptr );
	}

	NODECPP_FORCEINLINE void* zombieableAllocate(size_t sz)
	{
		void* ret = IibAllocatorBase::allocate( sz + guaranteed_prefix_size );
		return reinterpret_cast<uint8_t*>(ret) + guaranteed_prefix_size;
	}

	template<size_t sizeLowBound, size_t alignment>
	NODECPP_FORCEINLINE void* zombieableAllocateAligned(size_t sz)
	{
		void* ret = IibAllocatorBase::allocateAligned<sizeLowBound + guaranteed_prefix_size, alignment>( sz + guaranteed_prefix_size );
		return reinterpret_cast<uint8_t*>(ret) + guaranteed_prefix_size;
	}

	template<size_t alignment>
	NODECPP_FORCEINLINE void* zombieableAllocateAligned(size_t sz)
	{
		void* ret = IibAllocatorBase::allocateAligned<alignment>( sz + guaranteed_prefix_size );
		return reinterpret_cast<uint8_t*>(ret) + guaranteed_prefix_size;
	}

	template<size_t sz, size_t alignment>
	NODECPP_FORCEINLINE void* zombieableAllocateAligned()
	{
		void* ret = IibAllocatorBase::allocateAligned<sz + guaranteed_prefix_size, alignment>();
		return reinterpret_cast<uint8_t*>(ret) + guaranteed_prefix_size;
	}

	NODECPP_FORCEINLINE void zombieableDeallocate(void* userPtr)
	{
		//void* ptr = reinterpret_cast<void**>(userPtr) - 1;
		void* ptr = reinterpret_cast<uint8_t*>(userPtr) - guaranteed_prefix_size;
		if(ptr)
		{
#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
			if ( doZombieEarlyDetection_ )
			{
				size_t allocSize = IibAllocatorBase::getAllocatedSize(ptr);
				zombieMap.insert( std::make_pair( reinterpret_cast<uint8_t*>(ptr), allocSize ) );
			}
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION

			size_t offsetInPage = PageAllocatorT::getOffsetInPage( ptr );
			constexpr size_t memForbidden = alignUpExp( BulkAllocatorT::reservedSizeAtPageStart(), ALIGNMENT_EXP );
			if ( offsetInPage != memForbidden ) // small and medium size
			{
				size_t idx = PageAllocatorT::addressToIdx( ptr );
				if ( zombieBucketsFirst[idx] ) // LIKELY
				{
					if ( zombieBucketsLast[idx] )
						*(zombieBucketsLast[idx]) = ptr;
					zombieBucketsLast[idx] = reinterpret_cast<void**>( ptr );
				}
				else
				{
					zombieBucketsFirst[idx] = reinterpret_cast<void**>( ptr );
					if ( zombieBucketsLast[idx] )
						*(zombieBucketsLast[idx]) = ptr;
					zombieBucketsLast[idx] = reinterpret_cast<void**>( ptr );
				}
			}
			else
			{
				*reinterpret_cast<void**>( ptr ) = zombieLargeChunks;
				zombieLargeChunks = ptr;
			}
		}
	}

	NODECPP_FORCEINLINE size_t isZombieablePointerInBlock(void* allocatedPtr, void* ptr )
	{
		void* trueAllocatedPtr = reinterpret_cast<void**>(allocatedPtr) - 1;
		return ptr >= allocatedPtr && reinterpret_cast<uint8_t*>(ptr) < reinterpret_cast<uint8_t*>(allocatedPtr) + IibAllocatorBase::getAllocatedSize( trueAllocatedPtr );
	}

#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	NODECPP_FORCEINLINE bool isPointerNotZombie( void* ptr )
	{
		if ( doZombieEarlyDetection_ )
		{
			auto iter = zombieMap.lower_bound( reinterpret_cast<uint8_t*>( ptr ) );
			if ( iter != zombieMap.end() )
			{
				NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ptr >= iter->first );
				return reinterpret_cast<uint8_t*>( ptr ) >= iter->first + iter->second;
			}
			else
				return true;
		}
		else
			return true;
	}
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION

	NODECPP_FORCEINLINE void killAllZombies()
	{
#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, doZombieEarlyDetection_ || ( !doZombieEarlyDetection_ && zombieMap.empty() ) );
		zombieMap.clear();
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
		for ( size_t idx=0; idx<BucketCount; ++idx)
		{
			if ( zombieBucketsLast[idx] )
			{
				*(zombieBucketsLast[idx]) = buckets[idx];
				buckets[idx] = *(zombieBucketsFirst[idx]);
			}
			zombieBucketsFirst[idx] = nullptr;
			zombieBucketsLast[idx] = nullptr;
		}
		while ( zombieLargeChunks != nullptr )
		{
			void* next = *reinterpret_cast<void**>( zombieLargeChunks );
			void* pageStart = PageAllocatorT::ptrToPageStart( zombieLargeChunks );
			bulkAllocator.deallocate( pageStart );
			zombieLargeChunks = next;
		}
	}
	
	const BlockStats& getStats() const { return IibAllocatorBase::getStats(); }
	
	void printStats() const { IibAllocatorBase::printStats(); }

	void initialize(size_t size)
	{
		initialize();
	}

	void initialize()
	{
		IibAllocatorBase::initialize();
		for ( size_t i=0; i<BucketCount; ++i)
		{
			zombieBucketsFirst[i] = nullptr;
			zombieBucketsLast[i] = nullptr;
		}
		zombieLargeChunks = nullptr;
#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
		doZombieEarlyDetection_ = true;
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	}

	void deinitialize()
	{
		IibAllocatorBase::deinitialize();
#ifndef NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, doZombieEarlyDetection_ || ( !doZombieEarlyDetection_ && zombieMap.empty() ) );
		zombieMap.clear();
#endif // NODECPP_DISABLE_ZOMBIE_ACCESS_EARLY_DETECTION
	}

	~SafeIibAllocator()
	{
	}
};

#endif // NODECPP_DISNABLE_SAFE_ALLOCATION_MEANS


#ifdef NODECPP_DISNABLE_SAFE_ALLOCATION_MEANS
typedef IibAllocatorBase ThreadLocalAllocatorT;
#else
typedef SafeIibAllocator ThreadLocalAllocatorT;
#endif // NODECPP_DISNABLE_SAFE_ALLOCATION_MEANS

extern thread_local ThreadLocalAllocatorT g_AllocManager;

ThreadLocalAllocatorT* interceptNewDeleteOperators( ThreadLocalAllocatorT* allocator );

} // namespace nodecpp::iibmalloc


#endif // IIBMALLOC_H
