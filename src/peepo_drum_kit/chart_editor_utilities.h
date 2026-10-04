#pragma once
#include "core_types.h"
#include "chart_editor_jpos_easing.h"

namespace PeepoDrumKit
{
	struct ChartContext;
	struct ChartTimeline;

	struct JPOSMotionUtility
	{
		f32 MoveX = 100.0f, MoveY = 0.0f;
		JPOSEasing Easing = JPOSEasing::EaseOut;
		f32 EasingStrength = 1.0f;
		JPOSMotionSettings Motion;
		i32 LastGeneratedCount = 0;
		void DrawGui(ChartContext& context, const ChartTimeline& timeline);
	};

	struct ChartUtilitiesWindow
	{
		JPOSMotionUtility JPOSMotionGenerator;
		void DrawGui(ChartContext& context, const ChartTimeline& timeline);
	};
}
