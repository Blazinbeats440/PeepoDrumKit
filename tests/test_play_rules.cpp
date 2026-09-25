#include "peepo_drum_kit/chart_editor_test_play_rules.h"
#include <iostream>
#include <unordered_map>

using namespace PeepoDrumKit;

int main()
{
	int failures = 0;
	auto check = [&](bool condition, const char* name)
	{
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; ++failures; }
	};

	check(GetTestPlayJudgement(25.0, 25, 75, 108) == 1, "Good upper boundary");
	check(GetTestPlayJudgement(25.001, 25, 75, 108) == 2, "After Good boundary");
	check(GetTestPlayJudgement(75.0, 25, 75, 108) == 2, "Ok upper boundary");
	check(GetTestPlayJudgement(75.001, 25, 75, 108) == 3, "After Ok boundary");
	check(GetTestPlayJudgement(108.0, 25, 75, 108) == 3, "Bad upper boundary");
	check(GetTestPlayJudgement(108.001, 25, 75, 108) == 0, "Outside Bad boundary");
	check(GetTestPlayJudgement(12.5 / 0.5, 25, 75, 108) == 1, "Half-speed real-time window");
	check(GetTestPlayJudgement(37.5 / 0.5, 25, 75, 108) == 2, "Half-speed Ok window");

	const TestPlayInterval<int> selectedRange { 1000, 2000 };
	check(selectedRange.IsValid() && selectedRange.Contains(1000) && selectedRange.Contains(2000), "Selected range endpoints");
	check(!selectedRange.Contains(999) && !selectedRange.Contains(2001), "Notes outside selected range");
	check(selectedRange.Contains(1200, 1800), "Long note inside selected range");
	check(!selectedRange.Contains(900, 1200) && !selectedRange.Contains(1800, 2100), "Long note crossing a boundary");
	check(IsTestPlayLongNoteActive(2000, 1800, 2100, selectedRange), "Long note plays through inclusive range end");
	check(!IsTestPlayLongNoteActive(2001, 1800, 2100, selectedRange), "Long note stops after range end");
	check(!IsTestPlayLongNoteActive(1100, 900, 1200, selectedRange), "Long note starting before range is excluded");
	const TestPlayInterval<int> afterBackwardSeek { 500, 2000 };
	check(afterBackwardSeek.Contains(750) && !selectedRange.Contains(750), "Backward seek updates visible start");
	check(!TestPlayInterval<int>{ 2000, 2000 }.IsValid(), "Empty selection");
	check(GetTestPlayResumeTime(1500, selectedRange, true) == 1500, "Resume inside selected range");
	check(GetTestPlayResumeTime(500, selectedRange, true) == 1000, "Resume before selected range");
	check(GetTestPlayResumeTime(2500, selectedRange, true) == 1000, "Resume after selected range");
	check(GetTestPlayResumeTime(500, selectedRange, false) == 500, "Resume after clearing range");
	const auto resumedInsideRange = GetTestPlayAttemptInterval(selectedRange, 1500);
	check(!resumedInsideRange.Contains(1200) && resumedInsideRange.Contains(1500), "Only judged notes remain visible after mid-range resume");
	check(!CanResumeTestPlay(true, false, selectedRange, 2000), "Chart end stays paused without a new range");
	check(CanResumeTestPlay(true, true, selectedRange, 2500), "New range can resume from chart end");

	check(!IsTestPlayMissDue(1108, 1000, 108) && IsTestPlayMissDue(1109, 1000, 108), "Miss after window");
	check(!IsTestPlayLoopDue(3108, 2000, 1108) && IsTestPlayLoopDue(3109, 2000, 1108), "Loop after delay");
	check(GetTestPlayPauseAction(true, false, false) == TestPlayPauseAction::Pause, "Pause while playing");
	check(GetTestPlayPauseAction(false, false, true) == TestPlayPauseAction::ResumeWithLeadIn, "Resume at attempt start");
	check(GetTestPlayPauseAction(false, false, false) == TestPlayPauseAction::ResumeAtCursor, "Resume after seek");
	check(GetTestPlayPauseAction(false, true, true) == TestPlayPauseAction::StayAtEnd, "Resume at chart end stays paused");

	struct Result { int Judgement, TimingError; bool WasHit; };
	std::unordered_map<int, Result> results;
	StoreLatestTestPlayResult(results, 7, Result { 3, 0, false });
	StoreLatestTestPlayResult(results, 8, Result { 1, -10, true });
	StoreLatestTestPlayResult(results, 7, Result { 2, +30, true });
	check(results.size() == 2 && results.at(7).Judgement == 2 && results.at(7).TimingError == 30
		&& results.at(7).WasHit && results.at(8).Judgement == 1, "Retry replaces only that note's result");
	const auto stats = CalculateTestPlayStatistics(results);
	check(stats.Good == 1 && stats.Ok == 1 && stats.Bad == 0 && stats.Fast == 1 && stats.Slow == 1,
		"Current attempt counts use latest results");
	check(stats.AverageMs == 10.0 && stats.StandardDeviationMs == 20.0, "Signed average and standard deviation");

	if (failures == 0) std::cout << "Test play rules: all checks passed\n";
	return failures == 0 ? 0 : 1;
}
