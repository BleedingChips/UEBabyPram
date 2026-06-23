// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Utils.h"

#include "ContextInlines.h"
#include "BuildMacros.h"

#include <utility>

#if AUTORTFM_PLATFORM_WINDOWS
#include "WindowsHeader.h"
#include <dbghelp.h>
#else
#include <execinfo.h>
#endif


namespace AutoRTFM
{

void DoAssert(void(*Logger)()) //-V1082 Silence PVS [[noreturn]] false positive warning
{
	Logger();
	__builtin_unreachable();
}

void DoExpect(void(*Logger)())
{
	Logger();
}

std::string GetFunctionDescription(void* FunctionPtr)
{
#if AUTORTFM_PLATFORM_WINDOWS
    // This is gross, but it works. It's possible for someone to have SymInitialized before. But if they had, then this
    // will just fail. Also, this function is called in cases where we're failing, so it's ok if we do dirty things.
    SymInitialize(GetCurrentProcess(), nullptr, true);

    DWORD64 Displacement = 0;
    DWORD64 Address = reinterpret_cast<DWORD64>(FunctionPtr);
    char Buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO Symbol = reinterpret_cast<PSYMBOL_INFO>(Buffer);
    Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    Symbol->MaxNameLen = MAX_SYM_NAME;
    if (SymFromAddr(GetCurrentProcess(), Address, &Displacement, Symbol))
    {
        return Symbol->Name;
    }
    else
    {
        return "<error getting description>";
    }
#else // AUTORTFM_PLATFORM_WINDOWS -> so !AUTORTFM_PLATFORM_WINDOWS
    char** const symbols = backtrace_symbols(&FunctionPtr, 1);
    std::string Name(*symbols);
    free(symbols);
    return Name;
#endif // AUTORTFM_PLATFORM_WINDOWS -> so !AUTORTFM_PLATFORM_WINDOWS
}

UE_AUTORTFM_API UE_AUTORTFM_FORCENOINLINE void ReportError(const char* File, int Line, void* ProgramCounter, const char* Format, ...)
{
	if (ProgramCounter == nullptr)
	{
		ProgramCounter = __builtin_return_address(0);
	}

	const ForTheRuntime::EAutoRTFMInternalAbortActionState InternalAbortAction = ForTheRuntime::GetInternalAbortAction();

	if (Format)
	{
		if (InternalAbortAction == ForTheRuntime::EAutoRTFMInternalAbortActionState::Crash)
		{
			va_list Args;
			va_start(Args, Format);
			::AutoRTFM::LogV(File, Line, ProgramCounter, autortfm_log_fatal, Format, Args);
			va_end(Args);
		}
		else if (ForTheRuntime::GetEnsureOnInternalAbort())
		{
			va_list Args;
			va_start(Args, Format);
			[[maybe_unused]] static bool bCalled = [&]
			{
				::AutoRTFM::EnsureFailureV(File, Line, ProgramCounter, "!GetEnsureOnInternalAbort()", Format, Args);
				return true;
			}();
			va_end(Args);
		}
	}
	else
	{
		if (InternalAbortAction == ForTheRuntime::EAutoRTFMInternalAbortActionState::Crash)
		{
			::AutoRTFM::Log(File, Line, ProgramCounter, autortfm_log_fatal, "Transaction failing because of internal issue");
		}
		else if (ForTheRuntime::GetEnsureOnInternalAbort())
		{
			[[maybe_unused]] static bool bCalled = [&]
			{
				::AutoRTFM::EnsureFailure(File, Line, ProgramCounter, "!GetEnsureOnInternalAbort()", "Transaction failing because of internal issue");
				return true;
			}();
		}
	}

	switch (InternalAbortAction)
	{
		case ForTheRuntime::EAutoRTFMInternalAbortActionState::Abort:
			if (FContext* const Context = FContext::Get())
			{
				Context->AbortByLanguageAndThrow();
			}
			break;

		case ForTheRuntime::EAutoRTFMInternalAbortActionState::Crash:
			// The `autortfm_log_fatal` lines above probably triggered a crash already, but if we made it this far, force the crash.
			__builtin_trap();
			break;

		default:
			::AutoRTFM::EnsureFailure(File, Line, ProgramCounter, "Unexpected InternalAbortAction", "InternalAbortAction = %d", int(InternalAbortAction));
			break;
	}
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
