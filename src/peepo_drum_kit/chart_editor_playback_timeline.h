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

	struct PlaybackTimelineData
	{
		std::vector<PlaybackTimelineSection> Sections;
		std::vector<PlaybackTimelineNote> Notes;
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
		out.MinTime = out.MaxTime = Time::Zero();
		out.LaneCount = 1;
		const auto& delays = course.GetDelayChanges(branch);
		if (!delays.empty()) endBeat = Max(endBeat, delays.Sorted.back().BeatTime + Beat::FromTicks(1));
		Time offset = {};
		for (size_t index = 0; index <= delays.size(); ++index)
		{
			const Beat start = index == 0 ? Beat::Zero() : delays[index - 1].BeatTime;
			const Beat end = index == delays.size() ? Max(start, endBeat) : delays[index].BeatTime;
			PlaybackTimelineSection section { start, end, course.TempoMap.BeatToTime(start) + offset, course.TempoMap.BeatToTime(end) + offset, offset };
			std::vector<b8> occupied(out.LaneCount, false);
			for (const auto& previous : out.Sections)
			{
				if (section.StartTime < section.EndTime && previous.StartTime < previous.EndTime && section.StartTime < previous.EndTime && previous.StartTime < section.EndTime)
					occupied[previous.Lane] = true;
			}
			while (section.Lane < occupied.size() && occupied[section.Lane]) ++section.Lane;
			out.LaneCount = Max(out.LaneCount, section.Lane + 1);
			out.MinTime = Min(out.MinTime, Min(section.StartTime, section.EndTime));
			out.MaxTime = Max(out.MaxTime, Max(section.StartTime, section.EndTime));
			out.Sections.push_back(section);
			if (index < delays.size()) offset += delays[index].Duration;
		}
		auto isBranchedBeat = [&](Beat beat)
		{
			return std::any_of(course.Branches.begin(), course.Branches.end(), [&](const BranchRange& range)
			{
				return beat >= range.GetStart() && beat < GetBranchRangeEnd(course.Branches, range);
			});
		};
		const auto& timing = course.GetPlaybackTiming(branch);
		auto appendNote = [&](Note& note)
		{
			const size_t sectionIndex = static_cast<size_t>(std::upper_bound(delays.Sorted.begin(), delays.Sorted.end(), note.BeatTime,
				[](Beat beat, const DelayChange& delay) { return beat < delay.BeatTime; }) - delays.Sorted.begin());
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
