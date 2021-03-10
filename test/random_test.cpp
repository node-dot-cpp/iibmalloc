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


#include "random_test.h"

thread_local unsigned long long rnd_seed = 0;



void* runRandomTest( void* params )
{
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, params != nullptr );
	ThreadStartupParamsAndResults* testParams = reinterpret_cast<ThreadStartupParamsAndResults*>( params );
	switch ( testParams->startupParams.calcMod )
	{
		case USE_RANDOMPOS_RANDOMSIZE:
		{
			switch ( testParams->startupParams.allocatorType )
			{
				case USE_PER_THREAD_ALLOCATOR:
				{
					PerThreadAllocatorUnderTest allocator( testParams->threadResPerThreadAlloc );
					nodecpp::log::default_log::info( "    running thread {} with randomPos_RandomSize_FullMemAccess UsingPerThreadAllocator() [rnd_seed = {}] ...", testParams->threadID, rnd_seed );
					switch ( testParams->startupParams.mat )
					{
						case MEM_ACCESS_TYPE::none:
							randomPos_RandomSize<PerThreadAllocatorUnderTest,MEM_ACCESS_TYPE::none>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::full:
							randomPos_RandomSize<PerThreadAllocatorUnderTest,MEM_ACCESS_TYPE::full>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::single:
							randomPos_RandomSize<PerThreadAllocatorUnderTest,MEM_ACCESS_TYPE::single>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
					}
					break;
				}
				case USE_NEW_DELETE:
				{
					NewDeleteUnderTest allocator( testParams->threadResNewDel );
					nodecpp::log::default_log::info( "    running thread {} with randomPos_RandomSize_FullMemAccess UsingNewAndDelete() [rnd_seed = {}] ...", testParams->threadID, rnd_seed );
					switch ( testParams->startupParams.mat )
					{
						case MEM_ACCESS_TYPE::none:
							randomPos_RandomSize<NewDeleteUnderTest,MEM_ACCESS_TYPE::none>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::full:
							randomPos_RandomSize<NewDeleteUnderTest,MEM_ACCESS_TYPE::full>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::single:
							randomPos_RandomSize<NewDeleteUnderTest,MEM_ACCESS_TYPE::single>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
					}
					break;
				}
				case USE_EMPTY_TEST:
				{
					FakeAllocatorUnderTest allocator( testParams->threadResEmpty );
					nodecpp::log::default_log::info( "    running thread {} with randomPos_RandomSize_FullMemAccess Empty() [rnd_seed = {}] ...", testParams->threadID, rnd_seed );
					switch ( testParams->startupParams.mat )
					{
						case MEM_ACCESS_TYPE::none:
							randomPos_RandomSize<FakeAllocatorUnderTest,MEM_ACCESS_TYPE::none>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::full:
							randomPos_RandomSize<FakeAllocatorUnderTest,MEM_ACCESS_TYPE::full>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
						case MEM_ACCESS_TYPE::single:
							randomPos_RandomSize<FakeAllocatorUnderTest,MEM_ACCESS_TYPE::single>( allocator, testParams->startupParams.iterCount, testParams->startupParams.maxItems, testParams->startupParams.maxItemSize, testParams->threadID );
							break;
					}
					break;
				}
				default:
					NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false );
			}
			break;
		}
		default:
			NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false );
	}

	return nullptr;
}

