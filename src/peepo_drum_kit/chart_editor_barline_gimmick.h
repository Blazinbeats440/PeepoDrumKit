#pragma once
#include "chart_editor_undo.h"

namespace PeepoDrumKit
{
	enum class BarLineGimmickError : u8 { None, InvalidRange, Branches, Delay, Tempo, Signature, Scroll };
	struct BarLineGimmickSegment
	{
		Beat Start = {}, End = {}, Remainder = {};
		f32 BPM = 0.0f;
		i32 Denominator = 1, MeasureCount = 0;
	};
	struct BarLineGimmickPlan
	{
		BarLineGimmickError Error = BarLineGimmickError::None;
		Beat Start = {}, End = {}, FirstBar = {}, AfterBar = {}, ErrorBeat = {};
		TimeSignature PrefixSignature, SuffixSignature, RestoreSignature;
		i32 MeasureCount = 0;
		std::vector<BarLineGimmickSegment> Segments;
		std::vector<TimeSignatureChange> Signatures;
		std::vector<GenericListStructWithType> RemoveItems, AddItems;
	};

	inline b8 TryGetBarLineGimmickBarDuration(TimeSignature signature, Beat& duration)
	{
		if (!IsTimeSignatureSupported(signature) || signature.Numerator <= 0) return false;
		if (signature.GetSimplified().Numerator > I32Max / Beat::FromBars(1).Ticks) return false;
		duration = signature.GetDurationPerBar();
		return duration > Beat::Zero();
	}

	inline b8 TryFindBarLineGimmickBar(const SortedTempoMap& map, Beat target, Beat& start, TimeSignature& signature)
	{
		start = Beat::Zero();
		signature = FallbackTimeSignature;
		size_t changeIndex = 0;
		for (;;)
		{
			while (changeIndex < map.Signature.size() && map.Signature[changeIndex].Beat <= start)
				signature = map.Signature[changeIndex++].Signature;
			Beat duration;
			if (!TryGetBarLineGimmickBarDuration(signature, duration)) return false;
			const i64 targetOffset = static_cast<i64>(target.Ticks) - start.Ticks;
			if (changeIndex < map.Signature.size())
			{
				// Signature changes inside a bar take effect at the next existing bar boundary.
				const i64 changeOffset = static_cast<i64>(map.Signature[changeIndex].Beat.Ticks) - start.Ticks;
				const i64 changeBoundary = start.Ticks + ((changeOffset + duration.Ticks - 1) / duration.Ticks) * duration.Ticks;
				if (target.Ticks >= changeBoundary)
				{
					start = Beat::FromTicks(static_cast<i32>(changeBoundary));
					continue;
				}
			}
			start += Beat::FromTicks(static_cast<i32>((targetOffset / duration.Ticks) * duration.Ticks));
			return duration.Ticks <= I32Max - start.Ticks;
		}
	}

