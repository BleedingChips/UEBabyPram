// Runtime checks of the parser helpers which the memory report depends on: MemoryTagSnapView::GetMemoryTagValues()
// has to answer the value of every requested tag, and to answer std::numeric_limits<int64>::max() for a tag which the
// snapshot does not carry.

import std;
import UEBabyPramInsightParser;

using UEBabyPram::InsightParser::DurationSec;
using UEBabyPram::InsightParser::MemoryTagId;
using UEBabyPram::InsightParser::MemoryTagSnapView;
using UEBabyPram::InsightParser::MemoryTrackerId;

namespace
{
	// The width of the ids of the trace, taken from the types of the view instead of being written out: the ids are the
	// int64 and the uint32 of the platform types of Unreal, which are not visible to a translation unit which only
	// imports the module.
	using Int64 = decltype(MemoryTagId{}.id);
	using TagSpanVariant = decltype(MemoryTagSnapView{}.tag_span);
	using UInt32 = std::ranges::range_value_t<std::variant_alternative_t<1, TagSpanVariant>>;

	constexpr Int64 Missing = std::numeric_limits<Int64>::max();

	std::size_t FailureCount = 0;
	std::size_t CheckCount = 0;

	void Check(bool condition, std::string_view what)
	{
		++CheckCount;
		if (!condition)
		{
			++FailureCount;
			std::println("FAILED: {}", what);
		}
	}

	// Build a snapshot whose tags are 64 bit, which is one of the two interfaces of the trace parser.
	MemoryTagSnapView MakeSnap64(
		std::span<Int64 const> tags,
		std::span<Int64 const> values,
		MemoryTrackerId tracker_id = MemoryTrackerId{ 1 },
		DurationSec time = DurationSec{ 1.0 }
	)
	{
		return MemoryTagSnapView{
			tracker_id,
			time,
			std::span<Int64 const>{ tags.data(), tags.size() },
			values
		};
	}

	// Build a snapshot whose tags are 32 bit, which is the other interface of the trace parser.
	MemoryTagSnapView MakeSnap32(std::span<UInt32 const> tags, std::span<Int64 const> values)
	{
		return MemoryTagSnapView{
			MemoryTrackerId{ 2 },
			DurationSec{ 2.0 },
			std::span<UInt32 const>{ tags.data(), tags.size() },
			values
		};
	}
}

