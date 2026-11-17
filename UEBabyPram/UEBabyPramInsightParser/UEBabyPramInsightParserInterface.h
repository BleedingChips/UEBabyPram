#pragma once

#include "Trace/DataStream.h"

namespace UEBabyPram::InsightParser
{

	struct DataResourceInterface : public UE::Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) = 0;
	};

	struct ThreadTimeLineInterface
	{
		virtual void AppendBeginEvent(double StartTime, uint32 TimerId) = 0;
		virtual void AppendEndEvent(double EndTime) = 0;
		virtual ~ThreadTimeLineInterface() = default;
	};

	enum class MetaDataFormat
	{
		CborData,
		EventData
	};

	enum class ParserRequireFlag : std::size_t
	{
		CPU_EVENT = 1 << 1,
		EVENT_SPEC = 1 << 2,
		CONTEXT_SWITCH = 1 << 3,
		META_DATA = 1 << 4,
		// The specs of the memory tracing of the LLM channel: the memory tags (TagsSpec), the trackers (TrackerSpec) and
		// the tag sets (TagSetSpec). A parser which only resolves the id of a memory tag to a name requires this one, and
		// nothing else, so the snapshots of the tags are not read for it.
		MEMORY_TAG = 1 << 5,
		// The snapshots of the memory tags (TagValue): the time and the value which every tracker sampled for every tag
		// which it tracks. A parser which reports a value requires this one as well as MEMORY_TAG, because an id is only
		// resolvable to a name through the specs.
		MEMORY_VALUE = 1 << 6
	};

	struct BaseParser
	{
		static uint32 GetInvalidMetadataSpecId() { return (uint32)-1; }
		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) { return 0; }
		virtual bool IsThreadResuiredSystemId(uint32 thread_system_id) const { return true; }
		virtual bool IsThreadRequired(uint32 thread_id) const { return true; }
		virtual bool IsParserRequired(ParserRequireFlag mask) const { return true; }
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 active_core, double active_start_time, double active_end_time) {}
		virtual void OnThreadDiscovered(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len) {}
		virtual void OnCPUEventDiscovered(uint32 space_id, wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) { }
		virtual void OnCPUScopeEventDiscovered(uint32 space_id, char const* event_name, std::size_t event_name_len) { }
		virtual void OverrideCPUEventLocation(uint32 event_id, wchar_t const* file_name, std::size_t file_name_len) {}
		virtual void OverrideCPUEventName(uint32 event_id, wchar_t const* event_name, std::size_t event_name_len) {}
		virtual void OnMemoryTagsSpecId(int64 tag_id, int64 parent_id, int32 tag_set_id, char const* name, std::size_t name_count) {};
		virtual void OnMemoryTrackerSpec(int32 tracker_id, char const* name, std::size_t name_count) {};
		virtual void OnMemoryTagSetSpec(int32 tag_set_id, char const* name, std::size_t name_count) {};
		virtual void OnMemoryTagSnap(int32 tracker_id, double time, int64 const* tags, std::size_t tag_count, int64 const* sampled_value, std::size_t sampled_value_count) {}
		virtual void OnMemoryTagSnap(int32 tracker_id, double time, uint32 const* tags, std::size_t tag_count, int64 const* sampled_value, std::size_t sampled_value_count) {}
		virtual ThreadTimeLineInterface* GetThreadTimeLine(uint32 thread_id) = 0;
		//virtual void AddThread(uint32 thread_id, uint32 thread_system_id, char const* thread_name) = 0;
		virtual uint32 AddMetaData(uint32 SpecId, uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 thread_id) { return 0; }
		//virtual void SetMetadata(uint32 SpecId, uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId) {}
		static bool TryReadFromMetaData(MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, char const* field_name, wchar_t const*& out_string, std::size_t& string_len);
		virtual void AllAnalyzeDone() {};
	};

	void ExecuteParser(DataResourceInterface& resource, BaseParser& parser);
}


//export import UEBabyPramInsightInterface;