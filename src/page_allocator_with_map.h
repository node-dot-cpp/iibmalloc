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

 
#ifndef PAGE_ALLOCATOR_WITH_MAP_H
#define PAGE_ALLOCATOR_WITH_MAP_H

#include "page_allocator.h"
//#include <map>
#include <set>


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




template<uint8_t SIZE_EXP>
struct SimplifiedBucketBlock : ListItem
{
private:
	uint16_t totalBuckets = 0;
	uint16_t freeBucketListNext = 0;

	//this must be last member
	//size is not really 1, real size is determined at initialization
	uint16_t freeBucketList[1];

public:
	bool isFull() const { return freeBucketListNext == totalBuckets; }
	bool isEmpty() const { return freeBucketListNext == 0; }

	static
		std::pair<size_t, size_t> calculateBucketsBegin(size_t blockSize, size_t bucketSize)
	{
		size_t sz1 = blockSize - sizeof(BucketBlockV4<1>);

		size_t estCount = sz1 / (bucketSize + sizeof(uint16_t));

		sz1 -= estCount * sizeof(uint16_t);

		size_t realCount = sz1 / bucketSize;

		assert(realCount < UINT16_MAX);

		size_t begin = blockSize - (realCount * bucketSize);

		assert(sizeof(BucketBlockV4<1>) + realCount * sizeof(uint16_t) <= begin);

		return std::make_pair(begin, realCount);
	}

	void initialize(size_t bucketSize, size_t bucketsBegin, size_t bucketsCount)
	{
		assert(bucketSize >= sizeof(void*));
		assert(isAlignedExp(bucketsBegin, SIZE_EXP));
		assert(isAlignedExp(bucketSize, SIZE_EXP));

		this->totalBuckets = bucketsCount;

		freeBucketListNext = 0;
		// free list initialization


		uint16_t bkt = bucketsBegin >> SIZE_EXP;
		for (uint16_t i = 0; i < totalBuckets; ++i)
		{
			freeBucketList[i] = bkt;
			bkt += (bucketSize >> SIZE_EXP);
		}
	}

	FORCE_INLINE void* allocate()
	{
		assert(!isFull());

		uintptr_t begin = reinterpret_cast<uintptr_t>(this);

		uint16_t bucketNumber = freeBucketList[freeBucketListNext++];
		return reinterpret_cast<void*>(begin | (bucketNumber << SIZE_EXP));
	}
	FORCE_INLINE void release(void* ptr)
	{
		assert(!isEmpty());

		uintptr_t begin = reinterpret_cast<uintptr_t>(this);
		size_t bucketNumber = (reinterpret_cast<uintptr_t>(ptr) - begin) >> SIZE_EXP;
		assert(bucketNumber < UINT16_MAX);

		freeBucketList[--freeBucketListNext] = static_cast<uint16_t>(bucketNumber);
	}
};

constexpr size_t SimplifiedBucketSize = 64;

struct SimplifiedBucketAllocator
{
	typedef SimplifiedBucketBlock<sizeToExp(SimplifiedBucketSize)> BucketBlock;

	ItemList partials;
	ItemList fulls;
	ItemList emptys;

	size_t bktSz = SimplifiedBucketSize;;
	size_t bktBegin = 0;
	size_t bktCount = 0;

	uint8_t blockSizeExp = 0;
	size_t blockSize = 0;

	SimplifiedBucketAllocator()
	{
	}

	void initialize(void* firstBucketBlock, uint8_t blkSzExp)
	{
		blockSizeExp = blkSzExp;
		blockSize = expToSize(blkSzExp);

		auto begin = BucketBlock::calculateBucketsBegin(blockSize, bktSz);
		bktBegin = begin.first;
		bktCount = begin.second;

		//create first bucket block

		BucketBlock* bb = static_cast<BucketBlock*>(firstBucketBlock);
		bb->initialize(bktSz, bktBegin, bktCount);
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
			void* chk = alloc->getMapFreeBlock(blockSize);//alloc bucket;

			BucketBlock* bb = static_cast<BucketBlock*>(chk);

			bb->initialize(bktSz, bktBegin, bktCount);

			assert(bb->isEmpty());
			emptys.pushFront(bb);
		}
		else if (emptys.getCount() > 3)
		{
			BucketBlock* bb = static_cast<BucketBlock*>(emptys.popFront());
			alloc->freeMapChunk(bb);
		}
	}
};

template<class T>
struct ContainerAllocator
{
	static_assert(sizeof(T) <= SimplifiedBucketSize, "Please increase SimplifiedBucketSize!");

	SimplifiedBucketAllocator * bucketAlloc;
	typedef T value_type;
	ContainerAllocator(SimplifiedBucketAllocator* bucketAlloc) : bucketAlloc(bucketAlloc) {}

	ContainerAllocator(const ContainerAllocator&) = default;
	ContainerAllocator(ContainerAllocator&&) = default;

	ContainerAllocator& operator=(const ContainerAllocator&) = default;
	ContainerAllocator& operator=(ContainerAllocator&&) = default;

	template<class U>
	ContainerAllocator(const ContainerAllocator<U>& other) : ContainerAllocator(other.bucketAlloc) { }

