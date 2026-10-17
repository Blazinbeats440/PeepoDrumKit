#include "chart_editor_video_writer.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstring>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>

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

	b8 VideoExportWriter::Start(std::string_view filePathUTF8, u32 width, u32 height, u32 fps, u32 videoBitRate, u32 audioBitRate, b8 useHardwareEncoding)
	{
		if (StartInternal(filePathUTF8, width, height, fps, videoBitRate, audioBitRate, useHardwareEncoding)) return true;
		return useHardwareEncoding && StartInternal(filePathUTF8, width, height, fps, videoBitRate, audioBitRate, false);
	}

	b8 VideoExportWriter::StartInternal(std::string_view filePathUTF8, u32 width, u32 height, u32 framesPerSecond, u32 videoBitRate, u32 audioBitRate, b8 useHardwareEncoding)
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
		Microsoft::WRL::ComPtr<IMFAttributes> attributes;
		if (!impl->Check(MFCreateAttributes(attributes.GetAddressOf(), 2), "Create writer attributes") ||
			!impl->Check(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE), "Disable writer throttling") ||
			!impl->Check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, useHardwareEncoding), "Set hardware encoding preference") ||
			!impl->Check(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes.Get(), impl->Writer.GetAddressOf()), "MFCreateSinkWriterFromURL")) return false;

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

	VideoExportWriteStatus VideoExportWriter::GetWriteStatus()
	{
		if (!impl->Writing) return VideoExportWriteStatus::Failed;
		u64 queuedBytes = 0;
		for (DWORD stream : { impl->VideoStream, impl->AudioStream })
		{
			MF_SINK_WRITER_STATISTICS statistics {};
			statistics.cb = sizeof(statistics);
			if (!impl->Check(impl->Writer->GetStatistics(stream, &statistics), "Get writer queue statistics"))
				return VideoExportWriteStatus::Failed;
			queuedBytes += statistics.dwByteCountQueued;
		}
		const u64 frameBytes = static_cast<u64>(impl->Width) * impl->Height * 4;
		const u64 audioBytes = (44100 / impl->FramesPerSecond + 1) * 4;
		const u64 queueLimit = std::max<u64>(64 * 1024 * 1024, (frameBytes + audioBytes) * 3);
		return queuedBytes + frameBytes + audioBytes <= queueLimit ? VideoExportWriteStatus::Ready : VideoExportWriteStatus::Pending;
	}

	b8 VideoExportWriter::Finish()
	{
		if (!impl->Writing) { impl->Shutdown(); return false; }
		const b8 succeeded = impl->Check(impl->Writer->Finalize(), "Finalize video export");
		impl->Shutdown();
		return succeeded;
	}

	std::string_view VideoExportWriter::GetError() const { return impl->Error; }
	void VideoExportWriter::Abort() { impl->Shutdown(); }

	struct VideoExportAsyncWriter::Impl
	{
		mutable std::mutex Mutex;
		std::condition_variable Changed;
		std::thread Worker;
		std::deque<VideoExportFrame> Frames, RecycledFrames;
		VideoExportWorkerProgress Progress;
		b8 CancelRequested = false, FinishRequested = false, Processing = false;
		u32 Width = 0, Height = 0;

		void Complete(VideoExportWorkerStatus status, std::string error = {})
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Frames.clear(); RecycledFrames.clear(); Processing = false;
			Progress.Status = status;
			Progress.Error = std::move(error);
		}
		void Run(std::string path, u32 fps, u32 videoBitRate, u32 audioBitRate, b8 hardware)
		{
			VideoExportWriter writer;
			if (!writer.Start(path, Width, Height, fps, videoBitRate, audioBitRate, hardware))
			{
				const std::string error(writer.GetError());
				writer.Abort();
				Complete(VideoExportWorkerStatus::Failed, error);
				return;
			}
			{ std::lock_guard<std::mutex> lock(Mutex); Progress.Status = VideoExportWorkerStatus::Writing; }
			for (;;)
			{
				VideoExportFrame frame;
				{
					std::unique_lock<std::mutex> lock(Mutex);
					Changed.wait(lock, [&] { return CancelRequested || FinishRequested || !Frames.empty(); });
					if (CancelRequested) break;
					if (Frames.empty())
					{
						Progress.Status = VideoExportWorkerStatus::Finalizing;
						lock.unlock();
						const b8 succeeded = writer.Finish();
						const std::string error(writer.GetError());
						lock.lock();
						const b8 canceled = CancelRequested;
						lock.unlock();
						Complete(canceled ? VideoExportWorkerStatus::Canceled : succeeded ? VideoExportWorkerStatus::Finished : VideoExportWorkerStatus::Failed, error);
						return;
					}
					frame = std::move(Frames.front()); Frames.pop_front(); Processing = true;
				}
				VideoExportWriteStatus capacity;
				while ((capacity = writer.GetWriteStatus()) == VideoExportWriteStatus::Pending)
				{
					std::unique_lock<std::mutex> lock(Mutex);
					if (Changed.wait_for(lock, std::chrono::milliseconds(1), [&] { return CancelRequested; })) break;
				}
				{ std::lock_guard<std::mutex> lock(Mutex); if (CancelRequested) break; }
				if (capacity == VideoExportWriteStatus::Failed ||
					!writer.WriteVideoFrame(frame.Pixels.data(), Width * 4, frame.Index) ||
					!writer.WriteAudioSamples(frame.AudioSamples.data(), static_cast<u32>(frame.AudioSamples.size() / 2), frame.FirstAudioSample))
				{
					const std::string error(writer.GetError());
					writer.Abort();
					Complete(VideoExportWorkerStatus::Failed, error);
					return;
				}
				{
					std::lock_guard<std::mutex> lock(Mutex);
					++Progress.WrittenFrames; Processing = false;
					RecycledFrames.push_back(std::move(frame));
				}
			}
			writer.Abort();
			Complete(VideoExportWorkerStatus::Canceled);
		}
	};
	VideoExportAsyncWriter::VideoExportAsyncWriter() : impl(std::make_unique<Impl>()) {}
	VideoExportAsyncWriter::~VideoExportAsyncWriter() { Cancel(); Wait(); }
	void VideoExportAsyncWriter::Start(std::string path, u32 width, u32 height, u32 fps, u32 videoBitRate, u32 audioBitRate, b8 hardware)
	{
		Cancel(); Wait();
		{
			std::lock_guard<std::mutex> lock(impl->Mutex);
			impl->Progress = {}; impl->Progress.Status = VideoExportWorkerStatus::Starting;
			impl->Frames.clear(); impl->RecycledFrames.clear();
			impl->CancelRequested = impl->FinishRequested = impl->Processing = false;
			impl->Width = width; impl->Height = height;
		}
		impl->Worker = std::thread([state = impl.get(), path = std::move(path), fps, videoBitRate, audioBitRate, hardware]() mutable
			{ state->Run(std::move(path), fps, videoBitRate, audioBitRate, hardware); });
	}
	b8 VideoExportAsyncWriter::TrySubmit(VideoExportFrame& frame)
	{
		std::lock_guard<std::mutex> lock(impl->Mutex);
		if (impl->Progress.Status != VideoExportWorkerStatus::Writing || impl->CancelRequested || impl->FinishRequested ||
			impl->Frames.size() + impl->Processing >= MaxPendingFrames || frame.Index != impl->Progress.SubmittedFrames) return false;
		if (frame.Pixels.size() != static_cast<size_t>(impl->Width) * impl->Height * 4 || frame.AudioSamples.empty() || frame.AudioSamples.size() % 2 != 0) return false;
		impl->Frames.push_back(std::move(frame)); frame = {};
		if (!impl->RecycledFrames.empty()) { frame = std::move(impl->RecycledFrames.front()); impl->RecycledFrames.pop_front(); }
		++impl->Progress.SubmittedFrames;
		impl->Changed.notify_one();
		return true;
	}
	VideoExportWorkerProgress VideoExportAsyncWriter::GetProgress() const
	{
		std::lock_guard<std::mutex> lock(impl->Mutex);
		auto progress = impl->Progress;
		progress.PendingFrames = impl->Frames.size() + impl->Processing;
		return progress;
	}
	void VideoExportAsyncWriter::Finish() { { std::lock_guard<std::mutex> lock(impl->Mutex); impl->FinishRequested = true; } impl->Changed.notify_one(); }
	void VideoExportAsyncWriter::Cancel() { { std::lock_guard<std::mutex> lock(impl->Mutex); impl->CancelRequested = true; } impl->Changed.notify_one(); }
	void VideoExportAsyncWriter::Wait() { if (impl->Worker.joinable()) impl->Worker.join(); }

	b8 RunVideoAsyncWriterSelfTest(std::string& error)
	{
		error.clear();
		const auto fail = [&](std::string message) { error = std::move(message); return false; };
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized)) return fail("Could not initialize test COM");
		defer { CoUninitialize(); };
		if (FAILED(MFStartup(MF_VERSION))) return fail("Could not initialize test Media Foundation");
		defer { MFShutdown(); };
		const std::string prefix = "build/video-async-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
		VideoExportAsyncWriter writer;
		const auto awaitTerminal = [&]
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			for (;;)
			{
				const auto status = writer.GetProgress().Status;
				if (status == VideoExportWorkerStatus::Finished || status == VideoExportWorkerStatus::Failed || status == VideoExportWorkerStatus::Canceled) return true;
				if (std::chrono::steady_clock::now() >= deadline) return false;
				Sleep(1);
			}
		};
		for (b8 hardware : { false, true })
		{
			const std::string path = prefix + (hardware ? "-hardware.mp4" : "-software.mp4");
			writer.Start(path, 320, 180, 30, 2000000, 192000, hardware);
			VideoExportFrame frame;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			for (i64 index = 0; index < 30; ++index)
			{
				frame.Index = index; frame.FirstAudioSample = index * 1470;
				frame.Pixels.resize(320 * 180 * 4); frame.AudioSamples.assign(1470 * 2, 0);
				for (size_t pixel = 0; pixel < frame.Pixels.size(); pixel += 4)
				{
					for (i32 channel = 0; channel < 3; ++channel) frame.Pixels[pixel + channel] = (index & (1LL << channel)) ? 200 : 30;
					frame.Pixels[pixel + 3] = 255;
				}
				while (!writer.TrySubmit(frame))
				{
					const auto progress = writer.GetProgress();
					if (progress.Status == VideoExportWorkerStatus::Failed) return fail(progress.Error);
					if (progress.PendingFrames > VideoExportAsyncWriter::MaxPendingFrames) return fail("Frame queue exceeded its limit");
					if (std::chrono::steady_clock::now() >= deadline) return fail("Frame submission timed out");
					Sleep(1);
				}
			}
			writer.Finish();
			if (!awaitTerminal()) return fail("Finalizing the asynchronous writer timed out");
			writer.Wait();
			const auto progress = writer.GetProgress();
			if (progress.Status != VideoExportWorkerStatus::Finished || progress.SubmittedFrames != 30 || progress.WrittenFrames != 30 || progress.PendingFrames != 0)
				return fail("Unexpected completed writer state: " + progress.Error);
			Microsoft::WRL::ComPtr<IMFAttributes> attributes;
			Microsoft::WRL::ComPtr<IMFSourceReader> reader;
			Microsoft::WRL::ComPtr<IMFMediaType> type;
			const std::wstring widePath(path.begin(), path.end());
			if (FAILED(MFCreateAttributes(attributes.GetAddressOf(), 1)) || FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) ||
				FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), attributes.Get(), reader.GetAddressOf())) ||
				FAILED(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE)) || FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)) ||
				FAILED(MFCreateMediaType(type.GetAddressOf())) || FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
				FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) || FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get())))
				return fail("Could not open the generated video for decoding");
			i64 decodedFrames = 0;
			for (;;)
			{
				DWORD flags = 0;
				LONGLONG timestamp = 0;
				Microsoft::WRL::ComPtr<IMFSample> sample;
				if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, sample.GetAddressOf()))) return fail("Could not decode generated video");
				if (sample)
				{
					Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
					BYTE* pixels = nullptr; DWORD length = 0;
					if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())) || FAILED(buffer->Lock(&pixels, nullptr, &length))) return fail("Could not read decoded pixels");
					defer { buffer->Unlock(); };
					if (length < 4 || std::abs(timestamp - decodedFrames * 10000000 / 30) > 1) return fail("Decoded video timestamp changed");
					for (i32 channel = 0; channel < 3; ++channel)
						if (std::abs(static_cast<i32>(pixels[channel]) - ((decodedFrames & (1LL << channel)) ? 200 : 30)) > 12) return fail("Video frame order or color changed");
					++decodedFrames;
				}
				if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
			}
			if (decodedFrames != 30) return fail("Decoded video frame count changed");
		}
		writer.Start(prefix + "-canceled.mp4", 320, 180, 30, 2000000, 192000, false);
		writer.Cancel();
		if (!awaitTerminal()) return fail("Canceling the asynchronous writer timed out");
		writer.Wait();
		if (writer.GetProgress().Status != VideoExportWorkerStatus::Canceled || writer.GetProgress().PendingFrames != 0) return fail("Canceled writer retained frames");
		writer.Start(prefix + "-missing-directory/output.mp4", 320, 180, 30, 2000000, 192000, false);
		if (!awaitTerminal()) return fail("Failed writer did not terminate");
		writer.Wait();
		if (writer.GetProgress().Status != VideoExportWorkerStatus::Failed || writer.GetProgress().Error.empty()) return fail("Writer startup error was not reported");
		return true;
	}
}
