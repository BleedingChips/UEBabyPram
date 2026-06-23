// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheStats.h"

#if WITH_TESTS

#include "Tests/TestHarnessAdapter.h"

namespace UE::DerivedData
{

TEST_CASE_NAMED(FDerivedDataCacheTimeAveragedStatTest, "DerivedData::Cache::Stats::TimeAveragedStat", "[DerivedData]")
{
	const auto MakeTime = [](double Seconds) { return FMonotonicTimePoint::FromSeconds(Seconds); };

	FTimeAveragedStat Stat;
	Stat.SetPeriod(FMonotonicTimeSpan::FromSeconds(8.0));

	// Test StartTime after active window.
	Stat.Add(MakeTime(0.0), MakeTime(8.0), 1024.0); // A
	Stat.Add(MakeTime(16.0), MakeTime(32.0), 128.0); // B

	// Test 128b over 8s -> 16b/s.
	CHECK(Stat.GetRate(MakeTime(32.0)) == 16.0);   // (B)
	CHECK(Stat.GetValue(MakeTime(32.0)) == 128.0); // (B)

	// Test EndTime before active window.
	Stat.Add(MakeTime(16.0), MakeTime(24.0), 128.0); // C

	// Test 128b over 4s -> 32b/s.
	CHECK(Stat.GetRate(MakeTime(36.0)) == 32.0);   // (B)
	CHECK(Stat.GetValue(MakeTime(36.0)) == 128.0); // (B)

	// Test a range that is entirely past existing ranges.
	Stat.Add(MakeTime(40.0), MakeTime(48.0), 1024.0); // D

	// Test 1024b over 8s -> 128b/s.
	CHECK(Stat.GetRate(MakeTime(48.0)) == 128.0);   // (D)
	CHECK(Stat.GetValue(MakeTime(48.0)) == 1024.0); // (D)

	// Test that adding new values within the current window recalculates averages.
	Stat.Add(MakeTime(38.0), MakeTime(46.0), 2048.0); // E

	// Test 3072b over 8s -> 384b/s.
	CHECK(Stat.GetRate(MakeTime(48.0)) == 384.0);   // (D, E)
	CHECK(Stat.GetValue(MakeTime(48.0)) == 1536.0); // (D, E)

	// Test 3072b over 6s -> 512b/s.
	CHECK(Stat.GetRate(MakeTime(50.0)) == 512.0);   // (D, E)
	CHECK(Stat.GetValue(MakeTime(50.0)) == 1536.0); // (D, E)

	// Test a disjoint range that fits a previous range in the same window.
	Stat.Add(MakeTime(52.0), MakeTime(54.0), 512.0); // F

	// Test 1536b over 2+2s -> 384b/s.
	CHECK(Stat.GetRate(MakeTime(54.0)) == 384.0);  // (D, F)
	CHECK(Stat.GetValue(MakeTime(54.0)) == 768.0); // (D, F)

	// Test a disjoint range in the middle of a previous range in the same window.
	Stat.Add(MakeTime(50.5), MakeTime(51.0), 1536.0); // G

	// Test 3072b over 1.5+0.5+2s -> 768b/s.
	CHECK(Stat.GetRate(MakeTime(54.5)) == 768.0);   // (D, F, G)
	CHECK(Stat.GetValue(MakeTime(54.5)) == 1024.0); // (D, F, G)

	// Test a range that extends a previous range earlier in the same window.
	Stat.Add(MakeTime(50.0), MakeTime(51.0), 1024.0); // H

	// Test 4096b over 1+1+2s -> 1024b/s.
	CHECK(Stat.GetRate(MakeTime(55.0)) == 1024.0);  // (D, F, G, H)
	CHECK(Stat.GetValue(MakeTime(55.0)) == 1024.0); // (D, F, G, H)

	// Test a range that overlaps two previous ranges in the same window and ends before the later one.
	Stat.Add(MakeTime(46.5), MakeTime(50.5), 1536.0); // I

	// Test 5632b over 3.5+2s -> 1024b/s.
	CHECK(Stat.GetRate(MakeTime(55.5)) == 1024.0);  // (D, F, G, H, I)
	CHECK(Stat.GetValue(MakeTime(55.5)) == 1126.4); // (D, F, G, H, I)

	// Test a range that overlaps two previous ranges and ends after the later one.
	Stat.Add(MakeTime(50.0), MakeTime(53.0), 512.0); // J

	// Test 5120b over 5s -> 1024b/s.
	CHECK(Stat.GetRate(MakeTime(57.0)) == 1024.0);  // (F, G, H, I, J)
	CHECK(Stat.GetValue(MakeTime(57.0)) == 1024.0); // (F, G, H, I, J)

	// Test an empty range that touches the start of the last existing range.
	Stat.Add(MakeTime(70.0), MakeTime(74.0), 128.0); // K
	Stat.Add(MakeTime(70.0), MakeTime(70.0), 128.0); // L

	// Test 256b over 4s -> 64b/s.
	CHECK(Stat.GetRate(MakeTime(74.0)) == 64.0);   // (K, L)
	CHECK(Stat.GetValue(MakeTime(74.0)) == 128.0); // (K, L)

	// Test an empty range that touches the start of the first existing range.
	Stat.Add(MakeTime(75.0), MakeTime(76.0), 256.0); // M
	Stat.Add(MakeTime(70.0), MakeTime(70.0), 128.0); // N

	// Test 640b over 5s -> 128b/s.
	CHECK(Stat.GetRate(MakeTime(76.0)) == 128.0);  // (K, L, M, N)
	CHECK(Stat.GetValue(MakeTime(76.0)) == 160.0); // (K, L, M, N)
}

} // UE::DerivedData

#endif // WITH_TESTS