	template<class U>
	ContainerAllocator(ContainerAllocator<U>&& other) : ContainerAllocator(other.bucketAlloc) { }

	T* allocate(size_t n)
	{
		assert(bucketAlloc);
		assert(n == 1);
		return static_cast<T*>(bucketAlloc->allocate());
	}

	void deallocate(T* ptr, size_t)
	{
		assert(bucketAlloc);
		bucketAlloc->deallocate(ptr);
	}

	bool operator==(const ContainerAllocator& other) const { return realAlloc == other.bucketAlloc; }
	bool operator!=(const ContainerAllocator& other) const { return !(this->operator==(other)); }

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


	bool operator<(const PageDescriptor& other) const { return this->address < other.address; }
	bool operator==(const PageDescriptor& other) const { return this->address == other.address; }
};


struct PageDescriptorMap
{
	typedef std::set<PageDescriptor, std::less<PageDescriptor>,
		ContainerAllocator<PageDescriptor>> Container;
	SimplifiedBucketAllocator buckets;

	Container* descriptors = nullptr;
	static_assert(sizeof(Container) <= SimplifiedBucketSize, "Please increase SimplifiedBucketSize!");

	PageDescriptorMap() {}

	void initialize(void* ptr, uint8_t blockSizeExp)
	{
		buckets.initialize(ptr, blockSizeExp);
		void* bkt = buckets.allocate();
		descriptors = new(bkt) Container(ContainerAllocator<PageDescriptor>(&buckets));
	}

	void insert(void* ptr, size_t sz)
	{
		auto res = descriptors->emplace(ptr, sz);
		assert(res.second);
	}

	PageDescriptor* find(void* ptr)
	{
		Container::iterator it = descriptors->find(PageDescriptor(ptr,0));
		assert(it != descriptors->end());

		//mb: const_cast is safe here, because key elements are inmutable in PageDescriptor
		return const_cast<PageDescriptor*>(&(*it));
	}

	void erase(PageDescriptor* pd)
	{
		descriptors->erase(*pd);
	}

	template<class Allocator>
	void doHouseKeeping(Allocator* alloc)
	{
		buckets.doHouseKeeping(alloc);
	}
};



struct PageAllocatorWithDescriptorMap // to be further developed for practical purposes
{
	std::array<ItemList, max_cached_size+1> freeBlocks;
	PageDescriptorMap descriptors;

	BlockStats stats;

	uint8_t blockSizeExp = 0;
	size_t blockSize = 0;

public:
	static constexpr size_t CHUNK_OVERHEAD = 0;

	PageAllocatorWithDescriptorMap() {}

	void initialize(uint8_t blkSzExp)
	{
		assert(blkSzExp >= 10);

		blockSizeExp = blkSzExp;
		blockSize = expToSize(blockSizeExp);

		void* ptr = VirtualMemory::allocate(blockSize);
		descriptors.initialize(ptr, blockSizeExp);
		descriptors.insert(ptr, blockSize);
		descriptors.doHouseKeeping(this);
	}

	FORCE_INLINE
		size_t sizeToIndex(size_t sz)
	{
		assert(isAlignedExp(sz, blockSizeExp));
		return (sz >> blockSizeExp) - 1;
	}


		template<bool DO_HK>
		FORCE_INLINE
			void* getFreeBlockInt(size_t sz)
	{

		size_t ix = sizeToIndex(sz);
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				PageDescriptor* d = static_cast<PageDescriptor*>(freeBlocks[ix].popFront());
				return d->toPtr();
			}
		}

		void* ptr = VirtualMemory::allocate(sz);
		stats.allocate(sz);
		assert(ptr);
		descriptors.insert(ptr, sz);
		if(DO_HK)
			descriptors.doHouseKeeping(this);

		return ptr;
	}


	template<bool DO_HK = true>
	FORCE_INLINE
		void freeChunkInt( void* ptr )
	{
		assert(isAlignedExp(reinterpret_cast<uintptr_t>(ptr), blockSizeExp));

		auto d = descriptors.find(ptr);

		size_t sz = d->getSize();
		size_t ix = sizeToIndex(sz);
		if ( ix == 0 ) // quite likely case (all bucket chunks)
		{
			if ( freeBlocks[ix].getCount() < single_page_cache_size )
			{
				freeBlocks[ix].pushFront(d);
				return;
			}
		}
		else if ( ix < max_cached_size && freeBlocks[ix].getCount() < multi_page_cache_size )
		{
			freeBlocks[ix].pushFront(d);
			return;
		}

		stats.deallocate(sz);
		descriptors.erase(d);
		if(DO_HK)
			descriptors.doHouseKeeping(this);
		
		VirtualMemory::deallocate(ptr, sz);
	}

	void* getFreeBlock(size_t sz)
	{
		return getFreeBlockInt<true>(sz);
	}

	void freeChunk(void* ptr)
	{
		freeChunkInt<true>(ptr);
	}

	//to be used by map, avoid recursion
	void* getMapFreeBlock(size_t sz)
	{
		return getFreeBlockInt<false>(sz);
	}

	//to be used by map, avoid recursion
	void freeMapChunk(void* ptr)
	{
		freeChunkInt<false>(ptr);
	}

	void printStats()
	{
		stats.printStats();
	}
};


