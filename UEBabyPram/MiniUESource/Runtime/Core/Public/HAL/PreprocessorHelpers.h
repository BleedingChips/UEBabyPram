// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Turns an preprocessor token into a real string (see UBT_COMPILED_PLATFORM)
#define UE_STRINGIZE(Token) UE_PRIVATE_STRINGIZE(Token)
#define UE_PRIVATE_STRINGIZE(Token) #Token

// Concatenates two preprocessor tokens, performing macro expansion on them first
#define UE_JOIN(TokenA, TokenB) UE_PRIVATE_JOIN(TokenA, TokenB)
#define UE_PRIVATE_JOIN(TokenA, TokenB) TokenA##TokenB

// Concatenates the first two preprocessor tokens of a variadic list, after performing macro expansion on them
#define UE_JOIN_FIRST(Token, ...) UE_PRIVATE_JOIN_FIRST(Token, __VA_ARGS__)
#define UE_PRIVATE_JOIN_FIRST(Token, ...) Token##__VA_ARGS__

// Expands to the second argument or the third argument if the first argument is 1 or 0 respectively
#define UE_IF(OneOrZero, Token1, Token0) UE_JOIN(UE_PRIVATE_IF_, OneOrZero)(Token1, Token0)
#define UE_PRIVATE_IF_1(Token1, Token0) Token1
#define UE_PRIVATE_IF_0(Token1, Token0) Token0

// Expands to the parameter list of the macro - used to pass a *potentially* comma-separated identifier to another macro as a single parameter
#define UE_COMMA_SEPARATED(First, ...) First, ##__VA_ARGS__

// Expands to a number which is the count of variadic arguments passed to it.
#define UE_VA_ARG_COUNT(...) UE_APPEND_VA_ARG_COUNT(, ##__VA_ARGS__)

// Expands to a token of Prefix##<count>, where <count> is the number of variadic arguments.
//
// Example:
//   UE_APPEND_VA_ARG_COUNT(SOME_MACRO_)          => SOME_MACRO_0
//   UE_APPEND_VA_ARG_COUNT(SOME_MACRO_, a, b, c) => SOME_MACRO_3
#if !defined(_MSVC_TRADITIONAL) || !_MSVC_TRADITIONAL
	#define UE_APPEND_VA_ARG_COUNT(Prefix, ...) UE_PRIVATE_APPEND_VA_ARG_COUNT(Prefix, ##__VA_ARGS__, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#else
	#define UE_APPEND_VA_ARG_COUNT(Prefix, ...) UE_PRIVATE_APPEND_VA_ARG_COUNT_INVOKE(UE_PRIVATE_APPEND_VA_ARG_COUNT, (Prefix, ##__VA_ARGS__, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

	// MSVC's traditional preprocessor doesn't handle the zero-argument case correctly, so we use a workaround.
	// The workaround uses token pasting of Macro##ArgsInParens, which the conformant preprocessor doesn't like and emits C5103.
	#define UE_PRIVATE_APPEND_VA_ARG_COUNT_INVOKE(Macro, ArgsInParens) UE_PRIVATE_APPEND_VA_ARG_COUNT_EXPAND(Macro##ArgsInParens)
	#define UE_PRIVATE_APPEND_VA_ARG_COUNT_EXPAND(Arg) Arg
#endif
#define UE_PRIVATE_APPEND_VA_ARG_COUNT(Prefix,A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,Count,...) Prefix##Count

// Expands to nothing - used as a placeholder
#define UE_EMPTY

// Expands to nothing when used as a function - used as a placeholder
#define UE_EMPTY_FUNCTION(...)

// Removes a single layer of parentheses from a macro argument if they are present - used to allow
// brackets to be optionally added when the argument contains commas, e.g.:
//
// #define DEFINE_VARIABLE(Type, Name) UE_REMOVE_OPTIONAL_PARENS(Type) Name;
//
// DEFINE_VARIABLE(int, IntVar)                  // expands to: int IntVar;
// DEFINE_VARIABLE((TPair<int, float>), PairVar) // expands to: TPair<int, float> PairVar;
#define UE_REMOVE_OPTIONAL_PARENS(...) UE_JOIN_FIRST(UE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENS,UE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENS __VA_ARGS__)
#define UE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENS(...) UE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENS __VA_ARGS__
#define UE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENSUE_PRIVATE_PREPROCESSOR_REMOVE_OPTIONAL_PARENS

