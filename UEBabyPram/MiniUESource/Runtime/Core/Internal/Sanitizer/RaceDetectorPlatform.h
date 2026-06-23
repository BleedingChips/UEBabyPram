// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreMiscDefines.h"

#if USING_INSTRUMENTATION

#include "CoreTypes.h"

namespace UE::Sanitizer::RaceDetector {
	namespace Platform {
		// Performs any platform specific initialization.
		bool  InitializePlatform();
		// Performs any platform specific cleanup.
		bool  CleanupPlatform();
		// Prepare address space of shadow memory
		void  InitShadowMemory();
		// Returns the command line the process was started with.
		const TCHAR* GetCommandLine();
		// Returns the page size granularity.
		UPTRINT GetPageSize();
		// Returns the base address of the shadow memory address space. 
		// Note: InitShadowMemory must have been called before, otherwise this is meaningless.
		UPTRINT GetShadowMemoryBase();
		// Returns the size of the entire shadow memory address space.
		// Note: InitShadowMemory must have been called before, otherwise this is meaningless.
		UPTRINT GetShadowMemorySize();
		// Get the base of the shadow memory for clock banks.
		UPTRINT GetShadowClockBase();
		// Returns the number of bytes currently mapped in the shadow memory.
		uint64 GetShadowMemoryUsage();
		// Returns whether a particular range is already accessible in shadow memory.
		bool IsShadowMemoryMapped(UPTRINT Base, UPTRINT Size);
		// Maps a range in shadow memory so that it is safe to access.
		void MapShadowMemory(UPTRINT Base, UPTRINT Size);
		// Unmaps the entire range of shadow memory.
		void UnmapShadowMemory();
		// Returns whether a debugger is currently attached to our process.
		bool IsDebuggerPresent();
		// Returns whether there is any page currently mapped in shadow memory.
		bool HasShadowMemoryMapped();
		// Sends a hint to visual studio to hide first chance exception to reduce noise caused by shadow memory access.
		void HideFirstChanceExceptionInVisualStudio();
		// Sleeps for the given amount of milliseconds.
		void SleepMS(uint32 Milliseconds);
		// Capture the current callstack.
		uint16 CaptureStackBackTrace(uint32 FrameToSkip, uint32 FrameToCapture, void** Backtrace);
		// Allocates a Tls index.
		uint32 AllocTlsSlot();
		// Releases a Tls index.
		void FreeTlsSlot(uint32 Index);
		// Gets the value of the Tls index for the current thread.
		void* GetTlsValue(uint32 Index);
		// Sets the value of the Tls index for the current thread.
		void SetTlsValue(uint32 Index, void* Value);
		// Returns the current thread Id.
		uint32 GetCurrentThreadId();

		// Rewrites the patchable function prefix with the proper jmp to the target.
		void PrepareTrampoline(void* PatchableFunctionAddress, void* TargetFunctionAddress, bool bUseRETBypass);
		// Rewrites the patchable function prefix back to its original compiled NOPs.
		void CleanupTrampoline(void* PatchableFunctionAddress);
		// Rewrites the 2 first bytes of the function to jump at the beginning of the prefix section.
		void ActivateTrampoline(void* PatchableFunctionAddress);
		// Rewrites the 2 first bytes of the function to either do nothing or do an immediate return (RET bypass).
		void DeactivateTrampoline(void* PatchableFunctionAddress, bool bUseRETBypass);
		// This needs to be called after trampoline activation or deactivation to make sure it takes effect immediately.
		void FlushInstructionCache();
		// Get the limits of the stack for the current thread.
		void GetCurrentThreadStackLimits(void** LowLimit, void** HighLimit);
		// Check if the thread id given is currently alive
		bool IsThreadAlive(uint32 ThreadId);
		// Provides a compilation barrier
		FORCEINLINE void AsymmetricThreadFenceLight()
		{
			_ReadWriteBarrier();
		}

		// The function generates an interprocessor interrupt (IPI) to all processors that are part of the current process affinity.
		// It guarantees the visibility of write operations performed on one processor to the other processors.
		void AsymmetricThreadFenceHeavy();
	}
}

#endif // USING_INSTRUMENTATION