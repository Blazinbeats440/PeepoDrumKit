#include "core_types.h"
#include "core_string.h"
#include "chart_editor.h"
#include "chart_editor_settings.h"
#include "chart_editor_i18n.h"
#include "core_log.h"
#include "imgui/backend/imgui_impl_d3d11.h"
#include "imgui/backend/imgui_impl_win32.h"
#include <cstdio>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wrl/client.h>

namespace PeepoDrumKit
{
	struct EditorWindowLayouts
	{
		cstr EditingIniFileName = "settings_imgui.ini";
		cstr TestPlayIniFileName = "settings_imgui_test_play.ini";
		std::string EditingLayout, TestPlayLayout;
		b8 IsTestPlayActive = false;

		b8 Update(b8 isTestPlayActive)
		{
			if (isTestPlayActive == IsTestPlayActive) return false;
			auto& io = Gui::GetIO();
			auto& currentLayout = IsTestPlayActive ? TestPlayLayout : EditingLayout;
			size_t iniSize = 0;
			const cstr iniData = Gui::SaveIniSettingsToMemory(&iniSize);
			currentLayout.assign(iniData, iniSize);
			Gui::SaveIniSettingsToDisk(io.IniFilename);

			auto& nextLayout = isTestPlayActive ? TestPlayLayout : EditingLayout;
			if (nextLayout.empty() && isTestPlayActive)
			{
				const auto fileContent = File::ReadAllBytes(TestPlayIniFileName);
				if (fileContent.Content != nullptr && fileContent.Size > 0)
					nextLayout.assign(fileContent.AsString());
			}
			if (nextLayout.empty()) nextLayout = currentLayout;
			Gui::ClearIniSettings();
			Gui::LoadIniSettingsFromMemory(nextLayout.data(), nextLayout.size());
			io.IniFilename = isTestPlayActive ? TestPlayIniFileName : EditingIniFileName;
			IsTestPlayActive = isTestPlayActive;
			return true;
		}
	};

	struct ImGuiApplication
	{
		ChartEditor ChartEditor = {};
		EditorWindowLayouts WindowLayouts = {};
		EditorColorThemeState ColorTheme = {};

		ImGuiApplication()
		{
			auto& io = Gui::GetIO();
			io.ConfigWindowsMoveFromTitleBarOnly = true;
			io.KeyRepeatDelay = 0.450f;
			io.KeyRepeatRate = 0.050f;

			auto& style = Gui::GetStyle();
			style.CellPadding.y = 4.0f;
			style.FramePadding.y = 4.0f;
			// NOTE: The one downside of disabling this is that is makes tab bars and docked windows look the same
			style.WindowMenuButtonPosition = ImGuiDir_None;

			ColorTheme.Update(*Settings.Appearance.ColorTheme, GuiScaleFactorTarget, &style,
				*Settings.Appearance.ColorThemeAccent, *Settings.Appearance.ColorThemeAccentColor);
		}

		void OnUpdate()
		{
			ColorTheme.Update(*Settings.Appearance.ColorTheme, GuiScaleFactorTarget, &Gui::GetStyle(),
				*Settings.Appearance.ColorThemeAccent, *Settings.Appearance.ColorThemeAccentColor);
			const b8 videoExportActive = ChartEditor.GetIsVideoExportActive();
			const b8 enteredTestPlay = !videoExportActive && WindowLayouts.Update(ChartEditor.GetIsTestPlayActive()) && WindowLayouts.IsTestPlayActive;
			if (enteredTestPlay) Gui::SetWindowCollapsed(UI_WindowName("TAB_TEST_PLAY"), false);

			static constexpr ImGuiWindowFlags fullscreenWindowFlags =
				ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
				ImGuiWindowFlags_NoBackground;

			const ImGuiViewport* mainViewport = Gui::GetMainViewport();
			Gui::SetNextWindowPos(mainViewport->WorkPos);
			Gui::SetNextWindowSize(mainViewport->WorkSize);
			Gui::SetNextWindowViewport(mainViewport->ID);

			Gui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			Gui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			Gui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
			Gui::Begin("FullscreenDockSpaceWindow", nullptr, fullscreenWindowFlags);
			Gui::PopStyleVar(3);
			{
				const ImGuiID dockSpaceID = Gui::GetID("FullscreenDockSpace");
				if (Gui::DockBuilderGetNode(dockSpaceID) == nullptr)
					ChartEditor.RestoreDefaultDockSpaceLayout(dockSpaceID);

				Gui::DockSpace(dockSpaceID, { 0.0f, 0.0f }, ImGuiDockNodeFlags_PassthruCentralNode | (videoExportActive ? ImGuiDockNodeFlags_KeepAliveOnly : 0));
				ChartEditor.DrawFullscreenMenuBar();
			}
			Gui::End();

			ChartEditor.DrawGui();
			if (enteredTestPlay) Gui::SetWindowFocus(UI_WindowName("TAB_TEST_PLAY"));
		}

		ApplicationHost::CloseResponse OnWindowCloseRequest()
		{
			return ChartEditor.OnWindowCloseRequest();
		}
	};

