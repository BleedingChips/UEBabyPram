// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "HashMap.h"

namespace AutoRTFM
{

// Forward declaration
class FWriteLog;

/// A utility class to adaptively adjust the time spent hashing in the
/// transactional memory validator. The memory validator performs a hash of
/// all transactional memory writes when transitioning between closed to open
/// and open to closed. Some of these opens can be very frequent and the number
/// of writes being hashed is unbounded, so the time spent in the validation can
/// vary from negligible to orders of magnitude longer than the rest of the 
/// application's execution.
/// This utility is used to keep validation performance acceptable by skipping
/// validation for a percentage of the percentage closed <-> open transitions.
/// The percentage of transitions skipped is adjusted by the amount of time spent
/// validating, and on a per-open basis (identified using the open's return 
/// address).
class FOpenHashThrottler
{
public:
	using FSeconds = double;

	/// A RAII helper for the scope of a hash function.
	class FHashScope
	{
	public:
		// Constructor - Begins timing a validation hash.
		// * Throttler is the AutoRTFM FOpenHashThrottler.
		// * OpenReturnAddress is the return address for the call to AutoRTFM::Open(). Used to identify the open.
		// * WriteLog is the write log that is being hashed. Used only for statistics logging.
		FHashScope(FOpenHashThrottler& Throttler, const void* OpenReturnAddress, const FWriteLog& WriteLog);
		~FHashScope();
	private:
		FOpenHashThrottler& Throttler;
		const FWriteLog& WriteLog;
		const void* const OpenReturnAddress;
		FSeconds StartTime;
	};

	// Constructor
	// * LogInterval is time between each statistics log
	// * AdjustThrottleInterval is time between adjustments to hash probabilities.
	// * TargetFractionHashing is target fraction of time spent hashing / total time.
	FOpenHashThrottler(FSeconds LogInterval, FSeconds AdjustThrottleInterval, FSeconds TargetFractionHashing);

	// Returns the probability (0: never hash, 1: always hash) the given open return address should be hashed.
	// * OpenReturnAddress is the return address for the call to AutoRTFM::Open(). Used to identify the open.
	double HashProbabilityFor(const void* OpenReturnAddress);

	// Returns true if the open with the given return address should perform memory validation.
	// * OpenReturnAddress is the return address for the call to AutoRTFM::Open(). Used to identify the open.
	bool ShouldHashFor(const void* OpenReturnAddress);

	// Updates the profiler with timings for an open hash.
	// * HashStart is the wall-clock time before hashing begun
	// * HashStart is the wall-clock time after hashing ended
	// * OpenReturnAddress is the return address for the call to AutoRTFM::Open(). Used to identify the open.
	// * WriteLog is the write log that is being hashed. Used only for statistics logging.
	void OnHash(FSeconds HashStart, FSeconds HashEnd, const void* OpenReturnAddress, const FWriteLog& WriteLog);

	// Periodically adjusts the probabilities for hashing opens, and prints statistics.
	// * DeltaTime the time since the last call to Update(), or 0 to calculate using an internal clock.
	void Update(FSeconds DeltaTime = 0);

private:
	void UpdateThrottlingData();
	void UpdateLogStats();

	struct FOpenAddressInfo
	{
		// Total time spent hashing this open since last reset.
		FSeconds TimeSpentHashing = 0;
		// Throttled probability to hash this open.
		double Probability = 0;
		// True if the open address was queried or hashed since last throttling update.
		bool bActive = true;
	};
	
	// Data used for throttling hashing
	struct FThrottlingData
	{
		// Timestamp since this was last updated.
		FSeconds TimeSinceLastUpdate = 0;
		// Total time spent hashing for all opens since last reset.
		FSeconds TotalTimeSpentHashing = 0;
		// A map of open return address to open info.
		THashMap<const void*, FOpenAddressInfo> Opens;
		// The default hash probability if the open return address is not found
		// in Opens.
		double DefaultHashProbability = 1;
	};
	
	// Statistics for logging
	struct FLogStats
	{
		// Timestamp since this was last reset.
		FSeconds TimeSinceLastReset = 0;
		// Total time spent hashing since last reset.
		FSeconds TimeSpentHashing = 0;
		// Number of hash calls since last reset.
		size_t NumHashCalls = 0;
		// Number of bytes hashed since last reset.
		size_t NumBytesHashed = 0;
		// Number of write records hashed since last reset.
		size_t NumWriteRecords = 0;
		// Number of times ShouldHashFor() return true.
		size_t NumShouldHashForTrue = 0;
		// Number of times ShouldHashFor() return false.
		size_t NumShouldHashForFalse = 0;
	};

	const FSeconds LogInterval;
	const FSeconds AdjustThrottleInterval;
	const FSeconds TargetFractionHashing;

	FThrottlingData ThrottlingData;
	FLogStats LogStats;
	FSeconds LastUpdateTimestamp = 0;
};

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