struct PageDescriptorMap2
{
	SimplifiedBucketAllocator buckets;

	uint8_t blockSizeExp;
	uint8_t hashBucketExp;
	size_t hashBucketMask;
	ItemList* hashMap;
	std::array<ItemList, 64> initialHashMap;

	PageDescriptorMap2() :
		hashBucketExp(sizeToExp(initialHashMap.size())),
		hashBucketMask(expToMask(hashBucketExp)),
		hashMap(initialHashMap.data())
	{
	}

	void initialize(void* ptr, uint8_t blkSzExp)
	{
		blockSizeExp = blkSzExp;

		buckets.initialize(ptr, blockSizeExp);
	}

	size_t toHashBucket(void* ptr)
	{
		size_t u = reinterpret_cast<size_t>(ptr);
		u >>= blockSizeExp;
		u &= hashBucketMask;

		return u;
	}

	void insertNew(void* ptr, size_t sz)
	{
		size_t h = toHashBucket(ptr);

		void* bkt = buckets.allocate();
		PageDescriptor* pd = new(bkt) PageDescriptor(ptr, sz);

		hashMap[h].pushFront(pd);
	}

	void insert(PageDescriptor* pd)
	{
		size_t h = toHashBucket(pd->toPtr());

		hashMap[h].pushFront(pd);
	}

	PageDescriptor* getDescriptor(void* ptr)
	{
		size_t h = toHashBucket(ptr);
		assert(!hashMap[h].empty());

		ListItem* current = hashMap[h].front();

		while (!hashMap[h].isEnd(current))
		{
			PageDescriptor* pd = static_cast<PageDescriptor*>(current);
			if (pd->toPtr() == ptr)
			{
				hashMap[h].remove(pd);
				return pd;
			}
			current = current->listGetNext();
		}

		assert(false);
		return nullptr;
	}

	void deallocate(PageDescriptor* pd)
	{
		assert(!pd->isInList());
		buckets.deallocate(pd);
	}

	template<class Allocator>
	void doHouseKeeping(Allocator* alloc)
	{
		buckets.doHouseKeeping(alloc);
	}
};


struct PageAllocatorWithDescriptorHashMap // to be further developed for practical purposes
{
	std::array<ItemList, max_cached_size + 1> freeBlocks;
	PageDescriptorMap2 descriptors;

	BlockStats stats;

	uint8_t blockSizeExp = 0;
	size_t blockSize = 0;

public:
	static constexpr size_t CHUNK_OVERHEAD = 0;

	PageAllocatorWithDescriptorHashMap() { }

	void initialize(uint8_t blkSzExp)
	{
		assert(blkSzExp >= 10);
		blockSizeExp = blkSzExp;
		blockSize = expToSize(blockSizeExp);


		void* ptr = VirtualMemory::allocate(blockSize);
		descriptors.initialize(ptr, blockSizeExp);
		descriptors.insertNew(ptr, blockSize);
		descriptors.doHouseKeeping(this);
	}

	FORCE_INLINE
		size_t sizeToIndex(size_t sz)
	{
		assert(isAlignedExp(sz, blockSizeExp));
		return (sz >> blockSizeExp) - 1;
	}


	template<bool DO_HK>
	FORCE_INLINE
		void* getFreeBlockInt(size_t sz)
	{

		size_t ix = sizeToIndex(sz);
		if (ix < max_cached_size)
		{
			if (!freeBlocks[ix].empty())
			{
				PageDescriptor* d = static_cast<PageDescriptor*>(freeBlocks[ix].popFront());
				descriptors.insert(d);
				return d->toPtr();
			}
		}

		void* ptr = VirtualMemory::allocate(sz);
		stats.allocate(sz);
		assert(ptr);
		descriptors.insertNew(ptr, sz);
		if (DO_HK)
			descriptors.doHouseKeeping(this);

		return ptr;
	}


	template<bool DO_HK = true>
	FORCE_INLINE
		void freeChunkInt(void* ptr)
	{
		assert(isAlignedExp(reinterpret_cast<uintptr_t>(ptr), blockSizeExp));

		auto d = descriptors.getDescriptor(ptr);

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
		descriptors.deallocate(d);
		if (DO_HK)
			descriptors.doHouseKeeping(this);

		VirtualMemory::deallocate(ptr, sz);
	}

	void* getFreeBlock(size_t sz)
	{
		return getFreeBlockInt<true>(sz);
	}

	void freeChunk(void* ptr)
	{
		freeChunkInt<true>(ptr);
	}

	//to be used by map, avoid recursion
	void* getMapFreeBlock(size_t sz)
	{
		return getFreeBlockInt<false>(sz);
	}

	//to be used by map, avoid recursion
	void freeMapChunk(void* ptr)
	{
		freeChunkInt<false>(ptr);
	}

	void printStats()
	{
		stats.printStats();
	}
};


#endif //PAGE_ALLOCATOR_WITH_MAP_H
