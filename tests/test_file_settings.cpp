#include "peepo_drum_kit/chart_editor_settings.h"
#include "core_io.h"
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <thread>

using namespace PeepoDrumKit;

static int failures = 0;
static void Check(bool condition, const char* description)
{
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); failures++; }
}

int main()
{
	const auto commandLine = CommandLine::GetCommandLineUTF8();
	if (commandLine.Count == 2 && Path::GetFileName(commandLine.Arguments[1]) == u8"起動確認 & (test).tja")
	{
		const std::string report = std::string(commandLine.Arguments[1]) + "\n" + Directory::GetWorkingDirectory() + "\n2";
		return File::WriteAllBytes(std::string(commandLine.Arguments[1]) + ".result", report) ? 0 : 1;
	}

	const std::string executableDirectory = Directory::GetExecutableDirectory();
	Check(GetChartOpenDirectory("") == executableDirectory, "empty open folder uses executable directory");
	Check(GetChartOpenDirectory(executableDirectory + "/__missing_file_settings_test__/nested") == executableDirectory, "missing open folder falls back");
	Check(GetChartOpenDirectory(Directory::GetExecutablePath()) == executableDirectory, "file instead of folder falls back");
	const std::string workingDirectory = Directory::GetWorkingDirectory();
	Check(Path::CopyAndNormalize(GetChartOpenDirectory(workingDirectory)) == Path::CopyAndNormalize(workingDirectory), "valid absolute open folder");
	const std::string relativeFolder = std::filesystem::relative(std::filesystem::u8path(workingDirectory), std::filesystem::u8path(executableDirectory)).u8string();
	Check(std::filesystem::equivalent(std::filesystem::u8path(GetChartOpenDirectory(relativeFolder)), std::filesystem::u8path(workingDirectory)), "relative open folder uses executable as base");

	PersistentAppData app;
	Check(app.RecentFiles.MaxCount == 30, "recent file limit");
	for (int i = 0; i < 35; i++)
		app.RecentFiles.Add(u8"C:\\譜面\\曲" + std::to_string(i) + ".tja");
	Check(app.RecentFiles.SortedPaths.size() == 30, "recent file cap after 35 opens");
	Check(app.RecentFiles.SortedPaths.front() == u8"C:/譜面/曲34.tja" && app.RecentFiles.SortedPaths.back() == u8"C:/譜面/曲5.tja", "newest first and oldest evicted");
	app.RecentFiles.Add(u8"c:/譜面/曲10.TJA");
	Check(app.RecentFiles.SortedPaths.size() == 30 && app.RecentFiles.SortedPaths.front() == u8"c:/譜面/曲10.TJA", "reopen deduplicates case and slash differences");

	std::string appIni;
	app.RelatedFileDirectories.Audio = u8"C:/音源/Space Folder";
	app.RelatedFileDirectories.Jacket = u8"D:/ジャケット";
	app.RelatedFileDirectories.Movie = u8"E:/動画";
	SettingsToIni(app, appIni);
	PersistentAppData restoredApp;
	Check(!ParseSettingsIni(appIni, restoredApp).HasError, "recent files ini parses");
	Check(restoredApp.RecentFiles.SortedPaths == app.RecentFiles.SortedPaths, "all 30 recent files retain order after reload");
	Check(restoredApp.RelatedFileDirectories.Audio == app.RelatedFileDirectories.Audio
		&& restoredApp.RelatedFileDirectories.Jacket == app.RelatedFileDirectories.Jacket
		&& restoredApp.RelatedFileDirectories.Movie == app.RelatedFileDirectories.Movie, "each asset directory survives restart including Unicode");
	app.RecentFiles.Remove(u8"C:\\譜面\\曲10.tja");
	Check(app.RecentFiles.SortedPaths.size() == 29 && app.RecentFiles.SortedPaths.front() == u8"C:/譜面/曲34.tja", "remove normalizes path");

	RecentFilesList sameNames;
	sameNames.Add("C:/First/song.tja");
	sameNames.Add("C:/Second/song.tja");
	Check(sameNames.SortedPaths.size() == 2, "same file name in different folders stays distinct");

	std::string legacyIni = "[recent_files]\n";
	for (int i = 0; i < 9; i++)
		legacyIni += "file_0" + std::to_string(i) + " = C:/Legacy/" + std::to_string(i) + ".tja\n";
	PersistentAppData legacyApp;
	Check(!ParseSettingsIni(legacyIni, legacyApp).HasError && legacyApp.RecentFiles.SortedPaths.size() == 9, "existing nine-item history loads");
	Check(legacyApp.RelatedFileDirectories.Audio.empty() && legacyApp.RelatedFileDirectories.Jacket.empty()
		&& legacyApp.RelatedFileDirectories.Movie.empty(), "old settings have empty asset directory defaults");
	legacyApp.RecentFiles.Add("C:/New/song.tja");
	Check(legacyApp.RecentFiles.SortedPaths.size() == 10, "existing history grows beyond nine");
	PersistentAppData truncatedApp;
	for (int i = 9; i < 40; i++)
		legacyIni += "file_" + std::to_string(i) + " = C:/Legacy/" + std::to_string(i) + ".tja\n";
	Check(!ParseSettingsIni(legacyIni, truncatedApp).HasError && truncatedApp.RecentFiles.SortedPaths.size() == 30
		&& truncatedApp.RecentFiles.SortedPaths.back() == "C:/Legacy/29.tja", "oversized stored history retains the newest thirty");

	UserSettingsData defaults;
	Check(defaults.General.ChartOpenDirectory->empty() && *defaults.General.RecentFilesShowFullPath, "defaults preserve full path and use exe folder");
	Check(defaults.General.TaikojiroExecutablePath->empty(), "Taikojiro executable is initially unconfigured");
	Check(!*defaults.General.AutoPrefixChartSubtitle, "subtitle prefix completion is off by default");
	Check(!*defaults.General.VideoExportHardwareEncoding, "hardware encoding is off by default");
	Check(*defaults.Appearance.PreviewLaneBackgroundTransparency == 0.0f, "lane backgrounds are opaque by default");
	constexpr f32 expectedSENoteOffsets[] = { 5.0f, 2.0f, 6.2f, 2.0f, 2.0f, 3.0f, 4.0f, 2.0f, 2.0f, 2.0f, 3.0f, 0.0f, 4.0f, 4.0f, 0.0f, 0.0f, 0.0f };
	static_assert(ArrayCount(expectedSENoteOffsets) == std::extent_v<decltype(UserSettingsData::DeveloperData::SENoteOffsetX)>);
	for (size_t index = 0; index < ArrayCount(defaults.Developer.SENoteOffsetX); ++index)
		Check(*defaults.Developer.SENoteOffsetX[index] == expectedSENoteOffsets[index], "SENOTES defaults match the calibrated horizontal corrections");
	Check(!ParseSettingsIni("[general]\ndefault_creator_name = Existing\n", defaults).HasError
		&& defaults.General.ChartOpenDirectory->empty() && *defaults.General.RecentFilesShowFullPath
		&& defaults.General.TaikojiroExecutablePath->empty(), "old user settings keep new defaults");
	Check(!*defaults.General.AutoPrefixChartSubtitle, "old settings preserve subtitle editing behavior");
	Check(*defaults.General.GamePreviewLayout == 0, "old user settings retain the original preview layout");
	Check(*defaults.General.GamePreviewArcadeSizeMode == 0 && *defaults.General.GamePreviewJiroSizeMode == 1,
		"old settings preserve the arcade aspect ratio and maximize Jiro previews");
	Check(*defaults.Input.Editor_TogglePreviewLayout == MultiInputBinding(KeyBinding(ImGuiKey_F4)),
		"old user settings use F4 as the preview layout shortcut");
	UserSettingsData configured;
	configured.General.ChartOpenDirectory.Value = u8"C:/譜面フォルダ/Space Folder";
	configured.General.ChartOpenDirectory.SetHasValueIfNotDefault();
	configured.General.TaikojiroExecutablePath.Value = u8"C:/太鼓 さん次郎/太鼓さん次郎.exe";
	configured.General.TaikojiroExecutablePath.SetHasValueIfNotDefault();
	configured.General.AutoPrefixChartSubtitle.Value = true;
	configured.General.AutoPrefixChartSubtitle.SetHasValueIfNotDefault();
	configured.General.VideoExportHardwareEncoding.Value = true;
	configured.General.VideoExportHardwareEncoding.SetHasValueIfNotDefault();
	configured.General.RecentFilesShowFullPath.Value = false;
	configured.General.RecentFilesShowFullPath.SetHasValueIfNotDefault();
	configured.General.GamePreviewLayout.Value = 2;
	configured.General.GamePreviewLayout.SetHasValueIfNotDefault();
	configured.General.GamePreviewArcadeSizeMode.Value = 1;
	configured.General.GamePreviewArcadeSizeMode.SetHasValueIfNotDefault();
	configured.General.GamePreviewJiroSizeMode.Value = 0;
	configured.General.GamePreviewJiroSizeMode.SetHasValueIfNotDefault();
	configured.Input.Editor_TogglePreviewLayout.Value = MultiInputBinding(KeyBinding(ImGuiKey_F10));
	configured.Input.Editor_TogglePreviewLayout.SetHasValueIfNotDefault();
	for (size_t index = 0; index < ArrayCount(configured.Developer.SENoteOffsetX); ++index)
	{
		configured.Developer.SENoteOffsetX[index].Value = -5.0f + static_cast<f32>(index) * 0.25f;
		configured.Developer.SENoteOffsetX[index].SetHasValueIfNotDefault();
	}
	std::string userIni;
	SettingsToIni(configured, userIni);
	UserSettingsData restoredSettings;
	Check(!ParseSettingsIni(userIni, restoredSettings).HasError, "file settings ini parses");
	for (size_t index = 0; index < ArrayCount(configured.Developer.SENoteOffsetX); ++index)
		Check(*restoredSettings.Developer.SENoteOffsetX[index] == *configured.Developer.SENoteOffsetX[index],
			"every SENOTES correction retains its sign and fractional value after reload");
	restoredSettings.Developer.SENoteOffsetX[2].ResetToDefault();
	Check(*restoredSettings.Developer.SENoteOffsetX[2] == 6.2f, "reset restores the calibrated Don correction");
	UserSettingsData zeroCorrection;
	Check(!ParseSettingsIni("[developer]\nsenote_offset_x_don = 0\n", zeroCorrection).HasError
		&& *zeroCorrection.Developer.SENoteOffsetX[2] == 0.0f, "zero disables a correction independently of its default");
	UserSettingsData nonfiniteCorrection;
	const auto nonfiniteResult = ParseSettingsIni("[developer]\nsenote_offset_x_don = nan\n", nonfiniteCorrection);
	Check(nonfiniteResult.HasError || std::isfinite(*nonfiniteCorrection.Developer.SENoteOffsetX[2]),
		"nonfinite SENOTES coordinates cannot reach the renderer");
	Check(*restoredSettings.General.AutoPrefixChartSubtitle, "subtitle prefix setting survives reload");
	Check(*restoredSettings.General.VideoExportHardwareEncoding, "hardware encoding preference survives reload");
	restoredSettings.General.AutoPrefixChartSubtitle.ResetToDefault();
	Check(!*restoredSettings.General.AutoPrefixChartSubtitle, "reset turns subtitle prefix completion off");
	Check(*restoredSettings.General.TaikojiroExecutablePath == *configured.General.TaikojiroExecutablePath,
		"Taikojiro executable with Unicode and spaces survives reload");
	Check(GetTaikojiroExecutablePath("").empty(), "unconfigured Taikojiro path stays empty");
	Check(Path::CopyAndNormalize(GetTaikojiroExecutablePath(u8"次郎/taikojiro.exe")) == Path::CopyAndNormalize(executableDirectory + u8"/次郎/taikojiro.exe"),
		"relative Taikojiro path uses the editor executable folder");
	Check(Path::CopyAndNormalize(GetTaikojiroExecutablePath(*configured.General.TaikojiroExecutablePath)) == *configured.General.TaikojiroExecutablePath,
		"absolute Taikojiro path is preserved");
	Check(*restoredSettings.General.ChartOpenDirectory == *configured.General.ChartOpenDirectory
		&& !*restoredSettings.General.RecentFilesShowFullPath, "unicode folder and file name display survive reload");
	Check(*restoredSettings.General.GamePreviewLayout == 2 && restoredApp.VideoExport.Layout == 0,
		"preview layout survives reload independently of the video layout");
	Check(*restoredSettings.General.GamePreviewArcadeSizeMode == 1 && *restoredSettings.General.GamePreviewJiroSizeMode == 0,
		"arcade and Jiro preview sizes survive reload independently");
	for (f32 transparency : { 0.0f, 37.5f, 100.0f })
	{
		UserSettingsData transparentLanes;
		transparentLanes.Appearance.PreviewLaneBackgroundTransparency.Value = transparency;
		transparentLanes.Appearance.PreviewLaneBackgroundTransparency.SetHasValueIfNotDefault();
		std::string transparencyIni;
		SettingsToIni(transparentLanes, transparencyIni);
		UserSettingsData restoredLanes;
		Check(!ParseSettingsIni(transparencyIni, restoredLanes).HasError
			&& *restoredLanes.Appearance.PreviewLaneBackgroundTransparency == transparency,
			"lane transparency retains fractional values and both endpoints after reload");
		restoredLanes.Appearance.PreviewLaneBackgroundTransparency.ResetToDefault();
		Check(*restoredLanes.Appearance.PreviewLaneBackgroundTransparency == 0.0f,
			"reset restores opaque lane backgrounds");
	}
	UserSettingsData invalidTransparency;
	Check(!ParseSettingsIni("[appearance]\npreview_lane_background_transparency = -10\n", invalidTransparency).HasError
		&& *invalidTransparency.Appearance.PreviewLaneBackgroundTransparency == 0.0f, "negative lane transparency clamps to zero");
	Check(!ParseSettingsIni("[appearance]\npreview_lane_background_transparency = 150\n", invalidTransparency).HasError
		&& *invalidTransparency.Appearance.PreviewLaneBackgroundTransparency == 100.0f, "lane transparency clamps to 100 percent");
	const auto nonfiniteTransparencyResult = ParseSettingsIni("[appearance]\npreview_lane_background_transparency = nan\n", invalidTransparency);
	Check(nonfiniteTransparencyResult.HasError || std::isfinite(*invalidTransparency.Appearance.PreviewLaneBackgroundTransparency),
		"nonfinite lane transparency cannot reach the renderer");
	UserSettingsData invalidSizeModes;
	Check(!ParseSettingsIni("[general]\ngame_preview_arcade_size_mode = 99\ngame_preview_jiro_size_mode = 99\n", invalidSizeModes).HasError
		&& *invalidSizeModes.General.GamePreviewArcadeSizeMode == 1 && *invalidSizeModes.General.GamePreviewJiroSizeMode == 1,
		"both preview size modes clamp high values");
	Check(!ParseSettingsIni("[general]\ngame_preview_arcade_size_mode = -1\ngame_preview_jiro_size_mode = -1\n", invalidSizeModes).HasError
		&& *invalidSizeModes.General.GamePreviewArcadeSizeMode == 0 && *invalidSizeModes.General.GamePreviewJiroSizeMode == 0,
		"both preview size modes clamp negative values");
	restoredSettings.General.GamePreviewArcadeSizeMode.ResetToDefault();
	restoredSettings.General.GamePreviewJiroSizeMode.ResetToDefault();
	Check(*restoredSettings.General.GamePreviewArcadeSizeMode == 0 && *restoredSettings.General.GamePreviewJiroSizeMode == 1,
		"reset restores each layout's preview size default");
	for (i32 layout : { 0, 2 })
	{
		UserSettingsData legacyBarLines;
		const std::string legacyBarLineIni = "[general]\ngame_preview_layout = " + std::to_string(layout)
			+ "\ngame_preview_bar_line_mode = " + (layout == 2 ? "0\n" : "1\n");
		Check(!ParseSettingsIni(legacyBarLineIni, legacyBarLines).HasError && *legacyBarLines.General.GamePreviewLayout == layout,
			"retired bar line mode does not interfere with the selected layout");
		std::string upgradedIni;
		SettingsToIni(legacyBarLines, upgradedIni);
		Check(upgradedIni.find("game_preview_bar_line_mode") == std::string::npos,
			"retired bar line mode is omitted when settings are saved");
	}
	Check(*restoredSettings.Input.Editor_TogglePreviewLayout == *configured.Input.Editor_TogglePreviewLayout,
		"custom preview layout shortcut survives reload");
	UserSettingsData invalidLayout;
	Check(!ParseSettingsIni("[general]\ngame_preview_layout = 1\n", invalidLayout).HasError
		&& *invalidLayout.General.GamePreviewLayout == 1, "existing arcade layout settings are preserved");
	Check(!ParseSettingsIni("[general]\ngame_preview_layout = 99\n", invalidLayout).HasError
		&& *invalidLayout.General.GamePreviewLayout == 2, "preview layout clamps a high value");
	Check(!ParseSettingsIni("[general]\ngame_preview_layout = -1\n", invalidLayout).HasError
		&& *invalidLayout.General.GamePreviewLayout == 0, "preview layout clamps a negative value");
	UserSettingsData shortcutCycle;
	shortcutCycle.General.GamePreviewArcadeSizeMode.Value = 1;
	shortcutCycle.Input.Editor_TogglePreviewLayout.Value = *configured.Input.Editor_TogglePreviewLayout;
	shortcutCycle.Input.Editor_TogglePreviewLayout.SetHasValueIfNotDefault();
	const i32 expectedPreviewCycle[][3] = { { 1, 0, 1 }, { 1, 1, 1 }, { 2, 1, 0 }, { 2, 1, 1 }, { 0, 1, 1 } };
	for (i32 pass = 0; pass < 2; ++pass)
	for (const auto& expectedState : expectedPreviewCycle)
	{
		shortcutCycle.IsDirty = false;
		shortcutCycle.CycleGamePreviewLayout();
		Check(*shortcutCycle.General.GamePreviewLayout == expectedState[0]
			&& *shortcutCycle.General.GamePreviewArcadeSizeMode == expectedState[1]
			&& *shortcutCycle.General.GamePreviewJiroSizeMode == expectedState[2],
			"preview shortcut cycles through all five layout and size combinations and wraps");
		Check(shortcutCycle.IsDirty, "every preview shortcut transition marks settings for saving");
		std::string cycleIni;
		SettingsToIni(shortcutCycle, cycleIni);
		UserSettingsData restoredCycle;
		Check(!ParseSettingsIni(cycleIni, restoredCycle).HasError
			&& *restoredCycle.General.GamePreviewLayout == expectedState[0]
			&& *restoredCycle.General.GamePreviewArcadeSizeMode == expectedState[1]
			&& *restoredCycle.General.GamePreviewJiroSizeMode == expectedState[2]
			&& *restoredCycle.Input.Editor_TogglePreviewLayout == *configured.Input.Editor_TogglePreviewLayout,
			"each shortcut preset and the remapped key survive saving and reloading");
	}
	UserSettingsData manualPreview;
	manualPreview.General.GamePreviewLayout.Value = 2;
	manualPreview.General.GamePreviewArcadeSizeMode.Value = 0;
	manualPreview.General.GamePreviewJiroSizeMode.Value = 1;
	manualPreview.CycleGamePreviewLayout();
	Check(*manualPreview.General.GamePreviewLayout == 0 && *manualPreview.General.GamePreviewArcadeSizeMode == 0,
		"cycling from a manually selected Jiro maximized preset preserves the inactive arcade preference");
	restoredSettings.General.ChartOpenDirectory.ResetToDefault();
	restoredSettings.General.TaikojiroExecutablePath.ResetToDefault();
	Check(restoredSettings.General.TaikojiroExecutablePath->empty(), "reset clears the Taikojiro executable");
	restoredSettings.General.RecentFilesShowFullPath.ResetToDefault();
	Check(GetChartOpenDirectory(*restoredSettings.General.ChartOpenDirectory) == executableDirectory
		&& *restoredSettings.General.RecentFilesShowFullPath, "reset restores folder and display defaults");

	const auto testRoot = std::filesystem::current_path() / "build" / "taikojiro-launch-tests"
		/ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const auto testExecutable = testRoot / std::filesystem::u8path(u8"太鼓 さん次郎") / std::filesystem::u8path(u8"太鼓さん次郎 (test).exe");
	const auto testChart = testRoot / std::filesystem::u8path(u8"譜面 フォルダ") / std::filesystem::u8path(u8"起動確認 & (test).tja");
	std::filesystem::create_directories(testExecutable.parent_path());
	std::filesystem::create_directories(testChart.parent_path());
	std::filesystem::copy_file(std::filesystem::u8path(Directory::GetExecutablePath()), testExecutable);
	Check(File::WriteAllBytes(testChart.u8string(), "TEST TJA"), "launch test chart can be written");
	Check(!Shell::OpenFileWithExecutable("", testChart.u8string()), "empty executable cannot launch");
	Check(!Shell::OpenFileWithExecutable(testExecutable.u8string(), ""), "empty chart cannot launch");
	Check(!Shell::OpenFileWithExecutable((testRoot / "missing.exe").u8string(), testChart.u8string()), "missing executable cannot launch");
	Check(!Shell::OpenFileWithExecutable(testExecutable.u8string(), (testRoot / "missing.tja").u8string()), "missing chart cannot launch");
	Check(!Shell::OpenFileWithExecutable(testExecutable.u8string(), testChart.parent_path().u8string()), "directory cannot be a chart argument");
	Check(!Shell::OpenFileWithExecutable(testExecutable.u8string(), testChart.u8string() + "\" extra"), "quoted extra arguments are rejected");
	Check(!Shell::OpenFileWithExecutable(testExecutable.u8string(), std::string("path\0extra", 10)), "embedded null cannot truncate an argument");
	Check(Shell::OpenFileWithExecutable(testExecutable.u8string(), testChart.u8string()), "executable launches with Unicode and spaces in both paths");
	const std::string expectedReport = testChart.u8string() + "\n" + testExecutable.parent_path().u8string() + "\n2";
	const std::string reportPath = testChart.u8string() + ".result";
	bool receivedReport = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline)
	{
		const auto report = File::ReadAllBytes(reportPath);
		if (report.Content && std::string_view(reinterpret_cast<const char*>(report.Content.get()), report.Size) == expectedReport)
		{
			receivedReport = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	Check(receivedReport, "child receives exactly one absolute TJA argument and uses the executable working folder");

	if (failures != 0) return 1;
	std::puts("File settings tests passed.");
	return 0;
}
