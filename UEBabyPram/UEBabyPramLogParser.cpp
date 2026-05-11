module;
#include <ctre-unicode.hpp>
#include <cassert>

module UEBabyPramLogParser;

namespace UEBabyPram::LogParser
{

	using namespace Potato;

	constexpr std::u8string_view Levels[] = {
		u8"Fatal: ",
		u8"Error: ",
		u8"Warning: ",
		u8"Display: ",
		u8"Verbose: ",
		u8"VeryVerbose: "
	};

	

	std::optional<std::size_t> LogLine::GetFrameCount() const
	{
		if (!property.frame_count.empty())
		{
			std::size_t number = 0;
			if (Format::DirectDeformat(property.frame_count, number))
			{
				return number;
			}
		}
		return std::nullopt;
	}

	std::optional<LogLine::TimeT> LogLine::GetSystemClockTimePoint(std::int32_t year, std::size_t month, std::size_t day, std::size_t hour, std::size_t min, std::size_t second, std::size_t milisecond)
	{
		if (
			year >= 1970
			&& month >= 1 && month <= 12
			&& day >= 1 && day <= 31
			&& hour < 24
			&& min < 60
			&& second < 60
			&& milisecond < 1000
			)
		{
			//return std::nullopt;

			std::tm time;
			time.tm_year = year - 1900;
			time.tm_mon = month - 1;
			time.tm_mday = day;
			time.tm_hour = hour;
			time.tm_min = min;
			time.tm_sec = second;
			time.tm_wday = 0;
			time.tm_isdst = -1;

			std::time_t local_time_t = std::mktime(&time);

			auto system_time_point = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(local_time_t)) + std::chrono::milliseconds{ milisecond };
			return system_time_point;
		}
		assert(false);
		return std::nullopt;
	}

	std::optional<LogLine::TimeT> LogLine::GetSystemClockTimePoint() const
	{
		if (!property.time.year.empty())
		{
			std::array<std::size_t, 7> Buffer;
			Potato::Format::DirectDeformat(property.time.year, Buffer[0]);
			Potato::Format::DirectDeformat(property.time.month, Buffer[1]);
			Potato::Format::DirectDeformat(property.time.day, Buffer[2]);
			Potato::Format::DirectDeformat(property.time.hour, Buffer[3]);
			Potato::Format::DirectDeformat(property.time.minute, Buffer[4]);
			Potato::Format::DirectDeformat(property.time.second, Buffer[5]);
			Potato::Format::DirectDeformat(property.time.millisecond, Buffer[6]);

			return LogLine::GetSystemClockTimePoint(
				static_cast<std::int32_t>(Buffer[0]),
				Buffer[1],
				Buffer[2],
				Buffer[3],
				Buffer[4],
				Buffer[5],
				Buffer[6]
			);
		}
		return std::nullopt;
	}

	LinePropertyResult GetLineProperty(std::u8string_view string)
	{
		LineProperty property;
		std::size_t offset = 0;
		auto match = ctre::starts_with<UR"(\[([0-9]{0,4})\.([0-9]{1,2})\.([0-9]{1,2})-([0-9]{1,2})\.([0-9]{1,2})\.([0-9]{1,2}):([0-9]{1,3})\]\[\s{0,3}([0-9]{1,3})\])">(string.substr(offset));
		if (match)
		{
			property.time.year = *match.get<1>().to_optional_view();
			property.time.month = *match.get<2>().to_optional_view();
			property.time.day = *match.get<3>().to_optional_view();
			property.time.hour = *match.get<4>().to_optional_view();
			property.time.minute = *match.get<5>().to_optional_view();
			property.time.second = *match.get<6>().to_optional_view();
			property.time.millisecond = *match.get<7>().to_optional_view();
			property.frame_count = *match.get<8>().to_optional_view();
			offset = (match.end() - string.data());
		}
		auto match2 = ctre::starts_with<U"[a-zA-Z][a-zA-Z0-9]*?: ">(string.substr(offset));
		if (match2)
		{
			auto category = *match2.to_optional_view();
			property.category = category;
			offset += category.size();
			property.category = category.substr(0, property.category.size() - 2);
		}
		if (offset != 0)
		{
			auto iter_string = string.substr(offset);
			for (auto& ite : Levels)
			{
				if (string.starts_with(ite))
				{
					property.level = ite.substr(0, ite.size() - 2);
					offset += ite.size();
					break;
				}
			}
			if (property.level.empty())
			{
				property.level = u8"Log";
			}
		}
		return { property, offset };
	}

	LinePropertyIndexResult GetLinePropertyIndex(std::u8string_view string)
	{
		LinePropertyIndex property;
		std::size_t offset = 0;
		auto match = ctre::starts_with<UR"(\[([0-9]{0,4})\.([0-9]{1,2})\.([0-9]{1,2})-([0-9]{1,2})\.([0-9]{1,2})\.([0-9]{1,2}):([0-9]{1,3})\]\[\s{0,3}([0-9]{1,3})\])">(string.substr(offset));
		
		auto GetIndex = [](auto const& math, std::u8string_view str) ->Potato::Misc::IndexSpan<> {
			return { static_cast<std::size_t>(math.begin() - str.data()), static_cast<std::size_t>(math.end() - str.data()) };
		};
		
		if (match)
		{
			property.time.year = GetIndex(match.get<1>(), string);
			property.time.month = GetIndex(match.get<2>(), string);
			property.time.day = GetIndex(match.get<3>(), string);
			property.time.hour = GetIndex(match.get<4>(), string);
			property.time.minute = GetIndex(match.get<5>(), string);
			property.time.second = GetIndex(match.get<6>(), string);
			property.time.millisecond = GetIndex(match.get<7>(), string);
			property.frame_count = GetIndex(match.get<8>(), string);
			offset = (match.end() - string.data());
		}
		auto match2 = ctre::starts_with<U"[a-zA-Z][a-zA-Z0-9]*?: ">(string.substr(offset));
		if (match2)
		{
			auto category = *match2.to_optional_view();
			property.category = { offset, offset + category.size() - 2};
			offset += category.size();
		}
		if (offset != 0)
		{
			auto iter_string = string.substr(offset);
			for (auto& ite : Levels)
			{
				if (string.starts_with(ite))
				{
					property.level = ite.substr(0, ite.size() - 2);
					offset += ite.size();
					break;
				}
			}
			if (property.level.empty())
			{
				property.level = u8"Log";
			}
		}
		return { property, offset };
	}

	std::optional<LogLine> GetLogLine(std::u8string_view log, LineContext& context)
	{
		while (context.next_line_offset < log.size())
		{
			auto last_log = log.substr(context.next_line_offset);
			auto log_property_result = GetLineProperty(last_log);
			last_log = last_log.substr(log_property_result.offset);
			auto find = last_log.find(u8'\n');
			std::size_t line_end = 0;

			if (find == decltype(last_log)::npos)
			{
				line_end = last_log.size();
			}
			else {
				line_end = find + 1;
			}

			context.total_line += 1;
			line_end += context.next_line_offset + log_property_result.offset;

			if (!log_property_result)
			{
				if (!context.property.has_value())
				{
					LogLine line;
					line.line = Potato::Misc::IndexSpan<>{
						context.property_line,
						context.total_line
					}.WholeOffset(1);

					line.total_str = Misc::IndexSpan<>{
						context.property_line_offset,
						line_end
					}.Slice(log);

					if (line.total_str.ends_with(u8"\r\n"))
						line.total_str = line.total_str.substr(0, line.total_str.size() - 2);
					else if(line.total_str.ends_with(u8"\n"))
						line.total_str = line.total_str.substr(0, line.total_str.size() - 1);

					line.str = line.total_str;

					context.property_line_offset = line_end;
					context.property_line = context.total_line;
					context.property.reset();
					context.next_line_offset = line_end;

					return line;
				}
			}
			else {
				if (context.property.has_value())
				{
					LogLine line;
					line.property = context.property->property;
					line.line = Potato::Misc::IndexSpan<>{
						context.property_line,
						context.total_line - 1
					}.WholeOffset(1);
					line.total_str = Potato::Misc::IndexSpan<>{
						context.property_line_offset,
						context.next_line_offset
					}.Slice(log);
					
					if (line.total_str.ends_with(u8"\r\n"))
						line.total_str = line.total_str.substr(0, line.total_str.size() - 2);
					else if (line.total_str.ends_with(u8"\n"))
						line.total_str = line.total_str.substr(0, line.total_str.size() - 1);
					line.str = line.total_str.substr(context.property->offset);

					context.property = log_property_result;
					context.property_line = context.total_line - 1;
					context.property_line_offset = context.next_line_offset;

					context.next_line_offset = line_end;

					return line;
				}
				else {
					context.property = log_property_result;
				}
			}

			context.next_line_offset = line_end;
		}
		if (context.property.has_value())
		{
			LogLine line;
			line.property = context.property->property;
			line.line = Potato::Misc::IndexSpan<>{
				context.property_line,
				context.total_line
			}.WholeOffset(1);
			line.total_str = Potato::Misc::IndexSpan<>{
				context.property_line_offset,
				context.next_line_offset
			}.Slice(log);

			if (line.total_str.ends_with(u8"\r\n"))
				line.total_str = line.total_str.substr(0, line.total_str.size() - 2);
			else if (line.total_str.ends_with(u8"\n"))
				line.total_str = line.total_str.substr(0, line.total_str.size() - 1);
			line.str = line.total_str.substr(context.property->offset);
			context.property.reset();
			return line;
		}
		return std::nullopt;
	}

	std::optional<LogLine> LineProcessor::ReadLine(Potato::Document::PlainTextReader& reader)
	{
		Potato::Document::PlainTextReader::CutoffSetting cutoff;
		cutoff.cutoff_character = U'\n';
		if (current_line_property.has_value())
		{
			cache_line = current_line;
			cache_line_property = current_line_property;
			line_record = { line , line + 1 };
			current_line.clear();
			current_line_property.reset();
		}
		while (true)
		{
			auto info = reader.ReadPlainText(std::back_inserter(current_line), cutoff);
			if (info)
			{
				line += 1;
				auto line_property = GetLinePropertyIndex(current_line);
				if (line_property)
				{
					current_line_property = line_property;
					auto log_line = GetLogLine();
					if (log_line.has_value())
					{
						return log_line;
					}
					cache_line = current_line;
					cache_line_property = current_line_property;
					line_record = { line , line + 1 };
					current_line.clear();
					current_line_property.reset();
				}
				else {
					cache_line.append(current_line);
					current_line.clear();
					line_record.BackwardEnd(1);
					if (!cache_line_property.has_value())
					{
						cache_line_property = LinePropertyIndexResult{};
					}
				}
			}
			else {
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

	std::optional<LogLine> LineProcessor::GetLogLine() const
	{
		if (cache_line_property.has_value())
		{
			LogLine current_line;
			current_line.property = cache_line_property->property.Slice(cache_line);
			current_line.total_str = std::u8string_view{ cache_line };
			current_line.str = current_line.total_str.substr(cache_line_property->offset);
			current_line.line = line_record;
			return current_line;
		}
		return std::nullopt;
	}
}