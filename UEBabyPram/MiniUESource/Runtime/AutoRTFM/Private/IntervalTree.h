// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "BitStack.h"
#include "ContainerValidation.h"
#include "Stack.h"
#include "Utils.h"

namespace AutoRTFM
{

struct FIntervalTree final
{
    FIntervalTree() = default;

    FIntervalTree(const FIntervalTree&) = delete;
    FIntervalTree& operator=(const FIntervalTree&) = delete;

	UE_AUTORTFM_FORCENOINLINE bool Insert(const void* const Address, const size_t Size)
    {        
        FRange NewRange(Address, Size);
        return Insert(std::move(NewRange));
    }

    UE_AUTORTFM_FORCENOINLINE bool Contains(const void* const Address, const size_t Size) const
    {
        const FRange NewRange(Address, Size);

        if (AUTORTFM_UNLIKELY(IntervalTreeNodeIndexNone == Root))
        {
            return false;
        }

        FIntervalTreeNodeIndex Current = Root;

        do
        {
            const FRange Range = NodeRanges[Current];

            // This check does not need to prove that NewRange is entirely
            // enclosed within Range, because if any byte of NewRange was in the
            // original Range then it **must** already have been new memory.
            if ((NewRange.Start < Range.End) && (Range.Start < NewRange.End))
            {
                return true;
            }

			const StackType<FIntervalTreeNodeIndex>& Next = (NewRange.Start < Range.End) ? NodeLefts : NodeRights;
			Current = Next[Current];
        } while (IntervalTreeNodeIndexNone != Current);

        return false;
    }

    bool IsEmpty() const
    {
        return IntervalTreeNodeIndexNone == Root;
    }

    void Reset()
    {
        Root = IntervalTreeNodeIndexNone;
		NodeRanges.Reset();
		NodeLefts.Reset();
		NodeRights.Reset();
		NodeParents.Reset();
		NodeColors.Reset();
    }

    void Merge(const FIntervalTree& Other)
    {
        if (Other.IsEmpty())
        {
            return;
        }

		StackType<FIntervalTreeNodeIndex> ToProcess;

        ToProcess.Push(Other.Root);

        do
        {
            const FIntervalTreeNodeIndex Current = ToProcess.Back();
            ToProcess.Pop();

            FRange Range = Other.NodeRanges[Current];

            Insert(std::move(Range));

            const FIntervalTreeNodeIndex Left = Other.NodeLefts[Current];
            const FIntervalTreeNodeIndex Right = Other.NodeRights[Current];

            if (IntervalTreeNodeIndexNone != Left)
            {
                ToProcess.Push(Left);
            }

            if (IntervalTreeNodeIndexNone != Right)
            {
                ToProcess.Push(Right);
            }
        } while (!ToProcess.IsEmpty());
    }

	// Calls Callback with each interval in the tree, in order of address.
	// Callback must have the signature: Callback(uintptr_t Start, uintptr_t End)
	template<typename CALLBACK>
	void ForEach(CALLBACK&& Callback) const
	{
		if (Root != IntervalTreeNodeIndexNone)
		{
			ForEach(Root, Callback);
		}
	}

private:
	static constexpr size_t InlineArraySize = 8;
	template<typename T> using StackType = TStack<T, InlineArraySize, EContainerValidation::Disabled>;
	using BitStackType = TBitStack<InlineArraySize, EContainerValidation::Disabled>;

    struct FRange final
    {
        FRange(const void* const Address, const size_t Size) :
            Start(reinterpret_cast<uintptr_t>(Address)), End(reinterpret_cast<uintptr_t>(Address) + Size) {}

        uintptr_t Start;
        uintptr_t End;
    };

    using FIntervalTreeNodeIndex = uint32_t;

    static constexpr FIntervalTreeNodeIndex IntervalTreeNodeIndexNone = UINT32_MAX;

	enum EColor : bool
	{
		Red = false,
		Black = true
	};

    FIntervalTreeNodeIndex Root = IntervalTreeNodeIndexNone;
	StackType<FRange> NodeRanges;
	StackType<FIntervalTreeNodeIndex> NodeLefts;
	StackType<FIntervalTreeNodeIndex> NodeRights;
	StackType<FIntervalTreeNodeIndex> NodeParents;
	BitStackType NodeColors;

	static constexpr bool bExtraDebugging = false;

	template<typename CALLBACK>
	void ForEach(FIntervalTreeNodeIndex Index, CALLBACK&& Callback) const
	{
		if (FIntervalTreeNodeIndex Left = NodeLefts[Index]; Left != IntervalTreeNodeIndexNone)
		{
			ForEach(Left, Callback);
		}
		Callback(NodeRanges[Index].Start, NodeRanges[Index].End);
		if (FIntervalTreeNodeIndex Right = NodeRights[Index]; Right != IntervalTreeNodeIndexNone)
		{
			ForEach(Right, Callback);
		}
	}

