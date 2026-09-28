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

	struct VideoExportComboTimeline
	{
		std::vector<Time> HitTimes;

		void Rebuild(const ChartCourse& course, BranchType branch)
		{
			HitTimes.clear();
			for (const Note& note : course.GetNotes(branch))
				if (IsComboNote(note.Type))
					HitTimes.push_back(course.TempoMap.BeatToTime(note.BeatTime) + note.TimeOffset);
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
	};

	struct VideoExportSoundTimeline
	{
		std::vector<VideoExportSoundEvent> Events;

		void Rebuild(const ChartCourse& course, BranchType branch, f32 rollsPerSecond)
		{
			Events.clear();
			const Time hitInterval = rollsPerSecond > 0.0f ? Time::FromSec(1.0 / rollsPerSecond) : Time::Zero();
			for (const Note& note : course.GetNotes(branch))
			{
				const Time head = course.TempoMap.BeatToTime(note.BeatTime) + note.TimeOffset;
				if (IsLongNote(note.Type) && rollsPerSecond > 0.0f)
				{
					const Time tail = course.TempoMap.BeatToTime(note.GetEnd()) + note.TimeOffset;
					const i32 hitsInDuration = IsBalloonNote(note.Type)
						? static_cast<i32>(std::floor(std::max(0.0, (tail - head).ToSec()) * rollsPerSecond)) + 1
						: static_cast<i32>(std::ceil(std::max(0.0, (tail - head).ToSec()) * rollsPerSecond)) + 1;
					const i32 hitCount = IsBalloonNote(note.Type) ? std::min(note.BalloonPopCount, hitsInDuration) : hitsInDuration;
					for (i32 hit = 0; hit < hitCount; ++hit)
					{
						const b8 pop = IsBalloonNote(note.Type) && hit == note.BalloonPopCount - 1;
						Events.push_back({ head + hitInterval * hit, pop ? SoundEffectType::Balloon : SoundEffectType::TaikoDon });
					}
				}
				else if (IsComboNote(note.Type))
				{
					if (!IsKaNote(note.Type)) Events.push_back({ head, SoundEffectType::TaikoDon });
					if (IsKaNote(note.Type) || IsKaDonNote(note.Type)) Events.push_back({ head, SoundEffectType::TaikoKa });
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
