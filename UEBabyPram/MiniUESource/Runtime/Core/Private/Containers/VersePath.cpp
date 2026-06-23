// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/VersePath.h"
#include "Misc/Char.h"

#define LOCTEXT_NAMESPACE "VersePath"

namespace UE::VersePath::Private
{
	static const FText IdentDefaultTerm = LOCTEXT("IdentDefaultTerm", "Verse identifier");

	struct FNullTerminal
	{
		explicit FNullTerminal() = default;
	};

	FORCEINLINE bool operator==(const TCHAR* Ch, FNullTerminal) { return *Ch == TEXT('\0'); }
	FORCEINLINE bool operator!=(const TCHAR* Ch, FNullTerminal) { return *Ch != TEXT('\0'); }

	FORCEINLINE bool IsAlpha(TCHAR Ch)
	{
		return Ch == '_' || (Ch >= 'A' && Ch <= 'Z') || (Ch >= 'a' && Ch <= 'z');
	}

	FORCEINLINE bool IsNum(TCHAR Ch)
	{
		return (Ch >= '0' && Ch <= '9');
	}

	FORCEINLINE bool IsAlphaNum(TCHAR Ch)
	{
		return IsAlpha(Ch) || IsNum(Ch);
	};

	template <typename EndType>
	FStringView GetPathSection(const TCHAR* Start, EndType End)
	{
		const TCHAR* Ptr = Start;
		while ((Ptr != End) && (*Ptr != TEXT('/')))
		{
			++Ptr;
		}
		const int32 Len = int32(Ptr - Start);
		return FStringView(Start, Len);
	}

	FText MakeInvalidCharsMessage(const TSet<TCHAR>& InvalidChars, const FText& Owner)
	{
		if (InvalidChars.IsEmpty())
		{
			return FText::GetEmpty();
		}

		bool bContainsWhitespace = false;
		FString InvalidCharsNoWhitespace;
		InvalidCharsNoWhitespace.Reserve(InvalidChars.Num() * 2);
		for (TCHAR InvalidChar : InvalidChars)
		{
			if (FChar::IsWhitespace(InvalidChar))
			{
				bContainsWhitespace = true;
			}
			else
			{
				if (!InvalidCharsNoWhitespace.IsEmpty())
				{
					InvalidCharsNoWhitespace.AppendChar(TEXT(' '));
				}
				InvalidCharsNoWhitespace.AppendChar(InvalidChar);
			}
		}

		if (bContainsWhitespace && !InvalidCharsNoWhitespace.IsEmpty())
		{
			return FText::Format(
				LOCTEXT("ForbiddenWhitescapeAndChars", "{0} cannot contain whitespace characters or the following characters: {1}"),
				Owner,
				FText::AsCultureInvariant(MoveTemp(InvalidCharsNoWhitespace)));

		}
		else if (bContainsWhitespace)
		{
			return FText::Format(LOCTEXT("ForbiddenWhitescape", "{0} cannot contain whitespace characters"), Owner);
		}
		else
		{
			return FText::Format(
				LOCTEXT("ForbiddenChars", "{0} cannot contain the following characters: {1}"),
				Owner,
				FText::AsCultureInvariant(MoveTemp(InvalidCharsNoWhitespace)));
		}
	}

	template <typename EndType>
	bool ParseChar(TCHAR Ch, const TCHAR*& Ptr, EndType End)
	{
		const TCHAR* LocalPtr = Ptr;
		if (LocalPtr == End || *LocalPtr != Ch)
		{
			return false;
		}

		Ptr = LocalPtr + 1;
		return true;
	}

	FORCEINLINE bool IsValidDomainLabelChar(TCHAR Ch)
	{
		return Ch == TEXT('-') || Ch == TEXT('.') || IsAlphaNum(Ch);
	}

	template <typename EndType>
	bool IsDomainLabelEnd(const TCHAR* Ptr, EndType End, bool bStopOnSlash, bool bStopOnAtSign)
	{
		return Ptr == End || (bStopOnSlash && *Ptr == TEXT('/')) || (bStopOnAtSign && *Ptr == TEXT('@'));
	};

	template <typename EndType>
	TSet<TCHAR> GetDomainLabelInvalidChars(const TCHAR* Ptr, EndType End, bool bStopOnSlash, bool bStopOnAtSign)
	{
		TSet<TCHAR> InvalidChars;
		while (!IsDomainLabelEnd(Ptr, End, bStopOnSlash, bStopOnAtSign))
		{
			TCHAR Ch = *Ptr;
			if (!IsValidDomainLabelChar(Ch))
			{
				InvalidChars.Add(Ch);
			}
			++Ptr;
		}
		return InvalidChars;
	}

