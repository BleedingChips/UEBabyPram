// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "ExternAPI.h"

#include <cstddef>

namespace AutoRTFM
{

// A stl-compatible allocator which allocates with AutoRTFM::Allocate() and
// frees with AutoRTFM::Free().
// TODO(SOL-7652): Remove this once HashMap has been replaced with a bespoke
// hashmap implementation.
template <typename T>
class StlAllocator
{
public:
	using value_type = T;

	StlAllocator() = default;

	template <typename U>
	StlAllocator(const StlAllocator<U>&) {};

	T* allocate(size_t Count)
	{
		return static_cast<T*>(AutoRTFM::Allocate(Count * sizeof(T), alignof(T)));
	}

	void deallocate(T* Pointer, size_t)
	{
		AutoRTFM::Free(Pointer);
	}
};

template <typename T, typename U>
bool operator==(const StlAllocator<T>&, const StlAllocator<U>&)
{
	return true;
}

template <typename T, typename U>
bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&)
{
	return false;
}

}  // AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
