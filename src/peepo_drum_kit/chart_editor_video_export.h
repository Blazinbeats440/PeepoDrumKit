#pragma once

#include "chart.h"
#include "chart_editor_sound.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace PeepoDrumKit
{
	struct VideoResolutionPreset { cstr Name; u32 Width, Height, Bitrate; };
	inline constexpr VideoResolutionPreset VideoResolutionPresets[] = {
		{ "360p (640 x 360)", 640, 360, 2000000 },
		{ "540p (960 x 540)", 960, 540, 4000000 },
		{ "720p (1280 x 720)", 1280, 720, 6000000 },
		{ "1080p (1920 x 1080)", 1920, 1080, 12000000 },
	};

	enum class VideoBranchMode { Normal, Expert, Master, Auto, TestPlay, TestPlay2 };
	struct VideoBranchVisualState
	{
		BranchType Branch = BranchType::Normal, From = BranchType::Normal;
		size_t Index = SIZE_MAX;
		Time DecisionTime = Time::Zero(), Duration = Time::Zero();
	};
	struct VideoBranchRoute
	{
		std::vector<BranchType> Branches;
		std::vector<Time> DecisionTimes;
		BranchType InitialBranch = BranchType::Normal;
		b8 Animate = false;
		BranchType GetNoteBranch(const ChartCourse& course, Beat beat) const;
		VideoBranchVisualState GetVisualState(const ChartCourse& course, Time time) const;
		BranchType GetDisplayBranch(const ChartCourse& course, Beat beat, Time time) const;
	};
	struct PlaybackScrollTimeline
	{
		SortedScrollChangesList Changes;
		void Rebuild(const ChartCourse& course, const VideoBranchRoute& route, BranchType pendingBranch = BranchType::Count,
			size_t decidedBranches = SIZE_MAX, size_t transitionBranch = SIZE_MAX);
		Complex GetScroll(Beat beat) const;
	};
	struct VideoBranchRecording
	{
		const ChartCourse* Course = nullptr;
		i32 Changes = -1;
		VideoBranchRoute Route;
		b8 IsValid(const ChartCourse* course, i32 changes) const { return Course != nullptr && Course == course && Changes == changes; }
	};
	inline b8 CanUseVideoBranchMode(const ChartCourse& course, VideoBranchMode mode,
		const std::array<VideoBranchRecording, 2>& recordings, i32 changes)
	{
		if (mode >= VideoBranchMode::TestPlay)
		{
			const size_t slot = static_cast<size_t>(mode) - static_cast<size_t>(VideoBranchMode::TestPlay);
			return slot < recordings.size() && recordings[slot].IsValid(&course, changes);
		}
		return mode != VideoBranchMode::Auto || std::none_of(course.Branches.begin(), course.Branches.end(),
			[](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; });
	}
	struct VideoBranchTestPlayResult
	{
		std::vector<std::pair<const Note*, i32>> Judgements;
		std::vector<std::pair<const Note*, std::vector<Time>>> LongNoteHitTimes;
		std::vector<std::pair<size_t, BranchType>> Decisions;
	};
	Beat GetBranchJudgeCutoff(const ChartCourse& course, Beat branchBeat);
	VideoBranchRoute BuildFixedVideoBranchRoute(const ChartCourse& course, BranchType branch);
	b8 BuildAutoVideoBranchRoute(const ChartCourse& course, f32 rollsPerSecond, VideoBranchRoute& out);
	b8 BuildTestPlayVideoBranchRoute(const ChartCourse& course, const VideoBranchTestPlayResult& result, VideoBranchRoute& out);
	b8 RunVideoBranchSelfTest(std::string& error);
	b8 RunVideoLayoutSelfTest(std::string& error);

	template <typename Func>
	void ForEachVideoRollHit(const ChartCourse& course, const Note& note, f32 rollsPerSecond, Func callback, BranchType branch = BranchType::Normal)
	{
		if (!std::isfinite(rollsPerSecond) || rollsPerSecond <= 0.0f) return;
		const Time head = course.BeatToPlaybackTime(note.BeatTime, branch) + note.TimeOffset;
		const Time tail = course.BeatToPlaybackTime(note.GetEnd(), branch) + note.TimeOffset;
		for (i32 hit = 0; !IsBalloonNote(note.Type) || hit < note.BalloonPopCount; ++hit)
		{
			const Time time = head + Time::FromSec(static_cast<f64>(hit) / rollsPerSecond);
			if (time >= tail) break;
			callback(time, IsBalloonNote(note.Type) && hit == note.BalloonPopCount - 1);
		}
	}

	struct VideoExportComboTimeline
	{
		std::vector<Time> HitTimes;

		void Rebuild(const ChartCourse& course, BranchType branch)
		{
			Rebuild(course, BuildFixedVideoBranchRoute(course, branch));
		}
		void Rebuild(const ChartCourse& course, const VideoBranchRoute& route)
		{
			HitTimes.clear();
			for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			for (const Note& note : course.GetNotes(branch))
				if (IsComboNote(note.Type) && route.GetNoteBranch(course, note.BeatTime) == branch)
					HitTimes.push_back(course.BeatToPlaybackTime(note.BeatTime, branch) + note.TimeOffset);
			std::sort(HitTimes.begin(), HitTimes.end());
		}

		i32 GetMaxCombo() const { return static_cast<i32>(HitTimes.size()); }
		i32 GetCurrentCombo(Time time) const
		{
			return static_cast<i32>(std::upper_bound(HitTimes.begin(), HitTimes.end(), time) - HitTimes.begin());
		}
	};

	struct VideoExportSoundEvent
	{
		Time HitTime;
		SoundEffectType Sound;
		f32 Pan = 0.0f;
	};

	struct VideoExportSoundTimeline
	{
		std::vector<VideoExportSoundEvent> Events;

		void Rebuild(const ChartCourse& course, BranchType branch, f32 rollsPerSecond)
		{
			Rebuild(course, BuildFixedVideoBranchRoute(course, branch), rollsPerSecond);
		}
		void Rebuild(const ChartCourse& course, const VideoBranchRoute& route, f32 rollsPerSecond, f32 pan = 0.0f)
		{
			Events.clear();
			for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			for (const Note& note : course.GetNotes(branch))
			{
				if (route.GetNoteBranch(course, note.BeatTime) != branch) continue;
				const Time head = course.BeatToPlaybackTime(note.BeatTime, branch) + note.TimeOffset;
				if (IsLongNote(note.Type) && rollsPerSecond > 0.0f)
				{
					ForEachVideoRollHit(course, note, rollsPerSecond, [&](Time time, b8 pop)
					{
						Events.push_back({ time, pop ? SoundEffectType::Balloon : SoundEffectType::TaikoDon, pan });
					}, branch);
				}
				else if (IsComboNote(note.Type))
				{
					if (!IsKaNote(note.Type)) Events.push_back({ head, SoundEffectType::TaikoDon, pan });
					if (IsKaNote(note.Type) || IsKaDonNote(note.Type)) Events.push_back({ head, SoundEffectType::TaikoKa, pan });
				}
			}
			std::sort(Events.begin(), Events.end(), [](const VideoExportSoundEvent& left, const VideoExportSoundEvent& right) { return left.HitTime < right.HitTime; });
		}
	};

	struct VideoExportAudioSources
	{
		const Audio::PCMSampleBuffer* Song = nullptr;
		const Audio::PCMSampleBuffer* SoundEffects[EnumCount<SoundEffectType>] = {};
		Time SongOffset = Time::Zero();
		Time ContentStart = Time::Zero(), ContentEnd = Time::Zero();
		f32 FadeInSeconds = 0.0f, FadeOutSeconds = 0.0f;
		f32 SongVolume = 1.0f, DrumVolume = 1.0f, BalloonVolume = 1.0f;
	};

	void RenderVideoExportAudio(const VideoExportAudioSources& sources, const VideoExportSoundTimeline& sounds,
		Time chartStartTime, i64 firstOutputFrame, u32 outputFrameCount, std::vector<i16>& outInterleavedStereo);
}
