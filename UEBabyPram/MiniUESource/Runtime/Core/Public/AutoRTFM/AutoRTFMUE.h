// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Include Platform.h before AutoRTFM.h so that DLLEXPORT / DLLIMPORT is defined.
#include "HAL/Platform.h"

#include "AutoRTFM.h"

namespace AutoRTFM
{

#if UE_AUTORTFM

// Initializes AutoRTFM for use by the Unreal Engine.
UE_AUTORTFM_API void InitializeForUE();

#else

inline void InitializeForUE() {}

#endif

}
