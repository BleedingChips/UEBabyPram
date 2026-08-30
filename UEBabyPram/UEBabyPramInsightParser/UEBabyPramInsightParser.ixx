module;

#include "UEBabyPramInsightParserInterface.h"

export module UEBabyPramInsightParser;
import std;
import Potato;

export namespace UEBabyPram::InsightParser
{
	using UEBabyPram::InsightParser::DataResourceInterface;

	struct ScopeAnalyzer : public FSummarizeCpuScopeAnalyzer
	{
		using FSummarizeCpuScopeAnalyzer::OnCpuScopeDiscovered;
		using FSummarizeCpuScopeAnalyzer::OnCpuScopeAnalysisEnd;

		using FSummarizeCpuScopeAnalyzer::EScopeEventType;
		using FSummarizeCpuScopeAnalyzer::FScope;
		using FSummarizeCpuScopeAnalyzer::FScopeEvent;
		using FSummarizeCpuScopeAnalyzer::ContextSwitchEvent;

		virtual void OnCpuScopeName(uint32 ScopeId, std::wstring_view ScopeName) {}
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, std::wstring_view ScopeName) {};
		virtual void OnCpuScopeExit(const FScope& Scope, std::wstring_view ScopeName) {};
		virtual void OnCpuScopeTree(uint32 ThreadId, std::span<FSummarizeCpuScopeAnalyzer::FScopeEvent const> ScopeEvents, Potato::TMP::FunctionRef<std::wstring_view(std::uint32_t)> LookupScopeName) {};

	protected:
		
