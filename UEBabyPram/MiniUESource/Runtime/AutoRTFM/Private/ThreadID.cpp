// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "ThreadID.h"

#include <thread>

namespace AutoRTFM
{

const FThreadID FThreadID::Invalid;

UE_AUTORTFM_ALWAYS_OPEN
FThreadID FThreadID::GetCurrent()
{
	return FThreadID{std::this_thread::get_id()};
}

}

#endif // (defined(__AUTORTFM) && __AUTORTFM)
