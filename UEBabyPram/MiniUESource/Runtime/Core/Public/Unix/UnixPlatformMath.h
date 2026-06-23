// Copyright Epic Games, Inc. All Rights Reserved.
// IWYU pragma: begin_exports

/*=============================================================================================
	LinuxPlatformMath.h: Linux platform Math functions
==============================================================================================*/

#pragma once

#include "CoreTypes.h"
#include "Clang/ClangPlatformMath.h"
#include "Linux/LinuxSystemIncludes.h"
#include "Math/UnrealPlatformMathSSE4.h"

/**
* Linux implementation of the Math OS functions
**/
struct FLinuxPlatformMath : public TUnrealPlatformMathSSE4Base<FClangPlatformMath>
{
#if PLATFORM_ENABLE_VECTORINTRINSICS
	static UE_FORCEINLINE_HINT bool IsNaN( float A ) { return isnan(A) != 0; }
	static UE_FORCEINLINE_HINT bool IsNaN( double A ) { return isnan(A) != 0; }
	static UE_FORCEINLINE_HINT bool IsFinite( float A ) { return isfinite(A); }
	static UE_FORCEINLINE_HINT bool IsFinite( double A ) { return isfinite(A); }

#if PLATFORM_ENABLE_POPCNT_INTRINSIC
	/**
	 * Use the SSE instruction to count bits
	 */
	static UE_FORCEINLINE_HINT int32 CountBits(uint64 Bits)
	{
		return __builtin_popcountll(Bits);
	}
#endif
#endif
};

typedef FLinuxPlatformMath FPlatformMath;

// IWYU pragma: end_exports
