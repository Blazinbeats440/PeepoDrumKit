#pragma once

#include "core_types.h"
#include <memory>
#include <string>
#include <string_view>

namespace PeepoDrumKit
{
	class VideoExportWriter : NonCopyable
	{
	public:
		VideoExportWriter();
		~VideoExportWriter();

		b8 Start(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate);
		b8 WriteVideoFrame(const u8* bgraPixels, u32 sourceStride, i64 frameIndex);
		b8 WriteAudioSamples(const i16* interleavedStereoSamples, u32 frameCount, i64 firstSampleIndex);
		b8 Finish();
		std::string_view GetError() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
