#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chart_editor.h"
#include "core_io.h"
#include <cmath>
#include <filesystem>
#include <Windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace PeepoDrumKit
{
	static BranchType VideoBranchForCourse(const ChartCourse& course, BranchType branch)
	{
		return course.Branches.empty() ? BranchType::Normal : branch;
	}

	b8 SaveScreenshotPNG(u32 width, u32 height, const std::vector<u8>& pixels, std::string& outputPath)
	{
		if (width == 0 || height == 0 || pixels.size() != static_cast<size_t>(width) * height * 4 || pixels.size() > UINT_MAX) return false;
		const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
		defer { if (SUCCEEDED(initialized)) ::CoUninitialize(); };
		IWICImagingFactory* factory = nullptr;
		IWICBitmapEncoder* encoder = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		IStream* stream = nullptr;
		defer { if (frame) frame->Release(); if (encoder) encoder->Release(); if (stream) stream->Release(); if (factory) factory->Release(); };
		if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
			FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
			FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
			FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
			FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
			FAILED(frame->SetSize(width, height))) return false;
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) ||
			FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()))) ||
			FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
		STATSTG statistics = {};
		HGLOBAL memory = nullptr;
		if (FAILED(stream->Stat(&statistics, STATFLAG_NONAME)) || statistics.cbSize.HighPart != 0 ||
			FAILED(::GetHGlobalFromStream(stream, &memory))) return false;
		const void* png = ::GlobalLock(memory);
		if (!png) return false;
		defer { ::GlobalUnlock(memory); };
		const std::string executableDirectory = Directory::GetExecutableDirectory();
		if (executableDirectory.empty()) return false;
		const std::string directory = executableDirectory + "/Screenshot";
		if (!Directory::Exists(directory) && !Directory::Create(directory) && !Directory::Exists(directory)) return false;
		SYSTEMTIME now = {};
		::GetLocalTime(&now);
		char timestamp[32];
		sprintf_s(timestamp, "%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
		for (i32 index = 0; index < 1000; ++index)
		{
			outputPath = directory + "/" + timestamp + (index == 0 ? std::string{} : "_" + std::to_string(index + 1)) + ".png";
			HANDLE file = ::CreateFileW(UTF8::WideArg(outputPath).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				if (::GetLastError() == ERROR_FILE_EXISTS || ::GetLastError() == ERROR_ALREADY_EXISTS) continue;
				return false;
			}
			defer { ::CloseHandle(file); };
			DWORD written = 0;
			return ::WriteFile(file, png, statistics.cbSize.LowPart, &written, nullptr) != 0 && written == statistics.cbSize.LowPart;
		}
		return false;
	}

	void ChartEditor::DrawScreenshotPreview()
	{
		if (!screenshotStatus.empty() && Gui::GetTime() < screenshotStatusUntil)
		{
			Gui::SetNextWindowViewport(Gui::GetMainViewport()->ID);
			Gui::SetNextWindowPos(vec2(Gui::GetMainViewport()->WorkPos) + vec2(16.0f));
			if (Gui::Begin("##ScreenshotStatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing))
				Gui::TextUnformatted(screenshotStatus.c_str());
			Gui::End();
		}
		if (!screenshotPreviewRequested) return;
		screenshotPreviewRequested = false;
		if (!context.ChartSelectedCourse || videoExport.Exporting || videoExport.Preparing)
		{
			screenshotMovieWaiting = false;
			return;
		}
		ConfigureVideoPreview(screenshotPreview);
		screenshotPreview.VideoExportBranch = VideoBranchForCourse(*context.ChartSelectedCourse, context.ChartSelectedBranch);
		if (!screenshotMovieWaiting) screenshotPreview.VideoExportTime = context.GetCursorTime();
		screenshotPreview.VideoFadeInSeconds = screenshotPreview.VideoFadeOutSeconds = 0.0f;
		screenshotPreview.VideoExportCombos.Rebuild(*context.ChartSelectedCourse, screenshotPreview.VideoExportBranch);
		Gui::SetNextWindowViewport(Gui::GetMainViewport()->ID);
		Gui::SetNextWindowPos(vec2(Gui::GetMainViewport()->Pos) + vec2(16.0f));
		Gui::SetNextWindowSize(vec2(Gui::GetMainViewport()->Size) * 0.8f);
		if (Gui::Begin("##ScreenshotRender", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing))
		{
			ImGuiWindow* window = Gui::GetCurrentWindow();
			ImDrawList* originalDrawList = window->DrawList;
			screenshotDrawList = IM_NEW(ImDrawList)(Gui::GetDrawListSharedData());
			screenshotDrawList->_ResetForNewFrame();
			screenshotDrawList->PushTexture(originalDrawList->_CmdHeader.TexRef);
			screenshotDrawList->PushClipRect(originalDrawList->GetClipRectMin(), originalDrawList->GetClipRectMax());
			window->DrawList = screenshotDrawList;
			screenshotPreview.DrawGui(context, *screenshotPreview.VideoExportTime);
			screenshotDrawList->ChannelsMerge();
			window->DrawList = originalDrawList;
			if (screenshotPreview.VideoUseBackgroundMovie && screenshotPreview.MovieStatus == BackgroundMovieStatus::Pending)
			{
				screenshotPreviewRequested = screenshotMovieWaiting = true;
				IM_DELETE(screenshotDrawList);
				screenshotDrawList = nullptr;
			}
			else
			{
				screenshotMovieWaiting = false;
				if (screenshotPreview.VideoUseBackgroundMovie && !screenshotPreview.MovieError.empty())
				{
					IM_DELETE(screenshotDrawList);
					screenshotDrawList = nullptr;
					screenshotStatus = std::string(UI_Str("BACKGROUND_MOVIE_FAILED")) + "\n" + screenshotPreview.MovieError;
					screenshotStatusUntil = Gui::GetTime() + 6.0;
				}
			}
		}
		Gui::End();
	}

	void ChartEditor::SavePendingScreenshots()
	{
		const auto report = [&](b8 success, const std::string& path)
		{
			screenshotStatus = std::string(success ? UI_Str("SCREENSHOT_SAVED") : UI_Str("SCREENSHOT_FAILED")) + (path.empty() ? "" : "\n" + path);
			screenshotStatusUntil = Gui::GetTime() + 6.0;
		};
		if (screenshotWindowRequested && screenshotWindowDelay++ > 0)
		{
			screenshotWindowRequested = false;
			screenshotWindowDelay = 0;
			u32 width = 0, height = 0;
			std::vector<u8> pixels;
			std::string path;
			const b8 success = ApplicationHost::CaptureWindowToBGRA(width, height, pixels) && SaveScreenshotPNG(width, height, pixels, path);
			report(success, path);
		}
		if (screenshotDrawList)
		{
			const auto& resolution = VideoResolutionPresets[videoExport.Resolution];
			std::vector<u8> pixels;
			std::string path;
			const b8 success = ApplicationHost::CaptureDrawListToBGRA(screenshotDrawList, screenshotPreview.VideoExportViewport,
				resolution.Width, resolution.Height, pixels) && SaveScreenshotPNG(resolution.Width, resolution.Height, pixels, path);
			IM_DELETE(screenshotDrawList);
			screenshotDrawList = nullptr;
			report(success, path);
		}
	}

	static b8 LoadVideoBackgroundImage(std::string_view path, CustomDraw::GPUTexture& texture)
	{
		if (path.empty()) return false;
		auto [fileContent, fileSize] = File::ReadAllBytes(path);
		if (fileContent == nullptr || fileSize == 0) return false;
		SvgRasterizer rasterizer = {};
		if (!rasterizer.ParseMemory({ reinterpret_cast<char*>(fileContent.get()), fileSize })) return false;
		Rasterize(rasterizer, texture);
		return texture.IsValid();
	}

	static std::string SanitizeVideoFileTitle(std::string_view title)
	{
		constexpr size_t maxTitleBytes = 96;
		std::string result;
		for (size_t index = 0; index < title.size();)
		{
			const u8 firstByte = static_cast<u8>(title[index]);
			const size_t characterBytes = firstByte < 0x80 ? 1 : (firstByte & 0xE0) == 0xC0 ? 2
				: (firstByte & 0xF0) == 0xE0 ? 3 : (firstByte & 0xF8) == 0xF0 ? 4 : 1;
			if (result.size() + characterBytes > maxTitleBytes || index + characterBytes > title.size()) break;
			b8 validCharacter = true;
			for (size_t offset = 1; offset < characterBytes; ++offset)
				validCharacter &= (static_cast<u8>(title[index + offset]) & 0xC0) == 0x80;
			if (!validCharacter || (firstByte >= 0x80 && characterBytes == 1) || firstByte < 0x20 || firstByte == 0x7F ||
				title[index] == '<' || title[index] == '>' || title[index] == ':' || title[index] == '"' ||
				title[index] == '/' || title[index] == '\\' || title[index] == '|' || title[index] == '?' || title[index] == '*')
				result += '_';
			else result.append(title.substr(index, characterBytes));
			index += validCharacter ? characterBytes : 1;
		}
		while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
		while (!result.empty() && (result.front() == ' ' || result.front() == '.')) result.erase(result.begin());
		return result.empty() || result.find_first_not_of('_') == std::string::npos ? "chart" : result;
	}

	static std::string MakeVideoOutputBasePath(std::string_view directory, std::string_view title,
		std::string_view branchName, u32 height, i32 framesPerSecond)
	{
		SYSTEMTIME now = {};
		::GetLocalTime(&now);
		char suffix[64];
		sprintf_s(suffix, "_%up%dfps_%04u%02u%02u_%02u%02u", height, framesPerSecond,
			static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
			static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute));
		std::string base = std::string(directory) + "/" + SanitizeVideoFileTitle(title);
		if (!branchName.empty()) base += "_" + SanitizeVideoFileTitle(branchName);
		base += suffix;
		return base;
	}

	static std::string NumberedVideoOutputPath(std::string_view basePath, i32 index)
	{
		std::string path(basePath);
		if (index > 0) path += "_" + std::to_string(index + 1);
		return path + ".mp4";
	}

	static std::string MakeAvailableVideoOutputPath(std::string_view basePath)
	{
		for (i32 index = 0; index < 1000; ++index)
		{
			std::string path = NumberedVideoOutputPath(basePath, index);
			if (!File::Exists(path)) return path;
		}
		return {};
	}

	static std::string MakeTemporaryVideoPath(std::string_view outputPath)
	{
		for (i32 index = 0; index < 1000; ++index)
		{
			std::string path = std::string(outputPath) + ".partial";
			if (index > 0) path += std::to_string(index);
			path += ".mp4";
			if (!File::Exists(path)) return path;
		}
		return {};
	}

	static b8 DiscardTemporaryVideo(VideoExportWriter& writer, std::string_view path)
	{
		writer.Finish();
		if (path.empty()) return true;
		std::error_code error;
		std::filesystem::remove(std::filesystem::u8path(path), error);
		return !error;
	}

	static b8 CommitTemporaryVideo(std::string_view temporaryPath, std::string_view outputBasePath, std::string& outputPath)
	{
		const UTF8::WideArg temporaryWide(temporaryPath);
		for (i32 index = 0; index < 1000; ++index)
		{
			std::string candidate = NumberedVideoOutputPath(outputBasePath, index);
			if (File::Exists(candidate)) continue;
			if (::MoveFileExW(temporaryWide.c_str(), UTF8::WideArg(candidate).c_str(), MOVEFILE_WRITE_THROUGH) != 0)
			{
				outputPath = std::move(candidate);
				return true;
			}
			const DWORD error = ::GetLastError();
			if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || File::Exists(candidate)) continue;
			::SetLastError(error);
			return false;
		}
		::SetLastError(ERROR_ALREADY_EXISTS);
		return false;
	}

	static void FormatVideoExportTime(char (&buffer)[32], f64 seconds)
	{
		const i64 wholeSeconds = static_cast<i64>(std::max(0.0, std::round(seconds)));
		if (wholeSeconds >= 3600)
			sprintf_s(buffer, "%lld:%02lld:%02lld", wholeSeconds / 3600, (wholeSeconds / 60) % 60, wholeSeconds % 60);
		else
			sprintf_s(buffer, "%02lld:%02lld", wholeSeconds / 60, wholeSeconds % 60);
	}

	void ChartEditor::ConfigureVideoPreview(ChartGamePreview& preview)
	{
		auto& exportData = videoExport;
		if (!exportData.BackgroundInitialized)
		{
			if (exportData.BackgroundColor == 0)
				exportData.BackgroundColor = Gui::GetColorU32(ImGuiCol_WindowBg);
			if (exportData.BackgroundSource == -1 || exportData.BackgroundSource == 1)
			{
				if (File::Exists("assets/background.png") && LoadVideoBackgroundImage("assets/background.png", exportData.DefaultBackgroundTexture))
					exportData.BackgroundSource = 1;
				else
				{
					if (exportData.BackgroundSource == 1) exportData.Status = UI_Str("VIDEO_EXPORT_BACKGROUND_IMAGE_FAILED");
					exportData.BackgroundSource = 0;
				}
			}
			if (exportData.BackgroundSource == 2 && !LoadVideoBackgroundImage(exportData.BackgroundImagePath, exportData.CustomBackgroundTexture))
				exportData.Status = UI_Str("VIDEO_EXPORT_BACKGROUND_IMAGE_FAILED");
			exportData.BackgroundInitialized = true;
		}
		preview.VideoExportDrawList = nullptr;
		preview.VideoExportPixelAligned = false;
		preview.VideoExportResolutionWidth = VideoResolutionPresets[exportData.Resolution].Width;
		preview.VideoExportBranch = VideoBranchForCourse(*context.ChartSelectedCourse,
			exportData.Branch <= VideoBranchMode::Master ? static_cast<BranchType>(exportData.Branch) : BranchType::Normal);
		preview.VideoExportLayout = exportData.Layout;
		preview.VideoShowTitle = exportData.ShowTitle;
		preview.VideoShowSubtitle = exportData.ShowSubtitle;
		preview.VideoShowDifficulty = exportData.ShowDifficulty;
		preview.VideoShowMaxCombo = exportData.ShowMaxCombo;
		preview.VideoShowCurrentCombo = exportData.ShowCurrentCombo;
		preview.VideoTitleScale = exportData.TitleScale;
		preview.VideoTitlePaddingScale = exportData.TitlePaddingScale;
		preview.VideoTitleAlignment = exportData.TitleAlignment;
		preview.VideoTitleVerticalPosition = exportData.TitleVerticalPosition;
		preview.VideoTitleColor = exportData.TitleColor;
		preview.VideoTitleBandColor = exportData.TitleBandColor;
		preview.VideoBackgroundColor = Gui::ColorU32WithNewAlpha(exportData.BackgroundColor, 1.0f);
		preview.VideoLaneBackgroundOpacity = 1.0f - exportData.LaneBackgroundTransparency / 100.0f;
		preview.VideoBackgroundTexture = exportData.BackgroundSource == 1 ? &exportData.DefaultBackgroundTexture
			: exportData.BackgroundSource == 2 ? &exportData.CustomBackgroundTexture
			: exportData.BackgroundSource == 3 || exportData.BackgroundSource == 4 ? &context.JacketTexture : nullptr;
		preview.VideoUseBackgroundMovie = exportData.BackgroundSource == 4;
		preview.VideoBackgroundImageFit = exportData.BackgroundImageFit;
	}

	void ChartEditor::DrawVideoExportWindow()
	{
		auto& exportData = videoExport;
		if (!exportData.ShowWindow) return;
		exportData.FramePrepared = false;
		if (context.ChartSelectedCourse == nullptr)
		{
			if (exportData.Exporting) DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath);
			exportData.Preparing = false;
			exportData.Exporting = false;
			exportData.ShowWindow = false;
			return;
		}
		Gui::SetNextWindowSize(vec2(900.0f, 760.0f), ImGuiCond_FirstUseEver);
		Gui::SetNextWindowViewport(Gui::GetMainViewport()->ID);
		if (!Gui::Begin(UI_Str("VIDEO_EXPORT_WINDOW"), (exportData.Exporting || exportData.Preparing) ? nullptr : &exportData.ShowWindow,
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
		{
			Gui::End();
			return;
		}
		defer { Gui::End(); };
		ConfigureVideoPreview(exportData.Preview);

		const auto* song = context.SongSource == Audio::SourceHandle::Invalid ? nullptr : Audio::Engine.GetSourceSampleBufferView(context.SongSource);
		const ChartCourse& course = *context.ChartSelectedCourse;
		const Time songEnd = song && song->SampleRate > 0 ? context.Chart.SongOffset + Audio::FramesToTime(song->FrameCount, song->SampleRate) : Time::Zero();
		const Time fullEnd = course.GetPlaybackTimeBounds(std::max(context.Chart.GetUsedDuration(course), songEnd)).second;
		const b8 hasRange = context.RangeSelection.IsActiveAndHasEnd() && context.RangeSelection.GetDuration() > Beat::Zero();
		const b8 hasPreview = context.Chart.SongDemoStartTime > Time::Zero();
		const b8 hasMarker = context.Marker.IsActive;
		using RangeMode = VideoExportData::RangeMode;
		if ((exportData.Range == RangeMode::SelectedRange && !hasRange) ||
			(exportData.Range == RangeMode::Preview && !hasPreview) ||
			(exportData.Range == RangeMode::Marker && !hasMarker))
			exportData.Range = RangeMode::Full;

		Gui::BeginChild("##VideoExportOptions", vec2(0.0f, std::min(360.0f, Gui::GetContentRegionAvail().y * 0.55f)), true);
		Gui::BeginDisabled(exportData.Exporting || exportData.Preparing);
		const char* rangeNames[4] = { UI_Str("VIDEO_EXPORT_RANGE_FULL") };
		RangeMode rangeModes[4] = { RangeMode::Full };
		i32 rangeCount = 1;
		if (hasRange) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_SELECTED"); rangeModes[rangeCount++] = RangeMode::SelectedRange; }
		if (hasPreview) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_PREVIEW"); rangeModes[rangeCount++] = RangeMode::Preview; }
		if (hasMarker) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_MARKER"); rangeModes[rangeCount++] = RangeMode::Marker; }
		i32 rangeIndex = 0;
		for (i32 index = 0; index < rangeCount; ++index)
			if (rangeModes[index] == exportData.Range) rangeIndex = index;
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_RANGE_MODE"), &rangeIndex, rangeNames, rangeCount))
			exportData.Range = rangeModes[rangeIndex];
		if (exportData.Range == RangeMode::Preview || exportData.Range == RangeMode::Marker)
		{
			if (Gui::InputFloat(UI_Str("VIDEO_EXPORT_EXCERPT_SECONDS"), &exportData.ExcerptSeconds, 0.0f, 0.0f, "%.1f s"))
				exportData.ExcerptSeconds = std::isfinite(exportData.ExcerptSeconds) ? Clamp(exportData.ExcerptSeconds, 0.1f, 3600.0f) : 15.0f;
		}
		if (gamePreview.RecordedVideoRoute.Course && gamePreview.RecordedVideoRoute.Changes != context.Undo.NumberOfChangesMade)
			gamePreview.RecordedVideoRoute = {};
		const b8 hasRecording = gamePreview.RecordedVideoRoute.IsValid(&course, context.Undo.NumberOfChangesMade);
		const b8 hasScoreBranch = std::any_of(course.Branches.begin(), course.Branches.end(), [](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; });
		if (exportData.Branch == VideoBranchMode::TestPlay && !hasRecording) exportData.Branch = VideoBranchMode::Auto;
		const char* branchNames[] = { UI_Str("BRANCH_FORCED_NORMAL"), UI_Str("BRANCH_FORCED_EXPERT"), UI_Str("BRANCH_FORCED_MASTER"),
			UI_Str("VIDEO_EXPORT_BRANCH_AUTO"), UI_Str("VIDEO_EXPORT_BRANCH_TEST_PLAY") };
		if (!course.Branches.empty())
		{
			if (Gui::BeginCombo(UI_Str("VIDEO_EXPORT_BRANCH"), branchNames[static_cast<i32>(exportData.Branch)]))
			{
				for (i32 index = 0; index < (hasRecording ? 5 : 4); ++index)
				{
					const VideoBranchMode mode = static_cast<VideoBranchMode>(index);
					Gui::BeginDisabled(mode == VideoBranchMode::Auto && hasScoreBranch);
					if (Gui::Selectable(branchNames[index], exportData.Branch == mode)) exportData.Branch = mode;
					Gui::EndDisabled();
				}
				Gui::EndCombo();
			}
			if (hasScoreBranch) Gui::TextWrapped("%s", UI_Str("VIDEO_EXPORT_BRANCH_SCORE_UNAVAILABLE"));
		}
		const char* layoutNames[] = { UI_Str("VIDEO_EXPORT_LAYOUT_ORIGINAL"), UI_Str("VIDEO_EXPORT_LAYOUT_TAIKO") };
		i32 layoutIndex = static_cast<i32>(exportData.Layout);
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_LAYOUT"), &layoutIndex, layoutNames, ArrayCountI32(layoutNames)))
			exportData.Layout = static_cast<ChartGamePreview::VideoLayout>(layoutIndex);
		const char* resolutionNames[] = { VideoResolutionPresets[0].Name, VideoResolutionPresets[1].Name,
			VideoResolutionPresets[2].Name, VideoResolutionPresets[3].Name };
		Gui::Combo(UI_Str("VIDEO_EXPORT_RESOLUTION"), &exportData.Resolution, resolutionNames, ArrayCountI32(resolutionNames));
		const char* frameRateNames[] = { "15 fps", "30 fps", "60 fps", "120 fps" };
		static constexpr i32 frameRates[] = { 15, 30, 60, 120 };
		i32 frameRateIndex = 0;
		for (i32 index = 0; index < ArrayCountI32(frameRates); ++index)
			if (exportData.FramesPerSecond == frameRates[index]) frameRateIndex = index;
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_FPS"), &frameRateIndex, frameRateNames, ArrayCountI32(frameRateNames)))
			exportData.FramesPerSecond = frameRates[frameRateIndex];
		const char* audioBitRateNames[ArrayCountI32(VideoAudioBitRatePresets)];
		i32 audioBitRateIndex = 0;
		for (i32 index = 0; index < ArrayCountI32(VideoAudioBitRatePresets); ++index)
		{
			audioBitRateNames[index] = VideoAudioBitRatePresets[index].Name;
			if (exportData.AudioBitRate == VideoAudioBitRatePresets[index].BitRate) audioBitRateIndex = index;
		}
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_AUDIO_BIT_RATE"), &audioBitRateIndex, audioBitRateNames, ArrayCountI32(audioBitRateNames)))
			exportData.AudioBitRate = VideoAudioBitRatePresets[audioBitRateIndex].BitRate;
		const char* backgroundNames[] = { UI_Str("VIDEO_EXPORT_BACKGROUND_COLOR"), UI_Str("VIDEO_EXPORT_BACKGROUND_ASSET"),
			UI_Str("VIDEO_EXPORT_BACKGROUND_CUSTOM"), UI_Str("VIDEO_EXPORT_BACKGROUND_JACKET"), UI_Str("VIDEO_EXPORT_BACKGROUND_MOVIE") };
		const auto movieDefinition = ReadBackgroundMovieDefinition(context.Chart.OtherMetadata);
		const b8 hasBackgroundMovie = !movieDefinition.FileName.empty();
		if (!hasBackgroundMovie && exportData.BackgroundSource == 4) exportData.BackgroundSource = 0;
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_BACKGROUND"), &exportData.BackgroundSource, backgroundNames, hasBackgroundMovie ? 5 : 4) &&
			exportData.BackgroundSource == 1 && !exportData.DefaultBackgroundTexture.IsValid())
		{
			if (!LoadVideoBackgroundImage("assets/background.png", exportData.DefaultBackgroundTexture))
			{
				exportData.BackgroundSource = 0;
				exportData.Status = UI_Str("VIDEO_EXPORT_BACKGROUND_IMAGE_FAILED");
			}
		}
		if (exportData.BackgroundSource == 2)
		{
			if (Gui::Button(UI_Str("VIDEO_EXPORT_BACKGROUND_CHOOSE")))
			{
				Shell::FileDialog fileDialog {};
				fileDialog.InTitle = UI_Str("VIDEO_EXPORT_BACKGROUND_CHOOSE");
				fileDialog.InFilters = { { "Image Files", "*.jpg;*.jpeg;*.png" } };
				fileDialog.InParentWindowHandle = ApplicationHost::GlobalState.NativeWindowHandle;
				if (fileDialog.OpenRead() == Shell::FileDialogResult::OK)
				{
					CustomDraw::GPUTexture texture = {};
					if (LoadVideoBackgroundImage(fileDialog.OutFilePath, texture))
					{
						exportData.CustomBackgroundTexture = std::move(texture);
						exportData.BackgroundImagePath = fileDialog.OutFilePath;
						exportData.BackgroundSource = 2;
						exportData.Status.clear();
					}
					else exportData.Status = UI_Str("VIDEO_EXPORT_BACKGROUND_IMAGE_FAILED");
				}
			}
			if (!exportData.BackgroundImagePath.empty()) Gui::TextWrapped("%s", exportData.BackgroundImagePath.c_str());
		}
		Gui::ColorEdit3_U32(UI_Str("VIDEO_EXPORT_BACKGROUND_COLOR"), &exportData.BackgroundColor);
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_LANE_BACKGROUND_TRANSPARENCY"), &exportData.LaneBackgroundTransparency,
			0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
		const char* backgroundFitNames[] = { UI_Str("VIDEO_EXPORT_BACKGROUND_STRETCH"), UI_Str("VIDEO_EXPORT_BACKGROUND_CONTAIN"),
			UI_Str("VIDEO_EXPORT_BACKGROUND_CONTAIN_WIDTH"), UI_Str("VIDEO_EXPORT_BACKGROUND_WIDTH"), UI_Str("VIDEO_EXPORT_BACKGROUND_HEIGHT") };
		i32 backgroundFitIndex = static_cast<i32>(exportData.BackgroundImageFit);
		if (Gui::Combo(UI_Str("VIDEO_EXPORT_BACKGROUND_FIT"), &backgroundFitIndex, backgroundFitNames, ArrayCountI32(backgroundFitNames)))
			exportData.BackgroundImageFit = static_cast<ChartGamePreview::VideoBackgroundFit>(backgroundFitIndex);
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_SONG_VOLUME"), &exportData.SongVolume, 0.0f, 2.0f, "%.2f");
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_DRUM_VOLUME"), &exportData.DrumVolume, 0.0f, 2.0f, "%.2f");
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_LEAD_IN"), &exportData.LeadInSeconds, 0.0f, 5.0f, "%.1f s");
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_TAIL"), &exportData.TailSeconds, 0.0f, 5.0f, "%.1f s");
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_AUDIO_FADE"), &exportData.AudioFade);
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_TITLE"), &exportData.ShowTitle); Gui::SameLine();
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_DIFFICULTY"), &exportData.ShowDifficulty);
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_SUBTITLE"), &exportData.ShowSubtitle);
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_MAX_COMBO"), &exportData.ShowMaxCombo); Gui::SameLine();
		Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_CURRENT_COMBO"), &exportData.ShowCurrentCombo);
		f32 titleSizePercent = exportData.TitleScale * 100.0f;
		if (Gui::SliderFloat(UI_Str("VIDEO_EXPORT_TITLE_SIZE"), &titleSizePercent, 70.0f, 150.0f, "%.0f%%"))
			exportData.TitleScale = titleSizePercent / 100.0f;
		Gui::SliderFloat(UI_Str("VIDEO_EXPORT_TITLE_PADDING"), &exportData.TitlePaddingScale, 0.5f, 2.0f, "%.2f");
		const char* titleAlignmentNames[] = { UI_Str("VIDEO_EXPORT_TITLE_LEFT"), UI_Str("VIDEO_EXPORT_TITLE_CENTER") };
		Gui::Combo(UI_Str("VIDEO_EXPORT_TITLE_ALIGNMENT"), &exportData.TitleAlignment, titleAlignmentNames, ArrayCountI32(titleAlignmentNames));
		const char* titlePositionNames[] = { UI_Str("VIDEO_EXPORT_TITLE_TOP"), UI_Str("VIDEO_EXPORT_TITLE_BOTTOM") };
		Gui::Combo(UI_Str("VIDEO_EXPORT_TITLE_VERTICAL_POSITION"), &exportData.TitleVerticalPosition, titlePositionNames, ArrayCountI32(titlePositionNames));
		Gui::ColorEdit3_U32(UI_Str("VIDEO_EXPORT_TITLE_COLOR"), &exportData.TitleColor);
		Gui::ColorEdit4_U32(UI_Str("VIDEO_EXPORT_TITLE_BAND_COLOR"), &exportData.TitleBandColor, ImGuiColorEditFlags_AlphaBar);
		Gui::EndDisabled();
		Gui::EndChild();
		Time selectedStart = course.GetPlaybackTimeBounds().first, selectedEnd = fullEnd;
		if (exportData.Range == RangeMode::SelectedRange)
		{
			std::tie(selectedStart, selectedEnd) = context.GetRangePlaybackTimes();
		}
		else if (exportData.Range == RangeMode::Preview || exportData.Range == RangeMode::Marker)
		{
			selectedStart = exportData.Range == RangeMode::Preview
				? context.Chart.SongDemoStartTime + context.Chart.SongOffset : context.BeatToPlaybackTime(context.Marker.BeatTime);
			selectedEnd = selectedStart + Time::FromSec(exportData.ExcerptSeconds);
		}

		auto& preview = exportData.Preview;
		const f32 rollsPerSecond = *Settings.General.DrumrollPreviewRollsPerSecond;
		if (!exportData.Exporting && (exportData.RouteCourse != &course || exportData.RouteChanges != context.Undo.NumberOfChangesMade
			|| exportData.RouteMode != exportData.Branch || exportData.RouteRollSpeed != rollsPerSecond
			|| exportData.RouteRecordingVersion != gamePreview.VideoRecordingVersion))
		{
			exportData.RouteCourse = &course;
			exportData.RouteChanges = context.Undo.NumberOfChangesMade;
			exportData.RouteMode = exportData.Branch;
			exportData.RouteRollSpeed = rollsPerSecond;
			exportData.RouteRecordingVersion = gamePreview.VideoRecordingVersion;
			exportData.RouteReady = true;
			if (exportData.Branch == VideoBranchMode::Auto)
				exportData.RouteReady = BuildAutoVideoBranchRoute(course, rollsPerSecond, exportData.Route);
			else if (exportData.Branch == VideoBranchMode::TestPlay)
			{
				exportData.RouteReady = hasRecording;
				exportData.Route = gamePreview.RecordedVideoRoute.Route;
			}
			else exportData.Route = BuildFixedVideoBranchRoute(course, static_cast<BranchType>(exportData.Branch));
			preview.VideoExportCombos.Rebuild(course, exportData.Route);
			preview.VideoScrollDecisionCount = SIZE_MAX;
		}
		preview.VideoExportRoute = &exportData.Route;
		preview.VideoRollsPerSecond = exportData.RouteRollSpeed;
		ConfigureVideoPreview(preview);
		preview.VideoExportPixelAligned = true;
		preview.VideoFadeContentStart = exportData.Exporting ? exportData.ContentStartTime : selectedStart;
		preview.VideoFadeContentEnd = exportData.Exporting ? exportData.ContentEndTime : selectedEnd;
		preview.VideoFadeInSeconds = exportData.LeadInSeconds;
		preview.VideoFadeOutSeconds = exportData.TailSeconds;
		preview.VideoFadeFrameSeconds = exportData.Exporting ? 1.0f / exportData.FramesPerSecond : 0.0f;
		preview.VideoExportTime = exportData.Exporting
			? exportData.StartTime + Time::FromFrames(static_cast<f64>(std::min(exportData.FrameIndex, exportData.FrameCount - 1)), exportData.FramesPerSecond)
			: Clamp(context.GetCursorTime(), selectedStart, selectedEnd);
		const vec2 availablePreviewSize = Gui::GetContentRegionAvail();
		const f32 reservedRows = exportData.Exporting ? 8.0f : 5.0f;
		const f32 previewWidth = std::min(availablePreviewSize.x,
			std::max(160.0f, availablePreviewSize.y - Gui::GetFrameHeightWithSpacing() * reservedRows) * 16.0f / 9.0f);
		const f32 previewHeight = previewWidth * 9.0f / 16.0f;
		if (Gui::BeginChild("##VideoExportPreview", vec2(previewWidth, previewHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			preview.DrawGui(context, *preview.VideoExportTime);
		Gui::EndChild();
		const b8 movieFrameReady = !preview.VideoUseBackgroundMovie || preview.MovieStatus == BackgroundMovieStatus::Ready || preview.MovieStatus == BackgroundMovieStatus::Inactive;
		exportData.FramePrepared = exportData.Exporting && !exportData.Finalizing && preview.VideoExportDrawList != nullptr && movieFrameReady;
		if (preview.VideoUseBackgroundMovie && !preview.MovieError.empty() && !exportData.Exporting)
		{
			exportData.Status = UI_Str("BACKGROUND_MOVIE_FAILED");
			exportData.ErrorDetails = preview.MovieError;
		}
		else if (!exportData.Exporting && movieFrameReady && exportData.Status == UI_Str("BACKGROUND_MOVIE_FAILED"))
		{
			exportData.Status.clear();
			exportData.ErrorDetails.clear();
		}

		if (exportData.Preparing)
		{
			Gui::TextUnformatted(UI_Str("VIDEO_EXPORT_PREPARING"));
		}
		else if (exportData.Exporting)
		{
			Gui::TextUnformatted(exportData.Finalizing ? UI_Str("VIDEO_EXPORT_FINALIZING") : UI_Str("VIDEO_EXPORT_WRITING"));
			Gui::ProgressBar(exportData.FrameCount > 0 ? static_cast<f32>(exportData.FrameIndex) / exportData.FrameCount : 0.0f);
			Gui::Text("%s: %lld / %lld", UI_Str("VIDEO_EXPORT_FRAMES"), exportData.FrameIndex, exportData.FrameCount);
			char elapsedText[32];
			FormatVideoExportTime(elapsedText, exportData.ExportStopwatch.GetElapsed().ToSec());
			Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_ELAPSED"), elapsedText);
			if (!exportData.Finalizing)
			{
				if (exportData.FrameIndex > 0 && exportData.ExportStopwatch.GetElapsed().ToSec() >= 3.0 && exportData.SecondsPerFrame > 0.0)
				{
					char remainingText[32];
					FormatVideoExportTime(remainingText, exportData.SecondsPerFrame * (exportData.FrameCount - exportData.FrameIndex));
					Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_REMAINING"), remainingText);
				}
				else Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_REMAINING"), UI_Str("VIDEO_EXPORT_CALCULATING"));
				if (Gui::Button(UI_Str("VIDEO_EXPORT_CANCEL")))
				{
					exportData.Exporting = false;
					exportData.ExportStopwatch.Stop();
					if (!DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath))
						exportData.ErrorDetails = std::string(UI_Str("VIDEO_EXPORT_TEMP_LEFT")) + " " + exportData.TemporaryPath;
					exportData.Status = UI_Str("VIDEO_EXPORT_STOPPED");
				}
			}
		}
		else
		{
			Gui::BeginDisabled(!song || selectedEnd <= selectedStart || !exportData.RouteReady || !movieFrameReady);
			if (Gui::Button(UI_Str("VIDEO_EXPORT_START")))
			{
				exportData.OutputBasePath.clear();
				exportData.OutputPath.clear();
				exportData.TemporaryPath.clear();
				exportData.Status = UI_Str("VIDEO_EXPORT_PREPARING");
				exportData.ErrorDetails.clear();
				exportData.Finished = false;
				const std::string executableDirectory = Directory::GetExecutableDirectory();
				const std::string outputDirectory = executableDirectory + "/Video";
				if (executableDirectory.empty() ||
					(!Directory::Exists(outputDirectory) && !Directory::Create(outputDirectory) && !Directory::Exists(outputDirectory)))
				{
					exportData.Status = UI_Str("VIDEO_EXPORT_START_FAILED");
					exportData.ErrorDetails = std::string(UI_Str("VIDEO_EXPORT_DIRECTORY_FAILED")) + " " + outputDirectory;
				}
				else
				{
					const std::string_view branchName = course.Branches.empty() ? std::string_view{}
						: std::string_view(branchNames[EnumToIndex(exportData.Branch)]);
					exportData.OutputBasePath = MakeVideoOutputBasePath(outputDirectory, context.Chart.ChartTitle, branchName,
						VideoResolutionPresets[exportData.Resolution].Height, exportData.FramesPerSecond);
					exportData.OutputPath = MakeAvailableVideoOutputPath(exportData.OutputBasePath);
					exportData.TemporaryPath = exportData.OutputPath.empty() ? std::string{} : MakeTemporaryVideoPath(exportData.OutputPath);
					if (exportData.OutputPath.empty() || exportData.TemporaryPath.empty())
					{
						exportData.Status = UI_Str("VIDEO_EXPORT_START_FAILED");
						exportData.ErrorDetails = UI_Str("VIDEO_EXPORT_NAME_FAILED");
					}
					else
					{
						exportData.Course = context.ChartSelectedCourse;
						exportData.SongSource = context.SongSource;
						exportData.ChartChanges = context.Undo.NumberOfChangesMade;
						exportData.ContentStartTime = selectedStart;
						exportData.ContentEndTime = selectedEnd;
						exportData.StartTime = selectedStart - Time::FromSec(exportData.LeadInSeconds);
						exportData.EndTime = selectedEnd + Time::FromSec(exportData.TailSeconds);
						exportData.FrameIndex = 0;
						exportData.FrameCount = static_cast<i64>(std::ceil((exportData.EndTime - exportData.StartTime).ToSec() * exportData.FramesPerSecond));
						exportData.Preparing = true;
						exportData.Finalizing = false;
						exportData.FramePrepared = false;
					}
				}
			}
			Gui::EndDisabled();
			if (!song) Gui::TextUnformatted(UI_Str("VIDEO_EXPORT_NO_SONG"));
		}
		if (!exportData.OutputPath.empty()) Gui::TextWrapped("%s: %s", UI_Str("VIDEO_EXPORT_OUTPUT"), exportData.OutputPath.c_str());
		if (exportData.Finished)
		{
			if (Gui::Button(UI_Str("VIDEO_EXPORT_OPEN_VIDEO"))) Shell::OpenInExplorer(exportData.OutputPath);
			Gui::SameLine();
			if (Gui::Button(UI_Str("VIDEO_EXPORT_OPEN_FOLDER"))) Shell::OpenInExplorer(Path::GetDirectoryName(exportData.OutputPath));
		}
		if (!exportData.Status.empty()) Gui::TextWrapped("%s", exportData.Status.c_str());
		if (!exportData.ErrorDetails.empty() && Gui::TreeNode(UI_Str("VIDEO_EXPORT_ERROR_DETAILS")))
		{
			Gui::TextWrapped("%s", exportData.ErrorDetails.c_str());
			Gui::TreePop();
		}
	}

	void ChartEditor::OnAfterRender()
	{
		SavePendingScreenshots();
		auto& exportData = videoExport;
		if (exportData.Preparing)
		{
			exportData.Preparing = false;
			if (context.ChartSelectedCourse != exportData.Course || context.SongSource != exportData.SongSource ||
				context.Undo.NumberOfChangesMade != exportData.ChartChanges)
			{
				exportData.Status = UI_Str("VIDEO_EXPORT_SOURCE_CHANGED");
				return;
			}
			const auto& resolution = VideoResolutionPresets[exportData.Resolution];
			const u32 bitrate = resolution.Bitrate * std::max(exportData.FramesPerSecond, 30) / 60;
			if (!exportData.Writer.Start(exportData.TemporaryPath, resolution.Width, resolution.Height, exportData.FramesPerSecond, bitrate, exportData.AudioBitRate))
			{
				exportData.ErrorDetails = std::string(exportData.Writer.GetError());
				if (!DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath))
					exportData.ErrorDetails += "\n" + std::string(UI_Str("VIDEO_EXPORT_TEMP_LEFT")) + " " + exportData.TemporaryPath;
				exportData.Status = UI_Str("VIDEO_EXPORT_START_FAILED");
				return;
			}
			context.SetIsPlayback(false);
			exportData.LastFrameElapsedSeconds = exportData.SecondsPerFrame = 0.0;
			exportData.ExportStopwatch.Restart();
			exportData.Preview.VideoExportCombos.Rebuild(*exportData.Course, exportData.Route);
			exportData.Preview.VideoScrollDecisionCount = SIZE_MAX;
			exportData.Sounds.Rebuild(*exportData.Course, exportData.Route, exportData.RouteRollSpeed);
			exportData.Exporting = true;
			exportData.Status.clear();
			return;
		}
		if (!exportData.Exporting) return;
		const auto failExport = [&](cstr message, std::string details)
		{
			exportData.Exporting = false;
			exportData.Finalizing = false;
			exportData.ExportStopwatch.Stop();
			exportData.Status = message;
			exportData.ErrorDetails = std::move(details);
			if (!DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath))
				exportData.ErrorDetails += "\n" + std::string(UI_Str("VIDEO_EXPORT_TEMP_LEFT")) + " " + exportData.TemporaryPath;
		};
		if (exportData.Finalizing)
		{
			if (!exportData.Writer.Finish())
			{
				failExport(UI_Str("VIDEO_EXPORT_FINALIZE_FAILED"), std::string(exportData.Writer.GetError()));
				return;
			}
			if (!CommitTemporaryVideo(exportData.TemporaryPath, exportData.OutputBasePath, exportData.OutputPath))
			{
				const DWORD error = ::GetLastError();
				exportData.Exporting = exportData.Finalizing = false;
				exportData.ExportStopwatch.Stop();
				exportData.Status = UI_Str("VIDEO_EXPORT_MOVE_FAILED");
				exportData.ErrorDetails = "MoveFileExW failed (Win32 error " + std::to_string(error) + ")\n" +
					std::string(UI_Str("VIDEO_EXPORT_TEMP_LEFT")) + " " + exportData.TemporaryPath;
				return;
			}
			exportData.Exporting = exportData.Finalizing = false;
			exportData.ExportStopwatch.Stop();
			exportData.TemporaryPath.clear();
			exportData.Finished = true;
			exportData.Status = UI_Str("VIDEO_EXPORT_FINISHED");
			return;
		}
		if (context.ChartSelectedCourse != exportData.Course || context.SongSource != exportData.SongSource ||
			context.Undo.NumberOfChangesMade != exportData.ChartChanges)
		{
			failExport(UI_Str("VIDEO_EXPORT_SOURCE_CHANGED"), {});
			return;
		}
		if (exportData.Preview.VideoUseBackgroundMovie && !exportData.Preview.MovieError.empty())
		{
			failExport(UI_Str("BACKGROUND_MOVIE_FAILED"), exportData.Preview.MovieError);
			return;
		}
		if (!exportData.FramePrepared) return;
		const auto& resolution = VideoResolutionPresets[exportData.Resolution];
		const u32 width = resolution.Width;
		const u32 height = resolution.Height;
		if (!ApplicationHost::CaptureDrawListToBGRA(exportData.Preview.VideoExportDrawList,
			exportData.Preview.VideoExportViewport, width, height, exportData.Pixels))
		{
			failExport(UI_Str("VIDEO_EXPORT_CAPTURE_FAILED"), {});
			return;
		}
		const i64 firstAudioFrame = exportData.FrameIndex * Audio::Engine.OutputSampleRate / exportData.FramesPerSecond;
		const i64 lastAudioFrame = (exportData.FrameIndex + 1) * Audio::Engine.OutputSampleRate / exportData.FramesPerSecond;
		VideoExportAudioSources sources;
		sources.Song = Audio::Engine.GetSourceSampleBufferView(context.SongSource);
		for (SoundEffectType type : { SoundEffectType::TaikoDon, SoundEffectType::TaikoKa, SoundEffectType::Balloon })
		{
			const Audio::SourceHandle source = context.SfxVoicePool.TryGetSourceForType(type);
			if (source != Audio::SourceHandle::Invalid)
				sources.SoundEffects[EnumToIndex(type)] = Audio::Engine.GetSourceSampleBufferView(source);
		}
		sources.SongOffset = context.Chart.SongOffset;
		sources.SongVolume = exportData.SongVolume * context.Chart.SongVolume;
		sources.DrumVolume = exportData.DrumVolume * context.Chart.SoundEffectVolume;
		sources.BalloonVolume = sources.DrumVolume * *Settings.Audio.BalloonVolume;
		if (exportData.AudioFade)
		{
			sources.ContentStart = exportData.ContentStartTime;
			sources.ContentEnd = exportData.ContentEndTime;
			sources.FadeInSeconds = exportData.LeadInSeconds;
			sources.FadeOutSeconds = exportData.TailSeconds;
		}
		RenderVideoExportAudio(sources, exportData.Sounds, exportData.StartTime, firstAudioFrame,
			static_cast<u32>(lastAudioFrame - firstAudioFrame), exportData.AudioSamples);
		if (!exportData.Writer.WriteVideoFrame(exportData.Pixels.data(), width * 4, exportData.FrameIndex) ||
			!exportData.Writer.WriteAudioSamples(exportData.AudioSamples.data(), static_cast<u32>(lastAudioFrame - firstAudioFrame), firstAudioFrame))
		{
			failExport(UI_Str("VIDEO_EXPORT_WRITE_FAILED"), std::string(exportData.Writer.GetError()));
			return;
		}
		const f64 elapsedSeconds = exportData.ExportStopwatch.GetElapsed().ToSec();
		const f64 frameSeconds = elapsedSeconds - exportData.LastFrameElapsedSeconds;
		if (frameSeconds > 0.0)
			exportData.SecondsPerFrame = exportData.SecondsPerFrame > 0.0
				? exportData.SecondsPerFrame * 0.85 + frameSeconds * 0.15 : frameSeconds;
		exportData.LastFrameElapsedSeconds = elapsedSeconds;
		if (++exportData.FrameIndex >= exportData.FrameCount)
			exportData.Finalizing = true;
	}
}
