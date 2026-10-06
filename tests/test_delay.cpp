#include "peepo_drum_kit/chart_editor_undo.h"
#include "peepo_drum_kit/chart_editor_playback_timeline.h"
#include "peepo_drum_kit/chart_editor_widgets.h"
#include "peepo_drum_kit/chart_editor_delay_conversion.h"
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
	check(negativeTiming.ScrollStops.empty(), "Negative delays do not create stops");
	check(close(negativeTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBeats(4), Time::FromSec(1.0)) / Beat::TicksPerBeat, 2.0)
		&& close(negativeTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBeats(5), Time::FromSec(1.5)) / Beat::TicksPerBeat, 3.0), "Negative delay preserves scroll rewind");

	ChartProject belowZero;
	parse("#DELAY -0.5\n1000,", belowZero);
	check(close(belowZero.Courses[0]->GetPlaybackTimeBounds().first.ToSec(), -0.5), "Playback bounds include negative note times");

	ChartProject roll;
	parse("5000\n#DELAY 0.5\n0008,", roll);
	auto& rollCourse = *roll.Courses[0];
	const auto& rollNote = rollCourse.Notes_Normal[0];
	check(close((rollCourse.BeatToPlaybackTime(rollNote.GetEnd()) - rollCourse.BeatToPlaybackTime(rollNote.GetStart())).ToSec(), 2.25), "A mid-roll delay changes the tail time");
	const auto& rollTiming = rollCourse.GetPlaybackTiming();
	check(close(rollTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(rollNote.GetEnd(), rollCourse.BeatToPlaybackTime(rollNote.GetEnd())) / Beat::TicksPerBeat, 3.5), "A positive delay does not lengthen a BM/HB roll body");
	check(rollTiming.ScrollStops.size() == 1 && rollTiming.ScrollStops[0].BeatTime == Beat::FromBeats(2)
		&& close(rollTiming.ScrollStops[0].StartTime.ToSec(), 1.0) && close(rollTiming.ScrollStops[0].EndTime.ToSec(), 1.5), "A mid-roll delay stops at its source beat");

	ChartProject bpmOrder;
	parse("1000,\n#DELAY 0.5\n#BPMCHANGE 240\n#DELAY 0.25\n2000,", bpmOrder);
	auto& bpmCourse = *bpmOrder.Courses[0];
	const auto& bpmTiming = bpmCourse.GetPlaybackTiming();
	const Time secondBar = bpmCourse.BeatToPlaybackTime(Beat::FromBars(1));
	check(close(secondBar.ToSec(), 2.75) && close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), secondBar) / Beat::TicksPerBeat, 4.0), "Positive delays extend time without advancing scroll distance");
	check(bpmTiming.ScrollStops.size() == 2 && close(bpmTiming.ScrollStops[0].StartTime.ToSec(), 2.0)
		&& close(bpmTiming.ScrollStops[0].EndTime.ToSec(), 2.5) && close(bpmTiming.ScrollStops[1].StartTime.ToSec(), 2.5)
		&& close(bpmTiming.ScrollStops[1].EndTime.ToSec(), 2.75), "Consecutive source delays retain separate stop intervals");
	check(close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.25)) / Beat::TicksPerBeat, 4.0)
		&& close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.625)) / Beat::TicksPerBeat, 4.0), "Scroll stays fixed across a BPM change inside a stop");
	check(close(bpmTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromTicks(Beat::TicksPerBeat * 9 / 2), Time::FromSec(2.875)) / Beat::TicksPerBeat, 4.5), "Scroll resumes at the new BPM after a stop");
	ChartProject bpmRestored;
	roundTrip(bpmOrder, bpmRestored);
	const auto& restoredCourse = *bpmRestored.Courses[0];
	check(close(restoredCourse.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), restoredCourse.BeatToPlaybackTime(Beat::FromBars(1))) / Beat::TicksPerBeat, 4.0), "Positive stops and BPM ordering survive export");
	ChartProject negativeBpmOrder, negativeBpmRestored;
	parse("1000,\n#DELAY -0.5\n#BPMCHANGE 240\n#DELAY -0.25\n2000,", negativeBpmOrder);
	roundTrip(negativeBpmOrder, negativeBpmRestored);
	const auto& negativeBpmCourse = *negativeBpmRestored.Courses[0];
	check(close(negativeBpmCourse.BeatToPlaybackTime(Beat::FromBars(1)).ToSec(), 1.25)
		&& close(negativeBpmCourse.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(1.25)) / Beat::TicksPerBeat, 2.0), "Negative rewind uses each source command's BPM after export");
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
	check(close(canceledDelay.Courses[0]->GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.0)) / Beat::TicksPerBeat, 3.0), "Canceled timing still retains the negative source command's rewind");
	check(canceledDelay.Courses[0]->GetPlaybackTiming().TryFindScrollStop(Beat::FromBars(1), Time::FromSec(2.0)) == nullptr, "Canceled delays do not create a phantom wait after the note time");
	ChartProject partialRewind;
	parse("1000,\n#DELAY 1\n#DELAY -0.5\n2000,", partialRewind);
	const auto& partialTiming = partialRewind.Courses[0]->GetPlaybackTiming();
	check(close(partialTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.25)) / Beat::TicksPerBeat, 4.0)
		&& close(partialTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(2.5)) / Beat::TicksPerBeat, 3.0), "A partial rewind shortens the effective stop before rewinding scroll");
	ChartProject rewindThenStop;
	parse("1000,\n#DELAY -0.5\n#DELAY 0.5\n2000,", rewindThenStop);
	const auto& rewindThenStopTiming = rewindThenStop.Courses[0]->GetPlaybackTiming();
	check(rewindThenStopTiming.FindBeatCandidates(Time::FromSec(1.75)).size() == 2
		&& rewindThenStopTiming.ConvertTimeToBeatWithHint(Time::FromSec(1.75), Beat::FromBars(1)) == Beat::FromBars(1)
		&& rewindThenStopTiming.ConvertTimeToBeatWithHint(Time::FromSec(1.75), Beat::FromBeats(2)) == Beat::FromTicks(Beat::TicksPerBeat * 7 / 2), "Time inversion preserves either the stop or the overlapping earlier interval using its hint");
	check(close(rewindThenStopTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromBars(1), Time::FromSec(1.75)) / Beat::TicksPerBeat, 3.0)
		&& close(rewindThenStopTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::FromTicks(Beat::TicksPerBeat * 7 / 2), Time::FromSec(1.75)) / Beat::TicksPerBeat, 3.5), "A stop after a rewind keeps overlapping source intervals distinct");
	ChartProject initialStop;
	parse("#DELAY 0.5\n1000,", initialStop);
	const auto& initialTiming = initialStop.Courses[0]->GetPlaybackTiming();
	check(initialTiming.TryFindScrollStop({}, Time::Zero()) != nullptr
		&& close(initialTiming.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing({}, Time::FromSec(0.25)), 0.0)
		&& initialTiming.TryFindScrollStop({}, Time::FromSec(0.5)) == nullptr, "An initial stop includes its start and excludes its end");
	ChartProject stopBelowZero;
	parse("#DELAY -0.5\n#DELAY 0.5\n1000,", stopBelowZero);
	check(close(stopBelowZero.Courses[0]->GetPlaybackTimeBounds().first.ToSec(), -0.5), "Playback bounds include a stop before zero even when its signed commands cancel");
	check(stopBelowZero.Courses[0]->GetPlaybackTiming().ConvertTimeToBeatWithHint(Time::FromSec(-0.25), Beat::Zero()) == Beat::Zero(), "Time inversion retains an initial stop below zero");

	Undo::UndoHistory undo;
	const Beat eventBeat = Beat::FromBars(1);
	undo.Execute<Commands::UpdateDelay>(&negativeCourse, &negativeCourse.DelayChanges_Normal, DelayChange { eventBeat, Time::FromSec(-0.25) });
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.75), "Editing a delay changes playback timing");
	check(close(negativeCourse.TempoMap.BeatToTime(eventBeat).ToSec(), 2.0), "Editing a delay does not move the timeline event");
	undo.Undo();
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.0), "Undo restores delay timing");
	undo.Redo();
	check(close(negativeCourse.BeatToPlaybackTime(eventBeat).ToSec(), 1.75), "Redo restores edited delay timing");
	undo.Execute<Commands::UpdateDelay>(&negativeCourse, &negativeCourse.DelayChanges_Normal, DelayChange { eventBeat, Time::FromSec(0.5) });
	check(negativeCourse.GetPlaybackTiming().TryFindScrollStop(eventBeat, Time::FromSec(2.25)) != nullptr, "Changing an edited delay from negative to positive creates a stop");
	undo.Undo();
	check(negativeCourse.GetPlaybackTiming().ScrollStops.empty(), "Undo removes a stop when restoring a negative delay");
	undo.Redo();
	check(negativeCourse.GetPlaybackTiming().ScrollStops.size() == 1, "Redo restores the edited stop");

	ChartProject branches;
	parse("#BRANCHSTART p,50,80\n#N\n#DELAY 0.5\n1000,\n#E\n#DELAY -0.25\n2000,\n#M\n#DELAY 1\n3000,\n#BRANCHEND", branches);
	const auto& branchCourse = *branches.Courses[0];
	check(close(branchCourse.BeatToPlaybackTime({}, BranchType::Normal).ToSec(), 0.5) && close(branchCourse.BeatToPlaybackTime({}, BranchType::Expert).ToSec(), -0.25) && close(branchCourse.BeatToPlaybackTime({}, BranchType::Master).ToSec(), 1.0), "Branches keep independent delays");
	check(branchCourse.GetPlaybackTiming(BranchType::Normal).ScrollStops.size() == 1
		&& branchCourse.GetPlaybackTiming(BranchType::Expert).ScrollStops.empty()
		&& branchCourse.GetPlaybackTiming(BranchType::Master).ScrollStops.size() == 1, "Branches keep independent stop intervals");
	ChartProject branchRestored;
	roundTrip(branches, branchRestored);
	check(close(branchRestored.Courses[0]->BeatToPlaybackTime({}, BranchType::Expert).ToSec(), -0.25), "Branch delay survives export");
	ChartContext selection;
	parse("1000,\n#DELAY 0.5\n2000,", selection.Chart);
	selection.SetSelectedChart(selection.Chart.Courses[0].get(), BranchType::Normal);
	selection.SetCursorTime(Time::FromSec(2.25));
	check(selection.GetCursorBeat() == Beat::FromBars(1) && close(selection.GetCursorTime().ToSec(), 2.25), "Seeking inside a stop retains its exact time and source beat");
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
	ChartProject playbackView;
	parse("0001,\n#DELAY -2\n2000,", playbackView);
	PlaybackTimelineData playbackData;
	auto& playbackCourse = *playbackView.Courses[0];
	BuildPlaybackTimelineData(playbackCourse, BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Notes.size() == 2 && playbackData.Notes[0].OriginalNote->BeatTime == Beat::FromBars(1)
		&& playbackData.Notes[1].OriginalNote->BeatTime < playbackData.Notes[0].OriginalNote->BeatTime, "Playback view orders notes by delayed time while keeping source beats");
	check(playbackData.LaneCount == 2 && playbackData.Sections[0].Lane != playbackData.Sections[1].Lane, "Overlapping delay sections occupy separate playback rows");
	check(playbackCourse.Notes_Normal[0].BeatTime < playbackCourse.Notes_Normal[1].BeatTime, "Building the playback view preserves chart note order");
	PlaybackTimelineLayout visibleLayout;
	std::set<Beat> hiddenSections { playbackData.Sections[0].StartBeat };
	BuildPlaybackTimelineLayout(playbackData, hiddenSections, visibleLayout);
	check(visibleLayout.LaneCount == 1 && visibleLayout.SectionLanes[0] == PlaybackTimelineLayout::HiddenLane && visibleLayout.SectionLanes[1] == 0,
		"Closing a playback section removes its row and repacks the remaining section");
	check(playbackData.Sections[1].Lane == 1 && playbackCourse.Notes_Normal.size() == 2, "Closing a playback section preserves chart data and the original playback layout");
	hiddenSections.insert(playbackData.Sections[1].StartBeat);
	BuildPlaybackTimelineLayout(playbackData, hiddenSections, visibleLayout);
	check(visibleLayout.LaneCount == 0, "All playback sections can be closed");
	hiddenSections.clear();
	BuildPlaybackTimelineLayout(playbackData, hiddenSections, visibleLayout);
	check(visibleLayout.LaneCount == 2 && visibleLayout.SectionLanes[0] != visibleLayout.SectionLanes[1], "Reopening playback sections restores overlapping rows");
	BuildPlaybackTimelineData(playbackCourse, BranchType::Normal, Beat::FromBars(1), playbackData);
	check(playbackData.LaneCount == 2 && playbackData.Sections.back().EndBeat > playbackData.Sections.back().StartBeat, "A final delay and note still participate in playback row overlap");
	playbackCourse.Notes_Normal[1].TimeOffset = Time::FromSec(-0.125);
	BuildPlaybackTimelineData(playbackCourse, BranchType::Normal, Beat::FromBars(2), playbackData);
	check(close(playbackData.MinTime.ToSec(), -0.125) && close(playbackData.Notes[0].HeadTime.ToSec(), -0.125), "Playback view includes note offsets and negative time bounds");
	ChartProject separatedView;
	parse("1000,\n#DELAY 1\n2000,", separatedView);
	BuildPlaybackTimelineData(*separatedView.Courses[0], BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.LaneCount == 1 && playbackData.Sections.size() == 1 && close(playbackData.Sections[0].EndTime.ToSec(), 5.0), "A positive delay remains inside one editing section");
	check(playbackData.Stops.size() == 1 && close(playbackData.Stops[0].StartTime.ToSec(), 2.0)
		&& close(playbackData.Stops[0].EndTime.ToSec(), 3.0) && playbackData.Stops[0].SectionIndex == 0, "Positive gaps become stop bands within their editing section");
	BuildPlaybackTimelineData(bpmCourse, BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Stops.size() == 2 && playbackData.LaneCount == 1 && playbackData.Sections.size() == 1, "Consecutive stops do not split editing sections");
	ChartProject manyStops;
	std::string stopBody;
	for (i32 index = 0; index < 32; ++index) stopBody += "1000,\n#DELAY 0.5\n";
	parse(stopBody, manyStops);
	BuildPlaybackTimelineData(*manyStops.Courses[0], BranchType::Normal, Beat::FromBars(32), playbackData);
	check(playbackData.Stops.size() == 32 && playbackData.Sections.size() == 1 && playbackData.LaneCount == 1, "Many positive delays keep a single section and playback row");
	ChartProject mixedSections;
	parse("1000,\n#DELAY 1\n2000,\n#DELAY -2\n3000,\n#DELAY 0.5\n4000,", mixedSections);
	BuildPlaybackTimelineData(*mixedSections.Courses[0], BranchType::Normal, Beat::FromBars(4), playbackData);
	check(playbackData.Sections.size() == 2 && playbackData.LaneCount == 2 && playbackData.Stops.size() == 2
		&& playbackData.Stops[0].SectionIndex == 0 && playbackData.Stops[1].SectionIndex == 1, "Mixed delays split sections only at rewinds and retain stops in each row");
	BuildPlaybackTimelineData(*rewindThenStop.Courses[0], BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Sections.size() == 2 && playbackData.LaneCount == 2 && close(playbackData.Sections[1].StartTime.ToSec(), 1.5), "A stop after a rewind participates in row overlap even when total delay is zero");
	BuildPlaybackTimelineData(*partialRewind.Courses[0], BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Stops.size() == 1 && close(playbackData.Stops[0].EndTime.ToSec(), 2.5), "Stop bands end at the effective note time after a partial rewind");
	BuildPlaybackTimelineData(*canceledDelay.Courses[0], BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Stops.empty(), "Canceled waits do not leave a stop band");
	BuildPlaybackTimelineData(rollCourse, BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Stops.size() == 1 && close(playbackData.Notes[0].TailTime.ToSec(), 2.25), "A stop band inside a roll preserves its playback tail time");
	ChartProject noDelay;
	parse("1000,", noDelay);
	BuildPlaybackTimelineData(*noDelay.Courses[0], BranchType::Normal, Beat::FromBars(2), playbackData);
	check(playbackData.Stops.empty() && noDelay.Courses[0]->GetPlaybackTiming().ScrollStops.empty(), "Opening a chart without delays clears old stop bands");
	ChartProject sharedNotes;
	parse("1000,\n#BRANCHSTART p,50,80\n#N\n1000,\n#E\n2000,\n#M\n3000,\n#BRANCHEND\n1000,", sharedNotes);
	BuildPlaybackTimelineData(*sharedNotes.Courses[0], BranchType::Expert, Beat::FromBars(3), playbackData);
	check(playbackData.Notes.size() == 3 && playbackData.Notes[0].OriginalNote == &sharedNotes.Courses[0]->Notes_Normal[0]
		&& playbackData.Notes[1].OriginalNote->Type == NoteType::Ka, "Playback view combines shared notes and the selected branch without duplicates");
	selection.SetCursorTimeAtBeat(Time::FromSec(3.125), Beat::FromBars(1));
	check(selection.GetCursorBeat() == Beat::FromBars(1) && close(selection.GetCursorTime().ToSec(), 3.125), "Selecting a playback note retains its exact source beat and offset time");

	GameCamera camera;
	const SortedJPOSScrollChangesList noJpos;
	for (const Time cursorTime : { Time::FromSec(1.625), Time::FromSec(1.875) })
	{
		const Beat earlierBeat = rewindThenStopTiming.ConvertTimeToBeatWithHint(cursorTime, Beat::FromBeats(2));
		check(close(camera.GetCursorHBScrollBeatTick(earlierBeat, cursorTime, rewindThenStopTiming) / Beat::TicksPerBeat, 3.0), "A BM/HB stop pauses the entire lane even when an earlier overlapping beat is selected");
	}
	ChartProject overlappingStops;
	parse("1000,\n#DELAY 1\n2000,\n#DELAY -3\n#DELAY 1\n3000,", overlappingStops);
	const auto& overlappingStopTiming = overlappingStops.Courses[0]->GetPlaybackTiming();
	check(close(camera.GetCursorHBScrollBeatTick(Beat::FromBars(2), Time::FromSec(2.5), overlappingStopTiming) / Beat::TicksPerBeat, 4.0), "Overlapping BM/HB stops prioritize the first source command");
	for (const auto mode : { ScrollMethod::BMSCROLL, ScrollMethod::HBSCROLL, ScrollMethod::NMSCROLL })
	{
		ChartProject preview;
		const std::string command = mode == ScrollMethod::BMSCROLL ? "#BMSCROLL\n" : mode == ScrollMethod::HBSCROLL ? "#HBSCROLL\n" : "#NMSCROLL\n";
		parse(command + "1000,\n#DELAY 1\n2000,\n1000,", preview);
		const auto& previewCourse = *preview.Courses[0];
		const auto& timing = previewCourse.GetPlaybackTiming();
		auto position = [&](Beat noteBeat, Time cursorTime)
		{
			const Beat cursorBeat = timing.ConvertTimeToBeatWithHint(cursorTime, Beat::FromBars(1));
			const f64 cursorScroll = camera.GetCursorHBScrollBeatTick(cursorBeat, cursorTime, timing);
			return camera.GetNoteCoordinatesLane({}, cursorTime, cursorScroll, previewCourse.BeatToPlaybackTime(noteBeat), noteBeat,
				Tempo(120.0), Complex(2.0f, 0.5f), mode, 400.0, timing, noJpos);
		};
		const vec2 before = position(Beat::FromBars(2), Time::FromSec(2.25));
		const vec2 after = position(Beat::FromBars(2), Time::FromSec(2.75));
		if (mode == ScrollMethod::NMSCROLL)
			check(close(after.x - before.x, -200.0) && close(after.y - before.y, -50.0), "NMSCROLL keeps moving in real time through a positive delay");
		else
		{
			const bool bm = mode == ScrollMethod::BMSCROLL;
			check(close(before.x, bm ? 400.0 : 800.0) && close(before.y, bm ? 0.0 : 200.0)
				&& close(before.x, after.x) && close(before.y, after.y), "BM/HB notes and barlines stay fixed with their respective scroll speeds");
			check(close(position(Beat::FromBars(1), Time::FromSec(2.25)).x, 0.0), "The DELAY barline stays at the judgment position during a stop");
			const vec2 resumed = position(Beat::FromBars(2), Time::FromSec(3.25));
			check(close(resumed.x, bm ? 350.0 : 700.0) && close(resumed.y, bm ? 0.0 : 175.0), "BM/HB movement resumes after a positive delay");
		}
	}
	for (const Time cursorTime : { Time::FromSec(1.125), Time::FromSec(1.375) })
	{
		const f64 cursorScroll = camera.GetCursorHBScrollBeatTick(Beat::FromBeats(2), cursorTime, rollTiming);
		const vec2 head = camera.GetNoteCoordinatesLane({}, cursorTime, cursorScroll, rollCourse.BeatToPlaybackTime(rollNote.GetStart()), rollNote.GetStart(),
			Tempo(120.0), Complex(1.0f, 0.0f), ScrollMethod::HBSCROLL, 400.0, rollTiming, noJpos);
		const vec2 tail = camera.GetNoteCoordinatesLane({}, cursorTime, cursorScroll, rollCourse.BeatToPlaybackTime(rollNote.GetEnd()), rollNote.GetEnd(),
			Tempo(120.0), Complex(1.0f, 0.0f), ScrollMethod::HBSCROLL, 400.0, rollTiming, noJpos);
		check(close(head.x, -200.0) && close(tail.x, 150.0), "Both ends of a BM/HB roll remain fixed during a mid-roll stop");
	}
	const auto verifyConversion = [&](const std::string& body, Beat start, Beat end, bool merge, double duration, const std::vector<TimeSignatureChange>& signatures)
	{
		ChartContext editor;
		parse(body, editor.Chart);
		editor.ChartSelectedCourse = editor.Chart.Courses[0].get();
		auto& edited = *editor.ChartSelectedCourse;
		const ChartCourse original = edited;
		editor.RangeSelection = { end, start, true, true };
		editor.Marker = { end + Beat::FromBeats(1), true };
		editor.SetCursorTimeAtBeat(edited.BeatToPlaybackTime(end) + Time::FromSec(0.125), end);
		const Time originalCursorTime = editor.GetCursorTime();
		auto plan = BuildDelayConversionPlan(edited, start, end, merge);
		check(plan.Error == DelayConversionError::None, "A range without note heads or long note ends can be converted");
		if (plan.Error != DelayConversionError::None) return;
		check(close(plan.Duration.ToSec(), duration), "Conversion integrates BPM and counts existing positive delays once");
		check(plan.Signatures.size() == signatures.size(), "Conversion generates only the necessary signature changes");
		for (size_t i = 0; i < std::min(plan.Signatures.size(), signatures.size()); i++)
			check(plan.Signatures[i].Beat == signatures[i].Beat && plan.Signatures[i].Signature == signatures[i].Signature, "Partial measures get the expected length and signature restoration");
		editor.Undo.Execute<Commands::ConvertRangeToDelay>(&editor, std::move(plan));
		check(editor.Undo.UndoStack.size() == 1 && !editor.RangeSelection.IsActive && editor.GetCursorBeat() == start, "Conversion is one undo operation and selects its resulting position");
		check(editor.Marker.BeatTime == start + Beat::FromBeats(1), "Conversion moves the marker with later beats");
		check(edited.DelayChanges_Normal.TryFindExactAtBeat(start) && edited.DelayChanges_Normal.TryFindExactAtBeat(start)->IsSelected, "The generated DELAY is selected");
		check(edited.CanUseSourceTiming(), "Conversion retains valid source timing for unrelated DELAY commands");
		const auto verifyNotes = [&](const ChartCourse& actual)
		{
			check(actual.Notes_Normal.size() == original.Notes_Normal.size(), "Conversion does not remove notes");
			for (size_t i = 0; i < std::min(actual.Notes_Normal.size(), original.Notes_Normal.size()); i++)
			{
				const auto& before = original.Notes_Normal[i]; const auto& after = actual.Notes_Normal[i];
				check(after.BeatTime == (before.BeatTime >= end ? before.BeatTime - (end - start) : before.BeatTime), "Later notes move by the removed beat span");
				check(close((actual.BeatToPlaybackTime(after.BeatTime) + after.TimeOffset).ToSec(), (original.BeatToPlaybackTime(before.BeatTime) + before.TimeOffset).ToSec()), "All note hit times survive conversion and text round trip");
				check(after.Type == before.Type && after.BalloonPopCount == before.BalloonPopCount, "Conversion preserves note types and balloon pop counts");
				const Beat expectedTail = before.GetEnd() >= end ? before.GetEnd() - (end - start) : before.GetEnd();
				check(after.GetEnd() == expectedTail, "Long note tails move by the removed beat span");
				check(close((actual.BeatToPlaybackTime(after.GetEnd()) + after.TimeOffset).ToSec(), (original.BeatToPlaybackTime(before.GetEnd()) + before.TimeOffset).ToSec()), "Long note end times survive conversion and text round trip");
			}
		};
		verifyNotes(edited);
		const auto verifyBars = [&](const ChartCourse& actual)
		{
			std::vector<Beat> beforeBars, afterBars;
			original.TempoMap.ForEachBeatBar([&](const auto& bar) { beforeBars.push_back(bar.Beat); return bar.Beat > end + Beat::FromBars(2) ? ControlFlow::Break : ControlFlow::Continue; });
			actual.TempoMap.ForEachBeatBar([&](const auto& bar) { afterBars.push_back(bar.Beat); return bar.Beat > start + Beat::FromBars(3) ? ControlFlow::Break : ControlFlow::Continue; });
			for (const Beat beat : beforeBars)
				if (beat >= end)
				{
					const Beat shifted = beat - (end - start);
					check(std::find(afterBars.begin(), afterBars.end(), shifted) != afterBars.end(), "Later original bar boundaries survive conversion");
					check(close(actual.BeatToPlaybackTime(shifted).ToSec(), original.BeatToPlaybackTime(beat).ToSec()), "Later bar times survive conversion and export");
				}
		};
		verifyBars(edited);
		ChartProject convertedRestored;
		roundTrip(editor.Chart, convertedRestored);
		verifyNotes(*convertedRestored.Courses[0]); verifyBars(*convertedRestored.Courses[0]);
		for (const auto& delay : original.DelayChanges_Normal)
			if (delay.BeatTime > end)
			{
				const auto& restored = *convertedRestored.Courses[0];
				const Beat shifted = delay.BeatTime - (end - start);
				const double before = original.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(delay.BeatTime, original.BeatToPlaybackTime(delay.BeatTime));
				const double after = restored.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(shifted, restored.BeatToPlaybackTime(shifted));
				check(close(after, before - (end - start).Ticks), "Unrelated negative source commands retain their individual BPM rewind distances");
			}
		editor.Undo.Undo();
		check(editor.RangeSelection.Start == end && editor.RangeSelection.End == start && editor.RangeSelection.IsActive, "Undo restores the original reversed range selection");
		check(editor.GetCursorBeat() == end && close(editor.GetCursorTime().ToSec(), originalCursorTime.ToSec()), "Undo restores the exact cursor time");
		check(edited.SourceTempoChangeCount == original.SourceTempoChangeCount && edited.DelayChanges_Normal.size() == original.DelayChanges_Normal.size(), "Undo restores timing metadata and the original DELAY list");
		check(edited.TempoMap.Signature.size() == original.TempoMap.Signature.size(), "Undo restores the signature list");
		for (size_t i = 0; i < original.TempoMap.Signature.size(); i++)
			check(edited.TempoMap.Signature[i].Beat == original.TempoMap.Signature[i].Beat && edited.TempoMap.Signature[i].Signature == original.TempoMap.Signature[i].Signature, "Undo restores exact original signature values");
		check(edited.Notes_Normal.size() == original.Notes_Normal.size(), "Undo restores the original note count");
		for (size_t i = 0; i < std::min(edited.Notes_Normal.size(), original.Notes_Normal.size()); i++)
			check(edited.Notes_Normal[i].BeatTime == original.Notes_Normal[i].BeatTime && edited.Notes_Normal[i].BeatDuration == original.Notes_Normal[i].BeatDuration,
				"Undo restores long note heads and lengths");
		editor.Undo.Redo(); verifyNotes(edited); verifyBars(edited);
	};
	const std::string emptyBar = "1000,\n0000,\n2000,\n3000,";
	verifyConversion(emptyBar, Beat::FromBeats(5), Beat::FromBeats(6), false, 0.5, { { Beat::FromBeats(4), { 3, 4 } }, { Beat::FromBeats(7), { 4, 4 } } });
	verifyConversion(emptyBar, Beat::FromBeats(5), Beat::FromBeatsFraction(5.5), false, 0.25, { { Beat::FromBeats(4), { 7, 8 } }, { Beat::FromBeatsFraction(7.5), { 4, 4 } } });
	verifyConversion(emptyBar, Beat::FromBeats(5), Beat::FromBeatsFraction(5.25), false, 0.125, { { Beat::FromBeats(4), { 15, 16 } }, { Beat::FromBeatsFraction(7.75), { 4, 4 } } });
	verifyConversion(emptyBar, Beat::FromBeats(4), Beat::FromBeats(8), false, 2.0, {});
	verifyConversion("1020,\n1000,", Beat::FromBeats(1), Beat::FromBeats(2), false, 0.5, { { Beat::Zero(), { 3, 4 } }, { Beat::FromBeats(3), { 4, 4 } } });
	for (const std::string& rollStart : { "5", "6", "7" })
	{
		verifyConversion(rollStart + "008,\n1000,", Beat::FromBeats(1), Beat::FromBeats(2), false, 0.5, { { Beat::Zero(), { 3, 4 } }, { Beat::FromBeats(3), { 4, 4 } } });
		verifyConversion(rollStart + "008,\n1000,", Beat::FromBeats(1), Beat::FromBeats(3), false, 1.0, { { Beat::Zero(), { 2, 4 } }, { Beat::FromBeats(2), { 4, 4 } } });
	}
	verifyConversion("5080,\n0000,\n1000,", Beat::FromBeats(5), Beat::FromBeats(6), false, 0.5, { { Beat::FromBeats(4), { 3, 4 } }, { Beat::FromBeats(7), { 4, 4 } } });
	verifyConversion("0000,\n5008,\n1000,", Beat::FromBeats(2), Beat::FromBeats(4), false, 1.0, { { Beat::Zero(), { 2, 4 } }, { Beat::FromBeats(2), { 4, 4 } } });
	for (const std::string& scrollCommand : { "", "#BMSCROLL\n", "#HBSCROLL\n" })
	{
		const std::string spanningRoll = scrollCommand + "5000,\n0000,\n0008,\n1000,";
		verifyConversion(spanningRoll, Beat::FromBeats(7), Beat::FromBeats(9), false, 1.0, { { Beat::FromBeats(4), { 3, 4 } }, { Beat::FromBeats(10), { 4, 4 } } });
		verifyConversion(spanningRoll, Beat::FromBeats(7), Beat::FromBeats(9), true, 1.0, { { Beat::FromBeats(4), { 6, 4 } }, { Beat::FromBeats(10), { 4, 4 } } });
	}
	verifyConversion("#HBSCROLL\n5000,\n0\n#DELAY 0.25\n0\n#BPMCHANGE 240\n0\n#DELAY 0.125\n8,\n1000,", Beat::FromBeats(5), Beat::FromBeats(7), false, 1.125, { { Beat::FromBeats(4), { 2, 4 } }, { Beat::FromBeats(6), { 4, 4 } } });
	const std::string crossingBars = "1000,\n0000,\n0000,\n2000,";
	verifyConversion(crossingBars, Beat::FromBeats(7), Beat::FromBeats(9), false, 1.0, { { Beat::FromBeats(4), { 3, 4 } }, { Beat::FromBeats(10), { 4, 4 } } });
	verifyConversion(crossingBars, Beat::FromBeats(7), Beat::FromBeats(9), true, 1.0, { { Beat::FromBeats(4), { 6, 4 } }, { Beat::FromBeats(10), { 4, 4 } } });
	verifyConversion("1000,\n00\n#BPMCHANGE 240\n00,\n2000,\n#BPMCHANGE 180\n3000,", Beat::FromBeats(5), Beat::FromBeats(7), false, 0.75, { { Beat::FromBeats(4), { 2, 4 } }, { Beat::FromBeats(6), { 4, 4 } } });
	verifyConversion("1000,\n0\n#DELAY 0.25\n0\n#DELAY 0.5\n0\n#DELAY 0.125\n0,\n2000,", Beat::FromBeats(4), Beat::FromBeats(7), false, 2.375, { { Beat::FromBeats(4), { 1, 4 } }, { Beat::FromBeats(5), { 4, 4 } } });
	verifyConversion("1000,\n0000,\n#DELAY -0.25\n#BPMCHANGE 240\n#DELAY -0.125\n#BPMCHANGE 180\n2000,", Beat::FromBeats(5), Beat::FromBeats(6), false, 0.5, { { Beat::FromBeats(4), { 3, 4 } }, { Beat::FromBeats(7), { 4, 4 } } });
	verifyConversion("1000,\n0000,\n#MEASURE 3/4\n000,\n200,\n#MEASURE 4/4\n3000,", Beat::FromBeats(5), Beat::FromBeats(11), false, 3.0, { { Beat::FromBeats(4), { 1, 4 } }, { Beat::FromBeats(5), { 3, 4 } } });
	verifyConversion("0000,\n2000,", Beat::Zero(), Beat::FromBeats(4), false, 2.0, {});
	verifyConversion("0000,", Beat::Zero(), Beat::FromBeats(4), false, 2.0, {});
	const auto verifyRejected = [&](const std::string& body, Beat start, Beat end, DelayConversionError error)
	{
		ChartProject rejected; parse(body, rejected);
		const auto plan = BuildDelayConversionPlan(*rejected.Courses[0], start, end);
		check(plan.Error == error, "Unsafe ranges are rejected with a specific reason");
		check(plan.RemoveItems.empty() && plan.AddItems.empty(), "Rejected conversions do not prepare chart mutations");
	};
	verifyRejected("1000,", Beat::Zero(), Beat::FromBeats(1), DelayConversionError::Note);
	verifyRejected("0020,", Beat::FromBeats(1), Beat::FromBeats(3), DelayConversionError::Note);
	verifyRejected("5008,", Beat::FromBeats(1), Beat::FromBeatsFraction(3.5), DelayConversionError::LongNote);
	verifyRejected("5008,", Beat::FromBeats(3), Beat::FromBeatsFraction(3.5), DelayConversionError::LongNote);
	verifyRejected("5008,", Beat::Zero(), Beat::FromBeats(1), DelayConversionError::Note);
	verifyRejected("0508,", Beat::Zero(), Beat::FromBeats(2), DelayConversionError::Note);
	verifyRejected("7008,", Beat::FromBeats(1), Beat::FromBeatsFraction(3.5), DelayConversionError::LongNote);
	verifyRejected("0\n#SCROLL 2\n000,", Beat::FromBeats(1), Beat::FromBeats(2), DelayConversionError::Event);
	verifyRejected("00\n#SCROLL 2\n00,", Beat::FromBeats(1), Beat::FromBeats(2), DelayConversionError::Event);
	verifyRejected("00\n#HBSCROLL\n00,", Beat::FromBeats(1), Beat::FromBeats(2), DelayConversionError::Event);
	verifyRejected("#GOGOSTART\n0000,\n#GOGOEND\n2000,", Beat::FromBeats(3), Beat::FromBeats(4), DelayConversionError::Event);
	verifyRejected("00\n#DELAY -0.25\n00,", Beat::FromBeats(1), Beat::FromBeats(2), DelayConversionError::NegativeDelay);
	verifyRejected("00\n#DELAY 1\n#DELAY -0.25\n00,", Beat::FromBeats(1), Beat::FromBeats(3), DelayConversionError::NegativeDelay);
	verifyRejected("#BRANCHSTART p,50,80\n#N\n0000,\n#E\n0000,\n#M\n0000,\n#BRANCHEND", Beat::FromBeats(1), Beat::FromBeats(2), DelayConversionError::Branches);
	ChartProject activeMovement; parse("0000,", activeMovement);
	activeMovement.Courses[0]->JPOSScrollChanges.InsertOrUpdate({ Beat::Zero(), Complex(100.0f, 0.0f), 2.0f });
	check(BuildDelayConversionPlan(*activeMovement.Courses[0], Beat::FromBeats(1), Beat::FromBeats(2)).Error == DelayConversionError::ActiveJPOS, "Active JPOS movement is detected even when its command precedes the range");
	check(BuildDelayConversionPlan(*activeMovement.Courses[0], Beat::FromBeats(1), Beat::FromBeats(1)).Error == DelayConversionError::InvalidRange, "Zero length selections are rejected");
	ChartContext editedSource;
	parse("#DELAY 1\n#DELAY -0.5\n1000,\n00\n#BPMCHANGE 180\n00,\n2000,", editedSource.Chart);
	editedSource.ChartSelectedCourse = editedSource.Chart.Courses[0].get();
	auto& editedSourceCourse = *editedSource.ChartSelectedCourse;
	editedSourceCourse.TempoMap.Tempo.TryFindExactAtBeat(Beat::FromBeats(6))->Tempo = Tempo(240.0f);
	editedSourceCourse.TempoMap.RebuildAccelerationStructure();
	check(!editedSourceCourse.CanUseSourceTiming(), "Editing BPM invalidates original intermediate DELAY source commands");
	const double originalScroll = editedSourceCourse.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::Zero(), editedSourceCourse.BeatToPlaybackTime(Beat::Zero()));
	editedSource.RangeSelection = { Beat::FromBeats(5), Beat::FromBeats(7), true, true };
	auto editedPlan = BuildDelayConversionPlan(editedSourceCourse, Beat::FromBeats(5), Beat::FromBeats(7));
	check(editedPlan.Error == DelayConversionError::None, "Previously edited BPM can be converted");
	editedSource.Undo.Execute<Commands::ConvertRangeToDelay>(&editedSource, std::move(editedPlan));
	check(!editedSourceCourse.CanUseSourceTiming(), "Conversion cannot reactivate previously invalid DELAY source commands");
	check(close(originalScroll, editedSourceCourse.GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::Zero(), editedSourceCourse.BeatToPlaybackTime(Beat::Zero()))), "Conversion preserves the current rewind behavior before the selected range");
	ChartProject editedSourceRestored; roundTrip(editedSource.Chart, editedSourceRestored);
	check(close(originalScroll, editedSourceRestored.Courses[0]->GetPlaybackTiming().ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat::Zero(), editedSourceRestored.Courses[0]->BeatToPlaybackTime(Beat::Zero()))), "Previously invalid rewind commands stay inactive through export");
	editedSource.Undo.Undo();
	check(editedSourceCourse.SourceTempoChangeCount == 2 && !editedSourceCourse.CanUseSourceTiming(), "Undo restores the original invalid source timing state");
	check(Settings.Input.Timeline_ConvertRangeToDelay->Count == 0, "DELAY conversion shortcut is initially unassigned");
	std::cout << (failures ? "DELAY tests failed" : "DELAY tests passed") << '\n';
	return failures ? 1 : 0;
}
