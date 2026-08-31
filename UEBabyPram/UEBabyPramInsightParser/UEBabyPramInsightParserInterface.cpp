
#include "Trace/Analyzer.h"
#include "Analysis/Engine.h"
#include "Analysis/StreamReader.h"
#include "TraceServices/AnalyzerFactories.h"
#include "Templates/UniquePtr.h"
#include "Trace\DataStream.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"

#include "UEBabyPramInsightParserAnalysisInterface.h"
#include "UEBabyPramInsightParserCPUAnalysis.h"
#include "UEBabyPramInsightParserInterface.h"
#include "UEBabyPramInsightParserPlatformAnalysis.h"
#include "UEBabyPramInsightParserAnalysisContext.h"

namespace UEBabyPram::InsightParser
{

	void ExecuteParser(DataResourceInterface& resource, BaseParser& parser)
	{

		AnalysisContext ana_context{};

		CPUScopeAnalyzer cpu_analyzer{ parser, ana_context };

		FPlatformEventTraceAnalyzer platform_event_analyzer{ parser };
		//TSharedPtr<TraceServices::IAnalysisSession> Session = TraceServices::CreateAnalysisSession(0, nullptr, {});

		//FSummarizeCpuProfilerProvider CpuProfilerProvider;
		//TSharedPtr<UE::Trace::IAnalyzer> CpuProfilerAnalyzer = TraceServices::CreateCpuProfilerAnalyzer(*Session, CpuProfilerProvider, CpuProfilerProvider);

		TArray<UE::Trace::IAnalyzer*> List = { &cpu_analyzer, &platform_event_analyzer };
		UE::Trace::FMessageDelegate Delegate;
		UE::Trace::FAnalysisEngine engine{ std::move(List), std::move(Delegate) };

		engine.Begin();

		UE::Trace::FStreamBuffer Buffer(4 << 20);
		while (true)
		{

			int32 BytesRead = Buffer.Fill([&](uint8* Out, uint32 Size)
				{
					return resource.Read(Out, Size);
				});

			if (BytesRead <= 0)
			{
				break;
			}

			if (!engine.OnData(Buffer))
			{
				break;
			}
		}

		engine.End();

		parser.AllAnalyzeDone();
		
	}

	bool BaseParser::TryReadFromMetaData(MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, char const* field_name, wchar_t const*& out_string, std::size_t& string_len)
	{
		if (meta_data == nullptr || field_name == nullptr)
			return false;

		switch (format)
		{
		case MetaDataFormat::EventData:
		{
			auto EventData = reinterpret_cast<UE::Trace::IAnalyzer::FEventData const*>(meta_data);
			FStringView view;
			if (EventData->GetString(field_name, view))
			{
				out_string = view.GetData();
				string_len = view.Len();
				return true;
			}
			break;
		}
		default:
			break;
		}
		return false;
	}
}

//export import UEBabyPramInsightInterface;