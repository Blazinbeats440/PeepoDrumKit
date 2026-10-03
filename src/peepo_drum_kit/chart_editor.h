#pragma once
#include "core_types.h"
#include "core_string.h"
#include "chart.h"
#include "chart_editor_context.h"
#include "chart_editor_widgets.h"
#include "chart_editor_video_writer.h"
#include "chart_editor_settings_gui.h"
#include "chart_editor_file_drop.h"
#include "chart_editor_timeline.h"
#include "imgui/imgui_include.h"
#include "audio/audio_engine.h"

#include "test_gui_audio.h"
#include "test_gui_tja.h"

namespace PeepoDrumKit
{
	b8 SaveScreenshotPNG(u32 width, u32 height, const std::vector<u8>& pixels, std::string& outputPath);
	struct AsyncImportChartResult
	{
		std::string ChartFilePath;
		ChartProject Chart;

		struct TJATempData
		{
			std::string FileContentUTF8;
			std::vector<std::string_view> Lines;
			std::vector<TJA::Token> Tokens;
			TJA::ParsedTJA Parsed;
			TJA::ErrorList ParseErrors;
		} TJA;
	};

	struct AsyncLoadSongResult
	{
		std::string SongFilePath;
		Audio::PCMSampleBuffer SampleBuffer;
		Audio::WaveformMipChain WaveformL, WaveformR;
	};

	struct AsyncLoadJacketResult
	{
		std::string JacketFilePath;
		CustomDraw::GPUTexture JacketTexture = {};
	};

	struct ChartEditor
	{
	public:
		ChartEditor();
		~ChartEditor();

	public:
		void DrawFullscreenMenuBar();
		void DrawGui();
		void DrawVideoExportWindow();
		void ConfigureVideoPreview(ChartGamePreview& preview);
		void DrawScreenshotPreview();
		void SavePendingScreenshots();
		void OnAfterRender();
		void RestoreDefaultDockSpaceLayout(ImGuiID dockSpaceID);
		ApplicationHost::CloseResponse OnWindowCloseRequest();

	public:
		void UpdateApplicationWindowTitle(const ChartContext& context);

		void CreateNewChart(ChartContext& context);
		void CreateNewDifficulty(ChartContext& context, DifficultyType difficulty);
		void SaveChart(ChartContext& context, std::string_view filePath = "");
		b8 OpenChartSaveAsDialog(ChartContext& context);
		b8 TrySaveChartOrOpenSaveAsDialog(ChartContext& context);

		void StartAsyncImportingChartFile(std::string_view absoluteChartFilePath);
		void StartAsyncLoadingSongAudioFile(std::string_view absoluteAudioFilePath);
		void StartAsyncLoadingSongJacketFile(std::string_view absoluteJacketFilePath);
		void SetAndStartLoadingChartSongFileName(std::string_view relativeOrAbsoluteAudioFilePath, Undo::UndoHistory& undo);
		void SetAndStartLoadingSongJacketFileName(std::string_view relativeOrAbsoluteAudioFilePath, Undo::UndoHistory& undo);

		b8 OpenLoadChartFileDialog(ChartContext& context);
		b8 OpenLoadAudioFileDialog(Undo::UndoHistory& undo);
		b8 OpenLoadJacketFileDialog(Undo::UndoHistory& undo);

		void CheckOpenSaveConfirmationPopupThenCall(std::function<void()> onSuccess);
		void InternalUpdateAsyncLoading();
		void ReadSelectedCourseIntoTextEditor();
		void WriteTextEditorToSelectedCourse();
		void DrawTextEditorWindow();

	private:
		void StartAsyncReadingDroppedPath(std::string_view path, b8 isDirectory);

