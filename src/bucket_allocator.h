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

 
#ifndef SERIALIZABLE_ALLOCATOR_H
#define SERIALIZABLE_ALLOCATOR_H

#define NOMINMAX

#include <cstdlib>
#include <cstddef>
#include <cinttypes>
#include <memory>
#include <cstring>
#include <limits>
#include <algorithm>
#include <array>

// can't use AASSERTx because fmt::format allocates and gets recursive
#include <cassert>
//#include "../../../include/aassert.h"
//#include "../../../3rdparty/cppformat/fmt/format.h"

#if _MSC_VER
#include <intrin.h>
#define ALIGN(n)      __declspec(align(n))
#define NOINLINE      __declspec(noinline)
#define FORCE_INLINE	__forceinline
#elif __GNUC__
#define ALIGN(n)      __attribute__ ((aligned(n))) 
#define NOINLINE      __attribute__ ((noinline))
#define	FORCE_INLINE inline __attribute__((always_inline))
#else
#define	FORCE_INLINE inline
#define NOINLINE
//#define ALIGN(n)
#warning ALIGN, FORCE_INLINE and NOINLINE may not be properly defined
#endif




class SerializableAllocatorBase;
extern thread_local SerializableAllocatorBase g_AllocManager;

//static_assert(sizeof(void*) == sizeof(uint64_t), "Only 64 bits");

static union
{
	char asStr[sizeof(uint64_t)] = { 'A', 'l', 'l', 'o', 'c', 'a', 't', '\0' };
	uint64_t asUint;
} SerializableAllocatorMagic;

template<size_t IX>
constexpr
uint8_t sizeToExpImpl(size_t sz)
{
	return (sz == (1ull << IX)) ? IX : sizeToExpImpl<IX - 1>(sz);
}

template<>
constexpr
uint8_t sizeToExpImpl<0>(size_t sz)
{
	return 0; // error?
}

FORCE_INLINE constexpr
uint8_t sizeToExp(size_t sz)
{
	// keep it reasonable!
	return sizeToExpImpl<32>(sz);
}

FORCE_INLINE constexpr
size_t expToSize(uint8_t exp)
{
	return static_cast<size_t>(1) << exp;
}

static_assert(sizeToExp(64 * 1024) == 16, "broken!");

FORCE_INLINE constexpr
size_t expToMask(size_t sz)
{
	// keep it reasonable!
	return (static_cast<size_t>(1) << sz) - 1;
}


FORCE_INLINE constexpr
bool isAlignedMask(uintptr_t sz, uintptr_t alignmentMask)
{
	return (sz & alignmentMask) == 0;
}

FORCE_INLINE constexpr
uintptr_t alignDownExp(uintptr_t sz, uintptr_t alignmentExp)
{
	return ( sz >> alignmentExp ) << alignmentExp;
}

inline constexpr
bool isAlignedExp(uintptr_t sz, uintptr_t alignment)
{
	return alignDownExp(sz, alignment) == sz;
}

FORCE_INLINE constexpr
uintptr_t alignUpMask(uintptr_t sz, uintptr_t alignmentMask)
{
	return (( sz & alignmentMask) == 0) ? sz : sz - (sz & alignmentMask) + alignmentMask + 1;
}
inline constexpr
uintptr_t alignUpExp(uintptr_t sz, uintptr_t alignmentExp)
{
	return( -((-sz) >> alignmentExp ) << alignmentExp);
}

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


struct BucketBlockV2 : Chunk
{
private:
	size_t bucketSize = 0;
	uint8_t bucketSizeIndex = 0;
	size_t bucketsBegin = 0;

	size_t totalBuckets = 0;
	size_t freeBuckets = 0;
	
	size_t freeBucketList = 0;
public:
	
	static constexpr BucketKinds Kind = SmallBucket;


	bool isFull() const { return freeBuckets == 0; }
	bool isEmpty() const { return freeBuckets == totalBuckets; }

	size_t getBucketSize() const { return bucketSize; }
	uint8_t getBucketSizeIndex() const { return bucketSizeIndex; }

