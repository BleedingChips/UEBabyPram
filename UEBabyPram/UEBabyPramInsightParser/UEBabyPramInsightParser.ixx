module;

#include "UEBabyPramInsightParserInterface.h"

export module UEBabyPramInsightParser;
import std;
import Potato;

export namespace UEBabyPram::InsightParser
{
	using UEBabyPram::InsightParser::DataResourceInterface;

	struct ScopeAnalyzer : public FSummarizeCpuScopeAnalyzer
	{
		using FSummarizeCpuScopeAnalyzer::OnCpuScopeDiscovered;
		using FSummarizeCpuScopeAnalyzer::OnCpuScopeAnalysisEnd;

		virtual void OnCpuScopeName(uint32 ScopeId, std::wstring_view ScopeName) {}
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, std::wstring_view ScopeName) {};
		virtual void OnCpuScopeExit(const FScope& Scope, std::wstring_view ScopeName) {};
		virtual void OnCpuScopeTree(uint32 ThreadId, std::span<FSummarizeCpuScopeAnalyzer::FScopeEvent const> ScopeEvents, Potato::TMP::FunctionRef<std::wstring_view(std::uint32_t)> LookupScopeName) {};

	protected:
		

		/** Invoked when CPU scope specification is encountered in the trace stream. */
		virtual void OnCpuScopeName(uint32 ScopeId, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeName(ScopeId, std::wstring_view(ScopeName, ScopeNameLen));
		};

		/** Invoked when a scope is entered. The scope name might not be known yet. */
		virtual void OnCpuScopeEnter(const FScopeEvent& ScopeEnter, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeEnter(ScopeEnter, std::wstring_view(ScopeName, ScopeNameLen));
		};

		/** Invoked when a scope is exited. The scope name might not be known yet. */
		virtual void OnCpuScopeExit(const FScope& Scope, wchar_t const* ScopeName, std::size_t ScopeNameLen) {
			OnCpuScopeExit(Scope, std::wstring_view(ScopeName, ScopeNameLen));
		};

		using ScopeNameFunction = bool(void* Object, uint32, wchar_t const*& ScopeName, std::size_t& ScopeNameLen);

		/** Invoked when a root event on the specified thread along with all child events down to the leaves are known. */
		virtual void OnCpuScopeTree(uint32 ThreadId, FSummarizeCpuScopeAnalyzer::FScopeEvent const* ScopeEvents, std::size_t ScopeEventsLen, ScopeNameFunction func, void* Object) {};
	};

	struct DcomentWrapper : public UEBabyPram::InsightParser::DataResourceInterface
	{
		DcomentWrapper(Potato::Document::DocumentReader& reader) : reader(reader) {}
		virtual std::int32_t Read(void* out_data, std::uint32_t byte_size) override
		{
			return static_cast<std::int32_t>(reader.StreamRead(static_cast<std::byte*>(out_data), byte_size));
		}
	protected:
		Potato::Document::DocumentReader& reader;
	};

	void Test(Potato::Document::DocumentReader& Resource, ScopeAnalyzer& Analyzer)
	{
		DcomentWrapper ResourceWrapper(Resource);
		TestImp(ResourceWrapper, Analyzer);
	}
}


//export import UEBabyPramInsightInterface;