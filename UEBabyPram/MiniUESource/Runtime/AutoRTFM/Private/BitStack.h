// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "ContainerValidation.h"
#include "Stack.h"
#include "Utils.h"

#include <cstddef>
#include <cstdint>

namespace AutoRTFM
{

// A dynamically sized stack of bits.
//
// Notes:
//  * Heap allocated memory is not automatically freed when popping elements.
//    Only calling Reset() or destructing the stack will free heap allocated
//    memory.
//  * This class is not relocatable and so is not safe to use in UE's containers
//    which require elements to be relocatable.
//
// Template parameters:
//   InlineCapacity - the number of elements that can be held before spilling to
//                    the heap.
//   Validation - if Disabled, do not perform validity assertions.
template<size_t InlineCapacity, EContainerValidation Validation = EContainerValidation::Enabled>
class TBitStack
{
	using WordType = uint64_t;
	static constexpr size_t NumWordBits = 64;
	static_assert(NumWordBits == sizeof(WordType) * 8);

public:
	// A reference to a single bit in the stack.
	// Becomes invalid if the stack holding the bit is modified.
	class FBitRef
	{
	public:
		// Constructor
		FBitRef(WordType& Word, WordType Mask) : Word(Word), Mask(Mask) {}

		// Returns true if the bit is set.
		operator bool() const
		{
			return (Word & Mask) != 0;
		}

		// Copy assignment operator.
		// Changes the value of the referenced bit in the stack.
		FBitRef& operator = (const FBitRef& Other)
		{
			return *this = static_cast<bool>(Other);
		}

		// Assignment operator.
		// Changes the value of the referenced bit in the stack.
		FBitRef& operator = (bool Value)
		{
			Word = Value ? (Word | Mask) : (Word & ~Mask);
			return *this;
		}

	private:
		WordType& Word;
		const WordType Mask;
	};

	// Constructor.
	TBitStack() = default;
	// Copy constructor.
	TBitStack(const TBitStack& Other) = default;
	// Move constructor.
	TBitStack(TBitStack&& Other) : Words{std::move(Other.Words)}, Count{Other.Count}
	{
		if (&Other != this)
		{
			Other.Count = 0;
		}
	}
	// Destructor.
	~TBitStack() = default;
	// Copy assignment operator.
	TBitStack& operator=(const TBitStack& Other) = default;
	// Move assignment operator.
	TBitStack& operator=(TBitStack&& Other)
	{
		if (&Other != this)
		{
			Words = std::move(Other.Words);
			Count = Other.Count;
			Other.Count = 0;
		}
		return *this;
	}
	
	// Clears all the items from the stack, preserving the capacity.
	void Clear()
	{
		Words.Clear();
		Count = 0;
	}

	// Clears all the items from the stack, freeing all heap allocations and
	// resetting the capacity to InlineCapacity
	void Reset()
	{
		Words.Reset();
		Count = 0;
	}

	// Pushes a new bit on to the stack.
	inline void Push(bool Bit)
	{
		const WordType WordBitIndex = Count & (NumWordBits - 1);
		if (WordBitIndex == 0)
		{
			Words.Push(Bit ? 1 : 0);
		}
		else
		{
			WordType& Word = Words.Back();
			WordType Mask = static_cast<WordType>(1) << WordBitIndex;
			Word = Bit ? (Word | Mask) : (Word & ~Mask);
		}
		++Count;
		AUTORTFM_ASSERT(Validation == EContainerValidation::Disabled || NumWordsFor(Count) == Words.Num());
	}

	// Removes the last item on the stack.
	inline bool Pop()
	{
		AUTORTFM_ASSERT(Validation == EContainerValidation::Disabled || Count > 0);
		--Count;
		const WordType WordBitIndex = Count & (NumWordBits - 1);
		const WordType Word = Words.Back();
		const bool Value = ((Word >> WordBitIndex) & 1) != 0;
		if (WordBitIndex == 0)
		{
			Words.Pop();
		}
		AUTORTFM_ASSERT(Validation == EContainerValidation::Disabled || NumWordsFor(Count) == Words.Num());
		return Value;
	}

	// Reserves memory for NewCapacity bits.
	inline void Reserve(size_t NewCapacity)
	{
		Words.Reserve(NumWordsFor(NewCapacity));
	}

	// Returns the number of bits held by the stack.
	inline size_t Num() const { return Count; }

	// Returns true if the stack holds no bits.
	inline bool IsEmpty() const { return Count == 0; }

	// Index operator.
	// Returns a reference to the bit with the given index in the stack.
	FBitRef operator[](size_t Index)
	{
		AUTORTFM_ASSERT(Validation == EContainerValidation::Disabled || Index < Count);
		WordType& Word = Words[Index / NumWordBits];
		WordType Mask = static_cast<WordType>(1) << (Index & (NumWordBits-1));
		return FBitRef{Word, Mask};
	}

	// Constant index operator.
	// Returns true if the bit with the given index is set.
	bool operator[](size_t Index) const
	{
		AUTORTFM_ASSERT(Validation == EContainerValidation::Disabled || Index < Count);
		WordType Word = Words[Index / NumWordBits];
		WordType Mask = static_cast<WordType>(1) << (Index & (NumWordBits-1));
		return (Word & Mask) != 0;
	}

private:
	static constexpr size_t NumWordsFor(size_t NumBits)
	{
		return (NumBits + (NumWordBits-1)) / NumWordBits;
	}

	TStack<WordType, NumWordsFor(InlineCapacity), Validation> Words;
	size_t Count = 0; // In bits
};

}

#endif // (defined(__AUTORTFM) && __AUTORTFM)
