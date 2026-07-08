// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallstacksAnalysis.h"

#include "HAL/LowLevelMemTracker.h"
#include "Model/CallstacksProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Common/Utils.h"

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////
FCallstacksAnalyzer::FCallstacksAnalyzer(IAnalysisSession& InSession, FCallstacksProvider* InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
	check(Provider != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FCallstacksAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_Callstack, "Memory", "CallstackSpec");
}

////////////////////////////////////////////////////////////////////////////////
bool FCallstacksAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FCallstacksAnalyzer"));

	switch (RouteId)
	{
		case RouteId_Callstack:
			const TArrayReader<uint64>& Frames = Context.EventData.GetArray<uint64>("Frames");
			uint8 NumFrames = (uint8)FMath::Min(255u, Frames.Num());
			if (const uint32 Id = Context.EventData.GetValue<uint32>("CallstackId"))
			{
				if (NumFrames != Frames.Num())
				{
					UE_LOG(LogTraceServices, Warning, TEXT("Callstack with Id=%u has %u frames, but it will be limited to %u frames!"), Id, Frames.Num(), NumFrames);
				}
				Provider->AddCallstack(Id, Frames.GetData(), NumFrames);
			}
			// Backward compatibility with legacy memory trace format (5.0-EA).
			else if (const uint64 Hash = Context.EventData.GetValue<uint64>("Id"))
			{
				if (NumFrames != Frames.Num())
				{
					UE_LOG(LogTraceServices, Warning, TEXT("Callstack with Hash=%llu has %u frames, but it will be limited to %u frames!"), Hash, Frames.Num(), NumFrames);
				}
				Provider->AddCallstackWithHash(Hash, Frames.GetData(), NumFrames);
			}
			break;
	}
	return true;
}

} // namespace TraceServices
