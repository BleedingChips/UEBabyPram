// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Context.h"
#include "CallNest.h"

namespace AutoRTFM
{

template<typename TTryFunctor>
void FCallNest::Try(const TTryFunctor& TryFunctor)
{
	AbortJump.TryCatch(
		[&]()
		{
			TryFunctor();
			AUTORTFM_ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
		},
		[&]()
		{
			AUTORTFM_ASSERT(Context->GetStatus() != EContextStatus::Idle);
			AUTORTFM_ASSERT(Context->GetStatus() != EContextStatus::OnTrack);
		});
}

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
