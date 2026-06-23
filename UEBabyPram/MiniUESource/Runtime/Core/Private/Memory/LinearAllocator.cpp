// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/LinearAllocator.h"
#include "BuildSettings.h"

CORE_API FPersistentLinearAllocatorExtends GPersistentLinearAllocatorExtends;

static constexpr SIZE_T LinearAllocatorBlockSize = 64 * 1024;


#if UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

#include "CoreGlobals.h"
#include "Misc/ScopeLock.h"
#include "HAL/LowLevelMemTracker.h"

struct FPersistentLinearAllocator : public FLinearAllocator
{
	FPersistentLinearAllocator(SIZE_T ReserveMemorySize)
		: FLinearAllocator(ReserveMemorySize)
	{
		GPersistentLinearAllocatorExtends.Address = (uint64)VirtualMemory.GetVirtualPointer();
		GPersistentLinearAllocatorExtends.Size = (uint64)Reserved;
	}
};
 
FLinearAllocator::FLinearAllocator(SIZE_T ReserveMemorySize)
	: Reserved(ReserveMemorySize)
{
	if (FPlatformMemory::CanOverallocateVirtualMemory() && ReserveMemorySize)
	{
		VirtualMemory = VirtualMemory.AllocateVirtual(ReserveMemorySize);
		if (!VirtualMemory.GetVirtualPointer())
		{
			UE_LOG(LogMemory, Warning, TEXT("LinearAllocator failed to reserve %" SIZE_T_FMT " MB and will default to FMemory::Malloc instead"), ReserveMemorySize / 1024 / 1024);
			Reserved = 0;
		}
	}
	else
	{
#if PLATFORM_IOS || PLATFORM_TVOS
		UE_LOG(LogMemory, Warning, TEXT("LinearAllocator requires com.apple.developer.kernel.extended-virtual-addressing entitlement to work"));
#else
		UE_LOG(LogMemory, Warning, TEXT("This platform does not allow to allocate more virtual memory than there is physical memory. LinearAllocator will default to FMemory::Malloc instead"));
#endif
		Reserved = 0;
	}
}

void* FLinearAllocator::Allocate(SIZE_T Size, uint32 Alignment)
{
	Alignment = FMath::Max(Alignment, 8u);
	{
		void* Mem = nullptr;
		{
			FScopeLock AutoLock(&Lock);
			if (CanFit(Size, Alignment))
			{
				CurrentOffset = Align(CurrentOffset, Alignment);
				const SIZE_T NewOffset = CurrentOffset + Size;
				if (NewOffset > Committed)
				{
					LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
					const SIZE_T ToCommit = Align(NewOffset - Committed, FMath::Max((SIZE_T)VirtualMemory.GetCommitAlignment(), LinearAllocatorBlockSize));
					VirtualMemory.Commit(Committed, ToCommit);
					Committed += ToCommit;
					LLM_IF_ENABLED(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, (uint8*)VirtualMemory.GetVirtualPointer() + CurrentOffset, ToCommit));
				}
				Mem = (uint8*)VirtualMemory.GetVirtualPointer() + CurrentOffset;
				CurrentOffset += Size;
			}
		}
		if (Mem)
		{
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Mem, Size, ELLMTag::Untagged, ELLMAllocType::FMalloc));
			return Mem;
		}
	}

	static bool bOnce = false;
	if (!bOnce)
	{
		UE_LOG(LogMemory, Warning, TEXT("LinearAllocator exceeded %" SIZE_T_FMT " MB it reserved. Please tune PersistentAllocatorReserveSizeMB setting in [MemoryPools] ini group. Falling back to FMemory::Malloc"), Reserved / 1024 / 1024);
		bOnce = true;
	}
	return FMemory::Malloc(Size, Alignment);
}

bool FLinearAllocator::CanFit(SIZE_T Size, uint32 Alignment) const
{
	return (Reserved - Align(CurrentOffset, Alignment)) >= Size;
}

bool FLinearAllocator::ContainsPointer(const void* Ptr) const
{
	return (uintptr_t)Ptr - (uintptr_t)VirtualMemory.GetVirtualPointer() < Reserved;
}

#else

FLinearBlockAllocator::FLinearBlockAllocator(SIZE_T)
{
	AllocateNewBlock(LinearAllocatorBlockSize);
	FirstHeader = CurrentHeader;
}

FLinearBlockAllocator::~FLinearBlockAllocator()
{
	while (FirstHeader)
	{
		FBlock* Next = FirstHeader->Next;
		CurrentBlock = (uint8*)FirstHeader + sizeof(FBlock) - CurrentHeader->LinearAllocatorBlockSize;
		FMemory::Free(CurrentBlock);
		FirstHeader = Next;
	}
}

void* FLinearBlockAllocator::Allocate(SIZE_T Size, uint32 Alignment)
{
	FScopeLock AutoLock(&Lock);
	if (!CanFit(Size, Alignment))
	{
		//TODO: if Size >= BlockSize, allocate a new block and link it between curent and previous block
		AllocateNewBlock(Size + sizeof(FBlock));
	}

	CurrentOffset = Align(CurrentOffset, Alignment);
	uint8* Mem = &CurrentBlock[CurrentOffset];
	CurrentOffset += Size;

	return Mem;
}

void FLinearBlockAllocator::AllocateNewBlock(SIZE_T Size)
{
	Size = Align(Size, LinearAllocatorBlockSize);
	FBlock* PreviousHeader = CurrentHeader;

	CurrentBlock = (uint8*)FMemory::Malloc(Size);
	CurrentHeader = (FBlock*)(CurrentBlock + Size - sizeof(FBlock));
	CurrentHeader->Next = nullptr;
	CurrentHeader->LinearAllocatorBlockSize = Size;

	if (PreviousHeader)
	{
		PreviousHeader->Next = CurrentHeader;
	}

	CurrentOffset = 0;
	CurrentBlockSize = Size - sizeof(FBlock);
	TotalAllocated += Size;
}

bool FLinearBlockAllocator::CanFit(SIZE_T Size, uint32 Alignment) const
{
	return (CurrentBlockSize - Align(CurrentOffset, Alignment)) >= Size;
}

typedef FLinearBlockAllocator FPersistentLinearAllocator;

#endif //~UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

FLinearAllocator& GetPersistentLinearAllocator()
{
	// We have to make sure that PersistentLinearAllocator always reserves the amount of memory that's not multiple of 2 MB as it causes issues on platforms with transparent large pages
	static FPersistentLinearAllocator GPersistentLinearAllocator(BuildSettings::GetPersistentAllocatorReserveSize() + 64 * 1024);
	return GPersistentLinearAllocator;
}