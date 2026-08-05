
#include "Trace/Analyzer.h"
#include "Templates/Tuple.h"
#include "UEBabyPramInsightParserAnalysisContext.h"

namespace UEBabyPram::InsightParser
{

	void ContextThreadWrapper::AppendBeginEvent(double StartTime, const TraceServices::FTimingProfilerEvent& Event)
	{

	}
	
	void ContextThreadWrapper::AppendEndEvent(double EndTime)
	{
		
	}

	void AnalysisContext::AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority)
	{
		TUniquePtr<ContextThreadWrapper>* Found = Threads.Find(Id);
		if (!Found)
		{
			Threads.Add(Id, MakeUnique<ContextThreadWrapper>(Id, this));
		}
	}

	IEditableTimeline<FTimingProfilerEvent>& AnalysisContext::GetCpuThreadEditableTimeline(uint32 ThreadId)
	{
		TUniquePtr<ContextThreadWrapper>* Found = Threads.Find(ThreadId);
		if (Found)
		{
			return *(Found->Get());
		}

		return *Threads.Add(ThreadId, MakeUnique<ContextThreadWrapper>(ThreadId, this));
	}
}