	inline BarLineGimmickPlan BuildBarLineGimmickPlan(const ChartCourse& course, Beat start, Beat end, i32 rate)
	{
		BarLineGimmickPlan plan;
		plan.Start = start; plan.End = end;
		const auto fail = [&](BarLineGimmickError error, Beat beat) { plan.Error = error; plan.ErrorBeat = beat; return plan; };
		if (start < Beat::Zero() || end <= start || (rate != 120 && rate != 60 && rate != 30))
			return fail(BarLineGimmickError::InvalidRange, start);
		for (const BranchRange& branch : course.Branches)
			if (branch.GetStart() < end && (GetBranchRangeEnd(course.Branches, branch) > start || branch.GetStart() >= start))
				return fail(BarLineGimmickError::Branches, Max(start, branch.GetStart()));
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			for (const DelayChange& delay : course.GetDelayChanges(branch))
				if (delay.BeatTime >= start && delay.BeatTime <= end)
					return fail(BarLineGimmickError::Delay, delay.BeatTime);

		TimeSignature firstSignature, endSignature;
		Beat endBar;
		if (!TryFindBarLineGimmickBar(course.TempoMap, start, plan.FirstBar, firstSignature)
			|| !TryFindBarLineGimmickBar(course.TempoMap, end, endBar, endSignature))
			return fail(BarLineGimmickError::Signature, start);
		plan.AfterBar = endBar;
		plan.RestoreSignature = endSignature;
		if (endBar < end)
		{
			Beat duration;
			TryGetBarLineGimmickBarDuration(endSignature, duration);
			plan.AfterBar += duration;
			Beat restoreBar;
			if (!TryFindBarLineGimmickBar(course.TempoMap, plan.AfterBar, restoreBar, plan.RestoreSignature))
				return fail(BarLineGimmickError::Signature, plan.AfterBar);
		}
		const TimeSignatureChange* preceding = plan.FirstBar > Beat::Zero()
			? course.TempoMap.Signature.TryFindLastAtBeat(plan.FirstBar - Beat::FromTicks(1)) : nullptr;
		TimeSignature previousSignature = preceding ? preceding->Signature : FallbackTimeSignature;
		const auto appendSignature = [&](Beat beat, TimeSignature signature)
		{
			Beat duration;
			if (!TryGetBarLineGimmickBarDuration(signature, duration))
				{ plan.Error = BarLineGimmickError::Signature; plan.ErrorBeat = beat; return; }
			if (previousSignature == signature) return;
			TimeSignatureChange change(beat, signature);
			if (const auto* original = course.TempoMap.Signature.TryFindExactAtBeat(beat); original && original->Signature == signature)
				change = *original;
			plan.Signatures.push_back(change);
			previousSignature = signature;
		};
		if (plan.FirstBar < start)
		{
			plan.PrefixSignature = TimeSignature((start - plan.FirstBar).Ticks, Beat::FromBars(1).Ticks).GetSimplified(firstSignature.Denominator);
			appendSignature(plan.FirstBar, plan.PrefixSignature);
		}

		Beat segmentStart = start;
		static const std::vector<i32> supportedDenominators = []
		{
			std::vector<i32> result;
			for (i32 denominator = 1; denominator <= Beat::FromBars(1).Ticks; ++denominator)
				if (Beat::FromBars(1).Ticks % denominator == 0) result.push_back(denominator);
			return result;
		}();
		while (segmentStart < end)
		{
			const TempoChange* tempo = course.TempoMap.Tempo.TryFindLastAtBeat(segmentStart);
			const f32 bpm = TempoOrDefault(tempo).BPM;
			if (!std::isfinite(bpm) || bpm <= 0.0f) return fail(BarLineGimmickError::Tempo, segmentStart);
			Beat segmentEnd = end;
			const auto nextTempo = std::upper_bound(course.TempoMap.Tempo.begin(), course.TempoMap.Tempo.end(), segmentStart,
				[](Beat beat, const TempoChange& change) { return beat < change.Beat; });
			if (nextTempo != course.TempoMap.Tempo.end()) segmentEnd = Min(segmentEnd, nextTempo->Beat);
			const f64 targetDenominator = 240.0 * rate / bpm;
			i32 denominator = 1;
			f64 bestDistance = std::abs(targetDenominator - denominator);
			for (i32 candidate : supportedDenominators)
			{
				const f64 distance = std::abs(targetDenominator - candidate);
				if (distance < bestDistance) { denominator = candidate; bestDistance = distance; }
			}
			const i32 duration = Beat::FromBars(1).Ticks / denominator;
			const i32 length = (segmentEnd - segmentStart).Ticks;
			const i32 fullMeasures = length / duration;
			const Beat remainder = Beat::FromTicks(length % duration);
			if (fullMeasures > 0) appendSignature(segmentStart, TimeSignature(1, denominator));
			if (remainder > Beat::Zero())
				appendSignature(segmentEnd - remainder, TimeSignature(remainder.Ticks, Beat::FromBars(1).Ticks).GetSimplified());
			const i32 measureCount = fullMeasures + (remainder > Beat::Zero() ? 1 : 0);
			plan.Segments.push_back({ segmentStart, segmentEnd, remainder, bpm, denominator, measureCount });
			plan.MeasureCount += measureCount;
			segmentStart = segmentEnd;
		}
		if (end < plan.AfterBar)
		{
			plan.SuffixSignature = TimeSignature((plan.AfterBar - end).Ticks, Beat::FromBars(1).Ticks).GetSimplified(endSignature.Denominator);
			appendSignature(end, plan.SuffixSignature);
		}
		if (course.TempoMap.Signature.TryFindExactAtBeat(plan.AfterBar) == nullptr)
			appendSignature(plan.AfterBar, plan.RestoreSignature);
		if (plan.Error != BarLineGimmickError::None) return plan;
		for (const TimeSignatureChange& signature : course.TempoMap.Signature)
			if (signature.Beat >= plan.FirstBar && signature.Beat < plan.AfterBar)
				plan.RemoveItems.emplace_back(GenericList::SignatureChanges, signature);
		for (const TimeSignatureChange& signature : plan.Signatures)
			plan.AddItems.emplace_back(GenericList::SignatureChanges, signature);
		return plan;
	}