	FORCE_INLINE
	void initialize(size_t bucketSize, uint8_t bckSzIndex, size_t bucketsBegin, size_t bucketsCount)
	{
		uint8_t* begin = reinterpret_cast<uint8_t*>(this);
		this->setBucketKind(Kind);

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
		assert( freeBucketList < bucketsBegin + totalBuckets * bucketSize );
		freeBucketList = *reinterpret_cast<size_t*>(begin + freeBucketList);
		assert( freeBucketList < bucketsBegin + totalBuckets * bucketSize || freeBuckets == 0 );
		return begin + tmp;
	}
	FORCE_INLINE void release(void* ptr)
	{
		assert(freeBuckets != totalBuckets);
		uint8_t* begin = reinterpret_cast<uint8_t*>(this);
		assert( begin < ptr );
		assert( ptr < begin + bucketsBegin + totalBuckets * bucketSize );
		assert( freeBucketList < bucketsBegin + totalBuckets * bucketSize || freeBuckets == 0 );
		++freeBuckets;
		*reinterpret_cast<size_t*>(ptr) = freeBucketList;
		freeBucketList = reinterpret_cast<uint8_t*>(ptr) - begin;
		assert( freeBucketList < bucketsBegin + totalBuckets * bucketSize );
	}
};


template<uint8_t SIZE_EXP>
struct BucketBlockV4 : Chunk
{
private:
	size_t bucketSize = 0;
	uintptr_t blockSizeMask = 0;
	uint8_t bucketSizeIndex = 0;


	uint16_t totalBuckets = 0;
//	uint16_t freeBuckets = 0;
//	size_t bucketsBegin = 0;
//	size_t bucketsEnd = 0;

	uint16_t freeBucketListNext = 0;
	
	//this must be last member
	//size is not really 1, real size is determined at initialization
	uint16_t freeBucketList[1];

public:
	static constexpr BucketKinds Kind = MediumBucket;

	bool isFull() const { return freeBucketListNext == totalBuckets; }
	bool isEmpty() const { return freeBucketListNext == 0; }

	size_t getBucketSize() const { return bucketSize; }
	uint8_t getBucketSizeIndex() const { return bucketSizeIndex; }

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

	void initialize(size_t bucketSize, uint8_t bckSzIndex, size_t bucketsBegin, size_t bucketsCount)
	{
		assert(bucketSize >= sizeof(void*));
		assert(isAlignedExp(bucketsBegin, SIZE_EXP));
		assert(isAlignedExp(bucketSize, SIZE_EXP));

//		uintptr_t begin = reinterpret_cast<uintptr_t>(this);
		this->setBucketKind(Kind);

		this->bucketSize = bucketSize;
		this->bucketSizeIndex = bckSzIndex;
//		this->bucketsBegin = bucketsBegin;
//		this->bucketsEnd = bucketsEnd;

//		size_t totalB = (bucketsEnd - bucketsBegin) / bucketSize;
		this->totalBuckets = bucketsCount;
//		this->freeBuckets = totalBuckets;

		freeBucketListNext = 0;
		// free list initialization


		uint16_t bkt = bucketsBegin >> SIZE_EXP;
		for (uint16_t i = 0; i < totalBuckets; ++i)
		{
			freeBucketList[i] = bkt;
			bkt += (bucketSize >> SIZE_EXP);
		}
//		assert(bkt == (bucketsEnd >> SIZE_EXP));
	}

	FORCE_INLINE void* allocate()
	{
		assert(!isFull());

//		--freeBuckets;
		uintptr_t begin = reinterpret_cast<uintptr_t>(this);

		uint16_t bucketNumber = freeBucketList[freeBucketListNext++];
		return reinterpret_cast<void*>(begin | (bucketNumber << SIZE_EXP));
	}
	FORCE_INLINE void release(void* ptr)
	{
		assert(!isEmpty());
//		assert(bucketsBegin <= reinterpret_cast<size_t>(ptr));

		uintptr_t begin = reinterpret_cast<uintptr_t>(this);
		size_t bucketNumber = (reinterpret_cast<uintptr_t>(ptr) - begin) >> SIZE_EXP;
		assert(bucketNumber < UINT16_MAX);

		freeBucketList[--freeBucketListNext] = static_cast<uint16_t>(bucketNumber);
//		++freeBuckets;
	}
};



