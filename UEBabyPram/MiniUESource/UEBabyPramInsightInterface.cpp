module;

#include "Trace/Analyzer.h"
#include "Trace/DataStream.h"
#include "Containers/StringView.h"
#include "Analysis/Engine.h"
#include "Analysis/StreamReader.h"

module UEBabyPramInsightInterface;

import std;

struct Analyzer : public UE::Trace::IAnalyzer
{
	virtual bool OnNewEvent(std::uint16_t RouteId, const FEventTypeInfo& TypeInfo)
	{
		return true;
	}

	/** For each event subscribed to in OnAnalysisBegin(), the analysis engine
	 * will call this method when those events are encountered in a trace log
	 * @param RouteId User-provided identifier given when subscribing to a particular event.
	 * @param Style Indicates the style of event. Note that EventData is *undefined* if the style is LeaveScope!
	 * @param Context Access to the instance of the subscribed event.
	 * @return This analyzer is removed from the analysis session if false is returned. */
	virtual bool OnEvent(std::uint16_t RouteId, EStyle Style, const FOnEventContext& Context)
	{
		return true;
	}
};



namespace UEBabyPram::InsightParser
{
	using namespace UE::Trace;
	bool Test(std::filesystem::path path)
	{
		FFileDataStream file;
		if (file.Open(path.c_str()))
		{
			Analyzer analyze;
			TArray<IAnalyzer*> analyzers;
			FMessageDelegate delegate = [](EAnalysisMessageSeverity, FStringView) {
				volatile int i = 0;
				};
			FAnalysisEngine engine{ std::move(analyzers), std::move(delegate)};

			engine.Begin();
			FStreamBuffer Buffer(4 << 20);
			while (true)
			{

				int32 BytesRead = Buffer.Fill([&](uint8* Out, uint32 Size)
					{
						return file.Read(Out, Size);
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


			return true;
		}
		return false;
	}
}