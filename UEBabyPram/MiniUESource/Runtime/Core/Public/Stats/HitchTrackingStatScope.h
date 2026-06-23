// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Build.h"
#include "Stats/StatsCommon.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "ProfilingDebugging/UMemoryDefines.h"
#endif

#ifndef USE_LIGHTWEIGHT_STATS_FOR_HITCH_DETECTION
#define USE_LIGHTWEIGHT_STATS_FOR_HITCH_DETECTION 1
#endif

#if USE_LIGHTWEIGHT_STATS_FOR_HITCH_DETECTION && USE_HITCH_DETECTION
extern CORE_API TSAN_ATOMIC(bool) GHitchDetected;

namespace UE::Stats
{
	class FHitchTrackingStatScope
	{
		const PROFILER_CHAR* StatString;
	public:
		UE_FORCEINLINE_HINT FHitchTrackingStatScope(const PROFILER_CHAR* InStat)
		{
			StatString = GHitchDetected ? nullptr : InStat;
		}

		inline ~FHitchTrackingStatScope()
		{
			if (GHitchDetected && StatString)
			{
				ReportHitch();
			}
		}

		CORE_API void ReportHitch();
	};
} // namespace UE::Stats

UE_DEPRECATED(5.6, "Use UE::Stats::FHitchTrackingStatScope instead.")
typedef UE::Stats::FHitchTrackingStatScope FLightweightStatScope;

#endif // USE_LIGHTWEIGHT_STATS_FOR_HITCH_DETECTION && USE_HITCH_DETECTION