// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Analysis.h"

namespace UE {
namespace Trace {

////////////////////////////////////////////////////////////////////////////////
void FAnalysisContext::AddAnalyzer(IAnalyzer& Analyzer)
{
	Analyzers.Add(&Analyzer);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisContext::SetMessageDelegate(FMessageDelegate Delegate)
{
	OnMessage = Delegate;
}

////////////////////////////////////////////////////////////////////////////////
#if 0
FAnalysisProcessor FAnalysisContext::Process(IInDataStream& DataStream)
{
	FAnalysisProcessor Processor;
	if (Analyzers.Num() > 0)
	{
		Processor.Impl = new FAnalysisProcessor::FImpl(DataStream, MoveTemp(Analyzers), MoveTemp(OnMessage));
	}
	return MoveTemp(Processor);
}
#endif

} // namespace Trace
} // namespace UE
