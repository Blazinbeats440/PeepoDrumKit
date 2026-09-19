#include "core_beat.h"
#include <algorithm>

Time SortedTempoMap::GetDelayAtBeat(Beat beat, size_t branch, bool includeAtBeat) const
{
	Time offset = {};
	for (const auto& delay : Delays[branch])
	{
		if (delay.BeatTime > beat || (!includeAtBeat && delay.BeatTime == beat)) break;
		offset += delay.Duration;
	}
	return offset;
}

size_t SortedTempoMap::GetDelaySegment(Beat beat, size_t branch) const
{
	size_t segment = 0;
	for (const auto& delay : Delays[branch])
	{
		if (delay.BeatTime > beat) break;
		++segment;
	}
	return segment;
}

size_t SortedTempoMap::GetDelaySegmentAtTime(Time time, Beat preferredBeat, size_t branch) const
{
	const Beat beat = TimeToBeat(time, false, branch, preferredBeat);
	const size_t segment = GetDelaySegment(beat, branch);
	if (segment > 0)
	{
		const auto& delay = Delays[branch][segment - 1];
		const Time end = BeatToTime(delay.BeatTime, branch);
		if (beat == delay.BeatTime && delay.Duration > Time::Zero() && time >= end - delay.Duration && time < end)
			return segment - 1;
	}
	return segment;
}

std::vector<Beat> SortedTempoMap::TimeToBeats(Time time, size_t branch, bool truncTo0) const
{
	std::vector<Beat> result;
	Time offset = {};
	const auto& delays = Delays[branch];
	for (size_t segment = 0; segment <= delays.size(); ++segment)
	{
		const Time localTime = time - offset;
		const bool afterStart = segment == 0 || localTime >= BeatToTimeWithoutDelay(delays[segment - 1].BeatTime);
		const bool beforeEnd = segment == delays.size() || localTime < BeatToTimeWithoutDelay(delays[segment].BeatTime);
		if (afterStart && beforeEnd)
		{
			Beat beat = TimeToBeatWithoutDelay(localTime, truncTo0);
			if (segment < delays.size()) beat = Min(beat, delays[segment].BeatTime - Beat::FromTicks(1));
			if (segment > 0) beat = Max(beat, delays[segment - 1].BeatTime);
			result.push_back(beat);
		}
		if (segment < delays.size()) offset += delays[segment].Duration;
	}
	return result;
}

Beat SortedTempoMap::TimeToBeat(Time time, bool truncTo0, size_t branch, Beat preferredBeat) const
{
	if (Delays[branch].empty()) return TimeToBeatWithoutDelay(time, truncTo0);
	const auto candidates = TimeToBeats(time, branch, truncTo0);
	const size_t preferredSegment = GetDelaySegment(preferredBeat, branch);
	for (Beat beat : candidates)
		if (GetDelaySegment(beat, branch) == preferredSegment) return beat;
	if (!candidates.empty()) return candidates.front();
	// A positive delay creates a gap: keep the cursor at its boundary while audio continues.
	Time offset = {};
	for (const auto& delay : Delays[branch])
	{
		const Time start = BeatToTimeWithoutDelay(delay.BeatTime) + offset;
		if (delay.Duration > Time::Zero() && time >= start && time < start + delay.Duration)
			return delay.BeatTime;
		offset += delay.Duration;
	}
	return TimeToBeatWithoutDelay(time - offset, truncTo0);
}

Time TempoMapAccelerationStructure::ConvertBeatToTimeUsingLookupTableIndexing(Beat beat) const
{
	const i32 beatTickToTimesCount = static_cast<i32>(BeatTickToTimes.size());
	const i32 totalBeatTicks = beat.Ticks;

	if (totalBeatTicks < 0) // NOTE: Negative tick (tempo changes are assumed to only be positive)
	{
		// NOTE: Calculate the duration of a Beat at the first tempo
		const Time firstTickDuration = Time::FromSec((60.0 / abs(FirstTempoBPM)) / Beat::TicksPerBeat);

		// NOTE: Then scale by the negative tick
		return firstTickDuration * totalBeatTicks;
	}
	else if (totalBeatTicks >= beatTickToTimesCount) // NOTE: Tick is outside the defined tempo map
	{
		// NOTE: Take the last calculated time
		const Time lastTime = GetLastCalculatedTime();

		// NOTE: Calculate the duration of a Beat at the last used tempo
		const Time lastTickDuration = Time::FromSec((60.0 / abs(LastTempoBPM)) / Beat::TicksPerBeat);

		// NOTE: Then scale by the remaining ticks
		const i32 remainingTicks = (totalBeatTicks - beatTickToTimesCount) + 1;
		return lastTime + (lastTickDuration * remainingTicks);
	}
	else // NOTE: Use the pre calculated lookup table directly
	{
		return BeatTickToTimes.at(totalBeatTicks);
	}
}

