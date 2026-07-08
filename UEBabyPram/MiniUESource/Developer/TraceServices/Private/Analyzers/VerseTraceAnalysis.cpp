// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseTraceAnalysis.h"

#include "CborWriter.h"
#include "HAL/LowLevelMemTracker.h"
#include "Serialization/MemoryWriter.h"

// TraceServices
#include "Common/ProviderLock.h"
#include "Common/Utils.h"
#include "Model/TimingProfilerPrivate.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

FVerseAnalyzer::FVerseAnalyzer(IAnalysisSession& InSession, FTimingProfilerProvider& InTimingProfilerProvider)
	: Session(InSession)
	, TimingProfilerProvider(InTimingProfilerProvider)
{
}

void FVerseAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	// temporary backward compatibility
	Builder.RouteEvent(RouteId_DeclareString,  "VerseVMSampler", "StringEvent");
	Builder.RouteEvent(RouteId_BytecodeSample, "VerseVMSampler", "SampleEvent");
	Builder.RouteEvent(RouteId_NativeSample,   "VerseVMSampler", "NativeSampleEvent");

	Builder.RouteEvent(RouteId_DeclareString,  "Verse", "DeclareString");
	Builder.RouteEvent(RouteId_BytecodeSample, "Verse", "BytecodeSample");
	Builder.RouteEvent(RouteId_NativeSample,   "Verse", "NativeSample");
}

void FVerseAnalyzer::OnAnalysisEnd()
{
	if (NumSamples > 0)
	{
		FAnalysisSessionEditScope _(Session);
		IEditableTimeline<FTimingProfilerEvent>* Timeline = TimingProfilerProvider.GetVerseEditableTimeline();
		const double EndTime = LastSampleTime + MaxSampleDuration / 2.0;
		if (Timeline)
		{
			const int32 LastSampleStackSize = LastSampleStack.Num();
			for (int32 StackIndex = 0; StackIndex < LastSampleStackSize; ++StackIndex)
			{
				Timeline->AppendEndEvent(EndTime);
			}
			// Close the last "bytecode" or "native" event.
			Timeline->AppendEndEvent(EndTime);
		}
		Session.UpdateDurationSeconds(EndTime);
		LastSampleStack.Reset();
	}
	Timers.Reset();

	UE_LOG(LogTraceServices, Log, TEXT("[Verse] Analysis completed (%u timers, %u samples; max stack size = %d)."), NumVerseTimers, NumSamples, MaxStackSize);
}

