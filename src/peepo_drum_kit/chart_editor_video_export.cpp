#include "chart_editor_video_export.h"
#include "chart_editor_test_play_rules.h"
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
				route.DecisionTimes.push_back(course.TempoMap.BeatToTime(GetBranchJudgeCutoff(course, range.GetStart())));
		return route;
	}

	BranchType VideoBranchRoute::GetNoteBranch(const ChartCourse& course, Beat beat) const
	{
		for (size_t index = 0; index < course.Branches.size(); ++index)
			if (beat >= course.Branches[index].GetStart() && beat < GetBranchRangeEnd(course.Branches, course.Branches[index]))
				return index < Branches.size() ? Branches[index] : BranchType::Count;
		return BranchType::Normal;
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
			state.Duration = Max(Time::Zero(), Min(Time::FromSec(0.1), course.TempoMap.BeatToTime(course.Branches[index].GetStart()) - state.DecisionTime));
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
					const Time sectionTime = course.TempoMap.BeatToTime(sectionBeat), cutoffTime = course.TempoMap.BeatToTime(decision);
					for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
					for (const Note& note : course.GetNotes(branch))
					{
						if (out.GetNoteBranch(course, note.BeatTime) != branch) continue;
						if (IsComboNote(note.Type) && note.BeatTime >= sectionBeat && note.BeatTime < decision) ++total;
						if (IsLongNote(note.Type)) ForEachVideoRollHit(course, note, rollsPerSecond, [&](Time time, b8)
						{ if (time >= sectionTime && time < cutoffTime) ++rolls; });
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
					const Time sectionTime = course.TempoMap.BeatToTime(sectionBeat), cutoffTime = course.TempoMap.BeatToTime(decision);
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

	b8 RunVideoBranchSelfTest(std::string& error)
	{
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
