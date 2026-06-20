// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#if PLATFORM_USE_GENERIC_STRING_IMPLEMENTATION

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformStricmp.h"
#include "GenericPlatform/GenericPlatformString.h"
#include "HAL/PlatformCrt.h"
#include "Misc/Char.h"

/**
* Standard implementation
**/
struct FGenericWidePlatformString : public FGenericPlatformString
{
	using FGenericPlatformString::Stricmp;
	using FGenericPlatformString::Strncmp;
	using FGenericPlatformString::Strnicmp;

	template <typename CharType>
	static inline CharType* Strupr(CharType* Dest, SIZE_T DestCount)
	{
		for (CharType* Char = Dest; *Char && DestCount > 0; Char++, DestCount--)
		{
			*Char = TChar<CharType>::ToUpper(*Char);
		}
		return Dest;
	}

public:

	/**
	 * Unicode implementation
	 **/
	CORE_API static WIDECHAR* Strcpy(WIDECHAR* Dest, const WIDECHAR* Src);
	// This version was deprecated because Strcpy taking a size field does not match the CRT strcpy, and on some platforms the size field was being ignored.
	// If the memzeroing causes a performance problem, request a new function StrcpyTruncate.
	UE_DEPRECATED(5.6, "Use Strncpy instead. Note that Strncpy has a behavior difference from Strcpy: it memzeroes the entire DestCount-sized buffer after the end of string.")
	static WIDECHAR* Strcpy(WIDECHAR* Dest, SIZE_T DestCount, const WIDECHAR* Src)
	{
		return Strcpy(Dest, Src);
	}
	CORE_API static WIDECHAR* Strncpy(WIDECHAR* Dest, const WIDECHAR* Src, SIZE_T MaxLen);
	CORE_API static WIDECHAR* Strcat(WIDECHAR* Dest, const WIDECHAR* Src);
	// This version was deprecated because Strcat taking a size field does not match the CRT strcat, and on some platforms the size field was being ignored.
	UE_DEPRECATED(5.6, "Use Strncat instead. !!NOTE THAT STRNCAT takes SrcLen rather than DestCount. You must call Strncat(Dest, Src, DestCount - Strlen(Dest) - 1).")
	static WIDECHAR* Strcat(WIDECHAR* Dest, SIZE_T DestCount, const WIDECHAR* Src)
	{
		SIZE_T DestLen = Strlen(Dest);
		return Strncat(Dest, Src, DestLen + 1 <= DestCount ? DestCount - DestLen - 1 : 0);
	}
	/**
	 * Appends the first SrcLen characters of Src to Dest, plus a terminating null-character.
	 * If the length of the string in Src is less than SrcLen, only the content up to the terminating
	 * null-character is copied.
	 */
	CORE_API static WIDECHAR* Strncat(WIDECHAR* Dest, const WIDECHAR* Src, SIZE_T SrcLen);

	static int32 Strcmp( const WIDECHAR* String1, const WIDECHAR* String2 )
	{
		// walk the strings, comparing them case sensitively
		for (; *String1 || *String2; String1++, String2++)
		{
			WIDECHAR A = *String1, B = *String2;
			if (A != B)
			{
				return A - B;
			}
		}
		return 0;
	}

	static int32 Strlen( const WIDECHAR* String )
	{
		int32 Length = -1;

		do
		{
			Length++;
		}
		while (*String++);

		return Length;
	}

	static int32 Strnlen( const WIDECHAR* String, SIZE_T StringSize )
	{
		int32 Length = -1;

		do
		{
			Length++;
		}
		while (StringSize-- > 0 && *String++);

		return Length;
	}

#if PLATFORM_TCHAR_IS_CHAR16
	static int32 Strlen( const wchar_t* String )
	{
		int32 Length = -1;

		do
		{
			Length++;
		}
		while (*String++);

		return Length;
	}

