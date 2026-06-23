// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 *  AutoRTFM is designed to make it easy to take existing C++ code--even if it was never designed 
 *  to have any transactional semantics--and make it transactional just by using an alternate compiler.
 *  For details, see `Engine/Source/Runtime/AutoRTFM/Documentation/README.md`.
 */ 

// HEADER_UNIT_SKIP - unused warnings

#include "AutoRTFMConstants.h"
#include "AutoRTFMDefines.h" // IWYU pragma: export

#ifdef __cplusplus
#include "AutoRTFMTask.h"

#include <algorithm>
#include <cstdarg>
#include <tuple>
#include <type_traits>
#include <utility>
#endif // __cplusplus

#include <memory.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The C API exists for a few reasons:
//
// - It makes linking easy. AutoRTFM has to deal with a weird kind of linking
//   where the compiler directly emits calls to functions with a given name.
//   It's easiest to do that in llvm if the functions have C linkage and C ABI.
// - It makes testing easy. Even seemingly simple C++ code introduces pitfalls
//   for AutoRTFM. So very focused tests work best when written in C.
// - It makes compiler optimizations much easier as there is no mangling to
// 	 consider when looking for functions in the runtime we can optimize.
//
// We use snake_case for C API surface area to make it easy to distinguish.
//
// The C API should not be used directly - it is here purely as an
// implementation detail.

// This must match AutoRTFM::ETransactionResult.
typedef enum
{
    autortfm_aborted_by_request = 0,
    autortfm_aborted_by_language,
    autortfm_committed,
	autortfm_aborted_by_transact_during_commit,
	autortfm_aborted_by_transact_during_abort,
	autortfm_aborted_by_cascade,
} autortfm_result;

// This must match AutoRTFM::EContextStatus.
typedef enum
{
	autortfm_status_idle = 0,
	autortfm_status_ontrack,
	autortfm_status_aborted_by_failed_lock_aquisition,
	autortfm_status_aborted_by_language,
	autortfm_status_aborted_by_request,
	autortfm_status_committing,
	autortfm_status_aborted_by_cascading_abort,
	autortfm_status_aborted_by_cascading_retry,
	autortfm_status_in_static_local_initializer,
	autortfm_status_in_post_abort
} autortfm_status;

// AutoRTFM logging severity.
typedef enum
{
	autortfm_log_verbose = 0,
	autortfm_log_info,
	autortfm_log_warn,
	autortfm_log_error,
	autortfm_log_fatal,
} autortfm_log_severity;

// An opaque unique identifier for a transaction.
typedef uint64_t autortfm_transaction_id;

// Function pointers used by AutoRTFM for heap allocations, etc.
typedef struct
{
	// The function used to allocate memory from the heap.
	// Must not be null.
	void* (*Allocate)(size_t Size, size_t Alignment);

	// The function used to reallocate memory from the heap.
	// Must not be null.
	void* (*Reallocate)(void* Pointer, size_t Size, size_t Alignment);

	// The function used to allocate zeroed memory from the heap.
	// Must not be null.
	void* (*AllocateZeroed)(size_t Size, size_t Alignment);

	// The function used to free memory allocated by Allocate() and AllocateZeroed().
	// Must not be null.
	void (*Free)(void* Pointer);

	// Function used to log messages using a printf-style format string and va_list arguments.
	// Strings use UTF-8 encoding.
	// Must not be null.
	void (*Log)(const char* File, int Line, void* ProgramCounter, autortfm_log_severity Severity, const char* Format, va_list Args);

	// Function used to log messages with a callstack using a printf-style format string and va_list arguments.
	// Strings use UTF-8 encoding.
	// Must not be null.
	void (*LogWithCallstack)(void* ProgramCounter, autortfm_log_severity Severity, const char* Format, va_list Args);
	
	// Function used to report an ensure failure using a printf-style format string and va_list arguments.
	// Strings use UTF-8 encoding.
	// Must not be null.
	void (*EnsureFailure)(const char* File, int Line, void* ProgramCounter, const char* Condition, const char* Format, va_list Args);

	// Function used to query whether a log severity is active.
	// Must not be null.
	bool (*IsLogActive)(autortfm_log_severity Severity);

	// Optional callback to be informed when the value returned by
	// ForTheRuntime::IsAutoRTFMRuntimeEnabled() changes.
	// Can be null.
	void (*OnRuntimeEnabledChanged)();

	// Optional callback to be informed when the value returned by
	// ForTheRuntime::GetRetryTransaction() changes.
	// Can be null.
	void (*OnRetryTransactionsChanged)();

	// Optional callback to be informed when the value returned by
	// ForTheRuntime::GetMemoryValidationLevel() changes.
	// Can be null.
	void (*OnMemoryValidationLevelChanged)();

	// Optional callback to be informed when the value returned by
	// ForTheRuntime::GetMemoryValidationThrottlingEnabled() changes.
	// Can be null.
	void (*OnMemoryValidationThrottlingChanged)();

	// Optional callback to be informed when the value returned by
	// ForTheRuntime::GetMemoryValidationStatisticsEnabled() changes.
	// Can be null.
	void (*OnMemoryValidationStatisticsChanged)();

} autortfm_extern_api;

#if UE_AUTORTFM_ENABLED
// Initialize the AutoRTFM library.
// Parameters:
//   ExternAPI - Function pointers used by AutoRTFM for heap allocations, etc.
//               Must be non-null.
UE_AUTORTFM_API void autortfm_initialize(const autortfm_extern_api* ExternAPI) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_initialize(const autortfm_extern_api* ExternAPI) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(ExternAPI);
}
#endif

