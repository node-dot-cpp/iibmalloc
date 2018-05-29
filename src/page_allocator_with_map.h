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
 * Page Allocator with map: 
 *     - same functional interface than Page Allocator
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef PAGE_ALLOCATOR_WITH_MAP_H
#define PAGE_ALLOCATOR_WITH_MAP_H

#include "page_allocator.h"
//#include <map>
#include <set>
#include <chrono>


FORCE_INLINE constexpr
uint32_t sizeToKilo(size_t sz)
{
	assert(isAlignedExp(sz, 10));
	assert(sz <= expToSize(32 + 10));

	return static_cast<uint32_t>(sz >> 10);
}

FORCE_INLINE constexpr
size_t kiloToSize(uint32_t kiloSz)
{
	return static_cast<size_t>(kiloSz) << 10;
}

struct ListItem
{
	ListItem* next = nullptr;
	ListItem* prev = nullptr;

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



class ItemList
{
private:
	uint32_t count = 0;
	ListItem lst;
public:
	
	ItemList()
	{
		lst.listInitializeEmpty();
	}

	FORCE_INLINE
	bool empty() const { return count == 0; }
	FORCE_INLINE
		uint32_t getCount() const { return count; }
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
};




struct SimplifiedBucketBlock2 : ListItem
{
private:
	size_t bucketSize = 0;
	uint8_t bucketSizeIndex = 0;
	size_t bucketsBegin = 0;

	size_t totalBuckets = 0;
	size_t freeBuckets = 0;

	size_t freeBucketList = 0;
public:

//	static constexpr BucketKinds Kind = SmallBucket;
	static
		std::pair<size_t, size_t> calculateBucketsBegin(size_t blockSize, size_t bucketSize, uint8_t firstBucketAlignmentExp)
	{
		size_t headerSize = sizeof(SimplifiedBucketBlock2);
		size_t begin = alignUpExp(headerSize, firstBucketAlignmentExp);
		ptrdiff_t usableSize = blockSize - begin;
		assert(usableSize > 0 && static_cast<size_t>(usableSize) >= bucketsSize[i]);
		//integral math
		size_t count = usableSize / bucketSize;

		return std::make_pair(begin, count);
	}


	bool isFull() const { return freeBuckets == 0; }
	bool isEmpty() const { return freeBuckets == totalBuckets; }

	size_t getBucketSize() const { return bucketSize; }
	uint8_t getBucketSizeIndex() const { return bucketSizeIndex; }

	FORCE_INLINE
		void initialize(size_t bucketSize, uint8_t bckSzIndex, size_t bucketsBegin, size_t bucketsCount)
	{
		uint8_t* begin = reinterpret_cast<uint8_t*>(this);
//		this->setBucketKind(Kind);

		assert(bucketSize >= sizeof(void*));
		this->bucketSize = bucketSize;
		this->bucketSizeIndex = bckSzIndex;
		this->bucketsBegin = bucketsBegin;

		this->totalBuckets = bucketsCount;
		this->freeBuckets = totalBuckets;

		// free list initialization
		this->freeBucketList = bucketsBegin;

		size_t bucketsEnd = bucketsBegin + bucketsCount * bucketSize;

		for (size_t i = bucketsBegin; i<(bucketsEnd - bucketSize); i += bucketSize)
			*reinterpret_cast<size_t*>(begin + i) = i + bucketSize;

		*reinterpret_cast<size_t*>(begin + bucketsEnd - bucketSize) = 0;
		assert(freeBucketList < bucketsBegin + totalBuckets * bucketSize);
	}

	FORCE_INLINE void* allocate()
	{
		assert(freeBuckets != 0);
		--freeBuckets;
		uint8_t* begin = reinterpret_cast<uint8_t*>(this);
		size_t tmp = freeBucketList;
		assert(freeBucketList < bucketsBegin + totalBuckets * bucketSize);
		freeBucketList = *reinterpret_cast<size_t*>(begin + freeBucketList);
		assert(freeBucketList < bucketsBegin + totalBuckets * bucketSize || freeBuckets == 0);
		return begin + tmp;
	}
	FORCE_INLINE void release(void* ptr)
	{
		assert(freeBuckets != totalBuckets);
		uint8_t* begin = reinterpret_cast<uint8_t*>(this);
		assert(begin < ptr);
		assert(ptr < begin + bucketsBegin + totalBuckets * bucketSize);
		assert(freeBucketList < bucketsBegin + totalBuckets * bucketSize || freeBuckets == 0);
		++freeBuckets;
		*reinterpret_cast<size_t*>(ptr) = freeBucketList;
		freeBucketList = reinterpret_cast<uint8_t*>(ptr) - begin;
		assert(freeBucketList < bucketsBegin + totalBuckets * bucketSize);
	}
};

constexpr size_t AlignmentOfFirstBucket = 64;

template<size_t SZ>
struct SimplifiedBucketAllocator
{
	typedef SimplifiedBucketBlock2 BucketBlock;

