module;
#include <cassert>
export module UEBabyPramInsightDefine;

import std;
import Potato;
import UEBabyPramInsightAlgo;

export namespace UEBabyPram::InsightParser
{
	constexpr auto insight_log = Potato::TMP::TypeString(u8"InsightLog");

	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;

	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;

	using SIZE_T = std::size_t;
	using UPTRINT = std::size_t;
	using PTRINT = std::conditional_t<
		sizeof(nullptr) == sizeof(std::int32_t),
		std::int32_t,
		std::int64_t
	>;

	namespace FMath
	{
		template<class T1, class T2>
		auto Max(T1&& t1, T2&& t2)
		{
			return std::max(std::forward<T1>(t1), std::forward<T2>(t2));
		}

		template<class T1, class T2>
		auto Min(T1&& t1, T2&& t2)
		{
			return std::min(std::forward<T1>(t1), std::forward<T2>(t2));
		}

		constexpr inline uint32 CountLeadingZeros(uint32 Value)
		{
			return std::countl_zero(Value);
		}

		inline uint32 CeilLogTwo(uint32 Arg)
		{
			// if Arg is 0, change it to 1 so that we return 0
			Arg = Arg ? Arg : 1;
			return 32 - CountLeadingZeros(Arg - 1);
		}

		inline uint32 RoundUpToPowerOfTwo(uint32 Arg)
		{
			return 1u << CeilLogTwo(Arg);
		}
	}

	enum class EAllowShrinking : uint8
	{
		No,
		Yes,

		Default = Yes /* Prefer UE::Core::Private::AllowShrinkingByDefault<T>() in new code */
	};

	constexpr std::size_t UE_TRACE_BLOCK_POOL_MAXSIZE = 79 << 20;

	using ANSICHAR = char;
	using WIDECHAR = wchar_t;

	template<std::size_t Count>
	struct TInlineAllocator
	{

	};

	template<typename Type>
	struct TArrayView : public std::span<Type>
	{
		using std::span<Type>::span;
	};

	struct FAnsiStringView : public std::string_view
	{
		using std::string_view::string_view;
		auto GetData() { return data(); }
		auto GetData() const { return data(); }
		auto Len() const { return size(); }
	};

	struct FWideStringView : public std::wstring_view
	{
		using std::wstring_view::wstring_view;
	};

	using FStringView = FWideStringView;

	struct FString : public std::wstring 
	{
		using std::wstring::wstring;
		static FString ConstructFromPtrSize(char const* String, std::size_t Size)
		{
			FString result;
			Potato::Encode::UnicodeEncoder<char, wchar_t>::EncodeTo(
				std::span(String, Size),
				std::back_insert_iterator(result)
			);
			return result;
		}
		FString& operator=(FWideStringView View)
		{
			clear();
			append(View);
			return *this;
		}
	};

	template<typename T, class PREDICATE_CLASS>
	struct TDereferenceWrapper
	{
		const PREDICATE_CLASS& Predicate;

		TDereferenceWrapper(const PREDICATE_CLASS& InPredicate)
			: Predicate(InPredicate) {
		}

		/** Pass through for non-pointer types */
		bool operator()(T& A, T& B) { return Predicate(A, B); }
		bool operator()(const T& A, const T& B) const { return Predicate(A, B); }
	};
	/** Partially specialized version of the above class */
	template<typename T, class PREDICATE_CLASS>
	struct TDereferenceWrapper<T*, PREDICATE_CLASS>
	{
		const PREDICATE_CLASS& Predicate;

		TDereferenceWrapper(const PREDICATE_CLASS& InPredicate)
			: Predicate(InPredicate) {
		}

		/** Dereference pointers */
		bool operator()(T* A, T* B) const
		{
			return Predicate(*A, *B);
		}
	};


	template<typename Type, typename Allocator = void>
	struct TArray : public std::vector<Type>
	{
		using Super = std::vector<Type>;
		
		using Super::vector;


		auto Num() const { return static_cast<std::int32_t>(Super::size()); }
		decltype(auto) Last() { return *Super::rbegin(); }

		void Add(Type&& otype) {
			Super::emplace_back(std::move(otype));
		};

		void Add(Type const& otype) {
			Super::emplace_back(otype);
		};

