#include "chart_editor_video_export.h"
#include "chart_editor_test_play_rules.h"
#include "chart_editor_widgets.h"
#include <cmath>

namespace PeepoDrumKit
{
	Beat GetBranchJudgeCutoff(const ChartCourse& course, Beat branchBeat)
	{
		// The measure immediately before the branch is excluded from judgement.
		Beat previous = Beat::Zero();
		course.TempoMap.ForEachBeatBar([&](const SortedTempoMap::ForEachBeatBarData& bar)
		{
			if (bar.Beat >= branchBeat) return ControlFlow::Break;
			if (bar.IsBar) previous = bar.Beat;
			return ControlFlow::Continue;
		});
		return previous;
	}

	VideoBranchRoute BuildFixedVideoBranchRoute(const ChartCourse& course, BranchType branch)
	{
		VideoBranchRoute route;
		route.InitialBranch = branch == BranchType::Count || course.Branches.empty() ? BranchType::Normal : branch;
		route.Branches.assign(course.Branches.size(), branch);
		if (branch == BranchType::Count)
			for (const BranchRange& range : course.Branches)
				route.DecisionTimes.push_back(course.BeatToPlaybackTime(GetBranchJudgeCutoff(course, range.GetStart()), BranchType::Normal));
		return route;
	}

	BranchType VideoBranchRoute::GetNoteBranch(const ChartCourse& course, Beat beat) const
	{
		for (size_t index = 0; index < course.Branches.size(); ++index)
			if (beat >= course.Branches[index].GetStart() && beat < GetBranchRangeEnd(course.Branches, course.Branches[index]))
				return index < Branches.size() ? Branches[index] : BranchType::Count;
		return BranchType::Normal;
	}

	void PlaybackScrollTimeline::Rebuild(const ChartCourse& course, const VideoBranchRoute& route, BranchType pendingBranch,
		size_t decidedBranches, size_t transitionBranch)
	{
		Changes.Sorted.clear();
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		for (const ScrollChange& change : course.GetScrollChanges(branch))
		{
			const b8 hasSource = change.SourceCommandOrder >= 0 && change.SourceBeat == change.BeatTime
				&& (!change.SourceIsCommon || change.SourceScrollSpeed == change.ScrollSpeed);
			if (hasSource && change.SourceIsCommon)
			{
				const b8 alreadyAdded = std::any_of(Changes.Sorted.begin(), Changes.Sorted.end(), [&](const ScrollChange& previous)
				{
					return previous.SourceIsCommon && previous.SourceCommandOrder == change.SourceCommandOrder
						&& previous.BeatTime == change.BeatTime && previous.SourceScrollSpeed == previous.ScrollSpeed;
				});
				if (!alreadyAdded) Changes.Sorted.push_back(change);
				continue;
			}
			size_t branchIndex = SIZE_MAX;
			for (size_t index = 0; index < course.Branches.size(); ++index)
			{
				const BranchRange& range = course.Branches[index];
				const Beat end = GetBranchRangeEnd(course.Branches, range);
				if (hasSource && range.GetStart() == change.SourceBranchStart && change.BeatTime >= range.GetStart() && change.BeatTime <= end)
				{
					branchIndex = index;
					break;
				}
			}
			if (branchIndex == SIZE_MAX)
				for (size_t index = 0; index < course.Branches.size(); ++index)
					if (change.BeatTime >= course.Branches[index].GetStart() && change.BeatTime < GetBranchRangeEnd(course.Branches, course.Branches[index]))
					{
						branchIndex = index;
						break;
					}
			const BranchType chosen = branchIndex == SIZE_MAX ? BranchType::Normal
				: pendingBranch != BranchType::Count && (branchIndex >= decidedBranches || branchIndex == transitionBranch) ? pendingBranch
				: branchIndex < route.Branches.size() ? route.Branches[branchIndex] : BranchType::Count;
			if (chosen == branch) Changes.Sorted.push_back(change);
		}
		std::stable_sort(Changes.Sorted.begin(), Changes.Sorted.end(), [](const ScrollChange& left, const ScrollChange& right)
		{
			if (left.BeatTime != right.BeatTime) return left.BeatTime < right.BeatTime;
			const i32 leftOrder = left.SourceBeat == left.BeatTime && (!left.SourceIsCommon || left.SourceScrollSpeed == left.ScrollSpeed) && left.SourceCommandOrder >= 0 ? left.SourceCommandOrder : I32Max;
			const i32 rightOrder = right.SourceBeat == right.BeatTime && (!right.SourceIsCommon || right.SourceScrollSpeed == right.ScrollSpeed) && right.SourceCommandOrder >= 0 ? right.SourceCommandOrder : I32Max;
			return leftOrder < rightOrder;
		});
	}

