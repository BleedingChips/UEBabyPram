// Copyright Epic Games, Inc. All Rights Reserved.

// IWYU pragma: private
// Include include "AutoRTFM.h" instead

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)
#define UE_AUTORTFM 1  // Compiler is 'verse-clang'
#else
#define UE_AUTORTFM 0
#endif

#if (defined(__AUTORTFM_ENABLED) && __AUTORTFM_ENABLED)
#define UE_AUTORTFM_ENABLED 1  // Compiled with '-fautortfm'
#else
#define UE_AUTORTFM_ENABLED 0
#endif

#if !defined(UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT)
#define UE_AUTORTFM_ENABLED_RUNTIME_BY_DEFAULT 1
#endif

#if !defined(UE_AUTORTFM_STATIC_VERIFIER)
#define UE_AUTORTFM_STATIC_VERIFIER 0
#endif

#if UE_AUTORTFM
// The annotated function will have no AutoRTFM closed variant generated, and
// cannot be called from another closed function. This attribute will eventually
// be deprecated and replaced with AUTORTFM_DISABLE.
#define UE_AUTORTFM_NOAUTORTFM [[clang::noautortfm]]

// Omits AutoRTFM instrumentation from the function. Calling this annotated
// function from the closed will automatically enter the open for the duration
// of the call and will return back to the closed.
#define UE_AUTORTFM_ALWAYS_OPEN [[clang::autortfm_always_open]]

// The same as UE_AUTORTFM_ALWAYS_OPEN, but disables memory validation for this
// call.
#define UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION [[clang::autortfm_always_open_disable_memory_validation]]

// Annotation that can be applied to classes, methods or functions to prevent
// AutoRTFM closed function(s) from being generated.
// Applying the annotation to a class is equivalent to applying the annotation
// to each method of the class.
// When annotating a function, the attribute must be placed on a the function
// declaration (usually in the header) and not a function implementation.
// Annotated functions cannot be called from another closed function and
// attempting to do so will result in a runtime failure.
#define AUTORTFM_DISABLE [[clang::autortfm(disable)]]

// Applies the AUTORTFM_DISABLE annotation if the condition argument evaluates to true.
// Warning: This is an experimental API and may be removed in the future.
#define AUTORTFM_DISABLE_IF(...) [[clang::autortfm(disable, __VA_ARGS__)]]

// Begins a range where all classes and functions will be automatically
// annotated with AUTORTFM_DISABLE. Must be ended with AUTORTFM_DISABLE_END
// before the end of the file.
#define AUTORTFM_DISABLE_BEGIN _Pragma("clang attribute AutoRTFM_Disable.push (AUTORTFM_DISABLE, apply_to = any(function, record))")

// Ends a range started with AUTORTFM_DISABLE_BEGIN
#define AUTORTFM_DISABLE_END _Pragma("clang attribute AutoRTFM_Disable.pop")

// Annotation that can be applied to classes, methods or functions to re-enable
// AutoRTFM instrumentation which would otherwise be disabled by AUTORTFM_DISABLE.
// Useful for selectively enabling AutoRTFM on methods when a class is annotated
// AUTORTFM_DISABLE.
#define AUTORTFM_ENABLE [[clang::autortfm(enable)]]

// Annotation that can be applied to classes, methods or functions to infer
// whether AutoRTFM instrumentation should be enabled for each individual
// function based on the AutoRTFM-enabled state of each callee made by the
// function. If the function calls any AutoRTFM-disabled function, then the
// function will also be AutoRTFM-disabled, otherwise the function is 
// AutoRTFM-enabled.
// Applying the annotation to a class is equivalent to applying the annotation
// to each method of the class.
// When annotating a function, the attribute must be placed on a the function
// declaration (usually in the header) and not a function implementation.
#define AUTORTFM_INFER [[clang::autortfm(infer)]]

// Annotation that can be applied to classes, methods or functions to prevent
// AutoRTFM closed function(s) from being generated. Unlike AUTORTFM_DISABLE
// annotated functions can be called from closed functions, which will call
// the uninstrumented function.
// Applying the annotation to a class is equivalent to applying the annotation
// to each method of the class.
// When annotating a function, the attribute must be placed on a the function
// declaration (usually in the header) and not a function implementation.
#define AUTORTFM_OPEN [[clang::autortfm(open)]]

// Similar to AUTORTFM_OPEN, but disables memory validation on the call.
#define AUTORTFM_OPEN_NO_VALIDATION [[clang::autortfm(open_no_validation)]]

// Evaluates to a constant true if:
// * EXPR_OR_TYPE is an address of a AutoRTFM-disabled function or method
// * EXPR_OR_TYPE is a type of a AutoRTFM-disabled class or struct
// Warning: This is an experimental API and may be removed in the future.
#define AUTORTFM_IS_DISABLED(EXPR_OR_TYPE) __autortfm_is_disabled(EXPR_OR_TYPE)

// Evaluates to a constant true if CALL_EXPR is a call a function, method,
// constructor (new T(...)) or destructor, and the call target is
// AutoRTFM-disabled.
// Warning: This is an experimental API and may be removed in the future.
#define AUTORTFM_CALL_IS_DISABLED(CALL_EXPR) __autortfm_is_disabled(__autortfm_declcall(CALL_EXPR))

// Force the call statement to be inlined.
#define UE_AUTORTFM_CALLSITE_FORCEINLINE [[clang::always_inline]]

#else // ^^^ UE_AUTORTFM ^^^ | vvv !UE_AUTORTFM vvv

