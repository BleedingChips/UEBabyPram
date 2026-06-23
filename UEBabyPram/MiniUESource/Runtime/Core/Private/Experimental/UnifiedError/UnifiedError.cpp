// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/UnifiedError/UnifiedError.h"
#include "Containers/AnsiString.h"
#include "Internationalization/Text.h"
#include "Internationalization/Internationalization.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Logging/StructuredLog.h"
#include "Logging/StructuredLogFormat.h"


namespace UE::UnifiedError
{

// FError functions

void FError::PushErrorDetails(TRefCountPtr<IErrorDetails> InErrorDetails)
{
	if (!InErrorDetails.IsValid())
	{
		return;
	}
	InErrorDetails->SetInnerErrorDetails(ErrorDetails);
	ErrorDetails = InErrorDetails;
}

TRefCountPtr<const IErrorDetails> FError::GetInnerMostErrorDetails() const
{
	const IErrorDetails* Result = ErrorDetails;
	while (true)
	{
		if (Result->GetInnerErrorDetails())
		{
			Result = Result->GetInnerErrorDetails();
		}
		else
		{
			return Result;
		}
	}
}

FText FError::GetFormatErrorText() const
{
	return ErrorDetails->GetErrorFormatString(*this);
}

const FManditoryErrorDetails* FError::GetManditoryErrorDetails() const
{


	TRefCountPtr<const IErrorDetails> CurrentIt = ErrorDetails;
	while (CurrentIt != nullptr)
	{
		TRefCountPtr<const IErrorDetails> Next = CurrentIt->GetInnerErrorDetails();
		if (Next == nullptr)
		{
			break;
		}
		CurrentIt = Next;
	}

	if (!ensure(CurrentIt.GetReference()))
	{
		return nullptr;
	}
	return (const FManditoryErrorDetails*)(CurrentIt.GetReference());

}

FAnsiString FError::GetModuleIdAndErrorCodeString() const
{
	return FAnsiString::Printf("%s.%s", *GetModuleIdString(), *GetErrorCodeString());
}


FAnsiString FError::GetErrorCodeString() const
{
	const FManditoryErrorDetails* ManditoryErrorDetails = GetManditoryErrorDetails();
	check(ManditoryErrorDetails);
	return ManditoryErrorDetails->GetErrorCodeString(*this);
}

FAnsiString FError::GetModuleIdString() const
{
	const FManditoryErrorDetails* ManditoryErrorDetails = GetManditoryErrorDetails();
	check(ManditoryErrorDetails);
	return ManditoryErrorDetails->GetModuleIdString(*this);
}



#define USE_LOCALIZED_STRUCTURED_LOG_FOR_FERRORMESSAGE 1



FText FError::GetErrorMessage(bool bIncludeContext) const
{

	EDetailFilter LogDetailFilter = EDetailFilter::IncludeInLogMessage;
	if (bIncludeContext)
	{
		LogDetailFilter |= EDetailFilter::IncludeInContextLogMessage;
	}

	TArray<FText> AdditionalContext;
	AdditionalContext.Add(FText::Join(FText::FromString(": "), NSLOCTEXT("UnifiedError", "DefaultLogPrepend", "{Root/ModuleIdString}.{Root/ErrorCodeString}"), GetFormatErrorText()));

	const IErrorDetails* DetailsIt = ErrorDetails.GetReference();
	while (DetailsIt != nullptr)
	{
		if (DetailsIt->ShouldInclude(EDetailFilter::IncludeInContextLogMessage))
		{
			AdditionalContext.Add(FText::FromString(FUtf8String::Printf("(%s:{%s})", DetailsIt->GetErrorDetailsTypeNameForLog().GetData(), DetailsIt->GetErrorDetailsTypeNameForLog().GetData())));
		}
		const IErrorDetails* Next = DetailsIt->GetInnerErrorDetails().GetReference();
		DetailsIt = Next;
	}
	FText FormatText = FText::Join(FText::FromString(", "), AdditionalContext);

#if USE_LOCALIZED_STRUCTURED_LOG_FOR_FERRORMESSAGE
	UE::FLogTemplateOptions TemplateOptions;
	TemplateOptions.bAllowSubObjectReferences = true;

	FInlineLogTemplate Template(FormatText, TemplateOptions);
#else
	FString FormatString = FormatText.ToString();
	UE::FLogTemplateOptions TemplateOptions;
	TemplateOptions.bAllowSubObjectReferences = true;
	FInlineLogTemplate Template(*FormatString, TemplateOptions);
#endif
	FCbWriter Writer;
	SerializeDetails(Writer, EDetailFilter::IncludeInSerialize);

	TArray<char, TInlineAllocator<1024>> ScratchBuffer;
	ScratchBuffer.AddUninitialized((int32)Writer.GetSaveSize());

	return Template.FormatToText(Writer.Save(MakeMemoryView(ScratchBuffer)));
}

void FError::SerializeDetails(FCbWriter& Writer, const EDetailFilter DetailFilter, bool bIncludeRoot) const
{
	const IErrorDetails* DetailsIt = ErrorDetails.GetReference();
	while (DetailsIt != nullptr)
	{
		if (DetailsIt->ShouldInclude(DetailFilter))
		{
			Writer.SetName(DetailsIt->GetErrorDetailsTypeNameForLog());
			DetailsIt->SerializeToCb(Writer, *this);
		}
		const IErrorDetails* Next = DetailsIt->GetInnerErrorDetails().GetReference();
		if (Next == nullptr && bIncludeRoot)
		{
			// the last one contains some special information we want to reference by name
			Writer.SetName(ANSITEXTVIEW("Root"));
			DetailsIt->SerializeToCb(Writer, *this);
		}
		DetailsIt = Next;
	}
}

FString FError::SerializeToJsonString(const EDetailFilter DetailFilter) const
{
	FCbWriter Writer;
	// Writer.SetName(ANSITEXTVIEW("FError"));
	Writer.BeginObject();
	SerializeDetails(Writer, DetailFilter);
	Writer.EndObject();

	// SerializeBasicToCb(Writer, UTF8TEXTVIEW("AppendFormatString"), AppendFormatString);

	TStringBuilder<256> Text;
	CompactBinaryToJson(Writer.Save(), Text);
	return Text.ToString();
}


FString FError::SerializeToJsonForAnalytics() const
{
	return SerializeToJsonString(EDetailFilter::IncludeInAnalytics);
}

int32 FError::GetErrorCode() const
{
	return ErrorCode;
}

int32 FError::GetModuleId() const
{
	return ModuleId;
}

class FAppendFormatStringDetails : public FDynamicErrorDetails
{
	UE_DECLARE_FERROR_DETAILS(UnifiedError, FAppendFormatStringDetails);
public:
	FAppendFormatStringDetails() {}
	FAppendFormatStringDetails(FText&& InAppendFormatString, TRefCountPtr<const IErrorDetails> InInnerErrorDetails = nullptr) : FDynamicErrorDetails(InInnerErrorDetails)
	{
		AppendFormatString = MoveTemp(InAppendFormatString);
	}

private:
	FText AppendFormatString;


public:

