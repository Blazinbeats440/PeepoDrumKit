#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chart_editor_background_movie.h"
#include "core_string.h"
#include "core_io.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace PeepoDrumKit
{
	BackgroundMovieDefinition ReadBackgroundMovieDefinition(const std::map<std::string, std::string>& metadata)
	{
		BackgroundMovieDefinition result;
		for (const auto& [key, value] : metadata)
		{
			if (ASCII::MatchesInsensitive(key, "BGMOVIE")) result.FileName = ASCII::Trim(value);
			if (ASCII::MatchesInsensitive(key, "MOVIEOFFSET") &&
				(!ASCII::TryParse(ASCII::Trim(value), result.Offset.Seconds) || !std::isfinite(result.Offset.Seconds)))
				result.Error = "Invalid MOVIEOFFSET";
		}
		return result;
	}

	static void SetBackgroundMovieMetadata(std::map<std::string, std::string>& metadata, std::string_view key, std::string value)
	{
		for (auto entry = metadata.begin(); entry != metadata.end();)
		{
			if (ASCII::MatchesInsensitive(entry->first, key)) entry = metadata.erase(entry);
			else ++entry;
		}
		if (!value.empty()) metadata.emplace(key, std::move(value));
	}

	void SetBackgroundMovieFileName(std::map<std::string, std::string>& metadata, std::string_view filePath, std::string_view chartFilePath)
	{
		std::string path { ASCII::Trim(filePath) };
		if (!path.empty() && !chartFilePath.empty() && !Path::IsRelative(path))
		{
			if (auto relativePath = Path::TryMakeRelative(path, chartFilePath); !relativePath.empty()) path = std::move(relativePath);
		}
		SetBackgroundMovieMetadata(metadata, "BGMOVIE", Path::NormalizeInPlace(path));
	}

	void SetBackgroundMovieOffset(std::map<std::string, std::string>& metadata, Time offset)
	{
		if (std::isfinite(offset.Seconds)) SetBackgroundMovieMetadata(metadata, "MOVIEOFFSET", ASCII::ToString(offset.Seconds));
	}

	using Microsoft::WRL::ComPtr;
	static constexpr f64 MovieTicksPerSecond = 10000000.0;

	struct MovieDecoder
	{
		ComPtr<IMFSourceReader> Reader;
		ComPtr<IMFSample> Current, Next;
		LONGLONG CurrentTime = 0, NextTime = 0, CurrentDuration = 0, Duration = -1, FrameDuration = 333333;
		ivec2 Size = {}, CurrentSize = {}, NextSize = {};
		ivec2 DisplaySize = {}, CurrentDisplaySize = {}, NextDisplaySize = {};
		ivec2 CropOrigin = {}, CurrentCropOrigin = {}, NextCropOrigin = {};
		LONG Stride = 0, CurrentStride = 0, NextStride = 0;
		b8 EndOfStream = false, PositionInitialized = false;
		std::string Error;

		b8 Check(HRESULT result, const char* operation)
		{
			if (SUCCEEDED(result)) return true;
			char message[160];
			sprintf_s(message, "%s failed (HRESULT 0x%08X)", operation, static_cast<u32>(result));
			Error = message;
			return false;
		}

		b8 ReadFormat()
		{
			ComPtr<IMFMediaType> type;
			UINT32 width = 0, height = 0, stride = 0, numerator = 0, denominator = 0;
			GUID subtype = {};
			if (!Check(Reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type), "GetCurrentMediaType") ||
				!Check(type->GetGUID(MF_MT_SUBTYPE, &subtype), "Get video format") ||
				!Check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height), "Get video size")) return false;
			if (subtype != MFVideoFormat_RGB32 || width == 0 || height == 0 || width > 8192 || height > 8192 ||
				static_cast<u64>(width) * height * 4 > 256 * 1024 * 1024)
			{
				Error = "Unsupported background video format or size";
				return false;
			}
			Size = { static_cast<i32>(width), static_cast<i32>(height) };
			DisplaySize = Size;
			CropOrigin = {};
			MFVideoArea area = {};
			const auto readAperture = [&](IMFMediaType* source)
			{
				UINT32 bytes = 0;
				return (SUCCEEDED(source->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &bytes)) && bytes == sizeof(area)) ||
					(SUCCEEDED(source->GetBlob(MF_MT_GEOMETRIC_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &bytes)) && bytes == sizeof(area));
			};
			b8 hasAperture = readAperture(type.Get());
			if (!hasAperture)
			{
				ComPtr<IMFMediaType> nativeType;
				if (SUCCEEDED(Reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType))) hasAperture = readAperture(nativeType.Get());
			}
			if (hasAperture && area.Area.cx > 0 && area.Area.cy > 0 && area.OffsetX.value >= 0 && area.OffsetY.value >= 0 &&
				area.OffsetX.value + area.Area.cx <= Size.x && area.OffsetY.value + area.Area.cy <= Size.y)
			{
				DisplaySize = { area.Area.cx, area.Area.cy };
				CropOrigin = { area.OffsetX.value, area.OffsetY.value };
			}
			Stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride) : Size.x * 4;
			if (SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && numerator != 0 && denominator != 0)
				FrameDuration = std::max<LONGLONG>(1, static_cast<LONGLONG>(MovieTicksPerSecond * denominator / numerator));
			return true;
		}

		b8 Open(std::string_view path)
		{
			*this = MovieDecoder{};
			if (path.empty() || path.size() > INT_MAX || path.find('\0') != std::string_view::npos)
			{
				Error = "Invalid background video path";
				return false;
			}
			const std::wstring widePath = UTF8::Widen(path);
			if (widePath.empty()) { Error = "Invalid background video path"; return false; }
			const DWORD fileAttributes = GetFileAttributesW(widePath.c_str());
			if (fileAttributes == INVALID_FILE_ATTRIBUTES)
			{
				Error = "Could not access background video file (Win32 error " + std::to_string(GetLastError()) + ")";
				return false;
			}
			if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) { Error = "BGMOVIE must specify a video file"; return false; }
			ComPtr<IMFAttributes> attributes;
			ComPtr<IMFMediaType> type;
			if (!Check(MFCreateAttributes(&attributes, 1), "MFCreateAttributes") ||
				!Check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "Enable video color conversion") ||
				!Check(MFCreateSourceReaderFromURL(widePath.c_str(), attributes.Get(), &Reader), "Open background video") ||
				!Check(Reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE), "Disable media streams") ||
				!Check(Reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE), "Select video stream") ||
				!Check(MFCreateMediaType(&type), "MFCreateMediaType") ||
				!Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video type") ||
				!Check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 format") ||
				!Check(Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()), "Configure video decoder") ||
				!ReadFormat()) return false;
			PROPVARIANT duration = {};
			if (SUCCEEDED(Reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) && duration.vt == VT_UI8 &&
				duration.uhVal.QuadPart <= static_cast<ULONGLONG>(I64Max)) Duration = static_cast<LONGLONG>(duration.uhVal.QuadPart);
			PropVariantClear(&duration);
			return true;
		}

		b8 Seek(LONGLONG time)
		{
			PositionInitialized = true;
			Current.Reset(); Next.Reset(); EndOfStream = false;
			PROPVARIANT position = {};
			position.vt = VT_I8;
			position.hVal.QuadPart = time;
			return Check(Reader->SetCurrentPosition(GUID_NULL, position), "Seek background video");
		}

		b8 ReadSample(ComPtr<IMFSample>& sample, LONGLONG& time)
		{
			DWORD flags = 0;
			sample.Reset();
			if (!Check(Reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &time, &sample), "Decode background video")) return false;
			if (flags & MF_SOURCE_READERF_ERROR) { Error = "Background video decoder reported an error"; return false; }
			if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !ReadFormat()) return false;
			if (flags & MF_SOURCE_READERF_ENDOFSTREAM) EndOfStream = true;
			return true;
		}

		b8 CopyPixels(BackgroundMovieFrame& frame)
		{
			ComPtr<IMFMediaBuffer> buffer;
			if (!Check(Current->ConvertToContiguousBuffer(&buffer), "Get video pixels")) return false;
			ComPtr<IMF2DBuffer> buffer2D;
			BYTE* pixels = nullptr;
			LONG pitch = CurrentStride;
			DWORD length = 0;
			const b8 has2D = SUCCEEDED(buffer.As(&buffer2D));
			if (has2D)
			{
				if (!Check(buffer2D->Lock2D(&pixels, &pitch), "Lock video pixels")) return false;
			}
			else if (!Check(buffer->Lock(&pixels, nullptr, &length), "Lock video pixels")) return false;
			defer { if (has2D) buffer2D->Unlock2D(); else buffer->Unlock(); };
			const size_t rowBytes = static_cast<size_t>(CurrentDisplaySize.x) * 4;
			const size_t sourceRowBytes = static_cast<size_t>(CurrentSize.x) * 4;
			const u64 absolutePitch = pitch < 0 ? -static_cast<i64>(pitch) : pitch;
			if (!pixels || absolutePitch < sourceRowBytes || (!has2D && absolutePitch * (CurrentSize.y - 1) + sourceRowBytes > length))
			{
				Error = "Invalid background video pixel buffer";
				return false;
			}
			if (!has2D && pitch < 0) pixels += absolutePitch * (CurrentSize.y - 1);
			pixels += static_cast<ptrdiff_t>(pitch) * CurrentCropOrigin.y + CurrentCropOrigin.x * 4;
			frame.Size = CurrentDisplaySize;
			frame.Pixels.resize(rowBytes * CurrentDisplaySize.y);
			for (i32 row = 0; row < CurrentDisplaySize.y; ++row)
			{
				u8* destination = frame.Pixels.data() + rowBytes * row;
				memcpy(destination, pixels + static_cast<ptrdiff_t>(pitch) * row, rowBytes);
				for (size_t alpha = 3; alpha < rowBytes; alpha += 4) destination[alpha] = 255;
			}
			return true;
		}

		BackgroundMovieStatus Decode(LONGLONG target, BackgroundMovieFrame& frame, const std::function<b8()>& canceled)
		{
			if (!Error.empty()) return BackgroundMovieStatus::Failed;
			if (target < 0 || (Duration >= 0 && target >= Duration)) return BackgroundMovieStatus::Inactive;
			if (!PositionInitialized)
			{
				PositionInitialized = true;
				if (target > 0 && !Seek(target)) return BackgroundMovieStatus::Failed;
			}
			if (Current && (target < CurrentTime || target - CurrentTime > 2 * MovieTicksPerSecond))
				if (!Seek(target)) return BackgroundMovieStatus::Failed;
			LONGLONG seekBackoff = static_cast<LONGLONG>(MovieTicksPerSecond);
			for (;;)
			{
				if (canceled()) return BackgroundMovieStatus::Pending;
				if (!Current)
				{
					if (EndOfStream) return BackgroundMovieStatus::Inactive;
					if (!ReadSample(Current, CurrentTime)) return BackgroundMovieStatus::Failed;
					CurrentSize = Size; CurrentStride = Stride;
					CurrentDisplaySize = DisplaySize; CurrentCropOrigin = CropOrigin;
					if (!Current) continue;
					if (CurrentTime > target && target > 0 && seekBackoff > 0)
					{
						const LONGLONG earlier = std::max<LONGLONG>(0, target - seekBackoff);
						if (!Seek(earlier)) return BackgroundMovieStatus::Failed;
						seekBackoff = earlier == 0 ? 0 : seekBackoff + std::min(seekBackoff, target - seekBackoff);
						continue;
					}
				}
				if (!Next && !EndOfStream)
				{
					if (!ReadSample(Next, NextTime)) return BackgroundMovieStatus::Failed;
					NextSize = Size; NextStride = Stride;
					NextDisplaySize = DisplaySize; NextCropOrigin = CropOrigin;
					if (!Next && !EndOfStream) continue;
				}
				if (Next && NextTime <= target)
				{
					Current = std::move(Next); CurrentTime = NextTime;
					CurrentSize = NextSize; CurrentStride = NextStride;
					CurrentDisplaySize = NextDisplaySize; CurrentCropOrigin = NextCropOrigin;
					continue;
				}
				CurrentDuration = FrameDuration;
				LONGLONG sampleDuration = 0;
				if (SUCCEEDED(Current->GetSampleDuration(&sampleDuration)) && sampleDuration > 0) CurrentDuration = sampleDuration;
				const LONGLONG end = Next ? NextTime : CurrentTime + CurrentDuration;
				if (EndOfStream && !Next) Duration = end;
				if (target < CurrentTime || target >= end) return BackgroundMovieStatus::Inactive;
				frame.Start = Time::FromSec(CurrentTime / MovieTicksPerSecond);
				frame.End = Time::FromSec(end / MovieTicksPerSecond);
				return CopyPixels(frame) ? BackgroundMovieStatus::Ready : BackgroundMovieStatus::Failed;
			}
		}
	};

	struct BackgroundMovieReader::Impl
	{
		static constexpr size_t MaxCachedFrames = 16, MaxCachedBytes = 64 * 1024 * 1024;
		static constexpr LONGLONG ReadAheadTicks = 2500000;
		std::mutex Mutex;
		std::condition_variable Changed;
		std::thread Worker;
		b8 Stopping = false, InvalidTime = false;
		std::string Path, PublishedPath, Error;
		LONGLONG Target = 0, PublishedTarget = 0, Duration = -1;
		u64 RequestRevision = 0, PathRevision = 0, Sequence = 0;
		BackgroundMovieStatus Status = BackgroundMovieStatus::Inactive;
		std::deque<BackgroundMovieFrame> Frames;
		size_t CachedBytes = 0;

		const BackgroundMovieFrame* FindFrame(LONGLONG target) const
		{
			const f64 time = target / MovieTicksPerSecond;
			for (const auto& frame : Frames)
				if (time >= frame.Start.Seconds && time < frame.End.Seconds) return &frame;
			return nullptr;
		}

		void CacheFrame(BackgroundMovieFrame frame)
		{
			if (FindFrame(static_cast<LONGLONG>(std::llround(frame.Start.Seconds * MovieTicksPerSecond)))) return;
			// A frame larger than the cache budget is retained on its own.
			while (!Frames.empty() && (Frames.size() >= MaxCachedFrames || CachedBytes + frame.Pixels.size() > MaxCachedBytes))
			{
				CachedBytes -= Frames.front().Pixels.size();
				Frames.pop_front();
			}
			frame.Sequence = ++Sequence;
			CachedBytes += frame.Pixels.size();
			Frames.push_back(std::move(frame));
		}

		~Impl()
		{
			{ std::lock_guard<std::mutex> lock(Mutex); Stopping = true; }
			Changed.notify_one();
			if (Worker.joinable()) Worker.join();
		}

		void Run()
		{
			const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const HRESULT mediaResult = SUCCEEDED(comResult) ? MFStartup(MF_VERSION) : comResult;
			defer { if (SUCCEEDED(mediaResult)) MFShutdown(); if (SUCCEEDED(comResult)) CoUninitialize(); };
			MovieDecoder decoder;
			u64 decoderPathRevision = 0;
			u64 handled = 0;
			for (;;)
			{
				std::unique_lock<std::mutex> lock(Mutex);
				Changed.wait(lock, [&] { return Stopping || RequestRevision != handled; });
				if (Stopping) return;
				const std::string path = Path;
				const LONGLONG target = Target;
				const u64 revision = RequestRevision;
				const u64 pathRevision = PathRevision;
				const b8 cached = FindFrame(target) != nullptr;
				lock.unlock();
				BackgroundMovieFrame frame;
				BackgroundMovieStatus status = BackgroundMovieStatus::Inactive;
				if (path.empty()) { decoder = MovieDecoder{}; decoderPathRevision = pathRevision; }
				else
				{
					if (pathRevision != decoderPathRevision)
					{
						decoderPathRevision = pathRevision;
						if (FAILED(mediaResult)) decoder.Check(mediaResult, "Initialize video decoder");
						else decoder.Open(path);
					}
					status = cached ? BackgroundMovieStatus::Ready : decoder.Decode(target, frame, [&]
					{
						std::lock_guard<std::mutex> requestLock(Mutex);
						return Stopping || PathRevision != pathRevision || InvalidTime || Target < target || Target - target > ReadAheadTicks;
					});
				}
				lock.lock();
				handled = revision;
				if (PathRevision != pathRevision || InvalidTime || status == BackgroundMovieStatus::Pending) continue;
				Status = status;
				Error = decoder.Error;
				PublishedPath = path;
				PublishedTarget = target;
				Duration = decoder.Duration;
				if (status == BackgroundMovieStatus::Ready && !frame.Pixels.empty()) CacheFrame(std::move(frame));

				// Publish the requested frame first, then fill a bounded buffer ahead of the current cursor.
				while (!Stopping && !InvalidTime && PathRevision == pathRevision && status == BackgroundMovieStatus::Ready)
				{
					const auto* current = FindFrame(Target);
					if (!current) break;
					LONGLONG nextTarget = static_cast<LONGLONG>(std::llround(current->End.Seconds * MovieTicksPerSecond));
					while (const auto* next = FindFrame(nextTarget))
						nextTarget = static_cast<LONGLONG>(std::llround(next->End.Seconds * MovieTicksPerSecond));
					if (nextTarget - Target > ReadAheadTicks || (Duration >= 0 && nextTarget >= Duration)) break;
					const size_t frameBytes = current->Pixels.size();
					while (!Frames.empty() && (Frames.size() >= MaxCachedFrames || CachedBytes + frameBytes > MaxCachedBytes) &&
						Frames.front().End.Seconds <= Target / MovieTicksPerSecond)
					{
						CachedBytes -= Frames.front().Pixels.size();
						Frames.pop_front();
					}
					if (Frames.size() >= MaxCachedFrames || CachedBytes + frameBytes > MaxCachedBytes) break;
					lock.unlock();
					BackgroundMovieFrame ahead;
					const auto aheadStatus = decoder.Decode(nextTarget, ahead, [&]
					{
						std::lock_guard<std::mutex> requestLock(Mutex);
						return Stopping || InvalidTime || PathRevision != pathRevision ||
							(!FindFrame(Target) && (Target < nextTarget || Target - nextTarget > ReadAheadTicks));
					});
					lock.lock();
					if (PathRevision != pathRevision || InvalidTime || aheadStatus == BackgroundMovieStatus::Pending) break;
					Duration = decoder.Duration;
					if (aheadStatus != BackgroundMovieStatus::Ready) break;
					if (CachedBytes + ahead.Pixels.size() > MaxCachedBytes) break;
					CacheFrame(std::move(ahead));
				}
			}
		}
	};

	BackgroundMovieReader::BackgroundMovieReader() : impl(std::make_unique<Impl>()) {}
	BackgroundMovieReader::~BackgroundMovieReader() = default;

	void BackgroundMovieReader::Request(std::string_view path, Time time)
	{
		std::unique_lock<std::mutex> lock(impl->Mutex);
		if (!std::isfinite(time.Seconds) || std::abs(time.Seconds) >= I64Max / MovieTicksPerSecond)
		{
			impl->InvalidTime = true;
			impl->Error = "Invalid background video time";
			return;
		}
		const b8 recovering = impl->InvalidTime;
		if (recovering)
		{
			impl->InvalidTime = false; impl->PublishedPath.clear(); impl->Error.clear();
			impl->Frames.clear(); impl->CachedBytes = 0;
			++impl->PathRevision;
		}
		const LONGLONG target = static_cast<LONGLONG>(std::llround(time.Seconds * MovieTicksPerSecond));
		if (!recovering && impl->Path == path && impl->Target == target) return;
		if (impl->Path != path)
		{
			++impl->PathRevision;
			impl->PublishedPath.clear(); impl->Error.clear(); impl->Duration = -1;
			impl->Frames.clear(); impl->CachedBytes = 0;
			impl->Status = BackgroundMovieStatus::Pending;
		}
		impl->Path = path;
		impl->Target = target;
		++impl->RequestRevision;
		if (!impl->Worker.joinable() && !path.empty())
		{
			Impl* workerImpl = impl.get();
			impl->Worker = std::thread([workerImpl] { workerImpl->Run(); });
		}
		lock.unlock();
		impl->Changed.notify_one();
	}

	BackgroundMovieStatus BackgroundMovieReader::Poll(BackgroundMovieFrame& frame)
	{
		std::lock_guard<std::mutex> lock(impl->Mutex);
		if (impl->InvalidTime) return BackgroundMovieStatus::Failed;
		if (impl->Path.empty()) return BackgroundMovieStatus::Inactive;
		if (impl->PublishedPath != impl->Path) return BackgroundMovieStatus::Pending;
		if (impl->Status == BackgroundMovieStatus::Failed) return BackgroundMovieStatus::Failed;
		if (impl->Target < 0 || (impl->Duration >= 0 && impl->Target >= impl->Duration)) return BackgroundMovieStatus::Inactive;
		if (const auto* cached = impl->FindFrame(impl->Target))
		{
			if (frame.Sequence != cached->Sequence) frame = *cached;
			return BackgroundMovieStatus::Ready;
		}
		return impl->Status == BackgroundMovieStatus::Inactive && impl->Target == impl->PublishedTarget
			? BackgroundMovieStatus::Inactive : BackgroundMovieStatus::Pending;
	}

	std::string BackgroundMovieReader::GetError() const
	{
		std::lock_guard<std::mutex> lock(impl->Mutex);
		return impl->Error;
	}

	void BackgroundMovieReader::Close() { Request({}, Time::Zero()); }
}
