#include "chart_editor_utilities.h"
#include "chart_editor_context.h"
#include "chart_editor_timeline.h"
#include "chart_editor_undo.h"
#include "imgui/imgui_include.h"
#include <cstdio>

namespace PeepoDrumKit
{
	static void DrawJPOSEasingSettings(cstr label, JPOSEasing& easing, f32& strength)
	{
		Gui::PushID(label);
		const cstr easingLabels[] = { UI_Str("INTERPOLATION_EASING_LINEAR"), UI_Str("INTERPOLATION_EASING_EASE_IN"),
			UI_Str("INTERPOLATION_EASING_EASE_OUT"), UI_Str("UTILITY_JPOS_EASE_IN_OUT"), UI_Str("UTILITY_JPOS_EASE_OUT_IN") };
		if (Gui::BeginCombo(label, easingLabels[static_cast<i32>(easing)]))
		{
			for (i32 index = 0; index < ArrayCountI32(easingLabels); ++index)
				if (Gui::Selectable(easingLabels[index], index == static_cast<i32>(easing))) easing = static_cast<JPOSEasing>(index);
			Gui::EndCombo();
		}
		Gui::BeginDisabled(easing == JPOSEasing::Linear);
		Gui::SliderFloat(UI_Str("INTERPOLATION_EASING_STRENGTH"), &strength, 0.0f, static_cast<f32>(MaxJPOSEasingStrength), "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Gui::EndDisabled();
		Gui::PopID();
	}

	static void DrawJPOSMotionPreview(ChartContext& context, const JPOSEasingResult& generated, f64 startSeconds, f64 endSeconds, const JPOSMotionSettings& motion)
	{
		std::vector<ImVec2> positions = { { 0.0f, 0.0f } };
		std::vector<f64> times;
		positions.reserve(generated.Segments.size() + 1);
		times.reserve(generated.Segments.size());
		f32 minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
		for (const JPOSEasingSegment& segment : generated.Segments)
		{
			const ImVec2 previous = positions.back();
			const ImVec2 next = { previous.x + segment.Move.real(), previous.y + segment.Move.imag() };
			positions.push_back(next);
			times.push_back(context.BeatToTime(Beat::FromTicks(segment.BeatTicks)).ToSec());
			minX = Min(minX, next.x); maxX = Max(maxX, next.x);
			minY = Min(minY, next.y); maxY = Max(maxY, next.y);
		}
		const i32 sampleCount = Min(4097, Max(65, static_cast<i32>(generated.Segments.size()) * 4 + 1));
		std::vector<f32> x(sampleCount), y(sampleCount);
		size_t segmentIndex = 0;
		for (i32 sample = 0; sample < sampleCount; ++sample)
		{
			const f64 time = startSeconds + (endSeconds - startSeconds) * sample / (sampleCount - 1);
			while (segmentIndex < generated.Segments.size() && time >= times[segmentIndex] + generated.Segments[segmentIndex].DurationSeconds)
				++segmentIndex;
			ImVec2 position = positions[segmentIndex];
			if (segmentIndex < generated.Segments.size())
			{
				const f64 progress = std::clamp((time - times[segmentIndex]) / generated.Segments[segmentIndex].DurationSeconds, 0.0, 1.0);
				position.x += static_cast<f32>(generated.Segments[segmentIndex].Move.real() * progress);
				position.y += static_cast<f32>(generated.Segments[segmentIndex].Move.imag() * progress);
			}
			x[sample] = position.x; y[sample] = position.y;
		}
		Gui::PlotLines(UI_Str("UTILITY_JPOS_POSITION_X"), x.data(), sampleCount, 0, nullptr, minX - 1.0f, maxX + 1.0f, { 0.0f, 75.0f });
		Gui::PlotLines(UI_Str("UTILITY_JPOS_POSITION_Y"), y.data(), sampleCount, 0, nullptr, minY - 1.0f, maxY + 1.0f, { 0.0f, 75.0f });

		Gui::TextUnformatted(UI_Str("UTILITY_JPOS_TRAJECTORY"));
		const ImVec2 origin = Gui::GetCursorScreenPos();
		const ImVec2 size = { Max(100.0f, Gui::GetContentRegionAvail().x), 100.0f };
		Gui::InvisibleButton("JPOSTrajectory", size);
		ImDrawList* drawList = Gui::GetWindowDrawList();
		drawList->AddRectFilled(origin, { origin.x + size.x, origin.y + size.y }, Gui::GetColorU32(ImGuiCol_FrameBg));
		const f32 scale = Min((size.x - 20.0f) / Max(1.0f, maxX - minX), (size.y - 20.0f) / Max(1.0f, maxY - minY));
		for (ImVec2& position : positions)
			position = { origin.x + size.x * 0.5f + (position.x - (minX + maxX) * 0.5f) * scale,
				origin.y + size.y * 0.5f + (position.y - (minY + maxY) * 0.5f) * scale };
		drawList->AddPolyline(positions.data(), static_cast<i32>(positions.size()), Gui::GetColorU32(ImGuiCol_PlotLines), 0, 2.0f);
		drawList->AddCircleFilled(positions.front(), 4.0f, Gui::GetColorU32(ImGuiCol_Text));
		if (motion.Type == JPOSMotionType::Waypoints)
		{
			for (size_t index = 0; index < motion.Waypoints.size(); ++index)
			{
				const auto point = motion.Waypoints[index].Position;
				const ImVec2 screen = { origin.x + size.x * 0.5f + (point.real() - (minX + maxX) * 0.5f) * scale,
					origin.y + size.y * 0.5f + (point.imag() - (minY + maxY) * 0.5f) * scale };
				char number[16]; std::snprintf(number, sizeof(number), "%d", static_cast<i32>(index));
				drawList->AddCircleFilled(screen, 3.0f, Gui::GetColorU32(ImGuiCol_Text));
				drawList->AddText({ screen.x + 4.0f, screen.y + 2.0f }, Gui::GetColorU32(ImGuiCol_Text), number);
			}
		}
	}

	static void DrawJPOSWaypoints(JPOSMotionSettings& motion)
	{
		Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_WAYPOINT_DESC"));
		Gui::Checkbox(UI_Str("UTILITY_JPOS_WAYPOINT_SAME_EASING"), &motion.WaypointSameEasing);
		i32 removeIndex = -1, insertIndex = -1;
		if (Gui::BeginTable("JPOSWaypoints", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
		{
			Gui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 25.0f);
			Gui::TableSetupColumn(UI_Str("UTILITY_JPOS_ARRIVAL_PERCENT"));
			Gui::TableSetupColumn("X"); Gui::TableSetupColumn("Y");
			Gui::TableSetupColumn(UI_Str("UTILITY_JPOS_WAYPOINT_ACTIONS"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
			Gui::TableHeadersRow();
			for (i32 index = 0; index < static_cast<i32>(motion.Waypoints.size()); ++index)
			{
				auto& point = motion.Waypoints[index];
				const b8 first = index == 0, last = index + 1 == static_cast<i32>(motion.Waypoints.size());
				Gui::PushID(index); Gui::TableNextRow(); Gui::TableNextColumn(); Gui::Text("%d", index);
				Gui::TableNextColumn(); Gui::SetNextItemWidth(-1.0f); Gui::BeginDisabled(first || last);
				Gui::InputFloat("##Arrival", &point.ArrivalPercent, 0.0f, 0.0f, "%g"); Gui::EndDisabled();
				f32 x = point.Position.real(), y = point.Position.imag();
				Gui::TableNextColumn(); Gui::SetNextItemWidth(-1.0f); Gui::BeginDisabled(first); Gui::InputFloat("##X", &x, 0.0f, 0.0f, "%g"); Gui::EndDisabled();
				Gui::TableNextColumn(); Gui::SetNextItemWidth(-1.0f); Gui::BeginDisabled(first); Gui::InputFloat("##Y", &y, 0.0f, 0.0f, "%g"); Gui::EndDisabled();
				point.Position = { x, y };
				Gui::TableNextColumn(); Gui::BeginDisabled(last || motion.Waypoints.size() >= MaxJPOSEasingGrid + 1);
				if (Gui::SmallButton("+")) insertIndex = index; Gui::EndDisabled();
				Gui::SameLine(); Gui::BeginDisabled(first || last);
				if (Gui::SmallButton("-")) removeIndex = index; Gui::EndDisabled();
				Gui::PopID();
			}
			Gui::EndTable();
		}
		if (removeIndex >= 0) motion.Waypoints.erase(motion.Waypoints.begin() + removeIndex);
		else if (insertIndex >= 0)
		{
			const auto& before = motion.Waypoints[insertIndex]; const auto& after = motion.Waypoints[insertIndex + 1];
			JPOSWaypoint point;
			point.ArrivalPercent = (before.ArrivalPercent + after.ArrivalPercent) * 0.5f;
			point.Position = before.Position * 0.5f + after.Position * 0.5f;
			point.Easing = after.Easing; point.Strength = after.Strength;
			motion.Waypoints.insert(motion.Waypoints.begin() + insertIndex + 1, point);
		}
		if (!motion.WaypointSameEasing)
		{
			for (i32 index = 1; index < static_cast<i32>(motion.Waypoints.size()); ++index)
			{
				Gui::PushID(index); Gui::Text(UI_Str("UTILITY_JPOS_WAYPOINT_INTERVAL"), index - 1, index);
				DrawJPOSEasingSettings(UI_Str("UTILITY_JPOS_EASING_TYPE"), motion.Waypoints[index].Easing, motion.Waypoints[index].Strength);
				Gui::PopID();
			}
		}
	}

	void ChartUtilitiesWindow::DrawGui(ChartContext& context, const ChartTimeline& timeline)
	{
		if (Gui::BeginCombo(UI_Str("UTILITY_FUNCTION"), UI_Str("UTILITY_JPOS_MOTION")))
		{
			Gui::Selectable(UI_Str("UTILITY_JPOS_MOTION"), true);
			Gui::EndCombo();
		}
		Gui::Separator();
		JPOSMotionGenerator.DrawGui(context, timeline);
	}

	void JPOSMotionUtility::DrawGui(ChartContext& context, const ChartTimeline& timeline)
	{
		Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_MOTION_DESC"));
		const cstr types[] = { UI_Str("UTILITY_JPOS_ONE_WAY"), UI_Str("UTILITY_JPOS_ROUND_TRIP"), UI_Str("UTILITY_JPOS_SHAKE"),
			UI_Str("UTILITY_JPOS_ELLIPSE"), UI_Str("UTILITY_JPOS_BOUNCE"), UI_Str("UTILITY_JPOS_ZIGZAG"), UI_Str("UTILITY_JPOS_POLYGON"), UI_Str("UTILITY_JPOS_WAYPOINTS") };
		if (Gui::BeginCombo(UI_Str("UTILITY_JPOS_MOTION_TYPE"), types[static_cast<i32>(Motion.Type)]))
		{
			for (i32 index = 0; index < ArrayCountI32(types); ++index)
				if (Gui::Selectable(types[index], index == static_cast<i32>(Motion.Type))) Motion.Type = static_cast<JPOSMotionType>(index);
			Gui::EndCombo();
		}
		const b8 roundTrip = Motion.Type == JPOSMotionType::RoundTrip;
		const b8 shake = Motion.Type == JPOSMotionType::Shake, ellipse = Motion.Type == JPOSMotionType::Ellipse, bounce = Motion.Type == JPOSMotionType::Bounce;
		const b8 zigzag = Motion.Type == JPOSMotionType::Zigzag, polygon = Motion.Type == JPOSMotionType::Polygon, waypoints = Motion.Type == JPOSMotionType::Waypoints;
		const b8 curved = shake || ellipse || bounce || zigzag || polygon;
		if (!ellipse && !polygon && !waypoints)
		{
			Gui::InputFloat(roundTrip ? UI_Str("UTILITY_JPOS_TURN_X") : shake ? UI_Str("UTILITY_JPOS_AMPLITUDE_X") : UI_Str("UTILITY_JPOS_MOVE_X"), &MoveX, 1.0f, 10.0f, "%g");
			Gui::InputFloat(roundTrip ? UI_Str("UTILITY_JPOS_TURN_Y") : shake ? UI_Str("UTILITY_JPOS_AMPLITUDE_Y") : UI_Str("UTILITY_JPOS_MOVE_Y"), &MoveY, 1.0f, 10.0f, "%g");
		}
		if (curved)
		{
			Gui::InputInt(shake ? UI_Str("UTILITY_JPOS_SHAKE_COUNT") : ellipse || polygon ? UI_Str("UTILITY_JPOS_ORBIT_COUNT") : zigzag ? UI_Str("UTILITY_JPOS_ZIGZAG_COUNT") : UI_Str("UTILITY_JPOS_BOUNCE_COUNT"), &Motion.Repetitions);
			if (shake)
			{
				Gui::Checkbox(UI_Str("UTILITY_JPOS_DAMPED"), &Motion.Damped);
				if (Motion.Damped) Gui::SliderFloat(UI_Str("UTILITY_JPOS_DAMPING_STRENGTH"), &Motion.DampingStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}
			if (zigzag)
			{
				Gui::InputFloat(UI_Str("UTILITY_JPOS_LATERAL_AMPLITUDE"), &Motion.LateralAmplitude, 1.0f, 10.0f, "%g");
				const cstr paths[] = { UI_Str("UTILITY_JPOS_ANGULAR"), UI_Str("UTILITY_JPOS_SMOOTH") };
				if (Gui::BeginCombo(UI_Str("UTILITY_JPOS_PATH_SHAPE"), paths[Motion.SmoothZigzag ? 1 : 0]))
				{
					for (i32 index = 0; index < ArrayCountI32(paths); ++index)
						if (Gui::Selectable(paths[index], index == (Motion.SmoothZigzag ? 1 : 0))) Motion.SmoothZigzag = (index == 1);
					Gui::EndCombo();
				}
				const cstr sides[] = { UI_Str("UTILITY_JPOS_LEFT"), UI_Str("UTILITY_JPOS_RIGHT") };
				if (Gui::BeginCombo(UI_Str("UTILITY_JPOS_FIRST_SIDE"), sides[Motion.FirstSideLeft ? 0 : 1]))
				{
					for (i32 index = 0; index < ArrayCountI32(sides); ++index)
						if (Gui::Selectable(sides[index], index == (Motion.FirstSideLeft ? 0 : 1))) Motion.FirstSideLeft = (index == 0);
					Gui::EndCombo();
				}
				if (MoveX == 0.0f && MoveY == 0.0f) Gui::InputFloat(UI_Str("UTILITY_JPOS_REFERENCE_ANGLE"), &Motion.ReferenceAngle, 1.0f, 15.0f, "%g");
				Gui::Checkbox(UI_Str("UTILITY_JPOS_ZIGZAG_DAMPED"), &Motion.ZigzagDamped);
				if (Motion.ZigzagDamped) Gui::SliderFloat(UI_Str("UTILITY_JPOS_DAMPING_STRENGTH"), &Motion.ZigzagDampingStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_ZIGZAG_DESC"));
			}
			if (polygon)
			{
				const cstr shapes[] = { UI_Str("UTILITY_JPOS_RECTANGLE"), UI_Str("UTILITY_JPOS_TRIANGLE") };
				if (Gui::BeginCombo(UI_Str("UTILITY_JPOS_POLYGON_SHAPE"), shapes[static_cast<i32>(Motion.PolygonShape)]))
				{
					for (i32 index = 0; index < ArrayCountI32(shapes); ++index)
						if (Gui::Selectable(shapes[index], index == static_cast<i32>(Motion.PolygonShape))) Motion.PolygonShape = static_cast<JPOSPolygonShape>(index);
					Gui::EndCombo();
				}
				Gui::InputFloat(UI_Str("UTILITY_JPOS_WIDTH"), &Motion.Width, 1.0f, 10.0f, "%g");
				Gui::InputFloat(UI_Str("UTILITY_JPOS_HEIGHT"), &Motion.Height, 1.0f, 10.0f, "%g");
				Gui::InputFloat(UI_Str("UTILITY_JPOS_ROTATION_ANGLE"), &Motion.RotationAngle, 1.0f, 15.0f, "%g");
				Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_POLYGON_DESC"));
			}
			if (ellipse)
			{
				Gui::InputFloat(UI_Str("UTILITY_JPOS_RADIUS_X"), &Motion.RadiusX, 1.0f, 10.0f, "%g");
				Gui::InputFloat(UI_Str("UTILITY_JPOS_RADIUS_Y"), &Motion.RadiusY, 1.0f, 10.0f, "%g");
				Gui::InputFloat(UI_Str("UTILITY_JPOS_START_ANGLE"), &Motion.StartAngle, 1.0f, 15.0f, "%g");
				Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_ELLIPSE_DESC"));
			}
			if (ellipse || polygon)
			{
				const cstr directions[] = { UI_Str("UTILITY_JPOS_CLOCKWISE"), UI_Str("UTILITY_JPOS_COUNTERCLOCKWISE") };
				if (Gui::BeginCombo(UI_Str("UTILITY_JPOS_ROTATION_DIRECTION"), directions[Motion.Clockwise ? 0 : 1]))
				{
					for (i32 index = 0; index < ArrayCountI32(directions); ++index)
						if (Gui::Selectable(directions[index], index == (Motion.Clockwise ? 0 : 1))) Motion.Clockwise = (index == 0);
					Gui::EndCombo();
				}
				Gui::Checkbox(UI_Str("UTILITY_JPOS_EASING_PER_CYCLE"), &Motion.EasingPerCycle);
			}
			if (bounce)
			{
				Gui::InputFloat(UI_Str("UTILITY_JPOS_JUMP_HEIGHT"), &Motion.JumpHeight, 1.0f, 10.0f, "%g");
				Gui::Checkbox(UI_Str("UTILITY_JPOS_JUMP_DAMPED"), &Motion.JumpDamped);
				if (Motion.JumpDamped) Gui::SliderFloat(UI_Str("UTILITY_JPOS_DAMPING_STRENGTH"), &Motion.JumpDampingStrength, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			}
		}
		if (waypoints) DrawJPOSWaypoints(Motion);
		if (roundTrip)
		{
			Gui::InputInt(UI_Str("UTILITY_JPOS_ROUND_TRIPS"), &Motion.RoundTrips);
			f32 percent = Motion.OutboundRatio * 100.0f;
			if (Gui::SliderFloat(UI_Str("UTILITY_JPOS_OUTBOUND_RATIO"), &percent, 1.0f, 99.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp)) Motion.OutboundRatio = percent / 100.0f;
			Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_OUTBOUND_DESC"));
			Gui::Checkbox(UI_Str("UTILITY_JPOS_SAME_EASING"), &Motion.UseSameEasing);
		}
		if (!waypoints || Motion.WaypointSameEasing)
			DrawJPOSEasingSettings(roundTrip ? UI_Str("UTILITY_JPOS_OUTBOUND_EASING") : UI_Str("UTILITY_JPOS_EASING_TYPE"), Easing, EasingStrength);
		if (roundTrip && !Motion.UseSameEasing)
			DrawJPOSEasingSettings(UI_Str("UTILITY_JPOS_RETURN_EASING"), Motion.ReturnEasing, Motion.ReturnStrength);
		Gui::Text(UI_Str("UTILITY_JPOS_TIMELINE_GRID"), timeline.CurrentGridBarDivision);
		Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_TIMELINE_GRID_DESC"));

		ChartCourse* course = context.ChartSelectedCourse;
		const b8 hasRange = course != nullptr && context.RangeSelection.IsActiveAndHasEnd() && context.RangeSelection.GetDuration() > Beat::Zero();
		JPOSEasingResult generated;
		i32 conflicts = 0;
		if (hasRange)
		{
			const Beat start = context.RangeSelection.GetMin(), end = context.RangeSelection.GetMax();
			const Time startTime = context.BeatToTime(start), endTime = context.BeatToTime(end);
			Gui::Text(UI_Str("UTILITY_JPOS_RANGE"), start.BeatsFraction(), end.BeatsFraction(), (endTime - startTime).ToSec());
			const auto beatToSeconds = [&](int32_t ticks) { return context.BeatToTime(Beat::FromTicks(ticks)).ToSec(); };
			if (timeline.CurrentGridBarDivision > 0 && GetGridBeatSnap(timeline.CurrentGridBarDivision).Ticks > 0)
			{
				const auto nextGridTick = [&](int32_t ticks) { return timeline.CeilBeatToCurrentGrid(context, Beat::FromTicks(ticks + 1)).Ticks; };
				generated = GenerateJPOSMotionOnGrid(start.Ticks, end.Ticks, { MoveX, MoveY }, Easing, beatToSeconds, EasingStrength, Motion, nextGridTick);
			}
			else generated.Error = JPOSEasingError::InvalidGrid;
			const JPOSScrollChange* preceding = nullptr;
			for (const JPOSScrollChange& event : course->JPOSScrollChanges)
			{
				if (event.BeatTime < start) preceding = &event;
				else if (event.BeatTime < end) ++conflicts;
				else break;
			}
			if (preceding != nullptr && preceding->Duration > 0.0f &&
				context.BeatToTime(preceding->BeatTime) + Time::FromSec(preceding->Duration) > startTime)
				Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_INTERRUPTS_PREVIOUS"));
			if (conflicts > 0) Gui::TextWrapped(UI_Str("UTILITY_JPOS_CONFLICT"), conflicts);
			if (generated.Error == JPOSEasingError::None)
			{
				Gui::Text(UI_Str("UTILITY_JPOS_EVENT_COUNT"), static_cast<i32>(generated.Segments.size()));
				DrawJPOSMotionPreview(context, generated, startTime.ToSec(), endTime.ToSec(), Motion);
			}
			else
			{
				cstr error = UI_Str("UTILITY_JPOS_INVALID_TIME");
				if (generated.Error == JPOSEasingError::InvalidGrid) error = UI_Str("UTILITY_JPOS_INVALID_GRID");
				if (generated.Error == JPOSEasingError::InvalidMove) error = UI_Str("UTILITY_JPOS_INVALID_MOVE");
				if (generated.Error == JPOSEasingError::InvalidStrength) error = UI_Str("UTILITY_JPOS_INVALID_STRENGTH");
				if (generated.Error == JPOSEasingError::IntervalTooShort) error = UI_Str("UTILITY_JPOS_INTERVAL_TOO_SHORT");
				if (generated.Error == JPOSEasingError::InvalidMotion) error = UI_Str("UTILITY_JPOS_INVALID_MOTION");
				if (generated.Error == JPOSEasingError::InvalidMotionGrid) error = UI_Str("UTILITY_JPOS_INVALID_MOTION_GRID");
				if (generated.Error == JPOSEasingError::InvalidMotionTiming) error = UI_Str("UTILITY_JPOS_INVALID_MOTION_TIMING");
				if (generated.Error == JPOSEasingError::InvalidCurveMotion) error = UI_Str("UTILITY_JPOS_INVALID_CURVE_MOTION");
				if (generated.Error == JPOSEasingError::InvalidWaypoints) error = UI_Str("UTILITY_JPOS_INVALID_WAYPOINTS");
				if (waypoints && generated.ErrorInterval > 0) Gui::Text(UI_Str("UTILITY_JPOS_WAYPOINT_INTERVAL"), generated.ErrorInterval - 1, generated.ErrorInterval);
				Gui::TextWrapped("%s", error);
			}
		}
		else Gui::TextWrapped("%s", UI_Str("UTILITY_JPOS_SELECT_RANGE"));

		if (context.TestPlayActive) Gui::TextWrapped("%s", UI_Str("TEST_PLAY_EDITOR_LOCKED"));
		Gui::BeginDisabled(context.TestPlayActive || !hasRange || conflicts > 0 || generated.Error != JPOSEasingError::None);
		if (Gui::Button(UI_Str("UTILITY_JPOS_GENERATE")))
		{
			std::vector<JPOSScrollChange> events;
			events.reserve(generated.Segments.size());
			for (const JPOSEasingSegment& segment : generated.Segments)
				events.push_back({ Beat::FromTicks(segment.BeatTicks), Complex(segment.Move.real(), segment.Move.imag()), segment.DurationSeconds, false });
			LastGeneratedCount = static_cast<i32>(events.size());
			context.Undo.Execute<Commands::AddMultipleChartEvents<JPOSScrollChange>>(course, &course->JPOSScrollChanges, std::move(events));
		}
		Gui::EndDisabled();
		if (LastGeneratedCount > 0) Gui::TextWrapped(UI_Str("UTILITY_JPOS_GENERATED"), LastGeneratedCount);
	}
}