#if UE_AUTORTFM_ENABLED
// Note: There is no implementation of this function.
// The AutoRTFM compiler will replace all calls to this function with a constant boolean value.
UE_AUTORTFM_API bool autortfm_is_closed(void) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_closed(void) AUTORTFM_NOEXCEPT
{
    return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_transactional(void) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_transactional(void) AUTORTFM_NOEXCEPT
{
    return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_committing(void) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_committing(void) AUTORTFM_NOEXCEPT
{
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_committing_or_aborting(void) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_committing_or_aborting(void) AUTORTFM_NOEXCEPT
{
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_retrying(void) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_retrying(void) AUTORTFM_NOEXCEPT
{
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg) AUTORTFM_EXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE autortfm_result autortfm_transact(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg) AUTORTFM_EXCEPT
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
	return autortfm_committed;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
UE_AUTORTFM_CRITICAL_INLINE autortfm_result autortfm_transact_then_open(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
    return autortfm_committed;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_commit(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UninstrumentedWork(Arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_abort_transaction() AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_abort_transaction() AUTORTFM_NOEXCEPT {}
#endif

#if UE_AUTORTFM_ENABLED
AUTORTFM_OPEN UE_AUTORTFM_API autortfm_transaction_id autortfm_current_transaction_id() AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE autortfm_transaction_id autortfm_current_transaction_id() AUTORTFM_NOEXCEPT { return 0; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API autortfm_result autortfm_commit_transaction() AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE autortfm_result autortfm_commit_transaction() AUTORTFM_NOEXCEPT { return autortfm_aborted_by_language; }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_FORCENOINLINE UE_AUTORTFM_API void autortfm_open(void (*work)(void* arg), void* arg, const void* return_address);
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_open(void (*work)(void* arg), void* arg, const void* /* return_address */) { work(arg); }
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_FORCENOINLINE UE_AUTORTFM_API void autortfm_open_explicit_validation(autortfm_memory_validation_level, void (*work)(void* arg), void* arg, const void* return_address);
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_open_explicit_validation(autortfm_memory_validation_level, void (*work)(void* arg), void* arg, const void* /* return_address */) { work(arg); }
#endif

#if UE_AUTORTFM_ENABLED
[[nodiscard]] UE_AUTORTFM_API autortfm_status autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg);
#else
AUTORTFM_DISABLE_UNREACHABLE_CODE_WARNINGS
[[nodiscard]] UE_AUTORTFM_CRITICAL_INLINE autortfm_status autortfm_close(void (*UninstrumentedWork)(void*), void (*InstrumentedWork)(void*), void* Arg)
{
	UE_AUTORTFM_UNUSED(UninstrumentedWork);
	UE_AUTORTFM_UNUSED(InstrumentedWork);
	UE_AUTORTFM_UNUSED(Arg);
	abort();
	return autortfm_status_aborted_by_language;
}
AUTORTFM_RESTORE_UNREACHABLE_CODE_WARNINGS
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_record_open_write(void* Ptr, size_t Size) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_record_open_write(void* Ptr, size_t Size) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Ptr);
	UE_AUTORTFM_UNUSED(Size);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_record_open_write_no_memory_validation(void* Ptr, size_t Size) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_record_open_write_no_memory_validation(void* Ptr, size_t Size) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Ptr);
	UE_AUTORTFM_UNUSED(Size);
}
#endif

// autortfm_open_to_closed_mapping maps an open function to its closed variant.
struct autortfm_open_to_closed_mapping
{
	void* Open;
	void* Closed;
};

// autortfm_open_to_closed_table holds a pointer to a null-terminated list of
// autortfm_open_to_closed_mapping, and an intrusive linked-list pointer to the
// previous and next registered autortfm_open_to_closed_table.
struct autortfm_open_to_closed_table
{
	// Null-terminated open function to closed function mapping table.
	const struct autortfm_open_to_closed_mapping* Mappings;
	// An intrusive linked-list pointer to the previous autortfm_open_to_closed_table.
	// Used by autortfm_register_open_to_closed_functions().
	struct autortfm_open_to_closed_table* Prev;
	// An intrusive linked-list pointer to the next autortfm_open_to_closed_table.
	// Used by autortfm_register_open_to_closed_functions().
	struct autortfm_open_to_closed_table* Next;
};

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_register_open_to_closed_functions(struct autortfm_open_to_closed_table* Table) AUTORTFM_NOEXCEPT;
UE_AUTORTFM_API void autortfm_unregister_open_to_closed_functions(struct autortfm_open_to_closed_table* Table) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_register_open_to_closed_functions(autortfm_open_to_closed_table* Table) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Table);
}
UE_AUTORTFM_CRITICAL_INLINE void autortfm_unregister_open_to_closed_functions(autortfm_open_to_closed_table* Table) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Table);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API bool autortfm_is_on_current_transaction_stack(void* Ptr) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE bool autortfm_is_on_current_transaction_stack(void* Ptr) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Ptr);
	return false;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_on_commit(void (*work)(void* arg), void* arg);
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_on_commit(void (*work)(void* arg), void* arg)
{
    work(arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_on_pre_abort(void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_on_pre_abort(void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(work);
	UE_AUTORTFM_UNUSED(arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_on_abort(void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_on_abort(void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(work);
	UE_AUTORTFM_UNUSED(arg);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT;
UE_AUTORTFM_API void autortfm_pop_on_abort_handler(const void* key) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_push_on_abort_handler(const void* key, void (*work)(void* arg), void* arg) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(key);
	UE_AUTORTFM_UNUSED(work);
	UE_AUTORTFM_UNUSED(arg);
}

UE_AUTORTFM_CRITICAL_INLINE void autortfm_pop_on_abort_handler(const void* key) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(key);
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void* autortfm_did_allocate(void* ptr, size_t size) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void* autortfm_did_allocate(void* ptr, size_t size) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(size);
    return ptr;
}
#endif

#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_did_free(void* ptr) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_did_free(void* ptr) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(ptr);
}
#endif

// If running with AutoRTFM enabled, then perform an ABI check between the
// AutoRTFM compiler and the AutoRTFM runtime, to ensure that memory is being
// laid out in an identical manner between the AutoRTFM runtime and the AutoRTFM
// compiler pass. Should not be called manually by the user, a call to this will
// be injected by the compiler into a global constructor in the AutoRTFM compiled
// code.
#if UE_AUTORTFM_ENABLED
UE_AUTORTFM_API void autortfm_check_abi(void* ptr, size_t size) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_check_abi(void* ptr, size_t size) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(ptr);
	UE_AUTORTFM_UNUSED(size);
}
#endif

#if UE_AUTORTFM_ENABLED
// Called when execution unexpectedly reaches a code path that was considered unreachable.
// Either aborts execution of the program or aborts the current transaction, depending on the
// current 'InternalAbortAction' state.
[[noreturn]] UE_AUTORTFM_API void autortfm_unreachable(const char* Message) AUTORTFM_NOEXCEPT;
#else
UE_AUTORTFM_CRITICAL_INLINE void autortfm_unreachable(const char* Message) AUTORTFM_NOEXCEPT
{
	UE_AUTORTFM_UNUSED(Message);
}
#endif

#ifdef __cplusplus
}

namespace AutoRTFM
{

// The transaction result provides information on how a transaction completed. This is either Committed,
// or one of the various AbortedBy* variants to show why an abort occurred.
enum class ETransactionResult
{
	// The transaction aborted because of an explicit call to AbortTransaction or RollbackTransaction.
    AbortedByRequest = autortfm_aborted_by_request,

	// The transaction aborted because of unhandled constructs in the code (atomics, unhandled function calls, etc).
    AbortedByLanguage = autortfm_aborted_by_language,

	// The transaction committed successfully. For a nested transaction this does not mean that the transaction effects
	// cannot be undone later if the parent transaction is aborted for any reason.
    Committed = autortfm_committed,

	// The transaction aborted because a new transaction nest was attempted (via OnCommit or OnComplete) while the 
	// current transaction was being committed.
	AbortedByTransactDuringCommit = autortfm_aborted_by_transact_during_commit,

	// The transaction aborted because a new transaction nest was attempted (via OnAbort or OnComplete) while the 
	// current transaction was being aborted.
	AbortedByTransactDuringAbort = autortfm_aborted_by_transact_during_abort,

	// The transaction aborted because of an explicit call to CascadingAbortTransaction.
	AbortedByCascade = autortfm_aborted_by_cascade
};

// The context status shows what state the AutoRTFM context is currently in.
enum class EContextStatus : uint8_t
{
	// An Idle status means we are not in transactional code.
	Idle = autortfm_status_idle,

	// An OnTrack status means we are in transactional code.
	OnTrack = autortfm_status_ontrack,

	// Reserved for a full STM future.
	AbortedByFailedLockAcquisition = autortfm_status_aborted_by_failed_lock_aquisition,

	// An AbortedByLanguage status means that we found some unhandled constructs in the code
	// (atomics, unhandled function calls, etc) and are currently aborting because of it.
	AbortedByLanguage = autortfm_status_aborted_by_language,

	// An AbortedByRequest status means that a call to AbortTransaction or RollbackTransaction occurred,
	// and we are currently aborting because of it.
	AbortedByRequest = autortfm_status_aborted_by_request,

	// A Committing status means we are currently attempting to commit a transaction.
	Committing = autortfm_status_committing,

	// An AbortedByCascadingAbort status means that a call to CascadingAbortTransaction or
	// CascadingRollbackTransaction occurred, and we are currently aborting because of it.
	AbortedByCascadingAbort = autortfm_status_aborted_by_cascading_abort,

	// An AbortedByCascadingRetry status means that a call to CascadingRetryTransaction occurred,
	// and we are currently aborting because of it.
	AbortedByCascadingRetry = autortfm_status_aborted_by_cascading_retry,

	// Means we are in a static local initializer which always run in the open.
	// `IsTransactional()` will return `false` when in this state.
	InStaticLocalInitializer = autortfm_status_in_static_local_initializer,
};

// Returns true if Status represents one of the "aborting" states.
inline bool IsStatusAborting(EContextStatus Status)
{
	switch (Status)
	{
		case EContextStatus::AbortedByFailedLockAcquisition:
		case EContextStatus::AbortedByLanguage:
		case EContextStatus::AbortedByRequest:
		case EContextStatus::AbortedByCascadingAbort:
		case EContextStatus::AbortedByCascadingRetry:
			return true;
		case EContextStatus::Idle:
		case EContextStatus::OnTrack:
		case EContextStatus::Committing:
		case EContextStatus::InStaticLocalInitializer:
			return false;
	}
}

// An opaque unique identifier for a transaction.
using TransactionID = autortfm_transaction_id;

// An enumerator of transactional memory validation levels.
// Memory validation is used to detect modification by open-code to memory that was written by a
// transaction. In this situation, aborting the transaction can corrupt memory as the undo will
// overwrite the writes made in the open-code.
enum class EMemoryValidationLevel : uint8_t
{
	// The default memory validation level.
	Default = autortfm_memory_validation_level_default,

	// Disable memory validation.
	Disabled = autortfm_memory_validation_level_disabled,

	// Memory validation enabled as warnings.
	Warn = autortfm_memory_validation_level_warn,

	// Memory validation enabled as errors.
	Error = autortfm_memory_validation_level_error,
};

#if UE_AUTORTFM
namespace ForTheRuntime
{
	using FExternAPI = autortfm_extern_api;
	UE_AUTORTFM_API void Initialize(const FExternAPI& ExternAPI);
	UE_AUTORTFM_API void CascadingAbortTransactionInternal();
	UE_AUTORTFM_API void CascadingRetryTransactionInternal();
	UE_AUTORTFM_API void OnCommitInternal(TTask<void()>&& Work);
	UE_AUTORTFM_API void OnPreAbortInternal(TTask<void()>&& Work);
	UE_AUTORTFM_API void OnAbortInternal(TTask<void()>&& Work);
	UE_AUTORTFM_API void OnCompleteInternal(TTask<void()>&& Work);
	UE_AUTORTFM_API void PushOnCommitHandlerInternal(const void* Key, TTask<void()>&& Work);
	UE_AUTORTFM_API void PopOnCommitHandlerInternal(const void* Key);
	UE_AUTORTFM_API void PopAllOnCommitHandlersInternal(const void* Key);
	UE_AUTORTFM_API void PushOnAbortHandlerInternal(const void* Key, TTask<void()>&& Work);
	UE_AUTORTFM_API void PopOnAbortHandlerInternal(const void* Key);
	UE_AUTORTFM_API void PopAllOnAbortHandlersInternal(const void* Key);
	UE_AUTORTFM_API void RegisterOnCommitFromTheOpen(TTask<void()>&& Work);
	UE_AUTORTFM_API void RegisterOnAbortFromTheOpen(TTask<void()>&& Work);
	UE_AUTORTFM_API void RedirectedLoad(uint32_t AddressSpace, void* DestPointer, uint64_t Size, uint64_t SourceAddress);
	UE_AUTORTFM_API void RedirectedStore(uint32_t AddressSpace, uint64_t DestAddress, uint64_t Size, const void* SourcePointer);
} // namespace ForTheRuntime
#endif

template<typename FunctorType>
void AutoRTFMFunctorInvoker(void* Arg) { (*static_cast<const FunctorType*>(Arg))(); }

#if UE_AUTORTFM
extern "C" UE_AUTORTFM_FORCENOINLINE UE_AUTORTFM_API void* autortfm_lookup_function(void* OriginalFunction, const char* Where) AUTORTFM_NOEXCEPT;

template<typename FunctorType>
auto AutoRTFMLookupInstrumentedFunctorInvoker(const FunctorType& Functor) -> void(*)(void*)
{
	// keep this as a single expression to help ensure that even Debug builds optimize this.
	// if we put intermediate results in local variables then the compiler emits loads
	// and stores to the stack which confuse our custom pass that tries to strip away
	// the actual call to autortfm_lookup_function
	void (*Result)(void*) = reinterpret_cast<void(*)(void*)>(autortfm_lookup_function(reinterpret_cast<void*>(&AutoRTFMFunctorInvoker<FunctorType>), "AutoRTFMLookupInstrumentedFunctorInvoker"));
	return Result;
}
#else
template<typename FunctorType>
auto AutoRTFMLookupInstrumentedFunctorInvoker(const FunctorType& Functor) -> void(*)(void*)
{
	return nullptr;
}
#endif

// Tells if we are currently running in a transaction. This will return true in an open nest
// (see `Open`). This function is handled specially in the compiler and will be constant folded
// as true in closed code, or preserved as a function call in open code.
UE_AUTORTFM_CRITICAL_INLINE_ALWAYS bool IsTransactional() { return autortfm_is_transactional(); }

// Tells if we are currently running in the closed nest of a transaction. By default,
// transactional code is in a closed nest; the only way to be in an open nest is to request it
// via `Open`. This function is handled specially in the compiler and will be constant folded
// as true in closed code, and false in open code.
UE_AUTORTFM_CRITICAL_INLINE_ALWAYS bool IsClosed() { return autortfm_is_closed(); }

// Tells us if we are currently committing a transaction. This will return true
// when inside an on-commit handler.
UE_AUTORTFM_CRITICAL_INLINE bool IsCommitting() { return autortfm_is_committing(); }

// Tells us if we are currently committing or aborting a transaction. This will return true
// when inside an on-abort, on-commit or on-complete handler.
UE_AUTORTFM_CRITICAL_INLINE bool IsCommittingOrAborting() { return autortfm_is_committing_or_aborting(); }

// Tells us if we are currently retrying a transaction. This will return true when inside an
// on-abort or on-complete handler that was triggered by CascadingRetryTransaction. 
UE_AUTORTFM_CRITICAL_INLINE bool IsRetrying() { return autortfm_is_retrying(); }

// Returns true if the passed-in pointer is on the stack of the currently-executing transaction.
// This is occasionally necessary when writing OnAbort handlers for objects on the stack, since
// we don't want to scribble on stack memory that might have been reused.
UE_AUTORTFM_CRITICAL_INLINE bool IsOnCurrentTransactionStack(void* Ptr)
{
	return autortfm_is_on_current_transaction_stack(Ptr);
}

// Returns an opaque identifier for the current transaction.
UE_AUTORTFM_CRITICAL_INLINE TransactionID CurrentTransactionID()
{
	return autortfm_current_transaction_id();
}

// Run the functor in a transaction. Memory writes and other side effects get instrumented
// and will be reversed if the transaction aborts.
//
// If this begins a nested transaction, the instrumented effects are logged onto the root
// transaction, so the effects can be reversed later if the root transaction aborts, even
// if this nested transaction succeeds.
//
// If AutoRTFM is disabled, the code will be ran non-transactionally.
template<typename FunctorType>
UE_AUTORTFM_CRITICAL_INLINE ETransactionResult Transact(const FunctorType& Functor)
{
	ETransactionResult Result =
		static_cast<ETransactionResult>(
			autortfm_transact(
				&AutoRTFMFunctorInvoker<FunctorType>,
				AutoRTFMLookupInstrumentedFunctorInvoker<FunctorType>(Functor),
				const_cast<void*>(static_cast<const void*>(&Functor))));

	return Result;
}

// This is just like calling Transact([&] { Open([&] { Functor(); }); });
// The reason we expose it is that it allows the caller's module to not
// be compiled with the AutoRTFM instrumentation of functions if the only
// thing that's being invoked is a function in the open.
template<typename FunctorType>
UE_AUTORTFM_CRITICAL_INLINE ETransactionResult TransactThenOpen(const FunctorType& Functor)
{
	ETransactionResult Result =
		static_cast<ETransactionResult>(
			autortfm_transact_then_open(
				&AutoRTFMFunctorInvoker<FunctorType>,
				AutoRTFMLookupInstrumentedFunctorInvoker<FunctorType>(Functor),
				const_cast<void*>(static_cast<const void*>(&Functor))));

	return Result;
}

// Run the callback in a transaction like Transact, but abort program
// execution if the result is anything other than autortfm_committed.
// Useful for testing.
template<typename FunctorType>
UE_AUTORTFM_CRITICAL_INLINE void Commit(const FunctorType& Functor)
{
    autortfm_commit(
		&AutoRTFMFunctorInvoker<FunctorType>,
		AutoRTFMLookupInstrumentedFunctorInvoker<FunctorType>(Functor),
		const_cast<void*>(static_cast<const void*>(&Functor)));
}

// Ends a transaction while in the closed, discarding all effects. 
// Sends control to the end of the transaction immediately.
UE_AUTORTFM_CRITICAL_INLINE void AbortTransaction()
{
	autortfm_abort_transaction();
}

// End a transaction nest in the closed, discarding all effects. This cascades, 
// meaning an abort of a nested transaction will cause all transactions in the
// nest to abort. Once the transaction has aborted, on-complete callbacks will
// be invoked. Finally, control will be returned to the end of the outermost 
// Transact.
#if UE_AUTORTFM
UE_AUTORTFM_CRITICAL_INLINE void CascadingAbortTransaction()
{
	ForTheRuntime::CascadingAbortTransactionInternal();
}
#else
UE_AUTORTFM_CRITICAL_INLINE void CascadingAbortTransaction() {}
#endif

namespace Detail
{
template<typename T, typename = void>
struct THasAssignFromOpenToClosedMethod : std::false_type {};
template<typename T>
struct THasAssignFromOpenToClosedMethod<T, std::void_t<decltype(T::AutoRTFMAssignFromOpenToClosed(std::declval<T&>(), std::declval<T>()))>> : std::true_type {};
}

// Evaluates to true if the type T has a static method with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template<typename T>
static constexpr bool HasAssignFromOpenToClosedMethod = Detail::THasAssignFromOpenToClosedMethod<T>::value;

// Template class used to declare a method for safely copying or moving an
// object of type T from open to closed transactions.
// Specializations of TAssignFromOpenToClosed must have at least one static
// method with the signature:
//   static void Assign(T& Closed, U Open);
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
//
// TAssignFromOpenToClosed has pre-declared specializations for basic primitive
// types, and can be extended with user-declared template specializations.
//
// TAssignFromOpenToClosed has a pre-declared specialization that detects and
// calls a static method on T with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template<typename T, typename = void>
struct TAssignFromOpenToClosed;

namespace Detail
{
template<typename T, typename = void>
struct THasAssignFromOpenToClosedTrait : std::false_type {};
template<typename T>
struct THasAssignFromOpenToClosedTrait<T, std::void_t<decltype(TAssignFromOpenToClosed<T>::Assign(std::declval<T&>(), std::declval<T>()))>> : std::true_type {};
}

// Evaluates to true if the type T supports assigning from open to closed transactions.
template<typename T>
static constexpr bool HasAssignFromOpenToClosedTrait = Detail::THasAssignFromOpenToClosedTrait<T>::value;

// Specialization of TAssignFromOpenToClosed for fundamental types.
template<typename T>
struct TAssignFromOpenToClosed<T, std::enable_if_t<std::is_fundamental_v<T>>>
{
	UE_AUTORTFM_FORCEINLINE static void Assign(T& Closed, T Open) { Closed = Open; }
};

// Specialization of TAssignFromOpenToClosed for raw pointer types.
template<typename T>
struct TAssignFromOpenToClosed<T*, void>
{
	UE_AUTORTFM_FORCEINLINE static void Assign(T*& Closed, T* Open) { Closed = Open; }
};

// Specialization of TAssignFromOpenToClosed for std::tuple.
template<typename ... TYPES>
struct TAssignFromOpenToClosed<std::tuple<TYPES...>, std::enable_if_t<(HasAssignFromOpenToClosedTrait<TYPES> && ...)>>
{
	template<size_t I = 0, typename SRC = void>
	UE_AUTORTFM_FORCEINLINE static void AssignElements(std::tuple<TYPES...>& Closed, SRC&& Open)
	{
		if constexpr(I < sizeof...(TYPES))
		{
			using E = std::tuple_element_t<I, std::tuple<TYPES...>>;
			TAssignFromOpenToClosed<E>::Assign(std::get<I>(Closed), std::get<I>(std::forward<SRC>(Open)));
			AssignElements<I+1>(Closed, std::forward<SRC>(Open));
		}
	}

	template<typename SRC>
	UE_AUTORTFM_FORCEINLINE static void Assign(std::tuple<TYPES...>& Closed, SRC&& Open)
	{
		AssignElements(Closed, std::forward<SRC>(Open));
	}
};

// Specialization of TAssignFromOpenToClosed for types that have a static method
// with the signature:
//    static void AutoRTFMAssignFromOpenToClosed(T& Closed, U Open)
// Where `U` is `T`, `const T&` or `T&&`. Supports both copy assignment and move assignment.
template<typename T>
struct TAssignFromOpenToClosed<T, std::enable_if_t<HasAssignFromOpenToClosedMethod<T>>>
{
	template<typename OPEN>
	UE_AUTORTFM_FORCEINLINE static void Assign(T& Closed, OPEN&& Open)
	{
		Closed.AutoRTFMAssignFromOpenToClosed(Closed, std::forward<OPEN>(Open));
	}
};

// Specialization of TAssignFromOpenToClosed for `void` (used to make IsSafeToReturnFromOpen<void> work).
template<>
struct TAssignFromOpenToClosed<void, void>;

// Evaluates to true if the type T is safe to return from Open().
template<typename T>
static constexpr bool IsSafeToReturnFromOpen = HasAssignFromOpenToClosedTrait<T> || std::is_same_v<T, void>;

// Executes the given code non-transactionally regardless of whether we are in
// a transaction or not. Returns the value returned by Functor.
// ReturnType must be void or a type that can be safely copied from the open to a closed transaction.
// TAssignFromOpenToClosed must have a specialization for the type that is being returned.
template
<
	EMemoryValidationLevel VALIDATION_LEVEL = EMemoryValidationLevel::Default,
	typename FunctorType = void,
	typename ReturnType = decltype(std::declval<FunctorType>()())
>
UE_AUTORTFM_CRITICAL_INLINE ReturnType Open(const FunctorType& Functor)
{
	static_assert(IsSafeToReturnFromOpen<ReturnType>,
		"function return type is not safe to return from Open()");
#if UE_AUTORTFM
	if (!autortfm_is_closed())
	{
		return Functor();
	}

	if constexpr (IsSafeToReturnFromOpen<ReturnType>)
	{
		if constexpr (std::is_same_v<void, ReturnType>)
		{
			struct FCallHelper
			{
				AUTORTFM_DISABLE static void Call(void* Arg)
				{
					const FunctorType& Fn = *reinterpret_cast<FunctorType*>(Arg);
					UE_AUTORTFM_CALLSITE_FORCEINLINE Fn();
				}
			};

			if constexpr (VALIDATION_LEVEL == EMemoryValidationLevel::Default)
			{
				autortfm_open(&FCallHelper::Call, const_cast<void*>(reinterpret_cast<const void*>(&Functor)), __builtin_return_address(0));
			}
			else
			{
				autortfm_open_explicit_validation(
					static_cast<autortfm_memory_validation_level>(VALIDATION_LEVEL),
					&FCallHelper::Call,
					const_cast<void*>(static_cast<const void*>(&Functor)),
					__builtin_return_address(0));
			}
		}
		else
		{
			struct FCallHelper
			{
				AUTORTFM_DISABLE static void Call(void* Arg)
				{
					FCallHelper& Self = *reinterpret_cast<FCallHelper*>(Arg);
					UE_AUTORTFM_CALLSITE_FORCEINLINE
						TAssignFromOpenToClosed<ReturnType>::Assign(Self.ReturnValue, std::move(Self.Functor()));
				}
				const FunctorType& Functor;
				ReturnType ReturnValue{};
			};
			FCallHelper Helper{Functor};
			if constexpr (VALIDATION_LEVEL == EMemoryValidationLevel::Default)
			{
				autortfm_open(&FCallHelper::Call, reinterpret_cast<void*>(&Helper), __builtin_return_address(0));
			}
			else
			{
				autortfm_open_explicit_validation(
					static_cast<autortfm_memory_validation_level>(VALIDATION_LEVEL),
					&FCallHelper::Call,
					reinterpret_cast<void*>(&Helper),
					__builtin_return_address(0));
			}
			return Helper.ReturnValue;
		}
	}
#else // UE_AUTORTFM
	return Functor();
#endif // UE_AUTORTFM
}

// Always executes the given code transactionally when called from a transaction nest
// (whether we are in open or closed code).
//
// Will crash if called outside of a transaction nest.
//
// If Close() returns an aborting status (see IsStatusAborting()), then
// attempting to use the transaction is undefined behaviour. The caller should
// return to the closed as quickly as possible to avoid the risk of the
// transaction being used in its rolled-back state.
template<typename FunctorType> [[nodiscard]] UE_AUTORTFM_CRITICAL_INLINE EContextStatus Close(const FunctorType& Functor)
{
    return static_cast<EContextStatus>(
		autortfm_close(
			&AutoRTFMFunctorInvoker<FunctorType>,
			AutoRTFMLookupInstrumentedFunctorInvoker<FunctorType>(Functor),
			const_cast<void*>(static_cast<const void*>(&Functor))));
}

// Force a transaction nest to be retried. Once the transaction has aborted,
// all on-complete handlers will be called before retrying the transaction. 
// This is an expensive operation and should thus be used with extreme caution.
// If this is called outside of a transaction, the call is ignored.
#if UE_AUTORTFM
UE_AUTORTFM_CRITICAL_INLINE void CascadingRetryTransaction()
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::CascadingRetryTransactionInternal();
	}
}
#else
UE_AUTORTFM_CRITICAL_INLINE void CascadingRetryTransaction()
{
}
#endif

#if UE_AUTORTFM
// Have some work happen when this transaction commits. 
// In a nested transaction, the work is deferred until the outermost nest is committed;
// at that point, the worklist is run in FIFO order.
// If this is called outside a transaction or from an open nest, then the work
// happens immediately.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnCommit(FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnCommitInternal(std::forward<FunctorType>(Work));
	}
	else
	{
		UE_AUTORTFM_CALLSITE_FORCEINLINE Work();
	}
}
#else
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnCommit(FunctorType&& Work) { Work(); }
#endif

