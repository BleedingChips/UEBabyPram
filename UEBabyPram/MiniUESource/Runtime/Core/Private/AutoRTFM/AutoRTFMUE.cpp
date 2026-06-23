// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFM/AutoRTFMUE.h"

#if UE_AUTORTFM

#include "AutoRTFM.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/UnrealMemory.h"
#include "Logging/LogMacros.h"
#include "Logging/LogVerbosity.h"
#include "Misc/AssertionMacros.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

DECLARE_LOG_CATEGORY_EXTERN(LogAutoRTFM, Display, All)

DEFINE_LOG_CATEGORY(LogAutoRTFM)

static_assert(UE_AUTORTFM_ENABLED, "AutoRTFM/AutoRTFMUE.cpp requires the compiler flag '-fautortfm'");

namespace
{

void OnAutoRTFMRuntimeEnabledChanged()
{
	const bool bEnabled = AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled();
	UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM: %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
	FGenericCrashContext::SetGameData(TEXT("IsAutoRTFMRuntimeEnabled"), bEnabled ? TEXT("true") : TEXT("false"));
}

void OnAutoRTFMRetryTransactionsChanged()
{
	AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState Value = AutoRTFM::ForTheRuntime::GetRetryTransaction();
	switch (Value)
	{
	case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::NoRetry:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Retry Transactions: disabled"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("NoRetry"));
	case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::RetryNonNested:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Retry Transactions: retry-non-nested"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("RetryNonNested"));
	case AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState::RetryNestedToo:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Retry Transactions: retry-nested-too"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMRetryTransactionState"), TEXT("RetryNestedToo"));
	}
}

void OnAutoRTFMMemoryValidationLevelChanged()
{
	AutoRTFM::EMemoryValidationLevel Value = AutoRTFM::ForTheRuntime::GetMemoryValidationLevel();
	switch (Value)
	{
	case AutoRTFM::EMemoryValidationLevel::Disabled:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Memory Validation: disabled"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMMemoryValidationLevel"), TEXT("Disabled"));
	case AutoRTFM::EMemoryValidationLevel::Warn:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Memory Validation: enabled as warning"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMMemoryValidationLevel"), TEXT("Warn"));
	case AutoRTFM::EMemoryValidationLevel::Error:
		UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Memory Validation: enabled as error"));
		return FGenericCrashContext::SetGameData(TEXT("AutoRTFMMemoryValidationLevel"), TEXT("Enabled"));
	}
}

void OnAutoRTFMMemoryValidationThrottlingChanged()
{
	bool bEnabled = AutoRTFM::ForTheRuntime::GetMemoryValidationThrottlingEnabled();
	const TCHAR* Text = bEnabled ? TEXT("true") : TEXT("false");
	UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Memory Validation Throttling Enabled: %s"), Text);
	return FGenericCrashContext::SetGameData(TEXT("AutoRTFMMemoryValidationThrottlingEnabled"), Text);
}

void OnAutoRTFMMemoryValidationStatisticsChanged()
{
	bool bEnabled = AutoRTFM::ForTheRuntime::GetMemoryValidationStatisticsEnabled();
	const TCHAR* Text = bEnabled ? TEXT("true") : TEXT("false");
	UE_LOG(LogAutoRTFM, Log, TEXT("AutoRTFM Memory Validation Statistics Enabled: %s"), Text);
	return FGenericCrashContext::SetGameData(TEXT("AutoRTFMMemoryValidationStatisticsEnabled"), Text);
}

