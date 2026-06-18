module;
#include <cassert>

#define check(Condition, ...) assert(Condition)
#define checkf(Condition, ...) assert(Condition)

export module UEBabyPramInsightAlgo;

import std;

#define UE_REWRITE

namespace UE
{
	template <typename T, typename U>
	concept CSameAs = std::is_same_v<T, U>&& std::is_same_v<U, T>;
}

template <typename T = void>
struct TLess
{
	[[nodiscard]] UE_REWRITE constexpr bool operator()(const T& A, const T& B) const
	{
		if constexpr (requires { { A < B } -> UE::CSameAs<bool>; })
		{
			return A < B;
		}
		else
		{
			static_assert(sizeof(T) == 0, "Trying to use TLess<T> where T doesn't have an appropriate operator< overload. Please add bool operator<(T, T), do not specialize TLess.");
		}
	}
};

template <>
struct TLess<void>
{
	template <typename T, typename U>
	[[nodiscard]] UE_REWRITE constexpr bool operator()(T&& A, U&& B) const
	{
		if constexpr (requires { { Forward<T>(A) < Forward<U>(B) } -> UE::CSameAs<bool>; })
		{
			return Forward<T>(A) < Forward<U>(B);
		}
		else
		{
			static_assert(sizeof(T) == 0, "Trying to use TLess<void> with types without an appropriate operator< overload. Please add bool operator<(T, U), do not specialize TLess.");
		}
	}
};

#define UE_FORCEINLINE_HINT

export struct FIdentityFunctor
{
	template <typename T>
	UE_FORCEINLINE_HINT constexpr T&& operator()(T&& Val) const
	{
		return (T&&)Val;
	}
};

export namespace FMath
{
	float Loge(float Value) { return std::logf(Value); }
	double Loge(double Value) { return std::log(Value); }
}

export namespace UEBabyPram::InsightParser
{
	template <typename T>
	constexpr inline void Swap(T& A, T& B)
	{
		// std::is_swappable isn't correct here, because we allow bitwise swapping of types containing e.g. const and reference members,
		// but we don't want to allow swapping of types which are UE_NONCOPYABLE or equivalent.  We also allow bitwise swapping of arrays, so
		// extents should be removed first.
		static_assert(std::is_move_constructible_v<std::remove_all_extents_t<T>>, "Cannot swap non-movable types");

		if constexpr (std::is_pod_v<T>)
		{
			struct FAlignedBytes
			{
				alignas(T) char Bytes[sizeof(T)];
			};

			FAlignedBytes Temp;
			*(FAlignedBytes*)&Temp = *(FAlignedBytes*)&A;
			*(FAlignedBytes*)&A = *(FAlignedBytes*)&B;
			*(FAlignedBytes*)&B = *(FAlignedBytes*)&Temp;
		}
		else
		{
			T Temp = MoveTemp(A);
			A = MoveTemp(B);
			B = MoveTemp(Temp);
		}
	}
}



export namespace AlgoImpl
{
	using uint32 = std::uint32_t;

	using namespace UEBabyPram::InsightParser;
	/**
	 * Performs binary search, resulting in position of the first element >= Value
	 *
	 * @param First Pointer to array
	 * @param Num Number of elements in array
	 * @param Value Value to look for
	 * @param Projection Called on values in array to get type that can be compared to Value
	 * @param SortPredicate Predicate for sort comparison
	 *
	 * @returns Position of the first element >= Value, may be == Num
	 */
	template <typename RangeValueType, typename SizeType, typename PredicateValueType, typename ProjectionType, typename SortPredicateType>
	SizeType LowerBoundInternal(RangeValueType* First, const SizeType Num, const PredicateValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate)
	{
		// Current start of sequence to check
		SizeType Start = 0;
		// Size of sequence to check
		SizeType Size = Num;

		// With this method, if Size is even it will do one more comparison than necessary, but because Size can be predicted by the CPU it is faster in practice
		while (Size > 0)
		{
			const SizeType LeftoverSize = Size % 2;
			Size = Size / 2;

			const SizeType CheckIndex = Start + Size;
			const SizeType StartIfLess = CheckIndex + LeftoverSize;

			auto&& CheckValue = Invoke(Projection, First[CheckIndex]);
			Start = SortPredicate(CheckValue, Value) ? StartIfLess : Start;
		}
		return Start;
	}