	UE_AUTORTFM_FORCEINLINE FIntervalTreeNodeIndex AddNode(FRange&& NewRange)
	{
		const FIntervalTreeNodeIndex Index = static_cast<FIntervalTreeNodeIndex>(NodeRanges.Num());
        NodeRanges.Push(NewRange);
		NodeLefts.Push(IntervalTreeNodeIndexNone);
		NodeRights.Push(IntervalTreeNodeIndexNone);
		NodeParents.Push(IntervalTreeNodeIndexNone);
		NodeColors.Push(EColor::Black);
		return Index;
	}

	UE_AUTORTFM_FORCEINLINE FIntervalTreeNodeIndex AddNode(FRange&& NewRange, FIntervalTreeNodeIndex Parent)
	{
		const FIntervalTreeNodeIndex Index = static_cast<FIntervalTreeNodeIndex>(NodeRanges.Num());
        NodeRanges.Push(NewRange);
		NodeLefts.Push(IntervalTreeNodeIndexNone);
		NodeRights.Push(IntervalTreeNodeIndexNone);
		NodeParents.Push(Parent);
		NodeColors.Push(EColor::Red);
		return Index;
	}

    UE_AUTORTFM_FORCENOINLINE bool Insert(FRange&& NewRange)
    {
        if (AUTORTFM_UNLIKELY(IntervalTreeNodeIndexNone == Root))
        {
            AUTORTFM_ASSERT(NodeRanges.IsEmpty());
			Root = AddNode(std::move(NewRange));
            return true;
        }

        FIntervalTreeNodeIndex Current = Root;

        for(;;)
        {
            const FRange Range = NodeRanges[Current];

            if (AUTORTFM_UNLIKELY((NewRange.Start < Range.End) && (Range.Start < NewRange.End)))
            {
                return false;
            }

            if (NewRange.Start < Range.Start)
            {
                if (AUTORTFM_UNLIKELY(NewRange.End == Range.Start))
                {
                    AUTORTFM_ASSERT(NewRange.Start < Range.Start);

					// We can just modify the existing node in place.
					NodeRanges[Current].Start = NewRange.Start;
                    return true;
                }
                else if (IntervalTreeNodeIndexNone == NodeLefts[Current])
                {
					const FIntervalTreeNodeIndex Index = AddNode(std::move(NewRange), Current);
					NodeLefts[Current] = Index;
                    Current = Index;
                    break;
                }

                Current = NodeLefts[Current];
            }
            else
            {
                if (AUTORTFM_UNLIKELY(NewRange.Start == Range.End))
                {
                    AUTORTFM_ASSERT(NewRange.End > Range.End);

					// We can just modify the existing node in place.
					NodeRanges[Current].End = NewRange.End;
                    return true;
                }
                else if (IntervalTreeNodeIndexNone == NodeRights[Current])
                {
                    const FIntervalTreeNodeIndex Index = AddNode(std::move(NewRange), Current);
					NodeRights[Current] = Index;
                    Current = Index;
                    break;
                }

                Current = NodeRights[Current];
            }

            AUTORTFM_ASSERT(Root != Current);
        }

        auto IsBlack = [this](FIntervalTreeNodeIndex Index)
        {
			return (IntervalTreeNodeIndexNone == Index) || (EColor::Black == NodeColors[Index]);
        };

        for(;;)
        {
            FIntervalTreeNodeIndex Parent = NodeParents[Current];

            UE_AUTORTFM_ASSUME(Current != Parent);

            // The root will always have a black parent, so this check covers both.
            if (Parent == Root)
            {
                NodeColors[Parent] = EColor::Black;
                break;
            }
            else if (IsBlack(Parent))
            {
                break;
            }

            const FIntervalTreeNodeIndex GrandParent = NodeParents[Parent];

            UE_AUTORTFM_ASSUME((Parent != GrandParent) && (Current != GrandParent));

            const bool bParentIsLeft = NodeLefts[GrandParent] == Parent;

            // The uncle is the other node of our parent.
            const FIntervalTreeNodeIndex Uncle = bParentIsLeft ? NodeRights[GrandParent] : NodeLefts[GrandParent];

            UE_AUTORTFM_ASSUME((GrandParent != Uncle) && (Parent != Uncle) && (Current != Uncle));

            if (!IsBlack(Uncle))
            {
                AUTORTFM_ASSERT(IsBlack(GrandParent));
                NodeColors[Parent] = EColor::Black;
                NodeColors[Uncle] = EColor::Black;

                if (GrandParent == Root)
                {
                    break;
                }
                else
                {
					NodeColors[GrandParent] = EColor::Red;
                }

                Current = GrandParent;
                continue;
            }

            // Our uncle is black, so we need to swizzle around.
            const bool bCurrentIsLeft = NodeLefts[Parent] == Current;

            if (bParentIsLeft)
            {
                if (!bCurrentIsLeft)
                {
                    NodeRights[Parent] = NodeLefts[Current];

                    if (IntervalTreeNodeIndexNone != NodeRights[Parent])
                    {
                        NodeParents[NodeRights[Parent]] = Parent;
                    }

                    NodeParents[Parent] = Current;
                    NodeParents[Current] = GrandParent;
                    NodeLefts[Current] = Parent;
                    NodeLefts[GrandParent] = Current;
                    std::swap(Parent, Current);
                }

                NodeParents[Parent] = NodeParents[GrandParent];
                NodeLefts[GrandParent] = NodeRights[Parent];

                if (IntervalTreeNodeIndexNone != NodeLefts[GrandParent])
                {
                    NodeParents[NodeLefts[GrandParent]] = GrandParent;
                }

                NodeRights[Parent] = GrandParent;
                NodeParents[GrandParent] = Parent;

				const bool bGrandParentIsBlack = NodeColors[GrandParent];
				NodeColors[GrandParent] = NodeColors[Parent];
				NodeColors[Parent] = bGrandParentIsBlack;
            }
            else
            {
                if (bCurrentIsLeft)
                {
                    NodeLefts[Parent] = NodeRights[Current];
                    
                    if (IntervalTreeNodeIndexNone != NodeLefts[Parent])
                    {
                        NodeParents[NodeLefts[Parent]] = Parent;
                    }

                    NodeParents[Parent] = Current;
                    NodeParents[Current] = GrandParent;
                    NodeRights[Current] = Parent;
                    NodeRights[GrandParent] = Current;
                    std::swap(Parent, Current);
                }

                NodeParents[Parent] = NodeParents[GrandParent];
                NodeRights[GrandParent] = NodeLefts[Parent];

                if (IntervalTreeNodeIndexNone != NodeRights[GrandParent])
                {
                    NodeParents[NodeRights[GrandParent]] = GrandParent;
                }

                NodeLefts[Parent] = GrandParent;
                NodeParents[GrandParent] = Parent;

				const bool bGrandParentIsBlack = NodeColors[GrandParent];
				NodeColors[GrandParent] = NodeColors[Parent];
				NodeColors[Parent] = bGrandParentIsBlack;
            }

            if (IntervalTreeNodeIndexNone == NodeParents[Parent])
            {
                Root = Parent;
            }
            else
            {
				const FIntervalTreeNodeIndex GreatGrandParent = NodeParents[Parent];

                if (GrandParent == NodeLefts[GreatGrandParent])
                {
					NodeLefts[GreatGrandParent] = Parent;
                }
                else
                {
					NodeRights[GreatGrandParent] = Parent;
                }
            }

            break;
        }

		AssertStructureIsOk();

        return true;
    }

