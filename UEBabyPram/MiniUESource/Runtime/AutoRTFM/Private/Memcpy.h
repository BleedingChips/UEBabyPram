// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFMDefines.h"

#include <stdlib.h>

namespace AutoRTFM
{

class FContext;

AUTORTFM_DISABLE void* MemcpyToNew(void* Dst, const void* Src, size_t Size, FContext* Context);
AUTORTFM_DISABLE void* Memcpy(void* Dst, const void* Src, size_t Size, FContext* Context);
AUTORTFM_DISABLE void* Memmove(void* Dst, const void* Src, size_t Size, FContext* Context);
AUTORTFM_DISABLE void* Memset(void* Dst, int Value, size_t Size, FContext* Context);

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
