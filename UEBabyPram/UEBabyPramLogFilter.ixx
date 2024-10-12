module;

#include <cassert>

export module UEBabyPramLogFilter;

import PotatoEncode;
import PotatoReg;
import PotatoDocument;
import PotatoMisc;
import std;

export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct LogTime
	{
		std::size_t Year = 0;
		std::size_t Month = 0;
		std::size_t Day = 0;
		std::size_t Hour = 0;
		std::size_t Min = 0;
		std::size_t Sec = 0;
		std::size_t MSec = 0;
		std::size_t FrameCount = 0;
	};

	struct LogLine
	{
		std::u8string_view Time;
		std::u8string_view Cagetory;
		std::u8string_view Level;
		std::u8string_view Str;
		std::u8string_view TotalStr;
		IndexSpan<> LineIndex;
	};

	struct LogLineProcessor
	{
		struct Result
		{
			std::optional<LogLine> CaptureLine;
			bool IsEnd;
		};

		std::optional<LogLine> Consume();

		LogLineProcessor(std::u8string_view TotalStr);

		operator bool() const {return Offset < TotalStr.size() || LastIndex.has_value(); }

		static std::optional<LogTime> GetTime(LogLine InputLine);

		void Clear();

	private:

		struct LogLineIndex
		{
			IndexSpan<> Time;
			IndexSpan<> Category;
			IndexSpan<> Str;
			IndexSpan<> LineIndex;
		};

		static LogLineIndex Translate(Potato::Reg::ProcessorAcceptRef const& Re, std::size_t Offset, std::size_t StrSize, std::size_t LineOffset);

		std::optional<LogLineIndex> LastIndex;
		Potato::Reg::DfaProcessor Pro;
		std::size_t Offset = 0;
		std::u8string_view TotalStr;
	};

	template<typename Func>
	std::size_t ForeachLogLine(LogLineProcessor& Pro, Func&& Fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		Pro.Clear();
		std::size_t Count = 0;
		while (Pro)
		{
			auto Re = Pro.Consume();
			assert(Re);
			std::forward<Func>(Fun)(*Re);
			++Count;
		}
		return Count;
	}

	template<typename Func>
	std::size_t ForeachLogLine(std::u8string_view Str, Func&& Fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor Pro(Str);
		return ForeachLogLine(Pro, std::forward<Func>(Fun));
	}
}