#if UE_AUTORTFM
// Have some work happen when this transaction aborts (before memory rollback).
// If an abort occurs, the work list is run in LIFO order.
// If this is called outside a transaction or from an open nest then the work is ignored.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnPreAbort(FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnPreAbortInternal(std::forward<FunctorType>(Work));
	}
}
#else
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnPreAbort(FunctorType&&) {}
#endif

#if UE_AUTORTFM
// Have some work happen when this transaction aborts (after memory rollback).
// If an abort occurs, the work list is run in LIFO order.
// If this is called outside a transaction or from an open nest then the work is ignored.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnAbort(FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnAbortInternal(std::forward<FunctorType>(Work));
	}
}
#else
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnAbort(FunctorType&&) {}
#endif

#if UE_AUTORTFM
// Have some work happen when the transaction completes, whether that transaction is committed or aborted.
// In a nested transaction, the work is deferred until the very end of the outermost nest, right before
// the end of the transaction.
// The worklist is run in FIFO order, after the OnCommit or OnAbort worklist has finished.
// If this is called outside a transaction or from an open nest, then the work is ignored.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnComplete(FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::OnCompleteInternal(std::forward<FunctorType>(Work));
	}
}
#else
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void OnComplete(FunctorType&&) {}
#endif

#if UE_AUTORTFM
// Register a handler for transaction commit. Takes a key parameter so that
// the handler can be unregistered (see `PopOnCommitHandler`). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void PushOnCommitHandler(const void* Key, FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PushOnCommitHandlerInternal(Key, std::forward<FunctorType>(Work));
	}
}
#else
// Register a handler for transaction commit. Takes a key parameter so that
// the handler can be unregistered (see `PopOnCommitHandler`). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void PushOnCommitHandler(const void*, FunctorType&&) {}
#endif

