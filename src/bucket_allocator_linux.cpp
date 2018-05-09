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
 
 
#include "bucket_allocator.h"

#include <cstdlib>
#include <cstddef>
#include <memory>
#include <cstring>
#include <limits>

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


thread_local SerializableAllocatorBase g_AllocManager;


// void* operator new(std::size_t count)
// {
// 	return g_AllocManager.allocate(count);
// }
// 
// void* operator new[](std::size_t count)
// {
// 	return g_AllocManager.allocate(count);
// }
// 
// void operator delete(void* ptr) noexcept
// {
// 	g_AllocManager.deallocate(ptr);
// }
// 
// void operator delete[](void* ptr) noexcept
// {
// 	g_AllocManager.deallocate(ptr);
// }

#if __cplusplus >= 201703L

//We don't support alignment new/delete yet

//void* operator new(std::size_t count, std::align_val_t alignment);
//void* operator new[](std::size_t count, std::align_val_t alignment);
//void operator delete(void* ptr, std::align_val_t al) noexcept;
//void operator delete[](void* ptr, std::align_val_t al) noexcept;
#endif


// limit below is single read or write op in linux
static constexpr size_t MAX_LINUX = 0x7ffff000;
static_assert(MAX_LINUX <= MAX_CHUNK_SIZE, "Use of big chunks needs review.");

/*static*/
size_t VirtualMemory::getPageSize()
{
	long sz = sysconf(_SC_PAGESIZE);
	assert(sz != -1);
	return sz;  
}

/*static*/
size_t VirtualMemory::getAllocGranularity()
{
	// On linux page size and alloc granularity are the same thing
	return getPageSize();
}

size_t commitCtr = 0;
size_t decommitCtr = 0;
size_t commitSz = 0;
size_t decommitSz = 0;
size_t nowAllocated = 0;
size_t maxAllocated = 0;


/*static*/
uint8_t* VirtualMemory::reserve(void* addr, size_t size)
{
	return nullptr;
}

/*static*/
void VirtualMemory::commit(uintptr_t addr, size_t size)
{
	assert(false);
}

///*static*/
void VirtualMemory::decommit(uintptr_t addr, size_t size)
{
	assert(false);
}

void* VirtualMemory::allocate(size_t size)
{
	void* ptr = mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (!ptr)
		throw std::bad_alloc();

	++commitCtr;
	commitSz += size;

	return ptr;
}

void VirtualMemory::deallocate(void* ptr, size_t size)
{
	munmap(ptr, size);

	decommitSz += size;
	++decommitCtr;
}