	UE_AUTORTFM_FORCEINLINE void AssertStructureIsOk() const
	{
		if constexpr (bExtraDebugging)
		{
			if (IntervalTreeNodeIndexNone != Root)
			{
				AssertNodeIsOk(Root);
			}
		}
	}

	UE_AUTORTFM_FORCENOINLINE void AssertNodeIsOk(FIntervalTreeNodeIndex Index) const
	{
		// We need to use recursion to check because we cannot have any
		// allocations within the checker itself!
		if constexpr (bExtraDebugging)
		{
			AUTORTFM_ASSERT(IntervalTreeNodeIndexNone != Index);
			AUTORTFM_ASSERT(Index < NodeRanges.Num());

			const FIntervalTreeNodeIndex Parent = NodeParents[Index];
			const FIntervalTreeNodeIndex Left = NodeLefts[Index];
			const FIntervalTreeNodeIndex Right = NodeRights[Index];

			if (IntervalTreeNodeIndexNone == Parent)
			{
				AUTORTFM_ASSERT(Root == Index);
			}
			else
			{
				AUTORTFM_ASSERT(NodeColors[Parent] || NodeColors[Index]);
				AUTORTFM_ASSERT((NodeLefts[Parent] == Index) ^ (NodeRights[Parent] == Index));
			}

			AUTORTFM_ASSERT(Left != Index);
			AUTORTFM_ASSERT(Right != Index);

			if (IntervalTreeNodeIndexNone != Left)
			{
				AssertNodeIsOk(Left);
			}

			if (IntervalTreeNodeIndexNone != Right)
			{
				AssertNodeIsOk(Right);
			}
		}
	}
};

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
