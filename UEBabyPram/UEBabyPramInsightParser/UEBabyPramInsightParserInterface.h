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

	struct BaseParser
	{

		static uint32 GetInvalidMetadataSpecId() { return (uint32)-1; }
		virtual void SetMetadataSpec(uint32 TimerId, uint32 MetadataSpecId) {}
		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) { return 0; }
		virtual bool IsThreadRequired(uint32 thread_id) const { return true; }
		virtual bool IsContextSwitchRequired() const { return false; }
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 core_number, double start_time, double end_time) {}
		virtual void OnThreadDiscoverd(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len) {}
		virtual uint32 OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) { return 0; }
		virtual void OverrideCPUEventLocation(uint32 event_id, wchar_t const* file_name, std::size_t file_name_len) {}
		virtual void OverrideCPUEventName(uint32 event_id, wchar_t const* event_name, std::size_t event_name_len) {}
		virtual ThreadTimeLineInterface* GetThreadTimeLine(uint32 thread_id) = 0;
		//virtual void AddThread(uint32 thread_id, uint32 thread_system_id, char const* thread_name) = 0;
		virtual uint32 AddMetaData(uint32 TimerId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 ThreadId) { return 0; }
		virtual void SetMetadata(uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId) {}
		static bool TryReadFromMetaData(MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, char const* field_name, wchar_t const*& out_string, std::size_t& string_len);
		virtual void AllAnalyzeDone() {};
	};

	void ExecuteParser(DataResourceInterface& resource, BaseParser& parser);
}


//export import UEBabyPramInsightInterface;