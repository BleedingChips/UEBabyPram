// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreMiscDefines.h"

#if USING_INSTRUMENTATION

#include "Sanitizer/Types.h"
#include "Misc/Timeout.h"

namespace UE::Sanitizer::RaceDetector
{
	// Setup everything that is required for the race detector.
	CORE_API bool Initialize();
	// Cleanup everything about the race detector that was setup in Initialize.
	CORE_API bool Shutdown();

	typedef TFunctionRef<void(uint64 RaceAddress, uint32 FirstThreadId, uint32 SecondThreadId, const FFullLocation& FirstLocation, const FFullLocation& SecondLocation)> TRaceCallbackFn;
	// Set a callback that will be called whenever a race is detected (filters will not apply).
	// NOTE: It is not safe to perform any allocation within the callback since it will be
	//       called from instrumentation code.
	CORE_API void SetRaceCallbackFn(TRaceCallbackFn CallbackFn);
	// Remove any callback that was previously set.
	CORE_API void ResetRaceCallbackFn();

	// Dump the current thread context to help with debugging.
	CORE_API void DumpContext();
	// Dump the current thread context but with additional info.
	CORE_API void DumpContextDetailed();

	// Means that races that doesn't involve the current thread will be filtered.
	CORE_API void ToggleFilterOtherThreads(bool bEnable);

	// Toggle race detection until a call to the same function that toggles it back off.
	// If multiple threads activate race detection at the same time, it will be turned off when all thread toggle it back to off.
	// The grace period is how much time to wait until race detection is really turned off. Can be used when toggling is expected to happen too often or too fast.
	CORE_API void ToggleRaceDetection(bool bEnable, float GracePeriodSeconds = 0.0f);
	// Toggle filter on duplicate races. Does not apply when a callback is set.
	CORE_API void ToggleFilterDuplicateRaces(bool bEnable);
	// Activate race detection for certain time after which it will be automatically turned off.
	CORE_API void ToggleRaceDetectionUntil(UE::FTimeout Timeout);

	// Toggle detailed logging globally for all threads. Very slow!
	CORE_API void ToggleGlobalDetailedLog(bool bEnable);
	// Toggle detailed logging for the current thread.
	CORE_API void ToggleThreadDetailedLog(bool bEnable);
	// Toggle detailed logging filter to only log activity for a specific address.
	// NOTE: Set to nullptr to remove filtering.
	CORE_API void ToggleFilterDetailedLogOnAddress(void* Ptr);
	// Returns whether the race detector is currently active or not.
	CORE_API bool IsActive();
	
	class FRaceDetectorScope
	{
		bool bConditionalActivation;
		float GracePeriodSeconds;
	public:
		FRaceDetectorScope(bool bInConditionalActivation = true, float InGracePeriodSeconds = 0.0f)
			: bConditionalActivation(bInConditionalActivation)
			, GracePeriodSeconds(InGracePeriodSeconds)
		{
			if (bConditionalActivation)
			{
				ToggleRaceDetection(true);
			}
		}

		~FRaceDetectorScope()
		{
			if (bConditionalActivation)
			{
				ToggleRaceDetection(false, GracePeriodSeconds);
			}
		}
	};


} // UE::Sanitizer::RaceDetector

#endif