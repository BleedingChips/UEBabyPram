module;
#include "Trace/Analyzer.h"
#include "Analysis/Engine.h"
#include "Analysis/StreamReader.h"
#include "TraceServices/AnalyzerFactories.h"
#include "Templates/UniquePtr.h"
#include "Trace\DataStream.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"

module UEBabyPramInsightParser;
import std;
import UEBabyPramInsightParserAnalysisInterface;
import UEBabyPramInsightParserCPUAnalysis;
import UEBabyPramInsightParserAnalysisSession;

namespace UEBabyPram::InsightParser
{
	class FSummarizeCpuScopeAnalyzer
	{
	public:
		enum class EScopeEventType : uint32
		{
			Enter,
			Exit
		};

		struct FScopeEvent
		{
			EScopeEventType ScopeEventType;
			uint32 ScopeId;
			uint32 ThreadId;
			double Timestamp; // As Seconds
		};

		struct FScope
		{
			uint32 ScopeId;
			uint32 ThreadId;
			double EnterTimestamp; // As Seconds
			double ExitTimestamp;  // As Seconds
		};

	public:
		virtual ~FSummarizeCpuScopeAnalyzer() = default;

		/** Invoked when a CPU scope is discovered. This function is always invoked first when a CPU scope is encountered for the first time.*/
		virtual void OnCpuScopeDiscovered(uint32 ScopeId) {}

		/** Invoked when CPU scope specification is encountered in the trace stream. */
		virtual void OnCpuScopeName(uint32 ScopeId, const FStringView& ScopeName) {};

		/** Invoked when a scope is entered. The scope name might not be known yet. */
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, const FString* ScopeName) {};

		/** Invoked when a scope is exited. The scope name might not be known yet. */
		virtual void OnCpuScopeExit(const FScope& Scope, const FString* ScopeName) {};

		/** Invoked when a root event on the specified thread along with all child events down to the leaves are known. */
		virtual void OnCpuScopeTree(uint32 ThreadId, const TArray64<FSummarizeCpuScopeAnalyzer::FScopeEvent>& ScopeEvents, const TFunction<const FString* (uint32)>& ScopeLookupNameFn) {};

		/** Invoked when the trace stream has been fully consumed/processed. */
		virtual void OnCpuScopeAnalysisEnd() {};

