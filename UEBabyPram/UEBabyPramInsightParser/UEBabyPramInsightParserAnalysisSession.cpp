module;
#include "Trace/Analyzer.h"
#include "Templates/Tuple.h"

module UEBabyPramInsightParserAnalysisSession;

namespace UEBabyPram::InsightParser
{
	FAnalysisSession::FAnalysisSession(uint32 InTraceId, const TCHAR* SessionName)
		: Name(SessionName)
		, TraceId(InTraceId)
		, DurationSeconds(0.0)
		, Allocator(32 << 20)
		, StringStore(Allocator)
		, Cache(*Name)
	{
	}

	FAnalysisSession::~FAnalysisSession()
	{
		for (int32 AnalyzerIndex = Analyzers.Num() - 1; AnalyzerIndex >= 0; --AnalyzerIndex)
		{
			delete Analyzers[AnalyzerIndex];
		}
	}

	void FAnalysisSession::Start()
	{
		/*
		UE::Trace::FAnalysisContext Context;
		for (UE::Trace::IAnalyzer* Analyzer : ReadAnalyzers())
		{
			Context.AddAnalyzer(*Analyzer);
		}
		Context.SetMessageDelegate(UE::Trace::FMessageDelegate::CreateRaw(this, &FAnalysisSession::OnAnalysisMessage));
		Processor = Context.Process(*DataStream);
		*/
	}

	void FAnalysisSession::Stop(bool bAndWait) const
	{
		/*
		DataStream->Close();
		Processor.Stop();
		if (bAndWait)
		{
			Wait();
		}
		*/
	}

	void FAnalysisSession::Wait() const
	{
		//Processor.Wait();
	}

	void FAnalysisSession::EnumerateMetadata(TFunctionRef<void(const FTraceSessionMetadata& Metadata)> Callback) const
	{
		Lock.ReadAccessCheck();
		for (const auto& KV : Metadata)
		{
			Callback(KV.Value);
		}
	}

	void FAnalysisSession::AddMetadata(FName InName, int64 InValue)
	{
		Lock.WriteAccessCheck();
		FTraceSessionMetadata& Value = Metadata.Add(InName);
		Value.Name = InName;
		Value.Type = FTraceSessionMetadata::EType::Int64;
		Value.Int64Value = InValue;
	}

	void FAnalysisSession::AddMetadata(FName InName, double InValue)
	{
		Lock.WriteAccessCheck();
		FTraceSessionMetadata& Value = Metadata.Add(InName);
		Value.Name = InName;
		Value.Type = FTraceSessionMetadata::EType::Double;
		Value.DoubleValue = InValue;
	}

	void FAnalysisSession::AddMetadata(FName InName, FString InValue)
	{
		Lock.WriteAccessCheck();
		FTraceSessionMetadata& Value = Metadata.Add(InName);
		Value.Name = InName;
		Value.Type = FTraceSessionMetadata::EType::String;
		Value.StringValue = InValue;
	}

	uint32 FAnalysisSession::GetNumPendingMessages() const
	{
		return PendingMessagesCount.load();
	}

	TArray<FAnalysisMessage> FAnalysisSession::DrainPendingMessages()
	{
		Lock.WriteAccessCheck();
		PendingMessagesCount.store(0);
		return MoveTemp(PendingMessages);
	}

	void FAnalysisSession::AddAnalyzer(UE::Trace::IAnalyzer* Analyzer)
	{
		check(Analyzer != nullptr);
		Analyzers.Add(Analyzer);
	}

	void FAnalysisSession::AddAnalyzer(TSharedRef<UE::Trace::IAnalyzer> Analyzer)
	{
		//Analyzers.Add(new FAnalyzerWrapper(Analyzer));
	}

	void FAnalysisSession::AddProvider(const FName& InName, TSharedPtr<IProvider> Provider, TSharedPtr<IEditableProvider> EditableProvider)
	{
		Providers.Add(InName, MakeTuple(Provider, EditableProvider));
	}

	const IProvider* FAnalysisSession::ReadProviderPrivate(const FName& InName) const
	{
		const auto* FindIt = Providers.Find(InName);
		if (FindIt)
		{
			return FindIt->Key.Get();
		}
		else
		{
			return nullptr;
		}
	}

	IEditableProvider* FAnalysisSession::EditProviderPrivate(const FName& InName)
	{
		const auto* FindIt = Providers.Find(InName);
		if (FindIt)
		{
			return FindIt->Value.Get();
		}
		else
		{
			return nullptr;
		}
	}

	void FAnalysisSession::OnAnalysisMessage(UE::Trace::EAnalysisMessageSeverity InSeverity, FStringView InMessage)
	{
		EMessageSeverity::Type Severity = EMessageSeverity::Type::Info;
		switch (InSeverity)
		{
		case UE::Trace::EAnalysisMessageSeverity::Error: Severity = EMessageSeverity::Type::Error; break;
		case UE::Trace::EAnalysisMessageSeverity::Warning: Severity = EMessageSeverity::Type::Warning; break;
		case UE::Trace::EAnalysisMessageSeverity::Info: Severity = EMessageSeverity::Type::Info; break;
		}

		Lock.BeginEdit();
		PendingMessages.Push(FAnalysisMessage{ Severity, FString(InMessage) });
		PendingMessagesCount.fetch_add(1);
		Lock.EndEdit();
	}
}
