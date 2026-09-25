#pragma once
#include "core_types.h"
#include "core_undo.h"
#include "chart.h"
#include "chart_editor_sound.h"
#include "chart_editor_graphics.h"
#include "audio/audio_engine.h"
#include "audio/audio_waveform.h"
#include <set>
#include <unordered_map>

namespace PeepoDrumKit
{
	struct BeatAndTime { Beat Beat; Time Time; };

	// NOTE: Essentially the shared data between all the chart editor components, split into a separate file away from the chart editor to avoid circular #includes
	struct ChartContext : NonCopyable
	{
		ChartProject Chart;
		std::string ChartFilePath;

		// NOTE: Must **ALWAYS** point to a valid course. If "Chart.Courses.empty()" then a new one should automatically be added at the start of the frame before anything else is updated

		ChartCourse* ChartSelectedCourse = nullptr;
		BranchType ChartSelectedBranch = BranchType::Normal;

		b8 CompareMode = false;
		std::unordered_map<const ChartCourse*, std::set<BranchType>> ChartsCompared; // should always include ChartSelected

		// NOTE: Specifically for accurate playback preview sound future-offset calculations
		Time CursorNonSmoothTimeThisFrame, CursorNonSmoothTimeLastFrame;
		// NOTE: Store cursor time as Beat while paused to avoid any floating point precision issues and make sure "SetCursorBeat(x); assert(GetCursorBeat() == x)"
		Beat CursorBeatWhilePaused = Beat::Zero();
		mutable Beat CursorBeatHint = Beat::Zero();
		std::optional<Time> CursorExactTimeWhilePaused;
		b8 TimelineRealTime = false;
		// NOTE: Specifically to skip hit animations for notes before this time point
		Time CursorTimeOnPlaybackStart = Time::Zero();

		struct RangeSelectionData
		{
			Beat Start, End;
			b8 HasEnd;
			b8 IsActive;
			inline b8 IsActiveAndHasEnd() const { return (IsActive && HasEnd); }
			inline Beat GetMin() const { return ClampBot(Min(Start, End), Beat::Zero()); }
			inline Beat GetMax() const { return ClampBot(Max(Start, End), Beat::Zero()); }
			Beat GetDuration() const { return GetMax() - GetMin(); }
		} RangeSelection = {};

		struct MarkerData
		{
			Beat BeatTime;
			b8 IsActive;
		} Marker = {};

		Time GetRangeSelectionDuration() const
		{
			return BeatToTime(RangeSelection.GetMax()) - BeatToTime(RangeSelection.GetMin());
		}

		// NOTE: Specifically for animations, accumulated program delta time, unrelated to GetCursorTime() etc.
		Time ElapsedProgramTimeSincePlaybackStarted = Time::Zero();
		Time ElapsedProgramTimeSincePlaybackStopped = Time::Zero();

		Audio::Voice SongVoice = Audio::VoiceHandle::Invalid;
		Audio::SourceHandle SongSource = Audio::SourceHandle::Invalid;
		SoundEffectsVoicePool SfxVoicePool;
		ChartGraphicsResources Gfx;

		std::string SongSourceFilePath;
		std::string SongJacketFilePath;
		Audio::WaveformMipChain SongWaveformL;
		Audio::WaveformMipChain SongWaveformR;
		f32 SongWaveformFadeAnimationCurrent = 0.0f;
		f32 SongWaveformFadeAnimationTarget = 0.0f;

		CustomDraw::GPUTexture JacketTexture = {};
		f32 SongJacketFadeAnimationCurrent = 0.0f;
		f32 SongJacketFadeAnimationTarget = 0.0f;

		Undo::UndoHistory Undo;

	public:
		inline Time BeatToTime(Beat beat) const { return ChartSelectedCourse->TempoMap.BeatToTime(beat, EnumToIndex(ChartSelectedBranch)); }
		inline Beat TimeToBeat(Time time, bool truncTo0 = false) const { return ChartSelectedCourse->TempoMap.TimeToBeat(time, truncTo0, EnumToIndex(ChartSelectedBranch), CursorBeatHint); }
		inline f64 BeatAndTimeToHBScrollBeatTick(Beat beat, Time time) const { return ChartSelectedCourse->TempoMap.BeatAndTimeToHBScrollBeatTick(beat, time, EnumToIndex(ChartSelectedBranch)); }
		Time TimelineBeatToTime(Beat beat) const { return TimelineBeatToTime(beat, ChartSelectedBranch); }
		Time TimelineBeatToTime(Beat beat, BranchType branch) const
		{
			return TimelineRealTime ? ChartSelectedCourse->TempoMap.BeatToTime(beat, EnumToIndex(branch)) : ChartSelectedCourse->TempoMap.BeatToTimeWithoutDelay(beat);
		}
		Beat TimelineTimeToBeat(Time time, bool truncTo0 = false) const
		{
			return TimelineRealTime ? TimeToBeat(time, truncTo0) : ChartSelectedCourse->TempoMap.TimeToBeatWithoutDelay(time, truncTo0);
		}
		Time GetTimelineCursorTime() const { return TimelineRealTime ? GetCursorTime() : TimelineBeatToTime(GetCursorBeat()); }
		Time TimelineToSongTime(Time time) const
		{
			return (TimelineRealTime ? time : time + ChartSelectedCourse->TempoMap.GetDelayAtBeat(TimelineTimeToBeat(time), EnumToIndex(ChartSelectedBranch))) - Chart.SongOffset;
		}