	static int32 Strnlen( const wchar_t* String, SIZE_T StringSize )
	{
		int32 Length = -1;

		do
		{
			Length++;
		}
		while (StringSize-- > 0 && *String++);

		return Length;
	}
#endif

	static const WIDECHAR* Strstr( const WIDECHAR* String, const WIDECHAR* Find)
	{
		WIDECHAR Char1, Char2;
		if ((Char1 = *Find++) != 0)
		{
			size_t Length = Strlen(Find);
			
			do
			{
				do
				{
					if ((Char2 = *String++) == 0)
					{
						return nullptr;
					}
				}
				while (Char1 != Char2);
			}
			while (Strncmp(String, Find, Length) != 0);
			
			String--;
		}
		
		return String;
	}

	static const WIDECHAR* Strchr( const WIDECHAR* String, WIDECHAR C)
	{
		while (*String != C && *String != 0)
		{
			String++;
		}
		
		return (*String == C) ? String : nullptr;
	}

	static const WIDECHAR* Strrchr( const WIDECHAR* String, WIDECHAR C)
	{
		const WIDECHAR *Last = nullptr;
		
		while (true)
		{
			if (*String == C)
			{
				Last = String;
			}
			
			if (*String == 0)
			{
				break;
			}
			
			String++;
		}
		
		return Last;
	}

	CORE_API static int32 Strtoi(const WIDECHAR* Start, WIDECHAR** End, int32 Base);
	CORE_API static int64 Strtoi64(const WIDECHAR* Start, WIDECHAR** End, int32 Base);

	CORE_API static uint64 Strtoui64( const WIDECHAR* Start, WIDECHAR** End, int32 Base );
	CORE_API static float Atof(const WIDECHAR* String);
	CORE_API static double Atod(const WIDECHAR* String);

	static UE_FORCEINLINE_HINT int32 Atoi(const WIDECHAR* String)
	{
		return Strtoi( String, NULL, 10 );
	}
	
	static UE_FORCEINLINE_HINT int64 Atoi64(const WIDECHAR* String)
	{
		return Strtoi64( String, NULL, 10 );
	}

	
	
	CORE_API static WIDECHAR* Strtok( WIDECHAR* StrToken, const WIDECHAR* Delim, WIDECHAR** Context );

	CORE_API static int32 GetVarArgs(WIDECHAR* Dest, SIZE_T DestSize, const WIDECHAR*& Fmt, va_list ArgPtr);