	template <typename EndType>
	FText MakeDomainLabelInvalidCharsMessage(const TCHAR* Ptr, EndType End, bool bStopOnSlash, bool bStopOnAtSign)
	{
		const TSet<TCHAR> InvalidChars = GetDomainLabelInvalidChars(Ptr, End, bStopOnSlash, bStopOnAtSign);
		return MakeInvalidCharsMessage(InvalidChars, LOCTEXT("DomainLabel", "Domain label"));
	}

	template <typename EndType>
	bool ParseDomainLabel(const TCHAR*& Ptr, EndType End, bool bStopOnSlash, bool bStopOnAtSign, FText* OutErrorMessage = nullptr)
	{
		const TCHAR* LocalPtr = Ptr;
		if (IsDomainLabelEnd(LocalPtr, End, bStopOnSlash, bStopOnAtSign))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = LOCTEXT("LabelEmpty", "Domain label cannot be empty");
			}
			return false;
		}

		if (!IsAlphaNum(*LocalPtr))
		{
			if (OutErrorMessage)
			{
				TCHAR Ch = *LocalPtr;
				if (Ch == TEXT('-'))
				{
					*OutErrorMessage = LOCTEXT("LabelStartWithDash", "Domain label cannot start with a dash");
				}
				else if (Ch == TEXT('.'))
				{
					*OutErrorMessage = LOCTEXT("LabelStartWithDot", "Domain label cannot start with a dot");
				}
				else
				{
					*OutErrorMessage = MakeDomainLabelInvalidCharsMessage(LocalPtr, End, bStopOnSlash, bStopOnAtSign);
				}
			}
			return false;
		}

		++LocalPtr;
		for (;;)
		{
			if (IsDomainLabelEnd(LocalPtr, End, bStopOnSlash, bStopOnAtSign))
			{
				break;
			}

			TCHAR Ch = *LocalPtr;
			if (IsValidDomainLabelChar(Ch))
			{
				++LocalPtr;
			}
			else
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = MakeDomainLabelInvalidCharsMessage(LocalPtr, End, bStopOnSlash, bStopOnAtSign);
				}
				return false;
			}
		}

		Ptr = LocalPtr;
		return true;
	}

	template <typename EndType>
	TSet<TCHAR> GetIdentInvalidChars(const TCHAR* Ptr, EndType End, bool bStopOnSlash)
	{
		TSet<TCHAR> InvalidChars;
		while (Ptr != End && (!bStopOnSlash || *Ptr != TEXT('/')))
		{
			TCHAR Ch = *Ptr;
			if (!IsAlphaNum(Ch))
			{
				InvalidChars.Add(Ch);
			}
			++Ptr;
		}
		return InvalidChars;
	}

	template <typename EndType>
	FText MakeIdentInvalidCharsMessage(const TCHAR* Ptr, EndType End, bool bStopOnSlash, const FText& IdentTerm)
	{
		const TSet<TCHAR> InvalidChars = GetIdentInvalidChars(Ptr, End, bStopOnSlash);
		return MakeInvalidCharsMessage(InvalidChars, IdentTerm);
	}

	template <typename EndType>
	bool ParseIdent(const TCHAR*& Ptr, EndType End, bool bStopOnSlash, FText* OutErrorMessage = nullptr, const FText* IdentTermReplacement = nullptr)
	{
		const FText& IdentTermToUse = IdentTermReplacement ? *IdentTermReplacement : IdentDefaultTerm;

		auto EndReached =
			[End, bStopOnSlash](const TCHAR* Ptr)
			{
				return Ptr == End || (bStopOnSlash && *Ptr == TEXT('/'));
			};

		const TCHAR* LocalPtr = Ptr;
		if (EndReached(LocalPtr))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FText::Format(LOCTEXT("IdentEmpty", "{0} cannot be empty"), IdentTermToUse);
			}
			return false;
		}

		if (!IsAlpha(*LocalPtr))
		{
			if (OutErrorMessage)
			{
				if (IsNum(*LocalPtr))
				{
					*OutErrorMessage = FText::Format(LOCTEXT("IdentStartWithNumber", "{0} cannot start with a number"), IdentTermToUse);
				}
				else
				{
					*OutErrorMessage = MakeIdentInvalidCharsMessage(LocalPtr, End, bStopOnSlash, IdentTermToUse);
				}
			}
			return false;
		}

		++LocalPtr;
		for (;;)
		{
			if (EndReached(LocalPtr))
			{
				break;
			}

			if (IsAlphaNum(*LocalPtr))
			{
				++LocalPtr;
			}
			else
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = MakeIdentInvalidCharsMessage(LocalPtr, End, bStopOnSlash, IdentTermToUse);
				}
				return false;
			}
		}

		Ptr = LocalPtr;
		return true;
	}

	FORCEINLINE void MakeInvalidDomainErrorMessage(const FStringView Domain, FText& OutErrorMessage)
	{
		OutErrorMessage = FText::Format(LOCTEXT("InvalidDomain", "Invalid Verse domain \"{0}\" : {1}"), FText::FromStringView(Domain), OutErrorMessage);
	}

	template <typename EndType>
	bool ParseDomain(const TCHAR*& Ptr, EndType End, bool bStopOnSlash, FText* OutErrorMessage = nullptr)
	{
		const TCHAR* DomainStart = Ptr;

		if (!ParseDomainLabel(Ptr, End, bStopOnSlash, /*bStopOnAtSign=*/true, OutErrorMessage))
		{
			if (OutErrorMessage)
			{
				const FStringView Domain = GetPathSection(DomainStart, End);
				if (!Domain.IsEmpty())
				{
					MakeInvalidDomainErrorMessage(Domain, *OutErrorMessage);
				}
				else
				{
					*OutErrorMessage = LOCTEXT("EmptyDomain", "Verse domain cannot be empty");
				}
			}
			return false;
		}

		if (ParseChar('@', Ptr, End))
		{
			if (!ParseDomainLabel(Ptr, End, bStopOnSlash, /*bStopOnAtSign=*/false, OutErrorMessage))
			{
				if (OutErrorMessage)
				{
					const FStringView Domain = GetPathSection(DomainStart, End);
					if (ensureAlways(!Domain.IsEmpty()))
					{
						MakeInvalidDomainErrorMessage(Domain, *OutErrorMessage);
					}
				}
				return false;
			}
		}

		return true;
	}

	template <typename EndType>
	bool ParseSubpath(const TCHAR*& Ptr, EndType End, FText* OutErrorMessage = nullptr)
	{
		for (;;)
		{
			const TCHAR* IdentStart = Ptr;
			if (!ParseIdent(Ptr, End, /*bStopOnSlash=*/true, OutErrorMessage))
			{
				if (OutErrorMessage)
				{
					const FStringView Ident = GetPathSection(IdentStart, End);
					if (!Ident.IsEmpty())
					{
						*OutErrorMessage = FText::Format(LOCTEXT("InvalidIdentifierInSubPath", "Invalid subpath \"{0}\" : {1}"), FText::FromStringView(Ident), *OutErrorMessage);
					}
					else if (IdentStart == End)
					{
						*OutErrorMessage = LOCTEXT("EndWithSlash", "Verse path cannot end with a slash");
					}
					else
					{
						*OutErrorMessage = LOCTEXT("ConsecutiveSlashes", "Verse path cannot have consecutive slashes");
					}
				}
				return false;
			}

			if (!ParseChar('/', Ptr, End))
			{
				return true;
			}
		}
	}

	template <typename EndType>
	bool ParsePath(const TCHAR*& Ptr, EndType End, FText* OutErrorMessage = nullptr)
	{
		if (!ParseChar('/', Ptr, End))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = LOCTEXT("StartWithSlash", "Verse path must start with a slash");
			}
			return false;
		}

		if (!ParseDomain(Ptr, End, /*bStopOnSlash=*/true, OutErrorMessage))
		{
			return false;
		}

		if (ParseChar('/', Ptr, End))
		{
			if (!ParseSubpath(Ptr, End, OutErrorMessage))
			{
				return false;
			}
		}

		return true;
	}

	template <typename EndType>
	bool IsValidVersePath(const TCHAR* Ptr, EndType End, FText* OutErrorMessage = nullptr)
	{
		const bool bResult = UE::VersePath::Private::ParsePath(Ptr, End, OutErrorMessage);
		// Make sure the entire string was parsed.
		return bResult && Ptr == End;
	}

	template <typename EndType>
	bool IsValidDomain(const TCHAR* Ptr, EndType End, FText* OutErrorMessage = nullptr)
	{
		const bool bResult = UE::VersePath::Private::ParseDomain(Ptr, End, /*bStopOnSlash=*/false, OutErrorMessage);
		// Make sure the entire string was parsed.
		return bResult && Ptr == End;
	}

	template <typename EndType>
	bool IsValidSubpath(const TCHAR* Ptr, EndType End, FText* OutErrorMessage = nullptr)
	{
		const bool bResult = UE::VersePath::Private::ParseSubpath(Ptr, End, OutErrorMessage);
		// Make sure the entire string was parsed.
		return bResult && Ptr == End;
	}

	template <typename EndType>
	bool IsValidIdent(const TCHAR* Ptr, EndType End, FText* OutErrorMessage = nullptr, const FText* IdentTermReplacement = nullptr)
	{
		const bool bResult = UE::VersePath::Private::ParseIdent(Ptr, End, /*bStopOnSlash=*/false, OutErrorMessage, IdentTermReplacement);
		// Make sure the entire string was parsed.
		return bResult && Ptr == End;
	}

	template <typename EndType>
	void NormalizeDomainCase(TCHAR* Path, EndType End)
	{
		checkSlow(IsValidVersePath(Path, End));
		++Path;

		// Everything up to the second slash (if there is one) is normalized to lowercase
		for (;;)
		{
			if (Path == End)
			{
				return;
			}

			TCHAR Ch = *Path;
			if (Ch == '/')
			{
				return;
			}

			*Path++ = FChar::ToLower(Ch);
		}
	}
}

