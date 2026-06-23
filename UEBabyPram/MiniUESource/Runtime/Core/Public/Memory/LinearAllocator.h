// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "HAL/UnrealMemory.h"
#include "HAL/CriticalSection.h"

#ifndef UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR
#	define UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR PLATFORM_HAS_FPlatformVirtualMemoryBlock
#endif


#if UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

struct FLinearAllocator
{
	CORE_API FLinearAllocator(SIZE_T ReserveMemorySize);
	UE_FORCEINLINE_HINT ~FLinearAllocator()
	{
		VirtualMemory.FreeVirtual();
	}

	CORE_API void* Allocate(SIZE_T Size, uint32 Alignment = 8);

	UE_FORCEINLINE_HINT SIZE_T GetAllocatedMemorySize() const
	{
		return Committed;
	}

protected:
	FCriticalSection Lock;
	FPlatformMemory::FPlatformVirtualMemoryBlock VirtualMemory;
	SIZE_T Reserved;
	SIZE_T Committed = 0;
	SIZE_T CurrentOffset = 0;

	bool CanFit(SIZE_T Size, uint32 Alignment) const;
	bool ContainsPointer(const void* Ptr) const;
};

#else

struct FLinearBlockAllocator
{
	CORE_API FLinearBlockAllocator(SIZE_T);
	CORE_API ~FLinearBlockAllocator();

	CORE_API void* Allocate(SIZE_T Size, uint32 Alignment = 8);

	UE_FORCEINLINE_HINT SIZE_T GetAllocatedMemorySize() const
	{
		return TotalAllocated;
	}

private:
	struct FBlock
	{
		FBlock* Next;
		SIZE_T BlockSize;
	};

	FCriticalSection Lock;
	FBlock* FirstHeader = nullptr;
	FBlock* CurrentHeader = nullptr;
	uint8* CurrentBlock = nullptr;
	SIZE_T CurrentOffset = 0;
	SIZE_T CurrentBlockSize = 0;
	SIZE_T TotalAllocated = 0;

	void AllocateNewBlock(SIZE_T Size);
	bool CanFit(SIZE_T Size, uint32 Alignment) const;
};

typedef FLinearBlockAllocator FLinearAllocator;

#endif	//~UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

CORE_API FLinearAllocator& GetPersistentLinearAllocator();

struct FPersistentLinearAllocatorExtends
{
	uint64 Address = 0;
	uint64 Size = 0;
};

// Special case for the FPermanentObjectPoolExtents to reduce the amount of pointer dereferencing
extern CORE_API FPersistentLinearAllocatorExtends GPersistentLinearAllocatorExtends;