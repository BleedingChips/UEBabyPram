module;

module UEBabyPramLogFilter;
import Potato;

namespace UEBabyPram::LogFilter
{
	static constexpr std::u8string_view ebnf_string = u8R"(
		$:='\s+';
		INT:='[1-9][0-9]*' : [1];
		STR:='\"(.*?[^\\])\"' : [2];
		COMPARE:='<' : [4];
		COMPARE:='<=' : [5];
		COMPARE:='==' : [6];
		COMPARE:='>=':[7];
		COMPARE:='>' : [8];
		STRING_COMPARE:='StartWith' : [20];
		STRING_COMPARE:='EndWith' : [21];
		STRING_COMPARE:='Equal' : [22];
		STRING_COMPARE:='Contains' : [23];
		STRING_COMPARE:='Match' : [24];

		LOGLEVEL:='VeryVerbose':[10];
		LOGLEVEL:='Verbose':[11];
		LOGLEVEL:='Log':[12];
		LOGLEVEL:='Display':[13];
		LOGLEVEL:='Warning':[14];
		LOGLEVEL:='Error':[15];
		LOGLEVEL:='Fatal':[16];
		%%%%
		$:=<Exp>;
		<TIME>:=INT '.' INT '.' INT ':' INT '.' INT '.' INT ':' INT : [1];
			:=INT '.' INT '.' INT ':' INT '.' INT '.' INT  : [1];
			:=INT '.' INT '.' INT  ':' INT : [1];
			:=INT '.' INT '.' INT : [1];
		<STAT>:='Time' COMPARE <TIME> : [10];
			:='Level' COMPARE LOGLEVEL : [11];
			:='Line' COMPARE INT : [12];
			:='Log' '.'  STRING_COMPARE  '(' STR ')': [13];
			:='Category' '.'  STRING_COMPARE  '(' STR ')' : [14];
			:= '(' <STAT> ')' : [20];
			:= <STAT> '&&' <STAT>  : [21];
			:= <STAT> '||' <STAT>  : [22];
		<Exp>:=<STAT> ';' :[99];
			:=<STAT> :[99];
			:=;
		%%%%
		+('&&');
		+('||');
	)";

	static std::array<std::u8string_view, 7> log_level_array = {
		u8"VeryVerbose", u8"Verbose", u8"Log", u8"Display", u8"Warning", u8"Error", u8"Fatal"
	};

	Potato::EBNF::Ebnf const& GetEbnf()
	{
		static Potato::EBNF::Ebnf ebnf(ebnf_string);
		return ebnf;
	}

	std::u8string_view GetEbnfString()
	{
		return ebnf_string;
	}

	std::optional<bool> ConditionStatement::Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const
	{
		switch (property)
		{
		case PropertyType::Line:
			if (std::holds_alternative<std::size_t>(value))
			{
				std::size_t target_line = std::get<std::size_t>(value);
				switch(compare)
				{
				case CompareType::Bigger:
					return log.line.Begin() > target_line;
				case CompareType::BiggerEqual:
					return log.line.Begin() >= target_line;
				case CompareType::Equal:
					return log.line.Begin() == target_line;
				case CompareType::Smaller:
					return log.line.Begin() < target_line;
				case CompareType::SmallerEqual:
					return log.line.Begin() <= target_line;
				}
			}
			break;
		case PropertyType::Log:
			switch (compare)
			{
			case CompareType::Smaller:
				return log.str.starts_with(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::SmallerEqual:
				return log.str.ends_with(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::Equal:
				return log.str == std::u8string_view{ std::get<std::u8string>(value) };
			case CompareType::BiggerEqual:
				return log.str.contains(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::Bigger:
			{
				std::shared_ptr<re2::RE2> re2_ref = std::get<std::shared_ptr<re2::RE2>>(value);
				if (re2_ref->Match(
					std::string_view(reinterpret_cast<char const*>(log.str.data()), log.str.size()),
					0,
					log.str.size(),
					re2::RE2::Anchor::UNANCHORED,
					nullptr,
					0
				))
				{
					return true;
				}
				return false;
			}
			}
			break;
		case PropertyType::Category:
			switch (compare)
			{
			case CompareType::Smaller:
				return log.property.category.starts_with(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::SmallerEqual:
				return log.property.category.ends_with(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::Equal:
				return log.property.category == std::u8string_view{ std::get<std::u8string>(value) };
			case CompareType::BiggerEqual:
				return log.property.category.contains(std::u8string_view{ std::get<std::u8string>(value) });
			case CompareType::Bigger:
			{
				std::shared_ptr<re2::RE2> re2_ref = std::get<std::shared_ptr<re2::RE2>>(value);
				if (re2_ref->Match(
					std::string_view(reinterpret_cast<char const*>(log.property.category.data()), log.property.category.size()),
					0,
					log.str.size(),
					re2::RE2::Anchor::UNANCHORED,
					nullptr,
					0
				))
				{
					return true;
				}
				return false;
			}
			}
			break;
		case PropertyType::Level:
		{
			auto level_string = log.property.level;
			if (level_string.empty())
			{
				level_string = u8"Log";
			}
			std::size_t level = std::get<std::size_t>(value);
			std::size_t index = 0;
			for (; index < log_level_array.size(); ++index)
			{
				if (log.property.category == log_level_array[index])
				{
					break;
				}
			}
			if (index < log_level_array.size())
			{
				switch (compare)
				{
				case CompareType::Bigger:
					return index > level;
				case CompareType::BiggerEqual:
					return index >= level;
				case CompareType::Equal:
					return index == level;
				case CompareType::Smaller:
					return index < level;
				case CompareType::SmallerEqual:
					return index <= level;
				}
			}
		}
			break;
		case PropertyType::Time:
		{
			auto log_time = log.GetSystemClockTimePoint();
			if (!log_time.has_value())
			{
				return false;
			}
			auto value_time = std::get<LogParser::LogLine::TimeT>(value);
			auto bound_value = *UEBabyPram::LogParser::LogLine::GetSystemClockTimePoint(
				1970, 1, 2, 0, 0, 0, 0
			);
			if (bound_value > value_time)
			{
				log_time = *log_time - std::chrono::floor<std::chrono::days>(*log_time) + std::chrono::system_clock::time_point{};
			}
			switch (compare)
			{
			case CompareType::Bigger:
				return *log_time > value_time;
			case CompareType::BiggerEqual:
				return *log_time >= value_time;
			case CompareType::Equal:
				return *log_time == value_time;
			case CompareType::Smaller:
				return *log_time < value_time;
			case CompareType::SmallerEqual:
				return *log_time <= value_time;
			}
		}
			
			break;
		}
		return std::nullopt;
	}

	std::optional<bool> OperatorStatement::Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const
	{
		auto s1 = statement_1->Detect(log, processor);
		if (s1.has_value())
		{
			if (is_or && *s1 || !is_or && !*s1)
			{
				return *s1;
			}
			auto s2 = statement_2->Detect(log, processor);
			if (s2.has_value())
			{
				return *s2;
			}
		}
		return std::nullopt;
	}

	std::shared_ptr<StatementInterface> LogFilterProcessor::ComplierStatement(std::u8string_view statement, std::pmr::u8string& error_message)
	{
		Potato::EBNF::EbnfProcessor pro;

		auto lexical_function = [&](Potato::EBNF::SymbolInfo syminfo, std::size_t mask)->std::any {
			if (mask == 1)
			{
				std::size_t value = 0;
				Potato::Format::DirectDeformat(syminfo.TokenIndex.Slice(statement), value);
				return value;
			}
			else if (mask == 2)
			{
				std::u8string value{ syminfo.TokenIndex.Slice(statement) };
				return value;
			}
			else if (mask >= 4 && mask <= 8)
			{
				return static_cast<CompareType>(mask - 4);
			}
			else if (mask >= 10 && mask <= 16)
			{
				auto view = syminfo.TokenIndex.Slice(statement);
				std::size_t index = 0;
				for (; index < log_level_array.size(); ++index)
				{
					if (log_level_array[index] == view)
					{
						return std::size_t{ index };
					}
				}
				return log_level_array.size();
			}
			else if (mask >= 20 && mask <= 24)
			{
				return static_cast<CompareType>(mask - 20);
			}
			return std::any{};
		};
		auto parser_function = [&](Potato::EBNF::SymbolInfo symbol, Potato::EBNF::ReduceProduction production) -> std::any {

			if (production.UserMask >= 10 && production.UserMask <= 12)
			{
				auto state = std::make_shared<ConditionStatement>();
				state->compare = *production[1].TryConsume<CompareType>();

				switch (production.UserMask)
				{
				case 10:
					state->property = PropertyType::Time;
					state->value = *production[2].TryConsume<LogParser::LogLine::TimeT>();
					break;
				case 11:
					state->property = PropertyType::Level;
					state->value = *production[2].TryConsume<std::size_t>();
					break;
				case 12:
					state->property = PropertyType::Line;
					state->value = *production[2].TryConsume<std::size_t>();
					break;
				}
				return std::shared_ptr<StatementInterface>(state);
			}
			else if (production.UserMask == 13 || production.UserMask == 14)
			{
				auto state = std::make_shared<ConditionStatement>();
				state->property = (production.UserMask == 13 ? PropertyType::Log : PropertyType::Category);
				state->compare = *production[2].TryConsume<CompareType>();
				auto string = *production[4].TryConsume<std::u8string>();
				if (state->compare == CompareType::Bigger)
				{
					auto ptr = std::make_shared<re2::RE2>(
						std::string_view(reinterpret_cast<char const*>(string.data()), string.size())
					);
					if (!ptr->ok())
					{
						std::pmr::u8string error{ string };
						throw UnsupportReg{ std::move(error) };
					}
					state->value = ptr;
				}
				else {
					state->value = std::move(string);
				}
				return std::shared_ptr<StatementInterface>(state);
			}
			else {
				switch (production.UserMask)
				{
				case 20:
					return production[1].Consume();
				case 21:
				case 22:
				{
					auto state = std::make_shared<OperatorStatement>();
					state->statement_1 = *production[0].TryConsume<std::shared_ptr<StatementInterface>>();
					state->statement_2 = *production[2].TryConsume<std::shared_ptr<StatementInterface>>();
					state->is_or = (production.UserMask == 22);
					return std::shared_ptr<StatementInterface>(state);
				}
				case 1:
				{
					std::optional<LogParser::LogLine::TimeT> time;
					if (production.Size() == 13)
					{
						time = LogParser::LogLine::GetSystemClockTimePoint(
							*production[0].TryConsume<std::size_t>(),
							*production[2].TryConsume<std::size_t>(),
							*production[4].TryConsume<std::size_t>(),
							*production[6].TryConsume<std::size_t>(),
							*production[8].TryConsume<std::size_t>(),
							*production[10].TryConsume<std::size_t>(),
							*production[12].TryConsume<std::size_t>()
						);
					}
					else if (production.Size() == 11)
					{
						time = LogParser::LogLine::GetSystemClockTimePoint(
							*production[0].TryConsume<std::size_t>(),
							*production[2].TryConsume<std::size_t>(),
							*production[4].TryConsume<std::size_t>(),
							*production[6].TryConsume<std::size_t>(),
							*production[8].TryConsume<std::size_t>(),
							*production[10].TryConsume<std::size_t>(),
							0
						);
					}
					else if (production.Size() == 7)
					{
						time = LogParser::LogLine::GetSystemClockTimePoint(
							*production[0].TryConsume<std::size_t>(),
							*production[2].TryConsume<std::size_t>(),
							*production[4].TryConsume<std::size_t>(),
							*production[6].TryConsume<std::size_t>()
						);
					}
					else if (production.Size() == 5)
					{
						time = LogParser::LogLine::GetSystemClockTimePoint(
							*production[0].TryConsume<std::size_t>(),
							*production[2].TryConsume<std::size_t>(),
							*production[4].TryConsume<std::size_t>(),
							0
						);
					}
					if (!time.has_value())
					{
						Potato::Misc::IndexSpan<> index{
							production[0].TokenIndex.Begin(),
							production[production.Size() - 1].TokenIndex.End()
						};
						std::pmr::u8string error_message{ index.Slice(statement) };
						throw UnsupportTime{ error_message };
					}
					else {
						return *time;
					}
				}
				break;
				case 99:
					return production[0].Consume();
				}
			}
			return {};
		};


		pro.SetObserverTable(GetEbnf(), lexical_function, parser_function);

		try {
			if (Potato::EBNF::Process(pro, statement))
			{
				return pro.GetData<std::shared_ptr<StatementInterface>>();
			}
		}
		catch (UnsupportReg const& reg)
		{
			error_message = u8"Error: Unsupport Reg :";
			error_message.append(reg.error_message);
		}
		catch (UnsupportTime const& tim)
		{
			error_message = u8"Error: Unsupport time :";
			error_message.append(tim.error_message);
		}
		
		return {};
	}

	bool LogFilterProcessor::AddStatement(std::u8string_view statement, std::pmr::u8string& error_message)
	{
		auto node = ComplierStatement(statement, error_message);
		if (node)
		{
			if (this->statement)
			{
				auto state = std::make_shared<OperatorStatement>();
				state->statement_1 = this->statement;
				state->statement_2 = node;
				state->is_or = true;
				this->statement = state;
			}
			else {
				this->statement = std::move(node);
			}
			return true;
		}
		return false;
	}
}