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
#ifndef ALLOCATOR_RANDOM_TEST_H
#define ALLOCATOR_RANDOM_TEST_H

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

#ifndef __GNUC__
#include <intrin.h>
#else
#endif

#include "test_common.h"
#include "bucket_allocator.h"


extern thread_local unsigned long long rnd_seed;
constexpr size_t max_threads = 32;

FORCE_INLINE unsigned long long rng(void)
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

FORCE_INLINE uint32_t xorshift32( uint32_t x )
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

FORCE_INLINE size_t calcSizeWithStatsAdjustment( uint64_t randNum, size_t maxSizeExp )
{
	assert( maxSizeExp >= 3 );
	maxSizeExp -= 3;
	uint32_t statClassBase = (randNum & (( 1 << maxSizeExp ) - 1)) + 1; // adding 1 to avoid dealing with 0
	randNum >>= maxSizeExp;
	unsigned long idx;
#if _MSC_VER
	uint8_t r = _BitScanForward(&idx, statClassBase);
	assert( r );
#elif __GNUC__
	idx = __builtin_ctzll( statClassBase );
#else
	static_assert(false, "Unknown compiler");
#endif
//	assert( idx <= maxSizeExp - 3 );
	assert( idx <= maxSizeExp );
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
		size_t val = calcSizeWithStatsAdjustment( rng(), exp );
//		assert( val <= (((size_t)1)<<exp) );
		assert( val );
		if ( val <=8 )
			bins[3] +=1;
		else
			for ( size_t j=4; j<=exp; ++j )
				if ( val <= (((size_t)1)<<j) && val > (((size_t)1)<<(j-1) ) )
					bins[j] += 1;
	}
	printf( "<=3: %zd\n", bins[0] + bins[1] + bins[2] + bins[3] );
	total = 0;
	for ( size_t j=0; j<=exp; ++j )
	{
		total += bins[j];
		printf( "%zd: %zd\n", j, bins[j] );
	}
	assert( total == testCnt );
}

enum { USE_PER_THREAD_ALLOCATOR, USE_NEW_DELETE, USE_EMPTY_TEST };
enum { USE_RANDOMPOS_FIXEDSIZE, USE_RANDOMPOS_FULLMEMACCESS_FIXEDSIZE, USE_RANDOMPOS_FULLMEMACCESS_RANDOMSIZE, USE_DEALLOCALLOCLEASTRECENTLYUSED_RANDOMUNITSIZE, USE_DEALLOCALLOCLEASTRECENTLYUSED_SAMEUNITSIZE, 
	USE_FREQUENTANDINFREQUENT_RANDOMUNITSIZE, USE_FREQUENTANDINFREQUENT_SKEWEDBINSELECTION_RANDOMUNITSIZE, USE_FREQUENTANDINFREQUENTWITHACCESS_RANDOMUNITSIZE, USE_FREQUENTANDINFREQUENTWITHACCESS_SKEWEDBINSELECTION_RANDOMUNITSIZE };

struct ThreadTestRes
{
	size_t threadID;

	size_t innerDur;

	uint64_t rdtscTotal;
	uint64_t rdtscSetup;
	uint64_t rdtscMainLoop;
	uint64_t rdtscExit;

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
};

struct TestRes
{
	size_t durEmpty;
	size_t durNewDel;
	size_t durPerThreadAlloc;
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
	size_t usePerThreadAllocator;
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



#endif
