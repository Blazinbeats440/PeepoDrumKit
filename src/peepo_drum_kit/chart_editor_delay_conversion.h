#pragma once
#include "chart_editor_undo.h"

namespace PeepoDrumKit
{
	enum class DelayConversionError : u8 { None, InvalidRange, InvalidTime, Branches, Note, LongNote, Event, ActiveJPOS, NegativeDelay, Signature };
	struct DelayConversionPlan
	{
		DelayConversionError Error = DelayConversionError::None;
		Beat Start, End, ErrorBeat;
		GenericList ErrorList = GenericList::Count;
		Time Duration;
		i32 SourceTempoChangeCount = -1;
		std::vector<TimeSignatureChange> Signatures;
		std::vector<GenericListStructWithType> RemoveItems, AddItems;
	};

	inline DelayConversionPlan BuildDelayConversionPlan(const ChartCourse& course, Beat start, Beat end, b8 mergePartialBars = false)
	{
		DelayConversionPlan plan;
		plan.Start = start; plan.End = end;
		const auto fail = [&](DelayConversionError error, Beat beat) { plan.Error = error; plan.ErrorBeat = beat; return plan; };
		if (start < Beat::Zero() || end <= start)
			return fail(DelayConversionError::InvalidRange, start);
		if (!course.Branches.empty() || !course.BranchSections.empty() || !course.BranchLevelHolds.empty())
			return fail(DelayConversionError::Branches, start);
		const Beat removedBeats = end - start;
		plan.Duration = course.TempoMap.BeatToTime(end) - course.TempoMap.BeatToTime(start);
		if (!std::isfinite(plan.Duration.Seconds) || plan.Duration <= Time::Zero())
			return fail(DelayConversionError::InvalidTime, start);
		const b8 preserveSourceTiming = course.CanUseSourceTiming();
		for (const DelayChange& delay : course.DelayChanges_Normal)
		{
			if (delay.BeatTime < start || delay.BeatTime > end) continue;
			const b8 hasSourceRewind = preserveSourceTiming && delay.BeatTime == delay.SourceBeat && delay.Duration == delay.SourceDuration
				&& std::any_of(delay.SourceCommands.begin(), delay.SourceCommands.end(), [](const auto& command) { return command.Delay < Time::Zero(); });
			if (delay.Duration < Time::Zero() || hasSourceRewind)
				return fail(DelayConversionError::NegativeDelay, delay.BeatTime);
			plan.Duration += delay.Duration;
		}
		if (!std::isfinite(plan.Duration.Seconds)) return fail(DelayConversionError::InvalidTime, start);
		ForEachChartItem(course, [&](const ForEachChartItemData& item)
		{
			if (plan.Error != DelayConversionError::None) return;
			const Beat beat = GetBeat(item, course);
			if (IsNotesList(item.List))
			{
				if (beat >= start && beat < end) { plan.Error = DelayConversionError::Note; plan.ErrorBeat = beat; }
				else if (GetBeatDuration(item, course) > Beat::Zero())
				{
					const Beat tail = beat + GetBeatDuration(item, course);
					if (tail >= start && tail < end) { plan.Error = DelayConversionError::LongNote; plan.ErrorBeat = tail; }
				}
			}
			else if (item.List != GenericList::TempoChanges && item.List != GenericList::SignatureChanges && !IsDelayChangesList(item.List))
			{
				const Beat tail = beat + GetBeatDuration(item, course);
				const b8 hasGoGoEnd = item.List == GenericList::GoGoRanges && beat < start && tail >= start && tail <= end;
				if ((beat >= start && beat <= end) || hasGoGoEnd)
					{ plan.Error = DelayConversionError::Event; plan.ErrorBeat = hasGoGoEnd ? tail : beat; }
			}
			if (plan.Error != DelayConversionError::None) plan.ErrorList = item.List;
		});
		if (plan.Error != DelayConversionError::None) return plan;
		const Time stopStart = course.BeatToPlaybackTime(start) - (course.DelayChanges_Normal.TryFindExactAtBeat(start) ? course.DelayChanges_Normal.TryFindExactAtBeat(start)->Duration : Time::Zero());
		for (const JPOSScrollChange& event : course.JPOSScrollChanges)
		{
			const Time eventStart = course.BeatToPlaybackTime(event.BeatTime);
			if (event.Duration > 0.0f && eventStart < stopStart + plan.Duration && eventStart + Time::FromSec(event.Duration) > stopStart)
				return fail(DelayConversionError::ActiveJPOS, event.BeatTime);
		}

		std::vector<SortedTempoMap::ForEachBeatBarData> bars;
		TimeSignature precedingSignature = FallbackTimeSignature;
		course.TempoMap.ForEachBeatBar([&](const SortedTempoMap::ForEachBeatBarData& bar)
		{
			if (bar.Beat <= start)
			{
				if (!bars.empty()) precedingSignature = bars.back().Signature;
				bars.clear();
			}
			bars.push_back(bar);
			if (!IsTimeSignatureSupported(bar.Signature) || bar.Signature.GetDurationPerBar() <= Beat::Zero()
				|| bar.Signature.GetDurationPerBar().Ticks > I32Max - bar.Beat.Ticks)
				{ plan.Error = DelayConversionError::Signature; plan.ErrorBeat = bar.Beat; return ControlFlow::Break; }
			return bar.Beat >= end ? ControlFlow::Break : ControlFlow::Continue;
		});
		if (plan.Error != DelayConversionError::None) return plan;
		const Beat firstBar = bars.front().Beat, afterBar = bars.back().Beat;
		const auto mapBeat = [&](Beat beat) { return beat < start ? beat : beat < end ? start : beat - removedBeats; };
		std::vector<TimeSignatureChange> desiredSignatures;
		for (size_t i = 0; i + 1 < bars.size(); i++)
		{
			const Beat barStart = bars[i].Beat, barEnd = bars[i + 1].Beat;
			const Beat remaining = barEnd - barStart - Max(Beat::Zero(), Min(barEnd, end) - Max(barStart, start));
			if (remaining == Beat::Zero()) continue;
			const TimeSignature signature = remaining == barEnd - barStart ? bars[i].Signature
				: TimeSignature(remaining.Ticks, Beat::FromBars(1).Ticks).GetSimplified(bars[i].Signature.Denominator);
			desiredSignatures.emplace_back(mapBeat(barStart), signature);
		}
		if (mergePartialBars && desiredSignatures.size() > 1)
		{
			const Beat remaining = afterBar - firstBar - removedBeats;
			desiredSignatures.resize(1);
			desiredSignatures[0].Signature = TimeSignature(remaining.Ticks, Beat::FromBars(1).Ticks).GetSimplified(bars.front().Signature.Denominator);
		}
		desiredSignatures.emplace_back(afterBar - removedBeats, bars.back().Signature);
		for (const TimeSignatureChange& signature : desiredSignatures)
		{
			if (!IsTimeSignatureSupported(signature.Signature)) return fail(DelayConversionError::Signature, signature.Beat);
			if (signature.Signature == precedingSignature) continue;
			plan.Signatures.push_back(signature);
			precedingSignature = signature.Signature;
		}

		ForEachChartItem(course, [&](const ForEachChartItemData& item)
		{
			const Beat beat = GetBeat(item, course);
			const b8 signature = item.List == GenericList::SignatureChanges;
			const b8 tempo = item.List == GenericList::TempoChanges;
			if (beat < (signature ? firstBar : start))
			{
				if ((!IsNotesList(item.List) && item.List != GenericList::GoGoRanges) || beat + GetBeatDuration(item, course) < end) return;
			}
			GenericListStructWithType original;
			original.List = item.List;
			TryGetGenericStruct(course, item.List, item.Index, original.Value);
			plan.RemoveItems.push_back(original);
			if ((signature && beat <= afterBar) || (tempo && beat <= end) || (IsDelayChangesList(item.List) && beat <= end)) return;
			auto changed = original;
			SetBeat(mapBeat(beat), changed);
			if (IsNotesList(item.List) || item.List == GenericList::GoGoRanges)
				SetBeatDuration(mapBeat(beat + GetBeatDuration(original)) - mapBeat(beat), changed);
			if (preserveSourceTiming && tempo) changed.Value.POD.Tempo.SourceBeat = GetBeat(changed);
			if (preserveSourceTiming && IsDelayChangesList(item.List) && changed.Value.NonTrivial.Delay.BeatTime != original.Value.NonTrivial.Delay.BeatTime
				&& original.Value.NonTrivial.Delay.BeatTime == original.Value.NonTrivial.Delay.SourceBeat && original.Value.NonTrivial.Delay.Duration == original.Value.NonTrivial.Delay.SourceDuration)
				changed.Value.NonTrivial.Delay.SourceBeat = changed.Value.NonTrivial.Delay.BeatTime;
			plan.AddItems.push_back(std::move(changed));
		});
		const TempoChange* endTempo = course.TempoMap.Tempo.TryFindLastAtBeat(end);
		TempoChange tempo(start, endTempo ? endTempo->Tempo : FallbackTempo);
		tempo.SourceBeat = start; tempo.SourceTempo = tempo.Tempo;
		const TempoChange* beforeTempo = course.TempoMap.Tempo.TryFindLastAtBeat(start - Beat::FromTicks(1));
		if (start == Beat::Zero() || tempo.Tempo.BPM != (beforeTempo ? beforeTempo->Tempo.BPM : FallbackTempo.BPM))
			plan.AddItems.emplace_back(GenericList::TempoChanges, tempo);
		for (const TimeSignatureChange& signature : plan.Signatures)
			plan.AddItems.emplace_back(GenericList::SignatureChanges, signature);
		DelayChange delay { start, plan.Duration, true };
		plan.AddItems.emplace_back(GenericList::DelayChanges_Normal, std::move(delay));
		plan.SourceTempoChangeCount = preserveSourceTiming ? static_cast<i32>(course.TempoMap.Tempo.size())
			- static_cast<i32>(std::count_if(plan.RemoveItems.begin(), plan.RemoveItems.end(), [](const auto& item) { return item.List == GenericList::TempoChanges; }))
			+ static_cast<i32>(std::count_if(plan.AddItems.begin(), plan.AddItems.end(), [](const auto& item) { return item.List == GenericList::TempoChanges; }))
			: -1;
		return plan;
	}

