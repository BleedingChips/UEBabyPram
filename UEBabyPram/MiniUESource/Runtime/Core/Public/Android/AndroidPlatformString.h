// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Build.h"
#include "Misc/CoreMiscDefines.h"
#include "GenericPlatform/GenericWidePlatformString.h"

UE_DEPRECATED(5.7, "FAndroidPlatformString has been deprecated, please use FPlatformString instead.")
typedef FGenericWidePlatformString FAndroidPlatformString;

typedef FGenericWidePlatformString FPlatformString;

// Format specifiers to be able to print values of these types correctly, for example when using UE_LOG.
// SIZE_T format specifier
#if PLATFORM_64BITS
#define SIZE_T_FMT "llu"
// SIZE_T format specifier for lowercase hexadecimal output
#define SIZE_T_x_FMT "llx"
// SIZE_T format specifier for uppercase hexadecimal output
#define SIZE_T_X_FMT "llX"

// SSIZE_T format specifier
#define SSIZE_T_FMT "lld"
// SSIZE_T format specifier for lowercase hexadecimal output
#define SSIZE_T_x_FMT "llx"
// SSIZE_T format specifier for uppercase hexadecimal output
#define SSIZE_T_X_FMT "llX"
#else
#define SIZE_T_FMT "u"
// SIZE_T format specifier for lowercase hexadecimal output
#define SIZE_T_x_FMT "x"
// SIZE_T format specifier for uppercase hexadecimal output
#define SIZE_T_X_FMT "X"

// SSIZE_T format specifier
#define SSIZE_T_FMT "d"
// SSIZE_T format specifier for lowercase hexadecimal output
#define SSIZE_T_x_FMT "x"
// SSIZE_T format specifier for uppercase hexadecimal output
#define SSIZE_T_X_FMT "X"
#endif

// PTRINT format specifier for decimal output
#define PTRINT_FMT SSIZE_T_FMT
// PTRINT format specifier for lowercase hexadecimal output
#define PTRINT_x_FMT SSIZE_T_x_FMT
// PTRINT format specifier for uppercase hexadecimal output
#define PTRINT_X_FMT SSIZE_T_X_FMT

// UPTRINT format specifier for decimal output
#define UPTRINT_FMT SIZE_T_FMT
// UPTRINT format specifier for lowercase hexadecimal output
#define UPTRINT_x_FMT SIZE_T_x_FMT
// UPTRINT format specifier for uppercase hexadecimal output
#define UPTRINT_X_FMT SIZE_T_X_FMT

// int64 format specifier for decimal output
#define INT64_FMT "lld"
// int64 format specifier for lowercase hexadecimal output
#define INT64_x_FMT "llx"
// int64 format specifier for uppercase hexadecimal output
#define INT64_X_FMT "llX"

// uint64 format specifier for decimal output
#define UINT64_FMT "llu"
// uint64 format specifier for lowercase hexadecimal output
#define UINT64_x_FMT "llx"
// uint64 format specifier for uppercase hexadecimal output
#define UINT64_X_FMT "llX"
