module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{
	void ParserThreadTimeLine::AppendBeginEvent(double start_time, std::uint32_t event_id)
	{
		stacks.emplace_back(
			event_id,
			start_time,
			depth
		);
		++depth;
	}

	void ParserThreadTimeLine::AppendEndEvent(double end_time)
	{
		assert(depth > 0);
		if (depth > 0)
		{
			--depth;
		}
		stacks.emplace_back(
			std::nullopt,
			end_time,
			depth
		);
		if (depth == 0)
		{
			reference.OnCPUStackTree(ThreadCPUEventView{
				thread_id,
				std::span(stacks.data(), stacks.size())
				});
			stacks.clear();
		}
	}

	ParserThreadTimeLine::~ParserThreadTimeLine()
	{
		//assert(depth == 0);
	}

	std::wstring_view ParserInterface::CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen)
	{
		if (ScopeName != nullptr && ScopeNameLen > 0)
		{
			if (ScopeName[ScopeNameLen - 1] == 0)
			{
				return std::wstring_view{ ScopeName, ScopeNameLen - 1 };
			}
			return std::wstring_view{ ScopeName, ScopeNameLen };
		}
		return {};
	}

	ParserThreadTimeLine* ParserInterface::GetThreadTimeLine(uint32 thread_id)
	{
		auto ite = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (ite != thread_timelines.end())
			return ite->time_line.get();
		auto new_timeline = std::unique_ptr<ParserThreadTimeLine>(new ParserThreadTimeLine{ thread_id, *this });
		if (new_timeline)
		{
			auto pointer = new_timeline.get();
			thread_timelines.emplace_back(TimeLineTuple{ thread_id, {}, std::move(new_timeline) });
			return pointer;
		}
		return nullptr;
	}

	void ParserInterface::AddThread(uint32 thread_id, char const* thread_name)
	{
		std::string_view thread_name_str;
		if (thread_name != nullptr)
		{
			thread_name_str = std::string_view(thread_name);
		}
		auto new_timeline = std::unique_ptr<ParserThreadTimeLine>(new ParserThreadTimeLine{ thread_id, *this });
		auto ite = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (ite != thread_timelines.end())
		{
			ite->thread_name = thread_name_str;
		}
		else {
			thread_timelines.emplace_back(TimeLineTuple{ thread_id, std::string{thread_name_str}, std::move(new_timeline) });
		}
		OnThreadDiscoverd(thread_id, thread_name_str);
	}

	uint32 ParserInterface::OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line)
	{
		auto cur_event_name = CoverStringView(event_name, event_name_len);
		auto cur_file_name = CoverStringView(file, file_name_len);
		auto event_id = time_infos.size();
		time_infos.emplace_back(
			event_id,
			std::wstring{ cur_event_name },
			std::wstring{ cur_file_name },
			line
		);
		OnCPUEventDiscoverd(event_id, cur_event_name, cur_file_name, line);
		return static_cast<uint32>(event_id);
	}

	std::optional<std::wstring_view> ParserInterface::GetCPUEventName(std::size_t event_id) const
	{
		if (event_id < time_infos.size())
		{
			return std::wstring_view{ time_infos[event_id].event_name };
		}
		return std::nullopt;
	}

	std::optional<std::string_view> ParserInterface::GetThreadName(std::size_t thread_id) const
	{
		auto find = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (find != thread_timelines.end())
		{
			return std::string_view{ find->thread_name };
		}
		return std::nullopt;
	}
}