	namespace Commands
	{
		struct ConvertRangeToDelay : RemoveThenAddMultipleGenericItems
		{
			ConvertRangeToDelay(ChartContext* context, DelayConversionPlan plan)
				: RemoveThenAddMultipleGenericItems(context->ChartSelectedCourse, std::move(plan.RemoveItems), std::move(plan.AddItems)),
				Context(context), Course(context->ChartSelectedCourse), Start(plan.Start), End(plan.End), OldCursorBeat(context->GetCursorBeat()),
				OldRange(context->RangeSelection), OldMarker(context->Marker), OldCursorTime(context->GetCursorTime()), OldExactCursorTime(context->CursorTimeWhilePaused.has_value()),
				OldSourceTempoChangeCount(Course->SourceTempoChangeCount), NewSourceTempoChangeCount(plan.SourceTempoChangeCount) {}
			void Undo() override
			{
				Course->SourceTempoChangeCount = OldSourceTempoChangeCount;
				RemoveThenAddMultipleGenericItems::Undo();
				Context->RangeSelection = OldRange; Context->Marker = OldMarker;
				if (Context->ChartSelectedCourse == Course)
				{
					if (OldExactCursorTime) Context->SetCursorTimeAtBeat(OldCursorTime, OldCursorBeat);
					else Context->SetCursorBeat(OldCursorBeat);
				}
			}
			void Redo() override
			{
				Course->SourceTempoChangeCount = NewSourceTempoChangeCount;
				RemoveThenAddMultipleGenericItems::Redo();
				Context->RangeSelection = {};
				Context->Marker = OldMarker;
				if (Context->Marker.IsActive && Context->Marker.BeatTime >= Start)
					Context->Marker.BeatTime = Context->Marker.BeatTime < End ? Start : Context->Marker.BeatTime - (End - Start);
				if (Context->ChartSelectedCourse == Course) Context->SetCursorBeat(Start);
			}
			Undo::CommandInfo GetInfo() const override { return { "Convert Range to DELAY" }; }
			ChartContext* Context;
			ChartCourse* Course;
			Beat Start, End, OldCursorBeat;
			ChartContext::RangeSelectionData OldRange;
			ChartContext::MarkerData OldMarker;
			Time OldCursorTime;
			b8 OldExactCursorTime;
			i32 OldSourceTempoChangeCount, NewSourceTempoChangeCount;
		};
	}
}
