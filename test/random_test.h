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
 * Per-thread bucket allocator
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/
#ifndef ALLOCATOR_RANDOM_TEST_H
#define ALLOCATOR_RANDOM_TEST_H

#include "test_common.h"

#include <stdint.h>
#define NOMINMAX

#include <memory>
#include <stdio.h>
#include <time.h>
#include <thread>
#include <assert.h>
#include <chrono>
#include <random>
#include <limits.h>

#ifdef NODECPP_MSVC
#include <intrin.h>
#else
#endif

//#include "bucket_allocator.h"
#include "../src/iibmalloc.h"


extern thread_local unsigned long long rnd_seed;
constexpr size_t max_threads = 32;

NODECPP_FORCEINLINE unsigned long long rng64(void)
{
	unsigned long long c = 7319936632422683443ULL;
	unsigned long long x = (rnd_seed += c);
	
	x ^= x >> 32;
	x *= c;
	x ^= x >> 32;
	x *= c;
	x ^= x >> 32;
	
	/* Return lower 32bits */
	return x;
}

NODECPP_FORCEINLINE uint32_t xorshift32( uint32_t x )
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

NODECPP_FORCEINLINE size_t calcSizeWithStatsAdjustment( uint64_t randNum, size_t maxSizeExp )
{
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, maxSizeExp >= 3 );
	maxSizeExp -= 3;
	uint32_t statClassBase = (randNum & (( 1 << maxSizeExp ) - 1)) + 1; // adding 1 to avoid dealing with 0
	randNum >>= maxSizeExp;
	unsigned long idx;
#ifdef NODECPP_MSVC
	uint8_t r = _BitScanForward(&idx, statClassBase);
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, r );
#elif (defined NODECPP_GCC) || (defined NODECPP_CLANG)
	idx = __builtin_ctzll( statClassBase );
#else
	static_assert(false, "Unknown compiler");
#endif
//	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, idx <= maxSizeExp - 3 );
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, idx <= maxSizeExp );
	idx += 2;
	size_t szMask = ( 1 << idx ) - 1;
	return (randNum & szMask) + 1 + (((size_t)1)<<idx);
}

inline void testDistribution()
{
	constexpr size_t exp = 16;
	constexpr size_t testCnt = 0x100000;
	size_t bins[exp+1];
	memset( bins, 0, sizeof( bins) );
	size_t total = 0;

	for (size_t i=0;i<testCnt;++i)
	{
		size_t val = calcSizeWithStatsAdjustment( rng64(), exp );
//		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, val <= (((size_t)1)<<exp) );
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, val );
		if ( val <=8 )
			bins[3] +=1;
		else
			for ( size_t j=4; j<=exp; ++j )
				if ( val <= (((size_t)1)<<j) && val > (((size_t)1)<<(j-1) ) )
					bins[j] += 1;
	}
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "<=3: {}", bins[0] + bins[1] + bins[2] + bins[3] );
	total = 0;
	for ( size_t j=0; j<=exp; ++j )
	{
		total += bins[j];
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}: {}", j, bins[j] );
	}
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, total == testCnt );
}

enum { TRY_ALL = 0xFFFFFFFF, USE_EMPTY_TEST = 0x1, USE_PER_THREAD_ALLOCATOR = 0x2, USE_NEW_DELETE = 0x4, };
enum { USE_RANDOMPOS_RANDOMSIZE };
enum MEM_ACCESS_TYPE { none, single, full };


struct CommonTestResults
{
	size_t threadID;

	size_t innerDur;

	uint64_t rdtscBegin;
	uint64_t rdtscSetup;
	uint64_t rdtscMainLoop;
	uint64_t rdtscExit;
};

struct ThreadTestRes : public CommonTestResults
{
	size_t sysAllocCallCntAfterSetup;
	size_t sysDeallocCallCntAfterSetup;
	size_t sysAllocCallCntAfterMainLoop;
	size_t sysDeallocCallCntAfterMainLoop;
	size_t sysAllocCallCntAfterExit;
	size_t sysDeallocCallCntAfterExit;

