#include "chart_editor_video_writer.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstring>

namespace PeepoDrumKit
{
	struct VideoExportWriter::Impl
	{
		Microsoft::WRL::ComPtr<IMFSinkWriter> Writer;
		DWORD VideoStream = 0, AudioStream = 0;
		u32 Width = 0, Height = 0, FramesPerSecond = 0;
		b8 ComInitialized = false, MediaFoundationInitialized = false, Writing = false;
		std::string Error;

		b8 Check(HRESULT result, const char* operation)
		{
			if (SUCCEEDED(result)) return true;
			char message[128];
			sprintf_s(message, "%s failed (HRESULT 0x%08X)", operation, static_cast<u32>(result));
			Error = message;
			return false;
		}

		void Shutdown()
		{
			Writer.Reset();
			Writing = false;
			if (MediaFoundationInitialized) { MFShutdown(); MediaFoundationInitialized = false; }
			if (ComInitialized) { CoUninitialize(); ComInitialized = false; }
		}
	};

	VideoExportWriter::VideoExportWriter() : impl(std::make_unique<Impl>()) {}
	VideoExportWriter::~VideoExportWriter()
	{
		if (impl->Writing) impl->Writer->Finalize();
		impl->Shutdown();
	}

	b8 VideoExportWriter::Start(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate, u32 audioBitRate)
	{
		impl->Shutdown();
		impl->Error.clear();
		if (filePathUTF8.empty() || width == 0 || height == 0 || framesPerSecond == 0 || videoBitRate == 0 || !IsValidVideoAudioBitRate(audioBitRate) ||
			width % 2 != 0 || height % 2 != 0 || width > 8192 || height > 8192 || filePathUTF8.size() > INT_MAX)
		{
			impl->Error = "Invalid video export settings";
			return false;
		}
		const int pathLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filePathUTF8.data(), static_cast<int>(filePathUTF8.size()), nullptr, 0);
		if (pathLength <= 0) { impl->Error = "Invalid output file path"; return false; }
		std::wstring path(static_cast<size_t>(pathLength), L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filePathUTF8.data(), static_cast<int>(filePathUTF8.size()), path.data(), pathLength) != pathLength)
		{
			impl->Error = "Could not convert output file path";
			return false;
		}

		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return impl->Check(comResult, "CoInitializeEx");
		impl->ComInitialized = SUCCEEDED(comResult);
		if (!impl->Check(MFStartup(MF_VERSION), "MFStartup")) return false;
		impl->MediaFoundationInitialized = true;
		if (!impl->Check(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, impl->Writer.GetAddressOf()), "MFCreateSinkWriterFromURL")) return false;

		Microsoft::WRL::ComPtr<IMFMediaType> outputVideo, inputVideo, outputAudio, inputAudio;
		if (!impl->Check(MFCreateMediaType(outputVideo.GetAddressOf()), "MFCreateMediaType(video output)")) return false;
		if (!impl->Check(outputVideo->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video major type") ||
			!impl->Check(outputVideo->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Set H.264 type") ||
			!impl->Check(outputVideo->SetUINT32(MF_MT_AVG_BITRATE, videoBitRate), "Set video bit rate") ||
			!impl->Check(outputVideo->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set progressive video") ||
			!impl->Check(MFSetAttributeSize(outputVideo.Get(), MF_MT_FRAME_SIZE, width, height), "Set video frame size") ||
			!impl->Check(MFSetAttributeRatio(outputVideo.Get(), MF_MT_FRAME_RATE, framesPerSecond, 1), "Set video frame rate") ||
			!impl->Check(MFSetAttributeRatio(outputVideo.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set video pixel aspect ratio") ||
			!impl->Check(impl->Writer->AddStream(outputVideo.Get(), &impl->VideoStream), "Add video stream")) return false;

		if (!impl->Check(MFCreateMediaType(inputVideo.GetAddressOf()), "MFCreateMediaType(video input)")) return false;
		if (!impl->Check(inputVideo->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video input major type") ||
			!impl->Check(inputVideo->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 input type") ||
			!impl->Check(inputVideo->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4), "Set top-down video stride") ||
			!impl->Check(inputVideo->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set progressive input") ||
			!impl->Check(MFSetAttributeSize(inputVideo.Get(), MF_MT_FRAME_SIZE, width, height), "Set input frame size") ||
			!impl->Check(MFSetAttributeRatio(inputVideo.Get(), MF_MT_FRAME_RATE, framesPerSecond, 1), "Set input frame rate") ||
			!impl->Check(MFSetAttributeRatio(inputVideo.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set input pixel aspect ratio") ||
			!impl->Check(impl->Writer->SetInputMediaType(impl->VideoStream, inputVideo.Get(), nullptr), "Set video input media type")) return false;

		static constexpr u32 audioSampleRate = 44100, audioChannels = 2, audioBitsPerSample = 16;
		if (!impl->Check(MFCreateMediaType(outputAudio.GetAddressOf()), "MFCreateMediaType(audio output)")) return false;
		if (!impl->Check(outputAudio->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set audio major type") ||
			!impl->Check(outputAudio->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC), "Set AAC type") ||
			!impl->Check(outputAudio->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioSampleRate), "Set audio sample rate") ||
			!impl->Check(outputAudio->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels), "Set audio channel count") ||
			!impl->Check(outputAudio->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audioBitsPerSample), "Set audio bit depth") ||
			!impl->Check(outputAudio->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audioBitRate / 8), "Set AAC byte rate") ||
			!impl->Check(impl->Writer->AddStream(outputAudio.Get(), &impl->AudioStream), "Add audio stream")) return false;

		if (!impl->Check(MFCreateMediaType(inputAudio.GetAddressOf()), "MFCreateMediaType(audio input)")) return false;
		if (!impl->Check(inputAudio->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set PCM major type") ||
			!impl->Check(inputAudio->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Set PCM input type") ||
			!impl->Check(inputAudio->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioSampleRate), "Set PCM sample rate") ||
			!impl->Check(inputAudio->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels), "Set PCM channel count") ||
			!impl->Check(inputAudio->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audioBitsPerSample), "Set PCM bit depth") ||
			!impl->Check(inputAudio->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, audioChannels * audioBitsPerSample / 8), "Set PCM block alignment") ||
			!impl->Check(inputAudio->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, audioSampleRate * audioChannels * audioBitsPerSample / 8), "Set PCM byte rate") ||
			!impl->Check(impl->Writer->SetInputMediaType(impl->AudioStream, inputAudio.Get(), nullptr), "Set audio input media type") ||
			!impl->Check(impl->Writer->BeginWriting(), "Begin writing video")) return false;

		impl->Width = width; impl->Height = height; impl->FramesPerSecond = framesPerSecond;
		impl->Writing = true;
		return true;
	}

	b8 VideoExportWriter::WriteVideoFrame(const u8* bgraPixels, u32 sourceStride, i64 frameIndex)
	{
		if (!impl->Writing || !bgraPixels || sourceStride < impl->Width * 4 || frameIndex < 0) return false;
		const DWORD byteCount = impl->Width * impl->Height * 4;
		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		Microsoft::WRL::ComPtr<IMFSample> sample;
		if (!impl->Check(MFCreateMemoryBuffer(byteCount, buffer.GetAddressOf()), "Create video buffer")) return false;
		BYTE* target = nullptr;
		if (!impl->Check(buffer->Lock(&target, nullptr, nullptr), "Lock video buffer")) return false;
		const HRESULT copyResult = MFCopyImage(target, impl->Width * 4, bgraPixels, sourceStride, impl->Width * 4, impl->Height);
		buffer->Unlock();
		if (!impl->Check(copyResult, "Copy video frame") ||
			!impl->Check(buffer->SetCurrentLength(byteCount), "Set video buffer length") ||
			!impl->Check(MFCreateSample(sample.GetAddressOf()), "Create video sample") ||
			!impl->Check(sample->AddBuffer(buffer.Get()), "Add video buffer")) return false;
		const LONGLONG start = frameIndex * 10000000 / impl->FramesPerSecond;
		const LONGLONG end = (frameIndex + 1) * 10000000 / impl->FramesPerSecond;
		return impl->Check(sample->SetSampleTime(start), "Set video sample time") &&
			impl->Check(sample->SetSampleDuration(end - start), "Set video sample duration") &&
			impl->Check(impl->Writer->WriteSample(impl->VideoStream, sample.Get()), "Write video sample");
	}

	b8 VideoExportWriter::WriteAudioSamples(const i16* interleavedStereoSamples, u32 frameCount, i64 firstSampleIndex)
	{
		if (!impl->Writing || !interleavedStereoSamples || frameCount == 0 || firstSampleIndex < 0 || frameCount > UINT32_MAX / 4) return false;
		const DWORD byteCount = frameCount * 2 * sizeof(i16);
		Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
		Microsoft::WRL::ComPtr<IMFSample> sample;
		if (!impl->Check(MFCreateMemoryBuffer(byteCount, buffer.GetAddressOf()), "Create audio buffer")) return false;
		BYTE* target = nullptr;
		if (!impl->Check(buffer->Lock(&target, nullptr, nullptr), "Lock audio buffer")) return false;
		std::memcpy(target, interleavedStereoSamples, byteCount);
		buffer->Unlock();
		if (!impl->Check(buffer->SetCurrentLength(byteCount), "Set audio buffer length") ||
			!impl->Check(MFCreateSample(sample.GetAddressOf()), "Create audio sample") ||
			!impl->Check(sample->AddBuffer(buffer.Get()), "Add audio buffer")) return false;
		const LONGLONG start = firstSampleIndex * 10000000 / 44100;
		const LONGLONG end = (firstSampleIndex + frameCount) * 10000000 / 44100;
		return impl->Check(sample->SetSampleTime(start), "Set audio sample time") &&
			impl->Check(sample->SetSampleDuration(end - start), "Set audio sample duration") &&
			impl->Check(impl->Writer->WriteSample(impl->AudioStream, sample.Get()), "Write audio sample");
	}

	b8 VideoExportWriter::Finish()
	{
		if (!impl->Writing) { impl->Shutdown(); return false; }
		const b8 succeeded = impl->Check(impl->Writer->Finalize(), "Finalize video export");
		impl->Shutdown();
		return succeeded;
	}

	std::string_view VideoExportWriter::GetError() const { return impl->Error; }
}
