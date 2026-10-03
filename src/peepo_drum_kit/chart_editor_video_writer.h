#pragma once

#include "core_types.h"
#include <memory>
#include <string>
#include <string_view>

namespace PeepoDrumKit
{
	struct VideoAudioBitRatePreset { cstr Name; u32 BitRate; };
	inline constexpr VideoAudioBitRatePreset VideoAudioBitRatePresets[] = {
		{ "96 kbps", 96000 },
		{ "128 kbps", 128000 },
		{ "160 kbps", 160000 },
		{ "192 kbps", 192000 },
		{ "256 kbps", 256000 },
		{ "320 kbps", 320000 },
	};
	inline constexpr b8 IsValidVideoAudioBitRate(u32 bitRate)
	{
		for (const auto& preset : VideoAudioBitRatePresets)
			if (preset.BitRate == bitRate) return true;
		return false;
	}

	class VideoExportWriter : NonCopyable
	{
	public:
		VideoExportWriter();
		~VideoExportWriter();

		b8 Start(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate, u32 audioBitRate);
		b8 WriteVideoFrame(const u8* bgraPixels, u32 sourceStride, i64 frameIndex);
		b8 WriteAudioSamples(const i16* interleavedStereoSamples, u32 frameCount, i64 firstSampleIndex);
		b8 Finish();
		std::string_view GetError() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