Beat TempoMapAccelerationStructure::ConvertTimeToBeatUsingLookupTableBinarySearch(Time time) const
{
	return ConvertTimeToBeatUsingLookupTableBinarySearch(time, false);
}

Beat TempoMapAccelerationStructure::ConvertTimeToBeatUsingLookupTableBinarySearch(Time time, bool truncTo0) const
{
	const i32 beatTickToTimesCount = static_cast<i32>(BeatTickToTimes.size());
	const Time lastTime = GetLastCalculatedTime();

	if (time < Time::FromSec(0.0) || std::isnan(time.Seconds)) // NOTE: Negative time
	{
		// NOTE: Calculate the duration of a Beat at the first tempo
		const Time firstTickDuration = Time::FromSec((60.0 / abs(FirstTempoBPM)) / Beat::TicksPerBeat);

		// NOTE: Then the time by the negative tick, this is assuming all tempo changes happen on positive ticks
		return Beat(static_cast<i32>(time / firstTickDuration));
	}
	else if (time >= lastTime) // NOTE: Tick is outside the defined tempo map
	{
		const Time timePastLast = (time - lastTime);

		// NOTE: Each tick past the end has a duration of this value
		const Time lastTickDuration = Time::FromSec((60.0 / abs(LastTempoBPM)) / Beat::TicksPerBeat);

		// NOTE: So we just have to divide the remaining ticks by the duration
		const f64 ticks = (timePastLast / lastTickDuration);

		// NOTE: And add it to the last tick
		return Beat(static_cast<i32>(beatTickToTimesCount + ticks - 1));
	}
	else // NOTE: Perform a binary search
	{
		auto base = std::begin(BeatTickToTimes);
		auto [atOrAfter, after] = std::equal_range(base, std::end(BeatTickToTimes), time);
		if (atOrAfter != after) // found
			Beat::FromTicks(atOrAfter - base);
		auto before = after - 1;

		// left > right
		return Beat::FromTicks((truncTo0) ? before - base
			: (*after - time) < (time - *before) ? after - base : before - base);
	}
}

// find the integer HBScroll beat tick by `beat`, and then interpolate or extrapolate to `time`
// allow over-extrapolating for reproducing TaikoJiro "time offset over tempo change" behavior
f64 TempoMapAccelerationStructure::ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(Beat beat, Time time) const
{
	const i32 beatTickToTimesCount = static_cast<i32>(BeatTickToTimes.size());
	const i32 totalBeatTicks = beat.Ticks;

	if (totalBeatTicks < 0) // NOTE: Negative tick (tempo changes are assumed to only be positive)
	{
		// NOTE: Calculate the duration of a Beat at the first tempo
		const Time firstTickDuration = Time::FromSec((60.0 / FirstTempoBPM) / Beat::TicksPerBeat);

		// NOTE: Then the time by the negative tick, this is assuming all tempo changes happen on positive ticks
		return time / firstTickDuration;
	}
	else if (totalBeatTicks + 1 >= beatTickToTimesCount) // NOTE: Next tick is outside the defined tempo map
	{
		const f64 lastHBScrollBeatTick = GetLastCalculatedHBScrollBeatTick();
		const Time lastTime = GetLastCalculatedTime();
		const Time timePastLast = (time - lastTime);

		// NOTE: Each tick past the end has a duration of this value
		const Time lastTickDuration = Time::FromSec((60.0 / LastTempoBPM) / Beat::TicksPerBeat);

		// NOTE: So we just have to divide the remaining ticks by the duration
		const f64 ticks = (timePastLast / lastTickDuration);

		// NOTE: And add it to the last tick
		return (lastHBScrollBeatTick + ticks);
	}
	else // NOTE: Use the pre calculated lookup table directly
	{
		// find the integer HBScroll beat tick using `beat`, and then interpolate by `time`
		const i32 totalBeatTicks = beat.Ticks;
		const f64 timeLeft = BeatTickToTimes.at(totalBeatTicks).Seconds;
		const f64 timeRight = BeatTickToTimes.at(totalBeatTicks + 1).Seconds;
		const f64 ticksLeft = BeatTickToHBScrollBeatTicks.at(totalBeatTicks);
		const f64 ticksRight = BeatTickToHBScrollBeatTicks.at(totalBeatTicks + 1);

		return ConvertRange(timeLeft, timeRight, ticksLeft, ticksRight, time.Seconds);
	}
}

