#pragma once

#include "chart_editor_i18n.h"
#include <algorithm>
#include <string>
#include <vector>

namespace PeepoDrumKit
{
	struct TestPlayJudgementDisplay
	{
		std::string Timing;
		const char* Label;
		u32 TimingColor;
	};

	inline TestPlayJudgementDisplay FormatTestPlayJudgement(i32 judgement, i32 timingError, b8 wasHit, i32 mode, i32 neutralWindow, b8 showMsUnit = true)
	{
		const char* label = judgement == 1 ? UI_Str("TEST_PLAY_GOOD") : judgement == 2 ? UI_Str("TEST_PLAY_OK") : UI_Str("TEST_PLAY_MISS");
		const u32 timingColor = mode == 0 || !wasHit || Absolute(timingError) <= neutralWindow ? 0xFFFFFFFF : timingError < 0 ? 0xFFFFB060 : 0xFF6060FF;
		if (!wasHit || mode == 0) return { "", label, timingColor };
		const char* direction = timingError < 0 ? UI_Str("TEST_PLAY_FAST") : UI_Str("TEST_PLAY_SLOW");
		const std::string signedMs = (timingError >= 0 ? "+" : "") + std::to_string(timingError) + (showMsUnit ? " ms" : "");
		const std::string timing = mode == 3 ? signedMs : timingError == 0 ? (mode == 2 ? signedMs : "")
			: mode == 1 ? direction : std::string(direction) + " " + signedMs;
		return { timing, mode == 3 ? nullptr : label, timingColor };
	}

	inline b8 ShouldShowPausedTestPlayJudgement(i32 judgement, i32 timingError, b8 wasHit, i32 filter, i32 thresholdMs)
	{
		return filter == 0 || (filter == 1 ? judgement >= 2 : !wasHit || Absolute(timingError) >= thresholdMs);
	}

	inline vec2 PlaceTestPlayJudgementLabel(std::vector<Rect>& occupied, vec2 center, f32 bottom, vec2 size, f32 gap, b8* wasPlaced = nullptr)
	{
		Rect bounds = Rect::FromTLSize(vec2(center.x - size.x * 0.5f, bottom - size.y), size);
		for (i32 step = 0; step < 4; step++)
		{
			const b8 overlaps = std::any_of(occupied.begin(), occupied.end(), [&](const Rect& other) { return bounds.Overlaps(other); });
			if (!overlaps) break;
			bounds.TL.y -= size.y + gap;
			bounds.BR.y -= size.y + gap;
		}
		const b8 placed = !std::any_of(occupied.begin(), occupied.end(), [&](const Rect& other) { return bounds.Overlaps(other); });
		if (wasPlaced) *wasPlaced = placed;
		if (!placed) return bounds.TL;
		occupied.push_back(bounds);
		return bounds.TL;
	}
}
