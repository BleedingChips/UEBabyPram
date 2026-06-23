// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformMutex.h"
#include "HAL/PThreadsRecursiveMutex.h"
#include "HAL/PThreadsSharedMutex.h"

namespace UE
{

using FPlatformRecursiveMutex = FPThreadsRecursiveMutex;
using FPlatformSharedMutex = FPThreadsSharedMutex;
using FPlatformSystemWideMutex = FPlatformSystemWideMutexNotImplemented;

} // UE
