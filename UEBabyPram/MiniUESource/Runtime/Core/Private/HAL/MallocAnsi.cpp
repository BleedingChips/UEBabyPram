// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MallocAnsi.cpp: Binned memory allocator
=============================================================================*/

#include "HAL/MallocAnsi.h"
#include "Misc/AssertionMacros.h"
#include "Math/UnrealMathUtility.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "Templates/AlignmentTemplates.h"
#include "AutoRTFM.h"

#if PLATFORM_USE_ANSI_POSIX_MALLOC
	#include <malloc.h>
#endif

#if PLATFORM_IOS
	#include "mach/mach.h"
#endif

#if PLATFORM_WINDOWS
	#include "Windows/WindowsHWrapper.h"
#endif

#define MALLOC_ANSI_USES__ALIGNED_MALLOC !UE_AUTORTFM && PLATFORM_USES__ALIGNED_MALLOC

void* AnsiMalloc(SIZE_T Size, uint32 Alignment)
{
#if MALLOC_ANSI_USES__ALIGNED_MALLOC
	void* Result = _aligned_malloc( Size, Alignment );
#elif PLATFORM_USE_ANSI_POSIX_MALLOC
	void* Result;
	if (UNLIKELY(posix_memalign(&Result, Alignment, Size) != 0))
	{
		Result = nullptr;
	}
#elif PLATFORM_USE_ANSI_MEMALIGN
	void* Result = memalign(Alignment, Size);
#else
	void* Ptr = malloc(Size + Alignment + sizeof(void*) + sizeof(SIZE_T));
	void* Result = nullptr;
	if (Ptr)
	{
		Result = Align((uint8*)Ptr + sizeof(void*) + sizeof(SIZE_T), Alignment);
		 *((void**)((uint8*)Result - sizeof(void*))) = Ptr;
		*((SIZE_T*)((uint8*)Result - sizeof(void*) - sizeof(SIZE_T))) = Size;
	}
#endif
	MemoryTrace_Alloc(uint64(Result), Size, Alignment, EMemoryTraceRootHeap::SystemMemory);

	return Result;
}

static SIZE_T AnsiGetAllocationSize(void* Original)
{
#if MALLOC_ANSI_USES__ALIGNED_MALLOC
	return _aligned_msize(Original, 16, 0); // TODO: incorrectly assumes alignment of 16
#elif PLATFORM_USE_ANSI_POSIX_MALLOC || PLATFORM_USE_ANSI_MEMALIGN
	return malloc_usable_size(Original);
#else
	return *((SIZE_T*)((uint8*)Original - sizeof(void*) - sizeof(SIZE_T)));
#endif
}

void* AnsiRealloc(void* Ptr, SIZE_T NewSize, uint32 Alignment)
{
	void* Result;

#if MALLOC_ANSI_USES__ALIGNED_MALLOC
	MemoryTrace_ReallocFree(uint64(Ptr), EMemoryTraceRootHeap::SystemMemory);
	if (Ptr && NewSize)
	{
		Result = _aligned_realloc(Ptr, NewSize, Alignment);
	}
	else if (Ptr == nullptr)
	{
		Result = _aligned_malloc(NewSize, Alignment);
	}
	else
	{
		_aligned_free(Ptr);
		Result = nullptr;
	}
	MemoryTrace_ReallocAlloc(uint64(Result), NewSize, Alignment, EMemoryTraceRootHeap::SystemMemory);
#elif PLATFORM_USE_ANSI_POSIX_MALLOC
	MemoryTrace_ReallocFree(uint64(Ptr), EMemoryTraceRootHeap::SystemMemory);
	if (Ptr && NewSize)
	{
		SIZE_T UsableSize = malloc_usable_size(Ptr);
		if (UNLIKELY(posix_memalign(&Result, Alignment, NewSize) != 0))
		{
			Result = nullptr;
		}
		else if (LIKELY(UsableSize))
		{
			FMemory::Memcpy(Result, Ptr, FMath::Min(NewSize, UsableSize));
		}
		free(Ptr);
	}
	else if (Ptr == nullptr)
	{
		if (UNLIKELY(posix_memalign(&Result, Alignment, NewSize) != 0))
		{
			Result = nullptr;
		}
	}
	else
	{
		free(Ptr);
		Result = nullptr;
	}
	MemoryTrace_ReallocAlloc(uint64(Result), NewSize, Alignment, EMemoryTraceRootHeap::SystemMemory);
#elif PLATFORM_USE_ANSI_MEMALIGN
	MemoryTrace_ReallocFree(uint64(Ptr), EMemoryTraceRootHeap::SystemMemory);
	Result = reallocalign(Ptr, NewSize, Alignment);
	MemoryTrace_ReallocAlloc(uint64(Result), NewSize, Alignment, EMemoryTraceRootHeap::SystemMemory);
#else
	if (Ptr && NewSize)
	{
		// Can't use realloc as it might screw with alignment.
		Result = AnsiMalloc(NewSize, Alignment);
		SIZE_T PtrSize = AnsiGetAllocationSize(Ptr);
		FMemory::Memcpy(Result, Ptr, FMath::Min(NewSize, PtrSize));
		AnsiFree(Ptr);
	}
	else if (Ptr == nullptr)
	{
		Result = AnsiMalloc(NewSize, Alignment);
	}
	else
	{
		MemoryTrace_Free(uint64(Ptr), EMemoryTraceRootHeap::SystemMemory);
		free(*((void**)((uint8*)Ptr - sizeof(void*))));
		Result = nullptr;
	}
#endif

	return Result;
}