	inline std::vector<Beat> GetBarLineGimmickNoteBeats(const ChartCourse& course, const BarLineGimmickPlan& plan, BranchType branch)
	{
		std::vector<Beat> result;
		for (const Note& note : course.GetNotes(branch))
		{
			if (note.GetStart() >= plan.Start && note.GetStart() < plan.End) result.push_back(note.GetStart());
			if (note.GetEnd() > note.GetStart() && note.GetEnd() >= plan.Start && note.GetEnd() < plan.End)
				result.push_back(note.GetEnd());
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}

	inline Beat GetBarLineGimmickNextBar(const BarLineGimmickPlan& plan, Beat beat)
	{
		for (const auto& segment : plan.Segments)
		{
			if (beat < segment.Start) return segment.Start;
			if (beat >= segment.End) continue;
			const i32 duration = Beat::FromBars(1).Ticks / segment.Denominator;
			const i64 next = segment.Start.Ticks + (static_cast<i64>((beat - segment.Start).Ticks) / duration + 1) * duration;
			if (next < segment.End.Ticks) return Beat::FromTicks(static_cast<i32>(next));
		}
		return plan.End;
	}

	inline void AddBarLineGimmickNoteVisibilityToPlan(const ChartCourse& course, BarLineGimmickPlan& plan)
	{
		if (plan.Error != BarLineGimmickError::None) return;
		std::vector<BarLineChange> changes;
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
			for (Beat beat : GetBarLineGimmickNoteBeats(course, plan, branch))
			{
				changes.push_back({ beat, false });
				changes.push_back({ GetBarLineGimmickNextBar(plan, beat), true });
			}
		std::sort(changes.begin(), changes.end(), [](const BarLineChange& first, const BarLineChange& second)
		{
			return first.BeatTime != second.BeatTime ? first.BeatTime < second.BeatTime : first.IsVisible > second.IsVisible;
		});
		for (size_t index = 0; index < changes.size();)
		{
			BarLineChange change = changes[index++];
			while (index < changes.size() && changes[index].BeatTime == change.BeatTime) change = changes[index++];
			if (const auto* original = course.BarLineChanges.TryFindExactAtBeat(change.BeatTime); original && original->IsVisible == change.IsVisible)
				change = *original;
			plan.AddItems.emplace_back(GenericList::BarLineChanges, change);
		}
	}

	inline void AddBarLineGimmickScrollToPlan(const ChartCourse& course, BarLineGimmickPlan& plan,
		f32 startValue, f32 endValue, b8 interpolate)
	{
		if (plan.Error != BarLineGimmickError::None) return;
		if (!std::isfinite(startValue) || (interpolate && !std::isfinite(endValue)))
		{
			plan.Error = BarLineGimmickError::Scroll;
			plan.ErrorBeat = plan.Start;
			return;
		}
		const auto& finalSegment = plan.Segments.back();
		const Beat lastBar = finalSegment.Remainder > Beat::Zero() ? finalSegment.End - finalSegment.Remainder
			: finalSegment.End - Beat::FromTicks(Beat::FromBars(1).Ticks / finalSegment.Denominator);
		const auto scrollAtBar = [&](Beat beat)
		{
			if (!interpolate || lastBar == plan.Start) return Complex(startValue, 0.0f);
			const f64 progress = static_cast<f64>((beat - plan.Start).Ticks) / (lastBar - plan.Start).Ticks;
			return Complex(static_cast<f32>(static_cast<f64>(startValue) * (1.0 - progress) + static_cast<f64>(endValue) * progress), 0.0f);
		};
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			if (branch != BranchType::Normal && course.Branches.empty()) continue;
			const auto& originalScrolls = course.GetScrollChanges(branch);
			const GenericList list = BranchTypeToScrollChangesList(branch);
			const auto protectedBeats = GetBarLineGimmickNoteBeats(course, plan, branch);
			for (const ScrollChange& change : originalScrolls)
				if (change.BeatTime >= plan.Start && change.BeatTime < plan.End) plan.RemoveItems.emplace_back(list, change);
			Complex previous = ScrollOrDefault(plan.Start > Beat::Zero()
				? originalScrolls.TryFindLastAtBeat(plan.Start - Beat::FromTicks(1)) : nullptr);
			const auto appendScroll = [&](Beat beat, Complex speed)
			{
				if (previous == speed) return;
				ScrollChange change { beat, speed };
				if (const auto* original = originalScrolls.TryFindExactAtBeat(beat); original && original->ScrollSpeed == speed)
					change = *original;
				plan.AddItems.emplace_back(list, change);
				previous = speed;
			};
			size_t protectedIndex = 0;
			for (const auto& segment : plan.Segments)
			{
				const i32 duration = Beat::FromBars(1).Ticks / segment.Denominator;
				for (i32 index = 0; index < segment.MeasureCount; ++index)
				{
					const Beat bar = segment.Start + Beat::FromTicks(index * duration);
					while (protectedIndex < protectedBeats.size() && protectedBeats[protectedIndex] < bar)
					{
						const Beat beat = protectedBeats[protectedIndex++];
						appendScroll(beat, ScrollOrDefault(originalScrolls.TryFindLastAtBeat(beat)));
					}
					if (protectedIndex < protectedBeats.size() && protectedBeats[protectedIndex] == bar)
					{
						++protectedIndex;
						appendScroll(bar, ScrollOrDefault(originalScrolls.TryFindLastAtBeat(bar)));
					}
					else appendScroll(bar, scrollAtBar(bar));
				}
			}
			while (protectedIndex < protectedBeats.size())
			{
				const Beat beat = protectedBeats[protectedIndex++];
				appendScroll(beat, ScrollOrDefault(originalScrolls.TryFindLastAtBeat(beat)));
			}
		}
	}

	namespace Commands
	{
		struct GenerateBarLineGimmick : RemoveThenAddMultipleGenericItems
		{
			GenerateBarLineGimmick(ChartCourse* course, BarLineGimmickPlan plan)
				: RemoveThenAddMultipleGenericItems(course, std::move(plan.RemoveItems), std::move(plan.AddItems)) {}
			Undo::CommandInfo GetInfo() const override { return { UI_Str("UTILITY_BARLINE_GIMMICK") }; }
		};
	}
}
