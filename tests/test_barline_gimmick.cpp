#include "peepo_drum_kit/chart_editor_barline_gimmick.h"
#include <iostream>
#include <limits>

using namespace PeepoDrumKit;

namespace Audio
{
	struct AudioEngine::Impl {};
	AudioEngine::AudioEngine() = default;
	AudioEngine::~AudioEngine() = default;
	b8 Voice::GetIsPlaying() const { return false; }
	Time Voice::GetPositionSmooth() const { return {}; }
	void Voice::SetPosition(Time) {}
}
namespace PeepoDrumKit
{
	namespace i18n { cstr HashToString(u32) { return "Test"; } }
	SettingsReflectionMap StaticallyInitializeAppSettingsReflectionMap() { return {}; }
	void ChartCourse::RecalculateSENotes(BranchType) {}
	void ChartCourse::RecalculateComboCounts(BranchType) {}
	struct ChartGraphicsResources::OpaqueData {};
	ChartGraphicsResources::ChartGraphicsResources() = default;
	ChartGraphicsResources::~ChartGraphicsResources() = default;
}

int main()
{
	int failures = 0, checks = 0;
	std::string scenario;
	const auto check = [&](bool condition, const char* name)
	{
		++checks;
		if (!condition) { std::cerr << "FAILED: " << name << "\n" << scenario << '\n'; ++failures; }
	};
	const auto close = [](double first, double second) { return std::abs(first - second) < 0.0000001; };
	const auto parse = [&](const std::string& body, ChartProject& chart, int bpm = 120)
	{
		TJA::ErrorList errors;
		const std::string text = "TITLE:Barlines\nBPM:" + std::to_string(bpm) + "\nCOURSE:Oni\n#START\n" + body + "\n#END\n";
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
		check(errors.Errors.empty(), "Fixture parses without errors");
		check(CreateChartProjectFromTJA(parsed, chart), "Fixture imports");
	};
	const auto roundTrip = [&](const ChartProject& chart, ChartProject& restored)
	{
		TJA::ParsedTJA output;
		check(ConvertChartProjectToTJA(chart, output, false), "Generated chart exports");
		std::string text;
		TJA::ConvertParsedToText(output, text, TJA::SaveFormat::Current);
		if (UTF8::HasBOM(text)) text.erase(0, sizeof(UTF8::BOM_UTF8));
		TJA::ErrorList errors;
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
		check(errors.Errors.empty(), "Generated TJA parses without unsupported denominators or mid-measure changes");
		check(CreateChartProjectFromTJA(parsed, restored), "Generated TJA imports");
	};
	const auto bars = [](const ChartCourse& course, Beat limit)
	{
		std::vector<Beat> result;
		course.TempoMap.ForEachBeatBar([&](const auto& bar)
		{
			if (bar.Beat > limit) return ControlFlow::Break;
			result.push_back(bar.Beat);
			return ControlFlow::Continue;
		});
		return result;
	};
	const auto sameSignatures = [](const ChartCourse& first, const ChartCourse& second)
	{
		if (first.TempoMap.Signature.size() != second.TempoMap.Signature.size()) return false;
		for (size_t index = 0; index < first.TempoMap.Signature.size(); ++index)
		{
			const auto& before = first.TempoMap.Signature[index]; const auto& after = second.TempoMap.Signature[index];
			if (before.Beat != after.Beat || before.Signature != after.Signature || before.IsSelected != after.IsSelected) return false;
		}
		return true;
	};
	const auto verifyUnchanged = [&](const ChartCourse& original, const ChartCourse& actual, bool allowScrollChanges = false, bool allowBarLineChanges = false)
	{
		check(actual.TempoMap.Tempo.size() == original.TempoMap.Tempo.size(), "BPM event count stays unchanged");
		for (size_t index = 0; index < std::min(actual.TempoMap.Tempo.size(), original.TempoMap.Tempo.size()); ++index)
			check(actual.TempoMap.Tempo[index].Beat == original.TempoMap.Tempo[index].Beat
				&& actual.TempoMap.Tempo[index].Tempo.BPM == original.TempoMap.Tempo[index].Tempo.BPM, "BPM positions and values stay unchanged");
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			const auto& beforeNotes = original.GetNotes(branch); const auto& afterNotes = actual.GetNotes(branch);
			check(beforeNotes.size() == afterNotes.size(), "Note count stays unchanged");
			for (size_t index = 0; index < std::min(beforeNotes.size(), afterNotes.size()); ++index)
			{
				const Note& before = beforeNotes[index]; const Note& after = afterNotes[index];
				check(before.BeatTime == after.BeatTime && before.BeatDuration == after.BeatDuration && before.Type == after.Type
					&& before.BalloonPopCount == after.BalloonPopCount && before.TimeOffset == after.TimeOffset, "Note positions, long-note lengths and values stay unchanged");
				check(close(original.BeatToPlaybackTime(before.GetStart(), branch).ToSec(), actual.BeatToPlaybackTime(after.GetStart(), branch).ToSec())
					&& close(original.BeatToPlaybackTime(before.GetEnd(), branch).ToSec(), actual.BeatToPlaybackTime(after.GetEnd(), branch).ToSec()), "Note head and tail playback times stay unchanged");
			}
		}
		std::vector<std::pair<GenericList, Beat>> beforeEvents, afterEvents;
		ForEachChartItem(original, [&](const ForEachChartItemData& item)
			{ if (item.List != GenericList::SignatureChanges && (!allowScrollChanges || !IsScrollChangesList(item.List))
				&& (!allowBarLineChanges || item.List != GenericList::BarLineChanges)) beforeEvents.emplace_back(item.List, GetBeat(item, original)); });
		ForEachChartItem(actual, [&](const ForEachChartItemData& item)
			{ if (item.List != GenericList::SignatureChanges && (!allowScrollChanges || !IsScrollChangesList(item.List))
				&& (!allowBarLineChanges || item.List != GenericList::BarLineChanges)) afterEvents.emplace_back(item.List, GetBeat(item, actual)); });
		check(beforeEvents == afterEvents, "All non-signature event positions stay unchanged");
		if (!allowBarLineChanges)
			for (size_t index = 0; index < std::min(original.BarLineChanges.size(), actual.BarLineChanges.size()); ++index)
				check(original.BarLineChanges[index].IsVisible == actual.BarLineChanges[index].IsVisible, "Existing barline visibility stays unchanged");
		if (!allowScrollChanges)
			for (size_t index = 0; index < std::min(original.ScrollChanges_Normal.size(), actual.ScrollChanges_Normal.size()); ++index)
				check(original.ScrollChanges_Normal[index].ScrollSpeed == actual.ScrollChanges_Normal[index].ScrollSpeed, "SCROLL values stay unchanged");
		for (size_t index = 0; index < std::min(original.JPOSScrollChanges.size(), actual.JPOSScrollChanges.size()); ++index)
			check(original.JPOSScrollChanges[index].Move == actual.JPOSScrollChanges[index].Move
				&& original.JPOSScrollChanges[index].Duration == actual.JPOSScrollChanges[index].Duration, "JPOS values stay unchanged");
	};
	const auto verify = [&](const std::string& body, Beat start, Beat end, int rate, int bpm = 120)
	{
		scenario = "BPM " + std::to_string(bpm) + ", rate " + std::to_string(rate) + ", ticks "
			+ std::to_string(start.Ticks) + ".." + std::to_string(end.Ticks) + "\n" + body;
		ChartContext editor;
		parse(body, editor.Chart, bpm);
		editor.ChartSelectedCourse = editor.Chart.Courses[0].get();
		auto& edited = *editor.ChartSelectedCourse;
		edited.TempoMap.Signature[0].IsSelected = true;
		const ChartCourse original = edited;
		ChartProject savedOriginal;
		roundTrip(editor.Chart, savedOriginal);
		editor.RangeSelection = { end, start, true, true };
		editor.Marker = { end, true };
		editor.SetCursorBeat(start);
		auto plan = BuildBarLineGimmickPlan(edited, start, end, rate);
		check(plan.Error == BarLineGimmickError::None, "Supported selection produces a valid generation plan");
		if (plan.Error != BarLineGimmickError::None) return;
		const Beat firstBar = plan.FirstBar, afterBar = plan.AfterBar;
		const int count = plan.MeasureCount;
		for (const auto& signature : plan.Signatures)
		{
			check(IsTimeSignatureSupported(signature.Signature), "Every generated denominator divides the internal bar resolution");
			check(static_cast<i64>(signature.Signature.GetDurationPerBar().Ticks) * signature.Signature.Denominator
				== static_cast<i64>(Beat::FromBars(1).Ticks) * signature.Signature.Numerator, "Every generated measure is exact in both TJA ratio and internal ticks");
		}
		editor.Undo.Execute<Commands::GenerateBarLineGimmick>(&edited, std::move(plan));
		check(editor.Undo.UndoStack.size() == 1, "Generation is recorded as exactly one undo operation");
		check(editor.RangeSelection.Start == end && editor.RangeSelection.End == start && editor.RangeSelection.IsActive
			&& editor.Marker.BeatTime == end && editor.GetCursorBeat() == start, "Generation preserves reversed selection, marker and cursor");
		const auto verifyBars = [&](const ChartCourse& actual)
		{
			const Beat limit = afterBar + Beat::FromBars(2);
			const auto beforeBars = bars(original, limit), afterBars = bars(actual, limit);
			std::vector<Beat> beforeOutside, afterOutside;
			for (Beat beat : beforeBars) if (beat <= firstBar || beat >= afterBar) beforeOutside.push_back(beat);
			for (Beat beat : afterBars) if (beat <= firstBar || beat >= afterBar) afterOutside.push_back(beat);
			check(beforeOutside == afterOutside, "The original surrounding bar positions are preserved exactly");
			check(std::find(afterBars.begin(), afterBars.end(), start) != afterBars.end()
				&& std::find(afterBars.begin(), afterBars.end(), end) != afterBars.end(), "Range endpoints are exact new bar boundaries");
			check(std::count_if(afterBars.begin(), afterBars.end(), [&](Beat beat) { return beat >= start && beat < end; }) == count,
				"Preview measure count equals the generated range divisions");
			for (Beat beat : beforeOutside)
				check(close(original.BeatToPlaybackTime(beat).ToSec(), actual.BeatToPlaybackTime(beat).ToSec()), "Surrounding bar playback times stay unchanged");
		};
		verifyUnchanged(original, edited); verifyBars(edited);
		const ChartCourse generated = edited;
		ChartProject restored;
		roundTrip(editor.Chart, restored);
		verifyUnchanged(*savedOriginal.Courses[0], *restored.Courses[0]); verifyBars(*restored.Courses[0]);
		check(bars(edited, afterBar + Beat::FromBars(2)) == bars(*restored.Courses[0], afterBar + Beat::FromBars(2)), "All generated boundaries survive a save/reload round trip");
		editor.Undo.Undo();
		check(sameSignatures(original, edited), "One undo restores exact signature positions, ratios and selection flags");
		verifyUnchanged(original, edited);
		editor.Undo.Redo();
		check(sameSignatures(generated, edited), "One redo restores the same generation");
		verifyUnchanged(original, edited); verifyBars(edited);
		const auto repeated = BuildBarLineGimmickPlan(edited, start, end, rate);
		editor.Undo.Execute<Commands::GenerateBarLineGimmick>(&edited, repeated);
		check(editor.Undo.UndoStack.size() == 2 && sameSignatures(generated, edited), "Repeating generation is a separate undo operation without accumulating signatures");
		editor.Undo.Undo();
		check(sameSignatures(generated, edited), "Undoing a repeated generation restores its immediate predecessor");
	};

	const std::string plain = "1000,\n2000,\n3000,\n4000,";
	ChartProject central;
	parse(plain, central);
	const auto centralPlan = BuildBarLineGimmickPlan(*central.Courses[0], Beat::FromBeats(3), Beat::FromBeats(5), 120);
	check(centralPlan.Signatures.size() == 4 && centralPlan.Signatures[0].Signature == TimeSignature(3, 4)
		&& centralPlan.Signatures[1].Beat == Beat::FromBeats(3) && centralPlan.Signatures[1].Signature == TimeSignature(1, 240)
		&& centralPlan.Signatures[2].Beat == Beat::FromBeats(5) && centralPlan.Signatures[2].Signature == TimeSignature(3, 4)
		&& centralPlan.Signatures[3].Beat == Beat::FromBeats(8) && centralPlan.Signatures[3].Signature == TimeSignature(4, 4),
		"Central two beats of two 4/4 bars produce 3/4, gimmick, 3/4 and 4/4 restoration");
	for (int rate : { 120, 60, 30 }) verify(plain, Beat::FromBeats(3), Beat::FromBeats(5), rate);
	verify(plain, Beat::FromBeats(1), Beat::FromBeats(2), 120);
	verify(plain, Beat::Zero(), Beat::FromBars(1), 120);
	verify(plain, Beat::Zero(), Beat::FromBars(2), 60);
	verify(plain, Beat::FromTicks(1), Beat::FromTicks(2), 120);
	verify(plain, Beat::Zero(), Beat::FromTicks(167), 120);
	verify(plain, Beat::FromBeats(3) + Beat::FromTicks(1), Beat::FromBeats(5) + Beat::FromTicks(2), 120);
	verify(plain, Beat::FromBeats(3), Beat::FromBeats(5), 120, 123);
	verify(plain, Beat::FromBeats(3), Beat::FromBeats(5), 120, 175);
	verify("1000,\n#BPMCHANGE 180\n2000,\n3000,", Beat::FromBeats(3), Beat::FromBeats(5) + Beat::FromTicks(1), 120);
	verify("1000,\n00\n#BPMCHANGE 150\n00,\n2000,", Beat::FromBeats(5) + Beat::FromTicks(1), Beat::FromBeats(7) + Beat::FromTicks(2), 120);
	verify("1000,\n#MEASURE 3/4\n100,\n200,\n#MEASURE 4/4\n3000,\n4000,", Beat::FromBeats(5), Beat::FromBeats(11), 60);
	verify("#MEASURE 7/8\n1000000,\n2000000,\n3000000,\n4000000,", Beat::FromBeats(2), Beat::FromBeats(5), 30);
	verify("#BARLINEOFF\n#GOGOSTART\n5000,\n0\n#SCROLL 2\n0\n#GOGOEND\n00,\n8000,\n#BARLINEON\n3000,", Beat::FromBeats(3), Beat::FromBeats(5), 120);
	verify("#DELAY 0.25\n1000,\n2000,\n#DELAY -0.125\n3000,\n4000,", Beat::FromBeats(3), Beat::FromBeats(5), 120);
	verify("1000,\n2000,\n#BRANCHSTART p,50,80\n#N\n1000,\n#E\n2000,\n#M\n3000,\n#BRANCHEND\n4000,", Beat::FromBeats(3), Beat::FromBeats(5), 120);
	verify("#JPOSSCROLL 0.5 100 1\n1000,\n2000,\n3000,\n4000,", Beat::FromBeats(3), Beat::FromBeats(5), 120);
	const auto sameScrolls = [](const ChartCourse& first, const ChartCourse& second)
	{
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			const auto& before = first.GetScrollChanges(branch); const auto& after = second.GetScrollChanges(branch);
			if (before.size() != after.size()) return false;
			for (size_t index = 0; index < before.size(); ++index)
			{
				const auto& original = before[index]; const auto& actual = after[index];
				if (original.BeatTime != actual.BeatTime || original.ScrollSpeed != actual.ScrollSpeed || original.IsSelected != actual.IsSelected
					|| original.SourceCommandOrder != actual.SourceCommandOrder || original.SourceBeat != actual.SourceBeat
					|| original.SourceBranchStart != actual.SourceBranchStart || original.SourceScrollSpeed != actual.SourceScrollSpeed
					|| original.SourceIsCommon != actual.SourceIsCommon) return false;
			}
		}
		return true;
	};
	const auto sameBarLineChanges = [](const ChartCourse& first, const ChartCourse& second, bool compareSelection = true)
	{
		if (first.BarLineChanges.size() != second.BarLineChanges.size()) return false;
		for (size_t index = 0; index < first.BarLineChanges.size(); ++index)
		{
			const auto& before = first.BarLineChanges[index]; const auto& after = second.BarLineChanges[index];
			if (before.BeatTime != after.BeatTime || before.IsVisible != after.IsVisible || (compareSelection && before.IsSelected != after.IsSelected)) return false;
		}
		return true;
	};
	const auto verifyScroll = [&](const std::string& body, Beat start, Beat end, float startValue, float endValue, bool interpolate,
		bool hideNoteBars = false, bool applyScroll = true)
	{
		scenario = "SCROLL " + std::to_string(startValue) + ".." + std::to_string(endValue) + ", ticks "
			+ std::to_string(start.Ticks) + ".." + std::to_string(end.Ticks) + "\n" + body;
		ChartContext editor;
		parse(body, editor.Chart);
		editor.ChartSelectedCourse = editor.Chart.Courses[0].get();
		auto& edited = *editor.ChartSelectedCourse;
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			for (ScrollChange& change : edited.GetScrollChanges(branch)) change.IsSelected = true;
		for (BarLineChange& change : edited.BarLineChanges) change.IsSelected = true;
		const ChartCourse original = edited;
		ChartProject savedOriginal;
		roundTrip(editor.Chart, savedOriginal);
		editor.RangeSelection = { start, end, true, true };
		editor.Marker = { end, true };
		editor.SetCursorBeat(start);
		auto plan = BuildBarLineGimmickPlan(edited, start, end, 120);
		if (applyScroll) AddBarLineGimmickScrollToPlan(edited, plan, startValue, endValue, interpolate);
		if (hideNoteBars) AddBarLineGimmickNoteVisibilityToPlan(edited, plan);
		check(plan.Error == BarLineGimmickError::None, "SCROLL settings produce a valid plan");
		if (plan.Error != BarLineGimmickError::None) return;
		const Beat afterBar = plan.AfterBar;
		for (const auto& item : plan.RemoveItems)
			if (IsScrollChangesList(item.List)) check(GetBeat(item) >= start && GetBeat(item) < end, "Only SCROLL commands inside the range are removed");
		for (const auto& item : plan.AddItems)
			if (IsScrollChangesList(item.List)) check(GetBeat(item) >= start && GetBeat(item) < end, "No SCROLL restoration is added at or after the range end");
		editor.Undo.Execute<Commands::GenerateBarLineGimmick>(&edited, std::move(plan));
		check(editor.Undo.UndoStack.size() == 1, "Measures and SCROLL generation share one undo operation");
		check(editor.RangeSelection.Start == start && editor.RangeSelection.End == end && editor.RangeSelection.IsActive
			&& editor.Marker.BeatTime == end && editor.GetCursorBeat() == start, "SCROLL generation keeps the selection, marker and cursor");
		const auto verifySpeeds = [&](const ChartCourse& actual)
		{
			const auto allBars = bars(actual, afterBar);
			std::vector<Beat> insideBars;
			for (Beat beat : allBars) if (beat >= start && beat < end) insideBars.push_back(beat);
			check(!insideBars.empty(), "SCROLL range contains generated bar lines");
			if (insideBars.empty()) return;
			const Beat lastBar = insideBars.back();
			const auto sameSpeed = [](Complex first, Complex second)
			{
				return std::abs(first.GetRealPart() - second.GetRealPart()) < 0.00001f
					&& std::abs(first.GetImaginaryPart() - second.GetImaginaryPart()) < 0.00001f;
			};
			for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			{
				if (branch != BranchType::Normal && actual.Branches.empty()) continue;
				const auto& originalScrolls = original.GetScrollChanges(branch);
				const auto& actualScrolls = actual.GetScrollChanges(branch);
				std::vector<Beat> noteBeats;
				for (const Note& note : original.GetNotes(branch))
				{
					for (Beat beat : { note.GetStart(), note.GetEnd() })
					{
						if (beat < start || beat >= end) continue;
						noteBeats.push_back(beat);
						check(sameSpeed(ScrollOrDefault(originalScrolls.TryFindLastAtBeat(beat)), ScrollOrDefault(actualScrolls.TryFindLastAtBeat(beat))),
							"Every selected note head and tail keeps its original complex SCROLL");
					}
				}
				for (Beat beat : insideBars)
				{
					const bool hasNote = std::find(noteBeats.begin(), noteBeats.end(), beat) != noteBeats.end();
					const double progress = interpolate && lastBar > start ? static_cast<double>((beat - start).Ticks) / (lastBar - start).Ticks : 0.0;
					const Complex expected = hasNote || !applyScroll ? ScrollOrDefault(originalScrolls.TryFindLastAtBeat(beat))
						: Complex(static_cast<float>(static_cast<double>(startValue) * (1.0 - progress) + static_cast<double>(interpolate ? endValue : startValue) * progress), 0.0f);
					check(sameSpeed(expected, ScrollOrDefault(actualScrolls.TryFindLastAtBeat(beat))),
						"Bar lines use the requested constant or beat-linear SCROLL, with notes taking priority");
				}
				for (const ScrollChange& change : originalScrolls)
				{
					if (change.BeatTime >= start && change.BeatTime < end) continue;
					const auto* retained = actualScrolls.TryFindExactAtBeat(change.BeatTime);
					check(retained && sameSpeed(retained->ScrollSpeed, change.ScrollSpeed), "Existing SCROLL commands outside the range remain in place");
				}
			}
			if (hideNoteBars)
			{
				std::vector<Beat> allNoteBeats;
				for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
					for (const Note& note : original.GetNotes(branch))
						for (Beat beat : { note.GetStart(), note.GetEnd() })
							if (beat >= start && beat < end) allNoteBeats.push_back(beat);
				for (Beat beat : allNoteBeats)
				{
					const auto* off = actual.BarLineChanges.TryFindExactAtBeat(beat);
					check(off && !off->IsVisible, "Every selected note head and tail has BARLINEOFF");
					check(!VisibleOrDefault(actual.BarLineChanges.TryFindLastAtBeat(beat)), "Bar lines at note positions are hidden");
					const auto nextBar = std::upper_bound(insideBars.begin(), insideBars.end(), beat);
					const Beat next = nextBar != insideBars.end() ? *nextBar : end;
					const bool nextHasNote = std::find(allNoteBeats.begin(), allNoteBeats.end(), next) != allNoteBeats.end();
					const auto* on = actual.BarLineChanges.TryFindExactAtBeat(next);
					check(on && on->IsVisible == !nextHasNote, "The next bar has BARLINEON unless another note requires BARLINEOFF, including the range end");
				}
				if (allNoteBeats.empty()) check(sameBarLineChanges(original, actual, false), "A range without notes does not add visibility commands");
			}
		};
		verifyUnchanged(original, edited, applyScroll, hideNoteBars); verifySpeeds(edited);
		const ChartCourse generated = edited;
		ChartProject restored;
		roundTrip(editor.Chart, restored);
		verifyUnchanged(*savedOriginal.Courses[0], *restored.Courses[0], applyScroll, hideNoteBars); verifySpeeds(*restored.Courses[0]);
		editor.Undo.Undo();
		check(sameSignatures(original, edited) && sameScrolls(original, edited) && sameBarLineChanges(original, edited),
			"One undo restores original measures, SCROLLs and visibility including imported scope and selection");
		verifyUnchanged(original, edited);
		editor.Undo.Redo();
		check(sameSignatures(generated, edited) && sameScrolls(generated, edited) && sameBarLineChanges(generated, edited),
			"One redo restores the exact generated measures, SCROLLs and visibility");
		verifySpeeds(edited);
	};
	verifyScroll("0000,\n0000,", Beat::Zero(), Beat::FromBars(1), 2.0f, 2.0f, false);
	verifyScroll("0000,\n0000,", Beat::Zero(), Beat::FromBars(1), 1.0f, 3.0f, true);
	verifyScroll(plain, Beat::FromBeats(3), Beat::FromBeats(5), 2.0f, 4.0f, true);
	verifyScroll("#SCROLL 1.25\n1000,\n0\n#SCROLL 2+0.5i\n10\n#SCROLL -1.5\n1,\n2000,", Beat::FromBeats(3), Beat::FromBeats(7), -2.0f, 3.0f, true);
	verifyScroll("#SCROLL 1.25\n5000,\n0\n#SCROLL 2+0.5i\n0\n#SCROLL -1.5\n00,\n8000,", Beat::FromBeats(3), Beat::FromBeats(9), 0.0f, -2.0f, true);
	verifyScroll("#SCROLL -1.5\n7000,\n0\n#SCROLL 2+0.5i\n000,\n8000,", Beat::FromBeats(3), Beat::FromBeats(9), -3.0f, 0.0f, false);
	verifyScroll("#SCROLL 1.25\n1100,\n0000,", Beat::FromTicks(1), Beat::FromBeats(2) + Beat::FromTicks(13), 2.0f, 2.0f, false);
	verifyScroll("0000,\n#BPMCHANGE 180\n0000,\n0000,", Beat::FromBeats(3) + Beat::FromTicks(1), Beat::FromBeats(7) + Beat::FromTicks(2), -3.0f, 4.0f, true);
	verifyScroll("#SCROLL 1.25\n1111,\n#SCROLL 3\n0000,", Beat::Zero(), Beat::FromBars(1), 2.0f, 4.0f, true);
	verifyScroll("0000,", Beat::FromTicks(1), Beat::FromTicks(2), 2.0f, 4.0f, true);
	verifyScroll("0000,", Beat::Zero(), Beat::FromTicks(169), 3.0f, -2.0f, true);
	verifyScroll("0000,", Beat::Zero(), Beat::FromBeats(1), 2.0f, std::numeric_limits<float>::quiet_NaN(), false);
	verifyScroll("1000,\n2000,\n#BRANCHSTART p,50,80\n#N\n#SCROLL 2\n1000,\n#E\n#SCROLL 3\n2000,\n#M\n#SCROLL 4\n3000,\n#BRANCHEND\n4000,",
		Beat::FromBeats(3), Beat::FromBeats(5), 2.0f, 4.0f, true);
	verifyScroll("#BARLINEOFF\n#SCROLL 1.25\n1111,\n#BARLINEOFF\n0000,", Beat::Zero(), Beat::FromBars(1), 2.0f, 4.0f, true, true);
	verifyScroll("#MEASURE 1/240\n1,\n2,\n0,\n3,\n0,\n0,", Beat::Zero(), Beat::FromTicks(1008), 2.0f, 4.0f, true, true);
	verifyScroll("#SCROLL 1.25\n5000,\n8000,", Beat::FromBeats(3), Beat::FromBeats(5), 2.0f, 2.0f, false, true);
	verifyScroll("#SCROLL 1.25\n1100,\n0000,", Beat::FromTicks(1), Beat::FromBeats(2) + Beat::FromTicks(13), 2.0f, 2.0f, false, true);
	verifyScroll("1000,", Beat::Zero(), Beat::FromTicks(1), 2.0f, 4.0f, true, true);
	verifyScroll("#BARLINEOFF\n0000,", Beat::Zero(), Beat::FromBeats(1), 2.0f, 4.0f, true, true);
	verifyScroll("#SCROLL 1.25\n1111,\n0000,", Beat::Zero(), Beat::FromBars(1), 2.0f, 4.0f, true, true, false);
	scenario.clear();
	for (float invalid : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
	{
		auto invalidStart = centralPlan;
		AddBarLineGimmickScrollToPlan(*central.Courses[0], invalidStart, invalid, 1.0f, false);
		check(invalidStart.Error == BarLineGimmickError::Scroll && invalidStart.AddItems.size() == centralPlan.AddItems.size(),
			"Nonfinite start SCROLL is rejected before changing the plan");
		auto invalidEnd = centralPlan;
		AddBarLineGimmickScrollToPlan(*central.Courses[0], invalidEnd, 1.0f, invalid, true);
		check(invalidEnd.Error == BarLineGimmickError::Scroll && invalidEnd.RemoveItems.size() == centralPlan.RemoveItems.size(),
			"Nonfinite end SCROLL is rejected before changing the plan");
	}

	ChartCourse rounded = *central.Courses[0];
	rounded.TempoMap.Tempo[0].Tempo = Tempo(123.0f);
	check(BuildBarLineGimmickPlan(rounded, Beat::Zero(), Beat::FromBars(1), 120).Segments[0].Denominator == 240, "BPM 123 rounds to supported 1/240");
	rounded.TempoMap.Tempo[0].Tempo = Tempo(175.0f);
	check(BuildBarLineGimmickPlan(rounded, Beat::Zero(), Beat::FromBars(1), 120).Segments[0].Denominator == 168, "BPM 175 rounds to the nearest supported unit-fraction denominator");
	const Beat shiftedStart = Beat::FromBeats(3) + Beat::FromTicks(1), shiftedEnd = Beat::FromBeats(5) + Beat::FromTicks(2);
	ChartCourse crossed = *central.Courses[0];
	Undo::UndoHistory crossedUndo;
	crossedUndo.Execute<Commands::GenerateBarLineGimmick>(&crossed, BuildBarLineGimmickPlan(crossed, shiftedStart, shiftedEnd, 120));
	const auto crossedBars = bars(crossed, Beat::FromBars(3));
	check(std::find(crossedBars.begin(), crossedBars.end(), Beat::FromBars(1)) == crossedBars.end(), "A crossed original boundary is not forcibly retained off the new grid");

	ChartCourse rejected = *central.Courses[0];
	rejected.Branches.push_back({ Beat::FromBeats(8), Beat::FromBeats(4) });
	check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(9), Beat::FromBeats(10), 120).Error == BarLineGimmickError::Branches, "Overlapping a branching section is rejected");
	check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(3), Beat::FromBeats(5), 120).Error == BarLineGimmickError::None, "A branch outside the selection does not disable generation");
	check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(5), Beat::FromBeats(8), 120).Error == BarLineGimmickError::None, "Ending exactly at a branch start does not overlap it");
	rejected.Branches[0].EndsBranching = false;
	check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(15), Beat::FromBeats(16), 120).Error == BarLineGimmickError::Branches, "An open-ended branch remains excluded");
	rejected.Branches.clear();
	for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
	{
		rejected.GetDelayChanges(branch).InsertOrUpdate({ Beat::FromBeats(4), Time::FromSec(0.0) });
		check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(3), Beat::FromBeats(5), 120).Error == BarLineGimmickError::Delay, "Even a zero DELAY in any branch is excluded");
		check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(4), Beat::FromBeats(5), 120).Error == BarLineGimmickError::Delay, "DELAY at the start is excluded");
		check(BuildBarLineGimmickPlan(rejected, Beat::FromBeats(3), Beat::FromBeats(4), 120).Error == BarLineGimmickError::Delay, "DELAY at the joining end is excluded");
		rejected.GetDelayChanges(branch).Sorted.clear();
	}
	for (float bpm : { 0.0f, -120.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
	{
		rejected.TempoMap.Tempo[0].Tempo = Tempo(bpm);
		check(BuildBarLineGimmickPlan(rejected, Beat::Zero(), Beat::FromBeats(1), 120).Error == BarLineGimmickError::Tempo, "Nonpositive and nonfinite BPMs are excluded");
	}
	rejected.TempoMap.Tempo[0].Tempo = Tempo(120.0f);
	for (TimeSignature signature : { TimeSignature(1, 11), TimeSignature(0, 4), TimeSignature(-4, 4), TimeSignature(I32Max, 1) })
	{
		rejected.TempoMap.Signature[0].Signature = signature;
		check(BuildBarLineGimmickPlan(rejected, Beat::Zero(), Beat::FromBeats(1), 120).Error == BarLineGimmickError::Signature, "Unsupported, negative and overflowing source signatures are excluded");
	}
	check(BuildBarLineGimmickPlan(*central.Courses[0], Beat::FromBeats(1), Beat::FromBeats(1), 120).Error == BarLineGimmickError::InvalidRange, "Zero-length range is excluded");
	check(BuildBarLineGimmickPlan(*central.Courses[0], Beat::FromTicks(-1), Beat::FromBeats(1), 120).Error == BarLineGimmickError::InvalidRange, "Negative range start is excluded");
	check(BuildBarLineGimmickPlan(*central.Courses[0], Beat::Zero(), Beat::FromBeats(1), 45).Error == BarLineGimmickError::InvalidRange, "Unsupported rates are excluded");

	SortedTempoMap irregular;
	irregular.Signature.Sorted = { { Beat::Zero(), { 4, 4 } }, { Beat::FromBeats(3), { 3, 4 } },
		{ Beat::FromBeats(5), { 2, 4 } }, { Beat::FromBeats(8), { 4, 4 } } };
	irregular.ForEachBeatBar([&](const auto& bar)
	{
		if (bar.Beat > Beat::FromBeats(20)) return ControlFlow::Break;
		Beat found;
		TimeSignature signature;
		for (Beat offset : { Beat::Zero(), bar.Signature.GetDurationPerBar() / 2, bar.Signature.GetDurationPerBar() - Beat::FromTicks(1) })
			check(TryFindBarLineGimmickBar(irregular, bar.Beat + offset, found, signature) && found == bar.Beat && signature == bar.Signature,
				"Direct bar lookup agrees with the existing iterator, including off-boundary signature changes");
		return ControlFlow::Continue;
	});
	ChartCourse dense;
	dense.TempoMap.Signature.InsertOrUpdate({ Beat::Zero(), { 1, 40320 } });
	const auto distant = BuildBarLineGimmickPlan(dense, Beat::FromTicks(1000000000), Beat::FromTicks(1000000020), 120);
	check(distant.Error == BarLineGimmickError::None && distant.FirstBar.Ticks == 1000000000 && distant.AfterBar.Ticks == 1000000020,
		"A distant range after a billion tiny bars is found without enumerating those bars");
	std::cout << "Bar line gimmick: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
