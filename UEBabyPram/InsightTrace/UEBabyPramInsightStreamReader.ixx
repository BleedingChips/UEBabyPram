module;

#include <cassert>

export module UEBabyPramInsightEngine;

import std;
import Potato;
import UEBabyPramInsightDefine;
import UEBabyPramInsightAnalyzer;

export namespace UEBabyPram::InsightParser
{
	class FAnalysisEngine
	{
	public:
		FAnalysisEngine(TArray<IAnalyzer*>&& Analyzers, FMessageDelegate&& InMessage);
		~FAnalysisEngine();
		void				Begin();
		void				End();
		bool				OnData(FStreamReader& Reader);

	private:
		class FImpl;
		FAnalysisEngine(const FAnalysisEngine&) = delete;
		FAnalysisEngine(const FAnalysisEngine&&) = delete;
		FAnalysisEngine		operator = (const FAnalysisEngine&) = delete;
		FAnalysisEngine		operator = (const FAnalysisEngine&&) = delete;
		FImpl* Impl;
	};
}