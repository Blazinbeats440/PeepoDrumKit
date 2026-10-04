#pragma once
#include "core_types.h"
#include "chart_editor_jpos_easing.h"

namespace PeepoDrumKit
{
	struct ChartContext;

	struct JPOSMotionUtility
	{
		f32 MoveX = 100.0f, MoveY = 0.0f;
		i32 DivisionGrid = 16;
		JPOSEasing Easing = JPOSEasing::EaseOut;
		f32 EasingStrength = 1.0f;
		JPOSMotionSettings Motion;
		i32 LastGeneratedCount = 0;
		void DrawGui(ChartContext& context);
	};

	struct ChartUtilitiesWindow
	{
		JPOSMotionUtility JPOSMotionGenerator;
		void DrawGui(ChartContext& context);
	};
}