		ChartContext context = {};
		ChartTimeline timeline = {};
		ChartGamePreview gamePreview = {};
		ChartCourse* pendingDeleteCourse = nullptr;
		b8 openDeleteCoursePopup = false;
		std::vector<std::string> droppedFolderCharts;
		std::string droppedFolderPath;
		std::string lastDroppedFolderPath;
		b8 reopenDroppedFolderRequested = false;
		FileDrop::Error droppedPathFailure = FileDrop::Error::None;
		std::future<FileDrop::Result> droppedPathFuture;
		b8 droppedPathLoading = false, droppedPathCanceled = false;
		b8 screenshotWindowRequested = false, screenshotPreviewRequested = false;
		i32 screenshotWindowDelay = 0;
		ChartGamePreview screenshotPreview = {};
		ImDrawList* screenshotDrawList = nullptr;
		std::string screenshotStatus;
		f64 screenshotStatusUntil = 0.0;
		struct VideoExportData
		{
			enum class RangeMode { Full, SelectedRange, Preview, Marker };
			b8 ShowWindow = false, Preparing = false, Exporting = false, Finalizing = false, Finished = false, FramePrepared = false;
			RangeMode Range = RangeMode::Full;
			f32 ExcerptSeconds = 15.0f;
			f32 LaneBackgroundTransparency = 0.0f;
			b8 AudioFade = true;
			ChartGamePreview::VideoLayout Layout = ChartGamePreview::VideoLayout::Original;
			b8 ShowTitle = true, ShowSubtitle = false, ShowDifficulty = true, ShowMaxCombo = true, ShowCurrentCombo = true;
			f32 TitleScale = 1.0f, TitlePaddingScale = 1.0f;
			i32 TitleAlignment = 0, TitleVerticalPosition = 0;
			u32 TitleColor = 0xFFFFFFFF, TitleBandColor = 0xB0000000;
			i32 Resolution = 3, FramesPerSecond = 60;
			i32 AudioBitRate = 192000;
			i32 BackgroundSource = 0;
			u32 BackgroundColor = 0xFF1F1F1F;
			b8 BackgroundInitialized = false;
			ChartGamePreview::VideoBackgroundFit BackgroundImageFit = ChartGamePreview::VideoBackgroundFit::ContainWithWidth;
			std::string BackgroundImagePath;
			CustomDraw::GPUTexture DefaultBackgroundTexture = {}, CustomBackgroundTexture = {};
			f32 SongVolume = 1.0f, DrumVolume = 1.0f;
			f32 LeadInSeconds = 1.0f, TailSeconds = 2.0f;
			VideoBranchMode Branch = VideoBranchMode::Normal;
			VideoBranchRoute Route;
			const ChartCourse* RouteCourse = nullptr;
			i32 RouteChanges = -1;
			VideoBranchMode RouteMode = VideoBranchMode::Normal;
			f32 RouteRollSpeed = -1.0f;
			size_t RouteRecordingVersion = 0;
			b8 RouteReady = false;
			ChartCourse* Course = nullptr;
			Audio::SourceHandle SongSource = Audio::SourceHandle::Invalid;
			Time StartTime = Time::Zero(), EndTime = Time::Zero();
			Time ContentStartTime = Time::Zero(), ContentEndTime = Time::Zero();
			i64 FrameIndex = 0, FrameCount = 0;
			CPUStopwatch ExportStopwatch = {};
			f64 LastFrameElapsedSeconds = 0.0, SecondsPerFrame = 0.0;
			i32 ChartChanges = 0;
			ChartGamePreview Preview = {};
			VideoExportSoundTimeline Sounds = {};
			VideoExportWriter Writer = {};
			std::vector<u8> Pixels;
			std::vector<i16> AudioSamples;
			std::string Status;
			std::string ErrorDetails, OutputBasePath, OutputPath, TemporaryPath;
		} videoExport = {};

		std::future<AsyncImportChartResult> importChartFuture {};
		std::future<AsyncLoadSongResult> loadSongFuture {};
		std::future<AsyncLoadJacketResult> loadJacketFuture {};
		CPUStopwatch loadSongStopwatch = {};
		CPUStopwatch loadJacketStopwatch = {};
		b8 createBackupOfOriginalTJABeforeOverwriteSave = false;
		b8 wasAudioEngineRunningIdleOnFocusLost = false;
		b8 tryToCloseApplicationOnNextFrame = false;

		b8 focusHelpWindowNextFrame = false;
		b8 focusUpdateNotesWindowNextFrame = false;
		b8 focusChartStatsWindowNextFrame = false;
		b8 focusLyricsWindowNextFrame = false;
		b8 focusTextEditorWindowNextFrame = false;
		b8 focusSettingsWindowNextFrame = false;

		ChartHelpWindow helpWindow = {};
		ChartUpdateNotesWindow updateNotesWindow = {};
		ChartChartStatsWindow chartStatsWindow = {};
		ChartUndoHistoryWindow undoHistoryWindow = {};
		TempoCalculatorWindow tempoCalculatorWindow = {};
		ChartInspectorWindow chartInspectorWindow = {};
		ChartPropertiesWindow propertiesWindow = {};
		ChartBranchWindow branchWindow = {};
		ChartTempoWindow tempoWindow = {};
		ChartLyricsWindow lyricsWindow = {};
		ChartCommentsWindow commentsWindow = {};
		ChartSettingsWindow settingsWindow = {};
		AudioTestWindow audioTestWindow = {};
		TJATestWindow tjaTestWindow = {};

		struct TextEditorWindowData
		{
			::TextEditor Editor = CreateImGuiColorTextEditWithNiceTheme();
			std::string StatusText;
			b8 StatusIsError = false;
		} textEditorWindow = {};

		struct ZoomPopupData
		{
			b8 IsOpen;
			Time TimeSinceOpen;
			Time TimeSinceLastChange;
			inline void Open() { IsOpen = true; TimeSinceOpen = TimeSinceLastChange = {}; }
			inline void OnChange() { TimeSinceLastChange = {}; }
		} zoomPopup = {};

		struct SaveConfirmationPopupData
		{
			b8 OpenOnNextFrame;
			std::function<void()> OnSuccessFunction;
		} saveConfirmationPopup = {};

		struct PerformanceData
		{
			b8 ShowOverlay;
			f32 FrameTimesMS[256];
			size_t FrameTimeIndex;
			size_t FrameTimeCount;
		} performance = {};
	};
}