		Time GetUsedDuration() const { return GetUsedDuration(*ChartSelectedCourse); }
		Time GetUsedDurationFast() const { return GetUsedDurationFast(*ChartSelectedCourse); }
		Beat GetUsedBeatDuration() const { return GetUsedBeatDuration(*ChartSelectedCourse); }
		Beat GetUsedBeatDurationFast() const { return GetUsedBeatDurationFast(*ChartSelectedCourse); }

		Time GetUsedDuration(const ChartCourse& course) const { return Chart.GetUsedDuration(course); }
		Time GetUsedDurationFast(const ChartCourse& course) const { return Chart.GetUsedDurationFast(course); }
		Beat GetUsedBeatDuration(const ChartCourse& course) const { return Chart.GetUsedBeatDuration(course); }
		Beat GetUsedBeatDurationFast(const ChartCourse& course) const { return Chart.GetUsedBeatDurationFast(course); }

		void ResetChartsCompared() { ChartsCompared = { { ChartSelectedCourse, { ChartSelectedBranch } } }; CompareMode = false; }
		b8 IsChartCompared(const ChartCourse* course, BranchType branch) const
		{
			auto it = ChartsCompared.find(course);
			return (it != cend(ChartsCompared)) && (it->second.find(branch) != cend(it->second));
		}
		b8 IsCurrentChartCourse(const ChartCourse* course) const
		{
			for (const auto& currentCourse : Chart.Courses)
				if (currentCourse.get() == course)
					return true;
			return false;
		}

		void SetSelectedChart(ChartCourse* course, BranchType branch)
		{
			const b8 selectedCourseIsCurrent = IsCurrentChartCourse(ChartSelectedCourse);
			const b8 selectionChanged = !selectedCourseIsCurrent || course != ChartSelectedCourse || branch != ChartSelectedBranch;
			if (selectionChanged)
			{
				// A chart import replaces Chart.Courses before selecting its first course. Do not dereference
				// the old selected pointer after that replacement.
				const Time cursorTime = selectedCourseIsCurrent ? GetCursorTime()
					: GetIsPlayback() ? SongVoice.GetPositionSmooth() + Chart.SongOffset : CursorExactTimeWhilePaused.value_or(Time::Zero());
				if (!selectedCourseIsCurrent)
				{
					ChartsCompared.clear();
					CompareMode = false;
				}
				ChartSelectedCourse = course;
				ChartSelectedBranch = branch;
				SetCursorTime(cursorTime); // fix time jumping if timing is different
			}
			if (CompareMode)
				ChartsCompared[course].insert(branch);
			else if (!IsChartCompared(course, branch))
				ResetChartsCompared();
		}

		inline f32 GetPlaybackSpeed() { return SongVoice.GetPlaybackSpeed(); }
		inline void SetPlaybackSpeed(f32 newSpeed) { if (!ApproxmiatelySame(SongVoice.GetPlaybackSpeed(), newSpeed)) SongVoice.SetPlaybackSpeed(newSpeed); }

		inline b8 GetIsPlayback() const
		{
			return SongVoice.GetIsPlaying();
		}

		inline void SetIsPlayback(b8 newIsPlaying)
		{
			if (SongVoice.GetIsPlaying() == newIsPlaying)
				return;

			if (newIsPlaying)
			{
				SongVoice.SetPosition(GetCursorTime() - Chart.SongOffset);
				CursorBeatHint = CursorBeatWhilePaused;
				CursorTimeOnPlaybackStart = (SongVoice.GetPosition() + Chart.SongOffset);
				SongVoice.SetIsPlaying(true);
			}
			else
			{
				SongVoice.SetIsPlaying(false);
				CursorBeatWhilePaused = CursorBeatHint = TimeToBeat(SongVoice.GetPosition() + Chart.SongOffset);
				CursorExactTimeWhilePaused = SongVoice.GetPosition() + Chart.SongOffset;
				SfxVoicePool.PauseAllFutureVoices();
			}
		}

