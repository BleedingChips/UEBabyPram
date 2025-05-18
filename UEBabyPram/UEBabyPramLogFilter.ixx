module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;


export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct LogLine
	{
		using TimeT = std::chrono::system_clock::time_point;
		std::wstring_view time;
		std::wstring_view frame_count;
		std::wstring_view category;
		std::wstring_view level;
		std::wstring_view str;
		std::wstring_view total_str;
		IndexSpan<> line;
		std::optional<std::size_t> GetFrameCount() const;
		std::optional<TimeT> GetTimePoint() const;
	};

	struct LogLineProcessor
	{
		std::optional<LogLine> ConsumeLinedString(std::wstring_view lined_string);
		std::optional<LogLine> End();
		void Clear();
		LogLineProcessor();
	protected:
		struct LogLineIndex
		{
			IndexSpan<> time;
			IndexSpan<> frame_count;
			IndexSpan<> category;
			IndexSpan<> line;
			std::size_t str_offset;
		};
		static LogLineIndex Translate(Potato::Reg::ProcessorAcceptRef const& Re, std::size_t LineOffset);
		Potato::Reg::DfaProcessor processor;
		std::wstring temporary_buffer;
		std::wstring finished_string;
		std::optional<LogLineIndex> LastIndex;
	};

	template<typename Func>
	void ForeachLogLine(std::wstring_view str, Func&& fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor processor;
		auto ite = str;
		while (!ite.empty())
		{
			auto offset = ite.find(L'\n');
			if (offset != ite.size())
				offset += 1;
			auto line = processor.ConsumeLinedString(ite.substr(0, offset));
			if (line)
				fun(*line);
			ite = ite.substr(offset);
		}
		auto line = processor.End();
		if (line)
			fun(*line);
	}

	template<typename Func>
	void ForeachLogLine(Potato::Document::DocumentReader& reader, Func&& fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor processor;

		std::wstring current_line;

		while (true)
		{
			current_line.clear();
			reader.ReadLine(std::back_inserter(current_line));
			std::optional<LogLine> logline_result;
			if (!current_line.empty())
			{
				logline_result = processor.ConsumeLinedString(current_line);
			}
			else {
				logline_result = processor.End();
			}
			if (logline_result)
				fun(*logline_result);
			
			if (current_line.empty())
				break;
		}
	}
}