#if UE_AUTORTFM
// Unregister the most recently pushed handler (via `PushOnCommitHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopOnCommitHandler(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopOnCommitHandlerInternal(Key);
	}
}
#else
// Unregister the most recently pushed handler (via `PushOnCommitHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopOnCommitHandler(const void*) {}
#endif

#if UE_AUTORTFM
// Unregister all pushed handlers (via `PushOnCommitHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopAllOnCommitHandlers(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopAllOnCommitHandlersInternal(Key);
	}
}
#else
// Unregister all pushed handlers (via `PushOnCommitHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopAllOnCommitHandlers(const void*) {}
#endif

#if UE_AUTORTFM
// Register a handler for transaction abort. Takes a key parameter so that
// the handler can be unregistered (see `PopOnAbortHandler`). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void PushOnAbortHandler(const void* Key, FunctorType&& Work)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PushOnAbortHandlerInternal(Key, std::forward<FunctorType>(Work));
	}
}
#else
// Register a handler for transaction abort. Takes a key parameter so that
// the handler can be unregistered (see `PopOnAbortHandler`). This is useful
// for scoped mutations that need an abort handler present unless execution
// reaches the end of the relevant scope.
template<typename FunctorType> UE_AUTORTFM_CRITICAL_INLINE void PushOnAbortHandler(const void* Key, FunctorType&&) {}
#endif

