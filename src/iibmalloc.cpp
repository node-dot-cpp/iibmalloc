 /* -------------------------------------------------------------------------------
 * Copyright (c) 2018-2021, OLogN Technologies AG
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

#include <platform_base.h>
#include <nodecpp_assert.h>
#include "iibmalloc.h"

#ifdef NODECPP_NOT_USING_IIBMALLOC

namespace nodecpp::iibmalloc
{
	thread_local ThreadLocalAllocatorT* g_CurrentAllocManager = nullptr;
} // namespace nodecpp::iibmalloc

#else

namespace nodecpp::iibmalloc
{
	std::atomic<uint16_t> SafeIibAllocator::allocatorIDBase;

	thread_local ThreadLocalAllocatorT* g_CurrentAllocManager = nullptr;

	ThreadLocalAllocatorT* setCurrneAllocator( ThreadLocalAllocatorT* allocator )
	{
		ThreadLocalAllocatorT* ret = g_CurrentAllocManager;
		g_CurrentAllocManager = allocator;
		return ret;
	}
}

using namespace nodecpp::iibmalloc;

#ifndef NODECPP_IIBMALLOC_DISABLE_NEW_DELETE_INTERCEPTION

// We need it as a workaround because of P0302R0
//    http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0302r0.html
// if we want lambda-related allocations to be done with a specific allocator

#include <malloc_based_allocator.h>

void* operator new(std::size_t count)
{
	if ( g_CurrentAllocManager )
	{
		return g_CurrentAllocManager->allocateAligned<__STDCPP_DEFAULT_NEW_ALIGNMENT__>(count);
	}
	else
	{
		void* ret;
		if ( count ) // likely
            ret = malloc(count);
		else
			ret = malloc(++count);

		if ( ret )
			return ret; 
		throw std::bad_alloc{};
	}
}

void* operator new(std::size_t count, std::align_val_t al)
{
	void* ret;
	if ( g_CurrentAllocManager )
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= ThreadLocalAllocatorT::maximalSupportedAlignment, "{} vs. {}", (size_t)al, ThreadLocalAllocatorT::maximalSupportedAlignment );
		ret = g_CurrentAllocManager->allocateAligned<NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW>(count);
	}
	else
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW, "{} vs. {}", (size_t)al, NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW );
		ret = nodecpp::StdRawAllocator::allocate<NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW>(count);
	}
	NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, ( (size_t)ret & ((size_t)al - 1)) == 0, "ret = 0x{:x}, al = {}", (size_t)ret, (size_t)al );
	return ret; 
}

void* operator new[](std::size_t count)
{
	if ( g_CurrentAllocManager )
		return g_CurrentAllocManager->allocateAligned<__STDCPP_DEFAULT_NEW_ALIGNMENT__>(count);
	else
	{
		void* ret = malloc(count);
		if ( ret )
			return ret; 
		throw std::bad_alloc{};
	}
}

void* operator new[](std::size_t count, std::align_val_t al)
{
	void* ret;
	if ( g_CurrentAllocManager )
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= ThreadLocalAllocatorT::maximalSupportedAlignment, "{} vs. {}", (size_t)al, ThreadLocalAllocatorT::maximalSupportedAlignment );
		ret = g_CurrentAllocManager->allocateAligned<NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW>(count);
	}
	else
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW, "{} vs. {}", (size_t)al, NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW );
		ret = nodecpp::StdRawAllocator::allocate<NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW>(count);
	}
	NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, ( (size_t)ret & ((size_t)al - 1)) == 0, "ret = 0x{:x}, al = {}", (size_t)ret, (size_t)al );
	return ret; 
}

static NODECPP_FORCEINLINE
void operator_delete_impl(void* ptr) noexcept
{
	if ( g_CurrentAllocManager )
		g_CurrentAllocManager->deallocate(ptr);
	else
		free(ptr);
}

static NODECPP_FORCEINLINE
void operator_delete_impl(void* ptr, std::align_val_t al) noexcept
{
	if ( g_CurrentAllocManager )
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= ThreadLocalAllocatorT::maximalSupportedAlignment, "{} vs. {}", (size_t)al, ThreadLocalAllocatorT::maximalSupportedAlignment );
		g_CurrentAllocManager->deallocate(ptr);
	}
	else
	{
		NODECPP_ASSERT( nodecpp::iibmalloc::module_id, nodecpp::assert::AssertLevel::pedantic, (size_t)al <= NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW, "{} vs. {}", (size_t)al, NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW );
		nodecpp::StdRawAllocator::deallocate<NODECPP_MAX_SUPPORTED_ALIGNMENT_FOR_NEW>(ptr);
	}
}


void operator delete(void* ptr) noexcept
{
	operator_delete_impl(ptr);
}

void operator delete(void* ptr, std::align_val_t al) noexcept
{
	operator_delete_impl(ptr, al);
}

void operator delete[](void* ptr) noexcept
{
	operator_delete_impl(ptr);
}

void operator delete[](void* ptr, std::align_val_t al) noexcept
{
	operator_delete_impl(ptr, al);
}

// mb:sized deletes to make gcc happy, and to improve completeness,
// otherwise an operator delete of the std may get called and break things
void operator delete  ( void* ptr, std::size_t sz ) noexcept
{
	operator_delete_impl(ptr);
}

void operator delete[]( void* ptr, std::size_t sz ) noexcept
{
	operator_delete_impl(ptr);
}

void operator delete  ( void* ptr, std::size_t sz, std::align_val_t al ) noexcept
{
	operator_delete_impl(ptr, al);
}

void operator delete[]( void* ptr, std::size_t sz, std::align_val_t al ) noexcept
{
	operator_delete_impl(ptr, al);
}



#endif // NODECPP_IIBMALLOC_DISABLE_NEW_DELETE_INTERCEPTION

#if __cplusplus >= 201703L

//We don't support alignment new/delete yet

//void* operator new(std::size_t count, std::align_val_t alignment);
//void* operator new[](std::size_t count, std::align_val_t alignment);
//void operator delete(void* ptr, std::align_val_t al) noexcept;
//void operator delete[](void* ptr, std::align_val_t al) noexcept;
#endif




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

#endif // NODECPP_NOT_USING_IIBMALLOC
