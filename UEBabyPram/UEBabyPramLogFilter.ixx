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
		std::u8string_view time;
		std::u8string_view frame_count;
		std::u8string_view category;
		std::u8string_view level;
		std::u8string_view str;
		std::u8string_view total_str;
		IndexSpan<> line;
		std::optional<std::size_t> GetFrameCount() const;
		std::optional<TimeT> GetTimePoint() const;
	};

	struct LogLineProcessor
	{
		std::optional<LogLine> ConsumeLinedString(std::u8string_view lined_string);
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
		std::u8string temporary_buffer;
		std::u8string finished_string;
		std::optional<LogLineIndex> LastIndex;
	};

	template<typename Func>
	void ForeachLogLine(std::u8string_view str, Func&& fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor processor;
		auto ite = str;
		while (!ite.empty())
		{
			auto offset = ite.find(u8'\n');
			if (offset != decltype(ite)::npos)
				offset += 1;
			else
				offset = ite.size();
			auto line = processor.ConsumeLinedString(ite.substr(0, offset));
			if (line)
				fun(*line);
			ite = ite.substr(offset);
		}
		auto line = processor.End();
		if (line)
			fun(*line);
	}
}