	/**
	 * Performs binary search, resulting in position of the first element that is larger than the given value
	 *
	 * @param First Pointer to array
	 * @param Num Number of elements in array
	 * @param Value Value to look for
	 * @param SortPredicate Predicate for sort comparison
	 *
	 * @returns Position of the first element > Value, may be == Num
	 */
	template <typename RangeValueType, typename SizeType, typename PredicateValueType, typename ProjectionType, typename SortPredicateType>
	SizeType UpperBoundInternal(RangeValueType* First, const SizeType Num, const PredicateValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate)
	{
		// Current start of sequence to check
		SizeType Start = 0;
		// Size of sequence to check
		SizeType Size = Num;

		// With this method, if Size is even it will do one more comparison than necessary, but because Size can be predicted by the CPU it is faster in practice
		while (Size > 0)
		{
			const SizeType LeftoverSize = Size % 2;
			Size = Size / 2;

			const SizeType CheckIndex = Start + Size;
			const SizeType StartIfLess = CheckIndex + LeftoverSize;

			auto&& CheckValue = Invoke(Projection, First[CheckIndex]);
			Start = !SortPredicate(Value, CheckValue) ? StartIfLess : Start;
		}

		return Start;
	}

	template <typename IndexType>
	IndexType HeapGetParentIndex(IndexType Index)
	{
		return (Index - 1) / 2;
	}

	template <typename IndexType>
	IndexType HeapGetLeftChildIndex(IndexType Index)
	{
		return Index * 2 + 1;
	}

	template <typename IndexType>
	bool HeapIsLeaf(IndexType Index, IndexType Count)
	{
		return HeapGetLeftChildIndex(Index) >= Count;
	}

	template <typename RangeValueType, typename IndexType, typename ProjectionType, typename PredicateType>
	inline void HeapSiftDown(RangeValueType* Heap, IndexType Index, const IndexType Count, const ProjectionType& InProj, const PredicateType& Predicate)
	{
		// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
		const ProjectionType& Proj = InProj;
#else
		auto&& Proj = Projection(InProj);
#endif
		while (!HeapIsLeaf(Index, Count))
		{
			const IndexType LeftChildIndex = HeapGetLeftChildIndex(Index);
			const IndexType RightChildIndex = LeftChildIndex + 1;

			IndexType MinChildIndex = LeftChildIndex;
			if (RightChildIndex < Count)
			{
				// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
				MinChildIndex = Predicate(Invoke(Proj, Heap[LeftChildIndex]), Invoke(Proj, Heap[RightChildIndex])) ? LeftChildIndex : RightChildIndex;
#else
				MinChildIndex = Predicate(Proj(Heap[LeftChildIndex]), Proj(Heap[RightChildIndex])) ? LeftChildIndex : RightChildIndex;
#endif
			}

#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
			if (!Predicate(Invoke(Proj, Heap[MinChildIndex]), Invoke(Proj, Heap[Index])))
#else
			if (!Predicate(Proj(Heap[MinChildIndex]), Proj(Heap[Index])))
#endif
			{
				break;
			}

			Swap(Heap[Index], Heap[MinChildIndex]);
			Index = MinChildIndex;
		}
	}

	template <class RangeValueType, typename IndexType, typename ProjectionType, class PredicateType>
	inline IndexType HeapSiftUp(RangeValueType* Heap, IndexType RootIndex, IndexType NodeIndex, const ProjectionType& InProj, const PredicateType& Predicate)
	{
		// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
		const ProjectionType& Proj = InProj;
#else
		auto&& Proj = Projection(InProj);
#endif
		while (NodeIndex > RootIndex)
		{
			IndexType ParentIndex = HeapGetParentIndex(NodeIndex);
			// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
			if (!Predicate(Invoke(Proj, Heap[NodeIndex]), Invoke(Proj, Heap[ParentIndex])))
#else
			if (!Predicate(Proj(Heap[NodeIndex]), Proj(Heap[ParentIndex])))
#endif
			{
				break;
			}

			Swap(Heap[NodeIndex], Heap[ParentIndex]);
			NodeIndex = ParentIndex;
		}

		return NodeIndex;
	}

	template <typename RangeValueType, typename IndexType, typename ProjectionType, typename PredicateType>
	inline void HeapifyInternal(RangeValueType* First, IndexType Num, ProjectionType Proj, PredicateType Predicate)
	{
		if constexpr (std::is_signed_v<IndexType>)
		{
			checkf(Num >= 0, TEXT("Algo::HeapifyInternal called with negative count"));
		}

		if (Num == 0)
		{
			return;
		}

		IndexType Index = HeapGetParentIndex(Num - 1);
		for (;;)
		{
			HeapSiftDown(First, Index, Num, Proj, Predicate);
			if (Index == 0)
			{
				return;
			}
			--Index;
		}
	}