#if UE_AUTORTFM
// Unregister the most recently pushed handler (via `PushOnAbortHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopOnAbortHandler(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopOnAbortHandlerInternal(Key);
	}
}
#else
// Unregister the most recently pushed handler (via `PushOnAbortHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopOnAbortHandler(const void* Key)
{
	UE_AUTORTFM_UNUSED(Key);
}
#endif

#if UE_AUTORTFM
// Unregister all pushed handlers (via `PushOnAbortHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopAllOnAbortHandlers(const void* Key)
{
	if (autortfm_is_closed())
	{
		ForTheRuntime::PopAllOnAbortHandlersInternal(Key);
	}
}
#else
// Unregister all pushed handlers (via `PushOnAbortHandler`) for the given key.
UE_AUTORTFM_CRITICAL_INLINE void PopAllOnAbortHandlers(const void* Key)
{
	UE_AUTORTFM_UNUSED(Key);
}
#endif

struct FHeapRedirectCallbacks
{
	uint32_t AddressSpace;
	void (*RedirectedLoad)(void* DestPointer, uint64_t Size, uint64_t SourceAddress);
	void (*RedirectedStore)(uint64_t DestAddress, uint64_t Size, const void* SourcePointer);
};

#if UE_AUTORTFM
void RegisterHeapRedirectCallbacks(FHeapRedirectCallbacks Callbacks);
#else
UE_AUTORTFM_CRITICAL_INLINE void RegisterHeapRedirectCallbacks(FHeapRedirectCallbacks /*Callbacks*/)
{
}
#endif