void doTest( TestStartupParamsAndResults* startupParams )
{
	size_t testThreadCount = startupParams->startupParams.threadCount;

	ThreadStartupParamsAndResults testParams[max_threads];
	std::thread threads[ max_threads ];

	for ( size_t i=0; i<testThreadCount; ++i )
	{
		memcpy( testParams + i, startupParams, sizeof(TestStartupParams) );
		testParams[i].threadID = i;
		testParams[i].threadResEmpty = startupParams->testRes->threadResEmpty + i;
		testParams[i].threadResNewDel = startupParams->testRes->threadResNewDel + i;
		testParams[i].threadResPerThreadAlloc = startupParams->testRes->threadResPerThreadAlloc + i;
	}

	// run thread
	for ( size_t i=0; i<testThreadCount; ++i )
	{
		nodecpp::log::default_log::info( "about to run thread {}...", i );
		std::thread t1( runRandomTest, (void*)(testParams + i) );
		threads[i] = std::move( t1 );
//		t1.detach();
		nodecpp::log::default_log::info( "    ...done" );
	}
	// join threads
	for ( size_t i=0; i<testThreadCount; ++i )
	{
		nodecpp::log::default_log::info( "joining thread {}...", i );
		threads[i].join();
		nodecpp::log::default_log::info( "    ...done" );
	}
}

void runComparisonTest( TestStartupParamsAndResults& params )
{
	size_t memPageSize = nodecpp::VirtualMemory::getPageSize();
	nodecpp::log::default_log::info( "Memory page size: {} (0x{:x}) bytes", memPageSize, memPageSize );
	
	size_t start, end;
	size_t threadCount = params.startupParams.threadCount;

	size_t allocatorType = params.startupParams.allocatorType;

	if ( allocatorType & USE_EMPTY_TEST )
	{
		params.startupParams.allocatorType = USE_EMPTY_TEST;

		start = GetMillisecondCount();
		doTest( &params );
		end = GetMillisecondCount();
		params.testRes->durEmpty = end - start;
		nodecpp::log::default_log::info( "{} threads made {} alloc/dealloc operations in {} ms ({} ms per 1 million)", threadCount, params.startupParams.iterCount * threadCount, end - start, (end - start) * 1000000 / (params.startupParams.iterCount * threadCount) );
		params.testRes->cumulativeDurEmpty = 0;
		for ( size_t i=0; i<threadCount; ++i )
			params.testRes->cumulativeDurEmpty += params.testRes->threadResEmpty->innerDur;
		params.testRes->cumulativeDurEmpty /= threadCount;
	}

	if ( allocatorType & USE_NEW_DELETE )
	{
		params.startupParams.allocatorType = USE_NEW_DELETE;

		start = GetMillisecondCount();
		doTest( &params );
		end = GetMillisecondCount();
		params.testRes->durNewDel = end - start;
		nodecpp::log::default_log::info( "{} threads made {} alloc/dealloc operations in {} ms ({} ms per 1 million)", threadCount, params.startupParams.iterCount * threadCount, end - start, (end - start) * 1000000 / (params.startupParams.iterCount * threadCount) );
		params.testRes->cumulativeDurNewDel = 0;
		for ( size_t i=0; i<threadCount; ++i )
			params.testRes->cumulativeDurNewDel += params.testRes->threadResNewDel->innerDur;
		params.testRes->cumulativeDurNewDel /= threadCount;
	}

	if ( allocatorType & USE_PER_THREAD_ALLOCATOR )
	{
		params.startupParams.allocatorType = USE_PER_THREAD_ALLOCATOR;

		start = GetMillisecondCount();
		doTest( &params );
		end = GetMillisecondCount();
		params.testRes->durPerThreadAlloc = end - start;
		nodecpp::log::default_log::info( "{} threads made {} alloc/dealloc operations in {} ms ({} ms per 1 million)", threadCount, params.startupParams.iterCount * threadCount, end - start, (end - start) * 1000000 / (params.startupParams.iterCount * threadCount) );
		params.testRes->cumulativeDurPerThreadAlloc = 0;
		for ( size_t i=0; i<threadCount; ++i )
			params.testRes->cumulativeDurPerThreadAlloc += params.testRes->threadResPerThreadAlloc->innerDur;
		params.testRes->cumulativeDurPerThreadAlloc /= threadCount;
	}

	if ( allocatorType == TRY_ALL )
	{
		nodecpp::log::default_log::info( "Performance summary: {} threads, ({} - {}) / ({} - {}) = {}\n", threadCount, params.testRes->durNewDel, params.testRes->durEmpty, params.testRes->durPerThreadAlloc, params.testRes->durEmpty, (params.testRes->durNewDel - params.testRes->durEmpty) * 1. / (params.testRes->durPerThreadAlloc - params.testRes->durEmpty) );
		nodecpp::log::default_log::info( "Performance summary: {} threads, ({} - {}) / ({} - {}) = {}\n", threadCount, params.testRes->cumulativeDurNewDel, params.testRes->cumulativeDurEmpty, params.testRes->cumulativeDurPerThreadAlloc, params.testRes->cumulativeDurEmpty, (params.testRes->cumulativeDurNewDel - params.testRes->cumulativeDurEmpty) * 1. / (params.testRes->cumulativeDurPerThreadAlloc - params.testRes->cumulativeDurEmpty) );
	}
	params.startupParams.allocatorType = allocatorType; // restore
}

