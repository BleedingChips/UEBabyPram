// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"

namespace AutoRTFM
{

UE_AUTORTFM_API extern autortfm_extern_api GExternAPI;

inline void* Allocate(size_t Size, size_t Alignment)
{
	return GExternAPI.Allocate(Size, Alignment);
}

inline void* Reallocate(void* Pointer, size_t Size, size_t Alignment)
{
	return GExternAPI.Reallocate(Pointer, Size, Alignment);
}

inline void* AllocateZeroed(size_t Size, size_t Alignment)
{
	return GExternAPI.AllocateZeroed(Size, Alignment);
}

inline void Free(void* Pointer)
{
	return GExternAPI.Free(Pointer);
}

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