constexpr size_t ALIGNMENT = 2 * sizeof(uint64_t);
constexpr uint8_t ALIGNMENT_EXP = sizeToExp(ALIGNMENT);
static_assert( ( 1 << ALIGNMENT_EXP ) == ALIGNMENT, "" );
constexpr size_t ALIGNMENT_MASK = expToMask(ALIGNMENT_EXP);
static_assert( 1 + ALIGNMENT_MASK == ALIGNMENT, "" );

constexpr size_t MIN_CHUNK_SIZE = alignUpExp(sizeof(Chunk), ALIGNMENT_EXP);
constexpr size_t MAX_CHUNK_SIZE = alignDownExp(std::numeric_limits<Chunk::SizeT>::max(), ALIGNMENT_EXP);

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

constexpr size_t CHUNK_OVERHEAD = alignUpExp(sizeof(Chunk), FIRST_BUCKET_ALIGNMENT_EXP);



static constexpr size_t MAX_SMALL_BUCKET_SIZE = 32;

typedef BucketBlockV2 SmallBucketBlock;
typedef BucketBlockV4<sizeToExp(MAX_SMALL_BUCKET_SIZE) + 1> MediumBucketBlock;


class BucketSizes2
{
public:
	
	static constexpr size_t MaxSmallBucketSize = MAX_SMALL_BUCKET_SIZE;
	static constexpr size_t MaxBucketSize = MAX_BUCKET_SIZE;

	static constexpr uint8_t ArrSize = 16;
	size_t bucketsBegin[ArrSize];
	size_t bucketsSize[ArrSize];
	size_t bucketsCount[ArrSize];

	void initializeBucketOffsets(size_t blockSize, size_t firstBucketAlignmentExp)
	{
		for (uint8_t i = 0; i < ArrSize; ++i)
		{
			bucketsBegin[i] = 0;
			bucketsSize[i] = 0;
			bucketsCount[i] = 0;
		}
		
		for (uint8_t i = 0; i < ArrSize; ++i)
		{
			bucketsSize[i] = indexToBucketSize(i);
			if (bucketsSize[i] <= MaxSmallBucketSize)
			{
				size_t headerSize = sizeof(SmallBucketBlock);
				bucketsBegin[i] = alignUpExp(headerSize, firstBucketAlignmentExp);
				ptrdiff_t usableSize = blockSize - bucketsBegin[i];
				assert(usableSize > 0 && static_cast<size_t>(usableSize) >= bucketsSize[i]);
				//integral math
				bucketsCount[i] = usableSize / bucketsSize[i];
			}
			else
			{
				auto begin = MediumBucketBlock::calculateBucketsBegin(blockSize, bucketsSize[i]);
				bucketsBegin[i] = begin.first;
				bucketsCount[i] = begin.second;
			}
//			BucketsEnd[i] = BucketsBegin[i] + BucketsCount[i] * BucketsSize[i];

//			fmt::print("{} - {} -> {:x}, {}\n", i, bucketsSize[i], bucketsBegin[i], bucketsCount[i]);

			if (bucketsSize[i] >= MaxBucketSize)
				return;
		}
		assert(false); //shouldn't reach the end of array
	}

	

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
	
	static
	void dbgCheck()
	{
		assert(sizeToIndex(MaxBucketSize) < ArrSize);
		for (uint8_t i = 0; i != ArrSize; ++i)
			assert(sizeToIndex(indexToBucketSize(i)) == i);
		for (size_t i = 0; i != MaxBucketSize; ++i)
			assert(indexToBucketSize(sizeToIndex(i)) >= i);
	}
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

struct UsedBlockLists
{
	ChunkList partials;
	ChunkList fulls;
	ChunkList emptys;
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



template<class BS, class CM>
struct HeapManager
{
	uint64_t magic = SerializableAllocatorMagic.asUint;
	//uintptr_t begin = 0;
	//uintptr_t end = 0;
	//size_t reservedSize = 0;
	size_t blockSize = 0;
	uint8_t blockSizeExp = 0;
	size_t blockSizeMask = 0;
	size_t alignment = ALIGNMENT;
	uint8_t alignmentExp = ALIGNMENT_EXP;
	size_t alignmentMask = ALIGNMENT_MASK;
	uint8_t firstBucketAlignmentExp = FIRST_BUCKET_ALIGNMENT_EXP;