void alignedAllocTest()
{
	IibAllocatorBase::dbgImplementationConsistencyChecks();

	static constexpr size_t testCnt = 0x400;
	struct LargeAndAligned
	{
		alignas(32) uint8_t basemem[ 72 ];
		uintptr_t dummy;
	};
	struct LargeAndAlignedMaxNew
	{
		alignas(NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW) uint8_t basemem[ 40 ];
		uintptr_t dummy;
	};

	ThreadLocalAllocatorT allocManager;
	allocManager.initialize();
	ThreadLocalAllocatorT* formerAlloc = setCurrneAllocator( &allocManager );
	interceptNewDeleteOperators( &allocManager );

	void* ptrs[testCnt];

	// direct usage
	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = allocManager.allocateAligned<32>(47);
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & 31 ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		allocManager.deallocate(ptrs[i]);
	}

	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = allocManager.allocateAligned<16>(22);
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & 15 ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		allocManager.deallocate(ptrs[i]);
	}

	// new/delete interception (no iiballoc)
	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = new char [7];
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & 15 ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		delete [] ptrs[i];
	}

	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = new LargeAndAlignedMaxNew;
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & (alignof(LargeAndAlignedMaxNew) - 1 ) ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		delete (LargeAndAlignedMaxNew*)(ptrs[i]);
	}

	// new/delete interception (with iibmalloc)
	bool former = interceptNewDeleteOperators( true );
	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = new char [7];
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & 15 ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		delete [] ptrs[i];
	}

	for ( size_t i=0; i<testCnt; ++i )
	{
		ptrs[i] = new LargeAndAligned;
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ( (uintptr_t)(ptrs[i]) & (alignof(LargeAndAligned) - 1 ) ) == 0 );
	}
	for ( size_t i=0; i<testCnt; ++i )
	{
		delete (LargeAndAligned*)(ptrs[i]);
	}

	interceptNewDeleteOperators( former );
	formerAlloc = setCurrneAllocator( formerAlloc );
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, formerAlloc == &allocManager );
	allocManager.deinitialize();
}

