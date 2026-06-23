// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreMiscDefines.h"

#if USING_INSTRUMENTATION

#include "CoreTypes.h"
#include "Sanitizer/RaceDetectorTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRaceDetector, Log, All);

namespace UE::Sanitizer::RaceDetector {

	// Returns whether we should instrument depending on the current context state.
	bool ShouldInstrument(FContext& Context);
	// Gets the current thread context, could be nullptr.
	FContext* GetThreadContext();
	// Hints the sanitizer that this memory range is being freed.
	void FreeMemoryRange(void* Ptr, uint64 Size);

	// Makes sure the current thread has a context and returns it.
	FContext& EnsureCurrentContext();
	// Releases the current thread context.
	void ReleaseCurrentContext();
	// Returns a sync object for the given address, initialize one if there isn't one already.
	FSyncObjectRef GetSyncObject(FContext& Context, void* SyncAddr);
}

#endif // USING_INSTRUMENTATION