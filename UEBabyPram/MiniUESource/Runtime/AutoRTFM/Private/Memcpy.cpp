// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Memcpy.h"

#include "ContextInlines.h"
#include "Utils.h"

namespace AutoRTFM
{

void* MemcpyToNew(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
	AUTORTFM_VERBOSE("MemcpyToNew(%p, %p, %zu)", InDst, InSrc, Size);
    return memcpy(InDst, InSrc, Size);
}

void* Memcpy(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
	AUTORTFM_VERBOSE("Memcpy(%p, %p, %zu)", InDst, InSrc, Size);
    Context->RecordWrite(InDst, Size);
    return memcpy(InDst, InSrc, Size);
}

void* Memmove(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
	AUTORTFM_VERBOSE("Memmove(%p, %p, %zu)", InDst, InSrc, Size);
	Context->RecordWrite(InDst, Size);
	return memmove(InDst, InSrc, Size);
}

void* Memset(void* InDst, int Value, size_t Size, FContext* Context)
{
	AUTORTFM_VERBOSE("Memset(%p, %d, %zu)", InDst, Value, Size);
	Context->RecordWrite(InDst, Size);
	return memset(InDst, Value, Size);
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
