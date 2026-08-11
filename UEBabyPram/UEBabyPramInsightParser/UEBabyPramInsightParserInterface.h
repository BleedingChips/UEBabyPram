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

		struct ContextSwitchEvent
		{
			uint32 ThreadId;
			uint32 CoreNumber;
			double StartTime;
			double EndTime;
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

		virtual void OnContextSwitchEvent(const ContextSwitchEvent& Event) {};

		static constexpr uint32 CoroutineSpecId = (1u << 31u) - 1u;
		static constexpr uint32 CoroutineUnknownSpecId = (1u << 31u) - 2u;
	};

	struct ThreadTimeLineInterface
	{
		virtual void AppendBeginEvent(double StartTime, uint32 TimerId) = 0;
		virtual void AppendEndEvent(double EndTime) = 0;
		virtual ~ThreadTimeLineInterface() = default;
	};

	struct BaseParser
	{

		static uint32 GetInvalidMetadataSpecId() { return (uint32)-1; }

		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) { return 0; }
		virtual bool IsThreadRequired(wchar_t const* thread_name, std::size_t thread_name_len) { return true; }
		virtual bool IsContextSwitchRequired() const { return false; }
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 core_name, uint32 start_time, uint32 end_time) {}
		virtual void OnThreadDiscoverd(uint32 thread_id, wchar_t const* thread_name, std::size_t thread_name_len) {}
		virtual uint32 OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) { return 0; }
		virtual void OverrideCPUEventLocation(uint32 event_id, wchar_t const* file_name, std::size_t file_name_len) {}
		virtual void OverrideCPUEventName(uint32 event_id, wchar_t const* event_name, std::size_t event_name_len) {}
		virtual ThreadTimeLineInterface* GetThreadTimeLine(uint32 thread_id) = 0;
		virtual void AddThread(uint32 thread_id, char const* thread_name) = 0;
	};

	void ExecuteParser(DataResourceInterface& resource, BaseParser& parser);
}


//export import UEBabyPramInsightInterface;