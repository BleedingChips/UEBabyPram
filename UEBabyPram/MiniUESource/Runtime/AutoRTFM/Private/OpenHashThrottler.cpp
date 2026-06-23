// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "OpenHashThrottler.h"

#include "Utils.h"
#include "WriteLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace AutoRTFM
{

namespace
{

double TimeInSeconds()
{
	using Clock = std::chrono::high_resolution_clock;
	using Duration = std::chrono::duration<double>;
	using TimePoint = std::chrono::time_point<Clock, Duration>;
	TimePoint Now = Clock::now();
	return Now.time_since_epoch().count();
}

}

FOpenHashThrottler::FHashScope::FHashScope(FOpenHashThrottler& Throttler, const void* OpenReturnAddress, const FWriteLog& WriteLog)
	: Throttler(Throttler)
	, WriteLog(WriteLog)
	, OpenReturnAddress(OpenReturnAddress)
	, StartTime(TimeInSeconds())
{
}

FOpenHashThrottler::FHashScope::~FHashScope()
{
	const FSeconds EndTime = TimeInSeconds();
	Throttler.OnHash(StartTime, EndTime, OpenReturnAddress, WriteLog);
}

FOpenHashThrottler::FOpenHashThrottler(FSeconds LogInterval, FSeconds AdjustThrottleInterval, FSeconds TargetFractionHashing)
	: LogInterval(LogInterval)
	, AdjustThrottleInterval(AdjustThrottleInterval)
	, TargetFractionHashing(TargetFractionHashing)
{
}

void FOpenHashThrottler::OnHash(FSeconds StartTime, FSeconds EndTime, const void* OpenReturnAddress, const FWriteLog& WriteLog)
{
	const FSeconds HashDuration = EndTime - StartTime;
	ThrottlingData.TotalTimeSpentHashing += HashDuration;
	
	FOpenAddressInfo& OpenInfo = ThrottlingData.Opens.FindOrAdd(OpenReturnAddress);
	OpenInfo.TimeSpentHashing += HashDuration;
	OpenInfo.bActive = true;
	if (OpenInfo.Probability == 0)
	{
		OpenInfo.Probability = ThrottlingData.DefaultHashProbability;
	}
	
	LogStats.TimeSpentHashing += HashDuration;
	LogStats.NumHashCalls++;
	LogStats.NumBytesHashed += WriteLog.TotalSize();
	LogStats.NumWriteRecords += WriteLog.Num();
}

double FOpenHashThrottler::HashProbabilityFor(const void* OpenReturnAddress)
{
	FOpenAddressInfo& OpenInfo = ThrottlingData.Opens.FindOrAdd(OpenReturnAddress);
	if (OpenInfo.Probability == 0)
	{
		OpenInfo.Probability = ThrottlingData.DefaultHashProbability;
	}
	OpenInfo.bActive = true;
	return OpenInfo.Probability;
}

bool FOpenHashThrottler::ShouldHashFor(const void* OpenReturnAddress)
{
	auto FRand = [] { return static_cast<double>(rand()) / static_cast<double>(RAND_MAX); };
	// Multiply the results of two calls to FRand() to get decent fractional precision.
	const double RandSqr = FRand() * FRand();
	// Negate the probability as we want to bias towards not-hashing, and if
	// either call to FRand() returns zero then RandSqr will be zero.
	const double SkipProbability = 1.0 - HashProbabilityFor(OpenReturnAddress);
	if (RandSqr > SkipProbability * SkipProbability)
	{
		LogStats.NumShouldHashForTrue++;
		return true;
	}
	LogStats.NumShouldHashForFalse++;
	return false;
}

void FOpenHashThrottler::Update(FSeconds DeltaTime /* = 0 */)
{
	const FSeconds Now = TimeInSeconds();
	if (DeltaTime == 0)
	{
		DeltaTime = (LastUpdateTimestamp > 0) ? (Now - LastUpdateTimestamp) : 0;
	}

	ThrottlingData.TimeSinceLastUpdate += DeltaTime;
	if (ThrottlingData.TimeSinceLastUpdate >= AdjustThrottleInterval)
	{
		UpdateThrottlingData();
		ThrottlingData.TimeSinceLastUpdate = 0;
	}

	LogStats.TimeSinceLastReset += DeltaTime;
	if (LogStats.TimeSinceLastReset >= LogInterval)
	{
		UpdateLogStats();
		LogStats.TimeSinceLastReset = 0;
	}

	LastUpdateTimestamp = Now;
}

void FOpenHashThrottler::UpdateThrottlingData()
{
	// Count the number of opens that were active and those that performed hashing this update.
	size_t NumActiveOpens = 0;
	size_t NumOpensThatHashed = 0;
	for (auto& It : ThrottlingData.Opens)
	{
		if (It.Value.bActive)
		{
			NumActiveOpens++;
		}
		if (It.Value.TimeSpentHashing > 0)
		{
			NumOpensThatHashed++;
		}
	}

	// Active is a superset of those that hashed.
	AUTORTFM_ENSURE(NumActiveOpens >= NumOpensThatHashed);

	if (NumActiveOpens == 0)
	{
		return; // No opens were queried or used this update.
	}

	// Probability multiplier to reach the target time spent hashing.
	const double TotalGain = TargetFractionHashing * ThrottlingData.TimeSinceLastUpdate / ThrottlingData.TotalTimeSpentHashing;
	
	// Something non-zero, so we can scale probabilities back up with multiplications.
	const double MinProbability = 1e-6;

	if (TotalGain < 0.5)
	{
		// We've exceeded our budget by 2x or greater.
		// Instead of adjusting each of the open hash probabilities individually
		// to normalize the probabilities based on time spent hashing, apply the
		// total gain to all opens. This is done to prevent long hashing stalls
		// when there are sudden increases of the write log size. In this
		// situation, newly active active can have a probability that is
		// substantially higher than those that have been tuned, and allowing
		// each of these opens to hash even once in an update can dramatically
		// exceed the budgeted time.
		for (auto& It : ThrottlingData.Opens)
		{
			It.Value.Probability = std::max(It.Value.Probability * TotalGain, MinProbability);
			It.Value.TimeSpentHashing = 0;
			It.Value.bActive = false;
		}
		ThrottlingData.DefaultHashProbability = std::max(ThrottlingData.DefaultHashProbability * TotalGain, MinProbability);
	}
	else
	{
		// Average time spent hashing per open
		const double AverageTimeSpentHashingPerOpen =
			ThrottlingData.TotalTimeSpentHashing / static_cast<double>(std::max<size_t>(NumOpensThatHashed, 1));

		// The new lowest probability across all active opens.
		double LowestProbability = 1;

		// Apply the probability multiplier and normalize the time spent in each open.
		for (auto& It : ThrottlingData.Opens)
		{
			if (!It.Value.bActive)
			{
				continue;
			}

			// Calculate the target probability to normalize the time spent in each open, and
			// to aim for the target total fractional time spent in hashing (TargetFractionHashing).

			// The time spent hashing this open this update
			const FSeconds TimeSpentHashing = It.Value.TimeSpentHashing;
			
			// Target probability starts with the current probability
			double TargetProbability = std::max(It.Value.Probability, MinProbability);

			// Adjust for the relative time spent hashing this open compared to the others.
			if (TimeSpentHashing > 0)
			{
				TargetProbability *= AverageTimeSpentHashingPerOpen / TimeSpentHashing;
			}

			// If the open was hashed this update, or probabilities are being raised
			// then adjust by the total gain for all opens.
			if (TimeSpentHashing > 0 || TotalGain > 1)
			{
				TargetProbability *= TotalGain;
			}

			// Finally clamp between 0..1
			TargetProbability = std::clamp(TargetProbability, 0.0, 1.0);
			
			if (TargetProbability < It.Value.Probability)
			{
				// Probability is being reduced.
				// Apply target probability immediately to ensure the application doesn't stall.
				It.Value.Probability = TargetProbability;
			}
			else
			{
				// Probability is being increased.
				// Interpolate to the new target probability at 15% per second.
				It.Value.Probability = Lerp(TargetProbability, It.Value.Probability, std::pow(0.85, ThrottlingData.TimeSinceLastUpdate));
			}

			// Update LowestProbability if this is the new lowest probability.
			LowestProbability = std::min(LowestProbability, It.Value.Probability);

			// Reset the TimeSpentHashing and bActive state for this open.
			It.Value.TimeSpentHashing = 0;
			It.Value.bActive = false;
		}

		// Default new opens with the lowest probability of all active opens.
		ThrottlingData.DefaultHashProbability = LowestProbability;
	}

	ThrottlingData.TotalTimeSpentHashing = 0;
}

void FOpenHashThrottler::UpdateLogStats()
{
	if (!AutoRTFM::ForTheRuntime::GetMemoryValidationStatisticsEnabled())
	{
		return;
	}

	const double TimeSinceLastLog = LogStats.TimeSinceLastReset;
	AUTORTFM_LOG(
		"Transaction Hash Statistics\n"
		"-----------------------------\n"
		/* a */ "%f / %f seconds spent hashing (%.1f%%, target: %.1f%%)\n"
		/* b */ " * %zu hash calls (avr: %.1f calls/second)\n"
		/* c */ " * %zu / %zu open validations skipped (%.1f%%)\n"
		/* d */ " * %zu bytes hashed (avr: %.1f bytes/call, %.1f bytes/second)\n"
		/* e */ " * %zu write records (avr: %.1f records/call)\n",
		/* a.0 */ LogStats.TimeSpentHashing,
		/* a.1 */ TimeSinceLastLog,
		/* a.2 */ 100 * LogStats.TimeSpentHashing / TimeSinceLastLog,
		/* a.3 */ 100 * TargetFractionHashing,
		/* b.0 */ LogStats.NumHashCalls,
		/* b.1 */ static_cast<double>(LogStats.NumHashCalls) / TimeSinceLastLog,
		/* c.0 */ LogStats.NumShouldHashForFalse,
		/* c.1 */ LogStats.NumShouldHashForTrue + LogStats.NumShouldHashForFalse,
		/* c.2 */ 100 * static_cast<double>(LogStats.NumShouldHashForFalse) / static_cast<double>(LogStats.NumShouldHashForTrue + LogStats.NumShouldHashForFalse),
		/* d.0 */ LogStats.NumBytesHashed,
		/* d.1 */ static_cast<double>(LogStats.NumBytesHashed) / static_cast<double>(LogStats.NumHashCalls),
		/* d.2 */ static_cast<double>(LogStats.NumBytesHashed) / LogStats.TimeSpentHashing,
		/* e.0 */ LogStats.NumWriteRecords,
		/* e.1 */ static_cast<double>(LogStats.NumWriteRecords) / static_cast<double>(LogStats.NumHashCalls));
		
	LogStats = FLogStats{};
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