	/**
	 * Ansi implementation
	 **/
	static inline ANSICHAR* Strcpy(ANSICHAR* Dest, const ANSICHAR* Src)
	{
// Skip suggestions about using strcpy_s instead.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return strcpy( Dest, Src );
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// This version was deprecated because Strcpy taking a size field does not match the CRT strcpy, and on some platforms the size field was being ignored.
	// If the memzeroing causes a performance problem, request a new function StrcpyTruncate.
	UE_DEPRECATED(5.6, "Use Strncpy instead. Note that Strncpy has a behavior difference from Strcpy: it memzeroes the entire DestCount-sized buffer after the end of string.")
	static UE_FORCEINLINE_HINT ANSICHAR* Strcpy(ANSICHAR* Dest, SIZE_T DestCount, const ANSICHAR* Src)
	{
		return Strcpy(Dest, Src);
	}

	static inline ANSICHAR* Strncpy(ANSICHAR* Dest, const ANSICHAR* Src, SIZE_T MaxLen)
	{
// Skip suggestions about using strncpy_s instead.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		::strncpy(Dest, Src, MaxLen);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Dest[MaxLen-1]=0;
		return Dest;
	}

	static inline ANSICHAR* Strcat(ANSICHAR* Dest, const ANSICHAR* Src)
	{
// Skip suggestions about using strcat_s instead.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return strcat( Dest, Src );
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// This version was deprecated because Strcat taking a size field does not match the CRT strcat, and on some platforms the size field was being ignored.
	UE_DEPRECATED(5.6, "Use Strncat instead. !!NOTE THAT STRNCAT takes SrcLen rather than DestCount. You must call Strncat(Dest, Src, DestCount - Strlen(Dest) - 1).")
	static UE_FORCEINLINE_HINT ANSICHAR* Strcat(ANSICHAR* Dest, SIZE_T DestCount, const ANSICHAR* Src)
	{
		return Strcat(Dest, Src);
	}

	/**
	 * Appends the first SrcLen characters of Src to Dest, plus a terminating null-character.
	 * If the length of the string in Src is less than SrcLen, only the content up to the terminating
	 * null-character is copied.
	 */
	static inline ANSICHAR* Strncat(ANSICHAR* Dest, const ANSICHAR* Src, SIZE_T SrcLen)
	{
// Skip suggestions about using strcat_s instead.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return strncat(Dest, Src, SrcLen);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static UE_FORCEINLINE_HINT int32 Strcmp( const ANSICHAR* String1, const ANSICHAR* String2 )
	{
		return strcmp(String1, String2);
	}

	static UE_FORCEINLINE_HINT int32 Strncmp( const ANSICHAR* String1, const ANSICHAR* String2, SIZE_T Count )
	{
		return strncmp( String1, String2, Count );
	}

	static UE_FORCEINLINE_HINT int32 Strlen( const ANSICHAR* String )
	{
		return (int32)strlen( String );
	}

	static UE_FORCEINLINE_HINT int32 Strnlen( const ANSICHAR* String, SIZE_T StringSize )
	{
		return (int32)strnlen( String, StringSize );
	}

	static UE_FORCEINLINE_HINT const ANSICHAR* Strstr( const ANSICHAR* String, const ANSICHAR* Find)
	{
		return strstr(String, Find);
	}

	static UE_FORCEINLINE_HINT const ANSICHAR* Strchr( const ANSICHAR* String, ANSICHAR C)
	{
		return strchr(String, C);
	}

	static UE_FORCEINLINE_HINT const ANSICHAR* Strrchr( const ANSICHAR* String, ANSICHAR C)
	{
		return strrchr(String, C);
	}

	static UE_FORCEINLINE_HINT int32 Atoi(const ANSICHAR* String)
	{
		return atoi( String );
	}

	static UE_FORCEINLINE_HINT int64 Atoi64(const ANSICHAR* String)
	{
		return strtoll( String, NULL, 10 );
	}

	static UE_FORCEINLINE_HINT float Atof(const ANSICHAR* String)
	{
		return (float)atof( String );
	}

	static UE_FORCEINLINE_HINT double Atod(const ANSICHAR* String)
	{
		return atof( String );
	}

	static UE_FORCEINLINE_HINT int32 Strtoi( const ANSICHAR* Start, ANSICHAR** End, int32 Base )
	{
		return (int32)strtol( Start, End, Base );
	}

	static UE_FORCEINLINE_HINT int64 Strtoi64( const ANSICHAR* Start, ANSICHAR** End, int32 Base )
	{
		return strtoll(Start, End, Base);
	}

	static UE_FORCEINLINE_HINT uint64 Strtoui64( const ANSICHAR* Start, ANSICHAR** End, int32 Base )
	{
		return strtoull(Start, End, Base);
	}

	static inline ANSICHAR* Strtok(ANSICHAR* StrToken, const ANSICHAR* Delim, ANSICHAR** Context)
	{
// Skip suggestions about using strtok_s instead.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return strtok(StrToken, Delim);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	static int32 GetVarArgs( ANSICHAR* Dest, SIZE_T DestSize, const ANSICHAR*& Fmt, va_list ArgPtr )
	{
#if PLATFORM_USE_S_SPEC_FOR_NARROWCHAR_IN_VSPRINTF
		// fix up the Fmt string, as fast as possible, without using an FAnsiString
		ANSICHAR* NewFormat = (ANSICHAR*)alloca(Strlen(Fmt) + 1);

		const ANSICHAR* From = Fmt;
		ANSICHAR* To = NewFormat;
		for (;;)
		{
			ANSICHAR FromCh = *From++;
			*To++ = FromCh;
			if (FromCh != '%')
			{
				if (!FromCh)
				{
					break;
				}

				continue;
			}

			if (From[0] == 'h' && From[1] == 's')
			{
				// Skip the h and copy the s next time to give %s
				++From;
			}
		}
		int32 Result = vsnprintf(Dest, DestSize, NewFormat, ArgPtr);
#else
		int32 Result = vsnprintf(Dest, DestSize, Fmt, ArgPtr);
#endif
		return (Result != -1 && Result < (int32)DestSize) ? Result : -1;
	}

	/**
	 * UTF8CHAR implementation.
	 */
	static UE_FORCEINLINE_HINT UTF8CHAR* Strcpy(UTF8CHAR* Dest, const UTF8CHAR* Src)
	{
		return (UTF8CHAR*)Strcpy((ANSICHAR*)Dest, (const ANSICHAR*)Src);
	}

	// This version was deprecated because Strcpy taking a size field does not match the CRT strcpy, and on some platforms the size field was being ignored.
	// If the memzeroing causes a performance problem, request a new function StrcpyTruncate.
	UE_DEPRECATED(5.6, "Use Strncpy instead. Note that Strncpy has a behavior difference from Strcpy: it memzeroes the entire DestCount-sized buffer after the end of string.")
	static UE_FORCEINLINE_HINT UTF8CHAR* Strcpy(UTF8CHAR* Dest, SIZE_T DestCount, const UTF8CHAR* Src)
	{
		return (UTF8CHAR*)Strcpy((ANSICHAR*)Dest, DestCount, (const ANSICHAR*)Src);
	}

	static UE_FORCEINLINE_HINT UTF8CHAR* Strncpy(UTF8CHAR* Dest, const UTF8CHAR* Src, SIZE_T MaxLen)
	{
		return (UTF8CHAR*)Strncpy((ANSICHAR*)Dest, (const ANSICHAR*)Src, MaxLen);
	}

	static UE_FORCEINLINE_HINT UTF8CHAR* Strcat(UTF8CHAR* Dest, const UTF8CHAR* Src)
	{
		return (UTF8CHAR*)Strcat((ANSICHAR*)Dest, (const ANSICHAR*)Src);
	}

	// This version was deprecated because Strcat taking a size field does not match the CRT strcat, and on some platforms the size field was being ignored.
	UE_DEPRECATED(5.6, "Use Strncat instead. !!NOTE THAT STRNCAT takes SrcLen rather than DestCount. You must call Strncat(Dest, Src, DestCount - Strlen(Dest) - 1).")
	static UE_FORCEINLINE_HINT UTF8CHAR* Strcat(UTF8CHAR* Dest, SIZE_T DestCount, const UTF8CHAR* Src)
	{
		return Strcat(Dest, Src);
	}

	/**
	 * Appends the first SrcLen characters of Src to Dest, plus a terminating null-character.
	 * If the length of the string in Src is less than SrcLen, only the content up to the terminating
	 * null-character is copied.
	 */
	static UE_FORCEINLINE_HINT UTF8CHAR* Strncat(UTF8CHAR* Dest, const UTF8CHAR* Src, SIZE_T SrcLen)
	{
		return (UTF8CHAR*)Strncat((ANSICHAR*)Dest, (const ANSICHAR*)Src, SrcLen);
	}

	static UE_FORCEINLINE_HINT int32 Strcmp(const UTF8CHAR* String1, const UTF8CHAR* String2)
	{
		return Strcmp((const ANSICHAR*)String1, (const ANSICHAR*)String2);
	}

	static UE_FORCEINLINE_HINT int32 Strncmp(const UTF8CHAR* String1, const UTF8CHAR* String2, SIZE_T Count)
	{
		return Strncmp((const ANSICHAR*)String1, (const ANSICHAR*)String2, Count);
	}

	static UE_FORCEINLINE_HINT int32 Strlen(const UTF8CHAR* String)
	{
		return Strlen((const ANSICHAR*)String);
	}

	static UE_FORCEINLINE_HINT int32 Strnlen(const UTF8CHAR* String, SIZE_T StringSize)
	{
		return Strnlen((const ANSICHAR*)String, StringSize);
	}

	static UE_FORCEINLINE_HINT const UTF8CHAR* Strstr(const UTF8CHAR* String, const UTF8CHAR* Find)
	{
		return (const UTF8CHAR*)Strstr((const ANSICHAR*)String, (const ANSICHAR*)Find);
	}

	static UE_FORCEINLINE_HINT const UTF8CHAR* Strchr(const UTF8CHAR* String, UTF8CHAR C)
	{
		return (const UTF8CHAR*)Strchr((const ANSICHAR*)String, (ANSICHAR)C);
	}

	static UE_FORCEINLINE_HINT const UTF8CHAR* Strrchr(const UTF8CHAR* String, UTF8CHAR C)
	{
		return (const UTF8CHAR*)Strrchr((const ANSICHAR*)String, (ANSICHAR)C);
	}

	static UE_FORCEINLINE_HINT int32 Atoi(const UTF8CHAR* String)
	{
		return Atoi((const ANSICHAR*)String);
	}

	static UE_FORCEINLINE_HINT int64 Atoi64(const UTF8CHAR* String)
	{
		return Atoi64((const ANSICHAR*)String);
	}

	static UE_FORCEINLINE_HINT float Atof(const UTF8CHAR* String)
	{
		return Atof((const ANSICHAR*)String);
	}

	static UE_FORCEINLINE_HINT double Atod(const UTF8CHAR* String)
	{
		return Atod((const ANSICHAR*)String);
	}

	static UE_FORCEINLINE_HINT int32 Strtoi(const UTF8CHAR* Start, UTF8CHAR** End, int32 Base)
	{
		return Strtoi((const ANSICHAR*)Start, (ANSICHAR**)End, Base);
	}

	static UE_FORCEINLINE_HINT int64 Strtoi64(const UTF8CHAR* Start, UTF8CHAR** End, int32 Base)
	{
		return Strtoi64((const ANSICHAR*)Start, (ANSICHAR**)End, Base);
	}

	static UE_FORCEINLINE_HINT uint64 Strtoui64(const UTF8CHAR* Start, UTF8CHAR** End, int32 Base)
	{
		return Strtoui64((const ANSICHAR*)Start, (ANSICHAR**)End, Base);
	}

	static UE_FORCEINLINE_HINT UTF8CHAR* Strtok(UTF8CHAR* StrToken, const UTF8CHAR* Delim, UTF8CHAR** Context)
	{
		return (UTF8CHAR*)Strtok((ANSICHAR*)StrToken, (const ANSICHAR*)Delim, (ANSICHAR**)Context);
	}

	static UE_FORCEINLINE_HINT int32 GetVarArgs(UTF8CHAR* Dest, SIZE_T DestSize, const UTF8CHAR*& Fmt, va_list ArgPtr)
	{
		return GetVarArgs((ANSICHAR*)Dest, DestSize, *(const ANSICHAR**)&Fmt, ArgPtr);
	}

	/**
	 * UCS2 implementation
	 **/

	static inline int32 Strlen( const UCS2CHAR* String )
	{
		int32 Result = 0;
		while (*String++)
		{
			++Result;
		}

		return Result;
	}

	static inline int32 Strnlen( const UCS2CHAR* String, SIZE_T StringSize )
	{
		int32 Result = 0;
		while (StringSize-- > 0 && *String++)
		{
			++Result;
		}

		return Result;
	}
};

#endif
