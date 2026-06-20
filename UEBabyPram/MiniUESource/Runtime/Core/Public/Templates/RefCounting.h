// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "AutoRTFM.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PreprocessorHelpers.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Build.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryLayout.h"
#include "Templates/Requires.h"
#include "Templates/TypeHash.h"
#include "Templates/UnrealTemplate.h"
#include <atomic>
#include <type_traits>

/**
 * Simple wrapper class which holds a refcount; emits a deprecation warning when accessed.
 * 
 * It is unsafe to rely on the value of a refcount for any logic, and a non-deprecated
 * getter function should never be added. In a multi-threaded context, the refcount could
 * change after inspection. In a transactional context, the refcount may be higher than
 * expected, as releases are deferred until completion of the transaction.
 */
struct FReturnedRefCountValue
{
	explicit FReturnedRefCountValue(uint32 InRefCount)
		: RefCount(InRefCount)	 
	{
	}
	FReturnedRefCountValue(const FReturnedRefCountValue& Other) = default;
	FReturnedRefCountValue(FReturnedRefCountValue&& Other) = default;
	FReturnedRefCountValue& operator=(const FReturnedRefCountValue& Other) = default;
	FReturnedRefCountValue& operator=(FReturnedRefCountValue&& Other) = default;

	UE_DEPRECATED(5.6, "Inspecting an object's refcount is deprecated.")
	operator uint32() const
	{
		return RefCount;
	}

	void CheckAtLeast(uint32 N) const
	{
		// It's harmless to check if your refcount is at least a certain amount. Be aware 
		// that inside an AutoRTFM transaction, Release() is deferred until commit, so an
		// object's refcount may be higher than you expected. In other words, when inside
		// of a transaction, this check may not trigger even when the number of active
		// reference holders is lower than the passed-in value.
		checkSlow(RefCount >= N);
	}

private:
	uint32 RefCount = 0;
};

/**
 * UE::Private::TTransactionalAtomicRefCount manages a transactionally-safe atomic refcount value.
 * This is used by FRefCountBase, FThreadSafeRefCountedObject and TRefCountingMixin (in thread-safe mode).
 */
namespace UE::Private
{

CORE_API void CheckRefCountIsNonZero();

template <typename AtomicType>
class TTransactionalAtomicRefCount
{
public:
	template <auto DeleteFn>
	AtomicType AddRef() const
	{
		AtomicType Refs;

		UE_AUTORTFM_OPEN
		{
			Refs = RefCount++;
		};

		// If we are inside a transaction, a rollback must undo our refcount bump.
		// In general, this is best handled by Release(). However, there is one case 
		// that needs to be handled with special care. A brand-new object has a
		// refcount of zero, and a rollback must return it to this zero-refcount state.
		// However, calling AddRef() followed by Release() would not accomplish this;
		// instead, it would free the object entirely! We need to guard against this,
		// since it could lead to a double-free, so we detect the zero-reference case
		// and special-case it.
		if (Refs == 0)
		{
			UE_AUTORTFM_ONABORT(this)
			{
				--RefCount;
				// The refcount is likely zero now, but leaving the object alive isn't a leak.
				// We are restoring the object back to its initial "just-created" state.
			};
		}
		else
		{
			UE_AUTORTFM_ONABORT(this)
			{
				Release<DeleteFn>();
			};
		}

		return Refs + 1;
	}

	template <auto DeleteFn>
	AtomicType Release() const
	{
#if DO_GUARD_SLOW
		if (RefCount == 0)
		{
			CheckRefCountIsNonZero();
		}
#endif

		AtomicType RefsToReturn;

		if (AutoRTFM::IsClosed())
		{
			// We return the active number of references minus one to maintain the existing
			// Release() behavior as closely as possible while inside a transaction, even 
			// though we are deferring reference count changes until commit time.
			// Be advised that GetRefCount() would reveal our trickery, since it
			// always returns the true refcount.
			UE_AUTORTFM_OPEN
			{
				RefsToReturn = AtomicType(RefCount.load(std::memory_order_relaxed)) - 1;
			};

			// Refcount changes and frees are deferred until the transaction is concluded.
			UE_AUTORTFM_ONCOMMIT(this)
			{
				ImmediatelyRelease<DeleteFn>();
			};
		}
		else
		{
			RefsToReturn = ImmediatelyRelease<DeleteFn>() - 1;
		}

		return RefsToReturn;
	}