	virtual void SerializeToCb(FCbWriter& Writer, const FError& Error) const override
	{
		Writer.BeginObject();
		SerializeBasicToCb(Writer, UTF8TEXTVIEW("AppendFormatString"), AppendFormatString);
		Writer.EndObject();
	}

	CORE_API virtual const FText GetErrorFormatString(const FError& Error) const override
	{
		const FText InnerFormatString = GetInnerErrorDetails()->GetErrorFormatString(Error);
		return FText::Join(FText::FromString(" "), InnerFormatString, AppendFormatString);
	}

	virtual bool ShouldInclude(const EDetailFilter Filter) const { return EnumHasAnyFlags(Filter, EDetailFilter::Default & ~EDetailFilter::IncludeInContextLogMessage); }
};



void FError::AppendFormatString(FText&& InFormatString)
{
	TRefCountPtr<IErrorDetails> NewErrorDetails(new FAppendFormatStringDetails(MoveTemp(InFormatString)));
	PushErrorDetails(NewErrorDetails);
}


void SerializeForLog(FCbWriter& Writer, const FError& Error)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), ANSITEXTVIEW("FError"));

	// what I have to do
	TCbWriter<1024> InnerWriter;

	Error.SerializeDetails(InnerWriter, EDetailFilter::IncludeInSerialize);

	FText FormatText = FText::Join(FText::FromString(":"), NSLOCTEXT("UnifiedError", "DefaultLogPrepend", "{Root/ModuleIdString}.{Root/ErrorCodeString}"), Error.GetFormatErrorText());
#if USE_LOCALIZED_STRUCTURED_LOG_FOR_FERRORMESSAGE
	UE::FLogTemplateOptions TemplateOptions;
	TemplateOptions.bAllowSubObjectReferences = true;
	FInlineLogTemplate Template(FormatText, TemplateOptions);
#else
	UE::FLogTemplateOptions TemplateOptions;
	TemplateOptions.bAllowSubObjectReferences = true;
	FInlineLogTemplate Template(*FormatText.ToString(), TemplateOptions);
#endif
	FUtf8StringBuilderBase OutputMessage;


	FCbFieldIterator CbInnerIterator = InnerWriter.Save();

	Template.FormatTo(OutputMessage, CbInnerIterator);

	Writer.AddString(ANSITEXTVIEW("$text"), OutputMessage.ToString());

	Writer.EndObject();
}