	std::array<void*, 32> userPtrs;

	std::array<UsedBlockLists, BS::ArrSize> bucketBlocks;
	ChunkList usedNonBuckets;
	BS bs;

	bool delayedDeallocate = false;
	size_t pendingDeallocatesCount = 0;
	std::array<void*, 1024> pendingDeallocates;

	CM freeChunks;


	HeapManager()
	{
		pendingDeallocates.fill(nullptr);
		userPtrs.fill(nullptr);
	}


	void initialize()
	{
		//begin = reinterpret_cast<uintptr_t>(ptr);
		//end = begin + sz;
		//reservedSize = sz;
		blockSize = BLOCK_SIZE;
		blockSizeExp = BLOCK_SIZE_EXP;
		blockSizeMask = BLOCK_SIZE_MASK;

		freeChunks.initialize(blockSizeExp);
		bs.initializeBucketOffsets(blockSize, firstBucketAlignmentExp);
	}

	
	template<class BUCKET>
	FORCE_INLINE
	BUCKET* makeBucketBlock(Chunk* chk, uint8_t index)
	{
		assert(index < BS::ArrSize);
		BUCKET* bb = static_cast<BUCKET*>(chk);
		size_t bktSz = bs.bucketsSize[index];
		size_t bktBegin = bs.bucketsBegin[index];
		size_t bktCount = bs.bucketsCount[index];
		bb->initialize(bktSz, index, bktBegin, bktCount);

		return bb;
	}

	
	template<class BUCKET>
	NOINLINE void* allocate3WhenNoChunkReady(UsedBlockLists& bl, uint8_t index)
	{
		Chunk* chk = freeChunks.getFreeBlock(blockSize);//alloc bucket;

		BUCKET* bb = makeBucketBlock<BUCKET>(chk, index);

		void* ptr = bb->allocate();

		assert(!bb->isFull());
		bl.partials.pushFront(bb);

		return ptr;
	}


	template<class BUCKET>
	FORCE_INLINE
	void* allocate3(UsedBlockLists& bl, uint8_t index)
	{
		if (!bl.partials.empty())
		{
			Chunk* chk = static_cast<Chunk*>(bl.partials.front());
			BUCKET* bb = static_cast<BUCKET*>(chk);
			assert(chk->getBucketKind() == BUCKET::Kind);
			//assert(sz <= bb->getBucketSize());
			void* ptr = bb->allocate();

			if (!bb->isFull()) // likely
				return ptr;

			bl.partials.remove(bb);
			bl.fulls.pushFront(bb);

			return ptr;
		}
		else if (!bl.emptys.empty())
		{
			Chunk* chk = static_cast<Chunk*>(bl.emptys.front());
			assert(chk->getBucketKind() == BUCKET::Kind);
			BUCKET* bb = static_cast<BUCKET*>(chk);
			//assert(sz <= bb->getBucketSize());
			void* ptr = bb->allocate();

			assert(!bb->isFull());

			bl.emptys.remove(bb);
			bl.partials.pushFront(bb);

			return ptr;
		}
		else
		{
			return allocate3WhenNoChunkReady<BUCKET>( bl, index );
		}
	}

	NOINLINE void* allocate2ForLargeSize(size_t sz)
	{
		size_t chkSz = alignUpExp(sz + CHUNK_OVERHEAD, blockSizeExp);
		Chunk* chk = freeChunks.getFreeBlock(chkSz);//alloc bucket;
		chk->setBucketKind(Chunk::NoBucket);

		usedNonBuckets.pushFront(chk);
		return reinterpret_cast<uint8_t*>(chk) + CHUNK_OVERHEAD;
	}