	uint64_t rdtscSysAllocCallSumAfterSetup;
	uint64_t rdtscSysDeallocCallSumAfterSetup;
	uint64_t rdtscSysAllocCallSumAfterMainLoop;
	uint64_t rdtscSysDeallocCallSumAfterMainLoop;
	uint64_t rdtscSysAllocCallSumAfterExit;
	uint64_t rdtscSysDeallocCallSumAfterExit;

	uint64_t allocRequestCountAfterSetup;
	uint64_t deallocRequestCountAfterSetup;
	uint64_t allocRequestCountAfterMainLoop;
	uint64_t deallocRequestCountAfterMainLoop;
	uint64_t allocRequestCountAfterExit;
	uint64_t deallocRequestCountAfterExit;
};

void printThreadStats( const char* prefix, ThreadTestRes& res )
{
	uint64_t rdtscTotal = res.rdtscExit - res.rdtscBegin;
//	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}{}: {}ms; {} ({} | {} | {});", prefix, res.threadID, res.innerDur, rdtscTotal, res.rdtscSetup - res.rdtscBegin, res.rdtscMainLoop - res.rdtscSetup, res.rdtscExit - res.rdtscMainLoop );
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}{}: {}ms; {} ({:.2f} | {:.2f} | {:.2f});", prefix, res.threadID, res.innerDur, rdtscTotal, (res.rdtscSetup - res.rdtscBegin) * 100. / rdtscTotal, (res.rdtscMainLoop - res.rdtscSetup) * 100. / rdtscTotal, (res.rdtscExit - res.rdtscMainLoop) * 100. / rdtscTotal );
}

void printThreadStatsEx( const char* prefix, ThreadTestRes& res )
{
	uint64_t rdtscTotal = res.rdtscExit - res.rdtscBegin;
//	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}{}: {}ms; {} ({} | {} | {});", prefix, res.threadID, res.innerDur, rdtscTotal, res.rdtscSetup - res.rdtscBegin, res.rdtscMainLoop - res.rdtscSetup, res.rdtscExit - res.rdtscMainLoop );
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}{}: {}ms; {} ({:.2f} | {:.2f} | {:.2f});", prefix, res.threadID, res.innerDur, rdtscTotal, (res.rdtscSetup - res.rdtscBegin) * 100. / rdtscTotal, (res.rdtscMainLoop - res.rdtscSetup) * 100. / rdtscTotal, (res.rdtscExit - res.rdtscMainLoop) * 100. / rdtscTotal );

	size_t mainLoopAllocCnt = res.sysAllocCallCntAfterMainLoop - res.sysAllocCallCntAfterSetup;
	uint64_t mainLoopAllocCntRdtsc = res.rdtscSysAllocCallSumAfterMainLoop - res.rdtscSysAllocCallSumAfterSetup;
	size_t exitAllocCnt = res.sysAllocCallCntAfterExit - res.sysAllocCallCntAfterMainLoop;
	uint64_t exitAllocCntRdtsc = res.rdtscSysAllocCallSumAfterExit - res.rdtscSysAllocCallSumAfterMainLoop;
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}\t[{} -> {}, {} ({})] [{} -> {}, {} ({})] [{} -> {}, {} ({})] ", 
		prefix, 
		res.allocRequestCountAfterSetup, res.sysAllocCallCntAfterSetup, res.rdtscSysAllocCallSumAfterSetup, res.sysAllocCallCntAfterSetup ? res.rdtscSysAllocCallSumAfterSetup / res.sysAllocCallCntAfterSetup : 0,
		res.allocRequestCountAfterMainLoop - res.allocRequestCountAfterSetup, mainLoopAllocCnt, mainLoopAllocCntRdtsc, mainLoopAllocCnt ? mainLoopAllocCntRdtsc / mainLoopAllocCnt : 0,
		res.allocRequestCountAfterExit - res.allocRequestCountAfterMainLoop, exitAllocCnt, exitAllocCntRdtsc, exitAllocCnt ? exitAllocCntRdtsc / exitAllocCnt : 0 );

	size_t mainLoopDeallocCnt = res.sysDeallocCallCntAfterMainLoop - res.sysDeallocCallCntAfterSetup;
	uint64_t mainLoopDeallocCntRdtsc = res.rdtscSysDeallocCallSumAfterMainLoop - res.rdtscSysDeallocCallSumAfterSetup;
	size_t exitDeallocCnt = res.sysDeallocCallCntAfterExit - res.sysDeallocCallCntAfterMainLoop;
	uint64_t exitDeallocCntRdtsc = res.rdtscSysDeallocCallSumAfterExit - res.rdtscSysDeallocCallSumAfterMainLoop;
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "{}\t[{} -> {}, {} ({})] [{} -> {}, {} ({})] [{} -> {}, {} ({})] ", 
		prefix, 
		res.deallocRequestCountAfterSetup, res.sysDeallocCallCntAfterSetup, res.rdtscSysDeallocCallSumAfterSetup, res.sysDeallocCallCntAfterSetup ? res.rdtscSysDeallocCallSumAfterSetup / res.sysDeallocCallCntAfterSetup : 0,
		res.deallocRequestCountAfterMainLoop - res.deallocRequestCountAfterSetup, mainLoopDeallocCnt, mainLoopDeallocCntRdtsc, mainLoopDeallocCnt ? mainLoopDeallocCntRdtsc / mainLoopDeallocCnt : 0,
		res.deallocRequestCountAfterExit - res.deallocRequestCountAfterMainLoop, exitDeallocCnt, exitDeallocCntRdtsc, exitDeallocCnt ? exitDeallocCntRdtsc / exitDeallocCnt : 0 );
}