		static constexpr uint32 CoroutineSpecId = (1u << 31u) - 1u;
		static constexpr uint32 CoroutineUnknownSpecId = (1u << 31u) - 2u;
	};


	class FSummarizeCpuProfilerProvider
		: public IEditableThreadProvider
		, public IEditableTimingProfilerProvider
	{
	public:
		FSummarizeCpuProfilerProvider();

		/** Register an analyzer with this processor. The processor decodes the trace stream and invokes the registered analyzers when a CPU scope event occurs.*/
		void AddCpuScopeAnalyzer(TSharedPtr<FSummarizeCpuScopeAnalyzer> Analyzer);

		void AnalysisComplete();

	private:
		struct FScopeEnter
		{
			uint32 ScopeId;
			double Timestamp; // As Seconds
		};

		// Contains scope events for a root scope and its children along with extra info to analyze that tree at once.
		struct FScopeTreeInfo
		{
			// Records the current root scope and its children to run analysis that needs to know the parent/child relationship.
			TArray64<FSummarizeCpuScopeAnalyzer::FScopeEvent> ScopeEvents;

			// Indicates if one of the scope in the current hierarchy is nameless. (Its names specs hasn't been received yet).
			bool bHasNamelessScopes = false;

			void Reset()
			{
				ScopeEvents.Reset();
				bHasNamelessScopes = false;
			}
		};

		// For each thread we track what the stack of scopes are, for matching end-to-start
		struct FThread
			: public TraceServices::IEditableTimeline<TraceServices::FTimingProfilerEvent>
		{
			FThread(uint32 InThreadId, FSummarizeCpuProfilerProvider* InProvider)
				: ThreadId(InThreadId)
				, Provider(InProvider)
			{

			}

			virtual void AppendBeginEvent(double StartTime, const TraceServices::FTimingProfilerEvent& Event) override;
			virtual void AppendEndEvent(double EndTime) override;

			// The ThreadId of this thread
			uint32 ThreadId;

			// The provider to forward calls to
			FSummarizeCpuProfilerProvider* Provider;

			// The current stack state
			TArray<FScopeEnter> ScopeStack;

			// The events recorded for the current root scope and its children to run analysis that needs to know the parent/child relationship, for example to compute time including childreen and time
			// excluding childreen.
			FScopeTreeInfo ScopeTreeInfo;

			// Scope trees for which as least one scope name was unknown. Some analysis need scope names, but scope names/event can be emitted out of order by the engine depending on thread scheduling.
			// Some scope tree cannot be analyzed right away and need to be delayed until all scope names are discovered.
			TArray<FScopeTreeInfo> DelayedScopeTreeInfo;
		};

	private:
		// TraceServices::IEditableThreadProvider
		virtual void AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority) override;

		// TraceServices::IEditableTimingProfilerProvider
		virtual uint32 AddTimer(TraceServices::ETimingProfilerTimerType Type) override;
		virtual uint32 AddCpuTimer(FStringView Name, const TCHAR* File, uint32 Line) override;
		virtual void SetTimerName(uint32 TimerId, FStringView Name) override;
		virtual uint32 AddMetadata(uint32 MasterTimerId, TArray<uint8>&& Metadata) override;
		virtual TArrayView<uint8> GetEditableMetadata(uint32 TimerId) override;
		virtual TraceServices::IEditableTimeline<TraceServices::FTimingProfilerEvent>& GetCpuThreadEditableTimeline(uint32 ThreadId) override;

		// Callbacks from FThread to forward calls to Analyzers
		virtual void AppendBeginEvent(const FSummarizeCpuScopeAnalyzer::FScopeEvent& ScopeEvent);
		virtual void AppendEndEvent(const FSummarizeCpuScopeAnalyzer::FScope& ScopeEvent, const FString* ScopeName);

		// Processing the ScopeTree once it's complete
		void OnCpuScopeTree(uint32 ThreadId, const FScopeTreeInfo& ScopeTreeInfo);

		// Resolve a ScopeId to string
		const FString* LookupScopeName(uint32 ScopeId);

		uint32 AddCpuTimerInternal(FStringView Name);

	private:
		// The state at any moment of the threads
		TMap<uint32, TUniquePtr<FThread>> Threads;

		// The scope names, the array index correspond to the scope Id. If the optional is not set, the scope hasn't been encountered yet.
		TArray<TOptional<FString>> ScopeNames;

		// List of analyzers to invoke when a scope event is decoded.
		TArray<TSharedPtr<FSummarizeCpuScopeAnalyzer>> ScopeAnalyzers;

		// Scope name lookup function, cached for efficiency.
		TFunction<const FString* (uint32 ScopeId)> LookupScopeNameFn;

		struct FMetadata
		{
			TArray<uint8> Payload;
			uint32 TimerId;
		};

		TArray<FMetadata> Metadatas;
	};


	FSummarizeCpuProfilerProvider::FSummarizeCpuProfilerProvider()
		: LookupScopeNameFn([this](uint32 ScopeId) { return LookupScopeName(ScopeId); })
	{
	}

	void FSummarizeCpuProfilerProvider::AddCpuScopeAnalyzer(TSharedPtr<FSummarizeCpuScopeAnalyzer> Analyzer)
	{
		ScopeAnalyzers.Add(MoveTemp(Analyzer));
	}

	void FSummarizeCpuProfilerProvider::AnalysisComplete()
	{
		// Analyze scope trees that contained 'nameless' context when they were captured. Unless the trace was truncated,
		// all scope names should be known now.
		for (const auto& Thread : Threads)
		{
			for (FScopeTreeInfo& DelayedScopeTree : Thread.Value->DelayedScopeTreeInfo)
			{
				// Run summary analysis for this delayed hierarchy.
				OnCpuScopeTree(Thread.Key, DelayedScopeTree);
			}
		}

		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeAnalysisEnd();
		}
	}

	void FSummarizeCpuProfilerProvider::AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority)
	{
		if (Name != nullptr)
		{
			std::wstring_view ThreadName = Name;

			if (ThreadName.contains(L"Core"))
			{
				volatile int i = 0;
			}
		}
		

		TUniquePtr<FThread>* Found = Threads.Find(Id);
		if (!Found)
		{
			Threads.Add(Id, MakeUnique<FThread>(Id, this));
		}
	}

	uint32 FSummarizeCpuProfilerProvider::AddTimer(TraceServices::ETimingProfilerTimerType Type)
	{
		if (Type == TraceServices::ETimingProfilerTimerType::CPU)
		{
			return AddCpuTimerInternal(FStringView());
		}
		return 0;
	}

	uint32 FSummarizeCpuProfilerProvider::AddCpuTimer(FStringView Name, const TCHAR* File, uint32 Line)
	{
		return AddCpuTimerInternal(Name);
	}

	uint32 FSummarizeCpuProfilerProvider::AddCpuTimerInternal(FStringView Name)
	{
		TOptional<FString> ScopeName;

		if (!Name.IsEmpty())
		{
			ScopeName.Emplace(FString(Name));
		}

		uint32 TimerId = ScopeNames.Add(ScopeName);

		// Notify the analyzers.
		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeDiscovered(TimerId);

			if (!Name.IsEmpty())
			{
				Analyzer->OnCpuScopeName(TimerId, Name);
			}
		}

		return TimerId;
	}

	void FSummarizeCpuProfilerProvider::SetTimerName(uint32 TimerId, FStringView Name)
	{
		check(TimerId < uint32(ScopeNames.Num()));
		check(!Name.IsEmpty());

		ScopeNames[TimerId].Emplace(FString(Name));

		// Notify the registered scope analyzers.
		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeName(TimerId, Name);
		}
	}

	uint32 FSummarizeCpuProfilerProvider::AddMetadata(uint32 MasterTimerId, TArray<uint8>&& Metadata)
	{
		uint32 MetadataId = Metadatas.Num();
		Metadatas.Add({ MoveTemp(Metadata), MasterTimerId });
		return ~MetadataId;
	}

	TArrayView<uint8> FSummarizeCpuProfilerProvider::GetEditableMetadata(uint32 TimerId)
	{
		if (int32(TimerId) >= 0)
		{
			return TArrayView<uint8>();
		}

		TimerId = ~TimerId;
		if (TimerId >= uint32(Metadatas.Num()))
		{
			return TArrayView<uint8>();
		}

		FMetadata& Metadata = Metadatas[TimerId];
		return Metadata.Payload;
	}

	TraceServices::IEditableTimeline<TraceServices::FTimingProfilerEvent>& FSummarizeCpuProfilerProvider::GetCpuThreadEditableTimeline(uint32 ThreadId)
	{
		TUniquePtr<FThread>* Found = Threads.Find(ThreadId);
		if (Found)
		{
			return *(Found->Get());
		}

		return *Threads.Add(ThreadId, MakeUnique<FThread>(ThreadId, this));
	}

	void FSummarizeCpuProfilerProvider::FThread::AppendBeginEvent(double StartTime, const TraceServices::FTimingProfilerEvent& Event)
	{
		FScopeEnter ScopeEnter{ Event.TimerIndex, StartTime };
		ScopeStack.Add(ScopeEnter);

		FSummarizeCpuScopeAnalyzer::FScopeEvent ScopeEvent{ FSummarizeCpuScopeAnalyzer::EScopeEventType::Enter, Event.TimerIndex, ThreadId, StartTime };
		ScopeTreeInfo.ScopeEvents.Add(ScopeEvent);

		Provider->AppendBeginEvent(ScopeEvent);
	}

	void FSummarizeCpuProfilerProvider::FThread::AppendEndEvent(double EndTime)
	{
		if (ScopeStack.IsEmpty())
		{
			return;
		}

		FScopeEnter ScopeEnter = ScopeStack.Pop();

		FSummarizeCpuScopeAnalyzer::FScopeEvent ScopeEvent{ FSummarizeCpuScopeAnalyzer::EScopeEventType::Exit, ScopeEnter.ScopeId, ThreadId, EndTime };
		ScopeTreeInfo.ScopeEvents.Add(ScopeEvent);

		// Check if at this point if the scope has a name
		const FString* ScopeName = Provider->LookupScopeName(ScopeEnter.ScopeId);
		ScopeTreeInfo.bHasNamelessScopes |= ScopeName == nullptr || ScopeName->IsEmpty();

		FSummarizeCpuScopeAnalyzer::FScope Scope{ ScopeEnter.ScopeId, ThreadId, ScopeEnter.Timestamp, EndTime };
		Provider->AppendEndEvent(Scope, ScopeName);

		// The root scope on this thread just popped out.
		if (ScopeStack.IsEmpty())
		{
			if (ScopeTreeInfo.bHasNamelessScopes)
			{
				// Delay the analysis until all the scope names are known.
				DelayedScopeTreeInfo.Add(MoveTemp(ScopeTreeInfo));
			}
			else
			{
				// Run analysis for this scope tree.
				Provider->OnCpuScopeTree(ThreadId, ScopeTreeInfo);
			}

			ScopeTreeInfo.Reset();
		}
	}

	void FSummarizeCpuProfilerProvider::AppendBeginEvent(const FSummarizeCpuScopeAnalyzer::FScopeEvent& ScopeEvent)
	{
		//FPlatformEventTraceAnalyzer
		auto Name = LookupScopeName(ScopeEvent.ScopeId);
		std::wstring NameSW = **Name;
		//sadas 
		//sadsad
		static std::set<std::wstring> Names;
		if (Name != nullptr && ( Name->Contains(TEXT("GameThread")) || Name->Contains(TEXT("Context")) && Name->Contains(TEXT("Switch")) || Name->StartsWith(TEXT("Core"))))
		{
			if (Names.insert(NameSW).second)
			{
				volatile int o = 0;
			}
			
		}
		// Notify the registered scope analyzers.
		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeEnter(ScopeEvent, LookupScopeName(ScopeEvent.ScopeId));
		}
	}

	void FSummarizeCpuProfilerProvider::AppendEndEvent(const FSummarizeCpuScopeAnalyzer::FScope& Scope, const FString* ScopeName)
	{
		std::wstring_view name = **ScopeName;
		// Notify the registered scope analyzers.
		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeExit(Scope, ScopeName);
		}
	}

	void FSummarizeCpuProfilerProvider::OnCpuScopeTree(uint32 ThreadId, const FScopeTreeInfo& ScopeTreeInfo)
	{
		// Notify the registered scope analyzers.
		for (TSharedPtr<FSummarizeCpuScopeAnalyzer>& Analyzer : ScopeAnalyzers)
		{
			Analyzer->OnCpuScopeTree(ThreadId, ScopeTreeInfo.ScopeEvents, LookupScopeNameFn);
		}
	}

	const FString* FSummarizeCpuProfilerProvider::LookupScopeName(uint32 ScopeId)
	{
		if (int32(ScopeId) < 0)
		{
			ScopeId = Metadatas[~ScopeId].TimerId;
		}

		if (ScopeId < static_cast<uint32>(ScopeNames.Num()) && ScopeNames[ScopeId])
		{
			return &ScopeNames[ScopeId].GetValue();
		}

		return nullptr;
	}

	void Test(DataResourceInterface& resource)
	{
		InsightReciver Interface;

		FSummarizeCpuProfilerProvider provider;

		FAnalysisSession seesion{1, L"asdasd"};

		FCpuProfilerAnalyzer analyzer{ seesion, provider, provider };
		//TSharedPtr<TraceServices::IAnalysisSession> Session = TraceServices::CreateAnalysisSession(0, nullptr, {});

		//FSummarizeCpuProfilerProvider CpuProfilerProvider;
		//TSharedPtr<UE::Trace::IAnalyzer> CpuProfilerAnalyzer = TraceServices::CreateCpuProfilerAnalyzer(*Session, CpuProfilerProvider, CpuProfilerProvider);

		TArray<UE::Trace::IAnalyzer*> List = { &analyzer };
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
		
	}
}

//export import UEBabyPramInsightInterface;