	FORCE_INLINE void* allocate2(size_t sz)
	{
		if (sz <= BS::MaxSmallBucketSize)
		{
			uint8_t ix = BS::sizeToIndex(sz);
			assert(ix < bucketBlocks.size());
			return allocate3<SmallBucketBlock>(bucketBlocks[ix], ix);
		}
		else if (sz <= BS::MaxBucketSize)
		{
			uint8_t ix = BS::sizeToIndex(sz);
			assert(ix < bucketBlocks.size());
			return allocate3<MediumBucketBlock>(bucketBlocks[ix], ix);
		}
		else
		{
			return allocate2ForLargeSize( sz );
		}
	}

	FORCE_INLINE void* allocate(size_t sz)
	{
		void* ptr = allocate2(sz);

		if (ptr)
		{
//			memset(ptr, 0, sz);
			return ptr;
		}
		else
			throw std::bad_alloc();
	}

	FORCE_INLINE
	Chunk* getChunkFromUsrPtr(void* ptr)
	{
		return reinterpret_cast<Chunk*>(alignDownExp(reinterpret_cast<uintptr_t>(ptr), blockSizeExp));
	}

	template<class BUCKET>
	NOINLINE
	void finalizeChuckDeallocationSpecialCases(BUCKET* bb, bool wasFull, bool isEmpty)
	{
		size_t ix = bb->getBucketSizeIndex();
		if (wasFull)
		{
			assert(!bb->isEmpty());

			bucketBlocks[ix].fulls.remove(bb);
			bucketBlocks[ix].partials.pushFront(bb);
		}
		else if (isEmpty)
		{
			bucketBlocks[ix].partials.remove(bb);
			if(bucketBlocks[ix].emptys.size() < KEEP_EMPTY_BUCKETS)
				bucketBlocks[ix].emptys.pushFront(bb);
			else
				freeChunks.freeChunk(bb); //may merge
		}
	}

	FORCE_INLINE void deallocate(void* ptr)
	{
		//if list is full, fall throught to regular deallocate
		if (delayedDeallocate && pendingDeallocatesCount != pendingDeallocates.size())
		{
			pendingDeallocates[pendingDeallocatesCount] = ptr;
			++pendingDeallocatesCount;
			return;
		}

		Chunk* chk = getChunkFromUsrPtr(ptr);
//		assert(!chk->isFree());
		auto kind = chk->getBucketKind();
		if (kind == SmallBucketBlock::Kind)
		{
			SmallBucketBlock* bb = static_cast<SmallBucketBlock*>(chk);
			bool wasFull = bb->isFull();
			bb->release(ptr);

			if ( wasFull || bb->isEmpty() )
				finalizeChuckDeallocationSpecialCases( bb, wasFull, bb->isEmpty() );
		}
		else if (kind == MediumBucketBlock::Kind)
		{
			MediumBucketBlock* bb = static_cast<MediumBucketBlock*>(chk);
			bool wasFull = bb->isFull();
			bb->release(ptr);

			if (wasFull || bb->isEmpty())
				finalizeChuckDeallocationSpecialCases(bb, wasFull, bb->isEmpty());
		}
		else
		{
			assert(kind == Chunk::NoBucket);
			//free chunk
			usedNonBuckets.remove(chk);
			freeChunks.freeChunk(chk);
		}
	}

	void printStats()
	{
		printf("----------------------\n");

		freeChunks.printStats();

		printf("\nBucketBlocks lists:\n");
		for (uint8_t i = 0; i != bucketBlocks.size(); ++i)
		{
			auto& c = bucketBlocks[i];
			if (!c.fulls.empty() || !c.partials.empty() || !c.emptys.empty())
			{
				printf("[ %u ]  %u / %u / %u\n", 
					static_cast<unsigned>(bs.bucketsSize[i]),
					static_cast<unsigned>(c.fulls.size()),
					static_cast<unsigned>(c.partials.size()),
					static_cast<unsigned>(c.emptys.size()));
			}
		}
		printf("\nNonBuckets chunks:\n");
		if(!usedNonBuckets.empty())
			printf("      %u\n", static_cast<unsigned>(usedNonBuckets.size()));
		printf("----------------------\n");
	}