	using int32 = std::int32_t;

	template <typename T>
	int32 RotateInternal(T* First, int32 Num, int32 Count)
	{
		if (Count == 0)
		{
			return Num;
		}

		if (Count >= Num)
		{
			return 0;
		}

		T* Iter = First;
		T* Mid = First + Count;
		T* End = First + Num;

		T* OldMid = Mid;
		for (;;)
		{
			Swap(*Iter++, *Mid++);
			if (Mid == End)
			{
				if (Iter == OldMid)
				{
					return Num - Count;
				}

				Mid = OldMid;
			}
			else if (Iter == OldMid)
			{
				OldMid = Mid;
			}
		}
	}

	inline constexpr int32 MinMergeSubgroupSize = 2;

	template <typename T, typename ProjectionType, typename PredicateType>
	void Merge(T* First, int32 Mid, int32 Num, ProjectionType Projection, PredicateType Predicate)
	{
		int32 AStart = 0;
		int32 BStart = Mid;

		while (AStart < BStart && BStart < Num)
		{
			int32 NewAOffset = AlgoImpl::UpperBoundInternal(First + AStart, BStart - AStart, Invoke(Projection, First[BStart]), Projection, Predicate);
			AStart += NewAOffset;

			if (AStart >= BStart)
			{
				return;
			}

			int32 NewBOffset = AlgoImpl::LowerBoundInternal(First + BStart, Num - BStart, Invoke(Projection, First[AStart]), Projection, Predicate);
			AlgoImpl::RotateInternal(First + AStart, NewBOffset + BStart - AStart, BStart - AStart);
			BStart += NewBOffset;
			AStart += NewBOffset + 1;
		}
	}

	template <typename T, typename ProjectionType, typename PredicateType>
	void StableSortInternal(T* First, int32 Num, ProjectionType Projection, PredicateType Predicate)
	{
		int32 SubgroupStart = 0;

		if constexpr (MinMergeSubgroupSize > 1)
		{
			if constexpr (MinMergeSubgroupSize > 2)
			{
				// First pass with simple bubble-sort.
				do
				{
					int32 GroupEnd = SubgroupStart + MinMergeSubgroupSize;
					if (Num < GroupEnd)
					{
						GroupEnd = Num;
					}
					do
					{
						for (int32 It = SubgroupStart; It < GroupEnd - 1; ++It)
						{
							if (Invoke(Predicate, Invoke(Projection, First[It + 1]), Invoke(Projection, First[It])))
							{
								Swap(First[It], First[It + 1]);
							}
						}
						GroupEnd--;
					} while (GroupEnd - SubgroupStart > 1);

					SubgroupStart += MinMergeSubgroupSize;
				} while (SubgroupStart < Num);
			}
			else
			{
				for (int32 Subgroup = 0; Subgroup < Num; Subgroup += 2)
				{
					if (Subgroup + 1 < Num && Invoke(Predicate, Invoke(Projection, First[Subgroup + 1]), Invoke(Projection, First[Subgroup])))
					{
						Swap(First[Subgroup], First[Subgroup + 1]);
					}
				}
			}
		}

		int32 SubgroupSize = MinMergeSubgroupSize;
		while (SubgroupSize < Num)
		{
			SubgroupStart = 0;
			do
			{
				int32 MergeNum = SubgroupSize << 1;
				if (Num - SubgroupStart < MergeNum)
				{
					MergeNum = Num - SubgroupStart;
				}

				Merge(First + SubgroupStart, SubgroupSize, MergeNum, Projection, Predicate);
				SubgroupStart += SubgroupSize << 1;
			} while (SubgroupStart < Num);

			SubgroupSize <<= 1;
		}
	}

