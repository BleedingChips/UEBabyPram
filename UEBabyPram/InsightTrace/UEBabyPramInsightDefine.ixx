module;

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

	enum class EAllowShrinking : uint8
	{
		No,
		Yes,

		Default = Yes /* Prefer UE::Core::Private::AllowShrinkingByDefault<T>() in new code */
	};

	constexpr std::size_t UE_TRACE_BLOCK_POOL_MAXSIZE = 79 << 20;

	using ANSICHAR = char;
	using WIDECHAR = wchar_t;
	using FStreamReader = Potato::Streamer::StreamRandomReader;

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

	template<typename Type, typename Allocator = void>
	struct TArray : public std::vector<Type>
	{
		using Super = std::vector<Type>;
		
		using Super::vector;


		auto Num() const { return static_cast<std::int32_t>(Super::size()); }
		decltype(auto) Last() { return *Super::rbegin(); }

		template<typename OType>
		void Add(OType&& otype) {
			return Super::push(std::move(otype));
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
		void SetNum(std::size_t Size)
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
	};

	using std::memcpy;

	struct FMemory
	{
		static void* Malloc(std::size_t ByteSize) { return new std::byte[ByteSize]; }
		static void Free(void* Memory) { return delete Memory; }
		static void Memcpy(void* Target, void const* Source, std::size_t Size)
		{
			std::copy_n(
				reinterpret_cast<std::byte const*>(Source),
				Size,
				reinterpret_cast<std::byte*>(Target)
			);
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