	void setUserPtr(size_t ix, void* ptr) { userPtrs.at(ix) = ptr; }
	void* getUserPtr(size_t ix) const { return userPtrs.at(ix); }

	//size_t getReservedSize() const { return reservedSize; }
	//size_t getUsedSize() const
	//{
	//	return freeChunks.getLastUsedAddress() - begin;
	//}

	void enableDelayedDeallocate() { delayedDeallocate = true; }
	void flushDelayedDeallocate()
	{
		assert(delayedDeallocate);
		delayedDeallocate = false;

		for (size_t i = 0; i != pendingDeallocatesCount; ++i)
		{
			deallocate(pendingDeallocates[i]);
			pendingDeallocates[i] = nullptr;
		}

		pendingDeallocatesCount = 0;
		delayedDeallocate = true;
	}
};

//typedef BigBlockBase<BucketSizes, ChunkSizes> BigBlock;
typedef HeapManager<BucketSizes2, FreeChunks> Heap;
//typedef BigBlockBase<BucketSizes2, ChunkSizes> BigBlock;

class SerializableAllocatorBase
{
protected:
	Heap* heap = nullptr;
	size_t heapSz = 0;
	bool isEnabled = false;
	
public:
	SerializableAllocatorBase() {}
	SerializableAllocatorBase(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase(SerializableAllocatorBase&&) = default;
	SerializableAllocatorBase& operator=(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase& operator=(SerializableAllocatorBase&&) = default;

	void enable() {assert(!isEnabled); isEnabled = true;}
	void disable() {assert(isEnabled); isEnabled = false;}
	
	void enableDelayedDeallocate() { heap->enableDelayedDeallocate(); }
	void flushDelayedDeallocate() { heap->flushDelayedDeallocate(); }


	FORCE_INLINE void* allocate(size_t sz)
	{
		if (heap && isEnabled)
			return heap->allocate(sz);
		else
		{
			void* ptr = std::malloc(sz);

			if (ptr)
				return ptr;
			else
				throw std::bad_alloc();
		}
	}

	FORCE_INLINE void deallocate(void* ptr)
	{
		if(ptr)
		{
			if (heap && isEnabled)
				heap->deallocate(ptr);
			else
				std::free(ptr);
		}
	}
	
	void printStats()
	{
		assert(heap);
		heap->printStats();
	}

	void setUserPtr(size_t ix, void* ptr) { heap->setUserPtr(ix, ptr); }
	void* getUserPtr(size_t ix) const { return heap->getUserPtr(ix); }

	//void setUserRootPtr(void* ptr) { heap->setUserPtr(0, ptr); }
	//void* getDeserializedUserRootPtr() const { return heap->getUserPtr(0); }

	void serialize(void* buffer, size_t bufferSize)
	{
		//TODO
		//assert(heap);
		//assert(heap->getUsedSize() <= bufferSize);

		//memcpy(buffer, heap, arena->getUsedSize());
	}

	void initialize(size_t size)
	{
		initialize();
	}

	void initialize()
	{
		assert(!heap);
		
		size_t ps = VirtualMemory::getPageSize();
		uint8_t pageSizeExp = sizeToExp(ps);
		heapSz = alignUpExp(sizeof(Heap), pageSizeExp);

		
		void* ptr = VirtualMemory::allocate(heapSz);


		heap = new(ptr) Heap();
		heap->initialize();
	}

	~SerializableAllocatorBase()
	{
		if(heap)
			VirtualMemory::deallocate(heap, heapSz);
	}
	
	
	/* OS specific */
//	void deserialize(const std::string& fileName);//TODO
};


class InfraToAutomScope
{
public:
	InfraToAutomScope() {g_AllocManager.enable();}
	~InfraToAutomScope() {g_AllocManager.disable();}
};

class AutomToInfraScope
{
public:
	AutomToInfraScope() {g_AllocManager.disable();}
	~AutomToInfraScope() {g_AllocManager.enable();}
};

#endif //SERIALIZABLE_ALLOCATOR_H