int main()
{
	nodecpp::log::Log log;
	log.level = nodecpp::log::LogLevel::info;
	log.add( stdout );
	nodecpp::logging_impl::currentLog = &log;

	alignedAllocTest();

	TestRes testRes[max_threads];

	if( 1 )
	{
		memset( testRes, 0, sizeof( testRes ) );

		TestStartupParamsAndResults params;
		params.startupParams.iterCount = 100000;
		params.startupParams.maxItemSize = 16;
//		params.startupParams.maxItems = 23 << 20;
		params.startupParams.maxItemSize2 = 16;
		params.startupParams.maxItems2 = 16;
		params.startupParams.memReadCnt = 0;
//		params.startupParams.allocatorType = TRY_ALL;
//		params.startupParams.allocatorType = USE_EMPTY_TEST;
//		params.startupParams.allocatorType = USE_NEW_DELETE;
		params.startupParams.allocatorType = USE_PER_THREAD_ALLOCATOR;
		params.startupParams.calcMod = USE_RANDOMPOS_RANDOMSIZE;
		params.startupParams.mat = MEM_ACCESS_TYPE::full;

		size_t threadCountMax = 1;

		for ( params.startupParams.threadCount=1; params.startupParams.threadCount<=threadCountMax; ++(params.startupParams.threadCount) )
		{
			params.startupParams.maxItems = (1 << 25) / params.startupParams.threadCount;
			params.testRes = testRes + params.startupParams.threadCount;
			runComparisonTest( params );
		}

		nodecpp::log::default_log::info( "Test summary for USE_RANDOMPOS_RANDOMSIZE:" );
		for ( size_t threadCount=1; threadCount<=threadCountMax; ++threadCount )
		{
			TestRes& tr = testRes[threadCount];
			if ( params.startupParams.allocatorType == TRY_ALL )
				nodecpp::log::default_log::info( "{},{},{},{},{}", threadCount, tr.durEmpty, tr.durNewDel, tr.durPerThreadAlloc, (tr.durNewDel - tr.durEmpty) * 1. / (tr.durPerThreadAlloc - tr.durEmpty) );
			else
				nodecpp::log::default_log::info( "{},{},{},{}", threadCount, tr.durEmpty, tr.durNewDel, tr.durPerThreadAlloc );
			nodecpp::log::default_log::info( "Per-thread stats:" );
			for ( size_t i=0;i<threadCount;++i )
			{
				nodecpp::log::default_log::info( "   {}:", i );
				if ( params.startupParams.allocatorType & USE_EMPTY_TEST )
					printThreadStats( "\t", tr.threadResEmpty[i] );
				if ( params.startupParams.allocatorType & USE_NEW_DELETE )
					printThreadStats( "\t", tr.threadResNewDel[i] );
				if ( params.startupParams.allocatorType & USE_PER_THREAD_ALLOCATOR )
					printThreadStatsEx( "\t", tr.threadResPerThreadAlloc[i] );
			}
		}
		nodecpp::log::default_log::info( "" );

		nodecpp::log::default_log::info( "Short test summary for USE_RANDOMPOS_RANDOMSIZE:" );
		for ( size_t threadCount=1; threadCount<=threadCountMax; ++threadCount )
			if ( params.startupParams.allocatorType == TRY_ALL )
				nodecpp::log::default_log::info( "{},{},{},{},{}", threadCount, testRes[threadCount].durEmpty, testRes[threadCount].durNewDel, testRes[threadCount].durPerThreadAlloc, (testRes[threadCount].durNewDel - testRes[threadCount].durEmpty) * 1. / (testRes[threadCount].durPerThreadAlloc - testRes[threadCount].durEmpty) );
			else
				nodecpp::log::default_log::info( "{},{},{},{}", threadCount, testRes[threadCount].durEmpty, testRes[threadCount].durNewDel, testRes[threadCount].durPerThreadAlloc );

		nodecpp::log::default_log::info( "Short test summary for USE_RANDOMPOS_RANDOMSIZE (alt computations):" );
		for ( size_t threadCount=1; threadCount<=threadCountMax; ++threadCount )
			if ( params.startupParams.allocatorType == TRY_ALL )
				nodecpp::log::default_log::info( "{},{},{},{},{}", threadCount, testRes[threadCount].cumulativeDurEmpty, testRes[threadCount].cumulativeDurNewDel, testRes[threadCount].cumulativeDurPerThreadAlloc, (testRes[threadCount].cumulativeDurNewDel - testRes[threadCount].cumulativeDurEmpty) * 1. / (testRes[threadCount].cumulativeDurPerThreadAlloc - testRes[threadCount].cumulativeDurEmpty) );
			else
				nodecpp::log::default_log::info( "{},{},{},{}", threadCount, testRes[threadCount].cumulativeDurEmpty, testRes[threadCount].cumulativeDurNewDel, testRes[threadCount].cumulativeDurPerThreadAlloc );
	}

	nodecpp::log::default_log::info( "about to exit...                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         " );
	return 0;
}