	template <typename T, typename IndexType, typename ProjectionType, typename PredicateType>
	void IntroSortInternal(T* First, IndexType Num, ProjectionType Proj, PredicateType Predicate)
	{
		struct FStack
		{
			T* Min;
			T* Max;
			uint32 MaxDepth;
		};

		if (Num < 2)
		{
			return;
		}

		FStack RecursionStack[32] = { {First, First + Num - 1, (uint32)(FMath::Loge((float)Num) * 2.f)} }, Current, Inner;
		for (FStack* StackTop = RecursionStack; StackTop >= RecursionStack; --StackTop) //-V625
		{
			Current = *StackTop;

		Loop:
			IndexType Count = (IndexType)(Current.Max - Current.Min + 1);

			if (Current.MaxDepth == 0)
			{
				// We're too deep into quick sort, switch to heap sort
				HeapSortInternal(Current.Min, Count, Proj, Predicate);
				continue;
			}

			if (Count <= 8)
			{
				// Use simple bubble-sort.
				while (Current.Max > Current.Min)
				{
					T* Max, * Item;
					for (Max = Current.Min, Item = Current.Min + 1; Item <= Current.Max; Item++)
					{
						// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
						if (Invoke(Predicate, Invoke(Proj, *Max), Invoke(Proj, *Item)))
#else
						if (Predicate(Proj(*Max), Proj(*Item)))
#endif
						{
							Max = Item;
						}
					}
					Swap(*Max, *Current.Max--);
				}
			}
			else
			{
				// Grab middle element so sort doesn't exhibit worst-cast behavior with presorted lists.
				Swap(Current.Min[Count / 2], Current.Min[0]);

				// Divide list into two halves, one with items <=Current.Min, the other with items >Current.Max.
				Inner.Min = Current.Min;
				Inner.Max = Current.Max + 1;
				for (; ; )
				{
					// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
					while (++Inner.Min <= Current.Max && !Invoke(Predicate, Invoke(Proj, *Current.Min), Invoke(Proj, *Inner.Min)));
					while (--Inner.Max > Current.Min && !Invoke(Predicate, Invoke(Proj, *Inner.Max), Invoke(Proj, *Current.Min)));
#else
					while (++Inner.Min <= Current.Max && !Predicate(Proj(*Current.Min), Proj(*Inner.Min)));
					while (--Inner.Max > Current.Min && !Predicate(Proj(*Inner.Max), Proj(*Current.Min)));
#endif
					if (Inner.Min > Inner.Max)
					{
						break;
					}
					Swap(*Inner.Min, *Inner.Max);
				}
				Swap(*Current.Min, *Inner.Max);

				--Current.MaxDepth;

				// Save big half and recurse with small half.
				if (Inner.Max - 1 - Current.Min >= Current.Max - Inner.Min)
				{
					if (Current.Min + 1 < Inner.Max)
					{
						StackTop->Min = Current.Min;
						StackTop->Max = Inner.Max - 1;
						StackTop->MaxDepth = Current.MaxDepth;
						StackTop++;
					}
					if (Current.Max > Inner.Min)
					{
						Current.Min = Inner.Min;
						goto Loop;
					}
				}
				else
				{
					if (Current.Max > Inner.Min)
					{
						StackTop->Min = Inner.Min;
						StackTop->Max = Current.Max;
						StackTop->MaxDepth = Current.MaxDepth;
						StackTop++;
					}
					if (Current.Min + 1 < Inner.Max)
					{
						Current.Max = Inner.Max - 1;
						goto Loop;
					}
				}
			}
		}
	}
}

export namespace Algo
{
	/**
	 * Performs binary search, resulting in position of the first element >= Value using predicate
	 *
	 * @param Range Range to search through, must be already sorted by SortPredicate
	 * @param Value Value to look for
	 * @param SortPredicate Predicate for sort comparison, defaults to <
	 *
	 * @returns Position of the first element >= Value, may be position after last element in range
	 */
	template <typename RangeType, typename ValueType, typename SortPredicateType>
	[[nodiscard]] UE_REWRITE auto LowerBound(const RangeType& Range, const ValueType& Value, SortPredicateType SortPredicate) -> std::int32_t
	{
		return AlgoImpl::LowerBoundInternal(Range.GetData(), Range.Num(), Value, FIdentityFunctor(), SortPredicate);
	}
	template <typename RangeType, typename ValueType>
	[[nodiscard]] UE_REWRITE auto LowerBound(const RangeType& Range, const ValueType& Value) -> std::int32_t
	{
		return AlgoImpl::LowerBoundInternal(Range.GetData(), Range.Num(), Value, FIdentityFunctor(), TLess<>());
	}

