module;

#include <cassert>

export module UEBabyPramInsightParser;

import std;
import Potato;
import UEBabyPramInsightDefine;
import UEBabyPramInsightTransport;



export namespace UEBabyPram::InsightParser
{
	struct InsightContext
	{
		std::shared_ptr<FTransport> transport;
	};

	bool ForEachInsight(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter());
}

namespace UEBabyPram::InsightParser
{
	bool MetaDataStage(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter());
	bool EstablishTransportStage(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter());
}