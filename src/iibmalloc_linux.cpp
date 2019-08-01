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
 
 
//#include "bucket_allocator.h"
#include "iibmalloc.h"

#include <cstdlib>
#include <cstddef>
#include <memory>
#include <cstring>
#include <limits>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


namespace nodecpp::iibmalloc
{
	thread_local ThreadLocalAllocatorT g_AllocManager;
	thread_local ThreadLocalAllocatorT* g_CurrentAllocManager = nullptr;
	ThreadLocalAllocatorT* interceptNewDeleteOperators( ThreadLocalAllocatorT* allocator ) { 
		ThreadLocalAllocatorT* ret = g_CurrentAllocManager;
		g_CurrentAllocManager = allocator;
		return ret;
	}
}

using namespace nodecpp::iibmalloc;

#ifndef NODECPP_IIBMALLOC_DISABLE_NEW_DELETE_INTERCEPTION
void* operator new(std::size_t count)
{
	if ( g_CurrentAllocManager )
		return g_CurrentAllocManager->allocate(count);
	else
		return malloc(count);
}

void* operator new[](std::size_t count)
{
	if ( g_CurrentAllocManager )
		return g_CurrentAllocManager->allocate(count);
	else
		return malloc(count);
}

void operator delete(void* ptr) noexcept
{
	if ( g_CurrentAllocManager )
		g_CurrentAllocManager->deallocate(ptr);
	else
		free(ptr);
}

void operator delete[](void* ptr) noexcept
{
	if ( g_CurrentAllocManager )
		g_CurrentAllocManager->deallocate(ptr);
	else
		free(ptr);
}
#endif // NODECPP_IIBMALLOC_DISABLE_NEW_DELETE_INTERCEPTION

#if __cplusplus >= 201703L

//We don't support alignment new/delete yet

//void* operator new(std::size_t count, std::align_val_t alignment);
//void* operator new[](std::size_t count, std::align_val_t alignment);
//void operator delete(void* ptr, std::align_val_t al) noexcept;
//void operator delete[](void* ptr, std::align_val_t al) noexcept;
#endif


