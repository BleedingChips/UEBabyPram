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

	struct ThreadCPUEvent
	{
		std::optional<std::size_t> event_id;
		double time_as_second;
		std::size_t depth;
	};

	struct ThreadCPUEventView
	{
		ThreadCPUEventView(std::size_t thread_id, std::span<ThreadCPUEvent const> view) : view(view), thread_id(thread_id) {}
	protected:
		std::span<ThreadCPUEvent const> view;
		std::size_t thread_id;
	};

	struct ParserInterface;

	struct ParserThreadTimeLine : public ThreadTimeLineInterface
	{
		ParserThreadTimeLine(std::size_t thread_id, ParserInterface& reference) : thread_id(thread_id), reference(reference) {}
		virtual void AppendBeginEvent(double start_time, std::uint32_t event_id) override;
		virtual void AppendEndEvent(double end_time) override;
		std::size_t thread_id;
		std::size_t depth = 0;
		std::vector<ThreadCPUEvent> stacks;
		ParserInterface& reference;
		~ParserThreadTimeLine();
	};

	struct ParserInterface : private BaseParser
	{
		virtual bool IsThreadRequired(std::string_view thread_name) const { return true; }
		virtual bool IsContextSwitchRequired() const override { return true; }
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 core_name, uint32 start_time, uint32 end_time) override {}
		virtual void OnThreadDiscoverd(uint32 thread_id, std::string_view thread_name) {}
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) {}
		virtual void OnCPUEventDiscoverd(std::size_t id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) {}
		static std::wstring_view CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen);
		std::optional<std::wstring_view> GetCPUEventName(std::size_t event_id) const;
		std::optional<std::string_view> GetThreadName(std::size_t thread_id) const;

	private:
		
		virtual uint32 OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) override;

		virtual ParserThreadTimeLine* GetThreadTimeLine(uint32 thread_id) override;

		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) override {
			std::wstring_view format_view{format};
			std::vector<std::wstring_view> field_namess;
			for (std::size_t i = 0; i < field_names_len; ++i)
			{
				field_namess.emplace_back(field_names[i]);
			}
			return 0;
		}

		virtual void SetMetadataSpec(uint32 event_id, uint32 metadata_space_id) override {
			auto event_name = GetCPUEventName(event_id);
			if (event_name.has_value() && event_name == L"Frame")
			{
				volatile int i2 = 0;
			}

			volatile int i = 0;
		}
	
		struct TimeLineTuple
		{
			std::size_t thread_id;
			std::string thread_name;
			std::unique_ptr<ParserThreadTimeLine> time_line;
		};

		std::vector<TimeLineTuple> thread_timelines;

		struct TimerInfo
		{
			std::size_t id;
			std::wstring event_name;
			std::wstring file_name;
			std::size_t file_line;
		};
		
		std::vector<TimerInfo> time_infos;

		virtual void AddThread(uint32 thread_id, char const* thread_name);
		friend void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser);
	};

	void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser)
	{
		DcomentWrapper ResourceWrapper(Resource);
		ExecuteParser(ResourceWrapper, Parser);
	}
}


//export import UEBabyPramInsightInterface;