	/**
	 * Performs binary search, resulting in position of the first element with projected value >= Value using predicate
	 *
	 * @param Range Range to search through, must be already sorted by SortPredicate
	 * @param Value Value to look for
	 * @param Projection Functor or data member pointer, called via Invoke to compare to Value
	 * @param SortPredicate Predicate for sort comparison, defaults to <
	 *
	 * @returns Position of the first element >= Value, may be position after last element in range
	 */
	template <typename RangeType, typename ValueType, typename ProjectionType, typename SortPredicateType>
	[[nodiscard]] UE_REWRITE auto LowerBoundBy(const RangeType& Range, const ValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate) -> decltype(Range.Num())
	{
		return AlgoImpl::LowerBoundInternal(Range.GetData(), Range.Num(), Value, Projection, SortPredicate);
	}
	template <typename RangeType, typename ValueType, typename ProjectionType>
	[[nodiscard]] UE_REWRITE auto LowerBoundBy(const RangeType& Range, const ValueType& Value, ProjectionType Projection) -> decltype(Range.Num())
	{
		return AlgoImpl::LowerBoundInternal(Range.GetData(), Range.Num(), Value, Projection, TLess<>());
	}

	/**
		 * Sort a range of elements using its operator<. The sort is unstable.
		 *
		 * @param Range	The range to sort.
		 */
	template <typename RangeType>
	UE_REWRITE void IntroSort(RangeType&& Range)
	{
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), FIdentityFunctor(), TLess<>());
	}

	/**
	 * Sort a range of elements using a user-defined predicate class. The sort is unstable.
	 *
	 * @param Range		The range to sort.
	 * @param Predicate	A binary predicate object used to specify if one element should precede another.
	 */
	template <typename RangeType, typename PredicateType>
	UE_REWRITE void IntroSort(RangeType&& Range, PredicateType Predicate)
	{
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), FIdentityFunctor(), MoveTemp(Predicate));
	}

	template <typename RangeType>
	UE_REWRITE void Sort(RangeType&& Range)
	{
		IntroSort(std::forward<RangeType>(Range));
	}

	template <typename RangeType, typename ProjectionType>
	UE_REWRITE void IntroSortBy(RangeType&& Range, ProjectionType Proj)
	{
		// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), MoveTemp(Proj), TLess<>());
#else
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), Projection(MoveTemp(Proj)), TLess<>());
#endif
	}

	/**
	 * Sort a range of elements by a projection using a user-defined predicate class. The sort is unstable.
	 *
	 * @param Range			The range to sort.
	 * @param Proj			The projection to sort by when applied to the element.
	 * @param Predicate		A binary predicate object, applied to the projection, used to specify if one element should precede another.
	 */
	template <typename RangeType, typename ProjectionType, typename PredicateType>
	UE_REWRITE void IntroSortBy(RangeType&& Range, ProjectionType Proj, PredicateType Predicate)
	{
		// Workaround for a codegen bug that was discovered related to member function pointer projections that are virtual
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER < 194134123
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), MoveTemp(Proj), MoveTemp(Predicate));
#else
		AlgoImpl::IntroSortInternal(GetData(Range), GetNum(Range), Projection(MoveTemp(Proj)), Projection(MoveTemp(Predicate)));
#endif
	}

	/**
	 * Sort a range of elements using a user-defined predicate class.  The sort is unstable.
	 *
	 * @param  Range      The range to sort.
	 * @param  Predicate  A binary predicate object used to specify if one element should precede another.
	 */
	template <typename RangeType, typename PredicateType>
	UE_REWRITE void Sort(RangeType&& Range, PredicateType Pred)
	{
		IntroSort(Forward<RangeType>(Range), MoveTemp(Pred));
	}

	/**
	 * Sort a range of elements by a projection using the projection's operator<.  The sort is unstable.
	 *
	 * @param  Range  The range to sort.
	 * @param  Proj   The projection to sort by when applied to the element.
	 */
	template <typename RangeType, typename ProjectionType>
	UE_REWRITE void SortBy(RangeType&& Range, ProjectionType Proj)
	{
		IntroSortBy(std::forward<RangeType>(Range), std::move(Proj));
	}

	template <typename RangeType, typename PredicateType>
	UE_REWRITE void Heapify(RangeType&& Range, PredicateType Predicate)
	{
		AlgoImpl::HeapifyInternal(Range.Data(), Range.Num(), FIdentityFunctor(), Predicate);
	}

	template <typename RangeType>
	UE_REWRITE void StableSort(RangeType&& Range)
	{
		AlgoImpl::StableSortInternal(Range.GetData(), Range.Num(), FIdentityFunctor(), TLess<>());
	}

	template <typename RangeType, typename PredicateType>
	UE_REWRITE void StableSort(RangeType&& Range, PredicateType Pred)
	{
		AlgoImpl::StableSortInternal(Range.data(), Range.size(), FIdentityFunctor(), std::move(Pred));
	}
}