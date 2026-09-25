#pragma once
#include "core_types.h"
#include "core_string.h"
#include "chart_editor_context.h"
#include "chart_editor_settings.h"
#include "imgui/imgui_include.h"

namespace PeepoDrumKit
{
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

	struct ChartSettingsWindow
	{
		b8 DrawGui(ChartContext& context, UserSettingsData& settings);

	private:
		ImGuiTextFilter settingsFilterMain = {};
		ImGuiTextFilter settingsFilterInput = {};
		ChartSettingsWindowTempActiveWidgetGroup lastActiveGroup = {};
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
