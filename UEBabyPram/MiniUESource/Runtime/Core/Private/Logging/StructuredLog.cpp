// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/StructuredLog.h"

#include "Algo/Accumulate.h"
#include "Algo/AllOf.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Async/Mutex.h"
#include "Async/TransactionallySafeMutex.h"
#include "Async/UniqueLock.h"
#include "AutoRTFM.h"
#include "Containers/AnsiString.h"
#include "Containers/SparseArray.h"
#include "Containers/StringConv.h"
#include "Containers/StringView.h"
#include "CoreGlobals.h"
#include "HAL/PlatformMath.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Text.h"
#include "Logging/LogTrace.h"
#include "Logging/StructuredLogFormat.h"
#include "Misc/AsciiSet.h"
#include "Misc/DateTime.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValue.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/VarInt.h"
#include "String/Split.h"
#include "Templates/Function.h"
#include <cstdarg>

void StaticFailDebug (const TCHAR* Error, const ANSICHAR* File, int32 Line, void* ProgramCounter, const TCHAR* Message);
void StaticFailDebugV(const TCHAR* Error, const ANSICHAR* File, int32 Line, void* ProgramCounter, const TCHAR* DescriptionFormat, va_list DescriptionArgs);

namespace UE::Serialization::Private
{
template <typename CharType>
void AppendQuotedJsonString(TStringBuilderBase<CharType>& Builder, FUtf8StringView Value);
} // UE::Serialization::Private