	AtomicType GetRefCount() const
	{
		// This is equivalent to https://en.cppreference.com/w/cpp/memory/shared_ptr/use_count
		// 
		// Inside of an AutoRTFM transaction, the returned refcount value may be higher than you'd
		// expect, because all Release calls are deferred until the transaction commit time.
		AtomicType Refs;
		UE_AUTORTFM_OPEN
		{
			// A 'live' reference count is unstable by nature and so there's no benefit
			// to try and enforce memory ordering around the reading of it.
			Refs = RefCount.load(std::memory_order_relaxed);
		};
		return Refs;
	}

private:
	template <auto DeleteFn>
	AtomicType ImmediatelyRelease() const
	{
		// fetch_sub returns the refcount _before_ it was decremented. std::memory_order_acq_rel is
		// used so that, if we do end up executing the destructor, it's not possible for side effects 
		// from executing the destructor to end up being visible before we've determined that the 
		// reference count is actually zero.
		AtomicType RefsBeforeRelease = RefCount.fetch_sub(1, std::memory_order_acq_rel);

#if DO_GUARD_SLOW
		// A check-failure is issued if an object is over-released.
		if (RefsBeforeRelease == 0)
		{
			CheckRefCountIsNonZero();
		}
#endif
		// We immediately free the object if its refcount has become zero.
		if (RefsBeforeRelease == 1)
		{
			DeleteFn(this);
		}
		return RefsBeforeRelease;
	}

	mutable std::atomic<AtomicType> RefCount = 0;
};

}

/** A virtual interface for ref counted objects to implement. */
class IRefCountedObject
{
public:
	virtual ~IRefCountedObject() { }
	virtual FReturnedRefCountValue AddRef() const = 0;

	// TODO (SOL-7350): return FReturnedRefCountValue from Release(); clean up call sites
	// which rely on its return value.
	virtual uint32 Release() const = 0;

	// TODO (SOL-7350): mark this function as deprecated; clean up existing callers.
	virtual uint32 GetRefCount() const = 0;
};

/**
 * Base class implementing thread-safe reference counting.
 */
class FRefCountBase : UE::Private::TTransactionalAtomicRefCount<uint32>
{
public:
	FRefCountBase() = default;
	virtual ~FRefCountBase() = default;

	FRefCountBase(const FRefCountBase& Rhs) = delete;
	FRefCountBase& operator=(const FRefCountBase& Rhs) = delete;

	FReturnedRefCountValue AddRef() const
	{
		return FReturnedRefCountValue{Super::AddRef<DeleteThis>()};
	}

	uint32 Release() const
	{
		return Super::Release<DeleteThis>();
	}

	uint32 GetRefCount() const
	{
		return Super::GetRefCount();
	}

private:
	using Super = UE::Private::TTransactionalAtomicRefCount<uint32>;

	static void DeleteThis(const Super* This)
	{
		delete static_cast<const FRefCountBase*>(This);
	}
};

/**
 * The base class of reference counted objects.
 *
 * This class should not be used for new code as it does not use atomic operations to update 
 * the reference count.
 */