	Complex PlaybackScrollTimeline::GetScroll(Beat beat) const
	{
		const auto after = std::upper_bound(Changes.Sorted.begin(), Changes.Sorted.end(), beat,
			[](Beat value, const ScrollChange& change) { return value < change.BeatTime; });
		return after == Changes.Sorted.begin() ? Complex(1.0f, 0.0f) : (after - 1)->ScrollSpeed;
	}

	VideoBranchVisualState VideoBranchRoute::GetVisualState(const ChartCourse& course, Time time) const
	{
		VideoBranchVisualState state;
		state.Branch = state.From = InitialBranch;
		if (!Animate) return state;
		for (size_t index = 0; index < Branches.size() && index < DecisionTimes.size() && index < course.Branches.size(); ++index)
		{
			if (DecisionTimes[index] > time) break;
			if (Branches[index] == BranchType::Count || state.Branch == Branches[index]) continue;
			state.From = state.Branch;
			state.Branch = Branches[index];
			state.Index = index;
			state.DecisionTime = DecisionTimes[index];
			state.Duration = Max(Time::Zero(), Min(Time::FromSec(0.1), course.BeatToPlaybackTime(course.Branches[index].GetStart(), state.Branch) - state.DecisionTime));
			if (index + 1 < DecisionTimes.size()) state.Duration = Min(state.Duration, DecisionTimes[index + 1] - state.DecisionTime);
		}
		return state;
	}

	BranchType VideoBranchRoute::GetDisplayBranch(const ChartCourse& course, Beat beat, Time time) const
	{
		for (size_t index = 0; index < course.Branches.size(); ++index)
			if (beat >= course.Branches[index].GetStart() && beat < GetBranchRangeEnd(course.Branches, course.Branches[index]))
			{
				if (Animate && index < DecisionTimes.size() && time < DecisionTimes[index]) return GetVisualState(course, time).Branch;
				return index < Branches.size() ? Branches[index] : BranchType::Count;
			}
		return BranchType::Normal;
	}

	b8 BuildAutoVideoBranchRoute(const ChartCourse& course, f32 rollsPerSecond, VideoBranchRoute& out)
	{
		out = BuildFixedVideoBranchRoute(course, BranchType::Count);
		out.Animate = true;
		if (std::any_of(course.Branches.begin(), course.Branches.end(), [](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; })) return false;
		std::vector<Beat> cutoffs;
		for (const BranchRange& range : course.Branches) cutoffs.push_back(GetBranchJudgeCutoff(course, range.GetStart()));
		auto holds = course.BranchLevelHolds;
		std::sort(holds.begin(), holds.end(), [](const BranchLevelHold& left, const BranchLevelHold& right) { return left.BeatTime < right.BeatTime; });
		auto sections = course.BranchSections;
		std::sort(sections.begin(), sections.end());
		size_t decisionIndex = 0, branchIndex = 0, sectionIndex = 0, holdIndex = 0;
		Beat sectionBeat = Beat::Zero();
		BranchType currentBranch = BranchType::Normal;
		b8 held = false;
		const Beat infinity = Beat::FromTicks(I32Max);
		while (true)
		{
			const Beat decision = decisionIndex < cutoffs.size() ? cutoffs[decisionIndex] : infinity;
			const Beat start = branchIndex < course.Branches.size() ? course.Branches[branchIndex].GetStart() : infinity;
			const Beat section = sectionIndex < sections.size() ? sections[sectionIndex] : infinity;
			const Beat hold = holdIndex < holds.size() ? holds[holdIndex].BeatTime : infinity;
			const Beat next = std::min({ decision, start, section, hold });
			if (next == infinity) break;
			if (decision == next && (start != next || decisionIndex <= branchIndex))
			{
				BranchType chosen = currentBranch;
				if (!held)
				{
					i32 total = 0, rolls = 0;
					const Time sectionTime = course.BeatToPlaybackTime(sectionBeat, currentBranch), cutoffTime = course.BeatToPlaybackTime(decision, currentBranch);
					for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
					for (const Note& note : course.GetNotes(branch))
					{
						if (out.GetNoteBranch(course, note.BeatTime) != branch) continue;
						if (IsComboNote(note.Type) && note.BeatTime >= sectionBeat && note.BeatTime < decision) ++total;
						if (IsLongNote(note.Type)) ForEachVideoRollHit(course, note, rollsPerSecond, [&](Time time, b8)
						{ if (time >= sectionTime && time < cutoffTime) ++rolls; }, branch);
					}
					const BranchRange& range = course.Branches[decisionIndex];
					const f64 value = range.Condition == TJA::BranchCondition::Roll ? static_cast<f64>(rolls) : GetTestPlayBranchAccuracy(total, 0, total);
					chosen = static_cast<BranchType>(GetTestPlayBranchIndex(value, range.RequirementExpert, range.RequirementMaster));
				}
				out.Branches[decisionIndex++] = chosen;
			}
			else if (start == next) currentBranch = out.Branches[branchIndex++];
			else if (section == next) sectionBeat = sections[sectionIndex++];
			else
			{
				const BranchLevelHold& levelHold = holds[holdIndex++];
				if (levelHold.Branch == currentBranch && std::any_of(course.Branches.begin(), course.Branches.end(), [&](const BranchRange& range)
					{ return hold >= range.GetStart() && hold < GetBranchRangeEnd(course.Branches, range); })) held = true;
			}
		}
		return true;
	}

