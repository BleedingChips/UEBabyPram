#pragma once

#include "Trace/DataStream.h"

namespace UEBabyPram::InsightParser
{

	struct DataResourceInterface : public UE::Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) = 0;
	};

	struct InsightReciver
	{
		virtual bool RequireEventName(wchar_t const* event_name, std::size_t event_name_len) { return true; }
		virtual bool RequireThread(wchar_t const* thread_name, std::size_t thread_name_len) { return true; }
	};

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
		virtual void OnCpuScopeName(uint32 ScopeId, wchar_t const* ScopeName, std::size_t ScopeNameLen) {};

		/** Invoked when a scope is entered. The scope name might not be known yet. */
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, wchar_t const* ScopeName, std::size_t ScopeNameLen) {};

		/** Invoked when a scope is exited. The scope name might not be known yet. */
		virtual void OnCpuScopeExit(const FScope& Scope, wchar_t const* ScopeName, std::size_t ScopeNameLen) {};

		using ScopeNameFunction = bool(void* Object, uint32, wchar_t const* & ScopeName, std::size_t& ScopeNameLen);

		/** Invoked when a root event on the specified thread along with all child events down to the leaves are known. */
		virtual void OnCpuScopeTree(uint32 ThreadId, FSummarizeCpuScopeAnalyzer::FScopeEvent const* ScopeEvents, std::size_t ScopeEventsLen, ScopeNameFunction func, void* Object) {};

		/** Invoked when the trace stream has been fully consumed/processed. */
		virtual void OnCpuScopeAnalysisEnd() {};

		static constexpr uint32 CoroutineSpecId = (1u << 31u) - 1u;
		static constexpr uint32 CoroutineUnknownSpecId = (1u << 31u) - 2u;
	};

	void TestImp(DataResourceInterface& Resource, FSummarizeCpuScopeAnalyzer& resource);
}


//export import UEBabyPramInsightInterface;