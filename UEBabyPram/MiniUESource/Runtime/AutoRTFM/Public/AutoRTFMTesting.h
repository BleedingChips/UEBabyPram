// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM.h"

namespace AutoRTFM::Testing
{

// Implemented by the test framework, and called when AUTORTFM_TESTING_ASSERT() fails.
void AssertionFailure(const char* Expression, const char* File, int Line);

#define AUTORTFM_TESTING_ASSERT(Condition) \
	do { if (!(Condition)) { AssertionFailure("AUTORTFM_TESTING_ASSERT(" #Condition ")", __FILE__, __LINE__); }} while (false)

// Run the callback in a transaction like Transact, but abort program
// execution if the result is anything other than autortfm_committed.
// Useful for testing.
template<typename TFunctor>
static UE_AUTORTFM_FORCEINLINE void Commit(const TFunctor& Functor)
{
	const ETransactionResult Result = Transact(Functor);
	AUTORTFM_TESTING_ASSERT(ETransactionResult::Committed == Result);
}

// Run the callback in a transaction like Transact, but abort program
// execution if the result is anything other than abort.
template<typename TFunctor>
static UE_AUTORTFM_FORCEINLINE void Abort(const TFunctor& Functor)
{
	const ETransactionResult Result = Transact(Functor);
	AUTORTFM_TESTING_ASSERT(ETransactionResult::Committed != Result);
}

// Force set the AutoRTFM runtime state. Used in testing. Returns the old value.
UE_AUTORTFM_API ForTheRuntime::EAutoRTFMEnabledState ForceSetAutoRTFMRuntime(ForTheRuntime::EAutoRTFMEnabledState State);

struct FEnabledStateResetterScoped final
{
	FEnabledStateResetterScoped(ForTheRuntime::EAutoRTFMEnabledState State) : Original(ForceSetAutoRTFMRuntime(State)) {}
	~FEnabledStateResetterScoped() { ForceSetAutoRTFMRuntime(Original); }

private:
	const ForTheRuntime::EAutoRTFMEnabledState Original;
};

#undef AUTORTFM_TESTING_ASSERT

} // namespace AutoRTFM::Testing