	b8 BuildTestPlayVideoBranchRoute(const ChartCourse& course, const VideoBranchTestPlayResult& result, VideoBranchRoute& out)
	{
		out = BuildFixedVideoBranchRoute(course, BranchType::Count);
		out.Animate = true;
		if (std::any_of(course.Branches.begin(), course.Branches.end(), [](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; })) return false;
		std::vector<Beat> cutoffs;
		for (const BranchRange& range : course.Branches) cutoffs.push_back(GetBranchJudgeCutoff(course, range.GetStart()));
		auto holds = course.BranchLevelHolds;
		std::sort(holds.begin(), holds.end(), [](const BranchLevelHold& left, const BranchLevelHold& right) { return left.BeatTime < right.BeatTime; });
		auto sections = course.BranchSections;
		std::sort(sections.begin(), sections.end());
		size_t decisionIndex = 0, branchIndex = 0, sectionIndex = 0, holdIndex = 0;
		Beat sectionBeat = Beat::Zero();
		BranchType currentBranch = BranchType::Normal;
		b8 held = false;
		const Beat infinity = Beat::FromTicks(I32Max);
		while (true)
		{
			const Beat decision = decisionIndex < cutoffs.size() ? cutoffs[decisionIndex] : infinity;
			const Beat start = branchIndex < course.Branches.size() ? course.Branches[branchIndex].GetStart() : infinity;
			const Beat section = sectionIndex < sections.size() ? sections[sectionIndex] : infinity;
			const Beat hold = holdIndex < holds.size() ? holds[holdIndex].BeatTime : infinity;
			const Beat next = std::min({ decision, start, section, hold });
			if (next == infinity) break;
			if (decision == next && (start != next || decisionIndex <= branchIndex))
			{
				BranchType chosen = currentBranch;
				const auto recordedDecision = std::find_if(result.Decisions.begin(), result.Decisions.end(), [decisionIndex](const auto& item) { return item.first == decisionIndex; });
				if (recordedDecision != result.Decisions.end()) chosen = recordedDecision->second;
				else if (!held)
				{
					i32 good = 0, ok = 0, total = 0, rolls = 0;
					const Time sectionTime = course.BeatToPlaybackTime(sectionBeat, currentBranch), cutoffTime = course.BeatToPlaybackTime(decision, currentBranch);
					for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
					for (const Note& note : course.GetNotes(branch))
					{
						if (out.GetNoteBranch(course, note.BeatTime) != branch) continue;
						if (IsComboNote(note.Type) && note.BeatTime >= sectionBeat && note.BeatTime < decision)
						{
							++total;
							const auto judgement = std::find_if(result.Judgements.begin(), result.Judgements.end(), [&note](const auto& item) { return item.first == &note; });
							if (judgement != result.Judgements.end())
							{
								good += judgement->second == 1;
								ok += judgement->second == 2;
							}
						}
						if (IsLongNote(note.Type))
						{
							const auto hits = std::find_if(result.LongNoteHitTimes.begin(), result.LongNoteHitTimes.end(), [&note](const auto& item) { return item.first == &note; });
							if (hits != result.LongNoteHitTimes.end())
								for (Time time : hits->second) if (time >= sectionTime && time < cutoffTime) ++rolls;
						}
					}
					const BranchRange& range = course.Branches[decisionIndex];
					const f64 value = range.Condition == TJA::BranchCondition::Roll ? static_cast<f64>(rolls) : GetTestPlayBranchAccuracy(good, ok, total);
					chosen = static_cast<BranchType>(GetTestPlayBranchIndex(value, range.RequirementExpert, range.RequirementMaster));
				}
				out.Branches[decisionIndex++] = chosen;
			}
			else if (start == next) currentBranch = out.Branches[branchIndex++];
			else if (section == next) sectionBeat = sections[sectionIndex++];
			else
			{
				const BranchLevelHold& levelHold = holds[holdIndex++];
				if (levelHold.Branch == currentBranch && std::any_of(course.Branches.begin(), course.Branches.end(), [&](const BranchRange& range)
					{ return hold >= range.GetStart() && hold < GetBranchRangeEnd(course.Branches, range); })) held = true;
			}
		}
		return true;
	}