struct TestRes
{
	size_t durEmpty;
	size_t durNewDel;
	size_t durPerThreadAlloc;
	size_t cumulativeDurEmpty;
	size_t cumulativeDurNewDel;
	size_t cumulativeDurPerThreadAlloc;
	ThreadTestRes threadResEmpty[max_threads];
	ThreadTestRes threadResNewDel[max_threads];
	ThreadTestRes threadResPerThreadAlloc[max_threads];
};

struct TestStartupParams
{
	size_t threadCount;
	size_t calcMod;
	size_t maxItems;
	size_t maxItemSize;
	size_t maxItems2;
	size_t maxItemSize2;
	size_t memReadCnt;
	size_t iterCount;
	size_t allocatorType;
	MEM_ACCESS_TYPE mat;
};

struct TestStartupParamsAndResults
{
	TestStartupParams startupParams;
	TestRes* testRes;
};

struct ThreadStartupParamsAndResults
{
	TestStartupParams startupParams;
	size_t threadID;
	ThreadTestRes* threadResEmpty;
	ThreadTestRes* threadResNewDel;
	ThreadTestRes* threadResPerThreadAlloc;
};

class NewDeleteUnderTest
{
	CommonTestResults* testRes;
	size_t start;

public:
	NewDeleteUnderTest( CommonTestResults* testRes_ ) { testRes = testRes_; }
	static constexpr bool isFake() { return false; }
	void init( size_t threadID )
	{
		start = GetMillisecondCount();
		testRes->threadID = threadID; // just as received
		testRes->rdtscBegin = __rdtsc();
	}

	void* allocate( size_t sz ) { return new uint8_t[ sz ]; }
	void deallocate( void* ptr ) { delete [] reinterpret_cast<uint8_t*>(ptr); }

	void deinit() {}

	void doWhateverAfterSetupPhase() { testRes->rdtscSetup = __rdtsc(); }
	void doWhateverWithinMainLoopPhase() {}
	void doWhateverAfterMainLoopPhase() { testRes->rdtscMainLoop = __rdtsc(); }
	void doWhateverAfterCleanupPhase()
	{
		testRes->rdtscExit = __rdtsc();
		testRes->innerDur = GetMillisecondCount() - start;
	}
};

