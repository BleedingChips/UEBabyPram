module;
#include "Trace/Analyzer.h"
#include "Analysis/Engine.h"
module UEBabyPramInsightParser;
import std;

namespace UEBabyPram::InsightParser
{
	void Test(std::filesystem::path const& paths)
	{
		TArray<UE::Trace::IAnalyzer*> List;
		UE::Trace::FMessageDelegate Delegate;
		UE::Trace::FAnalysisEngine engine{ std::move(List), std::move(Delegate)};
	}
}


//export import UEBabyPramInsightInterface;