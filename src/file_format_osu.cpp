#include "file_format_osu.h"
#include "core_string.h"
#include <charconv>
#include <numeric>

namespace OSU
{
	// Note conversion follows delguoqing/osu2tja. Timing accepts legacy and lazer fields without version gating.
	static constexpr size_t MaxObjects = 250000;
	static constexpr i32 MaxChartTicks = 200000 * Beat::TicksPerBeat;

	static std::vector<std::string_view> Split(std::string_view text, char separator)
	{
		std::vector<std::string_view> parts;
		for (;;)
		{
			const auto end = text.find(separator);
			parts.push_back(ASCII::Trim(text.substr(0, end)));
			if (end == std::string_view::npos) return parts;
			text.remove_prefix(end + 1);
		}
	}

	template <typename T> static b8 ReadNumber(std::string_view text, T& out)
	{
		text = ASCII::Trim(text);
		if (text.empty()) return false;
		if (text.front() == '+') text.remove_prefix(1);
		const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
		return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(static_cast<f64>(out));
	}

	static b8 ReadTime(std::string_view text, f64& out)
	{
		return ReadNumber(text, out) && std::abs(out) <= 1000000000;
	}

	struct TimingPoint
	{
		f64 Time, BeatLength, Scroll = 1;
		i32 Meter = 4;
		b8 RedLine = true, GoGo = false;
		Beat BeatTime = {};
	};

	struct HitObject
	{
		f64 Time;
		i32 Type, Sound;
		std::vector<std::string_view> Fields;
	};

	struct Note
	{
		f64 Time;
		TJA::NoteType Type;
		i32 BalloonHits = 0;
		Beat BeatTime = {};
	};

	struct TempoAnchor
	{
		f64 Time;
		Beat BeatTime;
		Tempo Tempo;
		i32 Meter;
		f64 DelayMilliseconds = 0;
	};

	static TJA::NoteType CircleType(i32 sound)
	{
		const b8 ka = (sound & (2 | 8)) != 0, big = (sound & 4) != 0;
		return ka ? (big ? TJA::NoteType::KaBig : TJA::NoteType::Ka) : (big ? TJA::NoteType::DonBig : TJA::NoteType::Don);
	}

