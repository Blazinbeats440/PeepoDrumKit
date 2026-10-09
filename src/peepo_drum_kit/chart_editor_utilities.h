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

	struct BarLineGimmickUtility
	{
		i32 Rate = 120, LastGeneratedCount = 0;
		b8 ApplyScroll = false, InterpolateScroll = false, HideNoteBarLines = false;
		f32 ScrollStart = 1.0f, ScrollEnd = 1.0f;
		void DrawGui(ChartContext& context);
	};

	struct ChartUtilitiesWindow
	{
		i32 SelectedFunction = 0;
		JPOSMotionUtility JPOSMotionGenerator;
		BarLineGimmickUtility BarLineGimmickGenerator;
		void DrawGui(ChartContext& context, const ChartTimeline& timeline);
	};
}