#define UE_AUTORTFM_NOAUTORTFM
#define UE_AUTORTFM_ALWAYS_OPEN
#define UE_AUTORTFM_ALWAYS_OPEN_NO_MEMORY_VALIDATION
#define AUTORTFM_DISABLE
#define AUTORTFM_DISABLE_IF(CONDITION)
#define AUTORTFM_DISABLE_BEGIN
#define AUTORTFM_DISABLE_END
#define AUTORTFM_ENABLE
#define AUTORTFM_INFER
#define AUTORTFM_OPEN
#define AUTORTFM_OPEN_NO_VALIDATION
#define AUTORTFM_IS_DISABLED(EXPR_OR_TYPE) false
#define AUTORTFM_CALL_IS_DISABLED(EXPR_OR_TYPE) false
#define UE_AUTORTFM_CALLSITE_FORCEINLINE

#endif

#ifdef __cplusplus
#define AUTORTFM_NOEXCEPT noexcept
#define AUTORTFM_EXCEPT noexcept(false)
#else
#define AUTORTFM_NOEXCEPT
#define AUTORTFM_EXCEPT
#endif

#if UE_AUTORTFM && UE_AUTORTFM_STATIC_VERIFIER
#define UE_AUTORTFM_ENSURE_SAFE [[clang::autortfm_ensure_safe]]
#define UE_AUTORTFM_ASSUME_SAFE [[clang::autortfm_assume_safe]]
#else
#define UE_AUTORTFM_ENSURE_SAFE
#define UE_AUTORTFM_ASSUME_SAFE
#endif

#define UE_AUTORTFM_CONCAT_IMPL(A, B) A ## B
#define UE_AUTORTFM_CONCAT(A, B) UE_AUTORTFM_CONCAT_IMPL(A, B)

#if defined(__cplusplus) && defined(__UNREAL__) && !(defined(UE_AUTORTFM_DO_NOT_INCLUDE_PLATFORM_H) && UE_AUTORTFM_DO_NOT_INCLUDE_PLATFORM_H)
	// Include HAL/Platform.h for DLLIMPORT / DLLEXPORT definitions, which
	// UBT can use as a definition for AUTORTFM_API.
	#include <HAL/Platform.h>
	#define UE_AUTORTFM_API AUTORTFM_API
#else
	#ifndef UE_AUTORTFM_API
		#define UE_AUTORTFM_API
	#endif
#endif

#if defined(_MSC_VER)
#define UE_AUTORTFM_FORCEINLINE __forceinline
#define UE_AUTORTFM_FORCEINLINE_ALWAYS __forceinline
#define UE_AUTORTFM_FORCENOINLINE __declspec(noinline)
#define UE_AUTORTFM_ASSUME(x) __assume(x)
#elif defined(__clang__)
#define UE_AUTORTFM_FORCEINLINE __attribute__((always_inline)) inline
#define UE_AUTORTFM_FORCEINLINE_ALWAYS __attribute__((always_inline)) inline
#define UE_AUTORTFM_FORCENOINLINE __attribute__((noinline))
#define UE_AUTORTFM_ASSUME(x) __builtin_assume(x)
#else
#define UE_AUTORTFM_FORCEINLINE inline
#define UE_AUTORTFM_FORCEINLINE_ALWAYS inline
#define UE_AUTORTFM_FORCENOINLINE
#define UE_AUTORTFM_ASSUME(x)
#endif


#if (defined(UE_BUILD_DEBUG) && UE_BUILD_DEBUG) || (defined(AUTORTFM_BUILD_DEBUG) && AUTORTFM_BUILD_DEBUG)
// Force-inlining can make debugging glitchy. Disable this if we're running a debug build.
#undef UE_AUTORTFM_CALLSITE_FORCEINLINE
#define UE_AUTORTFM_CALLSITE_FORCEINLINE
#undef UE_AUTORTFM_FORCEINLINE
#define UE_AUTORTFM_FORCEINLINE inline
#endif

#ifdef _MSC_VER
#define AUTORTFM_DISABLE_UNREACHABLE_CODE_WARNINGS \
	__pragma (warning(push)) \
	__pragma (warning(disable: 4702)) /* unreachable code */
#define AUTORTFM_RESTORE_UNREACHABLE_CODE_WARNINGS \
	__pragma (warning(pop))
#else
#define AUTORTFM_DISABLE_UNREACHABLE_CODE_WARNINGS
#define AUTORTFM_RESTORE_UNREACHABLE_CODE_WARNINGS
#endif

#define UE_AUTORTFM_UNUSED(UNUSEDVAR) (void)UNUSEDVAR

// It is critical that these functions are both static and forceinline to prevent binary sizes to explode
// This is a trick to ensure that there will never be a non-inlined version of these functions that the linker can decide to use
#ifndef UE_HEADER_UNITS
#define UE_AUTORTFM_CRITICAL_INLINE static UE_AUTORTFM_FORCEINLINE
#define UE_AUTORTFM_CRITICAL_INLINE_ALWAYS static UE_AUTORTFM_FORCEINLINE_ALWAYS
#else
#define UE_AUTORTFM_CRITICAL_INLINE UE_AUTORTFM_FORCEINLINE // TODO: This needs to be revisited. we don't want bloated executables when modules are enabled
#define UE_AUTORTFM_CRITICAL_INLINE_ALWAYS UE_AUTORTFM_FORCEINLINE_ALWAYS // TODO: This needs to be revisited. we don't want bloated executables when modules are enabled
#endif
