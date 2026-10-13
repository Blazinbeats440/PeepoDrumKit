#include "peepo_drum_kit/chart_editor_settings.h"
#include "core_io.h"
#include <cstdio>
#include <filesystem>

using namespace PeepoDrumKit;

static int failures = 0;
static void Check(bool condition, const char* description)
{
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); failures++; }
}

int main()
{
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
	Check(!ParseSettingsIni("[general]\ndefault_creator_name = Existing\n", defaults).HasError
		&& defaults.General.ChartOpenDirectory->empty() && *defaults.General.RecentFilesShowFullPath, "old user settings keep new defaults");
	Check(*defaults.General.GamePreviewLayout == 0, "old user settings retain the original preview layout");
	Check(*defaults.Input.Editor_TogglePreviewLayout == MultiInputBinding(KeyBinding(ImGuiKey_F4)),
		"old user settings use F4 as the preview layout shortcut");
	UserSettingsData configured;
	configured.General.ChartOpenDirectory.Value = u8"C:/譜面フォルダ/Space Folder";
	configured.General.ChartOpenDirectory.SetHasValueIfNotDefault();
	configured.General.RecentFilesShowFullPath.Value = false;
	configured.General.RecentFilesShowFullPath.SetHasValueIfNotDefault();
	configured.General.GamePreviewLayout.Value = 1;
	configured.General.GamePreviewLayout.SetHasValueIfNotDefault();
	configured.Input.Editor_TogglePreviewLayout.Value = MultiInputBinding(KeyBinding(ImGuiKey_F10));
	configured.Input.Editor_TogglePreviewLayout.SetHasValueIfNotDefault();
	std::string userIni;
	SettingsToIni(configured, userIni);
	UserSettingsData restoredSettings;
	Check(!ParseSettingsIni(userIni, restoredSettings).HasError, "file settings ini parses");
	Check(*restoredSettings.General.ChartOpenDirectory == *configured.General.ChartOpenDirectory
		&& !*restoredSettings.General.RecentFilesShowFullPath, "unicode folder and file name display survive reload");
	Check(*restoredSettings.General.GamePreviewLayout == 1 && restoredApp.VideoExport.Layout == 0,
		"preview layout survives reload independently of the video layout");
	Check(*restoredSettings.Input.Editor_TogglePreviewLayout == *configured.Input.Editor_TogglePreviewLayout,
		"custom preview layout shortcut survives reload");
	UserSettingsData invalidLayout;
	Check(!ParseSettingsIni("[general]\ngame_preview_layout = 99\n", invalidLayout).HasError
		&& *invalidLayout.General.GamePreviewLayout == 1, "preview layout clamps a high value");
	Check(!ParseSettingsIni("[general]\ngame_preview_layout = -1\n", invalidLayout).HasError
		&& *invalidLayout.General.GamePreviewLayout == 0, "preview layout clamps a negative value");
	restoredSettings.General.ChartOpenDirectory.ResetToDefault();
	restoredSettings.General.RecentFilesShowFullPath.ResetToDefault();
	Check(GetChartOpenDirectory(*restoredSettings.General.ChartOpenDirectory) == executableDirectory
		&& *restoredSettings.General.RecentFilesShowFullPath, "reset restores folder and display defaults");

	if (failures != 0) return 1;
	std::puts("File settings tests passed.");
	return 0;
}
