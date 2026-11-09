import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterCLI;

using namespace UEBabyPram::InsightFilter;

namespace
{
	// The printer which writes every message of the parsers into a document writer, so the report of a parser is
	// stored in the file which -o names instead of the standard output. The message is a wide string, so its
	// characters are encoded into UTF-8 while they are written.
	struct DocumentLogPrinter : public Potato::Log::LogPrinter
	{
		DocumentLogPrinter(Potato::Document::DocumentWriter& writer)
			: writer(writer) {}

		virtual void Print(Potato::Log::FormatedSystemTime time, Potato::Log::LogLevel level, std::pmr::wstring const& message) override
		{
			std::pmr::u8string encoded_message;
			Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(message, std::back_inserter(encoded_message));

			writer.Write(std::span<std::byte const>{
				reinterpret_cast<std::byte const*>(encoded_message.data()),
				encoded_message.size()
			});
		}

		// The printer is owned by the caller of PrintToLog(), so the reference count of it is never used.
		virtual void AddLogPrinterRef() const override {}
		virtual void SubLogPrinterRef() const override {}

	protected:

		Potato::Document::DocumentWriter& writer;
	};
}

int main(int argc, char* argv[])
{
	CommandLineSetting setting;

	auto handle_result = HandleCommand(argc, argv, setting);
	if (handle_result > 0)
	{
		return 0;
	}
	if (handle_result < 0)
	{
		return handle_result;
	}

	if (std::endian::native == std::endian::big)
	{
		return -1;
	}

	if (!setting.parser || setting.insight_path.empty())
	{
		Potato::Log::Log<L"Require a target file with -f or --file, use -h for more infomation">();
		return -1;
	}

	auto start_time = std::chrono::system_clock::now();

	Potato::Log::Log<L"Start With UTrace File <{}>">(setting.insight_path.generic_wstring());

	Potato::Document::DocumentReader Reader(setting.insight_path);
	UEBabyPram::InsightParser::ExecuteParser(Reader, *setting.parser);

	// The report is written into the file which -o names, and into the standard output when -o is not given.
	if (setting.output_path.empty())
	{
		setting.parser->PrintToLog(*Potato::Log::GetSTDOutPrinter());
	}
	else {
		Potato::Document::DocumentWriter writer{
			setting.output_path,
			Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY
		};

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the output file <{}>">(setting.output_path.generic_wstring());
			return -1;
		}

		DocumentLogPrinter printer{ writer };
		setting.parser->PrintToLog(printer);
	}

	auto end_time = std::chrono::system_clock::now();

	Potato::Log::Log<"\nDone With Time: <{}s>">(
		std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
	);

	return 0;
}