static FAutoConsoleVariable CVarAutoRTFMRuntimeEnabled(
	TEXT("AutoRTFMRuntimeEnabled"),
	TEXT("default"),
	TEXT("Enables the AutoRTFM runtime"),
	FConsoleVariableDelegate::CreateLambda([] (IConsoleVariable* Variable)
	{
		FString Value = Variable->GetString().ToLower();
		if (Value == "default")
		{
			return;
		}
		if (Value == "forceon")
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_ForcedEnabled);
			return;
		}
		if (Value == "forceoff")
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_ForcedDisabled);
			return;
		}
		if (Value == "1") // The CVar system converts On to '1'.
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_Enabled);
			return;
		}
		if (Value == "0") // The CVar system converts Off to '0'.
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_Disabled);
			return;
		}
		if (Value == "2")
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_ForcedDisabled);
			return;
		}
		if (Value == "3")
		{
			AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_ForcedEnabled);
			return;
		}
		UE_LOG(LogAutoRTFM, Fatal, TEXT("'AutoRTFMRuntimeEnabled' CVar was set to '%s' which is not one of 'ForceOn', 'ForceOff', 'On', or 'Off'!"), *Value);
	}),
	ECVF_Default
);

static FAutoConsoleVariable CVarAutoRTFMInternalAbortAction(
	TEXT("AutoRTFMInternalAbortAction"),
	TEXT("default"),
	TEXT("If true when we hit an AutoRTFM issue assert over ensuring"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		FString Value = Variable->GetString().ToLower();
		if (Value == "default")
		{
			return;
		}
		if (Value == "crash")
		{
			AutoRTFM::ForTheRuntime::SetInternalAbortAction(AutoRTFM::ForTheRuntime::EAutoRTFMInternalAbortActionState::Crash);
			return;
		}
		if (Value == "abort")
		{
			AutoRTFM::ForTheRuntime::SetInternalAbortAction(AutoRTFM::ForTheRuntime::EAutoRTFMInternalAbortActionState::Abort);
			return;
		}
		UE_LOG(LogAutoRTFM, Fatal, TEXT("'AutoRTFMInternalAbortAction' CVar was set to '%s' which is not one of 'Crash' or 'Abort'!"), *Value);

	})
);

static FAutoConsoleVariable CVarAutoRTFMRetryTransactions(
	TEXT("AutoRTFMRetryTransactions"),
	static_cast<int>(AutoRTFM::ForTheRuntime::GetRetryTransaction()),
	TEXT("Enables the AutoRTFM sanitizer-like mode where we can force an abort-and-retry on transactions (useful to test abort codepaths work as intended)"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		const AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState Value = static_cast<AutoRTFM::ForTheRuntime::EAutoRTFMRetryTransactionState>(Variable->GetInt());
		AutoRTFM::ForTheRuntime::SetRetryTransaction(Value);
	}),
	ECVF_Default
);

static FAutoConsoleVariable CVarAutoRTFMMemoryValidationLevel(
	TEXT("AutoRTFMMemoryValidationLevel"),
	static_cast<int>(AutoRTFM::ForTheRuntime::GetMemoryValidationLevel()),
	TEXT("Detects potential memory corruption due to writes made both by a transaction and open-code"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		const int Value = Variable->GetInt();
		AutoRTFM::ForTheRuntime::SetMemoryValidationLevel(static_cast<AutoRTFM::EMemoryValidationLevel>(Value));
	}),
	ECVF_Default
);

static FAutoConsoleVariable CVarAutoRTFMMemoryValidationThrottlingEnabled(
	TEXT("AutoRTFMMemoryValidationThrottlingEnabled"),
	AutoRTFM::ForTheRuntime::GetMemoryValidationThrottlingEnabled(),
	TEXT("Automatically skips memory validation on opens if validation is taking an excessive amount of time"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		const bool bValue = Variable->GetBool();
		AutoRTFM::ForTheRuntime::SetMemoryValidationThrottlingEnabled(bValue);
	}),
	ECVF_Default
);

static FAutoConsoleVariable CVarAutoRTFMMemoryValidationStatisticsEnabled(
	TEXT("AutoRTFMMemoryValidationStatisticsEnabled"),
	AutoRTFM::ForTheRuntime::GetMemoryValidationStatisticsEnabled(),
	TEXT("Disable logging of memory validation statistics"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		const bool bValue = Variable->GetBool();
		AutoRTFM::ForTheRuntime::SetMemoryValidationStatisticsEnabled(bValue);
	}),
	ECVF_Default
);