class FRefCountedObject
{
public:
	FRefCountedObject(): NumRefs(0) {}
	virtual ~FRefCountedObject() { check(!NumRefs); }
	FRefCountedObject(const FRefCountedObject& Rhs) = delete;
	FRefCountedObject& operator=(const FRefCountedObject& Rhs) = delete;
	FReturnedRefCountValue AddRef() const
	{
		return FReturnedRefCountValue{uint32(++NumRefs)};
	}
	uint32 Release() const
	{
		uint32 Refs = uint32(--NumRefs);
		if(Refs == 0)
		{
			delete this;
		}
		return Refs;
	}
	uint32 GetRefCount() const
	{
		return uint32(NumRefs);
	}
private:
	mutable int32 NumRefs;
};

/**
 * Like FRefCountedObject, but the reference count is thread-safe.
 */
class FThreadSafeRefCountedObject : UE::Private::TTransactionalAtomicRefCount<uint32>
{
public:
	FThreadSafeRefCountedObject() = default;

	FThreadSafeRefCountedObject(const FThreadSafeRefCountedObject& Rhs) = delete;
	FThreadSafeRefCountedObject& operator=(const FThreadSafeRefCountedObject& Rhs) = delete;

	virtual ~FThreadSafeRefCountedObject()
	{ 
		check(Super::GetRefCount() == 0); 
	}
	
	FReturnedRefCountValue AddRef() const
	{
		return FReturnedRefCountValue{Super::AddRef<DeleteThis>()};
	}

	uint32 Release() const
	{
		return Super::Release<DeleteThis>();
	}

	uint32 GetRefCount() const
	{
		return Super::GetRefCount();
	}

private:
	using Super = UE::Private::TTransactionalAtomicRefCount<uint32>;

	static void DeleteThis(const Super* This)
	{
		delete static_cast<const FThreadSafeRefCountedObject*>(This);
	}
};

/**
 * ERefCountingMode is used select between either 'fast' or 'thread safe' ref-counting types.
 * This is only used at compile time to select between template specializations.
 */
enum class ERefCountingMode : uint8
{
	/** Forced to be not thread-safe. */
	NotThreadSafe = 0,

	/** Thread-safe: never spin locks, but slower. */
	ThreadSafe = 1
};

/**
 * Ref-counting mixin, designed to add ref-counting to an object without requiring a virtual destructor.
 * Implements support for AutoRTFM, is thread-safe by default, and can support custom deleters via T::StaticDestroyObject.
 * 
 * @note AutoRTFM means that the return value of AddRef/Release can't be trusted (as the ref-count doesn't decrement until
 *       the transaction is committed), but this is fine for use with TRefCountPtr, as it doesn't use those return values.
 * 
 * Basic Example:
 *  struct FMyRefCountedObject : public TRefCountingMixin<FMyRefCountedObject>
 *  {
 *      // ...
 *  };
 * 
 * Deleter Example:
 *  struct FMyRefCountedPooledObject : public TRefCountingMixin<FMyRefCountedPooledObject>
 *  {
 *      static void StaticDestroyObject(const FMyRefCountedPooledObject* Obj)
 *      {
 *          GPool->ReturnToPool(Obj);
 *      }
 *  };
 */
template <typename T, ERefCountingMode Mode = ERefCountingMode::ThreadSafe>
class TRefCountingMixin;

/**
 * Thread-safe specialization
 */
template <typename T>
class TRefCountingMixin<T, ERefCountingMode::ThreadSafe> : UE::Private::TTransactionalAtomicRefCount<uint32>
{
public:
	TRefCountingMixin() = default;

	TRefCountingMixin(const TRefCountingMixin&) = delete;
	TRefCountingMixin& operator=(const TRefCountingMixin&) = delete;

	FReturnedRefCountValue AddRef() const
	{
		return FReturnedRefCountValue{Super::template AddRef<StaticDestroyMixin>()};
	}

	uint32 Release() const
	{
		return Super::template Release<StaticDestroyMixin>();
	}

	uint32 GetRefCount() const
	{
		return Super::GetRefCount();
	}

	static void StaticDestroyObject(const T* Obj)
	{
		delete Obj;
	}

private:
	using Super = UE::Private::TTransactionalAtomicRefCount<uint32>;

