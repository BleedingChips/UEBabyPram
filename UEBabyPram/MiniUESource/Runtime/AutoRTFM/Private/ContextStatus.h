// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"
#include "Utils.h"

namespace AutoRTFM
{
inline const char* GetContextStatusName(EContextStatus Status)
{
    switch (Status)
    {
    default:
        InternalUnreachable();
        return nullptr;
    case EContextStatus::Idle:
        return "Idle";
    case EContextStatus::OnTrack:
        return "OnTrack";
    case EContextStatus::AbortedByFailedLockAcquisition:
        return "AbortedByFailedLockAcquisition";
    case EContextStatus::AbortedByLanguage:
        return "AbortedByLanguage";
    case EContextStatus::AbortedByRequest:
        return "AbortedByRequest";
    case EContextStatus::Committing:
        return "Committing";
    case EContextStatus::AbortedByCascadingAbort:
        return "AbortedByCascadingAbort";
    case EContextStatus::AbortedByCascadingRetry:
        return "AbortedByCascadingRetry";
    case EContextStatus::InStaticLocalInitializer:
        return "InStaticLocalInitializer";
    }
}

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
