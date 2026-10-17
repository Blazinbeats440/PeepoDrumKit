#pragma once
#include "chart.h"
#include <set>

namespace PeepoDrumKit
{
	struct BeatBarIndex
	{
		struct Entry : SortedTempoMap::ForEachBeatBarData { Time BaseTime; };
		u64 TempoRevision = 0, SignatureRevision = 0;
		Beat EndBeat = Beat::Zero();
		std::vector<Entry> Beats, Bars;
		void Update(const ChartCourse& course, Beat endBeat)
		{
			if (!Beats.empty() && TempoRevision == course.TempoMap.Revision && SignatureRevision == course.TempoMap.Signature.Revision && EndBeat > endBeat) return;
			Beats.clear(); Bars.clear();
			course.TempoMap.ForEachBeatBar([&](const SortedTempoMap::ForEachBeatBarData& bar)
			{
				const Entry entry { bar, course.TempoMap.BeatToTime(bar.Beat) };
				Beats.push_back(entry);
				if (bar.IsBar) Bars.push_back(entry);
				EndBeat = bar.Beat;
				return bar.IsBar && bar.Beat > endBeat ? ControlFlow::Break : ControlFlow::Fallthrough;
			});
			TempoRevision = course.TempoMap.Revision; SignatureRevision = course.TempoMap.Signature.Revision;
		}
		auto FindTimeRange(Time minimum, Time maximum) const
		{
			const auto first = std::lower_bound(Beats.begin(), Beats.end(), minimum, [](const Entry& entry, Time time) { return entry.BaseTime < time; });
			const auto last = std::upper_bound(first, Beats.end(), maximum, [](Time time, const Entry& entry) { return time < entry.BaseTime; });
			return std::make_pair(first, last);
		}
		auto FindBarRange(Beat minimum, Beat maximum) const
		{
			auto first = std::upper_bound(Bars.begin(), Bars.end(), minimum, [](Beat beat, const Entry& entry) { return beat < entry.Beat; });
			if (first != Bars.begin()) --first;
			const auto last = std::upper_bound(first, Bars.end(), maximum, [](Beat beat, const Entry& entry) { return beat < entry.Beat; });
			return std::make_pair(first, last);
		}
	};

	struct TimedBeatIndex
	{
		struct Entry { SortedTempoMap::ForEachBeatBarData Bar; Time Time; };
		std::vector<Entry> Beats;
		void Build(const ChartCourse& course, BranchType branch, const BeatBarIndex& source)
		{
			Beats.clear();
			for (const auto& bar : source.Beats) Beats.push_back({ bar, course.BeatToPlaybackTime(bar.Beat, branch) });
			std::stable_sort(Beats.begin(), Beats.end(), [](const Entry& first, const Entry& second) { return first.Time < second.Time; });
		}
		auto FindRange(Time minimum, Time maximum) const
		{
			const auto first = std::lower_bound(Beats.begin(), Beats.end(), minimum, [](const Entry& entry, Time time) { return entry.Time < time; });
			const auto last = std::upper_bound(first, Beats.end(), maximum, [](Time time, const Entry& entry) { return time < entry.Time; });
			return std::make_pair(first, last);
		}
	};

	struct TimedNoteIndex
	{
		struct Entry { const Note* OriginalNote; Time HeadTime, TailTime, FirstTime, LatestEnd; };
		std::vector<Entry> Notes;
		void Build(const ChartCourse& course, BranchType branch, b8 playbackTiming)
		{
			Notes.clear();
			for (const Note& note : course.GetNotes(branch))
			{
				const Time head = (playbackTiming ? course.BeatToPlaybackTime(note.GetStart(), branch) : course.TempoMap.BeatToTime(note.GetStart())) + note.TimeOffset;
				const Time tail = note.BeatDuration > Beat::Zero()
					? (playbackTiming ? course.BeatToPlaybackTime(note.GetEnd(), branch) : course.TempoMap.BeatToTime(note.GetEnd())) + note.TimeOffset : head;
				Notes.push_back({ &note, head, tail, Min(head, tail), Max(head, tail) });
			}
			std::stable_sort(Notes.begin(), Notes.end(), [](const Entry& first, const Entry& second) { return first.FirstTime < second.FirstTime; });
			Time latest = Time::FromSec(-F64Max);
			for (auto& entry : Notes) entry.LatestEnd = latest = Max(latest, entry.LatestEnd);
		}
		auto FindRange(Time minimum, Time maximum) const
		{
			const auto first = std::lower_bound(Notes.begin(), Notes.end(), minimum, [](const Entry& entry, Time time) { return entry.LatestEnd < time; });
			const auto last = std::upper_bound(first, Notes.end(), maximum, [](Time time, const Entry& entry) { return time < entry.FirstTime; });
			return std::make_pair(first, last);
		}
	};

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