class PerThreadAllocatorUnderTest
{
	ThreadTestRes* testRes;
	size_t start;

public:
	PerThreadAllocatorUnderTest( ThreadTestRes* testRes_ ) { testRes = testRes_; }
	static constexpr bool isFake() { return false; }

	void init( size_t threadID )
	{
		start = GetMillisecondCount();
		testRes->rdtscBegin = __rdtsc();
		g_AllocManager.initialize();
		g_AllocManager.enable();
	}

#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
	void* allocate( size_t sz ) { 
		void* ret = g_AllocManager.zombieableAllocate( sz ); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, g_AllocManager.isZombieablePointerInBlock(ret, ret)); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, !g_AllocManager.isZombieablePointerInBlock(ret, nullptr)); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, sz == 0 || g_AllocManager.isZombieablePointerInBlock(ret, reinterpret_cast<uint8_t*>(ret) + sz - 1)); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, sz < 16 || !g_AllocManager.isZombieablePointerInBlock(ret, reinterpret_cast<uint8_t*>(ret) + sz*2)); 
		return ret; 
	}
	void deallocate( void* ptr ) { g_AllocManager.zombieableDeallocate( ptr ); }
#else
	void* allocate( size_t sz ) { 
		void* ret = g_AllocManager.allocate( sz ); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, g_AllocManager.getAllocatedSize(ret) >= sz); 
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, sz < 8 || g_AllocManager.getAllocatedSize(ret) <= sz*2); 
		return ret; 
	}
	void deallocate( void* ptr ) { g_AllocManager.deallocate( ptr ); }
#endif
	void deinit()
	{
#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
		g_AllocManager.killAllZombies();
#endif
		g_AllocManager.deinitialize();
		g_AllocManager.disable();
	}

	void doWhateverAfterSetupPhase()
	{
#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
		g_AllocManager.killAllZombies();
#endif
		testRes->rdtscSetup = __rdtsc();
		testRes->rdtscSysAllocCallSumAfterSetup = g_AllocManager.getStats().rdtscSysAllocSpent;
		testRes->sysAllocCallCntAfterSetup = g_AllocManager.getStats().sysAllocCount;
		testRes->rdtscSysDeallocCallSumAfterSetup = g_AllocManager.getStats().rdtscSysDeallocSpent;
		testRes->sysDeallocCallCntAfterSetup = g_AllocManager.getStats().sysDeallocCount;
		testRes->allocRequestCountAfterSetup = g_AllocManager.getStats().allocRequestCount;
		testRes->deallocRequestCountAfterSetup = g_AllocManager.getStats().deallocRequestCount;
	}

	void doWhateverWithinMainLoopPhase()
	{
#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
		g_AllocManager.killAllZombies();
#endif
	}

	void doWhateverAfterMainLoopPhase()
	{
#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
		g_AllocManager.killAllZombies();
#endif
		testRes->rdtscMainLoop = __rdtsc();
		testRes->rdtscSysAllocCallSumAfterMainLoop = g_AllocManager.getStats().rdtscSysAllocSpent;
		testRes->sysAllocCallCntAfterMainLoop = g_AllocManager.getStats().sysAllocCount;
		testRes->rdtscSysDeallocCallSumAfterMainLoop = g_AllocManager.getStats().rdtscSysDeallocSpent;
		testRes->sysDeallocCallCntAfterMainLoop = g_AllocManager.getStats().sysDeallocCount;
		testRes->allocRequestCountAfterMainLoop = g_AllocManager.getStats().allocRequestCount;
		testRes->deallocRequestCountAfterMainLoop = g_AllocManager.getStats().deallocRequestCount;
	}

	void doWhateverAfterCleanupPhase()
	{
#ifdef NODECPP_ENABLE_SAFE_ALLOCATION_MEANS
		g_AllocManager.killAllZombies();
#endif
		testRes->rdtscExit = __rdtsc();
		testRes->rdtscSysAllocCallSumAfterExit = g_AllocManager.getStats().rdtscSysAllocSpent;
		testRes->sysAllocCallCntAfterExit = g_AllocManager.getStats().sysAllocCount;
		testRes->rdtscSysDeallocCallSumAfterExit = g_AllocManager.getStats().rdtscSysDeallocSpent;
		testRes->sysDeallocCallCntAfterExit = g_AllocManager.getStats().sysDeallocCount;
		testRes->allocRequestCountAfterExit = g_AllocManager.getStats().allocRequestCount;
		testRes->deallocRequestCountAfterExit = g_AllocManager.getStats().deallocRequestCount;
		testRes->innerDur = GetMillisecondCount() - start;
	}
};

