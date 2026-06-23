// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/IO/IoStatusError.h"
#include "IO/IoStatus.h"
#include "Containers/AnsiString.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"


UE_DEFINE_ERROR_MODULE(IoStore);

namespace UE::UnifiedError::IoStore
{

	void StripInvalidErrorCodeCharacters(FUtf8String& ErrorCode)
	{
		ErrorCode.ReplaceCharInline(UTF8TEXT(' '), UTF8TEXT('_'));
	}

	class FIoStoreErrorDetails : public UE::UnifiedError::FManditoryErrorDetails, public FRefCountBase
	{
	private:
		FString ErrorMessage;
		mutable FUtf8String CachedErrorName;
		static const FText GenericErrorFormatString;
	public:

		FIoStoreErrorDetails(const FStringView& InErrorMessage)
		{
			ErrorMessage = InErrorMessage;
		}

		virtual FReturnedRefCountValue AddRef() const final { return FRefCountBase::AddRef(); }
		virtual uint32 Release() const final { return FRefCountBase::Release(); }
		virtual uint32 GetRefCount() const final { return FRefCountBase::GetRefCount(); }

		const FUtf8String& GetErrorName(const FError& Error) const
		{
			if (CachedErrorName.IsEmpty())
			{
				CachedErrorName = TCHAR_TO_UTF8(GetIoErrorText(EIoErrorCode(Error.GetErrorCode())));
				StripInvalidErrorCodeCharacters(CachedErrorName);
			}
			return CachedErrorName;
		}

		FAnsiString GetErrorCodeString(const UE::UnifiedError::FError& Error) const override
		{
			return FAnsiString(*GetErrorName(Error));
		}

		FAnsiString GetModuleIdString(const UE::UnifiedError::FError& Error) const override
		{
			return FAnsiString(UE::UnifiedError::IoStore::GetStaticModuleName());
		}

		virtual const FText GetErrorFormatString(const FError& Error) const final override
		{
			return GenericErrorFormatString;
		}

		virtual void SerializeToCb(FCbWriter& Writer, const FError& Error) const override
		{
			Writer.BeginObject();
			if (Error.GetModuleId() == UE::UnifiedError::IoStore::StaticModuleId)
			{
				Writer.AddString(UTF8TEXTVIEW("ErrorCodeString"), GetErrorName(Error));
				Writer.AddString(UTF8TEXTVIEW("ModuleIdString"), UE::UnifiedError::IoStore::GetStaticModuleName());
				Writer.AddInteger(UTF8TEXTVIEW("ErrorCode"), Error.GetErrorCode());
				Writer.AddInteger(UTF8TEXTVIEW("ModuleId"), Error.GetModuleId());
			}
			Writer.AddString(UTF8TEXTVIEW("IoStoreErrorMessage"), ErrorMessage);
			Writer.AddString(UTF8TEXTVIEW("$format"), UTF8TEXTVIEW("{IoStoreErrorMessage}"));
			Writer.AddString(UTF8TEXTVIEW("$type"), UTF8TEXTVIEW("FIoStoreErrorDetails"));
			Writer.EndObject();
		}

		virtual const FAnsiStringView GetErrorDetailsTypeName() const override
		{
			return ANSITEXTVIEW("FIoStoreErrorDetails");
		}
		virtual const FAnsiStringView GetErrorDetailsTypeNameForLog() const override 
		{
			return ANSITEXTVIEW("FIoStoreErrorDetails");
		}
	};

	const FText FIoStoreErrorDetails::GenericErrorFormatString = NSLOCTEXT("IoStore", "GenericErrorMessage", "{FIoStoreErrorDetails}");


	CORE_API UE::UnifiedError::FError ConvertError(const FIoStatus& Status)
	{
		TRefCountPtr<FIoStoreErrorDetails> ErrorDetails = new FIoStoreErrorDetails(Status.GetErrorMessage());
		return FError(UE::UnifiedError::IoStore::StaticModuleId, (int32)(Status.GetErrorCode()), ErrorDetails);
	}
}