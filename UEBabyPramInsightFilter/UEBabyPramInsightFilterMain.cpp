

#include "Trace/Analyzer.h"
#include "Analysis/Engine.h"

import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserInterface;

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



int main(int argc, char* argv[])
{

	std::filesystem::path insight_path;

	for (std::size_t i = 0; i < argc; ++i)
	{
		std::string_view arg = argv[i];
		if (arg == "-f" || arg == "--file")
		{
			if (i + 1 < argc)
			{
				std::u8string path;
				Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
				insight_path = path;
				break;
			}
			else {
				std::cerr << "Error: No insight file path provided after " << arg << std::endl;
				return -1;
			}
		}
	}

	if (std::endian::native == std::endian::big)
	{
		return -1;
	}

	if (!insight_path.empty() && std::filesystem::exists(insight_path))
	{
		Potato::Document::DocumentReader Reader(insight_path);
		DcomentWrapper Wrapper{ Reader };
		UEBabyPram::InsightParser::Test(Wrapper);
	}

	return 0;
}