// Inform the runtime that we have performed a new object allocation. It's only
// necessary to call this inside of custom malloc implementations. As an
// optimization, you can choose to then only have your malloc return the pointer
// returned by this function. It's guaranteed to be equal to the pointer you
// passed, but it's blessed specially from the compiler's perspective, leading
// to some nice optimizations. This does nothing when called from open code.
UE_AUTORTFM_CRITICAL_INLINE void* DidAllocate(void* Ptr, size_t Size)
{
    return autortfm_did_allocate(Ptr, Size);
}

// Inform the runtime that we have free'd a given memory location.
UE_AUTORTFM_CRITICAL_INLINE void DidFree(void* Ptr)
{
    autortfm_did_free(Ptr);
}

// Informs the runtime that a block of memory is about to be overwritten in the open.
// During a transaction, this allows the runtime to copy the data in preparation for
// a possible abort. Normally, tracking memory overwrites should be automatically
// handled by AutoRTFM, but manual overwrite tracking may be required for third-party
// libraries or outside compilers (such as ISPC).
UE_AUTORTFM_CRITICAL_INLINE void RecordOpenWrite(void* Ptr, size_t Size)
{
	autortfm_record_open_write(Ptr, Size);
}

// Informs the runtime that a block of memory is about to be overwritten in the open.
// During a transaction, this allows the runtime to copy the data in preparation for
// a possible abort. Normally, tracking memory overwrites should be automatically
// handled by AutoRTFM, but manual overwrite tracking may be required for third-party
// libraries or outside compilers (such as ISPC).
template<typename Type> UE_AUTORTFM_CRITICAL_INLINE void RecordOpenWrite(Type* Ptr)
{
	autortfm_record_open_write(Ptr, sizeof(Type));
}

// Same as RecordOpenWrite() but marks the write as ignorable by the memory validator.
UE_AUTORTFM_CRITICAL_INLINE void RecordOpenWriteNoMemoryValidation(void* Ptr, size_t Size)
{
	autortfm_record_open_write_no_memory_validation(Ptr, Size);
}

// Same as RecordOpenWrite() but marks the write as ignorable by the memory validator.
template<typename Type> UE_AUTORTFM_CRITICAL_INLINE void RecordOpenWriteNoMemoryValidation(Type* Ptr)
{
	autortfm_record_open_write_no_memory_validation(Ptr, sizeof(Type));
}

// Report that a unreachable codepath is being hit. Used to manually ban certain codepaths
// from being transactionally safe.
#if UE_AUTORTFM_ENABLED
[[noreturn]] UE_AUTORTFM_CRITICAL_INLINE_ALWAYS void Unreachable(const char* Message = nullptr)
{
	autortfm_unreachable(Message);
}
#else
UE_AUTORTFM_CRITICAL_INLINE void Unreachable(const char* Message = nullptr)
{
	UE_AUTORTFM_UNUSED(Message);
}
#endif

// If we are running within a transaction, call `AutoRTFM::Unreachable`.
UE_AUTORTFM_CRITICAL_INLINE_ALWAYS void UnreachableIfTransactional(const char* Message = nullptr)
{
	if (AutoRTFM::IsTransactional())
	{
		AutoRTFM::Unreachable(Message);
	}
}

// If we are running within a closed transaction, call `AutoRTFM::Unreachable`.
UE_AUTORTFM_CRITICAL_INLINE_ALWAYS void UnreachableIfClosed(const char* Message = nullptr)
{
	if (AutoRTFM::IsClosed())
	{
		AutoRTFM::Unreachable(Message);
	}
}

// Evaluates to true if a call to the function with type FuncType with arguments
// of the given types resolves to a function overload that is AutoRTFM-disabled.
// Warning: This is an experimental API and may be removed in the future.
template <typename FuncType, typename ... ArgTypes>
static constexpr bool CallIsDisabled = AUTORTFM_CALL_IS_DISABLED(std::declval<FuncType>()(std::declval<ArgTypes>()...));

// Evaluates to true if the destructor of type Type is AutoRTFM-disabled.
// Warning: This is an experimental API and may be removed in the future.
template <typename Type>
static constexpr bool DestructorIsDisabled = AUTORTFM_CALL_IS_DISABLED(std::declval<Type>().~Type());

// Evaluates to true if the constructor of Type with the given arguments is AutoRTFM-disabled.
// Warning: This is an experimental API and may be removed in the future.
template <typename Type, typename ... ArgTypes>
static constexpr bool ConstructorIsDisabled = AUTORTFM_CALL_IS_DISABLED(::new Type(std::declval<ArgTypes>()...));

// A collection of power-user functions that are reserved for use by the AutoRTFM runtime only.
namespace ForTheRuntime
{
	// An enum to represent the various ways we want to enable/disable the AutoRTFM runtime.
	// This enum has effective groups of functionality such that if a higher priority group
	// has enabled or disabled the runtime, a lower priority group cannot then override that.
	// 
	// We have from higher to lower priority:
	// - Forced Enabled/Disabled - used by CVars when force enabling/disabling AutoRTFM.
	// - Override Enabled/Disabled - override any setting of enabled/disabled as was set by a CVar.
	// - Enabled/Disabled - used by CVars when enabling/disabling AutoRTFM.
	// - Default Enabled/Disabled - whether we should be enabled or disabled by default (used for different backend executables).
	//
	// For example the following would be valid:
	// - At compile time the state is compiled in as default disabled.
	// - The CVar is set to enabled, so we switch the state to enabled.
	// - At runtime we detect a mode where we want AutoRTFM and try to switch the default to enabled, but the CVar already enabled it so this is ignored.
	// - Then for a given codepath we override AutoRTFM to disabled so we switch the state to disabled.
	enum EAutoRTFMEnabledState
	{
		// Disable AutoRTFM.
		AutoRTFM_Disabled = 0,

		// Enable AutoRTFM.
		AutoRTFM_Enabled,

		// Force disable AutoRTFM.
		AutoRTFM_ForcedDisabled,

		// Force enable AutoRTFM.
		AutoRTFM_ForcedEnabled,

		// Whether our default is to be disabled.
		AutoRTFM_DisabledByDefault,

		// Whether our default is to be enabled.
		AutoRTFM_EnabledByDefault,

