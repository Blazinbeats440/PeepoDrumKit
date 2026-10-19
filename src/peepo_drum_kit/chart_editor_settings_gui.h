#pragma once
#include "core_types.h"
#include "core_string.h"
#include "chart_editor_context.h"
#include "chart_editor_settings.h"
#include "imgui/imgui_include.h"

namespace PeepoDrumKit
{
	enum class ChartSettingsCategory : u8 { General, File, Timeline, Editor, Audio, TestPlay, Appearance, Keys, Developer, About, Count };

	struct ChartSettingsWindowTempActiveWidgetGroup
	{
		void* ValuePtr;
		Rect GroupRect;
	};

	struct ChartSettingsWindowTempInputState
	{
		WithDefault<MultiInputBinding>* SelectedMultiBinding = nullptr;
		WithDefault<MultiInputBinding> SelectedMultiBindingOnOpenCopy = {};
		f32 MultiBindingPopupFadeCurrent = 0.0f;
		f32 MultiBindingPopupFadeTarget = 0.0f;
		InputBinding* TempAssignedBinding {};
		CPUStopwatch AssignedBindingStopwatch = {};
		std::array<std::string, 3> GridDivisionTexts = {};
	};

	struct ChartSettingsSectionNavigation
	{
		std::vector<std::string> Sections;
		std::string RequestedSection, SelectedSection;
		void RegisterSection(std::string_view section);
		void ScrollToSection(std::string_view section);
		void DrawHeading(std::string_view section);
	};

	struct ChartSettingsWindow
	{
		b8 DrawGui(ChartContext& context, UserSettingsData& settings);
		void SelectCategory(ChartSettingsCategory category) { selectedCategory = category; settingsFilterMain.Clear(); sectionNavigation = {}; }
		static b8 RunNavigationSelfTest(std::string& error);

	private:
		void SelectSection(std::string_view section);
		ChartSettingsCategory selectedCategory = ChartSettingsCategory::General;
		b8 showDeveloperSettings = false;
		ChartSettingsSectionNavigation sectionNavigation;
		ImGuiTextFilter settingsFilterMain = {};
		ImGuiTextFilter settingsFilterInput = {};
		ChartSettingsWindowTempActiveWidgetGroup lastActiveGroup = {};
		ChartSettingsWindowTempActiveWidgetGroup lastActiveThemeGroup = {};
		ChartSettingsWindowTempInputState inputState = {};
		b8 testPlayCalibrationActive = false;
		b8 testPlayCalibrationHasCandidate = false;
		f64 testPlayCalibrationFirstTick = 0.0;
		i32 testPlayCalibrationLastCue = -1;
		i32 testPlayCalibrationLastTap = -1;
		i32 testPlayCalibrationCandidate = 0;
		std::vector<i32> testPlayCalibrationErrors;
	};
}