	static void StaticDestroyMixin(const Super* This)
	{
		// This static_cast is traversing two levels of the class hierarchy.
		// We are casting from our parent class (TTransactionalAtomicRefCount*) to our subclass (T*).
		T::StaticDestroyObject(static_cast<const T*>(This));
	}
};

/**
 * Not-thread-safe specialization
 */
template <typename T>
class TRefCountingMixin<T, ERefCountingMode::NotThreadSafe> 
{
public:
	TRefCountingMixin() = default;

	TRefCountingMixin(const TRefCountingMixin&) = delete;
	TRefCountingMixin& operator=(const TRefCountingMixin&) = delete;

	FReturnedRefCountValue AddRef() const
	{
		return FReturnedRefCountValue{++RefCount};
	}

	uint32 Release() const
	{
		checkSlow(RefCount > 0);

		if (--RefCount == 0)
		{
			StaticDestroyMixin(this);
		}

		// Note: TRefCountPtr doesn't use the return value
		return 0;
	}

	uint32 GetRefCount() const
	{
		return RefCount;
	}

	static void StaticDestroyObject(const T* Obj)
	{
		delete Obj;
	}

private:
	static void StaticDestroyMixin(const TRefCountingMixin* This)
	{
		T::StaticDestroyObject(static_cast<const T*>(This));
	}

	mutable uint32 RefCount;
};

/**
 * A smart pointer to an object which implements AddRef/Release.
 */
template<typename ReferencedType>
class TRefCountPtr
{
	using ReferenceType = ReferencedType*;

public:
	UE_FORCEINLINE_HINT TRefCountPtr() = default;

	TRefCountPtr(ReferencedType* InReference, bool bAddRef = true)
	{
		Reference = InReference;
		if (Reference && bAddRef)
		{
			Reference->AddRef();
		}
	}

	TRefCountPtr(const TRefCountPtr& Copy)
	{
		Reference = Copy.Reference;
		if (Reference)
		{
			Reference->AddRef();
		}
	}

	template<typename CopyReferencedType>
	explicit TRefCountPtr(const TRefCountPtr<CopyReferencedType>& Copy)
	{
		Reference = static_cast<ReferencedType*>(Copy.GetReference());
		if (Reference)
		{
			Reference->AddRef();
		}
	}

	inline TRefCountPtr(TRefCountPtr&& Move)
	{
		Reference = Move.Reference;
		Move.Reference = nullptr;
	}

	template<typename MoveReferencedType>
	explicit TRefCountPtr(TRefCountPtr<MoveReferencedType>&& Move)
	{
		Reference = static_cast<ReferencedType*>(Move.GetReference());
		Move.Reference = nullptr;
	}

	~TRefCountPtr()
	{
		if (Reference)
		{
			Reference->Release();
		}
	}

	TRefCountPtr& operator=(ReferencedType* InReference)
	{
		if (Reference != InReference)
		{
			// Call AddRef before Release, in case the new reference is the same as the old reference.
			ReferencedType* OldReference = Reference;
			Reference = InReference;
			if (Reference)
			{
				Reference->AddRef();
			}
			if (OldReference)
			{
				OldReference->Release();
			}
		}
		return *this;
	}

	UE_FORCEINLINE_HINT TRefCountPtr& operator=(const TRefCountPtr& InPtr)
	{
		return *this = InPtr.Reference;
	}

	template<typename CopyReferencedType>
	UE_FORCEINLINE_HINT TRefCountPtr& operator=(const TRefCountPtr<CopyReferencedType>& InPtr)
	{
		return *this = InPtr.GetReference();
	}

	TRefCountPtr& operator=(TRefCountPtr&& InPtr)
	{
		if (this != &InPtr)
		{
			ReferencedType* OldReference = Reference;
			Reference = InPtr.Reference;
			InPtr.Reference = nullptr;
			if (OldReference)
			{
				OldReference->Release();
			}
		}
		return *this;
	}