// setup standardized way of including platform headers from the "uber-platform" headers like PlatformFile.h
#ifdef OVERRIDE_PLATFORM_HEADER_NAME
// allow for an override, so compiled platforms Win64 and Win32 will both include Windows
#define PLATFORM_HEADER_NAME OVERRIDE_PLATFORM_HEADER_NAME
#else
// otherwise use the compiled platform name
#define PLATFORM_HEADER_NAME UBT_COMPILED_PLATFORM
#endif

#define UE_SOURCE_LOCATION TEXT(__FILE__ "(" UE_STRINGIZE(__LINE__) ")")

#ifndef PLATFORM_IS_EXTENSION
#define PLATFORM_IS_EXTENSION 0
#endif

#if PLATFORM_IS_EXTENSION
// Creates a string that can be used to include a header in the platform extension form "PlatformHeader.h", not like
// below form. When using this you should add "// IWYU pragma: export" at the end of the line.
#define COMPILED_PLATFORM_HEADER(Suffix) PREPROCESSOR_TO_STRING(PREPROCESSOR_JOIN(PLATFORM_HEADER_NAME, Suffix))
#else
// Creates a string that can be used to include a header in the form "Platform/PlatformHeader.h", like
// "Windows/WindowsPlatformFile.h". When using this you should add "// IWYU pragma: export" at the end of the line.
#define COMPILED_PLATFORM_HEADER(Suffix) PREPROCESSOR_TO_STRING(PREPROCESSOR_JOIN(PLATFORM_HEADER_NAME/PLATFORM_HEADER_NAME, Suffix))
#endif

// Creates a string that can be used to include a header in the platform extension form "PlatformHeader.h", but will
// not include a directory like COMPILED_PLATFORM_HEADER does, generally for UBT generated headers.
#define COMPILED_PLATFORM_HEADER_GENERATED(Suffix) PREPROCESSOR_TO_STRING(PREPROCESSOR_JOIN(PLATFORM_HEADER_NAME, Suffix))

#if PLATFORM_IS_EXTENSION
// Creates a string that can be used to include a header with the platform in its name, like
// "Prefix/PlatformNameSuffix.h". When using this you should add "// IWYU pragma: export" at the end of the line.
#define COMPILED_PLATFORM_HEADER_WITH_PREFIX(Prefix, Suffix) PREPROCESSOR_TO_STRING(Prefix/PREPROCESSOR_JOIN(PLATFORM_HEADER_NAME, Suffix))
#else
// Creates a string that can be used to include a header with the platform in its name, like
// "Prefix/PlatformName/PlatformNameSuffix.h". When using this you should add "// IWYU pragma: export" at the end of the
// line.
#define COMPILED_PLATFORM_HEADER_WITH_PREFIX(Prefix, Suffix) PREPROCESSOR_TO_STRING(Prefix/PLATFORM_HEADER_NAME/PREPROCESSOR_JOIN(PLATFORM_HEADER_NAME, Suffix))
#endif

// These macros should be regarded as deprecated - use the UE_ macros they map to instead.
#define PREPROCESSOR_TO_STRING(Token)                 UE_STRINGIZE(Token)
#define PREPROCESSOR_JOIN(TokenA, TokenB)             UE_JOIN(TokenA, TokenB)
#define PREPROCESSOR_JOIN_FIRST(Token, ...)           UE_JOIN_FIRST(Token, ##__VA_ARGS__)
#define PREPROCESSOR_IF(OneOrZero, Token1, Token0)    UE_IF(OneOrZero, Token1, Token0)
#define PREPROCESSOR_COMMA_SEPARATED(First, ...)      UE_COMMA_SEPARATED(First, ##__VA_ARGS__)
#define PREPROCESSOR_VA_ARG_COUNT(...)                UE_VA_ARG_COUNT(__VA_ARGS__)
#define PREPROCESSOR_APPEND_VA_ARG_COUNT(Prefix, ...) UE_APPEND_VA_ARG_COUNT(Prefix, ##__VA_ARGS__)
#define PREPROCESSOR_NOTHING                          UE_EMPTY
#define PREPROCESSOR_NOTHING_FUNCTION(...)            UE_EMPTY_FUNCTION(__VA_ARGS__)
#define PREPROCESSOR_REMOVE_OPTIONAL_PARENS(...)      UE_REMOVE_OPTIONAL_PARENS(__VA_ARGS__)