	static b8 RunPlaybackScrollSelfTest(std::string& error)
	{
		auto fail = [&](cstr message) { error = message; return false; };
		const std::string source =
			"TITLE:Playback Scroll Test\nBPM:120\nCOURSE:Oni\n#START\n#SCROLL 2\n0000,\n"
			"#BRANCHSTART p,70,80\n#N\n1111,\n#E\n1111,\n#M\n#SCROLL 3\n1111,\n"
			"#BRANCHSTART p,70,80\n#N\n1111,\n#E\n#SCROLL 2\n1111,\n#M\n1111,\n"
			"#BRANCHEND\n1111,\n#SCROLL 4\n1111,\n#SCROLL -2+1i\n1111,\n#END\n";
		TJA::ErrorList parseErrors;
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(source)), parseErrors);
		ChartProject project;
		if (!parseErrors.Errors.empty() || !CreateChartProjectFromTJA(parsed, project) || project.Courses.size() != 1)
			return fail("Playback scroll fixture failed to import");
		ChartCourse& course = *project.Courses[0];
		PlaybackScrollTimeline timeline;
		VideoBranchRoute route = BuildFixedVideoBranchRoute(course, BranchType::Normal);
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			route = BuildFixedVideoBranchRoute(course, branch);
			timeline.Rebuild(course, route);
			if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(branch == BranchType::Master ? 3.0f : 2.0f, 0.0f))
				return fail("First branch did not inherit common SCROLL or apply the selected route's command");
		}
		route.Branches = { BranchType::Master, BranchType::Normal };
		timeline.Rebuild(course, route);
		if (timeline.GetScroll(Beat::FromBeats(-1)) != Complex(1.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(0)) != Complex(2.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(4)) != Complex(3.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(12)) != Complex(3.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(16)) != Complex(4.0f, 0.0f)
			|| timeline.GetScroll(Beat::FromBeats(20)) != Complex(-2.0f, 1.0f))
			return fail("Playback SCROLL was reset by branching, BRANCHEND, or lost complex scroll values");
		if (ScrollOrDefault(course.ScrollChanges_Normal.TryFindLastAtBeat(Beat::FromBeats(8))) != Complex(2.0f, 0.0f))
			return fail("Playback scroll reconstruction modified the editor's per-branch scroll data");
		TJA::ParsedTJA saved;
		if (!ConvertChartProjectToTJA(project, saved, false)) return fail("Playback SCROLL fixture could not be exported");
		std::string savedText;
		TJA::ConvertParsedToText(saved, savedText, TJA::SaveFormat::Current);
		std::string_view savedView = savedText;
		if (UTF8::HasBOM(savedView)) savedView = UTF8::TrimBOM(savedView);
		TJA::ErrorList savedErrors;
		const auto restored = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(savedView)), savedErrors);
		ChartProject restoredProject;
		if (!savedErrors.Errors.empty() || !CreateChartProjectFromTJA(restored, restoredProject))
			return fail("Exported playback SCROLL fixture could not be imported again");
		timeline.Rebuild(*restoredProject.Courses[0], route);
		if (timeline.GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f) || timeline.GetScroll(Beat::FromBeats(16)) != Complex(4.0f, 0.0f))
			return fail("Playback SCROLL inheritance changed after a TJA round trip");
		ChartCourse editedCourse = course;
		editedCourse.ScrollChanges_Master.Sorted.insert(editedCourse.ScrollChanges_Master.Sorted.begin() + 2,
			ScrollChange { Beat::FromBeats(6), Complex(3.5f, 0.0f) });
		timeline.Rebuild(editedCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(3.0f, 0.0f) || timeline.GetScroll(Beat::FromBeats(7)) != Complex(3.5f, 0.0f))
			return fail("Long note head and tail did not resolve their own playback SCROLL values");
		ScrollChange movedCommon = course.ScrollChanges_Normal.Sorted[0];
		movedCommon.BeatTime = Beat::FromBeats(5);
		movedCommon.ScrollSpeed = Complex(7.0f, 0.0f);
		editedCourse.ScrollChanges_Normal.Sorted.push_back(movedCommon);
		timeline.Rebuild(editedCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(5)) != Complex(3.0f, 0.0f))
			return fail("A moved SCROLL command incorrectly retained its imported common scope");
		route.Branches[1] = BranchType::Expert;
		timeline.Rebuild(course, route);
		if (timeline.GetScroll(Beat::FromBeats(8)) != Complex(2.0f, 0.0f))
			return fail("An explicit repeated SCROLL value did not overwrite the inherited value");
		VideoBranchTestPlayResult recording;
		recording.Decisions = { { 0, BranchType::Master }, { 1, BranchType::Normal } };
		if (!BuildTestPlayVideoBranchRoute(course, recording, route)) return fail("Recorded scroll route could not be built");
		timeline.Rebuild(course, route);
		if (timeline.GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f))
			return fail("Recorded test play video lost the carried SCROLL value");
		ChartGamePreview preview;
		preview.TestPlayCourse = &course;
		preview.TestPlayAutoBranchActive = true;
		preview.ResetTestPlayState(Time::Zero());
		preview.TestPlayBranchDecisions = { { 0, BranchType::Master } };
		preview.TestPlayNextDecisionIndex = 1;
		preview.RebuildTestPlayScrolls();
		if (preview.TestPlayScrolls[EnumToIndex(BranchType::Normal)].GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f)
			|| preview.TestPlayScrolls[EnumToIndex(BranchType::Expert)].GetScroll(Beat::FromBeats(8)) != Complex(2.0f, 0.0f))
			return fail("Undecided test play branch previews did not use the confirmed SCROLL history");
		preview.TestPlayBranchDecisions.emplace_back(1, BranchType::Normal);
		preview.TestPlayNextDecisionIndex = 2;
		preview.RebuildTestPlayScrolls();
		preview.ResetTestPlayState(course.TempoMap.BeatToTime(Beat::FromBeats(10)));
		if (preview.TestPlayBranchDecisions.size() != 2
			|| preview.TestPlayScrolls[0].GetScroll(Beat::FromBeats(12)) != Complex(3.0f, 0.0f))
			return fail("Test play resume lost previously visited branch SCROLL commands");
		preview.ResetTestPlayState(course.TempoMap.BeatToTime(Beat::FromBeats(4)));
		if (!preview.TestPlayBranchDecisions.empty() || preview.TestPlayScrolls[0].GetScroll(Beat::FromBeats(4)) != Complex(2.0f, 0.0f))
			return fail("Test play retry retained future SCROLL commands from the previous attempt");
		preview.VideoExportRoute = &route;
		preview.UpdateVideoScrolls(course, Time::FromSec(1.0));
		if (preview.VideoExportScrolls[0].GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f)
			|| preview.VideoExportScrolls[1].GetScroll(Beat::FromBeats(8)) != Complex(2.0f, 0.0f)
			|| preview.VideoExportTransitionScrolls[0].GetScroll(Beat::FromBeats(4)) != Complex(2.0f, 0.0f))
			return fail("Video lead-in or branch transition used the wrong candidate SCROLL history");
		preview.UpdateVideoScrolls(course, Time::FromSec(3.0));
		if (preview.VideoExportScrolls[1].GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f))
			return fail("Video playback did not replace tentative SCROLL with the confirmed route");
		preview.UpdateVideoScrolls(course, Time::FromSec(1.0));
		if (preview.VideoExportScrolls[1].GetScroll(Beat::FromBeats(8)) != Complex(2.0f, 0.0f))
			return fail("Video rewind did not reconstruct the earlier SCROLL preview");
		route.Branches[1] = BranchType::Master;
		preview.VideoScrollDecisionCount = SIZE_MAX;
		preview.UpdateVideoScrolls(course, Time::FromSec(3.0));
		if (preview.VideoExportTransitionScrolls[0].GetScroll(Beat::FromBeats(4)) != Complex(2.0f, 0.0f))
			return fail("A repeated branch decision replaced the actual SCROLL transition source");
		ChartCourse autoCourse = course;
		autoCourse.Branches[0].RequirementExpert = -2; autoCourse.Branches[0].RequirementMaster = -1;
		autoCourse.Branches[1].RequirementExpert = 101; autoCourse.Branches[1].RequirementMaster = 102;
		if (!BuildAutoVideoBranchRoute(autoCourse, 0.0f, route)
			|| route.Branches != std::vector<BranchType>{ BranchType::Master, BranchType::Normal })
			return fail("Auto SCROLL route did not follow forced branch thresholds");
		timeline.Rebuild(autoCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(8)) != Complex(3.0f, 0.0f))
			return fail("Auto video route did not retain a previous branch's SCROLL");
		const std::string boundarySource =
			"TITLE:Scroll Boundary Test\nBPM:120\nCOURSE:Oni\n#START\n#SCROLL 2\n"
			"#BRANCHSTART p,70,80\n#N\n1111,\n#E\n1111,\n#M\n#SCROLL 3\n1111,\n#SCROLL 6\n"
			"#BRANCHEND\n#SCROLL 4\n#BRANCHSTART p,70,80\n#N\n1111,\n#E\n1111,\n#M\n#SCROLL 5\n1111,\n"
			"#BRANCHEND\n1111,\n#END\n";
		parseErrors = {};
		const auto boundaryParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(boundarySource)), parseErrors);
		ChartProject boundaryProject;
		if (!parseErrors.Errors.empty() || !CreateChartProjectFromTJA(boundaryParsed, boundaryProject))
			return fail("Boundary SCROLL fixture failed to import");
		ChartCourse& boundaryCourse = *boundaryProject.Courses[0];
		route = BuildFixedVideoBranchRoute(boundaryCourse, BranchType::Normal);
		route.Branches[0] = BranchType::Master;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::Zero()) != Complex(3.0f, 0.0f) || timeline.GetScroll(Beat::FromBeats(4)) != Complex(4.0f, 0.0f))
			return fail("Common and branch SCROLL commands at the same beat were reordered");
		route.Branches[1] = BranchType::Master;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(5.0f, 0.0f))
			return fail("Selected branch SCROLL did not run after a common command at the same beat");
		for (auto& changes : { &boundaryCourse.ScrollChanges_Normal, &boundaryCourse.ScrollChanges_Expert, &boundaryCourse.ScrollChanges_Master })
			erase_remove_if(changes->Sorted, [](const ScrollChange& change) { return change.SourceIsCommon && change.ScrollSpeed == Complex(4.0f, 0.0f); });
		route.Branches[1] = BranchType::Normal;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(6.0f, 0.0f))
			return fail("SCROLL immediately before BRANCHEND was attributed to the next branch");
		route.Branches[0] = BranchType::Normal;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(2.0f, 0.0f))
			return fail("SCROLL at an unselected branch end leaked into the playback route");
		ScrollChange& editedCommon = boundaryCourse.ScrollChanges_Master.Sorted[0];
		editedCommon.ScrollSpeed = Complex(7.0f, 0.0f);
		route.Branches[0] = BranchType::Master;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::Zero()) != Complex(7.0f, 0.0f))
			return fail("An edited common SCROLL value did not apply to its selected branch");
		route.Branches[0] = BranchType::Normal;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::Zero()) != Complex(2.0f, 0.0f))
			return fail("An edited common SCROLL value leaked into an unselected branch");
		boundaryCourse.ScrollChanges_Normal.Sorted[0].ScrollSpeed = Complex(9.0f, 0.0f);
		route.Branches[0] = BranchType::Expert;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::Zero()) != Complex(2.0f, 0.0f))
			return fail("An edited Normal SCROLL discarded the other branches' common command");
		route.Branches[0] = BranchType::Normal;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::Zero()) != Complex(9.0f, 0.0f))
			return fail("An edited Normal SCROLL was overwritten by an unchanged common copy");
		for (ScrollChange& change : boundaryCourse.ScrollChanges_Master)
			if (!change.SourceIsCommon && change.BeatTime == Beat::FromBeats(4)) change.ScrollSpeed = Complex(8.0f, 0.0f);
		route.Branches[0] = BranchType::Master;
		timeline.Rebuild(boundaryCourse, route);
		if (timeline.GetScroll(Beat::FromBeats(4)) != Complex(8.0f, 0.0f))
			return fail("Editing a branch-end SCROLL moved it into the next branch");
		return true;
	}

	b8 RunVideoBranchSelfTest(std::string& error)
	{
		if (!RunPlaybackScrollSelfTest(error)) return false;
		auto fail = [&](cstr message) { error = message; return false; };
		ChartCourse course;
		course.TempoMap.Tempo.Sorted = { TempoChange(Beat::Zero(), Tempo(120.0f)) };
		course.TempoMap.Signature.Sorted = { TimeSignatureChange(Beat::Zero(), TimeSignature(4, 4)) };
		course.TempoMap.RebuildAccelerationStructure();
		auto makeNote = [](i32 beat, NoteType type, i32 duration = 0, i32 balloonCount = 0)
		{
			Note note {};
			note.BeatTime = Beat::FromBeats(beat);
			note.BeatDuration = Beat::FromBeats(duration);
			note.Type = type;
			note.BalloonPopCount = balloonCount;
			return note;
		};
		course.Notes_Normal.Sorted = { makeNote(0, NoteType::Don), makeNote(4, NoteType::Drumroll, 2), makeNote(8, NoteType::Ka), makeNote(20, NoteType::Don) };
		course.Notes_Expert.Sorted = { makeNote(8, NoteType::Ka), makeNote(16, NoteType::Ka) };
		course.Notes_Master.Sorted = { makeNote(8, NoteType::Drumroll, 2), makeNote(10, NoteType::Balloon, 2, 3), makeNote(14, NoteType::DonBig) };
		course.Branches = {
			{ Beat::FromBars(2), Beat::Zero(), TJA::BranchCondition::Precise, 70, 80, false },
			{ Beat::FromBars(4), Beat::FromBars(1), TJA::BranchCondition::Roll, 9, 12, true }
		};
		ChartGamePreview preview;
		preview.TestPlayCourse = &course;
		preview.TestPlayAutoBranchActive = true;
		Note branchHead = makeNote(8, NoteType::Ka);
		if (preview.IsTestPlayNoteActive(&branchHead, BranchType::Expert))
			return fail("Undecided branch notes were eligible for test play judgement");
		preview.TestPlayBranchDecisions.emplace_back(0, BranchType::Expert);
		preview.TestPlayNextDecisionIndex = 1;
		if (!preview.IsTestPlayNoteActive(&branchHead, BranchType::Expert))
			return fail("Decided branch head could not be judged by an early hit before BRANCHSTART");
		if (preview.IsTestPlayNoteActive(&branchHead, BranchType::Normal)
			|| preview.IsTestPlayNoteActive(&branchHead, BranchType::Master))
			return fail("Unselected branch notes were eligible for test play judgement");
		preview.TestPlayNextBranchIndex = 1;
		if (!preview.IsTestPlayNoteActive(&branchHead, BranchType::Expert))
			return fail("Branch head stopped being eligible at BRANCHSTART");
		Note nextBranchHead = makeNote(16, NoteType::Ka);
		if (preview.IsTestPlayNoteActive(&nextBranchHead, BranchType::Expert))
			return fail("Next undecided branch inherited the previous test play route");
		preview.TestPlayBranchDecisions.emplace_back(1, BranchType::Normal);
		preview.TestPlayNextDecisionIndex = 2;
		if (!preview.IsTestPlayNoteActive(&nextBranchHead, BranchType::Normal)
			|| preview.IsTestPlayNoteActive(&nextBranchHead, BranchType::Expert)
			|| !preview.IsTestPlayNoteActive(&branchHead, BranchType::Expert))
			return fail("Consecutive branch decisions lost the selected test play note routes");
		VideoBranchRoute route;
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches != std::vector<BranchType>{ BranchType::Master, BranchType::Expert })
			return fail("Auto route did not use perfect accuracy and route-specific roll/balloon hits");
		if (route.GetNoteBranch(course, Beat::Zero()) != BranchType::Normal || route.GetNoteBranch(course, Beat::FromBars(5)) != BranchType::Normal)
			return fail("Common notes before/after branching were not normal notes");
		if (route.GetDisplayBranch(course, Beat::FromBars(2), Time::FromSec(1.9)) != BranchType::Normal
			|| route.GetDisplayBranch(course, Beat::FromBars(2), Time::FromSec(2.1)) != BranchType::Master)
			return fail("Branch note display did not switch at the decision time");
		const auto visual = route.GetVisualState(course, Time::FromSec(2.05));
		if (visual.From != BranchType::Normal || visual.Branch != BranchType::Master || std::abs(visual.Duration.ToSec() - 0.1) > 0.00001)
			return fail("Video transition did not use the 0.1 second slide");
		VideoExportComboTimeline combos;
		combos.Rebuild(course, route);
		if (combos.GetMaxCombo() != 4 || combos.GetCurrentCombo(Time::FromSec(8.0)) != 3)
			return fail("Video combo did not follow the chosen route");
		VideoExportSoundTimeline sounds;
		sounds.Rebuild(course, route, 4.0f);
		if (sounds.Events.size() != 15 || std::count_if(sounds.Events.begin(), sounds.Events.end(), [](const VideoExportSoundEvent& event)
			{ return event.Sound == SoundEffectType::Balloon && event.HitTime == Time::FromSec(5.5); }) != 1
			|| std::count_if(sounds.Events.begin(), sounds.Events.end(), [](const VideoExportSoundEvent& event) { return event.Sound == SoundEffectType::TaikoKa; }) != 1)
			return fail("Video sound used the wrong route, roll endpoint, or balloon pop time");
		VideoBranchRecording recording;
		recording.Course = &course; recording.Changes = 7; recording.Route = route;
		route.Branches[0] = BranchType::Normal;
		if (!recording.IsValid(&course, 7) || recording.IsValid(&course, 8) || recording.Route.Branches[0] != BranchType::Master)
			return fail("Recorded route was not an independent snapshot");
		course.Notes_Normal.Sorted.insert(course.Notes_Normal.Sorted.begin() + 1, makeNote(2, NoteType::Ka));
		VideoBranchTestPlayResult partialResult;
		partialResult.Judgements.emplace_back(&course.Notes_Normal.Sorted[0], 1);
		if (!BuildTestPlayVideoBranchRoute(course, partialResult, route)
			|| route.Branches != std::vector<BranchType>{ BranchType::Normal, BranchType::Normal })
			return fail("Partial test play did not treat unplayed notes as misses and unplayed rolls as zero hits");
		partialResult.Decisions.emplace_back(1, BranchType::Expert);
		if (!BuildTestPlayVideoBranchRoute(course, partialResult, route) || route.Branches[1] != BranchType::Expert)
			return fail("Partial test play did not preserve a recorded branch decision");
		course.BranchSections = { Beat::FromBars(2) };
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches[1] != BranchType::Normal)
			return fail("SECTION did not reset the auto roll counter");
		course.BranchLevelHolds = { { Beat::FromBars(2), BranchType::Master } };
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches[1] != BranchType::Master)
			return fail("LEVELHOLD at branch start did not keep the selected branch");
		route.DecisionTimes[0] = Time::FromSec(3.975);
		if (std::abs(route.GetVisualState(course, Time::FromSec(3.98)).Duration.ToSec() - 0.025) > 0.00001)
			return fail("Short branch lead-in did not shorten the slide");
		course.Branches[0].Condition = TJA::BranchCondition::Score;
		if (BuildAutoVideoBranchRoute(course, 4.0f, route)) return fail("Score branching was silently evaluated as accuracy");
		course.BranchLevelHolds.clear(); course.BranchSections.clear();
		course.Branches[0].Condition = TJA::BranchCondition::Precise;
		course.Branches[1].RequirementMaster = 11;
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches[1] != BranchType::Master)
			return fail("An exact roll threshold did not choose the higher branch");
		if (!BuildAutoVideoBranchRoute(course, 0.0f, route) || route.Branches[1] != BranchType::Normal)
			return fail("Zero roll speed generated auto hits");
		course.Notes_Normal.Sorted.clear(); course.Notes_Expert.Sorted.clear(); course.Notes_Master.Sorted.clear();
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches[0] != BranchType::Normal)
			return fail("Empty accuracy interval was incorrectly treated as perfect accuracy");
		course.Branches[0] = CreateForcedBranchRange(Beat::FromBars(2), BranchType::Master);
		if (!BuildAutoVideoBranchRoute(course, 4.0f, route) || route.Branches[0] != BranchType::Master)
			return fail("Zero-length forced branch was not applied");
		error.clear();
		return true;
	}

	static f32 SampleChannel(const Audio::PCMSampleBuffer* source, i64 frame, u32 channel)
	{
		if (!source || !source->InterleavedSamples || source->SampleRate != Audio::Engine.OutputSampleRate ||
			source->ChannelCount == 0 || frame < 0 || frame >= source->FrameCount)
			return 0.0f;
		const u32 sourceChannel = std::min(channel, source->ChannelCount - 1);
		return static_cast<f32>(source->InterleavedSamples[frame * source->ChannelCount + sourceChannel]);
	}

	void RenderVideoExportAudio(const VideoExportAudioSources& sources, const VideoExportSoundTimeline& sounds,
		Time chartStartTime, i64 firstOutputFrame, u32 outputFrameCount, std::vector<i16>& outInterleavedStereo)
	{
		constexpr i64 sampleRate = Audio::Engine.OutputSampleRate;
		std::vector<f32> mixed(static_cast<size_t>(outputFrameCount) * 2, 0.0f);
		const i64 firstSongFrame = static_cast<i64>(std::llround((chartStartTime - sources.SongOffset).ToSec() * sampleRate)) + firstOutputFrame;
		for (u32 frame = 0; frame < outputFrameCount; ++frame)
			for (u32 channel = 0; channel < 2; ++channel)
				mixed[static_cast<size_t>(frame) * 2 + channel] = SampleChannel(sources.Song, firstSongFrame + frame, channel) * sources.SongVolume;

		for (const VideoExportSoundEvent& event : sounds.Events)
		{
			const auto* source = sources.SoundEffects[EnumToIndex(event.Sound)];
			if (!source || !source->InterleavedSamples || source->SampleRate != sampleRate) continue;
			const i64 eventFrame = static_cast<i64>(std::llround((event.HitTime - chartStartTime).ToSec() * sampleRate));
			const i64 firstOverlap = std::max(firstOutputFrame, eventFrame);
			const i64 lastOverlap = std::min(firstOutputFrame + outputFrameCount, eventFrame + source->FrameCount);
			if (firstOverlap >= lastOverlap) continue;
			const f32 volume = event.Sound == SoundEffectType::Balloon ? sources.BalloonVolume : sources.DrumVolume;
			for (i64 frame = firstOverlap; frame < lastOverlap; ++frame)
				for (u32 channel = 0; channel < 2; ++channel)
					mixed[static_cast<size_t>(frame - firstOutputFrame) * 2 + channel] += SampleChannel(source, frame - eventFrame, channel) * volume;
		}

		outInterleavedStereo.resize(mixed.size());
		for (u32 frame = 0; frame < outputFrameCount; ++frame)
		{
			const f64 time = chartStartTime.ToSec() + static_cast<f64>(firstOutputFrame + frame) / sampleRate;
			f64 gain = 1.0;
			if (sources.FadeInSeconds > 0.0f && time < sources.ContentStart.ToSec())
			{
				const f64 progress = std::clamp((time - sources.ContentStart.ToSec() + sources.FadeInSeconds) / (sources.FadeInSeconds * 0.9), 0.0, 1.0);
				gain = 1.0 - (1.0 - progress) * (1.0 - progress);
			}
			if (sources.FadeOutSeconds > 0.0f)
			{
				const f64 progress = std::clamp((time - sources.ContentEnd.ToSec() - sources.FadeOutSeconds * 0.2) /
					std::max(1.0 / sampleRate, sources.FadeOutSeconds * 0.8 - 1.0 / sampleRate), 0.0, 1.0);
				gain *= 1.0 - progress * progress;
			}
			for (u32 channel = 0; channel < 2; ++channel)
			{
				const size_t sample = static_cast<size_t>(frame) * 2 + channel;
				outInterleavedStereo[sample] = static_cast<i16>(std::round(std::clamp(mixed[sample] * gain, -32768.0, 32767.0)));
			}
		}
	}
}