	template<typename MoveReferencedType>
	TRefCountPtr& operator=(TRefCountPtr<MoveReferencedType>&& InPtr)
	{
		// InPtr is a different type (or we would have called the other operator), so we need not test &InPtr != this
		ReferencedType* OldReference = Reference;
		Reference = InPtr.Reference;
		InPtr.Reference = nullptr;
		if (OldReference)
		{
			OldReference->Release();
		}
		return *this;
	}

	UE_FORCEINLINE_HINT ReferencedType* operator->() const
	{
		return Reference;
	}

	UE_FORCEINLINE_HINT operator ReferenceType() const
	{
		return Reference;
	}

	inline ReferencedType** GetInitReference()
	{
		*this = nullptr;
		return &Reference;
	}

	UE_FORCEINLINE_HINT ReferencedType* GetReference() const
	{
		return Reference;
	}

	UE_FORCEINLINE_HINT friend bool IsValidRef(const TRefCountPtr& InReference)
	{
		return InReference.Reference != nullptr;
	}

	UE_FORCEINLINE_HINT bool IsValid() const
	{
		return Reference != nullptr;
	}

	UE_FORCEINLINE_HINT void SafeRelease()
	{
		*this = nullptr;
	}

	uint32 GetRefCount()
	{
		uint32 Result = 0;
		if (Reference)
		{
			Result = Reference->GetRefCount();
			check(Result > 0); // you should never have a zero ref count if there is a live ref counted pointer (*this is live)
		}
		return Result;
	}

	inline void Swap(TRefCountPtr& InPtr) // this does not change the reference count, and so is faster
	{
		ReferencedType* OldReference = Reference;
		Reference = InPtr.Reference;
		InPtr.Reference = OldReference;
	}

	void Serialize(FArchive& Ar)
	{
		ReferenceType PtrReference = Reference;
		Ar << PtrReference;
		if(Ar.IsLoading())
		{
			*this = PtrReference;
		}
	}

private:
	ReferencedType* Reference = nullptr;

	template <typename OtherType>
	friend class TRefCountPtr;

public:
	UE_FORCEINLINE_HINT bool operator==(const TRefCountPtr& B) const
	{
		return GetReference() == B.GetReference();
	}

	UE_FORCEINLINE_HINT bool operator==(ReferencedType* B) const
	{
		return GetReference() == B;
	}
};

ALIAS_TEMPLATE_TYPE_LAYOUT(template<typename T>, TRefCountPtr<T>, void*);

#if !PLATFORM_COMPILER_HAS_GENERATED_COMPARISON_OPERATORS
template<typename ReferencedType>
UE_FORCEINLINE_HINT bool operator==(ReferencedType* A, const TRefCountPtr<ReferencedType>& B)
{
	return A == B.GetReference();
}
#endif

template<typename ReferencedType>
UE_FORCEINLINE_HINT uint32 GetTypeHash(const TRefCountPtr<ReferencedType>& InPtr)
{
	return GetTypeHash(InPtr.GetReference());
}


template<typename ReferencedType>
FArchive& operator<<(FArchive& Ar,TRefCountPtr<ReferencedType>& Ptr)
{
	Ptr.Serialize(Ar);
	return Ar;
}

template <
	typename T,
	typename... TArgs
	UE_REQUIRES(!std::is_array_v<T>)
>
[[nodiscard]] inline TRefCountPtr<T> MakeRefCount(TArgs&&... Args)
{
	T* NewUnwrappedObject = new T(Forward<TArgs>(Args)...);

	// Set up `NewObject` in the open to avoid unnecessary (but harmless) OnAbort tasks.
	TRefCountPtr<T> NewObject;
	UE_AUTORTFM_OPEN
	{
		NewObject = NewUnwrappedObject;
	};
	return NewObject;
}
