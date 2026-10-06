#pragma once
#include "chart.h"
#include <set>

namespace PeepoDrumKit
{
	struct PlaybackTimelineSection
	{
		Beat StartBeat, EndBeat;
		Time StartTime, EndTime, Offset;
		size_t Lane = 0;
	};

	struct PlaybackTimelineNote
	{
		Note* OriginalNote;
		Time HeadTime, TailTime;
		size_t SectionIndex;
	};

	struct PlaybackTimelineStop
	{
		Beat BeatTime;
		Time StartTime, EndTime;
		size_t SectionIndex;
	};

	struct PlaybackTimelineData
	{
		std::vector<PlaybackTimelineSection> Sections;
		std::vector<PlaybackTimelineNote> Notes;
		std::vector<PlaybackTimelineStop> Stops;
		Time MinTime = {}, MaxTime = {};
		size_t LaneCount = 1;
	};

	struct PlaybackTimelineLayout
	{
		static constexpr size_t HiddenLane = std::numeric_limits<size_t>::max();
		std::vector<size_t> SectionLanes;
		size_t LaneCount = 0;
	};

	inline void BuildPlaybackTimelineLayout(const PlaybackTimelineData& data, const std::set<Beat>& hiddenSections, PlaybackTimelineLayout& out)
	{
		out.SectionLanes.assign(data.Sections.size(), PlaybackTimelineLayout::HiddenLane);
		out.LaneCount = 0;
		for (size_t index = 0; index < data.Sections.size(); ++index)
		{
			const auto& section = data.Sections[index];
			if (section.StartBeat == section.EndBeat || hiddenSections.find(section.StartBeat) != hiddenSections.end()) continue;
			std::vector<b8> occupied(out.LaneCount, false);
			for (size_t previousIndex = 0; previousIndex < index; ++previousIndex)
			{
				const size_t lane = out.SectionLanes[previousIndex];
				if (lane == PlaybackTimelineLayout::HiddenLane) continue;
				const auto& previous = data.Sections[previousIndex];
				if (section.StartTime < section.EndTime && previous.StartTime < previous.EndTime && section.StartTime < previous.EndTime && previous.StartTime < section.EndTime)
					occupied[lane] = true;
			}
			size_t lane = 0;
			while (lane < occupied.size() && occupied[lane]) ++lane;
			out.SectionLanes[index] = lane;
			out.LaneCount = Max(out.LaneCount, lane + 1);
		}
	}

	inline void BuildPlaybackTimelineData(ChartCourse& course, BranchType branch, Beat endBeat, PlaybackTimelineData& out)
	{
		out.Sections.clear();
		out.Notes.clear();
		out.Stops.clear();
		out.MinTime = out.MaxTime = Time::Zero();
		out.LaneCount = 1;
		const auto& delays = course.GetDelayChanges(branch);
		if (!delays.empty()) endBeat = Max(endBeat, delays.Sorted.back().BeatTime + Beat::FromTicks(1));
		const auto& timing = course.GetPlaybackTiming(branch);
		const b8 canUseSourceTiming = course.CanUseSourceTiming();
		Beat sectionStart = Beat::Zero();
		Time sectionStartTime = course.TempoMap.BeatToTime(sectionStart);
		Time offset = {};
		auto appendSection = [&](Beat end)
		{
			PlaybackTimelineSection section { sectionStart, end, sectionStartTime, course.TempoMap.BeatToTime(end) + offset, timing.GetDelayOffset(sectionStart) };
			out.MinTime = Min(out.MinTime, Min(section.StartTime, section.EndTime));
			out.MaxTime = Max(out.MaxTime, Max(section.StartTime, section.EndTime));
			out.Sections.push_back(section);
		};
		for (const DelayChange& delay : delays)
		{
			const b8 useSource = canUseSourceTiming && delay.BeatTime == delay.SourceBeat && delay.Duration == delay.SourceDuration && !delay.SourceCommands.empty();
			const b8 rewinds = useSource
				? std::any_of(delay.SourceCommands.begin(), delay.SourceCommands.end(), [](const auto& command) { return command.Delay < Time::Zero(); })
				: delay.Duration < Time::Zero();
			if (rewinds) appendSection(delay.BeatTime);
			offset += delay.Duration;
			if (rewinds)
			{
				sectionStart = delay.BeatTime;
				sectionStartTime = course.TempoMap.BeatToTime(sectionStart) + offset;
			}
		}
		appendSection(Max(sectionStart, endBeat));
		auto sectionIndexAtBeat = [&](Beat beat)
		{
			const auto after = std::upper_bound(out.Sections.begin(), out.Sections.end(), beat,
				[](Beat beat, const PlaybackTimelineSection& section) { return beat < section.StartBeat; });
			return after == out.Sections.begin() ? size_t(0) : static_cast<size_t>(after - out.Sections.begin() - 1);
		};
		auto isBranchedBeat = [&](Beat beat)
		{
			return std::any_of(course.Branches.begin(), course.Branches.end(), [&](const BranchRange& range)
			{
				return beat >= range.GetStart() && beat < GetBranchRangeEnd(course.Branches, range);
			});
		};
		for (const auto& stop : timing.ScrollStops)
		{
			const Time end = Min(stop.EndTime, timing.ConvertBeatToTimeUsingLookupTableIndexing(stop.BeatTime));
			if (end <= stop.StartTime) continue;
			const size_t sectionIndex = sectionIndexAtBeat(stop.BeatTime);
			out.Stops.push_back({ stop.BeatTime, stop.StartTime, end, sectionIndex });
			out.Sections[sectionIndex].StartTime = Min(out.Sections[sectionIndex].StartTime, stop.StartTime);
			out.Sections[sectionIndex].EndTime = Max(out.Sections[sectionIndex].EndTime, end);
			out.MinTime = Min(out.MinTime, stop.StartTime);
			out.MaxTime = Max(out.MaxTime, end);
		}
		PlaybackTimelineLayout layout;
		BuildPlaybackTimelineLayout(out, {}, layout);
		out.LaneCount = Max(layout.LaneCount, size_t(1));
		for (size_t index = 0; index < out.Sections.size(); ++index)
			out.Sections[index].Lane = layout.SectionLanes[index] == PlaybackTimelineLayout::HiddenLane ? 0 : layout.SectionLanes[index];
		auto appendNote = [&](Note& note)
		{
			const size_t sectionIndex = sectionIndexAtBeat(note.BeatTime);
			const Time head = timing.ConvertBeatToTimeUsingLookupTableIndexing(note.GetStart()) + note.TimeOffset;
			const Time tail = timing.ConvertBeatToTimeUsingLookupTableIndexing(note.GetEnd()) + note.TimeOffset;
			out.Notes.push_back({ &note, head, tail, sectionIndex });
			out.MinTime = Min(out.MinTime, Min(head, tail));
			out.MaxTime = Max(out.MaxTime, Max(head, tail));
		};
		for (Note& note : course.Notes_Normal)
			if (branch == BranchType::Normal || !isBranchedBeat(note.BeatTime)) appendNote(note);
		if (branch != BranchType::Normal)
			for (Note& note : course.GetNotes(branch))
				if (isBranchedBeat(note.BeatTime)) appendNote(note);
		std::stable_sort(out.Notes.begin(), out.Notes.end(), [](const PlaybackTimelineNote& first, const PlaybackTimelineNote& second) { return first.HeadTime < second.HeadTime; });
	}
}
