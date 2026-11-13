import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterCLI;

using namespace UEBabyPram::InsightFilter;

namespace
{
	// The writer which appends the report to a plain text file, the file is created when it does not exist and its
	// previous content is kept. The document writer of Potato writes at the beginning of a file which it opens without
	// emptying it, so the appending is done by the standard stream, which opens the file at its end.
	struct AppendDocumentWriter
	{
		explicit AppendDocumentWriter(std::filesystem::path const& path)
			: stream{ path, std::ios::binary | std::ios::app } {}

		std::size_t Write(std::span<std::byte const> input)
		{
			stream.write(reinterpret_cast<char const*>(input.data()), static_cast<std::streamsize>(input.size()));
			return stream ? input.size() : 0;
		}

		operator bool() const { return stream.is_open(); }

	protected:

		std::ofstream stream;
	};

	// The printer which writes every message of the parsers into the report file, so the report of a parser is
	// stored in the file which -o or -a names instead of the standard output. The message is a wide string, so its
	// characters are encoded into UTF-8 while they are written. The terminal is told the bytes which the message
	// carried and the bytes of the whole report which is written so far, which is what the progress of a long report
	// is read from, and it is told directly, so the numbers never end up in the report file.
	template<typename Writer>
	struct DocumentLogPrinter : public Potato::Log::LogPrinter
	{
		DocumentLogPrinter(Writer& writer)
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

		Writer& writer;
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
	else if (setting.append)
	{
		AppendDocumentWriter writer{ setting.output_path };

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the output file <{}> to append">(setting.output_path.generic_wstring());
			return -1;
		}

		DocumentLogPrinter<AppendDocumentWriter> printer{ writer };
		if (!setting.parser->PrintToLog(printer))
		{
			return -1;
		}
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

		DocumentLogPrinter<Potato::Document::DocumentWriter> printer{ writer };
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