		inline Time GetCursorTime() const
		{
			if (SongVoice.GetIsPlaying())
				return (SongVoice.GetPositionSmooth() + Chart.SongOffset);
			else
				return CursorExactTimeWhilePaused.value_or(BeatToTime(CursorBeatWhilePaused));
		}

		inline Beat GetCursorBeat() const
		{
			return GetCursorBeat(false);
		}

		inline Beat GetCursorBeat(bool truncTo0) const
		{
			if (SongVoice.GetIsPlaying())
				return CursorBeatHint = TimeToBeat(SongVoice.GetPositionSmooth() + Chart.SongOffset, truncTo0);
			else
				return CursorBeatWhilePaused;
		}

		// NOTE: Specifically to make sure the cursor time and beat are both in sync (as the song voice position is updated asynchronously)
		inline BeatAndTime GetCursorBeatAndTime() const
		{
			return GetCursorBeatAndTime(false);
		}

		inline BeatAndTime GetCursorBeatAndTime(bool truncTo0) const
		{
			return GetCursorBeatAndTime(ChartSelectedCourse, truncTo0);
		}

		inline BeatAndTime GetCursorBeatAndTime(const ChartCourse* course, bool truncTo0) const
		{
			return GetCursorBeatAndTime(course, ChartSelectedBranch, truncTo0);
		}

		inline BeatAndTime GetCursorBeatAndTime(const ChartCourse* course, BranchType branch, bool truncTo0) const
		{
			const Time time = GetCursorTime();
			if (!GetIsPlayback() && course == ChartSelectedCourse && branch == ChartSelectedBranch)
				return { CursorBeatWhilePaused, time };
			const Beat beat = course->TempoMap.TimeToBeat(time, truncTo0, EnumToIndex(branch), CursorBeatHint);
			if (course == ChartSelectedCourse && branch == ChartSelectedBranch) CursorBeatHint = beat;
			return { beat, time };
		}

		inline void SetCursorTime(Time newTime)
		{
			SongVoice.SetPosition(newTime - Chart.SongOffset);
			CursorNonSmoothTimeThisFrame = CursorNonSmoothTimeLastFrame = newTime;
			CursorBeatWhilePaused = CursorBeatHint = TimeToBeat(newTime);
			CursorExactTimeWhilePaused = newTime;
			CursorTimeOnPlaybackStart = newTime;
		}

		inline void SetCursorBeat(Beat newBeat)
		{
			const Time newTime = BeatToTime(newBeat);
			SongVoice.SetPosition(newTime - Chart.SongOffset);
			CursorNonSmoothTimeThisFrame = CursorNonSmoothTimeLastFrame = newTime;
			CursorBeatWhilePaused = newBeat;
			CursorExactTimeWhilePaused.reset();
			CursorBeatHint = newBeat;
			CursorTimeOnPlaybackStart = newTime;
		}
	};

	// NOTE: Soft clamp for sliders but still allow *typing in* values to be higher, even if it can cause clipping
	static constexpr f32 MinVolume = 0.0f;
	static constexpr f32 MaxVolumeSoftLimit = 1.0f;
	static constexpr f32 MaxVolumeHardLimit = F32Max;

	// limited values to prevent crashes due to high load
	static constexpr i32 MinTimeSignatureValue = -Beat::TicksPerBeat * 4;
	static constexpr i32 MaxTimeSignatureValue = Beat::TicksPerBeat * 4;
	static constexpr f32 MinBPM = -60000.0f;
	static constexpr f32 MaxBPM = 60000.0f;

	// practically no limits
	static constexpr i32 MinBalloonCount = 0;
	static constexpr i32 MaxBalloonCount = I32Max;
	static constexpr f32 MinScrollSpeed = -F32Max;
	static constexpr f32 MaxScrollSpeed = +F32Max;
	static constexpr f32 MinScrollBPM = -F32Max;
	static constexpr f32 MaxScrollBPM = +F32Max;
	static constexpr f32 MaxJPOSScrollMove = +F32Max;
	static constexpr f32 MinJPOSScrollMove = -F32Max;
	static constexpr f32 MaxJPOSScrollDuration = +F32Max;
	static constexpr f32 MinJPOSScrollDuration = -F32Max;
	static constexpr Time MinNoteTimeOffset = Time::FromMS(-F64Max);
	static constexpr Time MaxNoteTimeOffset = Time::FromMS(+F64Max);
}
