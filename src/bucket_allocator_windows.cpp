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

#include <windows.h>

//#include "../../../3rdparty/cppformat/fmt/format.h"
//#include "../../../include/alog.h"

//using namespace autom;

thread_local SerializableAllocatorBase g_AllocManager;

template< typename... ARGS >
void allocLog(const char* formatStr, const ARGS&... args)
{
	constexpr size_t string_max = 255;
	char buff[string_max+1];
	snprintf(buff, string_max, formatStr, args...);
	printf("%s\n", buff);
}

//void* operator new(std::size_t count)
//{
//	return g_AllocManager.allocate(count);
//}
//
//void* operator new[](std::size_t count)
//{
//	return g_AllocManager.allocate(count);
//}
//
//void operator delete(void* ptr) noexcept
//{
//	g_AllocManager.deallocate(ptr);
//}
//
//void operator delete[](void* ptr) noexcept
//{
//	g_AllocManager.deallocate(ptr);
//}

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
	SYSTEM_INFO siSysInfo;
	GetSystemInfo(&siSysInfo);

	return static_cast<size_t>(siSysInfo.dwPageSize);
}

/*static*/
size_t VirtualMemory::getAllocGranularity()
{
	SYSTEM_INFO siSysInfo;
	GetSystemInfo(&siSysInfo);

	return static_cast<size_t>(siSysInfo.dwAllocationGranularity);
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
	void* ptr = VirtualAlloc(addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	if (!addr)
	{
		++commitCtr;
		commitSz += size;
		allocLog("VirtualMemory::reserve %zd 0x%x", (size_t)(ptr), size);
	}
	else if (addr == ptr)
	{
		++commitCtr;
		commitSz += size;
		allocLog("VirtualMemory::reserve %zd 0x%x", (size_t)(ptr), size);
	}
	else
	{
		allocLog("VirtualMemory::reserve  %zd 0x%x FAILED", (size_t)(addr), size);

		MEMORY_BASIC_INFORMATION info;
		SIZE_T s = VirtualQuery(addr, &info, size);


		allocLog("BaseAddress=%zd", (size_t)(info.BaseAddress));
		allocLog("AllocationBase=%zd", (size_t)(info.AllocationBase));
		allocLog("RegionSize=%zd", info.RegionSize);
		allocLog("State=%d", info.State);

		allocLog("Failed to get the required address range");
		assert(false);
		return nullptr;
	}

	printf( "  allocating %zd\n", size );
	return static_cast<uint8_t*>(ptr);
}

/*static*/
void VirtualMemory::commit(uintptr_t addr, size_t size)
{
	void* ptr = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_COMMIT, PAGE_READWRITE);
	++commitCtr;
	commitSz += size;
	assert(ptr);
//	printf( "  allocating %zd\n", size );
	nowAllocated += size;
	if ( maxAllocated < nowAllocated )
		maxAllocated = nowAllocated;
}

/*static*/
void VirtualMemory::decommit(uintptr_t addr, size_t size)
{
	BOOL r = VirtualFree(reinterpret_cast<void*>(addr), size, MEM_DECOMMIT);
	assert(r);
	decommitSz += size;
	++decommitCtr;
//	printf( "deallocating                     %zd\n", size );
	if ( size < 0x10000000 ) // excluding 1GB of initial allocation
	{
//		assert( nowAllocated >= size );
		nowAllocated -= size;
	}
}

void* VirtualMemory::allocate(size_t size)
{
	void* ptr = VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!ptr)
		throw std::bad_alloc();

	++commitCtr;
	commitSz += size;

	return ptr;
}

void VirtualMemory::deallocate(void* ptr, size_t size)
{
	bool OK = VirtualFree(ptr, 0, MEM_RELEASE);
//	bool OK = VirtualFree(ptr, size, MEM_RELEASE);
	assert( OK );
	decommitSz += size;
	++decommitCtr;
}



//void SerializableAllocatorBase::deserialize(const std::string& fileName)
//{
//	assert(false);//TODO
//	/* On replay we avoid using heap before mmap
//		* so we have the best chances that heap address
//		* we need does not get used.
//		* So we read the header on the stack
//		*/
//	assert(!heap);
//	allocLog("SerializableAllocatorBase::deserialize fileName='{}'", fileName);
//
//	//int fd = open(fileName.c_str(), O_RDONLY);
//	//assert(fd != -1);
//	std::wstring wsTmp(fileName.begin(), fileName.end());
//	HANDLE fHandle = CreateFile(wsTmp.c_str(),
//		GENERIC_READ,
//		0,
//		NULL,
//		OPEN_EXISTING,
//		FILE_ATTRIBUTE_NORMAL,
//		NULL);
//	assert(fHandle != INVALID_HANDLE_VALUE);
//	
//	//First read header to a temporary location
//	Heap tmp;
//	//ssize_t c = read(fd, &tmp, sizeof(tmp));
//	//assert(c == sizeof(tmp));
//	DWORD readed = 0;
//	BOOL r = ReadFile(fHandle, &tmp, sizeof(tmp), &readed, NULL);
//	assert(r && readed == sizeof(tmp));
//	assert(tmp.magic == SerializableAllocatorMagic.asUint);
//// 		assert(tmp->usedSize <= bufferSize);
//	
//	//void* ptr = mmap(tmp.begin, tmp.allocSize, PROT_READ|PROT_WRITE,
//	//					MAP_PRIVATE|MAP_NORESERVE/*|MAP_FIXED*/, fd, 0);
//
//	//Now we can reserve all memory
////	void* ptr = VirtualMemory::reserve(reinterpret_cast<void*>(tmp.begin), tmp.getReservedSize());
//	
//	//Last, reset file and re-read everything
//	DWORD p = SetFilePointer(fHandle, 0, 0, FILE_BEGIN);
//	assert(p != INVALID_SET_FILE_POINTER);
//
//	
//	//BOOL rf = ReadFile(fHandle, ptr, static_cast<DWORD>(tmp.getReservedSize()), &readed, NULL);
//	//assert(rf && static_cast<size_t>(readed) == tmp.getReservedSize());
//
//	CloseHandle(fHandle);
//
////	heap = reinterpret_cast<Heap*>(ptr);
//	//allocLog("Aslr anchorPoint {}", (void*)&anchorPoint);
//	//arena->fixAslrVtables(&anchorPoint);
//	//arenaEnd = reinterpret_cast<uint8_t*>(ptr) + tmp.getReservedSize();
//}

