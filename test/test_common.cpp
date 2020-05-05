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


#include "test_common.h"

#include <stdint.h>
#include <assert.h>

#if defined NODECPP_WINDOWS
#include <Windows.h>
#elif defined NODECPP_LINUX
#include <time.h>
#elif defined NODECPP_MAC
#include <mach/clock.h>
#include <mach/mach.h>
#endif


int64_t GetMicrosecondCount()
{
	int64_t now = 0;
#ifdef NODECPP_WINDOWS
	static int64_t frec = 0;
	if (frec == 0)
	{
		LARGE_INTEGER val;
		BOOL ok = QueryPerformanceFrequency(&val);
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ok);
		frec = val.QuadPart;
	}
	LARGE_INTEGER val;
	BOOL ok = QueryPerformanceCounter(&val);
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ok);
	now = (val.QuadPart * 1000000) / frec;
#endif
	return now;
}


#if defined NODECPP_WINDOWS
NODECPP_NOINLINE
size_t GetMillisecondCount()
{
    size_t now;

	static uint64_t frec = 0;
	if (frec == 0)
	{
		LARGE_INTEGER val;
		BOOL ok = QueryPerformanceFrequency(&val);
		NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ok);
		frec = val.QuadPart / 1000;
	}
	LARGE_INTEGER val;
	BOOL ok = QueryPerformanceCounter(&val);
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, ok);
	now = val.QuadPart / frec;
    return now;
}

#elif defined NODECPP_LINUX
NODECPP_NOINLINE
size_t GetMillisecondCount()
{
    size_t now;

#if 1
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);//clock get time monotonic
    now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; // mks
#else
    struct timeval now_;
    gettimeofday(&now_, NULL);
    now = now_.tv_sec;
    now *= 1000;
    now += now_.tv_usec / 1000000;
#endif
    return now;
}

#elif defined NODECPP_MAC
NODECPP_NOINLINE
size_t GetMillisecondCount()
{
    size_t now;

	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
    now = (uint64_t)mts.tv_sec * 1000 + mts.tv_nsec / 1000000; // mks
    return now;
}

#else // other OSs

#error unknown/unsupported OS

#endif
