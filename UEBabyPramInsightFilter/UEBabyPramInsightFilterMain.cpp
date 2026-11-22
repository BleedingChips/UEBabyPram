import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterCLI;

using namespace UEBabyPram::InsightFilter;

namespace
{
	// The wrapper of the document reader which reports how much of the trace has been read: the parser reads
	// the whole trace through it, so the progress of a long input is reported while it is read. The progress is
	// printed once per 1 GiB of the input, and the end of the input is reported when a read returns fewer bytes
	// than it was asked for. The progress goes to the terminal, never into the report file.
	struct ProgressDocumentWrapper : public UEBabyPram::InsightParser::DcomentWrapper
	{
		ProgressDocumentWrapper(Potato::Document::DocumentReader& reader)
			: DcomentWrapper(reader) {}

		virtual std::int32_t Read(void* out_data, std::uint32_t byte_size) override
		{
			std::int32_t read_size = DcomentWrapper::Read(out_data, byte_size);

			if (read_size > 0)
			{
				std::size_t read_byte_count = static_cast<std::size_t>(read_size);
				total_byte_count += read_byte_count;
				pending_byte_count += read_byte_count;
				read_complete = false;

				if (pending_byte_count > progress_byte_step)
				{
					Potato::Log::Log<L"    Input <{}> byte(s), Total <{}> byte(s)">(
						pending_byte_count,
						total_byte_count
					);
					pending_byte_count = 0;
				}
			}

			// A read which carried fewer bytes than it was asked for is the end of the input. The reads which
			// follow the end of the input carry no bytes, so the message is only printed once.
			if (!read_complete && read_size >= 0 && static_cast<std::uint32_t>(read_size) < byte_size)
			{
				read_complete = true;
				Potato::Log::Log<L"    Input Read Complete, Total <{}> byte(s)">(total_byte_count);
			}

			return read_size;
		}

		// The byte count which one progress line is printed for, 1 GiB.
		static constexpr std::size_t progress_byte_step = 1024ull * 1024ull * 1024ull;

		// The bytes of the whole input which is read so far.
		std::size_t total_byte_count = 0;
		// The bytes which are read since the last progress line.
		std::size_t pending_byte_count = 0;
		// Whether the end of the input has been reported, it keeps the reads which follow the end of the
		// input from reporting it again.
		bool read_complete = false;
	};

	// The printer which writes every message of the parsers into the report file, so the report of a parser is
	// stored in the file which -o or -a names instead of the standard output. The message is a wide string, so its
	// characters are decoded into a fixed 512 byte buffer, the buffer is flushed to the file, and the rest of the
	// message is decoded into the buffer again, until the whole message is written. A long message therefore
	// never needs a copy of its whole UTF-8 text.
	struct DocumentLogPrinter : public Potato::Log::LogPrinter
	{
		DocumentLogPrinter(Potato::Document::DocumentWriter& writer)
			: writer(writer) {}

		virtual void Print(Potato::Log::FormatedSystemTime time, Potato::Log::LogLevel level, std::pmr::wstring const& message) override
		{
			std::array<char8_t, 512> encoded_message;

			auto flush = [&](std::size_t size)
			{
				if (size != 0)
				{
					writer.Write(std::span<std::byte const>{
						reinterpret_cast<std::byte const*>(encoded_message.data()),
						size
					});
				}
			};

			// Encode into the fixed buffer, flush it, and go on with the rest of the message, until every
			// character of the message is consumed.
			if (!message.empty())
			{
				std::span<wchar_t const> remaining_message{ message.data(), message.size() };
				while (!remaining_message.empty())
				{
					auto encode_info = Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(
						remaining_message,
						std::span<char8_t>{ encoded_message }
					);

					flush(encode_info.target_space);

					// The rest of the message can not be encoded (ex: it carries an invalid character), so the
					// loop stops instead of spinning on the same character.
					if (encode_info.source_space == 0)
					{
						break;
					}

					remaining_message = remaining_message.subspan(encode_info.source_space);
				}
			}
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
	ProgressDocumentWrapper Input(Reader);
	UEBabyPram::InsightParser::ExecuteParser(Input, *setting.parser);
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