bool FVerseAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Insights/FVerseAnalyzer"));

	switch (RouteId)
	{
	case RouteId_DeclareString:
	{
		const auto& EventData = Context.EventData;
		const uint32 Id = EventData.GetValue<uint32>("Id");
		FString Name;
		EventData.GetString("Name", Name);

		UE_LOG(LogTraceServices, VeryVerbose, TEXT("[Verse] DeclareString Id=0x%X Name=\"%s\""), Id, *Name);

		uint32 TimerId = 0;
		uint32* FoundTimerId = Timers.Find(Id);
		if (FoundTimerId)
		{
			TimerId = *FoundTimerId;
			UE_LOG(LogTraceServices, Warning, TEXT("[Verse] Duplicated timer Id=0x%X Name=\"%s\" --> TimerId=%u"), Id, *Name, TimerId);
		}
		else
		{
			{
				FAnalysisSessionEditScope _(Session);
				TimerId = TimingProfilerProvider.AddTimer(ETimingProfilerTimerType::VerseSampling, Name);
				++NumVerseTimers;
			}
			Timers.Add(Id, TimerId);
		}
		break;
	}
	case RouteId_BytecodeSample:
	case RouteId_NativeSample:
	{
		const auto& EventData = Context.EventData;
		uint64 Cycles = EventData.GetValue<uint64>("Cycles");
		double Time = Cycles != 0 ? Context.EventTime.AsSeconds(Cycles) : LastSampleTime + MaxSampleDuration / 2;
		TArrayView<const uint32> CallstackData = EventData.GetArrayView<uint32>("Callstack");

		TArray<uint32> Callstack;
		Callstack.Reserve(CallstackData.Num());
		for (int32 StackIndex = CallstackData.Num() - 1; StackIndex >= 0; --StackIndex)
		{
			Callstack.Add(CallstackData[StackIndex]);
		}

		// Bytecode Sample Event
		const int32 Offset = EventData.GetValue<int32>("BytecodeOffset", 0);
		const int32 Line = EventData.GetValue<int32>("Line", 0);

		const int32 LastSampleStackSize = LastSampleStack.Num();
		const int32 NewSampleStackSize = Callstack.Num();

		if (NewSampleStackSize > MaxStackSize)
		{
			MaxStackSize = NewSampleStackSize;
		}
		const bool bStackSizeWarning = (NewSampleStackSize >= 256);

		if (RouteId == RouteId_BytecodeSample)
		{
			if (bStackSizeWarning)
			{
				UE_LOG(LogTraceServices, Warning, TEXT("[Verse] BytecodeSample Time=%f [%d callstack frames] BytecodeOffset=%d Line=%d"), Time, NewSampleStackSize, Offset, Line);
			}
			else
			{
				UE_LOG(LogTraceServices, VeryVerbose, TEXT("[Verse] BytecodeSample Time=%f [%d callstack frames] BytecodeOffset=%d Line=%d"), Time, NewSampleStackSize, Offset, Line);
			}
		}
		else
		{
			if (bStackSizeWarning)
			{
				UE_LOG(LogTraceServices, Warning, TEXT("[Verse] NativeSample Time=%f [%d callstack frames]"), Time, NewSampleStackSize);
			}
			else
			{
				UE_LOG(LogTraceServices, VeryVerbose, TEXT("[Verse] NativeSample Time=%f [%d callstack frames]"), Time, NewSampleStackSize);
			}
		}

		// Ensure monotonic increase of time values.
		if (!ensure(Time >= LastSampleTime))
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[Verse] Time should increase monotonically (Time=%f < %f)!"), Time, LastSampleTime);
			Time = LastSampleTime;
		}

		const double LastSampleEndTime = LastSampleTime + MaxSampleDuration / 2;
		const double CurrentSampleStartTime = FMath::Max(0.0, Time - MaxSampleDuration / 2);

		constexpr double Tolerance = 0.5; // 50%; i.e. 1ms for 2ms intervals

		int32 StartNewStackIndex = 0;
		if (CurrentSampleStartTime - MaxSampleDuration * Tolerance <= LastSampleEndTime)
		{
			for (int32 StackIndex = 0; StackIndex < LastSampleStackSize; ++StackIndex)
			{
				if ((StackIndex >= NewSampleStackSize) ||
					(Callstack[StackIndex] != LastSampleStack[StackIndex]))
				{
					break;
				}
				StartNewStackIndex = StackIndex + 1;
			}
		}

		{
			FAnalysisSessionEditScope _(Session);
			IEditableTimeline<FTimingProfilerEvent>* Timeline = TimingProfilerProvider.GetVerseEditableTimeline();
			if (Timeline)
			{
				const double MidTime = (LastSampleTime + Time) / 2.0;
				const double EndTime = FMath::Min(LastSampleEndTime, MidTime);
				const double StartTime = FMath::Max(CurrentSampleStartTime, MidTime);

				for (int32 StackIndex = StartNewStackIndex; StackIndex < LastSampleStackSize; ++StackIndex)
				{
					Timeline->AppendEndEvent(EndTime);
				}
				if (NumSamples > 0)
				{
					// Close the previous "bytecode" or "native" event.
					Timeline->AppendEndEvent(EndTime);
				}
				for (int32 StackIndex = StartNewStackIndex; StackIndex < NewSampleStackSize; ++StackIndex)
				{
					FTimingProfilerEvent Event;
					const uint32* FoundTimerId = Timers.Find(Callstack[StackIndex]);
					Event.TimerIndex = FoundTimerId ? *FoundTimerId : GetUnknownVerseTimerId();
					Timeline->AppendBeginEvent(StartTime, Event);
				}
				if (RouteId == RouteId_BytecodeSample)
				{
					TArray<uint8> CborData;
					{
						CborData.Reserve(32);
						FMemoryWriter MemoryWriter(CborData, false, true);
						FCborWriter CborWriter(&MemoryWriter, ECborEndianness::StandardCompliant);
						CborWriter.WriteContainerStart(ECborCode::Map, 2); // 2 is the FieldCount
						CborWriter.WriteValue("Offset", 6);
						CborWriter.WriteValue((int64)Offset);
						CborWriter.WriteValue("Line", 4);
						CborWriter.WriteValue((int64)Line);
					}

					FTimingProfilerEvent Event;
					Event.TimerIndex = TimingProfilerProvider.AddMetadata(GetVerseBytecodeTimerId(), MoveTemp(CborData));
					Timeline->AppendBeginEvent(StartTime, Event);
				}
				else
				{
					FTimingProfilerEvent Event;
					Event.TimerIndex = GetVerseNativeTimerId();
					Timeline->AppendBeginEvent(StartTime, Event);
				}
			}
			Session.UpdateDurationSeconds(Time);
		}

		// LastSampleStack = Callstack
		if (StartNewStackIndex < LastSampleStackSize)
		{
			LastSampleStack.RemoveAt(StartNewStackIndex, LastSampleStackSize - StartNewStackIndex, EAllowShrinking::No);
		}
		for (int32 StackIndex = StartNewStackIndex; StackIndex < NewSampleStackSize; ++StackIndex)
		{
			LastSampleStack.Push(Callstack[StackIndex]);
		}

		LastSampleTime = Time;
		NumSamples++;
		break;
	}
	} // switch (RouteId)

	return true;
}

uint32 FVerseAnalyzer::GetUnknownVerseTimerId()
{
	if (!UnknownTimerId)
	{
		FAnalysisSessionEditScope _(Session);
		UnknownTimerId = TimingProfilerProvider.AddTimer(TraceServices::ETimingProfilerTimerType::VerseSampling, TEXTVIEW("<unknown>"));
		++NumVerseTimers;
	}
	return UnknownTimerId;
}

uint32 FVerseAnalyzer::GetVerseBytecodeTimerId()
{
	if (!BytecodeTimerId)
	{
		FAnalysisSessionEditScope _(Session);
		BytecodeTimerId = TimingProfilerProvider.AddTimer(TraceServices::ETimingProfilerTimerType::VerseSampling, TEXTVIEW(">bytecode"));
		++NumVerseTimers;
	}
	return BytecodeTimerId;
}

uint32 FVerseAnalyzer::GetVerseNativeTimerId()
{
	if (!NativeTimerId)
	{
		FAnalysisSessionEditScope _(Session);
		NativeTimerId = TimingProfilerProvider.AddTimer(TraceServices::ETimingProfilerTimerType::VerseSampling, TEXTVIEW(">native"));
		++NumVerseTimers;
	}
	return NativeTimerId;
}

} // namespace TraceServices