	b8 ConvertToTJA(std::string_view text, TJA::ParsedTJA& out, Error& outError)
	{
		outError = Error::InvalidChart;
		if (text.size() > 64 * 1024 * 1024) { outError = Error::SizeLimit; return false; }
		if (UTF8::HasBOM(text)) text = UTF8::TrimBOM(text);
		TJA::ParsedTJA parsed;
		std::string title, titleUnicode, artist, artistUnicode, source;
		std::string_view section;
		std::vector<TimingPoint> timing;
		std::vector<HitObject> objects;
		f64 sliderMultiplier = 1.4;
		i32 mode = 0;
		b8 hasHeader = false;
		for (const auto rawLine : TJA::SplitLines(text))
		{
			const auto line = ASCII::Trim(rawLine);
			if (line.size() > 1024 * 1024) { outError = Error::SizeLimit; return false; }
			if (line.empty() || line.substr(0, 2) == "//") continue;
			if (!hasHeader)
			{
				constexpr std::string_view prefix = "osu file format v";
				i32 version;
				if (line.substr(0, prefix.size()) != prefix || !ReadNumber(line.substr(prefix.size()), version)) return false;
				hasHeader = true;
				continue;
			}
			if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
			const auto colon = line.find(':');
			const auto key = ASCII::Trim(line.substr(0, colon));
			const auto value = colon == std::string_view::npos ? std::string_view() : ASCII::Trim(line.substr(colon + 1));
			if (section == "General")
			{
				if (key == "AudioFilename") parsed.Metadata.WAVE = value;
				else if (key == "Mode") { if (!ReadNumber(value, mode)) return false; }
				else if (key == "PreviewTime")
				{
					f64 preview;
					if (!ReadTime(value, preview)) return false;
					parsed.Metadata.DEMOSTART = Time::FromSec(preview / 1000);
				}
			}
			else if (section == "Metadata")
			{
				if (key == "Title") title = value;
				else if (key == "TitleUnicode") titleUnicode = value;
				else if (key == "Artist") artist = value;
				else if (key == "ArtistUnicode") artistUnicode = value;
				else if (key == "Source") source = value;
				else if (key == "Creator") parsed.Metadata.MAKER = value;
			}
			else if (section == "Difficulty" && key == "SliderMultiplier")
			{
				if (!ReadNumber(value, sliderMultiplier) || sliderMultiplier <= 0 || sliderMultiplier > 1000000) return false;
			}
			else if (section == "TimingPoints")
			{
				const auto fields = Split(line, ',');
				TimingPoint point {};
				if (fields.size() < 2 || !ReadTime(fields[0], point.Time) || !ReadNumber(fields[1], point.BeatLength) || point.BeatLength == 0) return false;
				point.RedLine = point.BeatLength > 0;
				if (fields.size() > 2 && !fields[2].empty() && !ReadNumber(fields[2], point.Meter)) return false;
				if (point.RedLine && (point.Meter < 1 || point.Meter > 64 || 60000 / point.BeatLength < 0.001 || 60000 / point.BeatLength > 1000000)) return false;
				i32 effects = 0;
				if (fields.size() > 7 && !fields[7].empty() && !ReadNumber(fields[7], effects)) return false;
				point.GoGo = (effects & 1) != 0;
				timing.push_back(point);
			}
			else if (section == "HitObjects")
			{
				HitObject object {};
				object.Fields = Split(line, ',');
				if (object.Fields.size() < 5 || !ReadTime(object.Fields[2], object.Time) || !ReadNumber(object.Fields[3], object.Type) || !ReadNumber(object.Fields[4], object.Sound)
					|| object.Type < 0 || object.Sound < 0 || object.Sound > 15) return false;
				objects.push_back(std::move(object));
			}
			if (timing.size() > MaxObjects || objects.size() > MaxObjects) { outError = Error::SizeLimit; return false; }
		}
		if (mode != 0 && mode != 1) { outError = Error::UnsupportedMode; return false; }
		if (!hasHeader || timing.empty()) return false;
		std::stable_sort(timing.begin(), timing.end(), [](const TimingPoint& first, const TimingPoint& second) { return first.Time < second.Time; });
		std::stable_sort(objects.begin(), objects.end(), [](const HitObject& first, const HitObject& second) { return first.Time < second.Time; });
		const auto firstRed = std::find_if(timing.begin(), timing.end(), [](const TimingPoint& point) { return point.RedLine; });
		if (firstRed == timing.end()) return false;
		f64 beatLength = firstRed->BeatLength;
		i32 meter = firstRed->Meter;
		for (auto& point : timing)
		{
			if (point.RedLine) { beatLength = point.BeatLength; meter = point.Meter; }
			else point.Scroll = -100 / point.BeatLength;
			if (!std::isfinite(point.Scroll) || point.Scroll <= 0 || point.Scroll > 1000000) return false;
			point.BeatLength = beatLength;
			point.Meter = meter;
		}
		const auto timingAt = [&](f64 milliseconds) -> const TimingPoint&
		{
			const auto found = std::upper_bound(timing.begin(), timing.end(), milliseconds, [](f64 time, const TimingPoint& point) { return time < point.Time; });
			return found == timing.begin() ? timing.front() : *std::prev(found);
		};
		std::vector<Note> notes;
		f64 rollEnd = -std::numeric_limits<f64>::infinity();
		for (const auto& object : objects)
		{
			if (object.Time <= rollEnd) continue;
			if ((object.Type & (1 | 2 | 8 | 128)) == 1) notes.push_back({ object.Time, CircleType(object.Sound) });
			else if ((object.Type & (1 | 2 | 8 | 128)) == 2)
			{
				const auto& fields = object.Fields;
				i32 spans;
				f64 length;
				if (fields.size() < 8 || !ReadNumber(fields[6], spans) || spans < 1 || spans > static_cast<i32>(MaxObjects)
					|| !ReadNumber(fields[7], length) || length < 0) return false;
				const auto& point = timingAt(object.Time);
				const f64 spanBeats = length / (100 * sliderMultiplier * point.Scroll), spanTime = spanBeats * point.BeatLength;
				const f64 end = object.Time + spanTime * spans;
				if (!std::isfinite(end) || std::abs(end) > 1000000000) return false;
				if ((mode == 1 && spanBeats < 1) || (mode == 0 && spanBeats * spans < 2))
				{
					const auto sounds = fields.size() > 8 && !fields[8].empty() ? Split(fields[8], '|') : std::vector<std::string_view>();
					if (!sounds.empty() && sounds.size() != static_cast<size_t>(spans) + 1) return false;
					if (notes.size() + spans + 1 > MaxObjects) { outError = Error::SizeLimit; return false; }
					for (i32 edge = 0; edge <= spans; edge++)
					{
						i32 sound = object.Sound;
						if (!sounds.empty() && (!ReadNumber(sounds[edge], sound) || sound < 0 || sound > 15)) return false;
						notes.push_back({ object.Time + spanTime * edge, CircleType(sound) });
					}
				}
				else
				{
					notes.push_back({ object.Time, object.Sound == 0 ? TJA::NoteType::Start_Drumroll : TJA::NoteType::Start_DrumrollBig });
					notes.push_back({ end, TJA::NoteType::End_BalloonOrDrumroll });
					rollEnd = end;
				}
			}
			else if ((object.Type & (1 | 2 | 8 | 128)) == 8)
			{
				f64 end;
				if (object.Fields.size() < 6 || !ReadTime(object.Fields[5], end) || end <= object.Time) return false;
				const auto hits = static_cast<i32>((end - object.Time) / 122);
				notes.push_back({ object.Time, TJA::NoteType::Start_Balloon, std::max(1, hits) });
				notes.push_back({ end, TJA::NoteType::End_BalloonOrDrumroll });
				rollEnd = end;
			}
			else return false;
			if (notes.size() > MaxObjects) { outError = Error::SizeLimit; return false; }
		}
		std::stable_sort(notes.begin(), notes.end(), [](const Note& first, const Note& second) { return first.Time < second.Time; });
		const f64 startTime = notes.empty() ? timing.front().Time : std::min(timing.front().Time, notes.front().Time);
		std::vector<TempoAnchor> anchors = { { startTime, Beat::Zero(), Tempo(static_cast<f32>(60000 / timing.front().BeatLength)), timing.front().Meter } };
		for (const auto& point : timing)
		{
			if (!point.RedLine) continue;
			const Tempo tempo(static_cast<f32>(60000 / point.BeatLength));
			const auto& previous = anchors.back();
			if (point.Time == previous.Time) { anchors.back().Tempo = tempo; anchors.back().Meter = point.Meter; continue; }
			const f64 ticks = (point.Time - previous.Time) * previous.Tempo.BPM * Beat::TicksPerBeat / 60000;
			if (!std::isfinite(ticks) || ticks < 0 || ticks + previous.BeatTime.Ticks > MaxChartTicks) { outError = Error::SizeLimit; return false; }
			const auto delta = Beat::FromTicks(static_cast<i32>(std::round(ticks)));
			const f64 correction = point.Time - previous.Time - delta.BeatsFraction() * 60000 / previous.Tempo.BPM;
			anchors.push_back({ point.Time, previous.BeatTime + delta, tempo, point.Meter, correction });
		}
		const auto beatAt = [&](f64 milliseconds, Beat& beat)
		{
			const auto found = std::upper_bound(anchors.begin(), anchors.end(), milliseconds, [](f64 time, const TempoAnchor& anchor) { return time < anchor.Time; });
			const auto& anchor = found == anchors.begin() ? anchors.front() : *std::prev(found);
			const f64 ticks = anchor.BeatTime.Ticks + (milliseconds - anchor.Time) * anchor.Tempo.BPM * Beat::TicksPerBeat / 60000;
			if (!std::isfinite(ticks) || ticks < 0 || ticks > MaxChartTicks) return false;
			beat = Beat::FromTicks(static_cast<i32>(std::round(ticks)));
			return true;
		};
		Beat endBeat = {};
		for (auto& point : timing)
		{
			if (!beatAt(point.Time, point.BeatTime)) { outError = Error::SizeLimit; return false; }
			endBeat = std::max(endBeat, point.BeatTime);
		}
		for (auto& note : notes)
		{
			if (!beatAt(note.Time, note.BeatTime)) { outError = Error::SizeLimit; return false; }
			endBeat = std::max(endBeat, note.BeatTime);
		}
		parsed.Metadata.TITLE = titleUnicode.empty() ? title : titleUnicode;
		const auto subtitle = source.empty() ? (artistUnicode.empty() ? artist : artistUnicode) : source;
		if (!subtitle.empty()) parsed.Metadata.SUBTITLE = "--" + subtitle;
		parsed.Metadata.BPM = anchors.front().Tempo;
		parsed.Metadata.OFFSET = Time::FromSec(-startTime / 1000);
		auto& course = parsed.Courses.emplace_back();
		course.HasChart = true;
		course.Metadata.COURSE = TJA::DifficultyType::Oni;
		course.Metadata.LEVEL = 0;
		course.Metadata.OmitLEVEL = true;
		std::vector<TJA::ConvertedMeasure> measures;
		size_t noteIndex = 0, timingIndex = 0, anchorIndex = 0;
		Beat measureStart = {};
		i64 generatedSlots = 0;
		f64 lastScroll = 1;
		b8 lastGoGo = false;
		do
		{
			while (anchorIndex + 1 < anchors.size() && anchors[anchorIndex + 1].BeatTime <= measureStart) anchorIndex++;
			const auto& anchor = anchors[anchorIndex];
			if (measureStart.Ticks > MaxChartTicks - anchor.Meter * Beat::TicksPerBeat || measures.size() >= 100000) { outError = Error::SizeLimit; return false; }
			Beat next = measureStart + Beat::FromBeats(anchor.Meter);
			if (anchorIndex + 1 < anchors.size()) next = std::min(next, anchors[anchorIndex + 1].BeatTime);
			auto& measure = measures.emplace_back();
			measure.StartTime = measureStart;
			const Beat duration = next - measureStart;
			measure.TimeSignature = TimeSignature(duration.Ticks, Beat::TicksPerBeat * 4).GetSimplified();
			if (anchor.BeatTime == measureStart)
			{
				measure.TempoChanges.push_back({ Beat::Zero(), anchor.Tempo });
				f64 correction = 0;
				size_t firstAnchor = anchorIndex;
				while (firstAnchor > 0 && anchors[firstAnchor - 1].BeatTime == measureStart) firstAnchor--;
				for (size_t index = firstAnchor; index <= anchorIndex; index++) correction += anchors[index].DelayMilliseconds;
				if (std::abs(correction) > 1e-9) measure.DelayChanges.push_back({ Beat::Zero(), Time::FromSec(correction / 1000), anchor.Tempo });
			}
			for (; noteIndex < notes.size() && notes[noteIndex].BeatTime < next; noteIndex++)
			{
				const auto& note = notes[noteIndex];
				const auto relative = note.BeatTime - measureStart;
				if (!measure.Notes.empty() && measure.Notes.back().TimeWithinMeasure == relative) continue;
				measure.Notes.push_back({ relative, note.Type });
				if (note.Type == TJA::NoteType::Start_Balloon) course.Metadata.BALLOON.push_back(note.BalloonHits);
			}
			for (; timingIndex < timing.size() && timing[timingIndex].BeatTime < next; timingIndex++)
			{
				const auto& point = timing[timingIndex];
				const auto relative = point.BeatTime - measureStart;
				if (point.Scroll != lastScroll) measure.ScrollChanges.push_back({ relative, Complex(static_cast<f32>(point.Scroll), 0) });
				if (point.GoGo != lastGoGo) measure.GoGoChanges.push_back({ relative, point.GoGo });
				lastScroll = point.Scroll;
				lastGoGo = point.GoGo;
			}
			i32 tickPerSlot = duration.Ticks;
			for (const auto& note : measure.Notes) tickPerSlot = std::gcd(tickPerSlot, note.TimeWithinMeasure.Ticks);
			for (const auto& point : measure.ScrollChanges) tickPerSlot = std::gcd(tickPerSlot, point.TimeWithinMeasure.Ticks);
			for (const auto& point : measure.GoGoChanges) tickPerSlot = std::gcd(tickPerSlot, point.TimeWithinMeasure.Ticks);
			generatedSlots += duration.Ticks / tickPerSlot;
			if (generatedSlots > 10000000) { outError = Error::SizeLimit; return false; }
			measureStart = next;
		} while (measureStart <= endBeat);
		TJA::ConvertConvertedMeasuresToParsedCommands(measures, course.ChartCommands);
		out = std::move(parsed);
		outError = Error::None;
		return true;
	}
}
