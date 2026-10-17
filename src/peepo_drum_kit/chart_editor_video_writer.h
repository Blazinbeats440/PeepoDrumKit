#pragma once

#include "core_types.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

	enum class VideoExportWriteStatus { Ready, Pending, Failed };

	class VideoExportWriter : NonCopyable
	{
	public:
		VideoExportWriter();
		~VideoExportWriter();

		b8 Start(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate, u32 audioBitRate, b8 useHardwareEncoding = false);
		b8 WriteVideoFrame(const u8* bgraPixels, u32 sourceStride, i64 frameIndex);
		b8 WriteAudioSamples(const i16* interleavedStereoSamples, u32 frameCount, i64 firstSampleIndex);
		VideoExportWriteStatus GetWriteStatus();
		b8 Finish();
		void Abort();
		std::string_view GetError() const;

	private:
		b8 StartInternal(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate, u32 audioBitRate, b8 useHardwareEncoding);
		struct Impl;
		std::unique_ptr<Impl> impl;
	};

	struct VideoExportFrame
	{
		i64 Index = 0, FirstAudioSample = 0;
		std::vector<u8> Pixels;
		std::vector<i16> AudioSamples;
	};
	enum class VideoExportWorkerStatus { Idle, Starting, Writing, Finalizing, Finished, Canceled, Failed };
	struct VideoExportWorkerProgress
	{
		VideoExportWorkerStatus Status = VideoExportWorkerStatus::Idle;
		i64 SubmittedFrames = 0, WrittenFrames = 0;
		size_t PendingFrames = 0;
		std::string Error;
	};
	class VideoExportAsyncWriter : NonCopyable
	{
	public:
		static constexpr size_t MaxPendingFrames = 3;
		VideoExportAsyncWriter();
		~VideoExportAsyncWriter();
		void Start(std::string filePath, u32 width, u32 height, u32 fps, u32 videoBitRate, u32 audioBitRate, b8 useHardwareEncoding);
		b8 TrySubmit(VideoExportFrame& frame);
		VideoExportWorkerProgress GetProgress() const;
		void Finish();
		void Cancel();
		void Wait();
	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
	b8 RunVideoAsyncWriterSelfTest(std::string& error);
}
