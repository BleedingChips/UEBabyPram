// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/CoreMiscDefines.h"

// For monolithic build, we include the per module inline only once in the core module.
#if IS_MONOLITHIC && USING_INSTRUMENTATION
#include "Instrumentation/PerModuleInline.inl"
#endif