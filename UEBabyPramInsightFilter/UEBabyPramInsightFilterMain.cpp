
import UEBabyPram;
import Potato;
import std;
import UEBabyPramInsightInterface;




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
		UEBabyPram::InsightParser::Test(insight_path);
	}

	return 0;
}