// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Stats.h"

#include "Utils.h"

#include <inttypes.h>

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::Transaction>(const uint64_t Data) const
{
	AUTORTFM_LOG("  Total transactions:        %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::Commit>(const uint64_t Data) const
{
	AUTORTFM_LOG("  Total commits:             %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::Abort>(const uint64_t Data) const
{
	AUTORTFM_LOG("  Total aborts:              %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageTransactionDepth>(const uint64_t Data) const
{
	const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
	AUTORTFM_LOG("  Average transaction depth: %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::MaximumTransactionDepth>(const uint64_t Data) const
{
    AUTORTFM_LOG("  Maximum transaction depth: %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageWriteLogEntries>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average write log entries: %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::MaximumWriteLogEntries>(const uint64_t Data) const
{
    AUTORTFM_LOG("  Maximum write log entries: %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageWriteLogBytes>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average write log bytes:   %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::MaximumWriteLogBytes>(const uint64_t Data) const
{
    AUTORTFM_LOG("  Maximum write log bytes:   %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::HitSetHit>(const uint64_t Data) const
{
    AUTORTFM_LOG("  HitSet hits:               %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::HitSetMiss>(const uint64_t Data) const
{
    AUTORTFM_LOG("  HitSet misses:             %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>(const uint64_t Data) const
{
    AUTORTFM_LOG("  HitSet skip (stack local): %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageCommitTasks>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average commit tasks:      %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::MaximumCommitTasks>(const uint64_t Data) const
{
    AUTORTFM_LOG("  Maximum commit tasks:      %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageAbortTasks>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average abort tasks:       %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::MaximumAbortTasks>(const uint64_t Data) const
{
    AUTORTFM_LOG("  Maximum abort tasks:       %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::NewMemoryTrackerHit>(const uint64_t Data) const
{
    AUTORTFM_LOG("  New memory hits:           %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::NewMemoryTrackerMiss>(const uint64_t Data) const
{
    AUTORTFM_LOG("  New memory misses:         %" PRIu64, Data);
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageHitSetSize>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average hit set size:      %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));
}

template<> void AutoRTFM::FStats::Report<AutoRTFM::EStatsKind::AverageHitSetCapacity>(const uint64_t Data) const
{
    const uint64_t TotalTransactions = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::Transaction)];
    AUTORTFM_LOG("  Average hit set capacity:  %15.3f", (static_cast<double>(Data) / static_cast<double>(TotalTransactions)));

    const uint64_t TotalHitSetSize = Datas[static_cast<size_t>(AutoRTFM::EStatsKind::AverageHitSetSize)];
    AUTORTFM_LOG("  Average hit set occupancy: %15.3f", 100 * (static_cast<double>(TotalHitSetSize) / static_cast<double>(Data)));
}

void AutoRTFM::FStats::Report() const
{
	if constexpr (bCollectStats)
	{
		AUTORTFM_LOG("AutoRTFM Statistics:");

		for (size_t I = 0; I < static_cast<size_t>(AutoRTFM::EStatsKind::Total); I++)
		{
			switch (static_cast<EStatsKind>(I))
			{
			default: AutoRTFM::InternalUnreachable(); break;
#define REPORT_CASE(x) case (x): Report<x>(Datas[static_cast<size_t>(x)]); break
			REPORT_CASE(AutoRTFM::EStatsKind::Transaction);
			REPORT_CASE(AutoRTFM::EStatsKind::Commit);
			REPORT_CASE(AutoRTFM::EStatsKind::Abort);
			REPORT_CASE(AutoRTFM::EStatsKind::AverageTransactionDepth);
            REPORT_CASE(AutoRTFM::EStatsKind::MaximumTransactionDepth);
            REPORT_CASE(AutoRTFM::EStatsKind::AverageWriteLogEntries);
            REPORT_CASE(AutoRTFM::EStatsKind::MaximumWriteLogEntries);
			REPORT_CASE(AutoRTFM::EStatsKind::AverageWriteLogBytes);
			REPORT_CASE(AutoRTFM::EStatsKind::MaximumWriteLogBytes);
            REPORT_CASE(AutoRTFM::EStatsKind::HitSetHit);
            REPORT_CASE(AutoRTFM::EStatsKind::HitSetMiss);
            REPORT_CASE(AutoRTFM::EStatsKind::HitSetSkippedBecauseOfStackLocalMemory);
            REPORT_CASE(AutoRTFM::EStatsKind::AverageCommitTasks);
            REPORT_CASE(AutoRTFM::EStatsKind::MaximumCommitTasks);
            REPORT_CASE(AutoRTFM::EStatsKind::AverageAbortTasks);
            REPORT_CASE(AutoRTFM::EStatsKind::MaximumAbortTasks);
            REPORT_CASE(AutoRTFM::EStatsKind::NewMemoryTrackerHit);
            REPORT_CASE(AutoRTFM::EStatsKind::NewMemoryTrackerMiss);
			REPORT_CASE(AutoRTFM::EStatsKind::AverageHitSetSize);
			REPORT_CASE(AutoRTFM::EStatsKind::AverageHitSetCapacity);
#undef REPORT_CASE
			}
		}
	}
}

AutoRTFM::FStats AutoRTFM::Stats;

#endif // defined(__AUTORTFM) && __AUTORTFM