	ItemList partials;
	ItemList fulls;
	ItemList emptys;

	size_t bktSz = SZ;
	size_t bktBegin = 0;
	size_t bktCount = 0;

	uint8_t blockSizeExp = 0;
	size_t blockSize = 0;

	void initialize(void* firstBucketBlock, uint8_t blkSzExp)
	{
		blockSizeExp = blkSzExp;
		blockSize = expToSize(blkSzExp);

		auto begin = BucketBlock::calculateBucketsBegin(blockSize, bktSz, sizeToExp(AlignmentOfFirstBucket));
		bktBegin = begin.first;
		bktCount = begin.second;

		//create first bucket block

		BucketBlock* bb = static_cast<BucketBlock*>(firstBucketBlock);
		bb->initialize(bktSz, 0, bktBegin, bktCount);
		assert(bb->isEmpty());
		emptys.pushFront(bb);
	}


	FORCE_INLINE
		void* allocate()
	{
		if (!partials.empty())
		{
			BucketBlock* bb = static_cast<BucketBlock*>(partials.front());
			void* ptr = bb->allocate();

			if (!bb->isFull()) // likely
				return ptr;

			partials.remove(bb);
			fulls.pushFront(bb);

			return ptr;
		}
		else if (!emptys.empty())
		{
			BucketBlock* bb = static_cast<BucketBlock*>(emptys.front());
			void* ptr = bb->allocate();

			assert(!bb->isFull());

			emptys.remove(bb);
			partials.pushFront(bb);

			return ptr;
		}
		else
		{
			assert(false);
			throw std::bad_alloc();
		}
	}

	NOINLINE
		void deallocationSpecialCases(BucketBlock* bb, bool wasFull, bool isEmpty)
	{
		if (wasFull)
		{
			assert(!bb->isEmpty());

			fulls.remove(bb);
			partials.pushFront(bb);
		}
		else if (isEmpty)
		{
			partials.remove(bb);
			emptys.pushFront(bb);
		}
	}

	FORCE_INLINE
		BucketBlock* getBucketBlockFromPtr(void* ptr)
	{
		return reinterpret_cast<BucketBlock*>(alignDownExp(reinterpret_cast<uintptr_t>(ptr), blockSizeExp));
	}

	FORCE_INLINE void deallocate(void* ptr)
	{
		BucketBlock* bb = getBucketBlockFromPtr(ptr);
		//		assert(!chk->isFree());
		bool wasFull = bb->isFull();
		bb->release(ptr);

		if (wasFull || bb->isEmpty())
			deallocationSpecialCases(bb, wasFull, bb->isEmpty());
	}

	template<class Allocator>
	void doHouseKeeping(Allocator* alloc)
	{
		if (emptys.getCount() == 0)
		{
			void* chk = alloc->getFreeBlock(blockSize);//alloc bucket;

			BucketBlock* bb = static_cast<BucketBlock*>(chk);

			bb->initialize(bktSz, 0, bktBegin, bktCount);

			assert(bb->isEmpty());
			emptys.pushFront(bb);
		}
		else if (emptys.getCount() > 3)
		{
			BucketBlock* bb = static_cast<BucketBlock*>(emptys.popFront());
			alloc->freeChunk(bb);
		}
	}
};


struct PageDescriptor : public ListItem
{
	void* address; //inmutable
	uint32_t kiloSz;

	PageDescriptor(void* addr, size_t sz) :
		address(addr),
		kiloSz(sizeToKilo(sz))
	{
	}

	PageDescriptor(const PageDescriptor&) = default;
	PageDescriptor(PageDescriptor&&) = default;

	size_t getSize() const { return kiloToSize(kiloSz); }
	void* toPtr() const { return address; }

	bool operator==(const PageDescriptor& other) const { return this->address == other.address; }
};


/*
	mb: we need to share PageDescriptor between hash map and cache,
	because of that, we can't put PageDescriptors inside the hashmap,
	we need to allocate it in simplified buckets and use lists
*/

struct PageDescriptorHashMap
{
	std::chrono::nanoseconds timeAccum;
	uint32_t maxHashCount = 0;
	uint32_t elemCount = 0;
	uint32_t maxElemCount = 0;

	std::hash<void*> hh;
//	uint8_t blockSizeExp;
	uint8_t hashSizeExp;
	size_t hashSizeMask;
	ItemList* hashMap;
	std::array<ItemList, 1024> initialHashMap;

	PageDescriptorHashMap() :
		hashSizeExp(sizeToExp(initialHashMap.size())),
		hashSizeMask(expToMask(hashSizeExp)),
		hashMap(initialHashMap.data())
	{
	}

	void initialize()
	{
//		blockSizeExp = blkSzExp;
	}