		// Whether we've overridden and AutoRTFM is disabled.
		AutoRTFM_OverriddenDisabled,

		// Whether we've overridden and AutoRTFM is enabled.
		AutoRTFM_OverriddenEnabled,
	};

	// An enum to represent whether we should abort and retry transactions (for testing purposes).
	enum EAutoRTFMRetryTransactionState
	{
		// Do not abort and retry transactions (the default).
		NoRetry = 0,

		// Abort and retry non-nested transactions (EG. only abort the parent transactional nest).
		RetryNonNested,

		// Abort and retry nested-transactions too. Will be slower as each nested-transaction will
		// be aborted and retried at least *twice* (once when the non-nested transaction runs the
		// first time, and a second time when the non-nested transaction is doing its retry after
		// aborting).
		RetryNestedToo,
	};

	enum EAutoRTFMInternalAbortActionState
	{
		// Crash the process if we hit an internal AutoRTFM abort.
		Crash = 0,

		// Just do a normal transaction abort and let the runtime recover (used to test aborting codepaths).
		Abort,
	};

	// Set whether the AutoRTFM runtime is enabled or disabled. Returns true when the state was changed
	// successfully.
	UE_AUTORTFM_API bool SetAutoRTFMRuntime(EAutoRTFMEnabledState State);

	UE_AUTORTFM_API bool IsAutoRTFMRuntimeEnabledInternal();

	// Query whether the AutoRTFM runtime is enabled.
	UE_AUTORTFM_CRITICAL_INLINE bool IsAutoRTFMRuntimeEnabled()
	{
		// If we are already in the closed nest of a transaction, we must have our runtime enabled!
		if (AutoRTFM::IsClosed())
		{
			return true;
		}

		return IsAutoRTFMRuntimeEnabledInternal();
	}

	// Set the percentage [0..100] chance that a call to `CoinTossDisable` will end up disabling AutoRTFM.
	// 100% means never disable via coin-toss, 0% means always disable. So passing `0.1` means disable
	// all but 1/1000's calls via `CoinTossDisable`.
	UE_AUTORTFM_API void SetAutoRTFMEnabledProbability(float Chance);

	// Get the enabled probability set via `SetAutoRTFMEnabledProbability`.
	UE_AUTORTFM_API float GetAutoRTFMEnabledProbability();

	// Call to randomly disable AutoRTFM with a probability set with `SetAutoRTFMEnabledProbability`.
	// Returns true if AutoRTFM was disabled by this call.
	UE_AUTORTFM_API bool CoinTossDisable();

	UE_AUTORTFM_API void SetInternalAbortAction(EAutoRTFMInternalAbortActionState State);

	UE_AUTORTFM_API EAutoRTFMInternalAbortActionState GetInternalAbortAction();
	
	UE_AUTORTFM_API bool GetEnsureOnInternalAbort();
	UE_AUTORTFM_API void SetEnsureOnInternalAbort(bool bEnabled);

	// Set whether we should trigger an ensure on an abort-by-language.
	[[deprecated("Use `SetEnsureOnInternalAbort` instead!")]]
	inline void SetEnsureOnAbortByLanguage(bool bEnabled)
	{
		SetEnsureOnInternalAbort(bEnabled);
	}

	// Returns whether the runtime will trigger an ensure on an abort-by-language, or not.
	[[deprecated("Use `GetEnsureOnInternalAbort` instead!")]]
	inline bool IsEnsureOnAbortByLanguageEnabled()
	{
		return GetEnsureOnInternalAbort();
	}

	// Returns whether we want to assert or ensure on a Language Error
	[[deprecated("Use `GetInternalAbortAction` instead!")]]
	inline bool IsAutoRTFMAssertOnError()
	{
		return EAutoRTFMInternalAbortActionState::Crash == GetInternalAbortAction();
	}

	// Set whether we should retry transactions.
	UE_AUTORTFM_API void SetRetryTransaction(EAutoRTFMRetryTransactionState State);

	// Returns whether we should retry transactions.
	UE_AUTORTFM_API EAutoRTFMRetryTransactionState GetRetryTransaction();

	// Returns true if we should retry non-nested transactions.
	UE_AUTORTFM_API bool ShouldRetryNonNestedTransactions();

	// Returns true if we should also retry nested transactions.
	UE_AUTORTFM_API bool ShouldRetryNestedTransactionsToo();

	// Returns the memory validation level currently enabled.
	UE_AUTORTFM_API EMemoryValidationLevel GetMemoryValidationLevel();

	// Sets the memory validation level. See IsWriteValidationEnabled().
	UE_AUTORTFM_API void SetMemoryValidationLevel(EMemoryValidationLevel Level);

	// Returns true if the memory validation throttling is enabled.
	UE_AUTORTFM_API bool GetMemoryValidationThrottlingEnabled();

	// Sets the memory validation throttling mode.
	UE_AUTORTFM_API void SetMemoryValidationThrottlingEnabled(bool bEnabled);

	// Returns true if the memory validation statistics are enabled.
	UE_AUTORTFM_API bool GetMemoryValidationStatisticsEnabled();

	// Sets whether memory validation statistics are logged or not.
	UE_AUTORTFM_API void SetMemoryValidationStatisticsEnabled(bool bEnabled);

	// A debug helper that will break to the debugger if the hash of the memory
	// write locations no longer matches the hash recorded when the transaction
	// was opened. Useful for isolating where the open write happened.
	// Requires the memory validation to be enabled to be called.
	UE_AUTORTFM_API void DebugBreakIfMemoryValidationFails();

	// Manually create a new transaction from open code and push it as a transaction nest.
	// Can only be called within an already active parent transaction (EG. this cannot start
	// a transaction nest itself).
	AUTORTFM_DISABLE UE_AUTORTFM_API void StartTransaction();
	
	// Manually commit the top transaction nest, popping it from the execution scope.
	// Can only be called within an already active parent transaction (EG. this cannot end
	// a transaction nest itself).
	AUTORTFM_DISABLE UE_AUTORTFM_API ETransactionResult CommitTransaction();

	// Manually clear the status of a user abort from the top transaction in a nest.
	AUTORTFM_DISABLE UE_AUTORTFM_API void ClearTransactionStatus();

	// Returns the current transaction status.
	AUTORTFM_DISABLE UE_AUTORTFM_API EContextStatus GetContextStatus();

	// RollbackTransaction() aborts the current transaction.
	// If the transaction is scoped, then this is kept on the transaction stack
	// until we return to the closed. Attempting to use this transaction before
	// transitioning to the closed is undefined behaviour.
	// If the transaction is unscoped, then this is popped immediately.
	// RollbackTransaction() sets the transaction status to AbortedByRequest.
	AUTORTFM_DISABLE UE_AUTORTFM_API void RollbackTransaction();

	// CascadingAbortRollbackTransaction() rolls back the current scoped 
	// transaction, which must be a scoped transaction (i.e. created by
	// AutoRTFM::Transact() and not by StartTransaction()).
	// CascadingAbortRollbackTransaction() sets the transaction status to
	// AbortedByCascadingAbort, which will cause the next open -> closed
	// transition point to abort all transactions on the transaction nest.
	// Attempting to use any transactions in the open while unwinding is
	// undefined behaviour.
	AUTORTFM_DISABLE UE_AUTORTFM_API void CascadingAbortRollbackTransaction();