		static std::wstring_view CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen)
		{
			if (ScopeName != nullptr && ScopeNameLen > 0)
			{
				if (ScopeName[ScopeNameLen - 1] == 0)
				{
					return std::wstring_view{ ScopeName, ScopeNameLen - 1};
				}
				return std::wstring_view{ ScopeName, ScopeNameLen };
			}
			return {};
		}
		/** Invoked when CPU scope specification is encountered in the trace stream. */
		virtual void OnCpuScopeName(uint32 ScopeId, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeName(ScopeId, CoverStringView(ScopeName, ScopeNameLen));
		};

		/** Invoked when a scope is entered. The scope name might not be known yet. */
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeEnter(ScopeEnter, CoverStringView(ScopeName, ScopeNameLen));
		};

		/** Invoked when a scope is exited. The scope name might not be known yet. */
		virtual void OnCpuScopeExit(const FScope& Scope, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeExit(Scope, CoverStringView(ScopeName, ScopeNameLen));
		};

		using ScopeNameFunction = bool(*)(void* Object, uint32, wchar_t const*& ScopeName, std::size_t& ScopeNameLen);

		/** Invoked when a root event on the specified thread along with all child events down to the leaves are known. */
		virtual void OnCpuScopeTree(uint32 ThreadId, FSummarizeCpuScopeAnalyzer::FScopeEvent const* ScopeEvents, std::size_t ScopeEventsLen, ScopeNameFunction func, void* Object) {
			auto fun_object = [=](std::uint32_t ScopeID) -> std::wstring_view {
				wchar_t const* OutString = nullptr;
				std::size_t OutStringLen = 0;
				if (func(Object, ScopeID, OutString, OutStringLen))
				{
					return CoverStringView(OutString, OutStringLen);
				}
				return {};
			};
			OnCpuScopeTree(ThreadId, std::span(ScopeEvents, ScopeEventsLen), fun_object);
		};
	};

	struct DcomentWrapper : public UEBabyPram::InsightParser::DataResourceInterface
	{
		DcomentWrapper(Potato::Document::DocumentReader& reader) : reader(reader) {}
		virtual std::int32_t Read(void* out_data, std::uint32_t byte_size) override
		{
			return static_cast<std::int32_t>(reader.StreamRead(static_cast<std::byte*>(out_data), byte_size));
		}
	protected:
		Potato::Document::DocumentReader& reader;
	};

	struct EventID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(EventID const&) const = default;
		bool operator==(EventID const&) const = default;
	};

	struct ThreadID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(ThreadID const&) const = default;
		bool operator==(ThreadID const&) const = default;
	};

	struct ThreadSystemID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(ThreadSystemID const&) const = default;
		bool operator==(ThreadSystemID const&) const = default;
	};

	struct ThreadCPUEvent
	{
		EventID event_id;
		double time_as_second;
		std::size_t depth;
	};

	struct ThreadCPUEventView
	{
		ThreadID thread_id;
		ThreadSystemID system_thread_id;
		std::span<ThreadCPUEvent const> view;
		std::optional<std::size_t> frame_count;

		struct EventIterator
		{
			EventID event_id;
			Potato::Misc::IndexSpan<> exist_range;
			Potato::Misc::IndexSpan<double> exist_time_in_second;
			double self_time_in_second = 0.0;
			std::size_t depth = std::numeric_limits<std::size_t>::max();
			operator bool() const { return event_id.id != std::numeric_limits<std::size_t>::max() && exist_range.Size() != 0; }
		};

		EventIterator FindNextEvent(std::span<EventID const> event_id_span, EventIterator current = {}, bool need_depper = true) const;
		EventID GetTopEvent() const {
			if (view.size() > 0)
			{
				return view[0].event_id;
			}
			return {};
		}
		
		std::optional<Potato::Misc::IndexSpan<double>> GetTimeRange() const {
			if (view.size() > 0)
			{
				return Potato::Misc::IndexSpan<double>{ view.begin()->time_as_second, view.rbegin()->time_as_second };
			}
			return std::nullopt;
		}
		
		template<typename Func>
			requires(std::is_invocable_r_v<bool, Func, EventIterator>)
		std::size_t ForeachEvent(std::span<EventID const> event_id_span, Func&& func, bool need_depper = true) const
		{
			std::size_t total_count = 0;
			EventIterator current;
			do {
				current = FindNextEvent(event_id_span, current, need_depper);
				if (current)
				{
					auto need_continue = func(current);
					total_count += 1;
					if (!need_continue)
						return total_count;
				}
			} while (current);
			return total_count;
		}
	};

	struct ParserInterface;

	struct ParserThreadTimeLine : public ThreadTimeLineInterface
	{
		ParserThreadTimeLine(ThreadID thread_id, ThreadSystemID thread_system_id, ParserInterface& reference) 
			: thread_id(thread_id), thread_system_id(thread_system_id), reference(reference) {}
		virtual void AppendBeginEvent(double start_time, std::uint32_t event_id) override;
		virtual void AppendEndEvent(double end_time) override;
		ThreadID thread_id;
		ThreadSystemID thread_system_id;
		std::size_t depth = 0;
		std::vector<ThreadCPUEvent> stacks;
		ParserInterface& reference;
		~ParserThreadTimeLine();
	};

	struct ParserInterface : private BaseParser
	{
		virtual bool IsThreadRequired(std::string_view thread_name) const { return true; }
		virtual bool IsContextSwitchRequired() const override { return true; }
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<double> duration) {}
		virtual void OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) {}
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) {}
		virtual void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) {}
		virtual	void AllAnalyzeDone() override;
		virtual bool IsThreadRequired(ThreadID thread_id) const { return true; }
		static std::wstring_view CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen);
		
		std::optional<std::wstring_view> GetCPUEventName(EventID event_id) const;
		std::optional<std::string_view> GetThreadName(ThreadID thread_id) const;
		std::optional<std::string_view> GetThreadName(ThreadSystemID thread_id) const;

		struct CPUEventInfo
		{
			EventID id;
			std::wstring_view event_name;
			std::wstring_view file_name;
			std::size_t file_line;
		};

		struct ThreadInfo
		{
			ThreadID thread_id;
			ThreadSystemID thread_system_id;
			std::string_view thread_name;
		};

		std::optional<CPUEventInfo> GetCPUEventInfo(EventID event_id) const;
		std::optional<ThreadInfo> GetThreadInfo(ThreadID thread_id) const;
		std::optional<ThreadInfo> GetThreadInfo(ThreadSystemID thread_system_id) const;

	private:
		
		virtual uint32 OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) override;

		virtual ParserThreadTimeLine* GetThreadTimeLine(uint32 thread_id) override;

		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) override { return 0; }

		virtual void SetMetadataSpec(uint32 event_id, uint32 metadata_space_id) override {}
		virtual bool IsThreadRequired(uint32 thread_id) const { return IsThreadRequired(ThreadID(thread_id)); }
		virtual uint32 AddMetaData(uint32 event_id, MetaDataFormat format, uint8 const* data, std::size_t meta_data_len, uint32 thread_id) override;
		virtual void SetMetadata(uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId) override;
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 core_name, double start_time, double end_time) override {
			return ContextSwitchEvent(ThreadSystemID{ thread_id }, core_name, Potato::Misc::IndexSpan<double>{start_time, end_time});
		}

		virtual void OnThreadDiscoverd(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len) override;
		struct TimeLineTuple
		{
			ThreadID thread_id;
			ThreadSystemID thread_system_id;
			std::string thread_name;
			std::unique_ptr<ParserThreadTimeLine> time_line;
		};

		std::vector<TimeLineTuple> thread_timelines;

		struct CPUEvent
		{
			EventID id;
			std::wstring event_name;
			std::wstring file_name;
			std::size_t file_line;
		};
		
		std::vector<CPUEvent> time_infos;
		std::vector<uint32> frame_event_id;

		//virtual void AddThread(uint32 thread_id, char const* thread_name);
		friend void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser);
	};

	void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser)
	{
		DcomentWrapper ResourceWrapper(Resource);
		ExecuteParser(ResourceWrapper, Parser);
	}
}


//export import UEBabyPramInsightInterface;