Time TempoMapAccelerationStructure::GetLastCalculatedTime() const
{
	return BeatTickToTimes.empty() ? Time::Zero() : BeatTickToTimes.back();
}

f64 TempoMapAccelerationStructure::GetLastCalculatedHBScrollBeatTick() const
{
	return BeatTickToHBScrollBeatTicks.empty() ? 0.0 : BeatTickToHBScrollBeatTicks.back();
}

void TempoMapAccelerationStructure::Rebuild(const TempoChange* inTempoChanges, size_t inTempoCount)
{
	const TempoChange* tempoChanges = inTempoChanges;
	size_t tempoCount = inTempoCount;

	// HACK: Handle this special case here by creating an adjusted copy instead of changing the algorithm itself to not risk accidentally messing anything up
	if (inTempoCount < 1 || inTempoChanges[0].Beat > Beat::Zero())
	{
		TempoBuffer.resize(inTempoCount + 1);
		TempoBuffer[0] = TempoChange(Beat::Zero(), FallbackTempo);
		memcpy(TempoBuffer.data() + 1, inTempoChanges, sizeof(TempoChange) * inTempoCount);

		tempoChanges = TempoBuffer.data();
		tempoCount = TempoBuffer.size();
	}

	size_t nTickValues = (tempoCount > 0) ? tempoChanges[tempoCount - 1].Beat.Ticks + 1 : 0;
	BeatTickToTimes.resize(nTickValues);
	BeatTickToHBScrollBeatTicks.resize(nTickValues);

	f64 lastEndTime = 0.0;
	i32 lastEndHBScrollBeatTick = 0;
	for (size_t tempoChangeIndex = 0; tempoChangeIndex < tempoCount; tempoChangeIndex++)
	{
		const TempoChange& tempoChange = tempoChanges[tempoChangeIndex];

		const f64 bpm = SafetyCheckTempo(tempoChange.Tempo).BPM;
		const f64 beatDuration = (60.0 / bpm);
		const f64 tickDuration = abs(beatDuration / Beat::TicksPerBeat);
		const f64 tickSign = Sign(beatDuration);

		const b8 isSingleOrLastTempo = (tempoCount == 1) || (tempoChangeIndex == (tempoCount - 1));
		const size_t timesCount = isSingleOrLastTempo ? BeatTickToTimes.size() : (tempoChanges[tempoChangeIndex + 1].Beat.Ticks);

		for (size_t i = 0, t = tempoChange.Beat.Ticks; t < timesCount; i++, t++)
		{
			BeatTickToTimes[t] = Time::FromSec((tickDuration * i) + lastEndTime);
			BeatTickToHBScrollBeatTicks[t] = ((tickSign * i) + lastEndHBScrollBeatTick);
		}

		if (tempoCount > 1)
		{
			lastEndTime = BeatTickToTimes[timesCount - 1].ToSec() + tickDuration;
			lastEndHBScrollBeatTick = BeatTickToHBScrollBeatTicks[timesCount - 1] + tickSign;
		}

		FirstTempoBPM = (tempoChangeIndex == 0) ? bpm : FirstTempoBPM;
		LastTempoBPM = bpm;
	}

	if (!TempoBuffer.empty())
		TempoBuffer.clear();
}