		auto GetData() { return Super::data(); }
		auto GetData() const { return Super::data(); }
		void Reset() { return Super::clear(); }
		void Reserve(std::size_t Size) { return Super::reserve(Size); }
		void SetNumZeroed(std::size_t size)
		{
			if (Super::size() < size)
			{
				auto old_size = Super::resize(size);
				std::memset(
					Super::data() + old_size,
					0,
					Super::size() - old_size
				);
			}
			else if(Super::size() > size)
			{
				Super::resize(size);
			}
		}
		void SetNum(std::size_t Size, EAllowShrinking Shrinking = EAllowShrinking::Default)
		{
			Super::resize(Size);
		}
		void SetNumUninitialized(std::size_t Size)
		{
			Super::resize(Size);
		}
		std::size_t AddUninitialized(std::size_t Size)
		{
			std::size_t old_size = Super::size();
			Super::resize(old_size + Size);
			return old_size;
		}
		Type& Emplace_GetRef()
		{
			Super::emplace_back();
			return *Super::rbegin();
		}
		template<typename Otype>
		void Push(Otype&& type)
		{
			Super::push_back(std::forward<Otype>(type));
		}
		auto Pop(EAllowShrinking Shrinking = EAllowShrinking::Default)
		{
			auto Back = std::move(*Super::rbegin());
			Super::pop_back();
			return Back;
		}
		std::int32_t Insert(std::initializer_list<Type> InitList, const std::int32_t InIndex)
		{
			Super::insert(Super::begin() + InIndex, std::ranges::all_of(InitList));
			return InIndex;
		}

		template<class FuncT>
		void RemoveAll(FuncT&& func)
		{
			Super::erase(
				std::remove_if(Super::begin(), Super::end(), std::forward<FuncT>(func)),
				Super::end()
			);
		}

		template<class FuncT>
		void RemoveAllSwap(FuncT&& func)
		{
			RemoveAll(std::forward<FuncT>(func));
		}

		bool IsEmpty() const { return Super::empty(); }

		template <class PREDICATE_CLASS>
		void Heapify(const PREDICATE_CLASS& Predicate)
		{
			TDereferenceWrapper<Type, PREDICATE_CLASS> PredicateWrapper(Predicate);
			Algo::Heapify(*this, PredicateWrapper);
		}

		template<typename ...OT>
		int32 Emplace(OT&& ...ot)
		{
			auto old_size = Super::size();
			Super::emplace_back(std::forward<OT>(ot)...);
			return old_size;
		}

		const Type& HeapTop() const
		{
			return (*this)[0];
		}

		template <class PREDICATE_CLASS>
		int32 HeapPush(Type InItem, const PREDICATE_CLASS& Predicate)
		{
			// Add at the end, then sift up
			this->Add(std::move(InItem));
			TDereferenceWrapper<Type, PREDICATE_CLASS> PredicateWrapper(Predicate);
			int32 Result = AlgoImpl::HeapSiftUp(GetData(), (int32)0, Num() - 1, FIdentityFunctor(), PredicateWrapper);

			return Result;
		}

		template <class PREDICATE_CLASS>
		void HeapPopDiscard(const PREDICATE_CLASS& Predicate, EAllowShrinking AllowShrinking = EAllowShrinking::Default)
		{
			Super::erase(Super::begin());
			TDereferenceWrapper<Type, PREDICATE_CLASS> PredicateWrapper(Predicate);
			AlgoImpl::HeapSiftDown(GetData(), (int32)0, Num(), FIdentityFunctor(), PredicateWrapper);
		}

		void Empty()
		{
			Super::clear();
		}

		Type& Add_GetRef(Type type)
		{
			Super::emplace_back(std::move(type));
			return *Super::rbegin();
		}
	};

	using std::memcpy;
	using std::memmove;

	struct FMemory
	{
		static void* Malloc(std::size_t ByteSize, std::size_t Aligmas = alignof(std::nullptr_t)) 
		{
			assert(Aligmas <= alignof(std::nullptr_t));
			return std::malloc(ByteSize);
		}
		static void Free(void* Memory) { return std::free(Memory); }
		static void Memcpy(void* Target, void const* Source, std::size_t Size)
		{
			std::copy_n(
				reinterpret_cast<std::byte const*>(Source),
				Size,
				reinterpret_cast<std::byte*>(Target)
			);
		}

		static void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = alignof(std::nullptr_t))
		{
			return std::realloc(Original, Count);
		}
	};

	struct FCStringAnsi
	{
		static std::size_t Strlen(char const* String)
		{
			return std::string_view{ String }.size();
		}
	};

	template<class Type>
	decltype(auto) MoveTemp(Type&& type) { return std::move(type); }
}