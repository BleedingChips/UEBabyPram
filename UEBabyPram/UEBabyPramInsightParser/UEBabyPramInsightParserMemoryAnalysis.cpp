

#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Containers/StringConv.h"
#include "TraceServices/Utils.h"
#include "TraceServices/Model/Memory.h"
#include "Common/Utils.h"
#include "Serialization/MemoryWriter.h"
#include "CborWriter.h"
#include "Serialization/MemoryReader.h"
#include "CborReader.h"
#include "Containers/ArrayView.h"

#include "UEBabyPramInsightParserMemoryAnalysis.h"

namespace UEBabyPram::InsightParser
{

	using namespace TraceServices;

	FMemoryAnalyzer::FMemoryAnalyzer(BaseParser& Parser, AnalysisContext& Context)
		: Parser(Parser)
		, AnaContext(Context)
	{
	}

	FMemoryAnalyzer::~FMemoryAnalyzer()
	{
	}

	void FMemoryAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
	{
		auto& Builder = Context.InterfaceBuilder;

		// The specs and the snapshots are two parts of the channel which a parser requires separately: a parser which only
		// resolves the id of a tag to a name requires the specs, and a parser which reports the values of the tags requires
		// the snapshots as well, so the routes of the snapshots are not registered for the former.
		if (Parser.IsParserRequired(ParserRequireFlag::MEMORY_TAG))
		{
			Builder.RouteEvent(RouteId_TagsSpec, "LLM", "TagsSpec");
			Builder.RouteEvent(RouteId_TrackerSpec, "LLM", "TrackerSpec");
			Builder.RouteEvent(RouteId_TagSetSpec, "LLM", "TagSetSpec"); // added in UE 5.6
		}

		if (Parser.IsParserRequired(ParserRequireFlag::MEMORY_VALUE))
		{
			Builder.RouteEvent(RouteId_TagValue, "LLM", "TagValue");
		}
	}

	void FMemoryAnalyzer::OnAnalysisEnd()
	{
	}

	bool FMemoryAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
	{

		LLM_SCOPE_BYNAME(TEXT("Insights/FMemoryAnalyzer"));

		const auto& EventData = Context.EventData;
		switch (RouteId)
		{
		case RouteId_TagsSpec:
		{
			const int64 TagId = EventData.GetValue<int64>("TagId");
			const int64 ParentId = EventData.GetValue<int64>("ParentId");
			const uint8 TagSetId = EventData.GetValue<uint8>("TagSetId"); // added in UE 5.6
			FAnsiStringView AnsiStringView;
			EventData.GetString("Name", AnsiStringView);
			//FString Name = FTraceAnalyzerUtils::LegacyAttachmentString<TCHAR>("Name", Context);
			//FProviderEditScopeLock _(*Provider);
			//Provider->AddTagSpec(TagId, Name, ParentId, (FMemoryTagSetId)TagSetId);
			Parser.OnMemoryTagsSpecId(TagId, ParentId, TagSetId, AnsiStringView.GetData(), AnsiStringView.Len());
		}
		break;
		case RouteId_TrackerSpec:
		{
			uint8 TrackerId = EventData.GetValue<uint8>("TrackerId");
			//FString Name = FTraceAnalyzerUtils::LegacyAttachmentString<ANSICHAR>("Name", Context);
			//FProviderEditScopeLock _(*Provider);
			//Provider->AddTrackerSpec((FMemoryTrackerId)TrackerId, Name);
			FAnsiStringView AnsiStringView;
			EventData.GetString("Name", AnsiStringView);
			Parser.OnMemoryTrackerSpec(TrackerId, AnsiStringView.GetData(), AnsiStringView.Len());
		}
		break;
		case RouteId_TagSetSpec: // added in UE 5.6
		{
			const uint8 TagSetId = EventData.GetValue<uint8>("TagSetId");
			//FString Name;
			//EventData.GetString("Name", Name);
			//FProviderEditScopeLock _(*Provider);
			//Provider->AddTagSetSpec((FMemoryTagSetId)TagSetId, Name);
			FAnsiStringView AnsiStringView;
			EventData.GetString("Name", AnsiStringView);
			Parser.OnMemoryTagSetSpec(TagSetId, AnsiStringView.GetData(), AnsiStringView.Len());
		}
		break;
		case RouteId_TagValue:
		{
			const uint8 TrackerId = EventData.GetValue<uint8>("TrackerId");
			const uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			const double Time = Context.EventTime.AsSeconds(Cycle);
			const TArrayReader<int64>& Tags = EventData.GetArray<int64>("Tags"); // was traced as (void*)[]
			const TArrayReader<int64>& Samples = EventData.GetArray<int64>("Values");

			/*
			const uint32 TagsCount = Tags.Num();
			const int64* TagsData = Tags.GetData();

			TArray<int64> TagsCopy;
			if (TagsData == nullptr)
			{
				// For backward compatibility with 32bit platforms.
				const TArrayReader<uint32>& Tags32 = EventData.GetArray<uint32>("Tags");
				check(Tags32.Num() == TagsCount);
				const uint32* TagsData32 = Tags32.GetData();
				if (!TagsData32)
				{
					break;
				}
				TagsCopy.Reserve(TagsCount);
				for (uint32 TagIndex = 0; TagIndex < TagsCount; ++TagIndex)
				{
					TagsCopy.Push(TagsData32[TagIndex]);
				}
				TagsData = TagsCopy.GetData();
			}

			check(Samples.Num() == TagsCount);
			const int64* SamplesData = Samples.GetData();
			check(SamplesData != nullptr);
			TArray<FMemoryTagSample> Values;
			Values.Reserve(TagsCount);
			for (uint32 TagIndex = 0; TagIndex < TagsCount; ++TagIndex)
			{
				Values.Push(FMemoryTagSample{ SamplesData[TagIndex] });
			}

			Sample++;

			{
				//FProviderEditScopeLock _(*Provider);
				//Provider->AddTagSnapshot(TrackerId, Time, TagsData, Values.GetData(), TagsCount);
			}
			*/
			if (Tags.GetData() != nullptr)
			{
				Parser.OnMemoryTagSnap(TrackerId, Time, Tags.GetData(), Tags.Num(), Samples.GetData(), Samples.Num());
			}
			else {
				const TArrayReader<uint32>& Tags32 = EventData.GetArray<uint32>("Tags");
				if (Tags32.GetData() != nullptr)
				{
					Parser.OnMemoryTagSnap(TrackerId, Time, Tags32.GetData(), Tags32.Num(), Samples.GetData(), Samples.Num());
				}
			}
		}
		break;
		}
		return true;
	}
}


//export import UEBabyPramInsightInterface;