void AnsiFree(void* Ptr)
{
	MemoryTrace_Free(uint64(Ptr), EMemoryTraceRootHeap::SystemMemory);
#if MALLOC_ANSI_USES__ALIGNED_MALLOC
	_aligned_free(Ptr);
#elif PLATFORM_USE_ANSI_POSIX_MALLOC || PLATFORM_USE_ANSI_MEMALIGN
	free(Ptr);
#else
	if (Ptr)
	{
		free(*((void**)((uint8*)Ptr - sizeof(void*))));
	}
#endif
}

FMallocAnsi::FMallocAnsi()
{
#if PLATFORM_WINDOWS
	// Enable low fragmentation heap - http://msdn2.microsoft.com/en-US/library/aa366750.aspx
	intptr_t	CrtHeapHandle = _get_heap_handle();
	ULONG		EnableLFH = 2;
	HeapSetInformation((void*)CrtHeapHandle, HeapCompatibilityInformation, &EnableLFH, sizeof(EnableLFH));
#endif
}

void* FMallocAnsi::TryMalloc(SIZE_T Size, uint32 Alignment)
{
#if !UE_BUILD_SHIPPING
	uint64 LocalMaxSingleAlloc = MaxSingleAlloc.Load(EMemoryOrder::Relaxed);
	if (LocalMaxSingleAlloc != 0 && Size > LocalMaxSingleAlloc)
	{
		return nullptr;
	}
#endif

	Alignment = FMath::Max(Size >= 16 ? (uint32)16 : (uint32)8, Alignment);

	void* Result = AnsiMalloc(Size, Alignment);

	return Result;
}

void* FMallocAnsi::Malloc(SIZE_T Size, uint32 Alignment)
{
	void* Result = TryMalloc(Size, Alignment); 

	if (Result == nullptr && Size)
	{
		FPlatformMemory::OnOutOfMemory(Size, Alignment);
	}

	return Result;
}

void* FMallocAnsi::TryRealloc(void* Ptr, SIZE_T NewSize, uint32 Alignment)
{
#if !UE_BUILD_SHIPPING
	uint64 LocalMaxSingleAlloc = MaxSingleAlloc.Load(EMemoryOrder::Relaxed);
	if (LocalMaxSingleAlloc != 0 && NewSize > LocalMaxSingleAlloc)
	{
		return nullptr;
	}
#endif

	Alignment = FMath::Max(NewSize >= 16 ? (uint32)16 : (uint32)8, Alignment);

	void* Result = AnsiRealloc(Ptr, NewSize, Alignment);

	return Result;
}

void* FMallocAnsi::Realloc( void* Ptr, SIZE_T NewSize, uint32 Alignment )
{
	void* Result = TryRealloc(Ptr, NewSize, Alignment);

	if (Result == nullptr && NewSize != 0)
	{
		FPlatformMemory::OnOutOfMemory(NewSize, Alignment);
	}

	return Result;
}

void FMallocAnsi::Free( void* Ptr )
{
	AnsiFree(Ptr);
}

bool FMallocAnsi::GetAllocationSize(void* Original, SIZE_T& SizeOut)
{
	if (!Original)
	{
		return false;
	}

#if MALLOC_ANSI_USES__ALIGNED_MALLOC
	return false;
#else
	SizeOut = AnsiGetAllocationSize(Original);
	return true;
#endif
}

bool FMallocAnsi::IsInternallyThreadSafe() const
{
#if PLATFORM_IS_ANSI_MALLOC_THREADSAFE
		return true;
#else
		return false;
#endif
}

bool FMallocAnsi::ValidateHeap()
{
#if PLATFORM_WINDOWS
	int32 Result = _heapchk();
	check(Result != _HEAPBADBEGIN);
	check(Result != _HEAPBADNODE);
	check(Result != _HEAPBADPTR);
	check(Result != _HEAPEMPTY);
	check(Result == _HEAPOK);
#endif
	return true;
}