bool UE::Core::FVersePath::IsBaseOf(const FVersePath& Other, FStringView* OutLeafPath) const
{
	if (!PathString.IsEmpty() && Other.PathString.StartsWith(PathString, ESearchCase::CaseSensitive))
	{
		if (PathString.Len() == Other.PathString.Len() || Other.PathString[PathString.Len()] == TEXT('/'))
		{
			if (OutLeafPath != nullptr)
			{
				*OutLeafPath = FStringView(Other.PathString).RightChop(PathString.Len() + 1);
			}
			return true;
		}
	}

	if (OutLeafPath != nullptr)
	{
		OutLeafPath->Reset();
	}
	return false;
}

bool UE::Core::FVersePath::IsValidFullPath(const TCHAR* String, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidVersePath(String, UE::VersePath::Private::FNullTerminal{}, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidFullPath(const TCHAR* String, int32 Len, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidVersePath(String, String + Len, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidDomain(const TCHAR* String, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidDomain(String, UE::VersePath::Private::FNullTerminal{}, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidDomain(const TCHAR* String, int32 Len, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidDomain(String, String + Len, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidSubpath(const TCHAR* String, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidSubpath(String, UE::VersePath::Private::FNullTerminal{}, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidSubpath(const TCHAR* String, int32 Len, FText* OutErrorMessage)
{
	return UE::VersePath::Private::IsValidSubpath(String, String + Len, OutErrorMessage);
}

bool UE::Core::FVersePath::IsValidIdent(const TCHAR* String, FText* OutErrorMessage, const FText* IdentTermReplacement)
{
	return UE::VersePath::Private::IsValidIdent(String, UE::VersePath::Private::FNullTerminal{}, OutErrorMessage, IdentTermReplacement);
}

bool UE::Core::FVersePath::IsValidIdent(const TCHAR* String, int32 Len, FText* OutErrorMessage, const FText* IdentTermReplacement)
{
	if (OutErrorMessage)
	{
		*OutErrorMessage = FText::GetEmpty();
	}
	return UE::VersePath::Private::IsValidIdent(String, String + Len, OutErrorMessage, IdentTermReplacement);
}

bool UE::Core::FVersePath::TryMake(FVersePath& OutPath, const FString& Path, FText* OutErrorMessage)
{
	if (!IsValidFullPath(*Path, OutErrorMessage))
	{
		return false;
	}

	OutPath.PathString = Path;
	TCHAR* OutPathPtr = OutPath.PathString.GetCharArray().GetData();
	UE::VersePath::Private::NormalizeDomainCase(OutPathPtr, OutPathPtr + OutPath.PathString.Len());
	return true;
}

bool UE::Core::FVersePath::TryMake(FVersePath& OutPath, FString&& Path, FText* OutErrorMessage)
{
	if (!IsValidFullPath(*Path, OutErrorMessage))
	{
		return false;
	}

	OutPath.PathString = MoveTemp(Path);
	TCHAR* OutPathPtr = OutPath.PathString.GetCharArray().GetData();
	UE::VersePath::Private::NormalizeDomainCase(OutPathPtr, OutPathPtr + OutPath.PathString.Len());
	return true;
}

FString UE::Core::MangleGuidToVerseIdent(const FString& Guid)
{
	return MakeValidVerseIdentifier(Guid);
}

FString UE::Core::MakeValidVerseIdentifier(FStringView Str)
{
	FString Ident;
	Ident.Reserve(Str.Len() + 1);

	bool bIsLastCharWhitespace = false;
	for (TCHAR Char : Str)
	{
		if (UE::VersePath::Private::IsAlphaNum(Char))
		{
			if (Ident.IsEmpty() && UE::VersePath::Private::IsNum(Char))
			{
				Ident.AppendChar(TEXT('_'));
			}
			Ident.AppendChar(Char);
			bIsLastCharWhitespace = false;
		}
		else if (FChar::IsWhitespace(Char) && !Ident.IsEmpty() && Ident[Ident.Len() - 1] != TEXT('_'))
		{
			Ident.AppendChar(TEXT('_'));
			bIsLastCharWhitespace = true;
		}
	}

	if (Ident.IsEmpty())
	{
		Ident.AppendChar(TEXT('_'));
	}
	else if (bIsLastCharWhitespace && Ident.Len() > 1)
	{
		Ident.RemoveFromEnd(TEXT("_"));
	}

	return Ident;
}

#undef LOCTEXT_NAMESPACE
