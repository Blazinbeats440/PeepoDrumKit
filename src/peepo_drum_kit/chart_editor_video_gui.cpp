#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chart_editor.h"
#include "core_io.h"
#include "imgui/backend/imgui_impl_d3d11.h"
#include "imgui/backend/imgui_impl_win32.h"
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
					context.ShowStatusMessage(std::string(UI_Str("BACKGROUND_MOVIE_FAILED")) + "\n" + screenshotPreview.MovieError, 6.0);
				}
			}
		}
		Gui::End();
	}

	void ChartEditor::SavePendingScreenshots()
	{
		const auto report = [&](b8 success, const std::string& path)
		{
			context.ShowStatusMessage(std::string(success ? UI_Str("SCREENSHOT_SAVED") : UI_Str("SCREENSHOT_FAILED")) + (path.empty() ? "" : "\n" + path), 6.0);
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

	static b8 DiscardTemporaryVideo(VideoExportAsyncWriter& writer, std::string_view path)
	{
		writer.Cancel(); writer.Wait();
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
		preview.VideoExportDrawListTarget = nullptr;
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
		if (GetIsVideoExportActive()) { DrawVideoExportProgress(); return; }
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
		if ((exportData.Exporting || exportData.Preparing) && !exportData.Finalizing &&
			(context.ChartSelectedCourse != exportData.Course || context.SongSource != exportData.SongSource ||
				context.Undo.NumberOfChangesMade != exportData.ChartChanges))
		{
			if (exportData.Exporting) DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath);
			exportData.Exporting = exportData.Preparing = false;
			exportData.ExportStopwatch.Stop();
			exportData.Status = UI_Str("VIDEO_EXPORT_SOURCE_CHANGED");
		}
		Gui::SetNextWindowSize(GuiScale(vec2(1000.0f, 600.0f)), ImGuiCond_FirstUseEver);
		Gui::SetNextWindowSizeConstraints(GuiScale(vec2(800.0f, 460.0f)), vec2(F32Max));
		Gui::SetNextWindowViewport(Gui::GetMainViewport()->ID);
		const std::string windowTitle = std::string(UI_Str("VIDEO_EXPORT_WINDOW")) + "###VideoExportWindowHorizontal";
		if (!Gui::Begin(windowTitle.c_str(), (exportData.Exporting || exportData.Preparing) ? nullptr : &exportData.ShowWindow,
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
		{
			Gui::End();
			return;
		}
		defer { Gui::End(); };
		ConfigureVideoPreview(exportData.Preview);

		const auto* song = context.SongSource == Audio::SourceHandle::Invalid ? nullptr : Audio::Engine.GetSourceSampleBufferView(context.SongSource);
		const Time songEnd = song && song->SampleRate > 0 ? context.Chart.SongOffset + Audio::FramesToTime(song->FrameCount, song->SampleRate) : Time::Zero();
		const b8 hasRange = context.RangeSelection.IsActiveAndHasEnd() && context.RangeSelection.GetDuration() > Beat::Zero();
		const b8 hasPreview = context.Chart.SongDemoStartTime > Time::Zero();
		const b8 hasMarker = context.Marker.IsActive;
		using RangeMode = VideoExportData::RangeMode;
		if ((exportData.Range == RangeMode::SelectedRange && !hasRange) ||
			(exportData.Range == RangeMode::Preview && !hasPreview) ||
			(exportData.Range == RangeMode::Marker && !hasMarker))
			exportData.Range = RangeMode::Full;

		for (auto& recording : gamePreview.RecordedVideoRoutes)
			if (recording.Course && recording.Changes != context.Undo.NumberOfChangesMade) recording = {};
		for (size_t index = 0; index < exportData.Selections.size(); ++index)
		{
			auto& selection = exportData.Selections[index];
			if (std::none_of(context.Chart.Courses.begin(), context.Chart.Courses.end(), [&](const auto& candidate) { return candidate.get() == selection.Lane.Course; }))
			{
				const b8 initial = selection.Lane.Course == nullptr;
				selection = {};
				selection.Lane.Course = context.ChartSelectedCourse;
				if (index == 0 && initial) selection.Branch = exportData.Branch;
				if (index == 2)
					for (const auto& candidate : context.Chart.Courses)
						if (candidate.get() != exportData.Selections[1].Lane.Course) { selection.Lane.Course = candidate.get(); break; }
			}
			if (!CanUseVideoBranchMode(*selection.Lane.Course, selection.Branch, gamePreview.RecordedVideoRoutes, context.Undo.NumberOfChangesMade))
				selection.Branch = VideoBranchMode::Normal;
		}
		const char* branchNames[] = { UI_Str("BRANCH_FORCED_NORMAL"), UI_Str("BRANCH_FORCED_EXPERT"), UI_Str("BRANCH_FORCED_MASTER"),
			UI_Str("VIDEO_EXPORT_BRANCH_AUTO"), UI_Str("VIDEO_EXPORT_BRANCH_TEST_PLAY_1P"), UI_Str("VIDEO_EXPORT_BRANCH_TEST_PLAY_2P") };
		const auto settingLabel = [](cstr label) { Gui::TextUnformatted(label); Gui::SetNextItemWidth(-1.0f); };
		const auto drawChartOptions = [&]()
		{
			const char* playbackNames[] = { UI_Str("VIDEO_EXPORT_PLAYBACK_SINGLE"), UI_Str("VIDEO_EXPORT_PLAYBACK_DUAL") };
			i32 playbackIndex = exportData.Dual ? 1 : 0;
			settingLabel(UI_Str("VIDEO_EXPORT_PLAYBACK_MODE"));
			if (Gui::Combo("##PlaybackMode", &playbackIndex, playbackNames, ArrayCountI32(playbackNames)))
				exportData.Dual = playbackIndex == 1;
			const i32 laneCount = exportData.Dual ? 2 : 1;
			const i32 firstSelection = exportData.Dual ? 1 : 0;
			if (Gui::BeginTable("##VideoExportCourses", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
			{
				Gui::TableSetupColumn(UI_Str("VIDEO_EXPORT_COURSE_SINGLE"));
				Gui::TableSetupColumn(UI_Str("VIDEO_EXPORT_BRANCH"));
				Gui::TableHeadersRow();
				for (i32 lane = 0; lane < laneCount; ++lane)
				{
					auto& selection = exportData.Selections[firstSelection + lane];
					Gui::PushID(firstSelection + lane);
					Gui::TableNextRow();
					Gui::TableSetColumnIndex(0);
					const char* courseLabel = exportData.Dual ? (lane == 0 ? UI_Str("VIDEO_EXPORT_COURSE_UPPER") : UI_Str("VIDEO_EXPORT_COURSE_LOWER")) : UI_Str("VIDEO_EXPORT_COURSE_SINGLE");
					const std::string selectedName = selection.Lane.Course->ToString();
					if (exportData.Dual) Gui::TextUnformatted(courseLabel);
					Gui::SetNextItemWidth(-1.0f);
					if (Gui::BeginCombo("##Course", selectedName.c_str()))
					{
						for (size_t index = 0; index < context.Chart.Courses.size(); ++index)
						{
							ChartCourse* candidate = context.Chart.Courses[index].get();
							Gui::PushID(static_cast<i32>(index));
							const std::string name = candidate->ToString();
							if (Gui::Selectable(name.c_str(), selection.Lane.Course == candidate)) selection.Lane.Course = candidate;
							Gui::PopID();
						}
						Gui::EndCombo();
					}
					if (Gui::IsItemHovered()) Gui::SetTooltip("%s", selectedName.c_str());
					const b8 hasScoreBranch = std::any_of(selection.Lane.Course->Branches.begin(), selection.Lane.Course->Branches.end(),
						[](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; });
					const auto available = [&](VideoBranchMode mode)
					{
						return CanUseVideoBranchMode(*selection.Lane.Course, mode, gamePreview.RecordedVideoRoutes, context.Undo.NumberOfChangesMade);
					};
					if (!available(selection.Branch)) selection.Branch = VideoBranchMode::Normal;
					Gui::TableSetColumnIndex(1);
					if (exportData.Dual) Gui::Dummy(vec2(0.0f, Gui::GetTextLineHeight()));
					Gui::SetNextItemWidth(-1.0f);
					if (Gui::BeginCombo("##Branch", branchNames[static_cast<i32>(selection.Branch)]))
					{
						for (i32 index = 0; index < ArrayCountI32(branchNames); ++index)
						{
							Gui::BeginDisabled(!available(static_cast<VideoBranchMode>(index)));
							if (Gui::Selectable(branchNames[index], static_cast<i32>(selection.Branch) == index)) selection.Branch = static_cast<VideoBranchMode>(index);
							Gui::EndDisabled();
						}
						Gui::EndCombo();
					}
					if (Gui::IsItemHovered()) Gui::SetTooltip("%s", branchNames[static_cast<i32>(selection.Branch)]);
					if (hasScoreBranch) Gui::TextWrapped("%s", UI_Str("VIDEO_EXPORT_BRANCH_SCORE_UNAVAILABLE"));
					Gui::PopID();
				}
				Gui::EndTable();
			}
			Gui::Spacing();
			const char* rangeNames[4] = { UI_Str("VIDEO_EXPORT_RANGE_FULL") };
			RangeMode rangeModes[4] = { RangeMode::Full };
			i32 rangeCount = 1;
			if (hasRange) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_SELECTED"); rangeModes[rangeCount++] = RangeMode::SelectedRange; }
			if (hasPreview) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_PREVIEW"); rangeModes[rangeCount++] = RangeMode::Preview; }
			if (hasMarker) { rangeNames[rangeCount] = UI_Str("VIDEO_EXPORT_RANGE_MARKER"); rangeModes[rangeCount++] = RangeMode::Marker; }
			i32 rangeIndex = 0;
			for (i32 index = 0; index < rangeCount; ++index)
				if (rangeModes[index] == exportData.Range) rangeIndex = index;
			settingLabel(UI_Str("VIDEO_EXPORT_RANGE_MODE"));
			if (Gui::Combo("##RangeMode", &rangeIndex, rangeNames, rangeCount))
				exportData.Range = rangeModes[rangeIndex];
			if (exportData.Range == RangeMode::Preview || exportData.Range == RangeMode::Marker)
			{
				settingLabel(UI_Str("VIDEO_EXPORT_EXCERPT_SECONDS"));
				if (Gui::InputFloat("##ExcerptSeconds", &exportData.ExcerptSeconds, 0.0f, 0.0f, "%.1f s"))
					exportData.ExcerptSeconds = std::isfinite(exportData.ExcerptSeconds) ? Clamp(exportData.ExcerptSeconds, 0.1f, 3600.0f) : 15.0f;
			}
			if (Gui::BeginTable("##VideoExportMargins", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
			{
				Gui::TableNextColumn();
				settingLabel(UI_Str("VIDEO_EXPORT_LEAD_IN"));
				Gui::SliderFloat("##LeadIn", &exportData.LeadInSeconds, 0.0f, 5.0f, "%.1f s");
				Gui::TableNextColumn();
				settingLabel(UI_Str("VIDEO_EXPORT_TAIL"));
				Gui::SliderFloat("##Tail", &exportData.TailSeconds, 0.0f, 5.0f, "%.1f s");
				Gui::EndTable();
			}
		};
		const auto drawLayoutOptions = [&]()
		{
			const char* layoutNames[] = { UI_Str("VIDEO_EXPORT_LAYOUT_ORIGINAL"), UI_Str("VIDEO_EXPORT_LAYOUT_TAIKO") };
			i32 layoutIndex = static_cast<i32>(exportData.Layout);
			settingLabel(UI_Str("VIDEO_EXPORT_LAYOUT"));
			if (Gui::Combo("##Layout", &layoutIndex, layoutNames, ArrayCountI32(layoutNames)))
				exportData.Layout = static_cast<ChartGamePreview::VideoLayout>(layoutIndex);
		};
		const auto drawOutputOptions = [&]()
		{
			if (Gui::BeginTable("##VideoExportFormat", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
			{
				Gui::TableNextColumn();
				const char* resolutionNames[] = { VideoResolutionPresets[0].Name, VideoResolutionPresets[1].Name,
					VideoResolutionPresets[2].Name, VideoResolutionPresets[3].Name };
				settingLabel(UI_Str("VIDEO_EXPORT_RESOLUTION"));
				Gui::Combo("##Resolution", &exportData.Resolution, resolutionNames, ArrayCountI32(resolutionNames));
				Gui::TableNextColumn();
				const char* frameRateNames[] = { "15 fps", "30 fps", "60 fps", "120 fps" };
				static constexpr i32 frameRates[] = { 15, 30, 60, 120 };
				i32 frameRateIndex = 0;
				for (i32 index = 0; index < ArrayCountI32(frameRates); ++index)
					if (exportData.FramesPerSecond == frameRates[index]) frameRateIndex = index;
				settingLabel(UI_Str("VIDEO_EXPORT_FPS"));
				if (Gui::Combo("##FrameRate", &frameRateIndex, frameRateNames, ArrayCountI32(frameRateNames)))
					exportData.FramesPerSecond = frameRates[frameRateIndex];
				Gui::EndTable();
			}
			const char* audioBitRateNames[ArrayCountI32(VideoAudioBitRatePresets)];
			i32 audioBitRateIndex = 0;
			for (i32 index = 0; index < ArrayCountI32(VideoAudioBitRatePresets); ++index)
			{
				audioBitRateNames[index] = VideoAudioBitRatePresets[index].Name;
				if (exportData.AudioBitRate == VideoAudioBitRatePresets[index].BitRate) audioBitRateIndex = index;
			}
			settingLabel(UI_Str("VIDEO_EXPORT_AUDIO_BIT_RATE"));
			if (Gui::Combo("##AudioBitRate", &audioBitRateIndex, audioBitRateNames, ArrayCountI32(audioBitRateNames)))
				exportData.AudioBitRate = VideoAudioBitRatePresets[audioBitRateIndex].BitRate;
		};
		const auto movieDefinition = ReadBackgroundMovieDefinition(context.Chart.OtherMetadata);
		const b8 hasBackgroundMovie = !movieDefinition.FileName.empty();
		if (!hasBackgroundMovie && exportData.BackgroundSource == 4) exportData.BackgroundSource = 0;
		const auto drawBackgroundOptions = [&]()
		{
			const char* backgroundNames[] = { UI_Str("VIDEO_EXPORT_BACKGROUND_COLOR"), UI_Str("VIDEO_EXPORT_BACKGROUND_ASSET"),
				UI_Str("VIDEO_EXPORT_BACKGROUND_CUSTOM"), UI_Str("VIDEO_EXPORT_BACKGROUND_JACKET"), UI_Str("VIDEO_EXPORT_BACKGROUND_MOVIE") };
			settingLabel(UI_Str("VIDEO_EXPORT_BACKGROUND"));
			if (Gui::Combo("##Background", &exportData.BackgroundSource, backgroundNames, hasBackgroundMovie ? 5 : 4) &&
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
			settingLabel(UI_Str("VIDEO_EXPORT_BACKGROUND_COLOR"));
			Gui::ColorEdit3_U32("##BackgroundColor", &exportData.BackgroundColor);
			settingLabel(UI_Str("VIDEO_EXPORT_LANE_BACKGROUND_TRANSPARENCY"));
			Gui::SliderFloat("##LaneBackgroundTransparency", &exportData.LaneBackgroundTransparency,
				0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
			if (exportData.BackgroundSource != 0)
			{
				const char* backgroundFitNames[] = { UI_Str("VIDEO_EXPORT_BACKGROUND_STRETCH"), UI_Str("VIDEO_EXPORT_BACKGROUND_CONTAIN"),
					UI_Str("VIDEO_EXPORT_BACKGROUND_CONTAIN_WIDTH"), UI_Str("VIDEO_EXPORT_BACKGROUND_WIDTH"), UI_Str("VIDEO_EXPORT_BACKGROUND_HEIGHT") };
				i32 backgroundFitIndex = static_cast<i32>(exportData.BackgroundImageFit);
				settingLabel(UI_Str("VIDEO_EXPORT_BACKGROUND_FIT"));
				if (Gui::Combo("##BackgroundFit", &backgroundFitIndex, backgroundFitNames, ArrayCountI32(backgroundFitNames)))
					exportData.BackgroundImageFit = static_cast<ChartGamePreview::VideoBackgroundFit>(backgroundFitIndex);
			}
		};
		const auto drawAudioOptions = [&]()
		{
			settingLabel(UI_Str("VIDEO_EXPORT_SONG_VOLUME"));
			Gui::SliderFloat("##SongVolume", &exportData.SongVolume, 0.0f, 2.0f, "%.2f");
			settingLabel(UI_Str("VIDEO_EXPORT_DRUM_VOLUME"));
			Gui::SliderFloat("##DrumVolume", &exportData.DrumVolume, 0.0f, 2.0f, "%.2f");
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_AUDIO_FADE"), &exportData.AudioFade);
		};
		const auto drawTitleOptions = [&]()
		{
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_TITLE"), &exportData.ShowTitle); Gui::SameLine();
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_DIFFICULTY"), &exportData.ShowDifficulty);
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_SUBTITLE"), &exportData.ShowSubtitle);
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_MAX_COMBO"), &exportData.ShowMaxCombo); Gui::SameLine();
			Gui::Checkbox(UI_Str("VIDEO_EXPORT_SHOW_CURRENT_COMBO"), &exportData.ShowCurrentCombo);
			f32 titleSizePercent = exportData.TitleScale * 100.0f;
			settingLabel(UI_Str("VIDEO_EXPORT_TITLE_SIZE"));
			if (Gui::SliderFloat("##TitleSize", &titleSizePercent, 70.0f, 150.0f, "%.0f%%"))
				exportData.TitleScale = titleSizePercent / 100.0f;
			settingLabel(UI_Str("VIDEO_EXPORT_TITLE_PADDING"));
			Gui::SliderFloat("##TitlePadding", &exportData.TitlePaddingScale, 0.5f, 2.0f, "%.2f");
			if (Gui::BeginTable("##VideoExportTitlePosition", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
			{
				Gui::TableNextColumn();
				const char* titleAlignmentNames[] = { UI_Str("VIDEO_EXPORT_TITLE_LEFT"), UI_Str("VIDEO_EXPORT_TITLE_CENTER") };
				settingLabel(UI_Str("VIDEO_EXPORT_TITLE_ALIGNMENT"));
				Gui::Combo("##TitleAlignment", &exportData.TitleAlignment, titleAlignmentNames, ArrayCountI32(titleAlignmentNames));
				Gui::TableNextColumn();
				const char* titlePositionNames[] = { UI_Str("VIDEO_EXPORT_TITLE_TOP"), UI_Str("VIDEO_EXPORT_TITLE_BOTTOM") };
				settingLabel(UI_Str("VIDEO_EXPORT_TITLE_VERTICAL_POSITION"));
				Gui::Combo("##TitlePosition", &exportData.TitleVerticalPosition, titlePositionNames, ArrayCountI32(titlePositionNames));
				Gui::EndTable();
			}
			settingLabel(UI_Str("VIDEO_EXPORT_TITLE_COLOR"));
			Gui::ColorEdit3_U32("##TitleColor", &exportData.TitleColor);
			settingLabel(UI_Str("VIDEO_EXPORT_TITLE_BAND_COLOR"));
			Gui::ColorEdit4_U32("##TitleBandColor", &exportData.TitleBandColor, ImGuiColorEditFlags_AlphaBar);
		};
		const vec2 contentSize = Gui::GetContentRegionAvail();
		const f32 footerHeight = Gui::GetFrameHeightWithSpacing() * 5.0f;
		const f32 bodyHeight = std::max(1.0f, contentSize.y - footerHeight - Gui::GetStyle().ItemSpacing.y * 2.0f);
		const f32 settingsWidth = Clamp(contentSize.x * 0.38f, GuiScale(320.0f), GuiScale(420.0f));
		Gui::BeginChild("##VideoExportOptions", vec2(settingsWidth, bodyHeight), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (Gui::BeginTabBar("##VideoExportTabs", ImGuiTabBarFlags_FittingPolicyScroll))
		{
			const auto drawTab = [&](cstr name, const auto& drawOptions)
			{
				if (Gui::BeginTabItem(name))
				{
					Gui::BeginChild("##TabOptions", vec2(0.0f), false);
					Gui::BeginDisabled(exportData.Exporting || exportData.Preparing);
					Gui::PushID(name);
					Gui::PushStyleVar(ImGuiStyleVar_ItemSpacing, vec2(Gui::GetStyle().ItemSpacing.x, GuiScale(6.0f)));
					Gui::PushItemWidth(-1.0f);
					drawOptions();
					Gui::PopItemWidth();
					Gui::PopStyleVar();
					Gui::PopID();
					Gui::EndDisabled();
					Gui::EndChild();
					Gui::EndTabItem();
				}
			};
			drawTab(UI_Str("VIDEO_EXPORT_TAB_CHART"), drawChartOptions);
			drawTab(UI_Str("VIDEO_EXPORT_TAB_DISPLAY"), [&]() { drawLayoutOptions(); Gui::Spacing(); drawTitleOptions(); });
			drawTab(UI_Str("VIDEO_EXPORT_TAB_BACKGROUND"), drawBackgroundOptions);
			drawTab(UI_Str("VIDEO_EXPORT_TAB_AUDIO"), drawAudioOptions);
			drawTab(UI_Str("VIDEO_EXPORT_TAB_OUTPUT"), drawOutputOptions);
			Gui::EndTabBar();
		}
		Gui::EndChild();
		exportData.Branch = exportData.Selections[0].Branch;
		const i32 laneCount = exportData.Dual ? 2 : 1;
		const i32 firstSelection = exportData.Dual ? 1 : 0;
		Time selectedStart = exportData.Selections[firstSelection].Lane.Course->GetPlaybackTimeBounds().first;
		Time selectedEnd = songEnd;
		for (i32 lane = 0; lane < laneCount; ++lane)
		{
			const ChartCourse& course = *exportData.Selections[firstSelection + lane].Lane.Course;
			const auto bounds = course.GetPlaybackTimeBounds(std::max(context.Chart.GetUsedDuration(course), songEnd));
			selectedStart = Min(selectedStart, bounds.first);
			selectedEnd = Max(selectedEnd, bounds.second);
		}
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
		exportData.RouteReady = true;
		exportData.RouteRollSpeed = rollsPerSecond;
		preview.VideoExportLaneCount = laneCount;
		for (i32 lane = 0; lane < laneCount; ++lane)
		{
			auto& selection = exportData.Selections[firstSelection + lane];
			const ChartCourse& course = *selection.Lane.Course;
			if (!exportData.Exporting && (selection.RouteCourse != &course || selection.RouteChanges != context.Undo.NumberOfChangesMade
				|| selection.RouteMode != selection.Branch || selection.RouteRollSpeed != rollsPerSecond
				|| selection.RouteRecordingVersion != gamePreview.VideoRecordingVersion))
			{
				selection.RouteCourse = &course;
				selection.RouteChanges = context.Undo.NumberOfChangesMade;
				selection.RouteMode = selection.Branch;
				selection.RouteRollSpeed = rollsPerSecond;
				selection.RouteRecordingVersion = gamePreview.VideoRecordingVersion;
				selection.RouteReady = true;
				if (selection.Branch == VideoBranchMode::Auto)
					selection.RouteReady = BuildAutoVideoBranchRoute(course, rollsPerSecond, selection.Route);
				else if (selection.Branch >= VideoBranchMode::TestPlay)
				{
					const auto& recording = gamePreview.RecordedVideoRoutes[static_cast<size_t>(selection.Branch) - static_cast<size_t>(VideoBranchMode::TestPlay)];
					selection.RouteReady = recording.IsValid(&course, context.Undo.NumberOfChangesMade);
					selection.Route = recording.Route;
				}
				else selection.Route = BuildFixedVideoBranchRoute(course, VideoBranchForCourse(course, static_cast<BranchType>(selection.Branch)));
				selection.Lane.Combos.Rebuild(course, selection.Route);
				selection.Lane.ScrollDecisionCount = SIZE_MAX;
			}
			selection.Lane.Route = &selection.Route;
			selection.Lane.Branch = selection.Branch <= VideoBranchMode::Master ? VideoBranchForCourse(course, static_cast<BranchType>(selection.Branch)) : BranchType::Normal;
			preview.VideoExportLanes[lane] = &selection.Lane;
			exportData.RouteReady &= selection.RouteReady;
		}
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
		Gui::SameLine();
		Gui::BeginChild("##VideoExportRightPane", vec2(0.0f, bodyHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const vec2 availablePreviewSize = Gui::GetContentRegionAvail();
		const f32 summaryHeight = Gui::GetTextLineHeightWithSpacing() * 3.0f + Gui::GetStyle().ItemSpacing.y;
		const f32 previewWidth = std::min(availablePreviewSize.x,
			std::max(1.0f, availablePreviewSize.y - summaryHeight) * 16.0f / 9.0f);
		const f32 previewHeight = previewWidth * 9.0f / 16.0f;
		Gui::SetCursorPosX(Gui::GetCursorPosX() + (availablePreviewSize.x - previewWidth) * 0.5f);
		if (Gui::BeginChild("##VideoExportPreview", vec2(previewWidth, previewHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			preview.DrawGui(context, *preview.VideoExportTime);
		Gui::EndChild();
		Gui::Text("%s / %d fps / %u kbps", VideoResolutionPresets[exportData.Resolution].Name,
			exportData.FramesPerSecond, exportData.AudioBitRate / 1000);
		const cstr rangeLabel = exportData.Range == RangeMode::SelectedRange ? UI_Str("VIDEO_EXPORT_RANGE_SELECTED")
			: exportData.Range == RangeMode::Preview ? UI_Str("VIDEO_EXPORT_RANGE_PREVIEW")
			: exportData.Range == RangeMode::Marker ? UI_Str("VIDEO_EXPORT_RANGE_MARKER") : UI_Str("VIDEO_EXPORT_RANGE_FULL");
		Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_RANGE_MODE"), rangeLabel);
		Gui::Text("%.2f s - %.2f s", selectedStart.ToSec(), selectedEnd.ToSec());
		Gui::EndChild();
		Gui::Separator();
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
			Gui::SameLine();
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
				Gui::SameLine();
				const f32 cancelWidth = Gui::CalcTextSize(UI_Str("VIDEO_EXPORT_CANCEL")).x + Gui::GetStyle().FramePadding.x * 2.0f;
				Gui::SetCursorPosX(Gui::GetCursorPosX() + std::max(0.0f, Gui::GetContentRegionAvail().x - cancelWidth));
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
			if (exportData.Finished)
			{
				if (Gui::Button(UI_Str("VIDEO_EXPORT_OPEN_VIDEO"))) Shell::OpenInExplorer(exportData.OutputPath);
				Gui::SameLine();
				if (Gui::Button(UI_Str("VIDEO_EXPORT_OPEN_FOLDER"))) Shell::OpenInExplorer(Path::GetDirectoryName(exportData.OutputPath));
				Gui::SameLine();
			}
			if (!song) { Gui::TextUnformatted(UI_Str("VIDEO_EXPORT_NO_SONG")); Gui::SameLine(); }
			const f32 startWidth = std::max(GuiScale(120.0f), Gui::CalcTextSize(UI_Str("VIDEO_EXPORT_START")).x + Gui::GetStyle().FramePadding.x * 2.0f);
			Gui::SetCursorPosX(Gui::GetCursorPosX() + std::max(0.0f, Gui::GetContentRegionAvail().x - startWidth));
			Gui::BeginDisabled(!song || selectedEnd <= selectedStart || !exportData.RouteReady || !movieFrameReady || context.TestPlayActive ||
				loadSongFuture.valid() || loadJacketFuture.valid() || importChartFuture.valid() || context.Gfx.IsAsyncLoading() || context.SfxVoicePool.LoadSoundEffectFuture.valid() ||
				screenshotWindowRequested || screenshotPreviewRequested || screenshotMovieWaiting);
			if (Gui::Button(UI_Str("VIDEO_EXPORT_START"), vec2(startWidth, 0.0f)))
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
					const auto& selection = exportData.Selections[firstSelection];
					const std::string_view branchName = exportData.Dual ? std::string_view(UI_Str("VIDEO_EXPORT_PLAYBACK_DUAL"))
						: selection.Lane.Course->Branches.empty() ? std::string_view{} : std::string_view(branchNames[EnumToIndex(selection.Branch)]);
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
						context.Undo.FlushAndExecuteEndOfFrameCommands();
						Gui::ClearActiveID();
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
						exportData.CancelRequested = exportData.FinishRequested = exportData.FailureReported = exportData.PendingFrameReady = false;
					}
				}
			}
			Gui::EndDisabled();
		}
		Gui::BeginChild("##VideoExportDetails", vec2(0.0f), false);
		if (!exportData.Preparing && !exportData.Exporting && !exportData.Status.empty()) Gui::TextWrapped("%s", exportData.Status.c_str());
		if (!exportData.OutputPath.empty() && Gui::TreeNode(UI_Str("VIDEO_EXPORT_OUTPUT")))
		{
			Gui::TextWrapped("%s", exportData.OutputPath.c_str());
			Gui::TreePop();
		}
		if (!exportData.ErrorDetails.empty() && Gui::TreeNode(UI_Str("VIDEO_EXPORT_ERROR_DETAILS")))
		{
			Gui::TextWrapped("%s", exportData.ErrorDetails.c_str());
			Gui::TreePop();
		}
		Gui::EndChild();
	}

	void ChartEditor::DrawVideoExportProgress()
	{
		auto& data = videoExport;
		data.FramePrepared = false;
		data.WorkerProgress = data.Writer.GetProgress();
		Gui::SetNextWindowViewport(Gui::GetMainViewport()->ID);
		const std::string title = std::string(UI_Str("VIDEO_EXPORT_WINDOW")) + "###VideoExportWindowHorizontal";
		if (!Gui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
		{
			Gui::End();
			return;
		}
		Gui::TextUnformatted(UI_Str("VIDEO_EXPORT_EDITOR_LOCKED"));
		const vec2 available = Gui::GetContentRegionAvail();
		const f32 previewHeight = std::max(72.0f, std::min(available.y - Gui::GetTextLineHeightWithSpacing() * 6.0f, available.x * 9.0f / 16.0f));
		const vec2 previewSize(std::max(128.0f, previewHeight * 16.0f / 9.0f), previewHeight);
		if (Gui::BeginChild("##ExportProgressPreview", previewSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			const vec2 previewPosition = Gui::GetCursorScreenPos();
			if (!data.CancelRequested && !data.FinishRequested && data.FrameIndex < data.FrameCount &&
				data.WorkerProgress.Status == VideoExportWorkerStatus::Writing && ApplicationHost::GetAsyncDrawListCaptureStatus() == ApplicationHost::DrawListCaptureStatus::Ready)
			{
				auto& preview = data.Preview;
				preview.VideoExportTime = data.StartTime + Time::FromFrames(static_cast<f64>(data.FrameIndex), data.FramesPerSecond);
				if (preview.VideoUseBackgroundMovie) preview.UpdateBackgroundMovie(context, *preview.VideoExportTime, true);
				if (!preview.VideoUseBackgroundMovie || preview.MovieStatus != BackgroundMovieStatus::Pending)
				{
					if (!data.RenderDrawList) data.RenderDrawList = IM_NEW(ImDrawList)(Gui::GetDrawListSharedData());
					data.RenderDrawList->_ResetForNewFrame();
					data.RenderDrawList->PushTexture(Gui::GetWindowDrawList()->_CmdHeader.TexRef);
					data.RenderDrawList->PushClipRect(Gui::GetCurrentWindow()->ClipRect.Min, Gui::GetCurrentWindow()->ClipRect.Max);
					preview.VideoExportDrawListTarget = data.RenderDrawList;
					preview.DrawGui(context, *preview.VideoExportTime);
					preview.VideoExportDrawListTarget = nullptr;
					data.RenderDrawList->ChannelsMerge();
					data.RenderDrawList->PopClipRect(); data.RenderDrawList->PopTexture(); data.RenderDrawList->_PopUnusedDrawCmd();
					data.FramePrepared = !preview.VideoUseBackgroundMovie || preview.MovieStatus == BackgroundMovieStatus::Ready || preview.MovieStatus == BackgroundMovieStatus::Inactive;
				}
			}
			Gui::SetCursorScreenPos(previewPosition);
			const auto texture = ApplicationHost::GetAsyncDrawListCapturePreviewTexture();
			if (texture) Gui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), previewSize);
			else Gui::Dummy(previewSize);
		}
		Gui::EndChild();
		Gui::TextUnformatted(data.CancelRequested ? UI_Str("VIDEO_EXPORT_CANCELING") : data.Finalizing ? UI_Str("VIDEO_EXPORT_FINALIZING") :
			data.WorkerProgress.Status == VideoExportWorkerStatus::Starting || data.Preparing ? UI_Str("VIDEO_EXPORT_PREPARING") : UI_Str("VIDEO_EXPORT_WRITING"));
		Gui::ProgressBar(data.FrameCount > 0 ? static_cast<f32>(data.WorkerProgress.WrittenFrames) / data.FrameCount : 0.0f);
		Gui::Text("%s: %lld / %lld", UI_Str("VIDEO_EXPORT_FRAMES"), data.WorkerProgress.WrittenFrames, data.FrameCount);
		char elapsed[32]; FormatVideoExportTime(elapsed, data.ExportStopwatch.GetElapsed().ToSec());
		Gui::SameLine(); Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_ELAPSED"), elapsed);
		if (data.SecondsPerFrame > 0.0 && !data.Finalizing)
		{
			char remaining[32]; FormatVideoExportTime(remaining, data.SecondsPerFrame * (data.FrameCount - data.WorkerProgress.WrittenFrames));
			Gui::Text("%s: %s", UI_Str("VIDEO_EXPORT_REMAINING"), remaining);
		}
		Gui::BeginDisabled(data.CancelRequested);
		if (Gui::Button(UI_Str("VIDEO_EXPORT_CANCEL")) || Gui::IsAnyPressed(*Settings.Input.Dialog_Cancel, false))
		{
			data.CancelRequested = true;
			data.Writer.Cancel();
		}
		Gui::EndDisabled();
		Gui::End();
	}

	void ChartEditor::StopVideoExport()
	{
		const b8 wasActive = GetIsVideoExportActive();
		videoExport.Writer.Cancel(); videoExport.Writer.Wait();
		if (wasActive && !videoExport.TemporaryPath.empty()) DiscardTemporaryVideo(videoExport.Writer, videoExport.TemporaryPath);
		ApplicationHost::ReleaseDrawListCaptureResources();
	}

	void ChartEditor::OnAfterRender()
	{
		if (!GetIsVideoExportActive()) SavePendingScreenshots();
		auto& exportData = videoExport;
		defer {
			ApplicationHost::GlobalState.UnthrottledRendering = GetIsVideoExportActive();
			ApplicationHost::GlobalState.ThrottleWindowPresentation = GetIsVideoExportActive();
			if (!GetIsVideoExportActive()) ApplicationHost::ReleaseDrawListCaptureResources();
		};
		if (exportData.Preparing)
		{
			exportData.Preparing = false;
			if (exportData.CancelRequested)
			{
				DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath);
				exportData.Status = UI_Str("VIDEO_EXPORT_STOPPED");
				return;
			}
			if (context.ChartSelectedCourse != exportData.Course || context.SongSource != exportData.SongSource ||
				context.Undo.NumberOfChangesMade != exportData.ChartChanges)
			{
				exportData.Status = UI_Str("VIDEO_EXPORT_SOURCE_CHANGED");
				return;
			}
			const auto& resolution = VideoResolutionPresets[exportData.Resolution];
			const u32 bitrate = resolution.Bitrate * std::max(exportData.FramesPerSecond, 30) / 60;
			exportData.Writer.Start(exportData.TemporaryPath, resolution.Width, resolution.Height, exportData.FramesPerSecond, bitrate, exportData.AudioBitRate, *Settings.General.VideoExportHardwareEncoding);
			ApplicationHost::BeginAsyncDrawListCapture();
			context.SetIsPlayback(false);
			exportData.LastFrameElapsedSeconds = exportData.SecondsPerFrame = 0.0;
			exportData.ExportStopwatch.Restart();
			exportData.Sounds.Events.clear();
			for (i32 lane = 0; lane < exportData.Preview.VideoExportLaneCount; ++lane)
			{
				auto& state = *exportData.Preview.VideoExportLanes[lane];
				state.Combos.Rebuild(*state.Course, *state.Route);
				state.ScrollDecisionCount = SIZE_MAX;
				VideoExportSoundTimeline sounds;
				sounds.Rebuild(*state.Course, *state.Route, exportData.RouteRollSpeed,
					exportData.Preview.VideoExportLaneCount == 1 ? 0.0f : lane == 0 ? -1.0f : 1.0f);
				exportData.Sounds.Events.insert(exportData.Sounds.Events.end(), sounds.Events.begin(), sounds.Events.end());
			}
			std::sort(exportData.Sounds.Events.begin(), exportData.Sounds.Events.end(),
				[](const auto& left, const auto& right) { return left.HitTime < right.HitTime; });
			exportData.AudioRenderState.Reset();
			exportData.ReadbackFrames = exportData.LastWrittenFrames = 0;
			exportData.PendingFrame = {};
			auto& sources = exportData.AudioSources;
			sources = {};
			sources.Song = context.SongSource == Audio::SourceHandle::Invalid ? nullptr : Audio::Engine.GetSourceSampleBufferView(context.SongSource);
			for (SoundEffectType type : { SoundEffectType::TaikoDon, SoundEffectType::TaikoKa, SoundEffectType::Balloon })
			{
				const auto source = context.SfxVoicePool.TryGetSourceForType(type);
				if (source != Audio::SourceHandle::Invalid) sources.SoundEffects[EnumToIndex(type)] = Audio::Engine.GetSourceSampleBufferView(source);
			}
			sources.SongOffset = context.Chart.SongOffset;
			sources.SongVolume = exportData.SongVolume * context.Chart.SongVolume;
			sources.DrumVolume = exportData.DrumVolume * context.Chart.SoundEffectVolume;
			sources.BalloonVolume = sources.DrumVolume * *Settings.Audio.BalloonVolume;
			if (exportData.AudioFade)
			{
				sources.ContentStart = exportData.ContentStartTime; sources.ContentEnd = exportData.ContentEndTime;
				sources.FadeInSeconds = exportData.LeadInSeconds; sources.FadeOutSeconds = exportData.TailSeconds;
			}
			exportData.Preview.VideoFadeContentStart = exportData.ContentStartTime;
			exportData.Preview.VideoFadeContentEnd = exportData.ContentEndTime;
			exportData.Preview.VideoFadeFrameSeconds = 1.0f / exportData.FramesPerSecond;
			exportData.Exporting = true;
			exportData.Status.clear();
			return;
		}
		if (!exportData.Exporting) return;
		const auto failExport = [&](cstr message, std::string details)
		{
			exportData.FailureReported = exportData.CancelRequested = true;
			exportData.Status = message;
			exportData.ErrorDetails = std::move(details);
			exportData.Writer.Cancel();
		};
		exportData.WorkerProgress = exportData.Writer.GetProgress();
		const auto& progress = exportData.WorkerProgress;
		if (progress.Status == VideoExportWorkerStatus::Finished || progress.Status == VideoExportWorkerStatus::Canceled || progress.Status == VideoExportWorkerStatus::Failed)
		{
			exportData.Writer.Wait();
			exportData.Preparing = exportData.Exporting = exportData.Finalizing = false;
			exportData.ExportStopwatch.Stop();
			if (progress.Status != VideoExportWorkerStatus::Finished || exportData.CancelRequested)
			{
				if (!exportData.FailureReported)
				{
					exportData.Status = exportData.CancelRequested ? UI_Str("VIDEO_EXPORT_STOPPED") : UI_Str("VIDEO_EXPORT_WRITE_FAILED");
					exportData.ErrorDetails = progress.Error;
				}
				if (!DiscardTemporaryVideo(exportData.Writer, exportData.TemporaryPath))
					exportData.ErrorDetails += "\n" + std::string(UI_Str("VIDEO_EXPORT_TEMP_LEFT")) + " " + exportData.TemporaryPath;
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
		if (exportData.CancelRequested || progress.Status != VideoExportWorkerStatus::Writing || exportData.FinishRequested) return;
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
		if (ApplicationHost::GetAsyncDrawListCaptureStatus() == ApplicationHost::DrawListCaptureStatus::Failed)
		{
			failExport(UI_Str("VIDEO_EXPORT_CAPTURE_FAILED"), {});
			return;
		}
		if (exportData.PendingFrameReady && exportData.Writer.TrySubmit(exportData.PendingFrame)) exportData.PendingFrameReady = false;
		if (!exportData.PendingFrameReady && exportData.ReadbackFrames < exportData.FrameIndex && progress.PendingFrames < VideoExportAsyncWriter::MaxPendingFrames)
		{
			const auto status = ApplicationHost::PollAsyncDrawListCapture(exportData.PendingFrame.Pixels, exportData.PendingFrame.Index);
			if (status == ApplicationHost::DrawListCaptureStatus::Failed) { failExport(UI_Str("VIDEO_EXPORT_CAPTURE_FAILED"), {}); return; }
			if (status == ApplicationHost::DrawListCaptureStatus::Ready)
			{
				if (exportData.PendingFrame.Index != exportData.ReadbackFrames++) { failExport(UI_Str("VIDEO_EXPORT_CAPTURE_FAILED"), "Unexpected frame order"); return; }
				const i64 index = exportData.PendingFrame.Index;
				const i64 firstAudio = index * Audio::Engine.OutputSampleRate / exportData.FramesPerSecond;
				const i64 lastAudio = (index + 1) * Audio::Engine.OutputSampleRate / exportData.FramesPerSecond;
				exportData.PendingFrame.FirstAudioSample = firstAudio;
				RenderVideoExportAudio(exportData.AudioSources, exportData.Sounds, exportData.StartTime, firstAudio,
					static_cast<u32>(lastAudio - firstAudio), exportData.PendingFrame.AudioSamples, &exportData.AudioRenderState);
				exportData.PendingFrameReady = !exportData.Writer.TrySubmit(exportData.PendingFrame);
			}
		}
		const auto& resolution = VideoResolutionPresets[exportData.Resolution];
		if (exportData.FramePrepared && exportData.FrameIndex < exportData.FrameCount)
		{
			const auto status = ApplicationHost::QueueAsyncDrawListCapture(exportData.Preview.VideoExportDrawList, exportData.Preview.VideoExportViewport,
				resolution.Width, resolution.Height, exportData.FrameIndex);
			if (status == ApplicationHost::DrawListCaptureStatus::Failed) { failExport(UI_Str("VIDEO_EXPORT_CAPTURE_FAILED"), {}); return; }
			if (status == ApplicationHost::DrawListCaptureStatus::Ready) ++exportData.FrameIndex;
		}
		exportData.WorkerProgress = exportData.Writer.GetProgress();
		if (exportData.WorkerProgress.WrittenFrames > exportData.LastWrittenFrames)
		{
			const f64 elapsed = exportData.ExportStopwatch.GetElapsed().ToSec();
			const f64 seconds = (elapsed - exportData.LastFrameElapsedSeconds) / (exportData.WorkerProgress.WrittenFrames - exportData.LastWrittenFrames);
			exportData.SecondsPerFrame = exportData.SecondsPerFrame > 0.0 ? exportData.SecondsPerFrame * 0.85 + seconds * 0.15 : seconds;
			exportData.LastFrameElapsedSeconds = elapsed; exportData.LastWrittenFrames = exportData.WorkerProgress.WrittenFrames;
		}
		if (exportData.WorkerProgress.SubmittedFrames == exportData.FrameCount)
		{
			exportData.Writer.Finish(); exportData.FinishRequested = true;
			exportData.Finalizing = true;
		}
	}

	b8 ChartEditor::RunVideoExportSelfTest(std::string& error)
	{
		error.clear();
		const auto fail = [&](std::string message) { error = std::move(message); return false; };
		const auto savedSettings = Settings_Mutable;
		const auto savedPersistent = PersistentApp;
		ImFont* savedFont = FontMain;
		defer { Settings_Mutable = savedSettings; PersistentApp = savedPersistent; FontMain = savedFont; };
		Settings_Mutable.Audio.OpenDeviceOnStartup = false;
		Settings_Mutable.General.AutoPrefixChartSubtitle = true;
		FontMain = Gui::GetIO().Fonts->Fonts[0];
		Audio::Engine.ApplicationStartup();
		defer { Audio::Engine.ApplicationShutdown(); };
		ChartEditor editor;
		auto& data = editor.videoExport;
		auto& context = editor.context;
		const ULONGLONG loadingDeadline = GetTickCount64() + 15000;
		while (context.Gfx.IsAsyncLoading() || context.SfxVoicePool.LoadSoundEffectFuture.valid())
		{
			context.Gfx.UpdateAsyncLoading(); context.SfxVoicePool.UpdateAsyncLoading();
			if (GetTickCount64() > loadingDeadline) return fail("Loading export assets timed out");
			Sleep(1);
		}
		context.Chart.ChartTitle = "Export pipeline test";
		context.Chart.ChartSubtitle = "Must remain unchanged";
		Note don {}, ka {};
		don.BeatTime = Beat::FromBeatsFraction(0.5); don.Type = NoteType::Don;
		ka.BeatTime = Beat::FromBeats(1); ka.Type = NoteType::Ka;
		context.ChartSelectedCourse->Notes_Normal.Sorted = { don, ka };
		Audio::PCMSampleBuffer song;
		song.SampleRate = Audio::Engine.OutputSampleRate; song.ChannelCount = 2; song.FrameCount = song.SampleRate;
		song.InterleavedSamples = std::make_unique<i16[]>(song.FrameCount * 2);
		for (u32 sample = 0; sample < song.FrameCount; ++sample)
			song.InterleavedSamples[sample * 2] = song.InterleavedSamples[sample * 2 + 1] = static_cast<i16>(500 * std::sin(sample * 0.1));
		context.SongSource = Audio::Engine.LoadSourceFromBufferMove("Export test song", std::move(song));
		const auto* selectedCourse = context.ChartSelectedCourse;
		const i32 revision = context.Undo.NumberOfChangesMade;
		data.BackgroundInitialized = true; data.BackgroundSource = 0;
		data.BackgroundColor = IM_COL32_BLACK; data.LaneBackgroundTransparency = 0.0f;
		data.Resolution = 0; data.FramesPerSecond = 30;
		data.LeadInSeconds = data.TailSeconds = 0.0f;
		data.Course = context.ChartSelectedCourse; data.SongSource = context.SongSource; data.ChartChanges = revision;
		data.StartTime = data.ContentStartTime = Time::Zero();
		data.EndTime = data.ContentEndTime = Time::FromSec(1.0);
		for (i32 lane = 0; lane < 2; ++lane)
		{
			auto& selection = data.Selections[lane];
			selection.Route = BuildFixedVideoBranchRoute(*data.Course, BranchType::Normal);
			selection.Lane.Course = data.Course; selection.Lane.Route = &selection.Route;
			data.Preview.VideoExportLanes[lane] = &selection.Lane;
		}
		const std::string prefix = "build/video-pipeline-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
		const auto begin = [&](i32 test)
		{
			data.FrameCount = 30; data.FrameIndex = 0;
			data.OutputBasePath = prefix + "-" + std::to_string(test);
			data.OutputPath.clear(); data.TemporaryPath = MakeTemporaryVideoPath(data.OutputBasePath);
			data.Preparing = true; data.Exporting = data.Finalizing = data.Finished = false;
			data.CancelRequested = data.FinishRequested = data.FailureReported = data.PendingFrameReady = false;
			data.ErrorDetails.clear(); data.Status.clear();
			data.Layout = test % 2 == 0 ? ChartGamePreview::VideoLayout::Original : ChartGamePreview::VideoLayout::Taiko;
			data.Preview.VideoExportLaneCount = test == 2 || test == 3 ? 2 : 1;
			editor.ConfigureVideoPreview(data.Preview);
			data.Preview.VideoExportPixelAligned = true;
			data.Preview.VideoFadeInSeconds = data.Preview.VideoFadeOutSeconds = 0.0f;
		};
		const auto step = [&](b8 injectInput)
		{
			ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
			auto& io = Gui::GetIO(); io.DisplaySize = vec2(1280, 720);
			io.AddKeyEvent(ImGuiMod_Ctrl, injectInput); io.AddKeyEvent(ImGuiKey_N, injectInput);
			io.AddKeyEvent(ImGuiKey_Delete, injectInput); io.AddKeyEvent(ImGuiKey_Space, injectInput);
			if (injectInput) ApplicationHost::GlobalState.FilePathsDroppedThisFrame = { "build/nonexistent-export-drop.tja" };
			Gui::NewFrame(); Gui::PushFont(FontMain, 16.0f);
			Gui::SetNextWindowPos(vec2(0, 0)); Gui::SetNextWindowSize(vec2(1000, 650));
			editor.DrawGui();
			Gui::PopFont(); Gui::Render(); editor.OnAfterRender();
			ApplicationHost::GlobalState.FilePathsDroppedThisFrame.clear();
		};
		for (i32 test = 0; test < 8; ++test)
		{
			Settings_Mutable.General.VideoExportHardwareEncoding = test % 2 != 0;
			Settings_Mutable.General.GamePreviewBarLineMode = test >= 2 ? 1 : 0;
			begin(test);
			if (test == 4) data.CancelRequested = true;
			if (test == 7) data.TemporaryPath = prefix + "-missing-directory/output.mp4";
			const ULONGLONG deadline = GetTickCount64() + 30000;
			while (editor.GetIsVideoExportActive())
			{
				step((Gui::GetFrameCount() % 4) == 0);
				if (data.FramePrepared && data.RenderDrawList->_Splitter._Count != 1) return fail("Export draw layers were not merged");
				if (context.ChartSelectedCourse != selectedCourse || context.Undo.NumberOfChangesMade != revision ||
					context.Chart.ChartSubtitle != "Must remain unchanged" || editor.importChartFuture.valid()) return fail("Export allowed chart editing");
				if (editor.GetIsVideoExportActive() && !data.CancelRequested && ((test == 5 && data.FrameIndex >= 3) || (test == 6 && data.FinishRequested)))
				{
					if (editor.OnWindowCloseRequest() != ApplicationHost::CloseResponse::SupressExit) return fail("Closing did not wait for export cancellation");
					editor.tryToCloseApplicationOnNextFrame = false;
				}
				if (GetTickCount64() > deadline) return fail("Export pipeline timed out: " + data.ErrorDetails);
				Sleep(1);
			}
			if (test < 4)
			{
				if (!data.Finished || !File::Exists(data.OutputPath) || data.FrameIndex != 30 || data.ReadbackFrames != 30 || data.WorkerProgress.WrittenFrames != 30 ||
					!data.AudioSources.Song || data.Sounds.Events.empty())
					return fail("Export pipeline did not complete: " + data.ErrorDetails);
				BackgroundMovieReader reader;
				BackgroundMovieFrame frame;
				reader.Request(data.OutputPath, Time::Zero());
				BackgroundMovieStatus status;
				const ULONGLONG decodeDeadline = GetTickCount64() + 10000;
				while ((status = reader.Poll(frame)) == BackgroundMovieStatus::Pending && GetTickCount64() < decodeDeadline) Sleep(1);
				if (status != BackgroundMovieStatus::Ready || frame.Size != ivec2(640, 360) || frame.Pixels.size() != 640 * 360 * 4)
					return fail("Could not decode exported chart: " + reader.GetError());
				size_t redPixels = 0, bluePixels = 0, footerPixels = 0;
				for (size_t pixel = 0; pixel < frame.Pixels.size(); pixel += 4)
				{
					const i32 blue = frame.Pixels[pixel], green = frame.Pixels[pixel + 1], red = frame.Pixels[pixel + 2];
					redPixels += red > 180 && green < 110 && blue < 110;
					bluePixels += blue > 140 && green > 140 && red < 130;
					footerPixels += std::abs(red - 132) < 8 && std::abs(green - 132) < 8 && std::abs(blue - 132) < 8;
				}
				std::string screenshotPath;
				if (!SaveScreenshotPNG(frame.Size.x, frame.Size.y, frame.Pixels, screenshotPath)) return fail("Could not save the exported chart verification image");
				std::printf("Exported chart verification case %d: %s\n", test, screenshotPath.c_str());
				if (redPixels < 50 || bluePixels < 50 || footerPixels < 640)
					return fail("Exported chart is missing notes or lane (case " + std::to_string(test) + ", red " + std::to_string(redPixels) +
						", blue " + std::to_string(bluePixels) + ", lane " + std::to_string(footerPixels) + ")");
			}
			else if (data.Finished || File::Exists(data.TemporaryPath)) return fail("Canceled export retained its temporary video");
			if (test == 7 && data.ErrorDetails.empty()) return fail("Export startup failure was not reported");
		}
		Gui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false); Gui::GetIO().AddKeyEvent(ImGuiKey_N, false);
		Gui::GetIO().AddKeyEvent(ImGuiKey_Delete, false); Gui::GetIO().AddKeyEvent(ImGuiKey_Space, false);
		return true;
	}
}
