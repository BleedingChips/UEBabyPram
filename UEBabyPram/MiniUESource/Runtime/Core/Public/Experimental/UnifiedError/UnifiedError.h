// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Containers/Utf8String.h"
#include "Containers/StringView.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Logging/StructuredLogFormat.h"
#include "Misc/TVariant.h"
#include "Templates/RefCounting.h"


#include <type_traits>

template<typename>
struct deferred_false : std::false_type {};


template<typename T>
class TErrorStructFeatures
{
public:
	static const FAnsiStringView GetErrorDetailsLogEntryNameAsString()
	{
		static_assert(deferred_false<T>::value, "All types are required to define DECLARE_ERRORSTRUCT_FEATURES to be used as a error context type");
		return ANSITEXTVIEW("");
	}
	static const FAnsiStringView GetErrorDetailsTypeNameAsString()
	{
		static_assert(deferred_false<T>::value, "All types are required to define DECLARE_ERRORSTRUCT_FEATURES to be used as a error context type");
		return ANSITEXTVIEW("");
	}

	static const FAnsiStringView GetErrorContextTypeNameAsString()
	{
		static_assert(deferred_false<T>::value, "All types are required to define DECLARE_ERRORSTRUCT_FEATURES to be used as a error context type");
		return ANSITEXTVIEW("");
	}
};



