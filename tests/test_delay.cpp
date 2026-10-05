#include "peepo_drum_kit/chart_editor_undo.h"
#include <iostream>

using namespace PeepoDrumKit;

namespace Audio
{
	struct AudioEngine::Impl {};
	AudioEngine::AudioEngine() = default;
	AudioEngine::~AudioEngine() = default;
	b8 TestVoicePlaying = false;
	Time TestVoicePosition = {};
	b8 Voice::GetIsPlaying() const { return TestVoicePlaying; }
	Time Voice::GetPositionSmooth() const { return TestVoicePosition; }
	void Voice::SetPosition(Time value) { TestVoicePosition = value; }
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
	int failures = 0;
	const auto check = [&](bool condition, const char* name)
	{
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; ++failures; }
	};
	const auto close = [](double first, double second) { return std::abs(first - second) < 0.00001; };
	const auto parse = [&](const std::string& body, ChartProject& chart)
	{
		TJA::ErrorList errors;
		const std::string text = "TITLE:Delay\nBPM:120\nCOURSE:Oni\n#START\n" + body + "\n#END\n";
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
		check(errors.Errors.empty(), "Fixture parses without errors");
		check(CreateChartProjectFromTJA(parsed, chart), "Chart imports");
	};
	const auto roundTrip = [&](const ChartProject& chart, ChartProject& restored)
	{
		TJA::ParsedTJA output;
		check(ConvertChartProjectToTJA(chart, output, false), "Chart exports");
		std::string text;
		TJA::ConvertParsedToText(output, text, TJA::SaveFormat::Current);
		if (UTF8::HasBOM(text)) text.erase(0, sizeof(UTF8::BOM_UTF8));
		TJA::ErrorList errors;
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
		check(errors.Errors.empty(), "Exported text parses");
		check(CreateChartProjectFromTJA(parsed, restored), "Exported chart imports");
	};

	ChartProject accumulated;
	parse("1000,\n#DELAY 0.5\n#DELAY 0.25\n2000,\n#DELAY 0\n3000,", accumulated);
	auto& course = *accumulated.Courses[0];
	check(course.DelayChanges_Normal.size() == 2, "Consecutive delays share an editable event and retain zero delay");
	check(course.DelayChanges_Normal[0].SourceCommands.size() == 2, "Consecutive source commands are retained");
	check(close(course.BeatToPlaybackTime(Beat::FromBars(1)).ToSec(), 2.75), "Delays accumulate");
	check(close(course.BeatToPlaybackTime(Beat::FromBars(2)).ToSec(), 4.75), "Zero does not reset accumulated delay");
	check(course.Notes_Normal[1].TimeOffset == Time::Zero(), "DELAY does not become a per-note offset");
	check(close(course.TempoMap.BeatToTime(Beat::FromBars(1)).ToSec(), 2.0), "Editing coordinates exclude delay");
	ChartProject accumulatedRestored;
	roundTrip(accumulated, accumulatedRestored);
	check(close(accumulatedRestored.Courses[0]->BeatToPlaybackTime(Beat::FromBars(2)).ToSec(), 4.75), "Cumulative timing survives a text round trip");
	ChartProject precise, preciseRestored;
	parse("#DELAY 0.123456789012345\n1000,", precise);
	roundTrip(precise, preciseRestored);
	check(preciseRestored.Courses[0]->DelayChanges_Normal[0].Duration == precise.Courses[0]->DelayChanges_Normal[0].Duration, "Delay precision survives a text round trip");
	TJA::ErrorList invalidErrors;
	TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines("BPM:120\nCOURSE:Oni\n#START\n#DELAY inf\n1000,\n#END\n")), invalidErrors);
	check(!invalidErrors.Errors.empty(), "Non-finite delays are rejected");

	ChartProject negative;
	parse("1000,\n#DELAY -1\n2000,", negative);
	auto& negativeCourse = *negative.Courses[0];
	const auto& negativeTiming = negativeCourse.GetPlaybackTiming();
	const auto candidates = negativeTiming.FindBeatCandidates(Time::FromSec(1.5));
	check(candidates.size() == 2, "A negative delay yields both overlapping beat candidates");
	check(negativeTiming.ConvertTimeToBeatWithHint(Time::FromSec(1.5), Beat::FromBeats(2)) == Beat::FromBeats(3), "An earlier interval remains selected");
	check(negativeTiming.ConvertTimeToBeatWithHint(Time::FromSec(1.5), Beat::FromBeats(4)) == Beat::FromBeats(5), "A later interval remains selected");
	check(close(negativeCourse.BeatToPlaybackTime(Beat::FromBars(1)).ToSec(), 1.0), "Negative delay moves the later bar earlier");
	check(close(negativeCourse.TempoMap.BeatToTime(Beat::FromBars(1)).ToSec(), 2.0), "Negative delay keeps timeline positions stable");

	ChartProject belowZero;
	parse("#DELAY -0.5\n1000,", belowZero);
	check(close(belowZero.Courses[0]->GetPlaybackTimeBounds().first.ToSec(), -0.5), "Playback bounds include negative note times");

	ChartProject roll;
	parse("5000\n#DELAY 0.5\n0008,", roll);
	const auto& rollCourse = *roll.Courses[0];
	const auto& rollNote = rollCourse.Notes_Normal[0];
	check(close((rollCourse.BeatToPlaybackTime(rollNote.GetEnd()) - rollCourse.BeatToPlaybackTime(rollNote.GetStart())).ToSec(), 2.25), "A mid-roll delay changes the tail time");

	ChartProject bpmOrder;
	parse("1000,\n#DELAY 0.5\n#BPMCHANGE 240\n#DELAY 0.25\n2000,", bpmOrder);
	const auto& bpmCourse = *bpmOrder.Courses[0];
	const auto& bpmTiming = bpmCourse.GetPlaybackTiming();
	const Time secondBar = bpmCourse.BeatToPlaybackTime(Beat::FromBars(1));
	check(close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), secondBar) / Beat::TicksPerBeat, 6.0), "Scroll offset uses each delay's command-time BPM");
	check(close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.25)) / Beat::TicksPerBeat, 4.5), "Scroll continues through a positive delay using its own BPM");
	ChartProject bpmRestored;
	roundTrip(bpmOrder, bpmRestored);
	const auto& restoredCourse = *bpmRestored.Courses[0];
	check(close(restoredCourse.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), restoredCourse.BeatToPlaybackTime(Beat::FromBars(1))) / Beat::TicksPerBeat, 6.0), "BPM/DELAY ordering survives export");
	ChartProject initialBpmReset, initialBpmRestored;
	parse("#BPMCHANGE 240\n#DELAY 0.25\n#BPMCHANGE 120\n1000,\n2000,", initialBpmReset);
	roundTrip(initialBpmReset, initialBpmRestored);
	check(close(initialBpmRestored.Courses[0]->BeatToPlaybackTime(Beat::FromBars(1)).ToSec(), 2.25), "An initial BPM reset after a delay survives export");
	ChartProject removedBpm, removedBpmRestored;
	parse("1000,\n#BPMCHANGE 240\n#DELAY 0.25\n2000,", removedBpm);
	removedBpm.Courses[0]->TempoMap.Tempo.RemoveAtBeat(Beat::FromBars(1));
	removedBpm.Courses[0]->TempoMap.RebuildAccelerationStructure();
	roundTrip(removedBpm, removedBpmRestored);
	check(close(removedBpmRestored.Courses[0]->TempoMap.Tempo[0].Tempo.BPM, 120.0) && removedBpmRestored.Courses[0]->TempoMap.Tempo.size() == 1, "Deleting a BPM change does not restore it from delay source commands");
	ChartProject canceledDelay;
	parse("1000,\n#DELAY 0.5\n#DELAY -0.5\n2000,", canceledDelay);
	check(close(canceledDelay.Courses[0]->GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.0)) / Beat::TicksPerBeat, 4.0), "Canceled delays do not create a phantom scroll wait");

	Undo::UndoHistory undo;
	const Beat eventBeat = Beat::FromBars(1);
	undo.Execute<Commands::UpdateDelay>(&negativeCourse, &negativeCourse.DelayChanges_Normal, DelayChange { eventBeat, Time::FromSec(-0.25) });
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.75), "Editing a delay changes playback timing");
	check(close(negativeCourse.TempoMap.BeatToTime(eventBeat).ToSec(), 2.0), "Editing a delay does not move the timeline event");
	undo.Undo();
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.0), "Undo restores delay timing");
	undo.Redo();
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.75), "Redo restores edited delay timing");

	ChartProject branches;
	parse("#BRANCHSTART p,50,80\n#N\n#DELAY 0.5\n1000,\n#E\n#DELAY -0.25\n2000,\n#M\n#DELAY 1\n3000,\n#BRANCHEND", branches);
	const auto& branchCourse = *branches.Courses[0];
	check(close(branchCourse.BeatToPlaybackTime({}, BranchType::Normal).ToSec(), 0.5) && close(branchCourse.BeatToPlaybackTime({}, BranchType::Expert).ToSec(), -0.25) && close(branchCourse.BeatToPlaybackTime({}, BranchType::Master).ToSec(), 1.0), "Branches keep independent delays");
	ChartProject branchRestored;
	roundTrip(branches, branchRestored);
	check(close(branchRestored.Courses[0]->BeatToPlaybackTime({}, BranchType::Expert).ToSec(), -0.25), "Branch delay survives export");
	ChartContext selection;
	parse("1000,\n#DELAY 0.5\n2000,", selection.Chart);
	selection.SetSelectedChart(selection.Chart.Courses[0].get(), BranchType::Normal);
	selection.SetCursorTime(Time::FromSec(2.25));
	const Beat pausedBeat = selection.GetCursorBeat();
	ChartProject replacement;
	parse("1000,\n#DELAY -0.5\n2000,", replacement);
	selection.Chart = std::move(replacement);
	selection.ResetChartsCompared();
	selection.SetSelectedChart(selection.Chart.Courses[0].get(), BranchType::Normal);
	check(selection.GetCursorBeat() == pausedBeat, "Opening a chart retains the paused beat after freeing the old course");
	check(close(selection.GetCursorTime().ToSec(), 1.5) && !selection.CursorTimeWhilePaused.has_value(), "Opening a chart uses the new delay timing and clears the old wait time");
	check(selection.ChartsCompared.size() == 1 && selection.ChartsCompared.begin()->first == selection.ChartSelectedCourse, "Opening a chart replaces the comparison reference");
	selection.SetCursorTime(Time::FromSec(1.25));
	selection.SetSelectedChart(selection.ChartSelectedCourse, BranchType::Expert);
	check(close(selection.GetCursorTime().ToSec(), 1.25), "Switching a live branch retains the exact cursor time");
	Audio::TestVoicePlaying = true;
	Audio::TestVoicePosition = Time::FromSec(1.25);
	ChartProject playingReplacement;
	parse("1000,\n#DELAY 1\n2000,", playingReplacement);
	selection.Chart = std::move(playingReplacement);
	selection.SetSelectedChart(selection.Chart.Courses[0].get(), BranchType::Normal);
	check(close(selection.GetCursorTime().ToSec(), 1.25), "Opening a chart during playback retains the audio time without accessing the freed course");
	Audio::TestVoicePlaying = false;
	std::cout << (failures ? "DELAY tests failed" : "DELAY tests passed") << '\n';
	return failures ? 1 : 0;
}