int main()
{
	// The snapshot which every check below reads: tag 100 -> 10, tag 200 -> 20, tag 300 -> 30.
	std::array<Int64, 3> tags = { 100, 200, 300 };
	std::array<Int64, 3> values = { 10, 20, 30 };
	MemoryTagSnapView snap = MakeSnap64(tags, values);

	// Every requested tag is found, and the output follows the order of the input, not the order of the snapshot.
	{
		std::array<MemoryTagId, 3> input = { MemoryTagId{ 300 }, MemoryTagId{ 100 }, MemoryTagId{ 200 } };
		std::array<Int64, 3> output = { -1, -1, -1 };
		std::size_t found = snap.GetMemoryTagValues(input, output);
		Check(found == 3, "every requested tag which the snapshot carries is counted");
		Check(output[0] == 30, "300 is the value of the third tag");
		Check(output[1] == 10, "100 is the value of the first tag");
		Check(output[2] == 20, "200 is the value of the second tag");
	}

	// A tag which the snapshot does not carry is answered with the maximum of int64, and it is not counted.
	{
		std::array<MemoryTagId, 2> input = { MemoryTagId{ 100 }, MemoryTagId{ 999 } };
		std::array<Int64, 2> output = { -1, -1 };
		std::size_t found = snap.GetMemoryTagValues(input, output);
		Check(found == 1, "only the tag which the snapshot carries is counted");
		Check(output[0] == 10, "the tag which the snapshot carries is answered");
		Check(output[1] == Missing, "the tag which the snapshot does not carry is answered with int64 max");
	}

	// An empty input writes nothing and answers nothing, and a longer output keeps its slots after the last tag untouched.
	{
		std::array<Int64, 5> output = { -1, -1, -1, -1, -1 };
		std::size_t found = snap.GetMemoryTagValues(std::span<MemoryTagId const>{}, output);
		Check(found == 0, "an empty input answers no tag");
		Check(output[0] == -1, "an empty input writes nothing");

		std::array<MemoryTagId, 1> input = { MemoryTagId{ 200 } };
		found = snap.GetMemoryTagValues(input, output);
		Check(found == 1, "the single requested tag is counted");
		Check(output[0] == 20, "the slot of the requested tag is written");
		Check(output[1] == -1 && output[2] == -1 && output[3] == -1 && output[4] == -1, "the slots after the input are untouched");
	}

	// A snapshot whose values do not cover its tags is not consistent, so it answers nothing, returns 0, and the output
	// keeps what the caller put into it, instead of answering with the value of another tag.
	{
		std::array<Int64, 2> shorter_values = { 10, 20 };
		MemoryTagSnapView inconsistent = MakeSnap64(tags, shorter_values);

		std::array<MemoryTagId, 3> input = { MemoryTagId{ 100 }, MemoryTagId{ 200 }, MemoryTagId{ 300 } };
		std::array<Int64, 3> output = { -7, -7, -7 };
		std::size_t found = inconsistent.GetMemoryTagValues(input, output);
		Check(found == 0, "an inconsistent snapshot counts no tag");
		Check(output[0] == -7 && output[1] == -7 && output[2] == -7, "an inconsistent snapshot answers nothing");
	}

	// A snapshot which carries more values than tags answers with the values of the tags which it carries.
	{
		std::array<Int64, 4> longer_values = { 10, 20, 30, 40 };
		MemoryTagSnapView longer = MakeSnap64(tags, longer_values);

		std::array<MemoryTagId, 2> input = { MemoryTagId{ 300 }, MemoryTagId{ 100 } };
		std::array<Int64, 2> output = { -1, -1 };
		std::size_t found = longer.GetMemoryTagValues(input, output);
		Check(found == 2, "both tags are counted");
		Check(output[0] == 30, "the value of the third tag is answered");
		Check(output[1] == 10, "the value of the first tag is answered");
	}

	// The 32 bit interface of the view answers the same way.
	{
		std::array<UInt32, 2> tags32 = { 7u, 8u };
		std::array<Int64, 2> values32 = { 70, 80 };
		MemoryTagSnapView snap32 = MakeSnap32(tags32, values32);

		std::array<MemoryTagId, 3> input = { MemoryTagId{ 8 }, MemoryTagId{ 7 }, MemoryTagId{ 9 } };
		std::array<Int64, 3> output = { -1, -1, -1 };
		std::size_t found = snap32.GetMemoryTagValues(input, output);
		Check(found == 2, "the two 32 bit tags which the snapshot carries are counted");
		Check(output[0] == 80, "the 32 bit tag is found");
		Check(output[1] == 70, "the 32 bit tag is found in the input order");
		Check(output[2] == Missing, "the 32 bit tag which is absent is answered with int64 max");
	}

	// A tag which the snapshot carries twice is read at its first index.
	{
		std::array<Int64, 3> duplicated_tags = { 100, 100, 200 };
		std::array<Int64, 3> duplicated_values = { 10, 11, 20 };
		MemoryTagSnapView duplicated = MakeSnap64(duplicated_tags, duplicated_values);

		std::array<MemoryTagId, 1> input = { MemoryTagId{ 100 } };
		std::array<Int64, 1> output = { -1 };
		std::size_t found = duplicated.GetMemoryTagValues(input, output);
		Check(found == 1, "a tag which the snapshot carries twice is counted once");
		Check(output[0] == 10, "the first index of a duplicated tag is read");
	}

	// A tag which the request carries twice is answered in every slot of the request, but it is counted once, and the walk
	// stops once every slot is answered.
	{
		std::array<MemoryTagId, 3> input = { MemoryTagId{ 200 }, MemoryTagId{ 100 }, MemoryTagId{ 200 } };
		std::array<Int64, 3> output = { -1, -1, -1 };
		std::size_t found = snap.GetMemoryTagValues(input, output);
		Check(found == 2, "the duplicated request of a tag is counted once");
		Check(output[0] == 20, "the first slot of the duplicated request is answered");
		Check(output[1] == 10, "the slot between the duplicated ones is answered");
		Check(output[2] == 20, "the second slot of the duplicated request is answered");
	}

	// The accessors of the view keep their own behaviour.
	{
		Check(snap.GetValueCount() == 3, "the count of the values is the count of the values of the view");
		Check(snap.GetMemoryTagId(0) == MemoryTagId{ 100 }, "the id of the first tag");
		Check(snap.GetMemoryTagValue(0) == std::optional<Int64>{ 10 }, "the value of the first tag");
		Check(!snap.GetMemoryTagValue(3).has_value(), "a value out of the range is absent");
	}

	std::println("{} check(s), {} failure(s)", CheckCount, FailureCount);
	return FailureCount == 0 ? 0 : 1;
}