#define UE_DECLARE_ERRORSTRUCT_FEATURES(DetailsNameSpace, DetailsStructName)\
	template<>\
	class TErrorStructFeatures<UE::UnifiedError::DetailsNameSpace::DetailsStructName>\
	{\
	public:\
		static const FAnsiStringView GetErrorDetailsLogEntryNameAsString() \
		{\
			return ANSITEXTVIEW(#DetailsNameSpace"_"#DetailsStructName);\
		}\
		static const FAnsiStringView GetErrorDetailsTypeNameAsString() \
		{\
			return ANSITEXTVIEW("TErrorDetails<"#DetailsNameSpace"::"#DetailsStructName">");\
		}\
		static const FAnsiStringView GetErrorContextTypeNameAsString() \
		{\
			return ANSITEXTVIEW(#DetailsNameSpace"::"#DetailsStructName);\
		}\
	};



template <unsigned int N>
struct TStringLiteralWithColonsReplaced
{
	constexpr TStringLiteralWithColonsReplaced(const char(&InStr)[N])
	{
		int OutIndex = 0;
		int i = 0;
		for (; i < N; ++i)
		{
			if (InStr[i] == '\0')
			{
				Str[OutIndex] = InStr[i];
				break;
			}
			else if (InStr[i] == ':' && InStr[i + 1] == ':')
			{
				Str[OutIndex] = '_';
				++OutIndex;
			}
			else if (InStr[i] != ':')
			{
				Str[OutIndex] = InStr[i];
				++OutIndex;
			}
		}
		for (; OutIndex < N; ++OutIndex)
		{
			Str[OutIndex] = '\0';
		}
	}

	char Str[N];
};


template <unsigned int N>
struct TStringLiteralWithSpacesReplaced
{
	constexpr TStringLiteralWithSpacesReplaced(const char(&InStr)[N])
	{
		int OutIndex = 0;
		int i = 0;
		for (; i < N; ++i)
		{
			if (InStr[i] == '\0')
			{
				Str[OutIndex] = InStr[i];
				break;
			}
			else if (InStr[i] == ' ')
			{
				Str[OutIndex] = '_';
				++OutIndex;
			}
			else 
			{
				Str[OutIndex] = InStr[i];
				++OutIndex;
			}
		}
		for (; OutIndex < N; ++OutIndex)
		{
			Str[OutIndex] = '\0';
		}
	}

	char Str[N];
};

constexpr uint64 CompileTimeHashString(const char* String)
{
	// Based on djb2
	uint64 Result = 5381;
	while (char Ch = *String++)
	{
		Result = ((Result << 5) + Result) + Ch;
	}
	return Result;
}

// TODO Daniel: StaticDetailsTypeId is intended to register the creation functions with the FErrorDetailsRegistry for support of serialization / deserialization of error details

#define UE_DECLARE_ERROR_DETAILS_INTERNAL(DetailsNamespace, TypeName)\
	inline static constexpr FAnsiStringView StaticGetErrorDetailsTypeName() \
	{\
		return #DetailsNamespace"::"#TypeName;\
	}\
	virtual const FAnsiStringView GetErrorDetailsTypeName() const override \
	{\
		return StaticGetErrorDetailsTypeName();\
	}\
	inline static FAnsiStringView StaticGetErrorDetailsTypeNameForLog() \
	{\
		static auto Replaced = TStringLiteralWithColonsReplaced(#DetailsNamespace"_"#TypeName);\
		return Replaced.Str;\
	}\
	virtual const FAnsiStringView GetErrorDetailsTypeNameForLog() const override\
	{\
		return StaticGetErrorDetailsTypeNameForLog(); \
	}\
	virtual uint64 GetErrorDetailsTypeId() const \
	{ \
		return StaticGetErrorDetailsTypeId(); \
	}\
	static uint64 StaticGetErrorDetailsTypeId() \
	{ \
		return CompileTimeHashString(StaticGetErrorDetailsTypeName().GetData()); \
	}


#define UE_DECLARE_FERROR_DETAILS_ABSTRACT(DetailsNamespace, TypeName)\
	public:\
	UE_DECLARE_ERROR_DETAILS_INTERNAL(DetailsNamespace, TypeName);
	
	// inline const uint32 StaticDetailsTypeId = UE::UnifiedError::FErrorDetailsRegistry::Get().RegisterDetails(StaticGetErrorDetailsTypeName(), nullptr);
	
	

#define UE_DECLARE_FERROR_DETAILS(DetailsNamespace, TypeName)\
	public:\
	UE_DECLARE_ERROR_DETAILS_INTERNAL(DetailsNamespace, TypeName);\
	friend IErrorDetails* Create();\
	inline static IErrorDetails* Create()\
	{\
		return new TypeName();\
	}

	// inline const uint32 StaticDetailsTypeId = UE::UnifiedError::FErrorDetailsRegistry::Get().RegisterDetails(StaticGetErrorDetailsTypeName(), TFunction<IErrorDetails* ()>([]() -> IErrorDetails* { return TypeName::Create(); }));
	



namespace UE::UnifiedError
{

	enum class EDetailFilter : uint8
	{
		IncludeInSerialize = 1 << 0, // included when we serialize the details objects, almost every details should have this flag
		IncludeInAnalytics = 1 << 1, // what details objects to include when we are creating events for analytics
		IncludeInContextLogMessage = 1 << 2, // what details objects are included when the log message includes context objects on the string
		IncludeInLogMessage = 1 << 3, // which details objects are included when we log a message without context objects included
		// append here


		// standard values
		Default = IncludeInSerialize | IncludeInContextLogMessage,
		None = 0x00,
		All = 0xff
	};
	ENUM_CLASS_FLAGS(EDetailFilter);

	namespace ErrorRegistry
	{
		CORE_API uint32 RegisterModule(const FAnsiStringView ModuleName);

		CORE_API int32 RegisterErrorCode(const FAnsiStringView ErrorName, int32 ModuleId, int32 ErrorCode);
	}


	class FError;

	class IErrorDetails : public IRefCountedObject
	{
	public:
		virtual ~IErrorDetails() = default;

		/// <summary>
		/// GetErrorFormatString; specifies the default error format string to be used when generating FError::GetErrorMessage.
		///	 The format string can specify any property exposed by any encapsulated IErrorDetails::GetErrorProperties.
		///  Example: GetErrorProperties adds Name:"ModuleId" Value:10. GetErrorFormatString returns "Module id was {ModuleId}".  Result "Module id was 10".
		/// </summary>
		/// <param name="Error"></param>
		/// <returns></returns>
		virtual const FText GetErrorFormatString(const FError& Error) const = 0;


		/// <summary>
		/// GetInnerErrorDetails; Exposes inner error details to FError, if this ErrorDetails allows inner details
		/// </summary>
		/// <returns></returns>
		virtual TRefCountPtr<const IErrorDetails> GetInnerErrorDetails() const { return nullptr; }

		/// <summary>
		/// SetInnerErrorDetails; Exposes inner error details to FError, if this ErrorDetails allows inner details
		/// </summary>
		/// <returns></returns>
		virtual void SetInnerErrorDetails(TRefCountPtr<const IErrorDetails> ErrorDetails) { checkf(false, TEXT("SetInnerErrorDetails not implemented!")); }

		/// <summary>
		/// GetErrorDetialsTypeId; Simple type information for error details, generated using hash of details name
		///  See also: #define FERROR_DETAILS
		/// </summary>
		/// <returns></returns>
		virtual uint64 GetErrorDetailsTypeId() const = 0;

		virtual const FAnsiStringView GetErrorDetailsTypeName() const = 0;
		virtual const FAnsiStringView GetErrorDetailsTypeNameForLog() const = 0;
		virtual void SerializeToCb(FCbWriter& Writer, const FError& Error) const =0;

		virtual bool ShouldInclude(const EDetailFilter DetailFilter) const { return EnumHasAnyFlags( EDetailFilter::Default, DetailFilter); }
	};


	/// <summary>
	/// FManditoryErrorDetails 
	/// every FError needs to be initialized with one of these, it contains core information about the error including errorcodestring and module name
	/// </summary>
	class FManditoryErrorDetails : public IErrorDetails
	{
		UE_DECLARE_FERROR_DETAILS_ABSTRACT(UnifiedError, FManditoryErrorDetails);
	private:
	public:
		virtual FAnsiString GetErrorCodeString(const FError& Error) const = 0;
		virtual FAnsiString GetModuleIdString(const FError& Error) const = 0;
	};

	class FErrorDetailsRegistry
	{
	private:
		TMap<uint32, TFunction<IErrorDetails* ()>> CreateFunctions;

		FErrorDetailsRegistry() { }
	public:
		static FErrorDetailsRegistry& Get()
		{
			static FErrorDetailsRegistry Registry;
			return Registry;
		}

		CORE_API uint32 RegisterDetails(const FAnsiStringView& ErrorDetailsName, TFunction<IErrorDetails*()> CreationFunction);
	};


	/// <summary>
	/// FRefCountedErrorDetails; base implementation of refcounting for IErrorDetails, this is used for heap allocated IErrorDetails implementations
	/// </summary>
	class FRefCountedErrorDetails : public IErrorDetails, public FRefCountBase
	{
		UE_DECLARE_FERROR_DETAILS_ABSTRACT(UnifiedError, FRefCountedErrorDetails);


		CORE_API virtual ~FRefCountedErrorDetails();

		virtual FReturnedRefCountValue AddRef() const final { return FRefCountBase::AddRef(); }
		virtual uint32 Release() const final { return FRefCountBase::Release(); }
		virtual uint32 GetRefCount() const final { return FRefCountBase::GetRefCount(); }
	};


	/// <summary>
	/// FDynamicErrorDetails; base implementation of inner error details, for use by derived classes to reduce unnessisary reimplementation 
	/// </summary>
	class FDynamicErrorDetails : public FRefCountedErrorDetails
	{
		UE_DECLARE_FERROR_DETAILS_ABSTRACT(UnifiedError, FDynamicErrorDetails);
	private:
		TRefCountPtr<const IErrorDetails> InnerErrorDetails;
	public:
		CORE_API FDynamicErrorDetails(TRefCountPtr<const IErrorDetails> InInnerErrorDetails = nullptr);
		CORE_API virtual ~FDynamicErrorDetails();


		// IErrorDetails functions
		CORE_API virtual void SetInnerErrorDetails(TRefCountPtr<const IErrorDetails> InInnerErrorDetails);
		virtual TRefCountPtr<const IErrorDetails> GetInnerErrorDetails() const { return InnerErrorDetails; }

		/// <summary>
		/// GetErrorFormatString; Pass through to the InnerErrorDetails.
		/// </summary>
		/// <param name="Error"></param>
		/// <returns></returns>
		CORE_API virtual const FText GetErrorFormatString(const FError& Error) const override;

		virtual bool ShouldInclude(const EDetailFilter DetailFilter) const override { return EnumHasAnyFlags(DetailFilter, ~EDetailFilter::IncludeInAnalytics); }
	};

	CORE_API void SerializeForLog(FCbWriter& Writer, const class FError& Error);

	template<typename T>
	class TErrorDetails : public FDynamicErrorDetails
	{
	public:
		TErrorDetails() {}
		TErrorDetails(T&& InErrorDetail, const EDetailFilter InDetailFilterMask = EDetailFilter::Default)
		{
			ErrorDetail = MoveTemp(InErrorDetail);
			DetailFilterMask = InDetailFilterMask;
		}
		/*TErrorDetails(T&& InErrorDetail, TRefCountPtr<const IErrorDetails> InInnerErrorDetails) : FDynamicErrorDetails(InInnerErrorDetails)
		{
			ErrorDetail = MoveTemp(InErrorDetail);
		}*/

		friend IErrorDetails* Create();
		inline static IErrorDetails* Create()
		{
			return new TErrorDetails<T>();
		}

		static uint64 StaticGetErrorDetailsTypeId()
		{
			return CompileTimeHashString(TErrorStructFeatures<T>::GetErrorDetailsTypeNameAsString().GetData());
		}
		virtual uint64 GetErrorDetailsTypeId() const
		{
			return StaticGetErrorDetailsTypeId();
		}

		/*inline static const uint32 StaticDetailsTypeId = UE::UnifiedError::FErrorDetailsRegistry::Get().RegisterDetails(TErrorStructFeatures<T>::GetErrorDetailsTypeNameAsString(), TFunction<IErrorDetails * ()>([]() -> IErrorDetails* { return Create(); }));
		static uint32 StaticGetErrorDetailsTypeId()
		{
			return StaticDetailsTypeId;
		}
		virtual uint32 GetErrorDetailsTypeId() const
		{
			return StaticGetErrorDetailsTypeId();
		}*/


		static FAnsiStringView StaticGetErrorDetailsTypeName()
		{
			return TErrorStructFeatures<T>::GetErrorDetailsTypeNameAsString();
		}
		const FAnsiStringView GetErrorDetailsTypeName() const override
		{
			return TErrorStructFeatures<T>::GetErrorDetailsTypeNameAsString();
		}

		const FAnsiStringView GetErrorDetailsTypeNameForLog() const override
		{
			return TErrorStructFeatures<T>::GetErrorDetailsLogEntryNameAsString();
		}

		virtual bool ShouldInclude(const EDetailFilter InDetailFilter) const override { return EnumHasAnyFlags(InDetailFilter, DetailFilterMask); }

	private:
		T ErrorDetail;
		EDetailFilter DetailFilterMask = EDetailFilter::Default;
		
	public:
		const T& GetErrorContext() const
		{
			return ErrorDetail;
		}

		virtual void SerializeToCb(FCbWriter& Writer, const FError& Error) const override
		{
			SerializeForLog(Writer, ErrorDetail);
		}

		const T& GetValue() const
		{
			return ErrorDetail;
		}

	};

	class FError
	{
	private: 
		int32 ErrorCode;
		int32 ModuleId;
		TRefCountPtr<const IErrorDetails> ErrorDetails;
	public:
		FError(int32 InModuleId, int32 InErrorCode, const FManditoryErrorDetails* InErrorDetails)
		{
			ErrorCode = InErrorCode;
			ModuleId = InModuleId;
			ErrorDetails = InErrorDetails;
		}
	public:
		FError(FError&& InError)
		{
			ErrorCode = InError.ErrorCode;
			InError.ErrorCode = 0;
			ModuleId = InError.ModuleId;
			InError.ModuleId = 0;
			if (InError.ErrorDetails)
			{
				ErrorDetails = InError.ErrorDetails;
				InError.ErrorDetails = nullptr;
			}
		}

		FError(const FError& InError)
		{
			ErrorCode = InError.ErrorCode;
			ModuleId = InError.ModuleId;
			ErrorDetails = InError.ErrorDetails;
		}

		~FError()
		{
			Invalidate();
		}


		CORE_API int32 GetErrorCode() const;
		CORE_API int32 GetModuleId() const;

		CORE_API FText GetErrorMessage(bool bAppendContext = false) const;
		CORE_API FText GetFormatErrorText() const;

		template<typename T>
		void PushErrorContext(T&& InErrorStruct, const EDetailFilter& InDetailFilter = EDetailFilter::Default)
		{
			PushErrorDetails(TRefCountPtr<IErrorDetails>(new TErrorDetails<T>(MoveTemp(InErrorStruct), InDetailFilter)));
		}

		template<typename T>
		const T* GetErrorContext() const
		{

			TRefCountPtr<const TErrorDetails<T>> CurrentDetails = GetErrorDetails<const TErrorDetails<T>>();
			if (const TErrorDetails<T>* Details = CurrentDetails.GetReference())
			{
				return &Details->GetErrorContext();
			}
			return nullptr;

		}

		CORE_API FAnsiString GetErrorCodeString() const;
		CORE_API FAnsiString GetModuleIdString() const;


		/// <summary>
		/// GetFullErrorCodeString, Return the combined module id and error code
		/// </summary>
		/// <returns></returns>
		CORE_API FAnsiString GetModuleIdAndErrorCodeString() const;

		CORE_API void AppendFormatString(FText&& InFormatString);

		CORE_API FString SerializeToJsonForAnalytics() const;

		UE_FORCEINLINE_HINT bool IsValid() const
		{
			return (ErrorCode != 0) && (ModuleId != 0);
		}

		inline void Invalidate()
		{
			ErrorCode = 0;
			ModuleId = 0;
			ErrorDetails = nullptr;
		}
	private:

		friend CORE_API void SerializeForLog(FCbWriter& Writer, const FError& Error);

		void SerializeDetails(FCbWriter& Writer, const EDetailFilter DetailFilter, bool bIncludeRoot = true) const;
		FString SerializeToJsonString(const EDetailFilter DetailFilter) const;

		const FManditoryErrorDetails* GetManditoryErrorDetails() const;

		CORE_API void PushErrorDetails(TRefCountPtr<IErrorDetails> InErrorDetails);
		template<typename DetailType>
		TRefCountPtr<const DetailType> GetErrorDetails() const
		{
			TRefCountPtr<const IErrorDetails> CurrentIt = ErrorDetails;
			while (CurrentIt != nullptr)
			{
				if (CurrentIt->GetErrorDetailsTypeId() == DetailType::StaticGetErrorDetailsTypeId())
				{
					return TRefCountPtr<const DetailType>((const DetailType*)CurrentIt.GetReference());
				}
				CurrentIt = CurrentIt->GetInnerErrorDetails();
			}
			return nullptr;
		}

		friend bool operator==(const FError& Error, const FError& OtherError)
		{
			if ((Error.ModuleId == OtherError.ModuleId) &&
				(Error.ErrorCode == OtherError.ErrorCode))
			{
				return true;
			}
			return false;
		}

		TRefCountPtr<const IErrorDetails> GetInnerMostErrorDetails() const;
	};




	/// <summary>
	/// FStaticErrorDetails; static error details and members are statically allocated
	///  Every error which uses DEFINE_ERROR will have FStaticErrorDetails generated for it
	///  Can not rely on it to be available for every error as some Error conversion functions will not use pregenerated errors or error codesF
	///  Use FError::GetErrorDetails to discover FStaticErrorDetails
	/// </summary>
	class FStaticErrorDetails : public FManditoryErrorDetails
	{
		UE_DECLARE_FERROR_DETAILS_ABSTRACT(UnifiedError, FStaticErrorDetails);
	private:
		const FAnsiStringView ErrorName;
		const FAnsiStringView ModuleName;
		FText ErrorFormatString;
	public:
		// TODO: convert AnsiStringView to UTF8StringView when c++ 20 is supported
		CORE_API FStaticErrorDetails(const FAnsiStringView InErrorName, const FAnsiStringView InModuleName, const FText& InErrorFormatString);

		virtual ~FStaticErrorDetails() = default;

		/// <summary>
		/// GetErrorCodeString; Accessor for ErrorName.  
		///  Can be called directly on FStaticErrorDetails object.  
		///  See also: FError::GetErrorDetails
		/// </summary>
		/// <returns></returns>
		CORE_API virtual FAnsiString GetErrorCodeString(const FError& Error) const override;


		/// <summary>
		/// GetModuleIdString; accessor for ModuleName.
		///  Can be called directly on FStaticErrorDetails object
		///  See also: FError::GetErrorDetails
		/// </summary>
		CORE_API virtual FAnsiString GetModuleIdString(const FError& Error) const override;

		// IErrorDetails implementation

		/// <summary>
		/// GetErrorFormatString; return the localized format text generated in DECLARE_ERROR macro.
		/// </summary>
		/// <param name="Error"></param>
		/// <returns></returns>
		CORE_API virtual const FText GetErrorFormatString(const FError& Error) const final;

		virtual void SerializeToCb(FCbWriter& Writer, const FError& Error) const final
		{
			Writer.BeginObject();
			Writer.AddString(UTF8TEXTVIEW("$format"), UTF8TEXTVIEW("({ModuleIdString}.{ErrorCodeString})"));
			Writer.AddString(UTF8TEXTVIEW("$type"), UTF8TEXTVIEW("FStaticErrorDetails"));
			Writer.AddString(UTF8TEXTVIEW("ErrorCodeString"), ErrorName);
			Writer.AddString(UTF8TEXTVIEW("ModuleIdString"), ModuleName);
			Writer.AddInteger(UTF8TEXTVIEW("ErrorCode"), Error.GetErrorCode());
			Writer.AddInteger(UTF8TEXTVIEW("ModuleId"), Error.GetModuleId());
			Writer.EndObject();
		}

		// IRefCountedObject implementation
		//  FStaticErrorDetails is statically allocated, make sure it is never released.
		virtual FReturnedRefCountValue AddRef() const final { return FReturnedRefCountValue(10); }
		virtual uint32 Release() const final { return 10; }
		virtual uint32 GetRefCount() const final { return 10; }

		
		virtual bool ShouldInclude(const EDetailFilter Filter) const override { return EnumHasAnyFlags(Filter, EDetailFilter::Default); }
	};


	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const FStringView& Value)
	{
		Writer.AddString(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const FUtf8StringView& Value)
	{
		Writer.AddString(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const int32& Value)
	{
		Writer.AddInteger(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const uint32& Value)
	{
		Writer.AddInteger(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const int64& Value)
	{
		Writer.AddInteger(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const uint64& Value)
	{
		Writer.AddInteger(PropertyName, Value);
	}
	inline void SerializeBasicToCb(FCbWriter& Writer, const FUtf8StringView& PropertyName, const FText& Value)
	{
		Writer.AddString(PropertyName, Value.ToString());
	}

	

} // namespace UnifiedError



CORE_API FString LexToString(const UE::UnifiedError::FError& Error);
/*

CORE_API constexpr uint32 GetTypeIdHash(const TCHAR* TypeName)
{
	return FCrc::StrCrc32(GetData(S));
}
*/


#define UE_DECLARE_ERROR_MODULE(DeclareApi, ModuleName) \
	namespace UE::UnifiedError { namespace ModuleName {\
		DeclareApi FAnsiStringView GetStaticModuleName(); \
		DeclareApi int32 GetStaticModuleId(); \
	}}

#define UE_DEFINE_ERROR_MODULE(ModuleName) \
	namespace UE::UnifiedError { namespace ModuleName {\
		const int32 StaticModuleId = UE::UnifiedError::ErrorRegistry::RegisterModule(ANSITEXTVIEW(#ModuleName));\
		int32 GetStaticModuleId() { return StaticModuleId; }\
		FAnsiStringView GetStaticModuleName() \
		{ \
			static const auto Reference = TStringLiteralWithSpacesReplaced(#ModuleName); \
			return Reference.Str; \
		}\
	}}


#define UE_DECLARE_ERROR_INTERNAL(DeclareApi, ErrorName, ErrorCode, ModuleName, FormatString) \
	namespace UE::UnifiedError { namespace ModuleName { namespace ErrorName {\
			DeclareApi FAnsiStringView GetStaticErrorName(); \
			static constexpr int32 DeclaredErrorCode = ErrorCode; \
			extern DeclareApi const FText DeclaredFormatString; \
			DeclareApi int32 GetErrorCodeId(); \
			DeclareApi TRefCountPtr<const FStaticErrorDetails> GetStaticErrorDetails(); \
			DeclareApi const FError& GetStaticError(); \
			DeclareApi bool OfType(const FError& Other); \
			static inline FText GetFormatString() { return FormatString; } \
	}}}

#define UE_DEFINE_ERROR(ErrorName, ModuleName) \
	namespace UE::UnifiedError { namespace ModuleName { namespace ErrorName {\
			const int32 ErrorCodeId = UE::UnifiedError::ErrorRegistry::RegisterErrorCode(GetStaticErrorName(), UE::UnifiedError::ModuleName::GetStaticModuleId(), DeclaredErrorCode);\
			const FText DeclaredFormatString = GetFormatString(); \
			int32 GetErrorCodeId() \
			{\
				return ErrorCodeId;\
			}\
			FAnsiStringView GetStaticErrorName() \
			{ \
				static const auto Reference = TStringLiteralWithSpacesReplaced(#ErrorName); \
				return Reference.Str; \
			}\
			TRefCountPtr<const FStaticErrorDetails> GetStaticErrorDetails() \
			{\
				static FStaticErrorDetails StaticErrorDetails(GetStaticErrorName(), UE::UnifiedError::ModuleName::GetStaticModuleName(), DeclaredFormatString);\
				return TRefCountPtr<const FStaticErrorDetails>(&StaticErrorDetails);\
			}\
			const FError& GetStaticError() \
			{\
				static FError StaticError = MakeError();\
				return StaticError;\
			}\
			bool OfType(const FError& Other) \
			{\
				return UE::UnifiedError::ModuleName::GetStaticModuleId() == Other.GetModuleId() && ErrorCodeId == Other.GetErrorCode(); \
			}\
	}}}

#define UE_DECLARE_ERROR(DeclareApi, ErrorName, ErrorCode, ModuleName, FormatString) \
	UE_DECLARE_ERROR_INTERNAL(DeclareApi, ErrorName, ErrorCode, ModuleName, FormatString) \
	namespace UE::UnifiedError { namespace ModuleName { namespace ErrorName {\
			inline FError MakeError() { return FError(UE::UnifiedError::ModuleName::GetStaticModuleId(), GetErrorCodeId(), TRefCountPtr<const UE::UnifiedError::FManditoryErrorDetails>(GetStaticErrorDetails().GetReference())); } \
			template<typename ErrorContextType> \
			inline FError MakeError(ErrorContextType&& Ctx, EDetailFilter DetailFilter = EDetailFilter::Default) \
			{\
				FError Error(UE::UnifiedError::ModuleName::GetStaticModuleId(), GetErrorCodeId(), TRefCountPtr<const UE::UnifiedError::FManditoryErrorDetails>(GetStaticErrorDetails().GetReference())); \
				Error.PushErrorContext(MoveTemp(Ctx), DetailFilter); \
				return Error; \
			}\
	}}}


#define UE_DECLARE_ERROR_ONEPARAM(DeclareApi, ErrorName, ErrorCode, ModuleName, FormatString, ParamOneType, ParamOneName, ParamOneDefault) \
	UE_DECLARE_ERROR_INTERNAL(DeclareApi, ErrorName, ErrorCode, ModuleName, FText::FromString(UTF8TEXT("{"#ModuleName"_F"#ErrorName"}")) ) \
	namespace UE::UnifiedError { namespace ModuleName { \
			struct F##ErrorName \
			{\
				ParamOneType ParamOneName;\
			};\
	}}\
	UE_DECLARE_ERRORSTRUCT_FEATURES(ModuleName, F##ErrorName);\
	inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::ModuleName::F##ErrorName& Context)\
	{\
		Writer.BeginObject();\
		Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::ModuleName::F##ErrorName>::GetErrorContextTypeNameAsString());\
		UE::SerializeLogFormat(Writer, FormatString); \
		UE::UnifiedError::SerializeBasicToCb(Writer, ANSITEXTVIEW(#ParamOneName), Context.ParamOneName);\
		Writer.EndObject();\
	}\
	namespace UE::UnifiedError { namespace ModuleName { \
	namespace ErrorName { \
			inline FError MakeError(ParamOneType ParamOneName = ParamOneDefault) \
			{ \
				FError Error = FError(UE::UnifiedError::ModuleName::GetStaticModuleId(), UE::UnifiedError::ModuleName::ErrorName::GetErrorCodeId(), UE::UnifiedError::ModuleName::ErrorName::GetStaticErrorDetails().GetReference()); \
				F##ErrorName Context = {ParamOneName}; \
				Error.PushErrorContext(MoveTemp(Context)); \
				return Error; \
			} \
	}}}

#define UE_DECLARE_ERROR_TWOPARAM(DeclareApi, ErrorName, ErrorCode, ModuleName, FormatString, ParamOneType, ParamOneName, ParamOneDefault, ParamTwoType, ParamTwoName, ParamTwoDefault) \
	UE_DECLARE_ERROR_INTERNAL(DeclareApi, ErrorName, ErrorCode, ModuleName, FText::FromString(UTF8TEXT("{"#ModuleName"_F"#ErrorName"}")) ) \
	namespace UE::UnifiedError { namespace ModuleName { \
			struct F##ErrorName \
			{\
				ParamOneType ParamOneName;\
				ParamTwoType ParamTwoName;\
			};\
	}}\
	UE_DECLARE_ERRORSTRUCT_FEATURES(ModuleName, F##ErrorName);\
	inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::ModuleName::F##ErrorName& Context)\
	{\
		Writer.BeginObject();\
		Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::ModuleName::F##ErrorName>::GetErrorContextTypeNameAsString());\
		UE::SerializeLogFormat(Writer, FormatString); \
		UE::UnifiedError::SerializeBasicToCb(Writer, ANSITEXTVIEW(#ParamOneName), Context.ParamOneName);\
		UE::UnifiedError::SerializeBasicToCb(Writer, ANSITEXTVIEW(#ParamTwoName), Context.ParamTwoName);\
		Writer.EndObject();\
	}\
	namespace UE::UnifiedError { namespace ModuleName { \
	namespace ErrorName { \
			inline FError MakeError(ParamOneType ParamOneName = ParamOneDefault, ParamTwoType ParamTwoName = ParamTwoDefault) \
			{ \
				FError Error = FError(UE::UnifiedError::ModuleName::GetStaticModuleId(), UE::UnifiedError::ModuleName::ErrorName::GetErrorCodeId(), UE::UnifiedError::ModuleName::ErrorName::GetStaticErrorDetails().GetReference()); \
				F##ErrorName Context = {ParamOneName, ParamTwoName}; \
				Error.PushErrorContext(MoveTemp(Context)); \
				return Error; \
			} \
	}}}

