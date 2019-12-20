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
 * iibmalloc allocator coomon defs and routines
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef IIBMALLOC_COMMON_H
#define IIBMALLOC_COMMON_H

#include <cstddef>
#include <cinttypes>
#include <memory>
#include <array>

#include <foundation.h>
#include <nodecpp_assert.h>


#ifdef NODECPP_MSVC
#include <intrin.h>
#endif

namespace nodecpp
{
	constexpr const char* iibmalloc_module_id = "iibmalloc";
}
namespace nodecpp::iibmalloc
{
	constexpr uint64_t module_id = 2;


	// HELPER FUNCTIONS

	template<size_t IX>
	constexpr
	uint8_t sizeToExpImpl(size_t sz)
	{
		return (sz == ((1ull) << (IX))) ? IX : sizeToExpImpl<IX - 1>(sz);
	}

	template<>
	constexpr
	uint8_t sizeToExpImpl<0>(size_t sz)
	{
		return 0; // error?
	}

	NODECPP_FORCEINLINE constexpr
	uint8_t sizeToExp(size_t sz)
	{
		// keep it reasonable!
		return sizeToExpImpl<32>(sz);
	}

	NODECPP_FORCEINLINE constexpr
	size_t expToSize(uint8_t exp)
	{
		return static_cast<size_t>(1) << exp;
	}

	static_assert(sizeToExp(64 * 1024) == 16, "broken!");

	NODECPP_FORCEINLINE constexpr
	size_t expToMask(size_t sz)
	{
		// keep it reasonable!
		return (static_cast<size_t>(1) << sz) - 1;
	}


	NODECPP_FORCEINLINE constexpr
	bool isAlignedMask(uintptr_t sz, uintptr_t alignmentMask)
	{
		return (sz & alignmentMask) == 0;
	}

	NODECPP_FORCEINLINE constexpr
	uintptr_t alignDownExp(uintptr_t sz, uintptr_t alignmentExp)
	{
		return ( sz >> alignmentExp ) << alignmentExp;
	}

	inline constexpr
	bool isAlignedExp(uintptr_t sz, uintptr_t alignment)
	{
		return alignDownExp(sz, alignment) == sz;
	}

	NODECPP_FORCEINLINE constexpr
	uintptr_t alignUpMask(uintptr_t sz, uintptr_t alignmentMask)
	{
		return (( sz & alignmentMask) == 0) ? sz : sz - (sz & alignmentMask) + alignmentMask + 1;
	}
	inline constexpr
	uintptr_t alignUpExp(uintptr_t sz, uintptr_t alignmentExp)
	{
		return ( ((uintptr_t)(-((intptr_t)((((uintptr_t)(-((intptr_t)sz))))) >> alignmentExp ))) << alignmentExp);
	}
} // namespace nodecpp::iibmalloc


#endif // IIBMALLOC_COMMON_H