class FakeAllocatorUnderTest
{
	CommonTestResults* testRes;
	size_t start;
	uint8_t* fakeBuffer = nullptr;
	static constexpr size_t fakeBufferSize = 0x1000000;

public:
	FakeAllocatorUnderTest( CommonTestResults* testRes_ ) { testRes = testRes_; }
	static constexpr bool isFake() { return true; } // thus indicating that certain checks over allocated memory should be ommited

	void init( size_t threadID )
	{
		start = GetMillisecondCount();
		testRes->threadID = threadID; // just as received
		testRes->rdtscBegin = __rdtsc();
		fakeBuffer = new uint8_t [fakeBufferSize];
	}

	void* allocate( size_t sz ) { NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, sz <= fakeBufferSize ); return fakeBuffer; }
	void deallocate( void* ptr ) {}

	void deinit() { if ( fakeBuffer ) delete [] fakeBuffer; fakeBuffer = nullptr; }

	void doWhateverAfterSetupPhase() { testRes->rdtscSetup = __rdtsc(); }
	void doWhateverWithinMainLoopPhase() {}
	void doWhateverAfterMainLoopPhase() { testRes->rdtscMainLoop = __rdtsc(); }
	void doWhateverAfterCleanupPhase()
	{
		testRes->rdtscExit = __rdtsc();
		testRes->innerDur = GetMillisecondCount() - start;
	}
};

constexpr double Pareto_80_20_6[7] = {
	0.262144000000,
	0.393216000000,
	0.245760000000,
	0.081920000000,
	0.015360000000,
	0.001536000000,
	0.000064000000};

struct Pareto_80_20_6_Data
{
	uint32_t probabilityRanges[6];
	uint32_t offsets[8];
};

NODECPP_FORCEINLINE
void Pareto_80_20_6_Init( Pareto_80_20_6_Data& data, uint32_t itemCount )
{
	data.probabilityRanges[0] = (uint32_t)(UINT32_MAX * Pareto_80_20_6[0]);
	data.probabilityRanges[5] = (uint32_t)(UINT32_MAX * (1. - Pareto_80_20_6[6]));
	for ( size_t i=1; i<5; ++i )
		data.probabilityRanges[i] = data.probabilityRanges[i-1] + (uint32_t)(UINT32_MAX * Pareto_80_20_6[i]);
	data.offsets[0] = 0;
	data.offsets[7] = itemCount;
	for ( size_t i=0; i<6; ++i )
		data.offsets[i+1] = data.offsets[i] + (uint32_t)(itemCount * Pareto_80_20_6[6-i]);
}

NODECPP_FORCEINLINE
size_t Pareto_80_20_6_Rand( const Pareto_80_20_6_Data& data, uint32_t rnum1, uint32_t rnum2 )
{
	size_t idx = 6;
	if ( rnum1 < data.probabilityRanges[0] )
		idx = 0;
	else if ( rnum1 < data.probabilityRanges[1] )
		idx = 1;
	else if ( rnum1 < data.probabilityRanges[2] )
		idx = 2;
	else if ( rnum1 < data.probabilityRanges[3] )
		idx = 3;
	else if ( rnum1 < data.probabilityRanges[4] )
		idx = 4;
	else if ( rnum1 < data.probabilityRanges[5] )
		idx = 5;
	uint32_t rangeSize = data.offsets[ idx + 1 ] - data.offsets[ idx ];
	uint32_t offsetInRange = rnum2 % rangeSize;
	return data.offsets[ idx ] + offsetInRange;
}

