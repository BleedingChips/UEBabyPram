#pragma once
#include "Potato/Public/PotatoStrFormat.h"

namespace UEBabyPram::LogFilter
{
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
		std::optional<LogTime> Time;
		std::u32string Cagetory;
		std::size_t Line;
		std::u32string Str;
	};

	bool LoadUE4Log(std::filesystem::path Path, bool (*)(LogLine, void*), void* Data);

	template<typename Func>
	bool LoadUE4Log(std::filesystem::path Path, Func&& FO) requires(std::is_invocable_r_v<bool, Func, LogLine>)
	{
		return LoadUE4Log(std::move(Path), [](LogLine Line, void* Data)->bool{
			return (*reinterpret_cast<Func*>(Data))(std::move(Line));
		}, &FO);
	}
}