// FRefCountedErrorDetails functions
FRefCountedErrorDetails::~FRefCountedErrorDetails()
{
}

// FDynamicErrorDetails functions

FDynamicErrorDetails::FDynamicErrorDetails(TRefCountPtr<const IErrorDetails> InInnerErrorDetails)
{
	InnerErrorDetails = InInnerErrorDetails;
}
FDynamicErrorDetails::~FDynamicErrorDetails()
{
}

const FText FDynamicErrorDetails::GetErrorFormatString(const FError & Error) const
{
	check(InnerErrorDetails);
	return InnerErrorDetails->GetErrorFormatString(Error);
}


void FDynamicErrorDetails::SetInnerErrorDetails(TRefCountPtr<const IErrorDetails> InInnerErrorDetails)
{
	InnerErrorDetails = InInnerErrorDetails;
}

// FStaticErrorDetails functions

FStaticErrorDetails::FStaticErrorDetails(const FAnsiStringView InErrorName, const FAnsiStringView InModuleName, const FText& InErrorFormatString) : ErrorName(InErrorName), ModuleName(InModuleName), ErrorFormatString(InErrorFormatString)
{
}

const FText FStaticErrorDetails::GetErrorFormatString(const FError& Error) const
{
	return ErrorFormatString;
}

FAnsiString FStaticErrorDetails::GetErrorCodeString(const FError& Error) const
{
	return FAnsiString(ErrorName);
}

FAnsiString FStaticErrorDetails::GetModuleIdString(const FError& Error) const
{
	return FAnsiString(ModuleName);
}

// FErrorDetailsRegistry
uint32 FErrorDetailsRegistry::RegisterDetails(const FAnsiStringView& ErrorDetailsName, TFunction<IErrorDetails*()> CreationFunction)
{
	uint32 DetailsId = FCrc::StrCrc32<FAnsiStringView::ElementType>(ErrorDetailsName.GetData());
	CreateFunctions.Add(DetailsId, CreationFunction);
	return DetailsId;
}

// ErrorRegistry functionality

namespace ErrorRegistry
{
	class FErrorRegistry
	{
	public:
		FErrorRegistry() {}

	public:
		uint32 RegisterModule(const FAnsiStringView ModuleName);

		int32 RegisterErrorCode(const FAnsiStringView ErrorName, int32 ModuleId, int32 ErrorCode);

	private:
		TMap<int32, FAnsiString> ModuleNameMap;
		TMap<TPair<int32, int32>, FAnsiString> ErrorCodeNameMap;
	};


	uint32 FErrorRegistry::RegisterModule(const FAnsiStringView ModuleName)
	{
		// todo: need to replace this with a stable hashing function
		uint32 ModuleId = GetTypeHash(ModuleName);
		checkf(ModuleNameMap.Contains(ModuleId) == false, TEXT("Module %s and %s are trying to register under module id %d"), ANSI_TO_TCHAR(ModuleName.GetData()), ANSI_TO_TCHAR(*ModuleNameMap.FindRef(ModuleId)), ModuleId);
		ModuleNameMap.Add(ModuleId, ModuleName.GetData());
		return ModuleId;
	}

	int32 FErrorRegistry::RegisterErrorCode(const FAnsiStringView ErrorName, int32 ModuleId, int32 ErrorCode)
	{
		TPair<int32, int32> CombinedErrorId(ModuleId, ErrorCode);
		checkf(ErrorCodeNameMap.Contains(CombinedErrorId) == false, TEXT("Error %s and %s are trying to register under same error code moduleid:%d errorcode:%d"), ANSI_TO_TCHAR(ErrorName.GetData()), ANSI_TO_TCHAR(*ErrorCodeNameMap.FindRef(CombinedErrorId)), ModuleId, ErrorCode);
		ErrorCodeNameMap.Add(CombinedErrorId, ErrorName.GetData());
		return ErrorCode;
	}

	FErrorRegistry GErrorRegistry;

	CORE_API uint32 RegisterModule(const FAnsiStringView ModuleName)
	{
		return GetTypeHash(ModuleName);
		//return GErrorRegistry.RegisterModule(ModuleName);
	}

	CORE_API int32 RegisterErrorCode(const FAnsiStringView ErrorName, int32 ModuleId, int32 ErrorCode)
	{
		return ErrorCode;
		// return GErrorRegistry.RegisterErrorCode(ErrorName, ModuleId, ErrorCode);
	}
}




} // namespace UE::Error


FString LexToString(const UE::UnifiedError::FError& Error)
{
	// TODO: daniel, switch to using structured logging output in the future instead of this
	return Error.GetErrorMessage().ToString();
}

