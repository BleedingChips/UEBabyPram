import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterCLI;

using namespace UEBabyPram::InsightFilter;

namespace
{
	// The printer which writes every message of the parsers into the report file, so the report of a parser is
	// stored in the file which -o or -a names instead of the standard output. The message is a wide string, so its
	// characters are encoded into UTF-8 while they are written. The terminal is told the bytes which the message
	// carried and the bytes of the whole report which is written so far, which is what the progress of a long report
	// is read from, and it is told directly, so the numbers never end up in the report file.
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

			total_byte_count += encoded_message.size();

			Potato::Log::Log<L"    Output <{}> byte(s), Total <{}> byte(s)">(
				encoded_message.size(),
				total_byte_count
			);
		}

		// The printer is owned by the caller of PrintToLog(), so the reference count of it is never used.
		virtual void AddLogPrinterRef() const override {}
		virtual void SubLogPrinterRef() const override {}

	protected:

		Potato::Document::DocumentWriter& writer;
		// The bytes of the whole report which is written by this run, it is the total which every message reports.
		std::size_t total_byte_count = 0;
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

	// The report is written into the file which -o names, which is created and emptied, or into the file which -a
	// names, which keeps its previous content, and into the standard output when neither of them is given.
	// The parser reports whether the report is written, the report of -es which is a CSV file is the one which may
	// fail, ex: when the file can not be opened, so the program fails with it.
	if (setting.output_path.empty())
	{
		if (!setting.parser->PrintToLog(*Potato::Log::GetSTDOutPrinter()))
		{
			return -1;
		}
	}
	else {
		// -o empties the file, while -a keeps its previous content and writes at the end of it, which is the reason
		// why the writer is opened with the mode of the option.
		auto open_mode = setting.append
			? Potato::Document::DocumentWriter::OpenMode::APPEND_OR_CREATE
			: Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY;

		Potato::Document::DocumentWriter writer{ setting.output_path, open_mode };

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the output file <{}>">(setting.output_path.generic_wstring());
			return -1;
		}

		DocumentLogPrinter printer{ writer };
		if (!setting.parser->PrintToLog(printer))
		{
			return -1;
		}
	}

	auto end_time = std::chrono::system_clock::now();

	Potato::Log::Log<"\nDone With Time: <{}s>">(
		std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count()
	);

	return 0;
}