namespace UE::Logging::Private
{

// Temporary override until performance and functionality are sufficient for this to be the default.
CORE_API bool GConvertBasicLogToLogRecord = false;

// Experimental feature to prepend log context to the log message during formatting.
CORE_API bool GPrependLogContextToLogMessage = false;

static constexpr ANSICHAR GFieldPathDelimiter = '/';
static constexpr FAsciiSet GValidLogFieldName("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_");
static constexpr FAsciiSet GValidLogFieldPath = GValidLogFieldName | FAsciiSet({GFieldPathDelimiter, '\0'});

static constexpr FAnsiStringView GLogContextsFieldName = ANSITEXTVIEW("$Contexts");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FLogTemplateOp
{
	enum EOpCode : int32 { OpEnd, OpSkip, OpText, OpName, OpPath, OpIndex, OpLocalized, OpCount };

	static constexpr int32 ValueShift = 3;
	static_assert(OpCount <= (1 << ValueShift));

	EOpCode Code = OpEnd;
	int32 Value = 0;

	inline int32 GetSkipSize() const
	{
		return Code == OpIndex || Code == OpLocalized ? 0 : Value;
	}

	static inline FLogTemplateOp Load(const uint8*& Data);
	static inline uint32 SaveSize(const FLogTemplateOp& Op) { return MeasureVarUInt(Encode(Op)); }
	static inline void Save(const FLogTemplateOp& Op, uint8*& Data);
	static constexpr uint64 Encode(const FLogTemplateOp& Op) { return uint64(Op.Code) | (uint64(Op.Value) << ValueShift); }
	static constexpr FLogTemplateOp Decode(uint64 Value) { return {EOpCode(Value & ((1 << ValueShift) - 1)), int32(Value >> ValueShift)}; }
};

static_assert(FLogTemplateOp::Decode(FLogTemplateOp::Encode({.Value = 123})).Value == 123);
static_assert(FLogTemplateOp::Decode(FLogTemplateOp::Encode({.Value = -123})).Value == -123);

inline FLogTemplateOp FLogTemplateOp::Load(const uint8*& Data)
{
	uint32 ByteCount = 0;
	ON_SCOPE_EXIT { Data += ByteCount; };
	return Decode(ReadVarUInt(Data, ByteCount));
}

inline void FLogTemplateOp::Save(const FLogTemplateOp& Op, uint8*& Data)
{
	Data += WriteVarUInt(Encode(Op), Data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
struct TLogFieldValueConstants;

template <>
struct TLogFieldValueConstants<UTF8CHAR>
{
	static inline const FAnsiStringView Null = ANSITEXTVIEW("null");
	static inline const FAnsiStringView True = ANSITEXTVIEW("true");
	static inline const FAnsiStringView False = ANSITEXTVIEW("false");
};

template <>
struct TLogFieldValueConstants<WIDECHAR>
{
	static inline const FWideStringView Null = WIDETEXTVIEW("null");
	static inline const FWideStringView True = WIDETEXTVIEW("true");
	static inline const FWideStringView False = WIDETEXTVIEW("false");
};

template <typename CharType>
static void LogFieldValue(TStringBuilderBase<CharType>& Out, const FCbFieldView& Field)
{
	using FConstants = TLogFieldValueConstants<CharType>;
	switch (FCbValue Accessor = Field.GetValue(); Accessor.GetType())
	{
	case ECbFieldType::Null:
		Out.Append(FConstants::Null);
		break;
	case ECbFieldType::Object:
	case ECbFieldType::UniformObject:
		{
			FCbObjectView Object = Accessor.AsObjectView();

			// Use $text if present.
			if (FCbFieldView TextField = Object.FindViewIgnoreCase(ANSITEXTVIEW("$text")); TextField.IsString())
			{
				Out.Append(TextField.AsString());
				break;
			}

			// Use $format for formatting if present.
			if (FCbFieldView FormatField = Object.FindViewIgnoreCase(ANSITEXTVIEW("$format")); FormatField.IsString())
			{
				TUtf8StringBuilder<128> Format(InPlace, FormatField.AsString());
				FInlineLogTemplate Template(*Format, {.bAllowSubObjectReferences = true});
				Template.FormatTo(Out, Object.CreateViewIterator());
				break;
			}

			// Use $locformat/$locns/$loctext for localized formatting if present.
			FCbFieldView LocFormatField = Object.FindViewIgnoreCase(ANSITEXTVIEW("$locformat"));
			FCbFieldView LocNamespaceField = Object.FindViewIgnoreCase(ANSITEXTVIEW("$locns"));
			FCbFieldView LocKeyField = Object.FindViewIgnoreCase(ANSITEXTVIEW("$lockey"));
			if (LocFormatField.IsString() && LocNamespaceField.IsString() && LocKeyField.IsString())
			{
				TStringBuilder<32> Namespace(InPlace, LocNamespaceField.AsString());
				TStringBuilder<32> Key(InPlace, LocKeyField.AsString());
				TUtf8StringBuilder<128> Format(InPlace, LocFormatField.AsString());
				FInlineLogTemplate Template(*Namespace, *Key, *Format, {.bAllowSubObjectReferences = true});
				Template.FormatTo(Out, Object.CreateViewIterator());
				break;
			}

			// Write as JSON as a fallback.
			Out.AppendChar('{');
			bool bNeedsComma = false;
			for (FCbFieldView It : Field)
			{
				if (bNeedsComma)
				{
					Out.AppendChar(',').AppendChar(' ');
				}
				bNeedsComma = true;
				Serialization::Private::AppendQuotedJsonString(Out, It.GetName());
				Out.AppendChar(':').AppendChar(' ');
				LogFieldValue(Out, It);
			}
			Out.AppendChar('}');
		}
		break;
	case ECbFieldType::Array:
	case ECbFieldType::UniformArray:
		{
			Out.AppendChar('[');
			bool bNeedsComma = false;
			for (FCbFieldView It : Field)
			{
				if (bNeedsComma)
				{
					Out.AppendChar(',').AppendChar(' ');
				}
				bNeedsComma = true;
				LogFieldValue(Out, It);
			}
			Out.AppendChar(']');
		}
		break;
	case ECbFieldType::Binary:
		CompactBinaryToCompactJson(Field.RemoveName(), Out);
		break;
	case ECbFieldType::String:
		Out.Append(Accessor.AsString());
		break;
	case ECbFieldType::IntegerPositive:
		Out << Accessor.AsIntegerPositive();
		break;
	case ECbFieldType::IntegerNegative:
		Out << Accessor.AsIntegerNegative();
		break;
	case ECbFieldType::Float32:
	case ECbFieldType::Float64:
		CompactBinaryToCompactJson(Field.RemoveName(), Out);
		break;
	case ECbFieldType::BoolFalse:
		Out.Append(FConstants::False);
		break;
	case ECbFieldType::BoolTrue:
		Out.Append(FConstants::True);
		break;
	case ECbFieldType::ObjectAttachment:
	case ECbFieldType::BinaryAttachment:
		Out << Accessor.AsAttachment();
		break;
	case ECbFieldType::Hash:
		Out << Accessor.AsHash();
		break;
	case ECbFieldType::Uuid:
		Out << Accessor.AsUuid();
		break;
	case ECbFieldType::DateTime:
		Out << FDateTime(Accessor.AsDateTimeTicks()).ToIso8601();
		break;
	case ECbFieldType::TimeSpan:
	{
		const FTimespan Span(Accessor.AsTimeSpanTicks());
		if (Span.GetDays() == 0)
		{
			Out << Span.ToString(TEXT("%h:%m:%s.%n"));
		}
		else
		{
			Out << Span.ToString(TEXT("%d.%h:%m:%s.%n"));
		}
		break;
	}
	case ECbFieldType::ObjectId:
		Out << Accessor.AsObjectId();
		break;
	case ECbFieldType::CustomById:
	case ECbFieldType::CustomByName:
		CompactBinaryToCompactJson(Field.RemoveName(), Out);
		break;
	default:
		checkNoEntry();
		break;
	}
}

static void AddFieldValue(FFormatNamedArguments& Out, FUtf8StringView FieldPath, const FCbFieldView& Field)
{
	FString FieldName(FieldPath);

	switch (FCbValue Accessor = Field.GetValue(); Accessor.GetType())
	{
	case ECbFieldType::IntegerPositive:
		Out.Emplace(MoveTemp(FieldName), Accessor.AsIntegerPositive());
		return;
	case ECbFieldType::IntegerNegative:
		Out.Emplace(MoveTemp(FieldName), Accessor.AsIntegerNegative());
		return;
	case ECbFieldType::Float32:
		Out.Emplace(MoveTemp(FieldName), Accessor.AsFloat32());
		return;
	case ECbFieldType::Float64:
		Out.Emplace(MoveTemp(FieldName), Accessor.AsFloat64());
		return;
	default:
		break;
	}

	// Handle anything that falls through as text.
	TStringBuilder<128> Text;
	LogFieldValue(Text, Field);
	Out.Emplace(MoveTemp(FieldName), FText::FromString(FString(Text)));
}

template <typename FormatCharType>
class TFieldFinder
{
public:
	inline TFieldFinder(const FormatCharType* InFormat, const FCbFieldViewIterator& InFields)
		: Format(InFormat)
		, Fields(InFields)
	{
	}

	const FCbFieldView& Find(FUtf8StringView Name, int32 IndexHint = -1)
	{
		if (IndexHint >= 0)
		{
			for (; Index < IndexHint && It; ++Index, ++It)
			{
			}
			if (IndexHint < Index)
			{
				It = Fields;
				for (Index = 0; Index < IndexHint && It; ++Index, ++It);
			}
			if (IndexHint == Index && Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		const int32 PrevIndex = Index;
		for (; It; ++Index, ++It)
		{
			if (Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		It = Fields;
		for (Index = 0; Index < PrevIndex && It; ++Index, ++It)
		{
			if (Name.Equals(It.GetName()))
			{
				return It;
			}
		}
		checkf(false, TEXT("Log format requires field '%s' which was not provided. [[%s]]"),
			*WriteToString<32>(Name), StringCast<TCHAR>(Format).Get());
		return It;
	}

	FCbFieldView FindByPath(FUtf8StringView Path, int32 IndexHint = -1)
	{
		FUtf8StringView Name = Path;
		bool bMore = String::SplitFirstChar(Path, UTF8CHAR(GFieldPathDelimiter), Name, Path);
		FCbFieldView Field = Find(Name, IndexHint);
		while (bMore)
		{
			Name = Path;
			bMore = String::SplitFirstChar(Path, UTF8CHAR(GFieldPathDelimiter), Name, Path);
			Field = Field.AsObjectView().FindView(Name);
			checkf(Field, TEXT("Log format requires field '%s' which was not provided. [[%s]]"),
				*WriteToString<32>(Path), StringCast<TCHAR>(Format).Get());
		}
		return Field;
	};

private:
	const FormatCharType* Format;
	const FCbFieldViewIterator Fields;
	FCbFieldViewIterator It{Fields};
	int32 Index{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A table of localized log formats referenced by log templates.
 */
class FLocalizedLogFormatTable
{
public:
	int32 Add(FTextFormat&& Format)
	{
		TUniqueLock Lock(Mutex);
		return Table.Emplace(MoveTemp(Format));
	}

	void RemoveAt(int32 Index)
	{
		TUniqueLock Lock(Mutex);
		Table.RemoveAt(Index);
	}

	FTextFormat Get(int32 Index) const
	{
		TUniqueLock Lock(Mutex);
		return Table[Index];
	}

private:
	TSparseArray<FTextFormat> Table;
	mutable FTransactionallySafeMutex Mutex;
};

static FLocalizedLogFormatTable& GetLocalizedLogFormatTable()
{
	static FLocalizedLogFormatTable Table;
	return Table;
}

} // UE::Logging::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{

class FLogTemplate
{
	using FLogField = Logging::Private::FLogField;

public:
	template <typename FormatCharType>
	static FLogTemplate* Create(
		const FormatCharType* Format,
		const FLogTemplateOptions& Options,
		const FLogField* Fields,
		int32 FieldCount,
		TFunctionWithContext<void* (int32)> Allocate);

	template <typename FormatCharType>
	static FLogTemplate* CreateLocalized(
		const FText& FormatText,
		const FormatCharType* Format,
		const FLogTemplateOptions& Options,
		const FLogField* Fields,
		int32 FieldCount,
		TFunctionWithContext<void* (int32)> Allocate);

	static void Destroy(FLogTemplate* Template);

	const TCHAR* GetFormat() const { return StaticFormat; }
	const UTF8CHAR* GetUtf8Format() const { return StaticFormatUtf8; }

	uint8* GetOpData() { return (uint8*)(this + 1); }
	const uint8* GetOpData() const { return (const uint8*)(this + 1); }

	template <typename CharType>
	void FormatTo(TStringBuilderBase<CharType>& Out, const FCbFieldViewIterator& Fields) const;

	template <typename FormatCharType, typename OutputCharType>
	static void FormatTo(
		const FormatCharType* Format,
		TStringBuilderBase<OutputCharType>& Out,
		const FCbFieldViewIterator& Fields,
		const uint8* FirstOp);

	FText FormatToText(const FCbFieldViewIterator& Fields) const;

private:
	template <typename CharType>
	void FormatLocalizedTo(TStringBuilderBase<CharType>& Out, const FCbFieldViewIterator& Fields) const;

	template <typename FormatCharType, typename OutputCharType>
	static void FormatLocalizedTo(
		const FormatCharType* const Format,
		TStringBuilderBase<OutputCharType>& Out,
		const FCbFieldViewIterator& Fields,
		const uint8* const FirstOp);

	FText FormatLocalizedToText(const FCbFieldViewIterator& Fields) const;

	template <typename FormatCharType>
	static FText FormatLocalizedToText(
		const FormatCharType* Format,
		const FCbFieldViewIterator& Fields,
		const uint8* FirstOp);

	inline constexpr explicit FLogTemplate(const TCHAR* Format)
		: StaticFormat(Format)
	{
	}

	inline constexpr explicit FLogTemplate(const UTF8CHAR* Format)
		: StaticFormatUtf8(Format)
	{
	}

	FLogTemplate(const FLogTemplate&) = delete;
	FLogTemplate& operator=(const FLogTemplate&) = delete;

	const TCHAR* StaticFormat = nullptr;
	const UTF8CHAR* StaticFormatUtf8 = nullptr;
};

static_assert(std::is_trivially_destructible_v<FLogTemplate>);

template <typename FormatCharType>
FLogTemplate* FLogTemplate::Create(
	const FormatCharType* Format,
	const FLogTemplateOptions& Options,
	const FLogField* Fields,
	const int32 FieldCount,
	TFunctionWithContext<void* (int32)> Allocate)
{
	using namespace Logging::Private;

	const TConstArrayView<FLogField> FieldsView(Fields, FieldCount);
	const bool bFindFields = !!Fields;
	const bool bPositional = !FieldCount || Algo::NoneOf(FieldsView, &FLogField::Name);
	checkf(bPositional || Algo::AllOf(FieldsView, &FLogField::Name),
		TEXT("Log fields must be entirely named or entirely anonymous. [[%s]]"), StringCast<TCHAR>(Format).Get());
	checkf(bPositional || Algo::AllOf(FieldsView,
		[](const FLogField& Field) { return *Field.Name && *Field.Name != '_' && FAsciiSet::HasOnly(Field.Name, GValidLogFieldName); }),
		TEXT("Log field names must match \"[A-Za-z0-9][A-Za-z0-9_]*\" in [[%s]]."), StringCast<TCHAR>(Format).Get());

	TArray<FLogTemplateOp, TInlineAllocator<16>> Ops;

	TAnsiStringBuilder<256> FieldPathData;
	TArray<int32, TInlineAllocator<16>> FieldPathSizes;

	int32 FieldSearchIndex = -1;
	int32 FormatFieldCount = 0;
	int32 SymbolSearchOffset = 0;
	for (const FormatCharType* TextStart = Format;;)
	{
		constexpr FAsciiSet Brackets("{}");
		const FormatCharType* const TextEnd = FAsciiSet::FindFirstOrEnd(TextStart + SymbolSearchOffset, Brackets);
		SymbolSearchOffset = 0;

		// Escaped "{{" or "}}"
		if ((TextEnd[0] == '{' && TextEnd[1] == '{') ||
			(TextEnd[0] == '}' && TextEnd[1] == '}'))
		{
			// Only "{{" or "}}"
			if (TextStart == TextEnd)
			{
				Ops.Add({FLogTemplateOp::OpSkip, 1});
				TextStart = TextEnd + 1;
				SymbolSearchOffset = 1;
			}
			// Text and "{{" or "}}"
			else
			{
				Ops.Add({FLogTemplateOp::OpText, UE_PTRDIFF_TO_INT32(1 + TextEnd - TextStart)});
				Ops.Add({FLogTemplateOp::OpSkip, 1});
				TextStart = TextEnd + 2;
			}
			continue;
		}

		// Text
		if (TextStart != TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpText, UE_PTRDIFF_TO_INT32(TextEnd - TextStart)});
		}

		// End
		if (!*TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpEnd});
			break;
		}

		// Parse and validate the field path.
		const FormatCharType* const FieldStart = TextEnd;
		checkf(*FieldStart == '{', TEXT("Log format has an unexpected '%c' character. Use '%c%c' to escape it. [[%s]]"),
			*FieldStart, *FieldStart, *FieldStart, StringCast<TCHAR>(Format).Get());
		const FormatCharType* const FieldPathEnd = FAsciiSet::Skip(FieldStart + 1, GValidLogFieldPath);
		checkf(*FieldPathEnd, TEXT("Log format has an unterminated field reference. Use '{{' to escape '{' if needed. [[%s]]"),
			StringCast<TCHAR>(Format).Get());
		checkf(*FieldPathEnd == '}', TEXT("Log format has invalid character '%c' in field name. "
			"Use '{{' to escape '{' if needed. Names must match \"[A-Za-z0-9][A-Za-z0-9_]*\". [[%s]]"),
			*FieldPathEnd, StringCast<TCHAR>(Format).Get());
		const FormatCharType* const FieldEnd = FieldPathEnd + 1;
		const int32 FieldLen = UE_PTRDIFF_TO_INT32(FieldEnd - FieldStart);
		checkf(FieldStart[1] != '_', TEXT("Log format uses reserved field name '%s' with leading '_'. "
			"Names must match \"[A-Za-z0-9][A-Za-z0-9_]*\". [[%s]]"),
			*WriteToString<32>(MakeStringView(FieldStart + 1, FieldLen - 2)), StringCast<TCHAR>(Format).Get());

		const int32 FieldPathIndex = FieldPathData.Len();
		FieldPathData.Append(FieldStart + 1, FieldLen - 2);
		const FAnsiStringView FieldPath = FieldPathData.ToView().RightChop(FieldPathIndex);
		FieldPathSizes.Add(FieldPath.Len());

		const bool bHasSubObjectReference = !!Algo::Find(FieldPath, GFieldPathDelimiter);
		checkf(!bHasSubObjectReference || Options.bAllowSubObjectReferences,
			TEXT("Log format has a sub-object reference (%c) in a context that does not allow them. [[%s]]"),
			GFieldPathDelimiter, StringCast<TCHAR>(Format).Get());

		if (bFindFields && !bPositional)
		{
			bool bFoundField = false;
			for (int32 SearchCount = FieldCount; SearchCount > 0; --SearchCount)
			{
				FieldSearchIndex = (FieldSearchIndex + 1) % FieldCount;
				if (FieldPath.Equals(Fields[FieldSearchIndex].Name))
				{
					Ops.Add({FLogTemplateOp::OpIndex, FieldSearchIndex});
					bFoundField = true;
					break;
				}
			}
			checkf(bFoundField, TEXT("Log format requires field '%.*hs' which was not provided. [[%s]]"),
				FieldPath.Len(), FieldPath.GetData(), StringCast<TCHAR>(Format).Get());
		}

		Ops.Add({bHasSubObjectReference ? FLogTemplateOp::OpPath : FLogTemplateOp::OpName, FieldLen});
		++FormatFieldCount;

		TextStart = FieldEnd;
	}

	checkf(!bFindFields || !bPositional || FormatFieldCount == FieldCount,
		TEXT("Log format requires %d fields and %d were provided. [[%s]]"),
		FormatFieldCount, FieldCount, StringCast<TCHAR>(Format).Get());

	const uint32 TotalSize = sizeof(FLogTemplate) + Algo::TransformAccumulate(Ops, FLogTemplateOp::SaveSize, 0);
	FLogTemplate* const Template = new(Allocate(TotalSize)) FLogTemplate(Format);
	uint8* Data = Template->GetOpData();
	for (const FLogTemplateOp& Op : Ops)
	{
		FLogTemplateOp::Save(Op, Data);
	}
	return Template;
}

template <typename FormatCharType>
FLogTemplate* FLogTemplate::CreateLocalized(
	const FText& FormatText,
	const FormatCharType* Format,
	const FLogTemplateOptions& Options,
	const FLogField* Fields,
	const int32 FieldCount,
	TFunctionWithContext<void* (int32)> Allocate)
{
	// A localized format string consists of an OpLocalized op followed by a sequence of OpSkip and OpName/OpPath ops
	// that are terminated by an OpEnd op. Only the first occurrence of each name/path is included and everything else
	// in the format string is skipped. Anything following the last name/path is ignored and not even skipped.

	using namespace Logging::Private;

	const bool bFindFields = !!Fields;
	checkf(!bFindFields || !Options.bAllowSubObjectReferences,
		TEXT("Validation of field names is not compatible with sub-object references. [[%s]]"), StringCast<TCHAR>(Format).Get());

	TArray<FLogTemplateOp, TInlineAllocator<16>> Ops;
	Ops.Add({FLogTemplateOp::OpLocalized, GetLocalizedLogFormatTable().Add(FTextFormat(FormatText))});

	// Track unique field names to avoid adding multiple ops for the same name.
	TAnsiStringBuilder<256> FieldPathData;
	TArray<int32, TInlineAllocator<16>> FieldPathSizes;

	// Parse the format string to find unique field names and optionally validate that required fields are present.
	int32 FieldSearchIndex = -1;
	int32 SymbolSearchOffset = 0;
	for (const FormatCharType* TextStart = Format;;)
	{
		constexpr FAsciiSet Symbols("`{}");
		const FormatCharType* const TextEnd = FAsciiSet::FindFirstOrEnd(TextStart + SymbolSearchOffset, Symbols);
		SymbolSearchOffset = 0;

		// Escaped "``" or "`{" or "`}"
		if (TextEnd[0] == '`' && (TextEnd[1] == '`' || TextEnd[1] == '{' || TextEnd[1] == '}'))
		{
			// Continue the search after the escaped symbol.
			SymbolSearchOffset = UE_PTRDIFF_TO_INT32(2 + TextEnd - TextStart);
			continue;
		}

		// End. Implicitly skips any text after the last field path.
		if (!*TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpEnd});
			break;
		}

		// Parse and validate the field path.
		const FormatCharType* const FieldStart = TextEnd;
		checkf(*FieldStart == '{', TEXT("Log format has an unexpected '%c' character. Use '`%c' to escape it. [[%s]]"),
			*FieldStart, *FieldStart, StringCast<TCHAR>(Format).Get());
		const FormatCharType* const FieldPathEnd = FAsciiSet::Skip(FieldStart + 1, GValidLogFieldPath);
		checkf(*FieldPathEnd, TEXT("Log format has an unterminated field reference. Use '`{' to escape '{' if needed. [[%s]]"),
			StringCast<TCHAR>(Format).Get());
		checkf(*FieldPathEnd == '}', TEXT("Log format has invalid character '%c' in field name. "
			"Use '`{' to escape '{' if needed. Names must match \"[A-Za-z0-9][A-Za-z0-9_]*\". [[%s]]"),
			*FieldPathEnd, StringCast<TCHAR>(Format).Get());
		const FormatCharType* const FieldEnd = FieldPathEnd + 1;
		const int32 FieldLen = UE_PTRDIFF_TO_INT32(FieldEnd - FieldStart);
		checkf(FieldStart[1] != '_', TEXT("Log format uses reserved field name '%s' with leading '_'. "
			"Names must match \"[A-Za-z0-9][A-Za-z0-9_]*\". [[%s]]"),
			*WriteToString<32>(MakeStringView(FieldStart + 1, FieldLen - 2)), StringCast<TCHAR>(Format).Get());

		const int32 FieldPathIndex = FieldPathData.Len();
		FieldPathData.Append(FieldStart + 1, FieldLen - 2);
		const FAnsiStringView FieldPath = FieldPathData.ToView().RightChop(FieldPathIndex);

		const bool bHasSubObjectReference = !!Algo::Find(FieldPath, GFieldPathDelimiter);
		checkf(!bHasSubObjectReference || Options.bAllowSubObjectReferences,
			TEXT("Log format has a sub-object reference (%c) in a context that does not allow them. [[%s]]"),
			GFieldPathDelimiter, StringCast<TCHAR>(Format).Get());

		// Check if the field path has been seen and skip it if it has.
		const bool bExistingField = !!Algo::FindByPredicate(FieldPathSizes, [FieldPath, Data = FieldPathData.GetData()](int32 Size) mutable
		{
			ON_SCOPE_EXIT { Data += Size; };
			return FieldPath.Equals(MakeStringView(Data, Size));
		});
		if (bExistingField)
		{
			// Continue the search after the repeated field path.
			SymbolSearchOffset = UE_PTRDIFF_TO_INT32(FieldEnd - TextStart);
			FieldPathData.RemoveSuffix(FieldPath.Len());
			continue;
		}
		FieldPathSizes.Add(FieldPath.Len());

		// Skip the text along with any escaped symbols and repeated field paths.
		if (TextStart != TextEnd)
		{
			Ops.Add({FLogTemplateOp::OpSkip, UE_PTRDIFF_TO_INT32(TextEnd - TextStart)});
		}

		if (bFindFields)
		{
			bool bFoundField = false;
			for (int32 SearchCount = FieldCount; SearchCount > 0; --SearchCount)
			{
				FieldSearchIndex = (FieldSearchIndex + 1) % FieldCount;
				if (FieldPath.Equals(Fields[FieldSearchIndex].Name))
				{
					Ops.Add({FLogTemplateOp::OpIndex, FieldSearchIndex});
					bFoundField = true;
					break;
				}
			}
			checkf(bFoundField, TEXT("Log format requires field '%.*hs' which was not provided. [[%s]]"),
				FieldPath.Len(), FieldPath.GetData(), StringCast<TCHAR>(Format).Get());
		}

		Ops.Add({bHasSubObjectReference ? FLogTemplateOp::OpPath : FLogTemplateOp::OpName, FieldLen});

		TextStart = FieldEnd;
	}

	const uint32 TotalSize = sizeof(FLogTemplate) + Algo::TransformAccumulate(Ops, FLogTemplateOp::SaveSize, 0);
	FLogTemplate* const Template = new(Allocate(TotalSize)) FLogTemplate(Format);
	uint8* Data = Template->GetOpData();
	for (const FLogTemplateOp& Op : Ops)
	{
		FLogTemplateOp::Save(Op, Data);
	}
	return Template;
}

void FLogTemplate::Destroy(FLogTemplate* Template)
{
	using namespace Logging::Private;

	const uint8* NextOp = Template->GetOpData();
	if (FLogTemplateOp Op = FLogTemplateOp::Load(NextOp); Op.Code == FLogTemplateOp::OpLocalized)
	{
		GetLocalizedLogFormatTable().RemoveAt(Op.Value);
	}
}

template <typename CharType>
void FLogTemplate::FormatTo(TStringBuilderBase<CharType>& Out, const FCbFieldViewIterator& Fields) const
{
	if (StaticFormatUtf8)
	{
		FormatTo(StaticFormatUtf8, Out, Fields, GetOpData());
	}
	else
	{
		FormatTo(StaticFormat, Out, Fields, GetOpData());
	}
}

template <typename FormatCharType, typename OutputCharType>
void FLogTemplate::FormatTo(
	const FormatCharType* const Format,
	TStringBuilderBase<OutputCharType>& Out,
	const FCbFieldViewIterator& Fields,
	const uint8* const FirstOp)
{
	using namespace Logging::Private;

	int32 FieldIndexHint = -1;
	const uint8* NextOp = FirstOp;
	const FormatCharType* NextFormat = Format;
	TFieldFinder FieldFinder(Format, Fields);
	for (;;)
	{
		const FLogTemplateOp Op = FLogTemplateOp::Load(NextOp);
		switch (Op.Code)
		{
		case FLogTemplateOp::OpLocalized:
			return FormatLocalizedTo(Format, Out, Fields, FirstOp);
		case FLogTemplateOp::OpEnd:
			return;
		case FLogTemplateOp::OpText:
			Out.Append(NextFormat, Op.Value);
			break;
		case FLogTemplateOp::OpIndex:
			FieldIndexHint = Op.Value;
			break;
		case FLogTemplateOp::OpName:
			{
				const auto Name = StringCast<UTF8CHAR, 32>(NextFormat + 1, Op.Value - 2);
				LogFieldValue(Out, FieldFinder.Find(Name, FieldIndexHint));
				FieldIndexHint = -1;
			}
			break;
		case FLogTemplateOp::OpPath:
			{
				const auto Path = StringCast<UTF8CHAR, 32>(NextFormat + 1, Op.Value - 2);
				LogFieldValue(Out, FieldFinder.FindByPath(Path, FieldIndexHint));
				FieldIndexHint = -1;
			}
			break;
		}
		NextFormat += Op.GetSkipSize();
	}
}

FText FLogTemplate::FormatToText(const FCbFieldViewIterator& Fields) const
{
	using namespace Logging::Private;

	const uint8* NextOp = GetOpData();
	if (FLogTemplateOp::Load(NextOp).Code == FLogTemplateOp::OpLocalized)
	{
		return FormatLocalizedToText(Fields);
	}
	else
	{
		TStringBuilder<512> Builder;
		FormatTo(Builder, Fields);
		return FText::FromStringView(Builder);
	}
}

template <typename CharType>
FORCENOINLINE void FLogTemplate::FormatLocalizedTo(TStringBuilderBase<CharType>& Out, const FCbFieldViewIterator& Fields) const
{
	Out.Append(FormatLocalizedToText(Fields).ToString());
}

template <typename FormatCharType, typename OutputCharType>
FORCENOINLINE void FLogTemplate::FormatLocalizedTo(
	const FormatCharType* const Format,
	TStringBuilderBase<OutputCharType>& Out,
	const FCbFieldViewIterator& Fields,
	const uint8* const FirstOp)
{
	Out.Append(FormatLocalizedToText(Format, Fields, FirstOp).ToString());
}

FText FLogTemplate::FormatLocalizedToText(const FCbFieldViewIterator& Fields) const
{
	if (StaticFormatUtf8)
	{
		return FormatLocalizedToText(StaticFormatUtf8, Fields, GetOpData());
	}
	else
	{
		return FormatLocalizedToText(StaticFormat, Fields, GetOpData());
	}
}

template <typename FormatCharType>
FText FLogTemplate::FormatLocalizedToText(
	const FormatCharType* const Format,
	const FCbFieldViewIterator& Fields,
	const uint8* const FirstOp)
{
	using namespace Logging::Private;

	TOptional<FTextFormat> TextFormat;
	FFormatNamedArguments TextFormatArguments;

	int32 FieldIndexHint = -1;
	const uint8* NextOp = FirstOp;
	const FormatCharType* NextFormat = Format;
	TFieldFinder FieldFinder(Format, Fields);
	while (NextOp)
	{
		const FLogTemplateOp Op = FLogTemplateOp::Load(NextOp);
		switch (Op.Code)
		{
		case FLogTemplateOp::OpLocalized:
			TextFormat = GetLocalizedLogFormatTable().Get(Op.Value);
			break;
		case FLogTemplateOp::OpEnd:
			NextOp = nullptr;
			break;
		case FLogTemplateOp::OpIndex:
			FieldIndexHint = Op.Value;
			break;
		case FLogTemplateOp::OpName:
			{
				const auto Name = StringCast<UTF8CHAR, 32>(NextFormat + 1, Op.Value - 2);
				AddFieldValue(TextFormatArguments, Name, FieldFinder.Find(Name, FieldIndexHint));
				FieldIndexHint = -1;
			}
			break;
		case FLogTemplateOp::OpPath:
			{
				const auto Path = StringCast<UTF8CHAR, 32>(NextFormat + 1, Op.Value - 2);
				AddFieldValue(TextFormatArguments, Path, FieldFinder.FindByPath(Path, FieldIndexHint));
				FieldIndexHint = -1;
			}
			break;
		}
		NextFormat += Op.GetSkipSize();
	}

	checkf(TextFormat, TEXT("Missing text format when formatting localized template. [[%s]]"), StringCast<TCHAR>(Format).Get());
	return FText::Format(*TextFormat, TextFormatArguments);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Logging::Private::CreateLogTemplate(const TCHAR* Format, const FLogTemplateOptions& Options, const FLogField* Fields, const int32 FieldCount, TFunctionWithContext<void* (int32)> Allocate)
{
	FLogTemplate::Create(Format, Options, Fields, FieldCount, Allocate);
}

void Logging::Private::CreateLogTemplate(const UTF8CHAR* Format, const FLogTemplateOptions& Options, const FLogField* Fields, const int32 FieldCount, TFunctionWithContext<void* (int32)> Allocate)
{
	FLogTemplate::Create(Format, Options, Fields, FieldCount, Allocate);
}

void Logging::Private::CreateLocalizedLogTemplate(const FText& Format, const FLogTemplateOptions& Options, const FLogField* Fields, const int32 FieldCount, TFunctionWithContext<void* (int32)> Allocate)
{
	FLogTemplate::CreateLocalized(Format, *Format.ToString(), Options, Fields, FieldCount, Allocate);
}

void Logging::Private::CreateLocalizedLogTemplate(const TCHAR* TextNamespace, const TCHAR* TextKey, const TCHAR* Format, const FLogTemplateOptions& Options, const FLogField* Fields, const int32 FieldCount, TFunctionWithContext<void* (int32)> Allocate)
{
	const FText FormatText = FText::AsLocalizable_Advanced(TextNamespace, TextKey, Format);
	FLogTemplate::CreateLocalized(FormatText, Format, Options, Fields, FieldCount, Allocate);
}

void Logging::Private::CreateLocalizedLogTemplate(const TCHAR* TextNamespace, const TCHAR* TextKey, const UTF8CHAR* Format, const FLogTemplateOptions& Options, const FLogField* Fields, const int32 FieldCount, TFunctionWithContext<void* (int32)> Allocate)
{
	const FText FormatText = FText::AsLocalizable_Advanced(TextNamespace, TextKey, Format);
	FLogTemplate::CreateLocalized(FormatText, Format, Options, Fields, FieldCount, Allocate);
}

void Logging::Private::DestroyLogTemplate(FLogTemplate* Template)
{
	if (Template)
	{
		FLogTemplate::Destroy(Template);
	}
}

void FormatLogTo(FUtf8StringBuilderBase& Out, const FLogTemplate* Template, const FCbFieldViewIterator& Fields)
{
	Template->FormatTo(Out, Fields);
}

void FormatLogTo(FWideStringBuilderBase& Out, const FLogTemplate* Template, const FCbFieldViewIterator& Fields)
{
	Template->FormatTo(Out, Fields);
}

FText FormatLogToText(const FLogTemplate* Template, const FCbFieldViewIterator& Fields)
{
	return Template->FormatToText(Fields);
}

void SerializeLogFormat(FCbWriter& Writer, const FText& Format)
{
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Format);
	const TOptional<FString> Key = FTextInspector::GetKey(Format);
	const FString* Source = FTextInspector::GetSourceString(Format);
	checkf(Namespace && Key && Source, TEXT("Serializing a localized format string requires a namespace, key, and source string. [[%s]]"), *Format.ToString());
	Writer.AddString(ANSITEXTVIEW("$locformat"), *Source);
	Writer.AddString(ANSITEXTVIEW("$locns"), *Namespace);
	Writer.AddString(ANSITEXTVIEW("$lockey"), *Key);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FLogTime FLogTime::Now()
{
	FLogTime Time;
	Time.UtcTicks = FDateTime::UtcNow().GetTicks();
	return Time;
}

FLogTime FLogTime::FromUtcTime(const FDateTime& UtcTime)
{
	FLogTime Time;
	Time.UtcTicks = UtcTime.GetTicks();
	return Time;
}

FDateTime FLogTime::GetUtcTime() const
{
	return FDateTime(UtcTicks);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename CharType>
FORCENOINLINE static void FormatDynamicRecordMessageTo(TStringBuilderBase<CharType>& Out, const FLogRecord& Record)
{
	const TCHAR* Format = Record.GetFormat();
	if (UNLIKELY(!Format))
	{
		return;
	}

	const TCHAR* TextNamespace = Record.GetTextNamespace();
	const TCHAR* TextKey = Record.GetTextKey();
	checkf(!TextNamespace == !TextKey,
		TEXT("Log record must have both or neither of the text namespace and text key. [[%s]]"), Format);

	TOptional<FInlineLogTemplate> LocalTemplate;
	TextKey ? LocalTemplate.Emplace(TextNamespace, TextKey, Format) : LocalTemplate.Emplace(Format);
	LocalTemplate->FormatTo(Out, Record.GetFields().CreateViewIterator());
}

template <typename CharType>
static void FormatRecordMessageTo(TStringBuilderBase<CharType>& Out, const FLogRecord& Record)
{
	using namespace Logging::Private;

	if (GPrependLogContextToLogMessage)
	{
		const FCbObject& Fields = Record.GetFields();
		for (FCbFieldView NameField : Fields[GLogContextsFieldName])
		{
			const FUtf8StringView NameView = NameField.AsString();
			if (FCbFieldView ContextField = Fields[NameView])
			{
				Out.Append(NameView);
				if (!ContextField.IsNull())
				{
					Out.AppendChar('(');
					CompactBinaryToCompactJson(ContextField.RemoveName(), Out);
					Out.AppendChar(')');
				}
				Out.AppendChar(':');
				Out.AppendChar(' ');
			}
		}
	}

	const FLogTemplate* Template = Record.GetTemplate();
	if (LIKELY(Template))
	{
		return Template->FormatTo(Out, Record.GetFields().CreateViewIterator());
	}
	FormatDynamicRecordMessageTo(Out, Record);
}

void FLogRecord::FormatMessageTo(FUtf8StringBuilderBase& Out) const
{
	FormatRecordMessageTo(Out, *this);
}

void FLogRecord::FormatMessageTo(FWideStringBuilderBase& Out) const
{
	FormatRecordMessageTo(Out, *this);
}

void FLogRecord::ConvertToCommonLog(FUtf8StringBuilderBase& OutFormat, FCbWriter& OutFields) const
{
	using namespace Logging::Private;

	for (FCbField& Field : Fields)
	{
		OutFields.SetName(Field.GetName());
		if (FCbArray Array = Field.AsArray(); !Field.HasError())
		{
			OutFields.BeginObject();
			OutFields.AddArray(ANSITEXTVIEW("$value"), Array);
			TUtf8StringBuilder<256> Text;
			LogFieldValue(Text, Field);
			OutFields.AddString(ANSITEXTVIEW("$text"), Text);
			OutFields.EndObject();
		}
		else if (FCbObject Object = Field.AsObject(); !Field.HasError() && !Object.FindView(ANSITEXTVIEW("$text")))
		{
			OutFields.BeginObject();
			for (FCbField& Child : Object)
			{
				OutFields.AddField(Child.GetName(), Child);
			}
			TUtf8StringBuilder<256> Text;
			LogFieldValue(Text, Field);
			OutFields.AddString(ANSITEXTVIEW("$text"), Text);
			OutFields.EndObject();
		}
		else
		{
			OutFields.AddField(Field);
		}
	}

	// TODO: Process localized format strings to remove argument modifiers and convert escaped braces.
	if (LIKELY(Format))
	{
		OutFormat.Append(Format);
	}
}

} // UE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::Logging::Private
{

#if NO_LOGGING
FLogCategory<ELogVerbosity::Fatal, ELogVerbosity::Fatal> LogFatal(TEXT("Fatal"));
#endif

thread_local FLogContext* LogContextHead = nullptr;
thread_local FLogContext* LogContextTail = nullptr;

FLogContext::FLogContext(const FLogField& InField)
: Prev(LogContextTail)
{
	(Prev ? Prev->Next : LogContextHead) = this;
	LogContextTail = this;

	const FUtf8StringView Name = InField.Name;

	TCbWriter<256> Writer;
	Writer.SetName(Name);
	if (InField.WriteValue)
	{
		InField.WriteValue(Writer, InField.Value);
	}
	else
	{
		Writer.AddNull();
	}
	Field = Writer.Save();
	Field.MakeOwned();

	for (FLogContext* Node = Prev; Node; Node = Node->Prev)
	{
		if (Node->Field.GetName().Equals(Name))
		{
			Node->bDisabledByNewerContext = true;
			bDisabledOlderContext = true;
		}
	}
}

FLogContext::~FLogContext()
{
	(Prev ? Prev->Next : LogContextHead) = Next;
	(Next ? Next->Prev : LogContextTail) = Prev;

	if (bDisabledOlderContext)
	{
		const FUtf8StringView Name = Field.GetName();
		for (FLogContext* Node = Prev; Node; Node = Node->Prev)
		{
			if (Node->Field.GetName().Equals(Name))
			{
				Node->bDisabledByNewerContext = false;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FLogTemplateFieldIterator
{
public:
	inline explicit FLogTemplateFieldIterator(const FLogTemplate& Template)
		: NextOp(Template.GetOpData())
		, NextFormat(Template.GetFormat())
	{
		++*this;
	}

	FLogTemplateFieldIterator& operator++();
	inline explicit operator bool() const { return !!NextOp; }
	inline const FStringView& GetName() const { return Name; }

private:
	FStringView Name;
	const uint8* NextOp = nullptr;
	const TCHAR* NextFormat = nullptr;
};

FLogTemplateFieldIterator& FLogTemplateFieldIterator::operator++()
{
	using namespace Logging::Private;

	while (NextOp)
	{
		FLogTemplateOp Op = FLogTemplateOp::Load(NextOp);
		if (Op.Code == FLogTemplateOp::OpName)
		{
			Name = FStringView(NextFormat + 1, Op.Value - 2);
			NextFormat += Op.GetSkipSize();
			return *this;
		}
		if (Op.Code == FLogTemplateOp::OpEnd)
		{
			break;
		}
		NextFormat += Op.GetSkipSize();
	}

	NextOp = nullptr;
	Name.Reset();
	return *this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if LOGTRACE_ENABLED
template <typename StaticLogRecordType>
FORCENOINLINE static void EnsureLogMessageSpec(const FLogCategoryBase& Category, const StaticLogRecordType& Log, ELogVerbosity::Type Verbosity)
{
	if (!Log.DynamicData.bInitializedTrace.load(std::memory_order_acquire))
	{
		FLogTrace::OutputLogMessageSpec(&Log, &Category, Verbosity, Log.File, Log.Line, TEXT("%s"));
		Log.DynamicData.bInitializedTrace.store(true, std::memory_order_release);
	}
}

// Tracing the log happens in its own function because that allows stack space for the message to
// be returned before calling into the output devices.
FORCENOINLINE static void LogToTrace(const void* LogPoint, const FLogRecord& Record)
{
	TStringBuilder<1024> Message;
	Record.FormatMessageTo(Message);
	FLogTrace::OutputLogMessage(LogPoint, *Message);
}

// Tracing the log happens in its own function because that allows stack space for the message to
// be returned before calling into the output devices.
FORCENOINLINE static void BasicLogToTrace(const void* LogPoint, const TCHAR* Format, va_list Args)
{
	TStringBuilder<1024> Message;
	Message.AppendV(Format, Args);
	FLogTrace::OutputLogMessage(LogPoint, *Message);
}
#endif // LOGTRACE_ENABLED

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStaticLogTemplateAllocator
{
public:
	~FStaticLogTemplateAllocator()
	{
		for (FBlock* Block = Tail; Block;)
		{
			FBlock* Previous = Block->Previous;
			FMemory::Free(Block);
			Block = Previous;
		}
	}

	void* Allocate(int32 Size)
	{
		TUniqueLock Lock(Mutex);
		if (!Tail || TailOffset + Size > Tail->Size)
		{
			int32 NewSize = (sizeof(FBlock) + Size + BlockSize - 1) & ~(BlockSize - 1);
			Tail = new(FMemory::Malloc(NewSize, alignof(FBlock))) FBlock{Tail, NewSize};
			TailOffset = sizeof(FBlock);
		}
		void* Address = (uint8*)Tail + TailOffset;
		// TODO: Aligned to 8 until unaligned pointer access has been tested on every platform.
		TailOffset += (Size + 7) & ~7;
		return Address;
	}

private:
	inline constexpr static int32 BlockSize = 4096;

	struct FBlock
	{
		FBlock* Previous = nullptr;
		int32 Size = 0;
	};

	FBlock* Tail = nullptr;
	int32 TailOffset = 0;
	FMutex Mutex;
};

static FStaticLogTemplateAllocator& GetStaticLogTemplateAllocator()
{
	static FStaticLogTemplateAllocator Allocator;
	return Allocator;
}

class FStaticLogTemplateStorage
{
public:
	FStaticLogTemplateStorage() = default;
	FStaticLogTemplateStorage(const FStaticLogTemplateStorage&) = delete;
	FStaticLogTemplateStorage& operator=(const FStaticLogTemplateStorage&) = delete;

	inline void* Allocate(int32 Size)
	{
		Data = GetStaticLogTemplateAllocator().Allocate(Size);
		return Data;
	}

	inline const void* Get() const
	{
		return Data;
	}

	inline void* Detach()
	{
		ON_SCOPE_EXIT { Data = nullptr; };
		return Data;
	}

private:
	// Intentionally leaked because the allocator will free the memory on exit.
	void* Data = nullptr;
};

// Serializing log fields to compact binary happens in its own function because that allows stack
// space for the writer to be returned before calling into the output devices.
FORCENOINLINE static FCbObject SerializeLogFields(
	const FLogTemplate& Template,
	const FLogField* Fields,
	const int32 FieldCount)
{
	const bool bHasContext = !!LogContextHead;
	if (FieldCount == 0 && !bHasContext)
	{
		return FCbObject();
	}

	TCbWriter<1024> Writer;
	Writer.BeginObject();

	TArray<FAnsiString, TInlineAllocator<16>> FieldNames;

	// Anonymous. Extract names from Template.
	if (!Fields->Name)
	{
		FLogTemplateFieldIterator It(Template);
		for (const FLogField* FieldsEnd = Fields + FieldCount; Fields != FieldsEnd; ++Fields, ++It)
		{
			check(It);
			const auto Name = StringCast<ANSICHAR>(It.GetName().GetData(), It.GetName().Len());
			const FAnsiStringView NameView(Name.Get(), Name.Length());
			Fields->WriteValue(Writer.SetName(NameView), Fields->Value);
			if (bHasContext)
			{
				FieldNames.Emplace(NameView);
			}
		}
		check(!It);
	}
	// Named
	else
	{
		for (const FLogField* FieldsEnd = Fields + FieldCount; Fields != FieldsEnd; ++Fields)
		{
			Fields->WriteValue(Writer.SetName(Fields->Name), Fields->Value);
			if (bHasContext)
			{
				FieldNames.Emplace(Fields->Name);
			}
		}
	}

	if (bHasContext)
	{
		TBitArray<> ActiveContext;
		int32 ContextIndex = 0;

		// Traverse contexts backward and activate any which have a name that has not been seen yet.
		for (FLogContext* Node = LogContextTail; Node; Node = Node->Prev)
		{
			if (!Node->bDisabledByNewerContext)
			{
				const FUtf8StringView NodeName = Node->Field.GetName();
				ActiveContext.Add(!FieldNames.FindByPredicate([NodeName](FAnsiStringView Name) { return NodeName.Equals(Name); }));
				++ContextIndex;
			}
		}

		// Traverse contexts forward and copy any which were activated above.
		for (FLogContext* Node = LogContextHead; Node; Node = Node->Next)
		{
			if (!Node->bDisabledByNewerContext && ActiveContext[--ContextIndex])
			{
				Writer.AddField(Node->Field.GetName(), Node->Field);
			}
		}

		// Traverse contexts forward and build an array of names in $Contexts.
		Writer.BeginArray(GLogContextsFieldName);
		for (FLogContext* Node = LogContextHead; Node; Node = Node->Next)
		{
			if (!Node->bDisabledByNewerContext)
			{
				Writer.AddString(Node->Field.GetName());
			}
		}
		Writer.EndArray();
	}

	Writer.EndObject();
	return Writer.Save().AsObject();
}

template <typename StaticLogRecordType>
FORCENOINLINE static FLogTemplate& CreateLogTemplate(const FLogCategoryBase& Category, const StaticLogRecordType& Log, const FLogField* Fields, const int32 FieldCount)
{
	for (FLogTemplate* Template = Log.DynamicData.Template.load(std::memory_order_acquire);;)
	{
		if (Template && Template->GetFormat() == Log.Format)
		{
			return *Template;
		}

		struct FTemplateCreator
		{
			static TLogTemplate<FStaticLogTemplateStorage> Create(const FStaticLogRecord& Log, const FLogField* Fields, int32 FieldCount)
			{
				return TLogTemplate<FStaticLogTemplateStorage>(Log.Format, {}, Fields, FieldCount);
			}

			static TLogTemplate<FStaticLogTemplateStorage> Create(const FStaticLocalizedLogRecord& Log, const FLogField* Fields, int32 FieldCount)
			{
				return TLogTemplate<FStaticLogTemplateStorage>(Log.TextNamespace, Log.TextKey, Log.Format, {}, Fields, FieldCount);
			}
		};

		TLogTemplate<FStaticLogTemplateStorage> LocalTemplate = FTemplateCreator::Create(Log, Fields, FieldCount);
		FLogTemplate* NewTemplate = LocalTemplate.Get();
		if (LIKELY(Log.DynamicData.Template.compare_exchange_strong(Template, NewTemplate, std::memory_order_release, std::memory_order_acquire)))
		{
			LocalTemplate.Detach();
			return *NewTemplate;
		}
	}
}

template <typename StaticLogRecordType>
inline static FLogTemplate& EnsureLogTemplate(const FLogCategoryBase& Category, const StaticLogRecordType& Log, const FLogField* Fields, const int32 FieldCount)
{
	// Format can change on a static log record due to Live Coding.
	if (FLogTemplate* Template = Log.DynamicData.Template.load(std::memory_order_acquire); LIKELY(Template && Template->GetFormat() == Log.Format))
	{
		return *Template;
	}
	return CreateLogTemplate(Category, Log, Fields, FieldCount);
}

template <typename StaticLogRecordType>
inline static FLogRecord CreateLogRecord(const FLogCategoryBase& Category, const StaticLogRecordType& Log, const FLogField* Fields, const int32 FieldCount)
{
#if LOGTRACE_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(LogChannel))
	{
		EnsureLogMessageSpec(Category, Log, Log.Verbosity);
	}
#endif

	FLogTemplate& Template = EnsureLogTemplate(Category, Log, Fields, FieldCount);

	FLogRecord Record;
	Record.SetFormat(Log.Format);
	Record.SetTemplate(&Template);
	Record.SetFields(SerializeLogFields(Template, Fields, FieldCount));
	Record.SetFile(Log.File);
	Record.SetLine(Log.Line);
	Record.SetCategory(Category.GetCategoryName());
	Record.SetVerbosity(Log.Verbosity);
	Record.SetTime(FLogTime::Now());
	return Record;
}

inline static void DispatchLogRecord(const FLogRecord& Record)
{
	FOutputDevice* OutputDevice = nullptr;
	switch (Record.GetVerbosity())
	{
	case ELogVerbosity::Error:
	case ELogVerbosity::Warning:
	case ELogVerbosity::Display:
	case ELogVerbosity::SetColor:
		OutputDevice = GWarn;
		break;
	default:
		break;
	}
	(OutputDevice ? OutputDevice : GLog)->SerializeRecord(Record);
}

#if !NO_LOGGING

template <typename StaticLogRecordType>
inline static void DispatchStaticLogRecord(const StaticLogRecordType& Log, const FLogRecord& Record)
{
#if LOGTRACE_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(LogChannel))
	{
		LogToTrace(&Log, Record);
	}
#endif

	DispatchLogRecord(Record);
}

UE_AUTORTFM_ALWAYS_OPEN
void LogWithFieldArray(const FLogCategoryBase& Category, const FStaticLogRecord& Log, const FLogField* Fields, const int32 FieldCount)
{
	DispatchStaticLogRecord(Log, CreateLogRecord(Category, Log, Fields, FieldCount));
}

void LogWithNoFields(const FLogCategoryBase& Category, const FStaticLogRecord& Log)
{
	// A non-null field pointer enables field validation in FLogTemplate::Create.
	static constexpr FLogField EmptyField{};
	LogWithFieldArray(Category, Log, &EmptyField, 0);
}

UE_AUTORTFM_ALWAYS_OPEN
void LogWithFieldArray(const FLogCategoryBase& Category, const FStaticLocalizedLogRecord& Log, const FLogField* Fields, const int32 FieldCount)
{
	FLogRecord Record = CreateLogRecord(Category, Log, Fields, FieldCount);
	Record.SetTextNamespace(Log.TextNamespace);
	Record.SetTextKey(Log.TextKey);
	DispatchStaticLogRecord(Log, Record);
}

void LogWithNoFields(const FLogCategoryBase& Category, const FStaticLocalizedLogRecord& Log)
{
	// A non-null field pointer enables field validation in FLogTemplate::Create.
	static constexpr FLogField EmptyField{};
	LogWithFieldArray(Category, Log, &EmptyField, 0);
}

#endif // !NO_LOGGING

UE_AUTORTFM_ALWAYS_OPEN
[[noreturn]] void FatalLogWithFieldArray(const FLogCategoryBase& Category, const FStaticLogRecord& Log, const FLogField* Fields, const int32 FieldCount) //-V1082
{
	FLogRecord Record = CreateLogRecord(Category, Log, Fields, FieldCount);
	TStringBuilder<512> Message;
	Record.FormatMessageTo(Message);

	StaticFailDebug(TEXT("Fatal error:"), Log.File, Log.Line, PLATFORM_RETURN_ADDRESS(), *Message);

	UE_DEBUG_BREAK_AND_PROMPT_FOR_REMOTE();
	FDebug::ProcessFatalError(PLATFORM_RETURN_ADDRESS());

	for (;;);
}

void FatalLogWithNoFields(const FLogCategoryBase& Category, const FStaticLogRecord& Log)
{
	// A non-null field pointer enables field validation in FLogTemplate::Create.
	static constexpr FLogField EmptyField{};
	FatalLogWithFieldArray(Category, Log, &EmptyField, 0);
}

UE_AUTORTFM_ALWAYS_OPEN
[[noreturn]] void FatalLogWithFieldArray(const FLogCategoryBase& Category, const FStaticLocalizedLogRecord& Log, const FLogField* Fields, const int32 FieldCount) //-V1082
{
	FLogRecord Record = CreateLogRecord(Category, Log, Fields, FieldCount);
	TStringBuilder<512> Message;
	Record.FormatMessageTo(Message);

	StaticFailDebug(TEXT("Fatal error:"), Log.File, Log.Line, PLATFORM_RETURN_ADDRESS(), *Message);

	UE_DEBUG_BREAK_AND_PROMPT_FOR_REMOTE();
	FDebug::ProcessFatalError(PLATFORM_RETURN_ADDRESS());

	for (;;);
}

void FatalLogWithNoFields(const FLogCategoryBase& Category, const FStaticLocalizedLogRecord& Log)
{
	// A non-null field pointer enables field validation in FLogTemplate::Create.
	static constexpr FLogField EmptyField{};
	FatalLogWithFieldArray(Category, Log, &EmptyField, 0);
}

} // UE::Logging::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::Logging::Private
{

static constexpr const TCHAR* const GStaticBasicLogFormat = TEXT("{Message}");

static FLogTemplate* GetStaticBasicLogTemplate()
{
	static FInlineLogTemplate Template(GStaticBasicLogFormat);
	return Template.Get();
}

// Serializing the log to compact binary happens in its own function because that allows stack
// space for the writer to be returned before calling into the output devices.
FORCENOINLINE static FCbObject SerializeBasicLogMessage(const FStaticBasicLogRecord& Log, va_list Args)
{
	TStringBuilder<512> Message;
	Message.AppendV(Log.Format, Args);

	TCbWriter<512> Writer;
	Writer.BeginObject();

	const FUtf8StringView MessageName = UTF8TEXTVIEW("Message");
	Writer.AddString(MessageName, Message);

	if (LogContextHead)
	{
		// Traverse contexts forward and copy any that Message did not override.
		for (FLogContext* Node = LogContextHead; Node; Node = Node->Next)
		{
			const FUtf8StringView Name = Node->Field.GetName();
			if (!Node->bDisabledByNewerContext && !Name.Equals(MessageName))
			{
				Writer.AddField(Name, Node->Field);
			}
		}

		// Traverse contexts forward and build an array of names in $Contexts.
		Writer.BeginArray(GLogContextsFieldName);
		for (FLogContext* Node = LogContextHead; Node; Node = Node->Next)
		{
			if (!Node->bDisabledByNewerContext)
			{
				Writer.AddString(Node->Field.GetName());
			}
		}
		Writer.EndArray();
	}

	Writer.EndObject();
	return Writer.Save().AsObject();
}

UE_AUTORTFM_ALWAYS_OPEN
static void BasicLogV(FStaticBasicLogRecordParam Log, const FLogCategoryBase& Category, ELogVerbosity::Type Verbosity, va_list Args)
{
#if !NO_LOGGING
#if LOGTRACE_ENABLED
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(LogChannel))
	{
		EnsureLogMessageSpec(Category, Log, Verbosity);
	}
#endif

	if (GConvertBasicLogToLogRecord)
	{
		FCbObject Fields = SerializeBasicLogMessage(Log, Args);

		FLogRecord Record;
		Record.SetFormat(GStaticBasicLogFormat);
		Record.SetTemplate(GetStaticBasicLogTemplate());
		Record.SetFields(MoveTemp(Fields));
		Record.SetFile(Log.File);
		Record.SetLine(Log.Line);
		Record.SetCategory(Category.GetCategoryName());
		Record.SetVerbosity(Verbosity);
		Record.SetTime(FLogTime::Now());

		DispatchStaticLogRecord(Log, Record);
	}
	else
	{
	#if LOGTRACE_ENABLED
		if (UE_TRACE_CHANNELEXPR_IS_ENABLED(LogChannel))
		{
			va_list Args2;
			va_copy(Args2, Args);
			BasicLogToTrace(&Log, Log.Format, Args2);
			va_end(Args2);
		}
	#endif
		FMsg::LogV(Log.File, Log.Line, Category.GetCategoryName(), Verbosity, Log.Format, Args);
	}
#endif
}

template <ELogVerbosity::Type Verbosity>
FORCENOINLINE void BasicLog(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...)
{
#if !NO_LOGGING
	va_list Args;
	va_start(Args, Category);
	BasicLogV(Log, *Category, Verbosity, Args);
	va_end(Args);
#endif
}

template CORE_API void BasicLog<ELogVerbosity::Error>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::Warning>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::Display>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::Log>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::Verbose>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::VeryVerbose>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);
template CORE_API void BasicLog<ELogVerbosity::SetColor>(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...);

UE_AUTORTFM_ALWAYS_OPEN
static void BasicFatalLogV(FStaticBasicLogRecordParam Log, void* ProgramCounter, va_list Args)
{
#if !NO_LOGGING
	StaticFailDebugV(TEXT("Fatal error:"), Log.File, Log.Line, ProgramCounter, Log.Format, Args);

	UE_DEBUG_BREAK_AND_PROMPT_FOR_REMOTE();
	FDebug::ProcessFatalError(ProgramCounter);
#endif
}

FORCENOINLINE void BasicFatalLog(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, ...)
{
#if !NO_LOGGING
	va_list Args;
	va_start(Args, Category);
	BasicFatalLogV(Log, PLATFORM_RETURN_ADDRESS(), Args);
	va_end(Args);
#endif
}

FORCENOINLINE void BasicFatalLogWithProgramCounter(FStaticBasicLogRecordParam Log, const FLogCategoryBase* Category, void* ProgramCounter, ...)
{
#if !NO_LOGGING
	va_list Args;
	va_start(Args, ProgramCounter);
	BasicFatalLogV(Log, ProgramCounter, Args);
	va_end(Args);
#endif
}

} // UE::Logging::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{

void DispatchDynamicLogRecord(const FLogRecord& Record)
{
	Logging::Private::DispatchLogRecord(Record);
}

void VisitLogContext(TFunctionRef<void (const FCbField&)> Visitor)
{
	using namespace UE::Logging::Private;
	for (const FLogContext* Node = LogContextHead; Node; Node = Node->Next)
	{
		if (!Node->bDisabledByNewerContext)
		{
			Visitor(Node->Field);
		}
	}
}

} // UE
