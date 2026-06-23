// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM.h"

#if UE_AUTORTFM
#include "Async/TransactionallySafeRecursiveMutex.h"
#else
#include "HAL/CriticalSection.h"
#endif // UE_AUTORTFM

#if UE_AUTORTFM
using FTransactionallySafeCriticalSection = ::UE::FTransactionallySafeRecursiveMutex;
#else
using FTransactionallySafeCriticalSection = ::FCriticalSection;
#endif // UE_AUTORTFM