	static b8 RunWindowLayoutSelfTest(std::string& error)
	{
		error.clear();
		const auto fail = [&](cstr message) { error = message; return false; };
		const std::string testDirectory = "build/window-layout-tests";
		if (!Directory::Exists(testDirectory) && !Directory::Create(testDirectory))
			return fail("Unable to create the window layout test directory");
		const std::string testPrefix = testDirectory + "/" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());
		const std::string editingFile = testPrefix + "-editing.ini", testPlayFile = testPrefix + "-test-play.ini";
		ImGuiContext* previousContext = Gui::GetCurrentContext();
		ImGuiContext* testContext = Gui::CreateContext();
		Gui::SetCurrentContext(testContext);
		defer {
			Gui::SetCurrentContext(testContext);
			Gui::GetIO().IniFilename = nullptr;
			Gui::DestroyContext(testContext);
			Gui::SetCurrentContext(previousContext);
		};
		const auto initializeContext = [&]
		{
			auto& io = Gui::GetIO();
			io.IniFilename = editingFile.c_str();
			io.LogFilename = nullptr;
			io.DisplaySize = vec2(1280.0f, 720.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigErrorRecoveryEnableAssert = io.ConfigErrorRecoveryEnableDebugLog = io.ConfigErrorRecoveryEnableTooltip = false;
			Gui::GetCurrentContext()->ErrorCallbackUserData = &error;
			Gui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void* userData, cstr message) { *static_cast<std::string*>(userData) += message; };
			unsigned char* pixels;
			i32 atlasWidth, atlasHeight;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
		};
		initializeContext();
		EditorWindowLayouts layouts;
		layouts.EditingIniFileName = editingFile.c_str();
		layouts.TestPlayIniFileName = testPlayFile.c_str();
		const auto drawFrame = [&](b8 testPlayActive, b8 rearrange = false, b8 exportActive = false)
		{
			Gui::NewFrame();
			const b8 changed = layouts.Update(testPlayActive);
			Gui::SetNextWindowPos(vec2(0.0f));
			Gui::SetNextWindowSize(Gui::GetIO().DisplaySize);
			Gui::Begin("Window layout self-test host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
			const ImGuiID dockSpaceID = Gui::GetID("WindowLayoutSelfTestDockSpace");
			if (Gui::DockBuilderGetNode(dockSpaceID) == nullptr)
			{
				Gui::DockBuilderAddNode(dockSpaceID, ImGuiDockNodeFlags_DockSpace);
				Gui::DockBuilderSetNodeSize(dockSpaceID, Gui::GetContentRegionAvail());
				ImGuiID previewDock, timelineDock;
				Gui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Down, 0.4f, &timelineDock, &previewDock);
				Gui::DockBuilderDockWindow(UI_WindowName("TAB_GAME_PREVIEW"), previewDock);
				Gui::DockBuilderDockWindow(UI_WindowName("TAB_TIMELINE"), timelineDock);
				Gui::DockBuilderFinish(dockSpaceID);
			}
			Gui::DockSpace(dockSpaceID, vec2(0.0f), exportActive ? ImGuiDockNodeFlags_KeepAliveOnly : 0);
			Gui::End();
			if (exportActive) { Gui::Render(); return changed; }
			if (rearrange)
			{
				Gui::SetNextWindowDockID(0);
				Gui::SetNextWindowPos(vec2(160.0f, 80.0f));
				Gui::SetNextWindowSize(vec2(640.0f, 360.0f));
			}
			Gui::Begin(UI_WindowName("TAB_GAME_PREVIEW")); Gui::End();
			Gui::Begin(UI_WindowName("TAB_TIMELINE")); Gui::End();
			if (testPlayActive)
			{
				Gui::SetNextWindowPos(vec2(820.0f, 80.0f), rearrange ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
				Gui::SetNextWindowSize(vec2(400.0f, 200.0f), rearrange ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
				Gui::Begin(UI_WindowName("TAB_TEST_PLAY")); Gui::End();
			}
			Gui::Render();
			return changed;
		};
		struct WindowPlacement { ImGuiID DockID; vec2 Position, Size; };
		const auto placement = [](cstr name)
		{
			const auto* window = Gui::FindWindowByName(name);
			return WindowPlacement { window->DockId, vec2(window->Pos), vec2(window->SizeFull) };
		};
		const auto samePlacement = [](const WindowPlacement& first, const WindowPlacement& second)
		{
			return first.DockID == second.DockID && first.Position == second.Position && first.Size == second.Size;
		};
		for (i32 frame = 0; frame < 3; ++frame) drawFrame(false);
		if (!error.empty()) return false;
		const auto editingPreview = placement(UI_WindowName("TAB_GAME_PREVIEW"));
		const auto editingTimeline = placement(UI_WindowName("TAB_TIMELINE"));
		for (i32 frame = 0; frame < 30; ++frame) drawFrame(false, false, true);
		for (i32 frame = 0; frame < 3; ++frame) drawFrame(false);
		if (!samePlacement(editingPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
			!samePlacement(editingTimeline, placement(UI_WindowName("TAB_TIMELINE"))))
			return fail("Hiding editor windows during video export changed their layout");
		if (!drawFrame(true)) return fail("Starting test play did not switch window layouts");
		for (i32 frame = 0; frame < 2; ++frame) drawFrame(true);
		if (!samePlacement(editingPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
			!samePlacement(editingTimeline, placement(UI_WindowName("TAB_TIMELINE"))))
			return fail("First test play did not inherit the editing window layout");
		drawFrame(true, true);
		drawFrame(true);
		const auto testPlayPreview = placement(UI_WindowName("TAB_GAME_PREVIEW"));
		const auto testPlayControls = placement(UI_WindowName("TAB_TEST_PLAY"));
		if (testPlayPreview.DockID != 0 || samePlacement(editingPreview, testPlayPreview))
			return fail("The test play window layout fixture did not change");
		for (i32 frame = 0; frame < 3; ++frame)
			if (drawFrame(true) || !samePlacement(testPlayPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
				!samePlacement(testPlayControls, placement(UI_WindowName("TAB_TEST_PLAY"))))
				return fail("Playing or pausing changed the test play window layout");
		if (!drawFrame(false)) return fail("Exiting test play did not switch window layouts");
		for (i32 frame = 0; frame < 2; ++frame) drawFrame(false);
		if (!samePlacement(editingPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
			!samePlacement(editingTimeline, placement(UI_WindowName("TAB_TIMELINE"))))
			return fail("Exiting test play did not restore the editing window layout");
		drawFrame(true);
		for (i32 frame = 0; frame < 2; ++frame) drawFrame(true);
		if (!samePlacement(testPlayPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
			!samePlacement(testPlayControls, placement(UI_WindowName("TAB_TEST_PLAY"))))
			return fail("The next test play did not restore its window layout");
		Gui::SaveIniSettingsToDisk(Gui::GetIO().IniFilename);
		if (!File::Exists(editingFile) || !File::Exists(testPlayFile)) return fail("Both window layout files were not saved");
		if (!error.empty()) return false;

		ImGuiContext* restartedContext = Gui::CreateContext();
		Gui::SetCurrentContext(restartedContext);
		defer {
			Gui::SetCurrentContext(restartedContext);
			Gui::GetIO().IniFilename = nullptr;
			Gui::DestroyContext(restartedContext);
			Gui::SetCurrentContext(testContext);
		};
		initializeContext();
		layouts = {};
		layouts.EditingIniFileName = editingFile.c_str();
		layouts.TestPlayIniFileName = testPlayFile.c_str();
		for (i32 frame = 0; frame < 3; ++frame) drawFrame(false);
		if (!samePlacement(editingPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))))
			return fail("Restarting did not load the editing window layout");
		for (i32 frame = 0; frame < 3; ++frame) drawFrame(true);
		if (!samePlacement(testPlayPreview, placement(UI_WindowName("TAB_GAME_PREVIEW"))) ||
			!samePlacement(testPlayControls, placement(UI_WindowName("TAB_TEST_PLAY"))))
			return fail("Restarting did not load the test play window layout");
		return error.empty();
	}

	b8 ChartSettingsWindow::RunNavigationSelfTest(std::string& error)
	{
		error.clear();
		const auto fail = [&](std::string message) { error = std::move(message); return false; };
		auto& io = Gui::GetIO();
		io.ConfigErrorRecoveryEnableAssert = io.ConfigErrorRecoveryEnableDebugLog = io.ConfigErrorRecoveryEnableTooltip = false;
		Gui::GetCurrentContext()->ErrorCallbackUserData = &error;
		Gui::GetCurrentContext()->ErrorCallback = [](ImGuiContext*, void* userData, cstr message) { *static_cast<std::string*>(userData) += message; };
		ImFont* savedFont = FontMain;
		defer { FontMain = savedFont; };
		FontMain = io.Fonts->AddFontFromFileTTF("assets/NotoSansCJKjp-Regular.otf", 16.0f);
		if (!FontMain) return fail("Cannot load settings navigation test font");
		i18n::ReloadLocaleFile("ja");
		Audio::Engine.ApplicationStartup();
		defer { Audio::Engine.ApplicationShutdown(); };
		ChartContext context;
		UserSettingsData preferences;
		ChartSettingsWindow settings;
		EditorColorThemeState themeState;
		const ImGuiStyle previousStyle = Gui::GetStyle();
		defer { ApplyEditorColorTheme(*Settings.Appearance.ColorTheme, &Gui::GetStyle(), *Settings.Appearance.ColorThemeAccent,
			*Settings.Appearance.ColorThemeAccentColor); Gui::GetStyle() = previousStyle; };
		if (settings.showDeveloperSettings) return fail("Developer settings are visible before the activation shortcut");
		ImGuiWindow* sidebar = nullptr;
		ImGuiWindow* content = nullptr;
		f32 categoryStep = 0.0f, sectionStep = 0.0f;
		b8 preferencesChanged = false;
		const auto drawFrame = [&](vec2 mouse = vec2(-F32Max), i32 mouseDown = -1)
		{
			themeState.Update(*preferences.Appearance.ColorTheme, GuiScaleFactorTarget, &Gui::GetStyle(),
				*preferences.Appearance.ColorThemeAccent, *preferences.Appearance.ColorThemeAccentColor);
			ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
			io.DisplaySize = vec2(620, 300); io.DeltaTime = 0.1f;
			io.AddMousePosEvent(mouse.x, mouse.y);
			if (mouseDown >= 0) io.AddMouseButtonEvent(0, mouseDown != 0);
			Gui::NewFrame(); Gui::PushFont(FontMain, 16.0f);
			Gui::SetNextWindowPos(vec2(0.0f)); Gui::SetNextWindowSize(io.DisplaySize);
			Gui::Begin("Settings navigation self-test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGuiWindow* host = Gui::GetCurrentWindow();
			const ImGuiID sidebarID = Gui::GetID("SettingsCategories");
			Gui::PushID(static_cast<i32>(settings.selectedCategory));
			const ImGuiID contentID = Gui::GetID("SettingsContent");
			Gui::PopID();
			categoryStep = Gui::GetFontSize() + GuiScale(10.0f) + Gui::GetStyle().ItemSpacing.y;
			sectionStep = Gui::GetFontSize() + Gui::GetStyle().ItemSpacing.y;
			preferencesChanged |= settings.DrawGui(context, preferences);
			for (ImGuiWindow* window : Gui::GetCurrentContext()->Windows)
				if (window->ParentWindow == host)
				{
					if (window->ChildId == sidebarID) sidebar = window;
					if (window->ChildId == contentID) content = window;
				}
			Gui::End(); Gui::PopFont(); Gui::Render();
			ImGui_ImplDX11_RenderDrawData(Gui::GetDrawData());
		};
		const auto saveView = [&]
		{
			u32 width = 0, height = 0;
			std::vector<u8> pixels;
			std::string path;
			if (!ApplicationHost::CaptureWindowToBGRA(width, height, pixels) || !SaveScreenshotPNG(width, height, pixels, path)) return false;
			std::printf("Settings navigation verification: %s\n", path.c_str());
			return true;
		};
		settings.SelectCategory(ChartSettingsCategory::Editor);
		for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		if (!error.empty()) return false;
		const std::vector<std::string> expected { UI_Str("SETTINGS_SECTION_PROPERTIES"), UI_Str("SETTINGS_SECTION_EVENTS"), UI_Str("SETTINGS_SECTION_PREVIEW") };
		if (settings.sectionNavigation.Sections != expected || !sidebar || !content) return fail("Editor section navigation does not match its headings");
		if (!saveView()) return fail("Cannot capture settings navigation overview");
		strcpy_s(settings.settingsFilterMain.InputBuf, "No matching preference"); settings.settingsFilterMain.Build();
		for (i32 frame = 0; frame < 2; ++frame) drawFrame();
		if (settings.sectionNavigation.Sections != expected) return fail("Filtering removed section navigation links");
		const vec2 previewLink(sidebar->DC.CursorStartPos.x + GuiScale(30.0f),
			sidebar->DC.CursorStartPos.y + 4 * categoryStep + 2 * sectionStep + Gui::GetFontSize() * 0.5f);
		drawFrame(previewLink, 1); drawFrame(previewLink, 0);
		for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		if (settings.sectionNavigation.SelectedSection != expected[2] || settings.settingsFilterMain.IsActive() || content->Scroll.y < 100.0f)
			return fail("Clicking a section did not clear the filter and jump to its heading");
		if (preferencesChanged) return fail("Section navigation changed preference values");
		if (!saveView()) return fail("Cannot capture settings section jump");
		const vec2 categoryHover(sidebar->DC.CursorStartPos.x + GuiScale(30.0f), sidebar->DC.CursorStartPos.y + categoryStep * 0.5f);
		for (i32 frame = 0; frame < 12; ++frame) drawFrame(categoryHover);
		for (ImGuiWindow* window : Gui::GetCurrentContext()->Windows)
			if (window->Active && (window->Flags & ImGuiWindowFlags_Tooltip)) return fail("Hovering the settings sidebar opened a tooltip");
		settings.SelectSection(expected.front());
		for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		if (content->Scroll.y > 150.0f) return fail("Jumping back to the first settings section did not scroll upward");
		for (i32 category = 0; category < static_cast<i32>(ChartSettingsCategory::Developer); ++category)
		{
			settings.SelectCategory(static_cast<ChartSettingsCategory>(category));
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			if (settings.sectionNavigation.Sections.empty()) return fail("A settings category has no section navigation");
			if (!error.empty()) return false;
		}
		settings.SelectSection(UI_Str("SETTINGS_CATEGORY_TIMELINE"));
		for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		ImGuiWindow* bindings = nullptr;
		for (ImGuiWindow* window : Gui::GetCurrentContext()->Windows)
			if (window->ParentWindow == content && std::string_view(window->Name).find("BindingsTable") != std::string_view::npos) bindings = window;
		if (!bindings || bindings->Scroll.y <= 0.0f) return fail("Shortcut section navigation did not scroll its inner table");
		if (!saveView()) return fail("Cannot capture the translated key bindings");
		const auto toggleDeveloper = [&]
		{
			Gui::SetWindowFocus("Settings navigation self-test");
			io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiMod_Shift, true); io.AddKeyEvent(ImGuiMod_Alt, true);
			io.AddKeyEvent(ImGuiKey_D, true);
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			io.AddKeyEvent(ImGuiKey_D, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false); io.AddKeyEvent(ImGuiMod_Shift, false); io.AddKeyEvent(ImGuiMod_Alt, false);
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		};
		toggleDeveloper();
		if (!settings.showDeveloperSettings || settings.selectedCategory != ChartSettingsCategory::Developer ||
			settings.sectionNavigation.Sections != std::vector<std::string> { UI_Str("SETTINGS_SECTION_SENOTES") })
			return fail("The developer shortcut did not reveal and open the SENOTES settings");
		if (!saveView()) return fail("Cannot capture the developer SENOTES settings");
		preferences.Developer.SENoteOffsetX[2].Value = -1.25f;
		preferences.Developer.SENoteOffsetX[2].SetHasValueIfNotDefault();
		toggleDeveloper();
		if (settings.showDeveloperSettings || settings.selectedCategory != ChartSettingsCategory::General ||
			*preferences.Developer.SENoteOffsetX[2] != -1.25f)
			return fail("Hiding developer settings reset a correction or left the hidden category selected");
		toggleDeveloper();
		if (!settings.showDeveloperSettings || *preferences.Developer.SENoteOffsetX[2] != -1.25f || preferencesChanged)
			return fail("Showing developer settings changed preference values");
		ChartSettingsWindow freshSettings;
		if (freshSettings.showDeveloperSettings) return fail("A new settings window did not hide developer settings");
		settings.SelectCategory(ChartSettingsCategory::About);
		for (i32 frame = 0; frame < 3; ++frame) drawFrame();
		if (preferencesChanged || !saveView()) return fail("The About page changed preferences or could not be captured");
		settings.SelectCategory(ChartSettingsCategory::Appearance);
		for (i32 theme : { 0, 1, 2, 0 })
		{
			preferences.Appearance.ColorTheme.Value = theme;
			preferences.Appearance.ColorTheme.SetHasValueIfNotDefault();
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			settings.SelectSection(UI_Str("SETTINGS_SECTION_THEME"));
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			if (settings.sectionNavigation.Sections.empty() || settings.sectionNavigation.Sections.front() != UI_Str("SETTINGS_SECTION_THEME")
				|| themeState.AppliedTheme != theme || !error.empty()) return fail("Theme selection was not available or did not apply");
			auto textEditor = CreateImGuiColorTextEditWithNiceTheme();
			const auto originalPalette = textEditor.GetPalette();
			ApplyTextEditorColorTheme(textEditor, theme);
			if (theme == 2 && textEditor.GetPalette() != ::TextEditor::GetLightPalette()) return fail("The TJA editor did not follow Light");
			ApplyTextEditorColorTheme(textEditor, 0);
			if (textEditor.GetPalette() != originalPalette) return fail("The TJA editor did not restore its original palette");
			if (!saveView()) return fail("Cannot capture a color theme");
		}
		const std::pair<i32, i32> accentSamples[] = { { 0, 2 }, { 1, 5 }, { 0, 4 }, { 1, 7 } };
		for (const auto& sample : accentSamples)
		{
			preferences.Appearance.ColorTheme.Value = sample.first;
			preferences.Appearance.ColorThemeAccent.Value = sample.second;
			preferences.Appearance.ColorThemeAccent.SetHasValueIfNotDefault();
			preferences.Appearance.ColorThemeAccentColor.Value = 0xFFB060E0;
			preferences.Appearance.ColorThemeAccentColor.SetHasValueIfNotDefault();
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			Gui::SetScrollY(content, GuiScale(140.0f));
			for (i32 frame = 0; frame < 3; ++frame) drawFrame();
			if (themeState.AppliedAccent != sample.second || !error.empty()) return fail("Accent selection did not apply");
			if (!saveView()) return fail("Cannot capture an accent color");
		}
		return error.empty();
	}

	enum class LoadSettingsResponse { FileNotFound, AllGood, ErrorAbort, ErrorRetry, ErrorIgnore };

	template <typename T>
	static LoadSettingsResponse ReadParseSettingsIniFile(cstr iniFilePath, T& out)
	{
		auto fileContent = File::ReadAllBytes(iniFilePath);
		if (fileContent.Content == nullptr || fileContent.Size <= 0)
			return LoadSettingsResponse::AllGood;

		const SettingsParseResult parseResult = ParseSettingsIni(std::string_view(reinterpret_cast<const char*>(fileContent.Content.get()), fileContent.Size), out);
		if (!parseResult.HasError)
			return LoadSettingsResponse::AllGood;

		out.ResetDefault();

		char messageBuffer[2048];
		const int messageLength = sprintf_s(messageBuffer,
			"Failed to parse settings file:\n"
			"\"%s\"\n"
			"\n"
			"Syntax Error '%s' on Line %d\n"
			"\n"
			"[ Abort ] to exit application\n"
			"[ Retry ]  to read file again\n"
			"[ Ignore ] to *LOSE ALL SETTINGS* and restore the defaults\n"
			, iniFilePath, !parseResult.ErrorMessage.empty() ? parseResult.ErrorMessage.c_str() : "Unknown", parseResult.ErrorLineIndex + 1);

		const Shell::MessageBoxResult result = Shell::ShowMessageBox(
			std::string_view(messageBuffer, messageLength), "PeepoDrumKit Ver.B - Syntax Error",
			Shell::MessageBoxButtons::AbortRetryIgnore, Shell::MessageBoxIcon::Warning, nullptr);

		if (result == Shell::MessageBoxResult::Abort) return LoadSettingsResponse::ErrorAbort;
		if (result == Shell::MessageBoxResult::Retry) return LoadSettingsResponse::ErrorRetry;
		if (result == Shell::MessageBoxResult::Ignore) return LoadSettingsResponse::ErrorIgnore;
		assert(!"Unreachable"); return LoadSettingsResponse::ErrorAbort;
	}

	int EntryPoint()
	{
		Log::Write("EntryPoint begin");
		auto [argc, argv] = CommandLine::GetCommandLineUTF8();
		for (size_t i = 1; i < argc; i++)
		{
			if (argv[i] == "--test-editor-caches")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!RunEditorCacheSelfTest(error)) { Log::Write("Editor cache self-test failed: %s", error.c_str()); return 1; }
				Log::Write("Editor cache self-test passed");
				return 0;
			}
			if (argv[i] == "--test-window-layouts")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!RunWindowLayoutSelfTest(error))
				{
					std::fprintf(stderr, "Window layout self-test failed: %s\n", error.c_str());
					return 1;
				}
				std::puts("Editing and test play window layout self-test passed");
				return 0;
			}
			if (argv[i] == "--test-d3d11-recovery")
				return ApplicationHost::RunD3D11DeviceRecoverySelfTest() ? 0 : 1;
			if (argv[i] == "--test-window-lifecycle")
			{
				static std::string error;
				if (!ApplicationHost::RunD3D11DeviceRecoverySelfTest([] { return ApplicationHost::RunWindowLifecycleSelfTest(error); }))
				{
					std::fprintf(stderr, "Window lifecycle self-test failed: %s\n", error.c_str()); return 1;
				}
				std::puts("Minimized, restored, and closing window lifecycle self-test passed");
				return 0;
			}
			if (argv[i] == "--test-settings-navigation")
			{
				i18n::InitBuiltinLocale();
				static std::string error;
				if (!ApplicationHost::RunD3D11DeviceRecoverySelfTest([] { return ChartSettingsWindow::RunNavigationSelfTest(error); }))
				{
					std::fprintf(stderr, "Settings navigation self-test failed: %s\n", error.c_str()); return 1;
				}
				std::puts("Settings section navigation and hover self-test passed");
				return 0;
			}
			if (argv[i] == "--test-video-async-writer")
			{
				std::string error;
				if (!RunVideoAsyncWriterSelfTest(error)) { std::fprintf(stderr, "Asynchronous video writer self-test failed: %s\n", error.c_str()); return 1; }
				std::puts("Asynchronous video writer self-test passed");
				return 0;
			}
			if (argv[i] == "--test-video-export-pipeline")
			{
				i18n::InitBuiltinLocale();
				static std::string error;
				if (!ApplicationHost::RunD3D11DeviceRecoverySelfTest([] { return ChartEditor::RunVideoExportSelfTest(error); }))
				{
					std::fprintf(stderr, "Video export pipeline self-test failed: %s\n", error.c_str()); return 1;
				}
				std::puts("Video export pipeline and editing lock self-test passed");
				return 0;
			}
			if (argv[i] == "--test-aac-capabilities")
			{
				if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
				defer { CoUninitialize(); };
				if (FAILED(MFStartup(MF_VERSION))) return 1;
				defer { MFShutdown(); };
				MFT_REGISTER_TYPE_INFO outputFilter { MFMediaType_Audio, MFAudioFormat_AAC };
				IMFActivate** encoders = nullptr;
				UINT32 count = 0;
				if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
					nullptr, &outputFilter, &encoders, &count))) return 1;
				defer { for (UINT32 index = 0; index < count; ++index) encoders[index]->Release(); CoTaskMemFree(encoders); };
				if (count == 0) return 1;
				Microsoft::WRL::ComPtr<IMFTransform> encoder;
				if (FAILED(encoders[0]->ActivateObject(IID_PPV_ARGS(encoder.GetAddressOf())))) return 1;
				for (DWORD index = 0;; ++index)
				{
					Microsoft::WRL::ComPtr<IMFMediaType> type;
					const HRESULT result = encoder->GetOutputAvailableType(0, index, type.GetAddressOf());
					if (result == MF_E_NO_MORE_TYPES) break;
					if (FAILED(result)) return 1;
					UINT32 sampleRate = 0, channels = 0, bytesPerSecond = 0;
					if (FAILED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)) ||
						FAILED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
						FAILED(type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond))) return 1;
					if (channels == 2 && (sampleRate == 44100 || sampleRate == 48000))
						Log::Write("AAC supported stereo format: %u Hz, %u kbps", sampleRate, bytesPerSecond * 8 / 1000);
				}
				for (UINT32 bitRate : { 256000u, 320000u })
				{
					Microsoft::WRL::ComPtr<IMFMediaType> type;
					if (FAILED(MFCreateMediaType(type.GetAddressOf())) ||
						FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
						FAILED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitRate / 8))) return 1;
					const HRESULT result = encoder->SetOutputType(0, type.Get(), MFT_SET_TYPE_TEST_ONLY);
					Log::Write("AAC stereo %u kbps probe: HRESULT 0x%08X", bitRate / 1000, static_cast<u32>(result));
				}
				return 0;
			}
			if (argv[i] == "--test-video-audio")
			{
				std::string error;
				if (!RunVideoAudioSelfTest(error))
				{
					std::fprintf(stderr, "Video audio self-test failed: %s\n", error.c_str());
					return 1;
				}
				std::puts("Video audio self-test passed");
				return 0;
			}
			if (argv[i] == "--test-video-branches")
			{
				std::string error;
				if (!RunVideoBranchSelfTest(error))
				{
					Log::Write("Video branch self-test failed: %s", error.c_str());
					return 1;
				}
				Log::Write("Video branch self-test passed");
				return 0;
			}
			if (argv[i] == "--test-video-layout")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!RunVideoLayoutSelfTest(error))
				{
					Log::Write("Video layout self-test failed: %s", error.c_str());
					std::fprintf(stderr, "Video layout self-test failed: %s\n", error.c_str());
					return 1;
				}
				Log::Write("Video layout self-test passed");
				std::puts("Video and preview layout self-test passed");
				return 0;
			}
			if (argv[i] == "--test-screenshot-png")
			{
				const std::vector<u8> pixels = { 0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255 };
				std::string firstPath, secondPath;
				if (!SaveScreenshotPNG(2, 2, pixels, firstPath) || !SaveScreenshotPNG(2, 2, pixels, secondPath) || firstPath == secondPath)
				{
					Log::Write("Screenshot PNG self-test failed");
					return 1;
				}
				Log::Write("Screenshot PNG self-test passed: %s, %s", firstPath.c_str(), secondPath.c_str());
				return 0;
			}
			if (argv[i] == "--test-video-writer")
			{
				Audio::PCMSampleBuffer hitSound {};
				hitSound.ChannelCount = 1;
				hitSound.SampleRate = Audio::Engine.OutputSampleRate;
				hitSound.FrameCount = 2;
				hitSound.InterleavedSamples = std::make_unique<i16[]>(2);
				hitSound.InterleavedSamples[0] = 1000;
				hitSound.InterleavedSamples[1] = 500;
				VideoExportAudioSources testSources;
				testSources.SoundEffects[EnumToIndex(SoundEffectType::TaikoDon)] = &hitSound;
				VideoExportSoundTimeline testSounds;
				testSounds.Events.push_back({ Time::FromSec(2.0 / Audio::Engine.OutputSampleRate), SoundEffectType::TaikoDon });
				std::vector<i16> mixedSamples;
				RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
				if (mixedSamples.size() != 10 || mixedSamples[2 * 2] != 1000 || mixedSamples[2 * 2 + 1] != 1000 ||
					mixedSamples[3 * 2] != 500 || mixedSamples[0] != 0 || mixedSamples[4 * 2] != 0)
				{
					Log::Write("Video writer self-test audio mixing failed");
					return 1;
				}
				for (f32 pan : { -1.0f, 1.0f })
				{
					testSounds.Events[0].Pan = pan;
					RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
					const auto gain = Audio::GetPanGain(pan, Audio::AudioEngine::PanLaw);
					for (u32 channel = 0; channel < 2; ++channel)
						if (mixedSamples[4 + channel] != static_cast<i16>(std::round(1000.0f * gain[channel])))
						{
							Log::Write("Video writer self-test stereo balance failed");
							return 1;
						}
				}
				testSounds.Events[0].Pan = -1.0f;
				testSounds.Events.push_back({ testSounds.Events[0].HitTime, SoundEffectType::TaikoDon, 1.0f });
				RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
				if (mixedSamples[4] != 1414 || mixedSamples[5] != 1414)
				{
					Log::Write("Video writer self-test simultaneous stereo hits failed");
					return 1;
				}
				VideoExportAudioSources fadeSources;
				fadeSources.Song = &hitSound;
				fadeSources.ContentStart = Time::FromSec(1.0);
				fadeSources.ContentEnd = Time::FromSec(2.0);
				fadeSources.FadeInSeconds = fadeSources.FadeOutSeconds = 1.0f;
				const f64 fadeTimes[] = { 0.0, 0.45, 0.9, 1.5, 2.2, 2.6, 3.0 };
				const i32 expectedLevels[] = { 0, 750, 1000, 1000, 1000, 750, 0 };
				for (size_t sample = 0; sample < ArrayCount(fadeTimes); ++sample)
				{
					fadeSources.SongOffset = Time::FromSec(fadeTimes[sample]);
					RenderVideoExportAudio(fadeSources, {}, fadeSources.SongOffset, 0, 1, mixedSamples);
					if (std::abs(static_cast<i32>(mixedSamples[0]) - expectedLevels[sample]) > 1 || mixedSamples[0] != mixedSamples[1])
					{
						Log::Write("Video writer self-test audio fade failed at %g seconds", fadeTimes[sample]);
						return 1;
					}
				}
				fadeSources.FadeInSeconds = fadeSources.FadeOutSeconds = 0.0f;
				RenderVideoExportAudio(fadeSources, {}, fadeSources.SongOffset, 0, 1, mixedSamples);
				if (mixedSamples[0] != 1000 || mixedSamples[1] != 1000) return 1;
				std::vector<u8> pixels(640 * 360 * 4, 0);
				std::vector<i16> samples(1470 * 2, 0);
				for (const auto& preset : VideoAudioBitRatePresets)
				{
					PersistentAppData settings, restored;
					settings.VideoExport.AudioBitRate = preset.BitRate;
					std::string ini;
					SettingsToIni(settings, ini);
					if (ParseSettingsIni(ini, restored).HasError || restored.VideoExport.AudioBitRate != preset.BitRate) return 1;
					VideoExportWriter writer;
					const std::string outputPath = "build/video-writer-self-test-" + std::to_string(preset.BitRate / 1000) + ".mp4";
					const b8 stressTest = preset.BitRate == VideoAudioBitRatePresets[0].BitRate;
					const u32 width = stressTest ? 1920 : 640, height = stressTest ? 1080 : 360;
					const i32 fps = stressTest ? 60 : 30, frameCount = stressTest ? 180 : 30;
					const i32 audioFrames = 44100 / fps;
					pixels.resize(static_cast<size_t>(width) * height * 4);
					if (!writer.Start(outputPath, width, height, fps, stressTest ? 12000000 : 2000000, preset.BitRate))
					{
						Log::Write("Video writer self-test setup failed at %u bps: %s", preset.BitRate, writer.GetError().data());
						return 1;
					}
					for (i32 frame = 0; frame < frameCount; ++frame)
					{
						auto waitStopwatch = CPUStopwatch::StartNew();
						VideoExportWriteStatus status;
						while ((status = writer.GetWriteStatus()) == VideoExportWriteStatus::Pending && waitStopwatch.GetElapsed().ToSec() < 15.0)
							::Sleep(1);
						if (status != VideoExportWriteStatus::Ready)
						{
							Log::Write("Video writer self-test queue failed or stalled at frame %d: %s", frame, writer.GetError().data());
							return 1;
						}
						for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
						{
							pixels[pixel + 0] = static_cast<u8>(frame * 8);
							pixels[pixel + 1] = 64;
							pixels[pixel + 2] = 128;
							pixels[pixel + 3] = 255;
						}
						for (i32 sample = 0; sample < audioFrames; ++sample)
						{
							const i16 value = static_cast<i16>(10000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * (frame * audioFrames + sample) / 44100.0));
							samples[sample * 2] = samples[sample * 2 + 1] = value;
						}
						if (!writer.WriteVideoFrame(pixels.data(), width * 4, frame) ||
							!writer.WriteAudioSamples(samples.data(), audioFrames, frame * audioFrames))
						{
							Log::Write("Video writer self-test frame failed: %s", writer.GetError().data());
							return 1;
						}
					}
					if (!writer.Finish())
					{
						Log::Write("Video writer self-test finalize failed: %s", writer.GetError().data());
						return 1;
					}
					if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
					defer { CoUninitialize(); };
					if (FAILED(MFStartup(MF_VERSION))) return 1;
					defer { MFShutdown(); };
					Microsoft::WRL::ComPtr<IMFSourceReader> reader;
					Microsoft::WRL::ComPtr<IMFMediaType> audioType;
					if (FAILED(MFCreateSourceReaderFromURL(UTF8::WideArg(outputPath).c_str(), nullptr, reader.GetAddressOf())) ||
						FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, audioType.GetAddressOf()))) return 1;
					UINT32 bytesPerSecond = 0, sampleRate = 0, channels = 0;
					GUID subtype = {};
					if (FAILED(audioType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond)) || bytesPerSecond * 8 != preset.BitRate ||
						FAILED(audioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)) || sampleRate != 44100 ||
						FAILED(audioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) || channels != 2 ||
						FAILED(audioType->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFAudioFormat_AAC)
					{
						Log::Write("Video writer self-test audio format mismatch: requested %u, got %u bps", preset.BitRate, bytesPerSecond * 8);
						return 1;
					}
					Log::Write("Video writer self-test verified %u kbps AAC stereo at 44100 Hz", preset.BitRate / 1000);
				}
				PersistentAppData legacySettings;
				if (ParseSettingsIni("[video_export]\nframes_per_second = 60\n", legacySettings).HasError || legacySettings.VideoExport.AudioBitRate != 192000) return 1;
				VideoExportWriter invalidWriter;
				if (invalidWriter.Start("build/video-writer-invalid.mp4", 640, 360, 30, 2000000, 0) || invalidWriter.GetError().empty()) return 1;
				Log::Write("Video writer self-test passed");
				return 0;
			}
			if (argv[i] == "--test-tja-branches")
			{
				std::string error;
				if (RunTJAChartBranchSelfTest(error))
				{
					Log::Write("TJA branch self-test passed");
					return 0;
				}
				Log::Write("TJA branch self-test failed: %s", error.c_str());
				return 1;
			}
			if (argv[i] == "--test-timeline-delay")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!ChartTimeline::RunPlaybackTimelineSelfTest(error))
				{
					Log::Write("Timeline DELAY self-test failed: %s", error.c_str());
					return 1;
				}
				Log::Write("Timeline DELAY self-test passed");
				return 0;
			}
		}

		while (true)
		{
			const auto response = ReadParseSettingsIniFile<PersistentAppData>(PersistentAppIniFileName, PersistentApp);
			if (response == LoadSettingsResponse::FileNotFound) { break; }
			if (response == LoadSettingsResponse::AllGood) { break; }
			if (response == LoadSettingsResponse::ErrorAbort) { return -1; }
			if (response == LoadSettingsResponse::ErrorRetry) { continue; }
			if (response == LoadSettingsResponse::ErrorIgnore) { break; }
			assert(!"Unreachable"); break;
		}

		while (true)
		{
			// TODO: Also set IsDirty in case of (older) version mismatch (?)
			const auto response = ReadParseSettingsIniFile<UserSettingsData>(SettingsIniFileName, Settings_Mutable);
			if (response == LoadSettingsResponse::FileNotFound) { Settings_Mutable.IsDirty = true; break; }
			if (response == LoadSettingsResponse::AllGood) { break; }
			if (response == LoadSettingsResponse::ErrorAbort) { return -1; }
			if (response == LoadSettingsResponse::ErrorRetry) { continue; }
			if (response == LoadSettingsResponse::ErrorIgnore) { break; }
			assert(!"Unreachable"); break;
		}

		if (!Settings.General.FolderDropSearchDepth.HasValue)
			Settings_Mutable.IsDirty = true;

		static std::unique_ptr<ImGuiApplication> app;
		ApplicationHost::StartupParam startupParam = {};
		ApplicationHost::UserCallbacks callbacks = {};

		GuiScaleFactorTarget = ClampRoundGuiScaleFactor(PersistentApp.LastSession.GuiScale);
		SelectedGuiLanguage = PersistentApp.LastSession.GuiLanguage;
		SelectedGuiLanguageTJA = ASCII::IETFLangTagToTJALangTag(SelectedGuiLanguage);

		ApplicationHost::GlobalState.SwapInterval = PersistentApp.LastSession.OSWindow_SwapInterval;
		ApplicationHost::GlobalState.VSyncOffFPSLimit = Max(0, *Settings.General.VSyncOffFPSLimit);
		startupParam.WaitableSwapChain = false;
		startupParam.AllowSwapChainTearing = true;
		startupParam.WindowTitle = PeepoDrumKitApplicationTitle;
		// TODO: ...
		// startupParam.WindowPosition = ...;
		// startupParam.WindowSize = ...;

		callbacks.OnStartup = []
		{
			Log::Write("User startup begin");
			i18n::RefreshLocales();
			i18n::InitBuiltinLocale();
			i18n::ReloadLocaleFile(SelectedGuiLanguage.c_str());
			Audio::Engine.ApplicationStartup();
			app = std::make_unique<ImGuiApplication>();
			Log::Write("User startup complete");
		};
		callbacks.OnBeforeUpdate = []
		{
			app->OnUpdate();
		};
		callbacks.OnUpdate = []
		{
			ApplicationHost::GlobalState.VSyncOffFPSLimit = Max(0, *Settings.General.VSyncOffFPSLimit);
			app->OnUpdate();
		};
		callbacks.OnAfterRender = [] { app->ChartEditor.OnAfterRender(); };
		callbacks.OnShutdown = []
		{
			Log::Write("User shutdown begin");
			app = nullptr;
			Audio::Engine.ApplicationShutdown();

			PersistentApp.LastSession.GuiScale = GuiScaleFactorTarget;
			PersistentApp.LastSession.GuiLanguage = SelectedGuiLanguage;
			PersistentApp.LastSession.OSWindow_SwapInterval = ApplicationHost::GlobalState.SwapInterval;
			PersistentApp.LastSession.OSWindow_Region = Rect::FromTLSize(vec2(ApplicationHost::GlobalState.WindowPosition), vec2(ApplicationHost::GlobalState.WindowSize));
			// TODO: PersistentApp.LastSession.OSWindow_RegionRestore = ...;
			PersistentApp.LastSession.OSWindow_IsFullscreen = ApplicationHost::GlobalState.IsBorderlessFullscreen;
			// TODO: PersistentApp.LastSession.OSWindow_IsMaximized = ...;

			std::string iniFileContent; iniFileContent.reserve(4096);
			SettingsToIni(PersistentApp, iniFileContent); File::WriteAllBytes(PersistentAppIniFileName, iniFileContent);

			if (Settings.IsDirty)
			{
				iniFileContent.clear();
				SettingsToIni(Settings, iniFileContent); File::WriteAllBytes(SettingsIniFileName, iniFileContent);
				Settings_Mutable.IsDirty = false;
			}
			Log::Write("User shutdown complete");
		};
		callbacks.OnWindowCloseRequest = []
		{
			return app->OnWindowCloseRequest();
		};

		return ApplicationHost::EnterProgramLoop(startupParam, callbacks);
	}
}

#if 1
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
static void Win32SetupConsoleMagic()
{
	::SetConsoleOutputCP(CP_UTF8);
	::_setmode(::_fileno(stdout), _O_BINARY);
	// TODO: Maybe overwrite the current locale too (?)
}
#else
static void Win32SetupConsoleMagic() { return; }
#endif

#if PEEPO_DEBUG
int main(int, const char**) { Win32SetupConsoleMagic(); Log::Initialize(); Log::InstallCrashHandler(); const int result = PeepoDrumKit::EntryPoint(); Log::Shutdown(); return result; }
#elif PEEPO_RELEASE

#if 1
#include <Windows.h>
int WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { Win32SetupConsoleMagic(); Log::Initialize(); Log::InstallCrashHandler(); const int result = PeepoDrumKit::EntryPoint(); Log::Shutdown(); return result; }
#else
int WinMain(void*, void*, const wchar_t*, int) { Win32SetupConsoleMagic(); return PeepoDrumKit::EntryPoint(); }
#endif

#else
#error "Neither PEEPO_DEBUG nor PEEPO_RELEASE are defined :monkaS:"
#endif
