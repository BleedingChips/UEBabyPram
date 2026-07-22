module;
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "Model/MonotonicTimeline.h"
#include "TraceServices/Model/Threads.h"


export module UEBabyPramInsightParserAnalysisInterface;

export namespace UEBabyPram::InsightParser
{

	using TraceServices::IEditableProvider;
	using TraceServices::ETimingProfilerTimerType;
	using TraceServices::FMetadataSpec;
	using TraceServices::FGpuSignalFence;

	using TraceServices::FGpuWaitFence;
	using TraceServices::IEditableTimeline;
	using TraceServices::FTimingProfilerEvent;

	using TraceServices::FTimingProfilerEvent;
	using TraceServices::ITimingProfilerProvider;
	using TraceServices::IThreadProvider;
	using TraceServices::FTimingProfilerTimer;
	using TraceServices::ITimingProfilerTimerReader;

	using TraceServices::IAnalysisSession;

	class IEditableTimingProfilerProvider
		: public IEditableProvider
	{
	public:
		virtual ~IEditableTimingProfilerProvider() = default;

		//////////////////////////////////////////////////
		// Timers

		/**
		 * Adds/registers a new timer.
		 *
		 * @param Type	The type of the new timer.
		 * @return		The identity of the new timer.
		 */
		virtual uint32 AddTimer(ETimingProfilerTimerType Type) { return 0; }

		/**
		 * Adds/registers a new timer.
		 *
		 * @param Type	The type of the new timer.
		 * @param Name	The name attached to the timer.
		 * @return		The identity of the new timer.
		 */
		virtual uint32 AddTimer(ETimingProfilerTimerType Type, FStringView Name)
		{
			const uint32 TimerId = AddTimer(Type);
			if (TimerId != 0)
			{
				SetTimerName(TimerId, Name);
			}
			return TimerId;
		}

		/**
		 * Adds/registers a new timer.
		 *
		 * @param Type	The type of the new timer.
		 * @param Name	The name attached to the timer.
		 * @param File	The source file in which the timer is defined.
		 * @param Line	The line number of the source file in which the timer is defined.
		 * @return		The identity of the new timer.
		 */
		virtual uint32 AddTimer(ETimingProfilerTimerType Type, FStringView Name, const TCHAR* File, uint32 Line)
		{
			const uint32 TimerId = AddTimer(Type);
			if (TimerId != 0)
			{
				SetTimerNameAndLocation(TimerId, Name, File, Line);
			}
			return TimerId;
		}

		/**
		 * Updates an existing timer with information. Some information is unavailable when it's created.
		 *
		 * @param TimerId	The identity of the timer to update.
		 * @param Name		The name attached to the timer.
		 */
		virtual void SetTimerName(uint32 TimerId, FStringView Name) = 0;

		/**
		 * Updates an existing timer with information. Some information is unavailable when it's created.
		 *
		 * @param TimerId	The identity of the timer to update.
		 * @param Name		The name attached to the timer.
		 * @param File		The source file in which the timer is defined.
		 * @param Line		The line number of the source file in which the timer is defined.
		 */
		virtual void SetTimerNameAndLocation(uint32 TimerId, FStringView Name, const TCHAR* File, uint32 Line)
		{
			SetTimerName(TimerId, Name);
			SetTimerLocation(TimerId, File, Line);
		}

		/**
		 * Updates an existing timer with information. Some information is unavailable when it's created.
		 *
		 * @param TimerId	The identity of the timer to update.
		 * @param File		The source file in which the timer is defined.
		 * @param Line		The line number of the source file in which the timer is defined.
		 */
		virtual void SetTimerLocation(uint32 TimerId, const TCHAR* File, uint32 Line) {}

		//////////////////////////////////////////////////
		// Metadata

		/**
		 * Sets the metadata spec for an existing timer.
		 *
		 * @param TimerId			The identity of the timer to update.
		 * @param MetadataSpecId	The metadata spec to associate with the timer.
		 */
		virtual void SetMetadataSpec(uint32 TimerId, uint32 MetadataSpecId) {}

		/**
		 * Adds metadata to a CPU or GPU timer.
		 *
		 * @param OriginalTimerId	The identity of the timer to add metadata to.
		 * @param Metadata			The metadata.
		 *
		 * @return The identity of the metadata.
		 */
		virtual uint32 AddMetadata(uint32 OriginalTimerId, TArray<uint8>&& Metadata) = 0;

		/**
		 * Sets metadata for the specified MetadataTimerId.
		 * The MetadataTimerId must be a value returned by AddMetadata.
		 * The function is meant to be used to replace the metadata at a MetadataTimerId.
		 *
		 * @param MetadataTimerId	The identity of the metadata to replace.
		 * @param Metadata			The metadata.
		 *
		 */
		virtual void SetMetadata(uint32 MetadataTimerId, TArray<uint8>&& Metadata) {}

		/**
		 * Sets metadata for the specified MetadataTimerId and replaces the OriginalTimerId with a new value.
		 * The MetadataTimerId must be a value returned by AddMetadata.
		 * The function is meant to be used to replace the metadata at a MetadataTimerId.
		 *
		 * @param MetadataTimerId	The identity of the metadata to replace.
		 * @param Metadata			The metadata.
		 * @param NewTimerId		The new TimerId. Represents the identity of the timer the metadata is attached to.
		 *
		 */
		virtual void SetMetadata(uint32 MetadataTimerId, TArray<uint8>&& Metadata, uint32 NewTimerId) {}

		/**
		 * Gets metadata by id.
		 *
		 * @param MetadataTimerId	The identity of the metadata.
		 *
		 * @return The metadata.
		 */
		virtual TArrayView<uint8> GetEditableMetadata(uint32 MetadataTimerId) = 0;

		/**
		 * Adds a metadata spec to storage.
		 *
		 * @param Metadata	The metadata spec to add.
		 *
		 * @return			The identity of the metadata spec.
		 */
		virtual uint32 AddMetadataSpec(FMetadataSpec&& Metadata) { return 0; }

		//////////////////////////////////////////////////
		// GPU

		/**
		 * Adds/registers a new GPU timer.
		 *
		 * @param Name	The name attached to the timer.
		 * @param File	The source file in which the timer is defined.
		 * @param Line	The line number of the source file in which the timer is defined.
		 *
		 * @return The identity of the GPU timer.
		 */
		virtual uint32 AddGpuTimer(FStringView Name)
		{
			return AddTimer(ETimingProfilerTimerType::GpuScope, Name);
		}

		/**
		 * Adds a new GPU queue.
		 *
		 * @param QueueId	The GPU queue for which the events are for.
		 */
		virtual void AddGpuQueue(uint32 QueueId, uint8 GPU, uint8 Index, uint8 Type, const TCHAR* Name) {}

		/**
		 * Adds a new GPU signal fence to a queue.
		 *
		 * @param QueueId		The GPU queue that issues the signal.
		 * @param SignalFence	The signal fence.
		 */
		virtual void AddGpuSignalFence(uint32 QueueId, const FGpuSignalFence& SignalFence) {};

		/**
		 * Adds a new GPU wait fence to a queue.
		 *
		 * @param QueueId		The GPU queue that waits.
		 * @param WaitFence		The wait fence.
		 */
		virtual void AddGpuWaitFence(uint32 QueueId, const FGpuWaitFence& WaitFence) {};

		/**
		 * Gets an object to receive ordered GPU timing events for a GPU queue.
		 *
		 * @param QueueId	The queue for which the events are for.
		 *
		 * @return The object to receive the serial GPU timing events for the specified queue.
		 */
		virtual IEditableTimeline<FTimingProfilerEvent>* GetGpuQueueEditableTimeline(uint32 QueueId) { return nullptr; }

		/**
		 * Gets an object to receive ordered GPU Work timing events for a GPU queue.
		 *
		 * @param QueueId	The queue for which the events are for.
		 *
		 * @return The object to receive the serial GPU Work timing events for the specified queue.
		 */
		virtual IEditableTimeline<FTimingProfilerEvent>* GetGpuQueueWorkEditableTimeline(uint32 QueueId) { return nullptr; }

		//////////////////////////////////////////////////
		// Verse Sampling

		/**
		 * Adds/registers a new Verse Sampling timer.
		 *
		 * @param Name	The name attached to the timer.
		 *
		 * @return The identity of the CPU timer.
		 */
		virtual uint32 AddVerseTimer(FStringView Name)
		{
			return AddTimer(ETimingProfilerTimerType::VerseSampling, Name);
		}

		/**
		 * Gets an object to receive ordered Verse timing events for the Verse sampling.
		 *
		 * @return The object to receive the serial Verse timing events for Verse sampling.
		 */
		virtual IEditableTimeline<FTimingProfilerEvent>* GetVerseEditableTimeline() { return nullptr; }

		//////////////////////////////////////////////////
		// CPU

		/**
		 * Adds/registers a new CPU Scope timer.
		 *
		 * @param Name	The name attached to the timer.
		 * @param File	The source file in which the timer is defined.
		 * @param Line	The line number of the source file in which the timer is defined.
		 *
		 * @return The identity of the CPU timer.
		 */
		virtual uint32 AddCpuTimer(FStringView Name, const TCHAR* File, uint32 Line)
		{
			return AddTimer(ETimingProfilerTimerType::CpuScope, Name, File, Line);
		}

		/**
		 * Gets an object to receive ordered CPU timing events for a CPU thread.
		 *
		 * @param ThreadId	The thread for which the events are for.
		 *
		 * @return The object to receive the serial CPU timing events for the specified thread.
		 */
		virtual IEditableTimeline<FTimingProfilerEvent>& GetCpuThreadEditableTimeline(uint32 ThreadId) = 0;

		//////////////////////////////////////////////////
		// Misc

		/**
		 * Sets the Ratio of Threads to be used.
		 * @param InRatioOfThreadsToUse The new ratio of threads to use.
		 */
		virtual void SetRatioOfThreadsToUse(double InRatioOfThreadsToUse) {}

		/**
		 * Gets the read provider.
		 * @returns The read only provider or nullptr if not available.
		 */
		virtual const ITimingProfilerProvider* GetReadProvider() const { return nullptr; }
	};

	class IEditableThreadProvider
		: public IEditableProvider
	{
	public:
		virtual ~IEditableThreadProvider() = default;

		/*
		* Note the existence of a new thread.
		*
		* @param Id			The thread identity.
		* @param Name		The name the user may know the thread by, if available.
		* @param Priority	The system priority level of the thread, if available.
		*/
		virtual void AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority) = 0;

		/*
		* Gets the read provider.
		* @returns The read only provider or nullptr if not available.
		*/
		virtual const IThreadProvider* GetReadProvider() const { return nullptr; }
	};

}