	FORCE_INLINE
	size_t getHash(void* ptr)
	{
		size_t u = hh(ptr);
		u &= hashSizeMask;

		return u;
	}

	void insert(PageDescriptor* pd)
	{
		size_t h = getHash(pd->toPtr());

		hashMap[h].pushFront(pd);

		maxHashCount = std::max(maxHashCount, hashMap[h].getCount());
		++elemCount;
		maxElemCount = std::max(maxElemCount, elemCount);
	}

	PageDescriptor* findAndErase(void* ptr)
	{
		auto begin_time = std::chrono::high_resolution_clock::now();
		size_t h = getHash(ptr);
		assert(!hashMap[h].empty());

		ListItem* current = hashMap[h].front();

		while (!hashMap[h].isEnd(current))
		{
			PageDescriptor* pd = static_cast<PageDescriptor*>(current);
			if (pd->toPtr() == ptr)
			{
				hashMap[h].remove(pd);
				auto diff = std::chrono::high_resolution_clock::now() - begin_time;
				timeAccum += diff;

				--elemCount;

				return pd;
			}
			current = current->listGetNext();
		}

		assert(false);
		return nullptr;
	}

	void printStats()
	{
		std::chrono::milliseconds time = std::chrono::duration_cast<std::chrono::milliseconds>(timeAccum);
		printf("Time spent on find %lld ms, max hash count %u, max elem count %u\n", time.count(), maxHashCount, maxElemCount);
	}
};



struct PageAllocatorWithDescriptorHashMap // to be further developed for practical purposes
{
	std::array<ItemList, max_cached_size + 1> freeBlocks;
	SimplifiedBucketAllocator<32> buckets;
	PageDescriptorHashMap usedBlocks;
	static_assert(sizeof(PageDescriptor) <= 32, "Please increase bucket size!");


	BlockStats stats;

	uint8_t blockSizeExp = 0;
	size_t blockSize = 0;

public:

	PageAllocatorWithDescriptorHashMap() { }

	void initialize(uint8_t blkSzExp)
	{
		assert(blkSzExp >= 10);
		blockSizeExp = blkSzExp;
		blockSize = expToSize(blockSizeExp);


		void* ptr = VirtualMemory::allocate(blockSize);
		
		usedBlocks.initialize();
		buckets.initialize(ptr, blockSizeExp);

		void* bkt = buckets.allocate();
		PageDescriptor* pd = new(bkt) PageDescriptor(ptr, blockSize);

		usedBlocks.insert(pd);
	}


	FORCE_INLINE
		size_t sizeToIndex(size_t sz)
	{
		assert(isAlignedExp(sz, blockSizeExp));
		return (sz >> blockSizeExp) - 1;
	}


	FORCE_INLINE
	void* getFreeBlock(size_t sz)
	{

		size_t ix = sizeToIndex(sz);
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				PageDescriptor* d = static_cast<PageDescriptor*>(freeBlocks[ix].popFront());
				usedBlocks.insert(d);
				return d->toPtr();
			}
		}

		void* ptr = VirtualMemory::allocate(sz);
		stats.allocate(sz);
		assert(ptr);

		void* bkt = buckets.allocate();
		PageDescriptor* pd = new(bkt) PageDescriptor(ptr, sz);

		usedBlocks.insert(pd);

		return ptr;
	}

	FORCE_INLINE
		MemoryBlockListItem* getBucketBlock(size_t sz)
	{
		//returns a block with alignment requirements to make buckets
		void* ptr = getFreeBlock(sz);
		MemoryBlockListItem* item = new(ptr) MemoryBlockListItem();
		item->initialize(0, 0);

		return item;
	}

	FORCE_INLINE
		void freeChunk(void* ptr)
	{
		assert(isAlignedExp(reinterpret_cast<uintptr_t>(ptr), blockSizeExp));

		auto d = usedBlocks.findAndErase(ptr);

		size_t sz = d->getSize();
		size_t ix = sizeToIndex(sz);
		if (ix == 0) // quite likely case (all bucket chunks)
		{
			if (freeBlocks[ix].getCount() < single_page_cache_size)
			{
				freeBlocks[ix].pushFront(d);
				return;
			}
		}
		else if (ix < max_cached_size && freeBlocks[ix].getCount() < multi_page_cache_size)
		{
			freeBlocks[ix].pushFront(d);
			return;
		}

		stats.deallocate(sz);
//		descriptors.deallocate(d);

		assert(!d->isInList());
		buckets.deallocate(d);

		VirtualMemory::deallocate(ptr, sz);
	}

	void doHouseKeeping()
	{
		buckets.doHouseKeeping(this);
	}

	void printStats()
	{
		stats.printStats();
		usedBlocks.printStats();
	}
};


#endif //PAGE_ALLOCATOR_WITH_MAP_H
