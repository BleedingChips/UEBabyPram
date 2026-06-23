// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFMDefines.h"
#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"
#include "BuildMacros.h"
#include "ContextInlines.h"
#include "ExternAPI.h"
#include "FunctionMapInlines.h"
#include "Memcpy.h"

namespace AutoRTFM
{

#if !AUTORTFM_BUILD_SHIPPING

// Check for writes to null in development code, so that the inevitable crash will occur
// in the caller's code rather than in the AutoRTFM runtime.
#define UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr)     \
    do                                         \
    {                                          \
        if (AUTORTFM_UNLIKELY(Ptr == nullptr)) \
        {                                      \
            return;                            \
        }                                      \
    }                                          \
    while (0)

#else

// In shipping code, we don't want to spend any cycles on a redundant check.
// We do want the compiler to optimize as if the pointer is non-null, though.
#define UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr)  \
    UE_ASSUME(Ptr != nullptr)

#endif

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_write(void* Ptr, size_t Size) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);
	FContext* Context = FContext::Get();
	Context->RecordWrite(Ptr, Size);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_write_1(void* Ptr) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);
	FContext* Context = FContext::Get();
	Context->RecordWrite<1>(Ptr);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_write_2(void* Ptr) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);
	FContext* Context = FContext::Get();
	Context->RecordWrite<2>(Ptr);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_write_4(void* Ptr) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);
	FContext* Context = FContext::Get();
	Context->RecordWrite<4>(Ptr);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_write_8(void* Ptr) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);
	FContext* Context = FContext::Get();
	Context->RecordWrite<8>(Ptr);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_record_masked_write(void* Ptr, uintptr_t Mask, int MaskWidthBits, int ValueSizeBytes) noexcept
{
	UE_AUTORTFM_HANDLE_NULL_WRITE(Ptr);

	char* IncrementablePtr = static_cast<char*>(Ptr);
	for(int i = 0; i < MaskWidthBits; i++)
	{
		if (Mask & (1u << i))
		{
			autortfm_record_write(IncrementablePtr, ValueSizeBytes);
		}

		IncrementablePtr += ValueSizeBytes;
	}
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_load(uint32 AddressSpace, void* DestPointer, uint64 Size, uint64 SourceAddress) noexcept
{
	ForTheRuntime::RedirectedLoad(AddressSpace, DestPointer, Size, SourceAddress);
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN uint64 autortfm_redirected_load_8(uint32 AddressSpace, uint64 SourceAddress) noexcept
{
	uint64 Result = 0;
	ForTheRuntime::RedirectedLoad(AddressSpace, &Result, 8, SourceAddress);
	return Result;
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN uint32 autortfm_redirected_load_4(uint32 AddressSpace, uint64 SourceAddress) noexcept
{
	uint32 Result = 0;
	ForTheRuntime::RedirectedLoad(AddressSpace, &Result, 4, SourceAddress);
	return Result;
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN uint16 autortfm_redirected_load_2(uint32 AddressSpace, uint64 SourceAddress) noexcept
{
	uint16 Result = 0;
	ForTheRuntime::RedirectedLoad(AddressSpace, &Result, 2, SourceAddress);
	return Result;
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN uint8 autortfm_redirected_load_1(uint32 AddressSpace, uint64 SourceAddress) noexcept
{
	uint8 Result = 0;
	ForTheRuntime::RedirectedLoad(AddressSpace, &Result, 1, SourceAddress);
	return Result;
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_store(uint32 AddressSpace, uint64 DestAddress, uint64 Size, const void* SourcePointer) noexcept
{
	ForTheRuntime::RedirectedStore(AddressSpace, DestAddress, Size, SourcePointer);
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_store_8(uint32 AddressSpace, uint64 DestAddress, uint64 Value) noexcept
{
	ForTheRuntime::RedirectedStore(AddressSpace, DestAddress, sizeof(Value), &Value);
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_store_4(uint32 AddressSpace, uint64 DestAddress, uint32 Value) noexcept
{
	ForTheRuntime::RedirectedStore(AddressSpace, DestAddress, sizeof(Value), &Value);
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_store_2(uint32 AddressSpace, uint64 DestAddress, uint16 Value) noexcept
{
	ForTheRuntime::RedirectedStore(AddressSpace, DestAddress, sizeof(Value), &Value);
}

extern "C" UE_AUTORTFM_API AUTORTFM_NO_ASAN void autortfm_redirected_store_1(uint32 AddressSpace, uint64 DestAddress, uint8 Value) noexcept
{
	ForTheRuntime::RedirectedStore(AddressSpace, DestAddress, sizeof(Value), &Value);
}

// Note: Internal.aem maps this to itself when called in the closed
extern "C" void* autortfm_lookup_function(void* OriginalFunction, const char* Where) noexcept
{
	return FunctionMapLookup(OriginalFunction, Where);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API void autortfm_memcpy(void* Dst, const void* Src, size_t Size) noexcept
{
	FContext* Context = FContext::Get();
    Memcpy(Dst, Src, Size, Context);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API void autortfm_memmove(void* Dst, const void* Src, size_t Size) noexcept
{
	FContext* Context = FContext::Get();
    Memmove(Dst, Src, Size, Context);
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API void autortfm_memset(void* Dst, int Value, size_t Size) noexcept
{
	FContext* Context = FContext::Get();
    Memset(Dst, Value, Size, Context);
}

extern "C" UE_AUTORTFM_ALWAYS_OPEN UE_AUTORTFM_API void autortfm_unreachable(const char* Message) noexcept
{
	AUTORTFM_REPORT_ERROR("AutoRTFM Unreachable: %s", Message);
	__builtin_unreachable();
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API void autortfm_llvm_fail(const char* Message) noexcept
{
	if (Message)
	{
		AUTORTFM_REPORT_ERROR("AutoRTFM LLVM Failure: %s", Message);
	}
	else
	{
		AUTORTFM_REPORT_ERROR("AutoRTFM LLVM Failure");
	}
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_API void autortfm_llvm_missing_function() noexcept
{
	if (ForTheRuntime::GetInternalAbortAction() == ForTheRuntime::EAutoRTFMInternalAbortActionState::Crash)
	{
		AUTORTFM_FATAL("Transaction failing because of missing function");
	}
	else
	{
		AUTORTFM_ENSURE_MSG(!ForTheRuntime::GetEnsureOnInternalAbort(), "Transaction failing because of missing function");
	}

	FContext* Context = FContext::Get();
    Context->AbortByLanguageAndThrow();
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_FORCENOINLINE UE_AUTORTFM_API void autortfm_called_no_autortfm() noexcept
{
	AUTORTFM_FATAL("inlined UE_AUTORTFM_NOAUTORTFM function called from the closed");
}

extern "C" AUTORTFM_DISABLE UE_AUTORTFM_FORCENOINLINE UE_AUTORTFM_API void autortfm_disable_called() noexcept
{
	AUTORTFM_FATAL("inlined AUTORTFM_DISABLE function called from the closed");
}
} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
