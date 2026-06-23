// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

namespace AutoRTFM
{

struct FAutoRTFMMetrics
{
	int64_t NumTransactionsStarted;
	int64_t NumTransactionsCommitted;

	int64_t NumTransactionsAborted;
	int64_t NumTransactionsAbortedByRequest;
	int64_t NumTransactionsAbortedByLanguage;
};

// reset the internal metrics tracking back to zero
void ResetAutoRTFMMetrics();

// get a snapshot of the current internal metrics
FAutoRTFMMetrics GetAutoRTFMMetrics();

}
