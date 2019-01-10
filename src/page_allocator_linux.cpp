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
 
 
#include "iibmalloc_page_allocator.h"

#include <cstdlib>
#include <cstddef>
#include <memory>
#include <cstring>
#include <limits>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


using namespace nodecpp::iibmalloc;

thread_local PageAllocatorWithCaching thg_PageAllocatorWithCaching;

// limit below is single read or write op in linux
static constexpr size_t MAX_LINUX = 0x7ffff000;
// [DI: what depends on what/] static_assert(MAX_LINUX <= MAX_CHUNK_SIZE, "Use of big chunks needs review.");

/*static*/
size_t VirtualMemory::getPageSize()
{
	long sz = sysconf(_SC_PAGESIZE);
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, sz != -1);
	return sz;  
}

/*static*/
size_t VirtualMemory::getAllocGranularity()
{
	// On linux page size and alloc granularity are the same thing
	return getPageSize();
}


/*static*/
uint8_t* VirtualMemory::reserve(void* addr, size_t size)
{
	return nullptr;
}

/*static*/
void VirtualMemory::commit(uintptr_t addr, size_t size)
{
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false);
}

///*static*/
void VirtualMemory::decommit(uintptr_t addr, size_t size)
{
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, false);
}

void* VirtualMemory::allocate(size_t size)
{
	void* ptr = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (ptr == (void*)(-1))
	{
		int e = errno;
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "mmap error at allocate({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}

	return ptr;
}

void VirtualMemory::deallocate(void* ptr, size_t size)
{
	NODECPP_ASSERT(nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::critical, size % 4096 == 0 );
	int ret = munmap(ptr, size);
 	if ( ret == -1 )
	{
		int e = errno;
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "munmap error at deallocate(0x{:x}, 0x{:x}), error = {} ({})", (size_t)(ptr), size, e, strerror(e) );
//		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "munmap error at deallocate({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
}



void* VirtualMemory::AllocateAddressSpace(size_t size)
{
    void * ptr = mmap((void*)0, size, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (ptr == (void*)(-1))
	{
		int e = errno;
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "mmap error at AllocateAddressSpace({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
 //   msync(ptr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}
 
void* VirtualMemory::CommitMemory(void* addr, size_t size)
{
//    void * ptr = mmap(addr, size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED|MAP_ANON, -1, 0);
    void * ptr = mmap(addr, size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0);
	if (ptr == (void*)(-1))
	{
		int e = errno;
//		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "allocation error at CommitMemory(0x{:x}, 0x{:x}), error = {} ({})", (size_t)(addr), size, e, strerror(e) );
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "mmap error at CommitMemory({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
//    msync(addr, size, MS_SYNC|MS_INVALIDATE);
    return ptr;
}
 
void VirtualMemory::DecommitMemory(void* addr, size_t size)
{
    // instead of unmapping the address, we're just gonna trick 
    // the TLB to mark this as a new mapped area which, due to 
    // demand paging, will not be committed until used.
 
    void * ptr = mmap(addr, size, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0);
 	if (ptr == (void*)(-1))
	{
		int e = errno;
//		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "allocation error at DecommitMemory(0x{:x}, 0x{:x}), error = {} ({})", (size_t)(addr), size, e, strerror(e) );
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "mmap error at DecommitMemory({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
   msync(addr, size, MS_SYNC|MS_INVALIDATE);
}
 
void VirtualMemory::FreeAddressSpace(void* addr, size_t size)
{
    int ret = msync(addr, size, MS_SYNC);
  	if ( ret == -1 )
	{
		int e = errno;
//		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "allocation error at FreeAddressSpace(0x{:x}, 0x{:x}), error = {} ({})", (size_t)(addr), size, e, strerror(e) );
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "msync error at FreeAddressSpace({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
	ret = munmap(addr, size);
 	if ( ret == -1 )
	{
		int e = errno;
//		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "allocation error at FreeAddressSpace(0x{:x}, 0x{:x}), error = {} ({})", (size_t)(addr), size, e, strerror(e) );
		nodecpp::log::log<nodecpp::iibmalloc::module_id, nodecpp::log::LogLevel::error>( "munmap error at FreeAddressSpace({}), error = {} ({})", size, e, strerror(e) );
		throw std::bad_alloc();
	}
}