	// CascadingRetryRollbackTransaction() rolls back the current scoped 
	// transaction, which must be a scoped transaction (i.e. created by
	// AutoRTFM::Transact() and not by StartTransaction()).
	// CascadingRetryRollbackTransaction() sets the transaction status to
	// AbortedByCascadingRetry, which will cause the next open -> closed
	// transition point to abort all transactions on the transaction nest,
	// and then retry the outermost transaction.
	// Attempting to use any transactions in the open while unwinding is
	// undefined behaviour.
	AUTORTFM_DISABLE UE_AUTORTFM_API void CascadingRetryRollbackTransaction();

	// Reserved for future.
	UE_AUTORTFM_CRITICAL_INLINE void RecordOpenRead(void const*, size_t) {}

	// Reserved for future.
	template<typename Type> UE_AUTORTFM_CRITICAL_INLINE void RecordOpenRead(Type*) {}

} // namespace ForTheRuntime

} // namespace AutoRTFM

// Macro-based variants so we completely compile away when not in use, even in debug builds
#if UE_AUTORTFM

namespace AutoRTFM::Private
{
	struct FOpenHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::Open(std::forward<FunctorType>(F));
		}
	};
	struct FOpenNoMemoryValidationHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::Open<EMemoryValidationLevel::Disabled>(std::forward<FunctorType>(F));
		}
	};
	struct FCloseHelper
	{
		template<typename FunctorType>
		[[nodiscard]] EContextStatus operator+(FunctorType F)
		{
			return AutoRTFM::Close(std::move(F));
		}
	};
	struct FOnPreAbortHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::OnPreAbort(std::forward<FunctorType>(F));
		}
	};
	struct FOnAbortHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::OnAbort(std::forward<FunctorType>(F));
		}
	};
	struct FOnCommitHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::OnCommit(std::forward<FunctorType>(F));
		}
	};
	struct FTransactHelper
	{
		template<typename FunctorType>
		UE_AUTORTFM_FORCEINLINE void operator+(FunctorType&& F)
		{
			AutoRTFM::Transact(std::forward<FunctorType>(F));
		}
	};
	namespace /* must have internal linkage */
	{
		struct FThreadLocalHelper
		{
			template <typename Type, int Unique>
			UE_AUTORTFM_ALWAYS_OPEN static Type& Get()
			{
				thread_local Type Data;
				return Data;
			}
		};
	}
} // namespace AutoRTFM::Private

#define UE_AUTORTFM_DECLARE_THREAD_LOCAL_VAR_IMPL(Type, Name) Type& Name = ::AutoRTFM::Private::FThreadLocalHelper::Get<Type, __COUNTER__>()

#define UE_AUTORTFM_OPEN_IMPL          ::AutoRTFM::Private::FOpenHelper{} + [&]()
#define UE_AUTORTFM_OPEN_NO_VALIDATION_IMPL ::AutoRTFM::Private::FOpenNoMemoryValidationHelper{} + [&]()
#define UE_AUTORTFM_CLOSE_IMPL         ::AutoRTFM::Private::FCloseHelper{} + [&]()
#define UE_AUTORTFM_ONPREABORT_IMPL(...)  ::AutoRTFM::Private::FOnPreAbortHelper{} + [__VA_ARGS__]() mutable
#define UE_AUTORTFM_ONABORT_IMPL(...)  ::AutoRTFM::Private::FOnAbortHelper{} + [__VA_ARGS__]() mutable
#define UE_AUTORTFM_ONCOMMIT_IMPL(...) ::AutoRTFM::Private::FOnCommitHelper{} + [__VA_ARGS__]() mutable
#define UE_AUTORTFM_TRANSACT_IMPL      ::AutoRTFM::Private::FTransactHelper{} + [&]()
#else

// Do nothing, these should be followed by blocks that should be either executed or not executed
#define UE_AUTORTFM_DECLARE_THREAD_LOCAL_VAR_IMPL(Type, Name) thread_local Type Name
#define UE_AUTORTFM_OPEN_IMPL
#define UE_AUTORTFM_OPEN_NO_VALIDATION_IMPL
#define UE_AUTORTFM_CLOSE_IMPL
#define UE_AUTORTFM_ONPREABORT_IMPL(...) while (false)
#define UE_AUTORTFM_ONABORT_IMPL(...) while (false)
#define UE_AUTORTFM_ONCOMMIT_IMPL(...)
#define UE_AUTORTFM_TRANSACT_IMPL
#endif

// Declares an AutoRTFM-aware thread local variable. `thread_local` variables are not yet natively supported (#jira SOL-7684)
// Calls should be written like this: 
//     UE_AUTORTFM_DECLARE_THREAD_LOCAL_VAR(FString, MyThreadLocalString); 
//     MyThreadLocalString = "Hello";
#define UE_AUTORTFM_DECLARE_THREAD_LOCAL_VAR(Type, Name) UE_AUTORTFM_DECLARE_THREAD_LOCAL_VAR_IMPL(Type, Name)

// Runs a block of code in the open, non-transactionally. Anything performed in the open will not be undone if a transaction fails.
// Calls should be written like this: UE_AUTORTFM_OPEN { ... code ... };
#define UE_AUTORTFM_OPEN UE_AUTORTFM_OPEN_IMPL

#define UE_AUTORTFM_OPEN_NO_VALIDATION UE_AUTORTFM_OPEN_NO_VALIDATION_IMPL

// Runs a block of code in the closed, transactionally. Anything performed in the closed will be undone if a transaction fails.
// Calls should be written like this: UE_AUTORTFM_CLOSE { ... code ... };
#define UE_AUTORTFM_CLOSE UE_AUTORTFM_CLOSE_IMPL

// Runs a block of code if a transaction aborts (before memory rollback).
// In non-transactional code paths the block of code will not be executed at all.
// The macro arguments are the capture specification for the lambda.
// Calls should be written like this: UE_AUTORTFM_ONPREABORT(=) { ... code ... };
#define UE_AUTORTFM_ONPREABORT(...)  UE_AUTORTFM_ONPREABORT_IMPL(__VA_ARGS__)

// Runs a block of code if a transaction aborts (after memory rollback).
// In non-transactional code paths the block of code will not be executed at all.
// The macro arguments are the capture specification for the lambda.
// Calls should be written like this: UE_AUTORTFM_ONABORT(=) { ... code ... };
#define UE_AUTORTFM_ONABORT(...)  UE_AUTORTFM_ONABORT_IMPL(__VA_ARGS__)

// Runs a block of code if a transaction commits successfully.
// In non-transactional code paths the block of code will be executed immediately.
// The macro arguments are the capture specification for the lambda.
// Calls should be written like this: UE_AUTORTFM_ONCOMMIT(=) { ... code ... };
#define UE_AUTORTFM_ONCOMMIT(...)  UE_AUTORTFM_ONCOMMIT_IMPL(__VA_ARGS__)

// Runs a block of code in the closed, transactionally, within a new transaction.
// Calls should be written like this: UE_AUTORTFM_TRANSACT { ... code ... };
#define UE_AUTORTFM_TRANSACT  UE_AUTORTFM_TRANSACT_IMPL

#if UE_AUTORTFM
#define UE_AUTORTFM_REGISTER_OPEN_TO_CLOSED_FUNCTIONS(...) \
	static const ::AutoRTFM::ForTheRuntime::TAutoRegisterOpenToClosedFunctions<__VA_ARGS__> UE_AUTORTFM_CONCAT(AutoRTFMFunctionRegistration, __COUNTER__)
#else
#define UE_AUTORTFM_REGISTER_OPEN_TO_CLOSED_FUNCTIONS(...)
#endif

#endif // __cplusplus
