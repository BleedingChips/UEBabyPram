module;

#include <cassert>

export module UEBabyPramInsightFilterCLI;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThread;
import UEBabyPramInsightParser;
import Potato;
import std;

export namespace UEBabyPram::InsightFilter
{
	using namespace Potato;

	constexpr auto CommandLineLogCategory = Potato::TMP::TypeString("InsightFilterCLI");

	// All the results which are read from the command line.
	struct CommandLineSetting
	{
		std::filesystem::path insight_path;
		std::unique_ptr<Parser> parser;
	};

	// Analyze the command line and fill the setting,
	// the parser is built with the GameThreadParser::Config, or with the EventSearchParser::Config
	// when an event name regex is given by -r --regex, which is collected from the command line,
	// and it is stored in the setting.
	// Return 0 when the program should keep going,
	// return a positive value when the command line has been fully handled and the program should exit with success,
	// return a negative value when the command line is invalid and the program should exit with that code.
	int HandleCommand(int argc, char* argv[], CommandLineSetting& setting);
}