#if 1
template< class AllocatorUnderTest, MEM_ACCESS_TYPE mat>
void randomPos_RandomSize( AllocatorUnderTest& allocatorUnderTest, size_t iterCount, size_t maxItems, size_t maxItemSizeExp, size_t threadID )
{
	constexpr bool doMemAccess = mat != MEM_ACCESS_TYPE::none;
	constexpr bool doFullAccess = mat == MEM_ACCESS_TYPE::full;
//	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "rnd_seed = {}, iterCount = {}, maxItems = {}, maxItemSizeExp = {}", rnd_seed, iterCount, maxItems, maxItemSizeExp );
	allocatorUnderTest.init( threadID );

	size_t dummyCtr = 0;

	Pareto_80_20_6_Data paretoData;
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, maxItems <= UINT32_MAX );
	Pareto_80_20_6_Init( paretoData, (uint32_t)maxItems );

	size_t start = GetMillisecondCount();

	struct TestBin
	{
		uint8_t* ptr;
		size_t sz;
	};

	TestBin* baseBuff = nullptr; 
	if ( !allocatorUnderTest.isFake() )
		baseBuff = reinterpret_cast<TestBin*>( allocatorUnderTest.allocate( maxItems * sizeof(TestBin) ) );
	else
		baseBuff = new TestBin [ maxItems ]; // just using standard allocator
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, baseBuff );
	memset( baseBuff, 0, maxItems * sizeof( TestBin ) );

	// setup (saturation)
	for ( size_t i=0;i<maxItems/32; ++i )
	{
		uint32_t randNum = (uint32_t)( rng64() );
		for ( size_t j=0; j<32; ++j )
			if ( (randNum >> j) & 1 )
			{
				size_t randNumSz = rng64();
				size_t sz = calcSizeWithStatsAdjustment( randNumSz, maxItemSizeExp );
				baseBuff[i*32+j].sz = sz;
				baseBuff[i*32+j].ptr = reinterpret_cast<uint8_t*>( allocatorUnderTest.allocate( sz ) );
				if constexpr ( doMemAccess )
				{
					if constexpr ( doFullAccess )
						memset( baseBuff[i*32+j].ptr, (uint8_t)sz, sz );
					else
					{
						static_assert( mat == MEM_ACCESS_TYPE::single, "" );
						baseBuff[i*32+j].ptr[sz/2] = (uint8_t)sz;
					}
				}
			}
	}
	allocatorUnderTest.doWhateverAfterSetupPhase();

	// main loop
	for ( size_t j=0;j<iterCount/10000; ++j )
	{
		for ( size_t k=0;k<10000; ++k )
		{
			size_t randNum = rng64();
	//		size_t idx = randNum % maxItems;
			uint32_t rnum1 = (uint32_t)randNum;
			uint32_t rnum2 = (uint32_t)(randNum >> 32);
			size_t idx = Pareto_80_20_6_Rand( paretoData, rnum1, rnum2 );
			if ( baseBuff[idx].ptr )
			{
				if constexpr ( doMemAccess )
				{
					if constexpr ( doFullAccess )
					{
						size_t i=0;
						for ( ; i<baseBuff[idx].sz/sizeof(size_t ); ++i )
							dummyCtr += ( reinterpret_cast<size_t*>( baseBuff[idx].ptr) )[i];
						uint8_t* tail = baseBuff[idx].ptr + i * sizeof(size_t );
						for ( i=0; i<baseBuff[idx].sz % sizeof(size_t); ++i )
							dummyCtr += tail[i];
					}
					else
					{
						static_assert( mat == MEM_ACCESS_TYPE::single, "" );
						dummyCtr += baseBuff[idx].ptr[baseBuff[idx].sz/2];
					}
				}
				allocatorUnderTest.deallocate( baseBuff[idx].ptr );
				baseBuff[idx].ptr = 0;
			}
			else
			{
				size_t sz = calcSizeWithStatsAdjustment( rng64(), maxItemSizeExp );
				baseBuff[idx].sz = sz;
				baseBuff[idx].ptr = reinterpret_cast<uint8_t*>( allocatorUnderTest.allocate( sz ) );
				if constexpr ( doMemAccess )
				{
					if constexpr ( doFullAccess )
						memset( baseBuff[idx].ptr, (uint8_t)sz, sz );
					else
					{
						static_assert( mat == MEM_ACCESS_TYPE::single, "" );
						baseBuff[idx].ptr[sz/2] = (uint8_t)sz;
					}
				}
			}
		}
		allocatorUnderTest.doWhateverWithinMainLoopPhase();
	}
	allocatorUnderTest.doWhateverAfterMainLoopPhase();

	// exit
	for ( size_t idx=0; idx<maxItems; ++idx )
		if ( baseBuff[idx].ptr )
		{
			if constexpr ( doMemAccess )
			{
				if constexpr ( doFullAccess )
				{
					size_t i=0;
					for ( ; i<baseBuff[idx].sz/sizeof(size_t ); ++i )
						dummyCtr += ( reinterpret_cast<size_t*>( baseBuff[idx].ptr) )[i];
					uint8_t* tail = baseBuff[idx].ptr + i * sizeof(size_t );
					for ( i=0; i<baseBuff[idx].sz % sizeof(size_t); ++i )
						dummyCtr += tail[i];
				}
				else
				{
					static_assert( mat == MEM_ACCESS_TYPE::single, "" );
					dummyCtr += baseBuff[idx].ptr[baseBuff[idx].sz/2];
				}
			}
			allocatorUnderTest.deallocate( baseBuff[idx].ptr );
		}

	if ( !allocatorUnderTest.isFake() )
		allocatorUnderTest.deallocate( baseBuff );
	else
		delete [] baseBuff;
	allocatorUnderTest.deinit();
	allocatorUnderTest.doWhateverAfterCleanupPhase();
		
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "about to exit thread {} ({} operations performed) [ctr = {}]...", threadID, iterCount, dummyCtr );
}
#else
template< class AllocatorUnderTest, MEM_ACCESS_TYPE mat>
void randomPos_RandomSize( AllocatorUnderTest& allocatorUnderTest, size_t iterCount, size_t maxItems, size_t maxItemSizeExp, size_t threadID )
{
	constexpr bool doMemAccess = mat != MEM_ACCESS_TYPE::none;
	constexpr bool doFullAccess = mat == MEM_ACCESS_TYPE::full;
//	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "rnd_seed = {}, iterCount = {}, maxItems = {}, maxItemSizeExp = {}", rnd_seed, iterCount, maxItems, maxItemSizeExp );
	allocatorUnderTest.init( threadID );

	size_t dummyCtr = 0;

	Pareto_80_20_6_Data paretoData;
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, maxItems <= UINT32_MAX );
	Pareto_80_20_6_Init( paretoData, (uint32_t)maxItems );

	size_t start = GetMillisecondCount();

	struct TestBin
	{
		uint8_t* ptr;
		size_t sz;
	};

	TestBin* baseBuff = nullptr; 
	if ( !allocatorUnderTest.isFake() )
		baseBuff = reinterpret_cast<TestBin*>( allocatorUnderTest.allocate( maxItems * sizeof(TestBin) ) );
	else
		baseBuff = new TestBin [ maxItems ]; // just using standard allocator
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, baseBuff );
	memset( baseBuff, 0, maxItems * sizeof( TestBin ) );

	// setup (saturation)
	for ( size_t i=0;i<maxItems/32; ++i )
	{
		uint32_t randNum = (uint32_t)( rng64() );
		for ( size_t j=0; j<32; ++j )
			if ( (randNum >> j) & 1 )
			{
//				size_t randNumSz = rng64();
//				size_t sz = calcSizeWithStatsAdjustment( randNumSz, maxItemSizeExp );
				size_t sz = 1 << maxItemSizeExp;
				baseBuff[i*32+j].sz = sz;
				baseBuff[i*32+j].ptr = reinterpret_cast<uint8_t*>( allocatorUnderTest.allocate( sz ) );
				if constexpr ( doMemAccess )
				{
					if constexpr ( doFullAccess )
						memset( baseBuff[i*32+j].ptr, (uint8_t)sz, sz );
					else
					{
						static_assert( mat == MEM_ACCESS_TYPE::single, "" );
						baseBuff[i*32+j].ptr[sz/2] = (uint8_t)sz;
					}
				}
			}
	}
	allocatorUnderTest.doWhateverAfterSetupPhase();

	// main loop
	for ( size_t j=0;j<iterCount; ++j )
	{
		size_t randNum = rng64();
		size_t idx = randNum % maxItems;
//		uint32_t rnum1 = (uint32_t)randNum;
//		uint32_t rnum2 = (uint32_t)(randNum >> 32);
//		size_t idx = Pareto_80_20_6_Rand( paretoData, rnum1, rnum2 );
		if ( baseBuff[idx].ptr )
		{
			if constexpr ( doMemAccess )
			{
				if constexpr ( doFullAccess )
				{
					size_t i=0;
					for ( ; i<baseBuff[idx].sz/sizeof(size_t ); ++i )
						dummyCtr += ( reinterpret_cast<size_t*>( baseBuff[idx].ptr) )[i];
					uint8_t* tail = baseBuff[idx].ptr + i * sizeof(size_t );
					for ( i=0; i<baseBuff[idx].sz % sizeof(size_t); ++i )
						dummyCtr += tail[i];
				}
				else
				{
					static_assert( mat == MEM_ACCESS_TYPE::single, "" );
					dummyCtr += baseBuff[idx].ptr[baseBuff[idx].sz/2];
				}
			}
			allocatorUnderTest.deallocate( baseBuff[idx].ptr );
			baseBuff[idx].ptr = 0;
		}
		else
		{
//			size_t sz = calcSizeWithStatsAdjustment( rng64(), maxItemSizeExp );
			size_t sz = 1 << maxItemSizeExp;
			baseBuff[idx].sz = sz;
			baseBuff[idx].ptr = reinterpret_cast<uint8_t*>( allocatorUnderTest.allocate( sz ) );
			if constexpr ( doMemAccess )
			{
				if constexpr ( doFullAccess )
					memset( baseBuff[idx].ptr, (uint8_t)sz, sz );
				else
				{
					static_assert( mat == MEM_ACCESS_TYPE::single, "" );
					baseBuff[idx].ptr[sz/2] = (uint8_t)sz;
				}
			}
		}
	}
	allocatorUnderTest.doWhateverAfterMainLoopPhase();

	// exit
	for ( size_t idx=0; idx<maxItems; ++idx )
		if ( baseBuff[idx].ptr )
		{
			if constexpr ( doMemAccess )
			{
				if constexpr ( doFullAccess )
				{
					size_t i=0;
					for ( ; i<baseBuff[idx].sz/sizeof(size_t ); ++i )
						dummyCtr += ( reinterpret_cast<size_t*>( baseBuff[idx].ptr) )[i];
					uint8_t* tail = baseBuff[idx].ptr + i * sizeof(size_t );
					for ( i=0; i<baseBuff[idx].sz % sizeof(size_t); ++i )
						dummyCtr += tail[i];
				}
				else
				{
					static_assert( mat == MEM_ACCESS_TYPE::single, "" );
					dummyCtr += baseBuff[idx].ptr[baseBuff[idx].sz/2];
				}
			}
			allocatorUnderTest.deallocate( baseBuff[idx].ptr );
		}

	if ( !allocatorUnderTest.isFake() )
		allocatorUnderTest.deallocate( baseBuff );
	else
		delete [] baseBuff;
	allocatorUnderTest.deinit();
	allocatorUnderTest.doWhateverAfterCleanupPhase();
		
	nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::info>( "about to exit thread {} ({} operations performed) [ctr = {}]...", threadID, iterCount, dummyCtr );
}
#endif



#endif
