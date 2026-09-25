#pragma once

#include <algorithm>
#include <cmath>

namespace PeepoDrumKit
{
	template <typename T>
	struct TestPlayInterval
	{
		T Start, End;
		bool Contains(T time) const { return Start <= time && time <= End; }
		bool Contains(T start, T end) const { return Contains(start) && Contains(end); }
		bool IsValid() const { return Start < End; }
	};

	template <typename T>
	TestPlayInterval<T> GetTestPlayAttemptInterval(TestPlayInterval<T> selectedRange, T attemptStart)
	{
		return { std::max(selectedRange.Start, attemptStart), selectedRange.End };
	}

	template <typename T>
	T GetTestPlayResumeTime(T cursor, TestPlayInterval<T> interval, bool rangeSelected)
	{
		return rangeSelected && interval.IsValid() && !interval.Contains(cursor) ? interval.Start : cursor;
	}

	template <typename T>
	bool CanResumeTestPlay(bool finished, bool rangeSelected, TestPlayInterval<T> interval, T cursor)
	{
		return !finished || (rangeSelected && interval.IsValid() && !interval.Contains(cursor));
	}

	inline int GetTestPlayJudgement(double absoluteErrorMs, int goodWindowMs, int okWindowMs, int badWindowMs)
	{
		return absoluteErrorMs <= goodWindowMs ? 1 : absoluteErrorMs <= okWindowMs ? 2 : absoluteErrorMs <= badWindowMs ? 3 : 0;
	}

	enum class TestPlayPauseAction { Pause, ResumeWithLeadIn, ResumeAtCursor, StayAtEnd };
	inline TestPlayPauseAction GetTestPlayPauseAction(bool isPlaying, bool finished, bool atAttemptStart)
	{
		return isPlaying ? TestPlayPauseAction::Pause : finished ? TestPlayPauseAction::StayAtEnd
			: atAttemptStart ? TestPlayPauseAction::ResumeWithLeadIn : TestPlayPauseAction::ResumeAtCursor;
	}

	template <typename T>
	bool IsTestPlayMissDue(T now, T noteTime, T badWindow) { return now - noteTime > badWindow; }

	template <typename T>
	bool IsTestPlayLoopDue(T now, T end, T delay) { return now > end + delay; }

	template <typename T>
	bool IsTestPlayLongNoteActive(T now, T start, T end, TestPlayInterval<T> interval)
	{
		return interval.Contains(start) && now >= start && now <= std::min(end, interval.End);
	}

	template <typename Map, typename Key, typename Value>
	void StoreLatestTestPlayResult(Map& results, const Key& note, const Value& result)
	{
		results.insert_or_assign(note, result);
	}

	struct TestPlayStatistics
	{
		int Good = 0, Ok = 0, Bad = 0, Fast = 0, Slow = 0, TimedHits = 0;
		double AverageMs = 0.0, StandardDeviationMs = 0.0;
	};

	template <typename Map>
	TestPlayStatistics CalculateTestPlayStatistics(const Map& results)
	{
		TestPlayStatistics stats;
		double sum = 0.0, squareSum = 0.0;
		for (const auto& [note, result] : results)
		{
			if (result.Judgement == 1) ++stats.Good;
			else if (result.Judgement == 2) ++stats.Ok;
			else if (result.Judgement == 3) ++stats.Bad;
			if (!result.WasHit) continue;
			const double timing = static_cast<double>(result.TimingError);
			sum += timing;
			squareSum += timing * timing;
			++stats.TimedHits;
			if (timing < 0.0) ++stats.Fast;
			if (timing > 0.0) ++stats.Slow;
		}
		if (stats.TimedHits > 0)
		{
			stats.AverageMs = sum / stats.TimedHits;
			stats.StandardDeviationMs = std::sqrt(std::max(0.0, squareSum / stats.TimedHits - stats.AverageMs * stats.AverageMs));
		}
		return stats;
	}
}