static FAutoConsoleVariable CVarAutoRTFMEnabledProbability(
	TEXT("AutoRTFMEnabledProbability"),
	AutoRTFM::ForTheRuntime::GetAutoRTFMEnabledProbability(),
	TEXT("A rational percentage from [0..100] of what threshold to `CoinTossDisable` AutoRTFM. 100 means always enable, 0 means always disable"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		const float Value = Variable->GetFloat();
		AutoRTFM::ForTheRuntime::SetAutoRTFMEnabledProbability(Value);
	}),
	ECVF_Default
);

bool IsSeverityActive(autortfm_log_severity Severity)
{
	switch (Severity)
	{
		case autortfm_log_verbose:
			return UE_LOG_ACTIVE(LogAutoRTFM, Verbose);
		case autortfm_log_info:
			return UE_LOG_ACTIVE(LogAutoRTFM, Display);
		case autortfm_log_warn:
			return UE_LOG_ACTIVE(LogAutoRTFM, Warning);
		case autortfm_log_error:
			return UE_LOG_ACTIVE(LogAutoRTFM, Error);
		case autortfm_log_fatal:
			return UE_LOG_ACTIVE(LogAutoRTFM, Fatal);
	}
}

UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION
TStringConversion<TStringConvert<UTF8CHAR, TCHAR>, 128> FmtToTChar(const char* Format, va_list Args)
{
	static constexpr size_t InlineBufferLength = 256;
	char InlineBuffer[InlineBufferLength];

	va_list Args2;
	va_copy(Args2, Args);
	int Count = vsnprintf(InlineBuffer, InlineBufferLength, Format, Args);

	if (Count < InlineBufferLength)
	{
		va_end(Args2);
		return StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(InlineBuffer));
	}
	else
	{
		std::unique_ptr<char[]> Buffer{new char[Count+1]};
		vsnprintf(Buffer.get(), Count+1, Format, Args2);
		va_end(Args2);
		return StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Buffer.get()));
	}
}

UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION
void AutoRTFMLog(const char* File, int Line, void* ProgramCounter, autortfm_log_severity Severity, const char* Format, va_list Args)
{
#if !NO_LOGGING
	if (!IsSeverityActive(Severity))
	{
		return;
	}

	static ::UE::Logging::Private::FStaticBasicLogDynamicData DynamicData;
	UE::Logging::Private::FStaticBasicLogRecord Record(
		/* Format */ TEXT("%s"),
		/* File */ File,
		/* Line */ Line,
		/* DynamicData */ &DynamicData
	);

	auto Message = FmtToTChar(Format, Args);

	switch (Severity)
	{
		case autortfm_log_verbose:
			UE::Logging::Private::BasicLog<ELogVerbosity::Verbose>(Record, &LogAutoRTFM, Message.Get());
			break;
		case autortfm_log_info:
			UE::Logging::Private::BasicLog<ELogVerbosity::Display>(Record, &LogAutoRTFM, Message.Get());
			break;
		case autortfm_log_warn:
			UE::Logging::Private::BasicLog<ELogVerbosity::Warning>(Record, &LogAutoRTFM, Message.Get());
			break;
		case autortfm_log_error:
			UE::Logging::Private::BasicLog<ELogVerbosity::Error>(Record, &LogAutoRTFM, Message.Get());
			break;
		case autortfm_log_fatal:
			FDebug::DumpStackTraceToLog(TEXT("AutoRTFM backtrace"), ELogVerbosity::Error);
			UE::Logging::Private::BasicFatalLogWithProgramCounter(Record, &LogAutoRTFM, ProgramCounter, Message.Get());
			break;
	}
#endif
}

UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION
void AutoRTFMLogWithCallstack(void* ProgramCounter, autortfm_log_severity Severity, const char* Format, va_list Args)
{
#if !NO_LOGGING
	if (!IsSeverityActive(Severity))
	{
		return;
	}

	auto Message = FmtToTChar(Format, Args);

	switch (Severity)
	{
		case autortfm_log_verbose:
			FDebug::DumpStackTraceToLog(Message.Get(), ELogVerbosity::Verbose);
			break;
		case autortfm_log_info:
			FDebug::DumpStackTraceToLog(Message.Get(), ELogVerbosity::Display);
			break;
		case autortfm_log_warn:
			FDebug::DumpStackTraceToLog(Message.Get(), ELogVerbosity::Warning);
			break;
		case autortfm_log_error:
			FDebug::DumpStackTraceToLog(Message.Get(), ELogVerbosity::Error);
			break;
		case autortfm_log_fatal:
			FDebug::DumpStackTraceToLog(Message.Get(), ELogVerbosity::Fatal);
			break;
	}
#endif
}

UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION
void AutoRTFMEnsureFailure(const char* File, int Line, void* ProgramCounter, const char* Condition, const char* Format, va_list Args)
{
#if DO_ENSURE
	if (Format == nullptr)
	{
		Format = "";
	}
	FDebug::DumpStackTraceToLog(TEXT("AutoRTFM backtrace"), ELogVerbosity::Error);
	FDebug::EnsureFailed(Condition, File, Line, ProgramCounter, FmtToTChar(Format, Args).Get());
#endif
}

} // anonymous namespace

LLM_DEFINE_TAG(AutoRTFM);

static void* AutoRTFMAllocate(size_t Size, size_t Alignment)
{
	LLM_SCOPE_BYTAG(AutoRTFM);
	return FMemory::Malloc(Size, Alignment);
}

static void* AutoRTFMReallocate(void* Pointer, size_t Size, size_t Alignment)
{
	LLM_SCOPE_BYTAG(AutoRTFM);
	return FMemory::Realloc(Pointer, Size, Alignment);
}

static void* AutoRTFMAllocateZeroed(size_t Size, size_t Alignment)
{
	LLM_SCOPE_BYTAG(AutoRTFM);
	return FMemory::MallocZeroed(Size, Alignment);
}

static void AutoRTFMFree(void* Pointer)
{
	LLM_SCOPE_BYTAG(AutoRTFM);
	FMemory::Free(Pointer);
}

CORE_API void AutoRTFM::InitializeForUE()
{
	AutoRTFM::ForTheRuntime::FExternAPI ExternAPI;
	ExternAPI.Allocate = AutoRTFMAllocate;
	ExternAPI.Reallocate = AutoRTFMReallocate;
	ExternAPI.AllocateZeroed = AutoRTFMAllocateZeroed;
	ExternAPI.Free = AutoRTFMFree;
	ExternAPI.EnsureFailure = AutoRTFMEnsureFailure;
	ExternAPI.Log = AutoRTFMLog;
	ExternAPI.LogWithCallstack = AutoRTFMLogWithCallstack;
	ExternAPI.IsLogActive = IsSeverityActive;

	ExternAPI.OnRuntimeEnabledChanged = OnAutoRTFMRuntimeEnabledChanged;
	ExternAPI.OnRetryTransactionsChanged = OnAutoRTFMRetryTransactionsChanged;
	ExternAPI.OnMemoryValidationLevelChanged = OnAutoRTFMMemoryValidationLevelChanged;
	ExternAPI.OnMemoryValidationThrottlingChanged = OnAutoRTFMMemoryValidationThrottlingChanged;
	ExternAPI.OnMemoryValidationStatisticsChanged = OnAutoRTFMMemoryValidationStatisticsChanged;

	AutoRTFM::ForTheRuntime::Initialize(ExternAPI);

	// Call the OnAutoRTFMXXXChanged() handlers now so that values are logged
	// and crash context data is set.
	OnAutoRTFMRuntimeEnabledChanged();
	OnAutoRTFMRetryTransactionsChanged();
	OnAutoRTFMMemoryValidationLevelChanged();
	OnAutoRTFMMemoryValidationThrottlingChanged();
	OnAutoRTFMMemoryValidationStatisticsChanged();
}

#endif // UE_AUTORTFM
