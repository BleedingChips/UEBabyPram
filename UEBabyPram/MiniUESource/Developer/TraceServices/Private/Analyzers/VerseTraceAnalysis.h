// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/Array.h"
#include "Containers/Map.h"

#include "Trace/Analyzer.h"

namespace TraceServices
{

class IAnalysisSession;
class FTimingProfilerProvider;

class FVerseAnalyzer : public UE::Trace::IAnalyzer
{
private:
	enum : uint16
	{
		RouteId_DeclareString,
		RouteId_BytecodeSample,
		RouteId_NativeSample,
	};

public:
	FVerseAnalyzer(IAnalysisSession& Session, FTimingProfilerProvider& InTimingProfilerProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	uint32 GetUnknownVerseTimerId();
	uint32 GetVerseBytecodeTimerId();
	uint32 GetVerseNativeTimerId();

private:
	IAnalysisSession& Session;
	FTimingProfilerProvider& TimingProfilerProvider;

	TArray<uint32> LastSampleStack;
	double LastSampleTime = 0.0;
	uint32 NumSamples = 0;
	int32 MaxStackSize = 0;

	TMap<uint32, uint32> Timers; // string id -> timer id
	uint32 UnknownTimerId = 0;
	uint32 BytecodeTimerId = 0;
	uint32 NativeTimerId = 0;
	uint32 NumVerseTimers = 0;

	static constexpr double MaxSampleDuration = 0.002; // 2ms
};

} // namespace TraceServices
