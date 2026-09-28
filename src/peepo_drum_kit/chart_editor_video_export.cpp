#include "chart_editor_video_export.h"
#include <cmath>

namespace PeepoDrumKit
{
	static f32 SampleChannel(const Audio::PCMSampleBuffer* source, i64 frame, u32 channel)
	{
		if (!source || !source->InterleavedSamples || source->SampleRate != Audio::Engine.OutputSampleRate ||
			source->ChannelCount == 0 || frame < 0 || frame >= source->FrameCount)
			return 0.0f;
		const u32 sourceChannel = std::min(channel, source->ChannelCount - 1);
		return static_cast<f32>(source->InterleavedSamples[frame * source->ChannelCount + sourceChannel]);
	}

	void RenderVideoExportAudio(const VideoExportAudioSources& sources, const VideoExportSoundTimeline& sounds,
		Time chartStartTime, i64 firstOutputFrame, u32 outputFrameCount, std::vector<i16>& outInterleavedStereo)
	{
		constexpr i64 sampleRate = Audio::Engine.OutputSampleRate;
		std::vector<f32> mixed(static_cast<size_t>(outputFrameCount) * 2, 0.0f);
		const i64 firstSongFrame = static_cast<i64>(std::llround((chartStartTime - sources.SongOffset).ToSec() * sampleRate)) + firstOutputFrame;
		for (u32 frame = 0; frame < outputFrameCount; ++frame)
			for (u32 channel = 0; channel < 2; ++channel)
				mixed[static_cast<size_t>(frame) * 2 + channel] = SampleChannel(sources.Song, firstSongFrame + frame, channel) * sources.SongVolume;

		for (const VideoExportSoundEvent& event : sounds.Events)
		{
			const auto* source = sources.SoundEffects[EnumToIndex(event.Sound)];
			if (!source || !source->InterleavedSamples || source->SampleRate != sampleRate) continue;
			const i64 eventFrame = static_cast<i64>(std::llround((event.HitTime - chartStartTime).ToSec() * sampleRate));
			const i64 firstOverlap = std::max(firstOutputFrame, eventFrame);
			const i64 lastOverlap = std::min(firstOutputFrame + outputFrameCount, eventFrame + source->FrameCount);
			if (firstOverlap >= lastOverlap) continue;
			const f32 volume = event.Sound == SoundEffectType::Balloon ? sources.BalloonVolume : sources.DrumVolume;
			for (i64 frame = firstOverlap; frame < lastOverlap; ++frame)
				for (u32 channel = 0; channel < 2; ++channel)
					mixed[static_cast<size_t>(frame - firstOutputFrame) * 2 + channel] += SampleChannel(source, frame - eventFrame, channel) * volume;
		}

		outInterleavedStereo.resize(mixed.size());
		for (u32 frame = 0; frame < outputFrameCount; ++frame)
		{
			const f64 time = chartStartTime.ToSec() + static_cast<f64>(firstOutputFrame + frame) / sampleRate;
			f64 gain = 1.0;
			if (sources.FadeInSeconds > 0.0f && time < sources.ContentStart.ToSec())
			{
				const f64 progress = std::clamp((time - sources.ContentStart.ToSec() + sources.FadeInSeconds) / (sources.FadeInSeconds * 0.9), 0.0, 1.0);
				gain = 1.0 - (1.0 - progress) * (1.0 - progress);
			}
			if (sources.FadeOutSeconds > 0.0f)
			{
				const f64 progress = std::clamp((time - sources.ContentEnd.ToSec() - sources.FadeOutSeconds * 0.2) /
					std::max(1.0 / sampleRate, sources.FadeOutSeconds * 0.8 - 1.0 / sampleRate), 0.0, 1.0);
				gain *= 1.0 - progress * progress;
			}
			for (u32 channel = 0; channel < 2; ++channel)
			{
				const size_t sample = static_cast<size_t>(frame) * 2 + channel;
				outInterleavedStereo[sample] = static_cast<i16>(std::round(std::clamp(mixed[sample] * gain, -32768.0, 32767.0)));
			}
		}
	}
}
