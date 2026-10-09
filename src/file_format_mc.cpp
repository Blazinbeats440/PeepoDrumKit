#include "file_format_mc.h"
#include "core_string.h"
#include <charconv>
#include <map>
#include <numeric>

namespace MC
{
	// Conversion rules follow LuiCat/mc2tja; see license_txt/mc2tja.txt.
	struct JSONValue
	{
		enum class Type { Null, Boolean, Number, String, Array, Object } Kind = Type::Null;
		f64 Number = 0;
		std::string String;
		std::vector<JSONValue> Array;
		std::map<std::string, JSONValue> Object;

		const JSONValue& Get(std::string_view key) const
		{
			static const JSONValue missing;
			const auto found = Object.find(std::string(key));
			return found == Object.end() ? missing : found->second;
		}
		b8 Is(Type type) const { return Kind == type; }
		b8 Has(std::string_view key) const { return Object.find(std::string(key)) != Object.end(); }
	};

	class JSONReader
	{
		std::string_view Text;
		size_t Position = 0;

		void SkipSpace()
		{
			while (Position < Text.size() && (Text[Position] == ' ' || Text[Position] == '\t' || Text[Position] == '\r' || Text[Position] == '\n')) Position++;
		}
		b8 Take(char value)
		{
			SkipSpace();
			if (Position >= Text.size() || Text[Position] != value) return false;
			Position++;
			return true;
		}
		b8 ReadHex(u32& out)
		{
			out = 0;
			for (i32 index = 0; index < 4; index++)
			{
				if (Position >= Text.size()) return false;
				const char digit = Text[Position++];
				const i32 value = digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
				if (value < 0) return false;
				out = out * 16 + value;
			}
			return true;
		}
		b8 ReadString(std::string& out)
		{
			if (!Take('"')) return false;
			while (Position < Text.size())
			{
				const char value = Text[Position++];
				if (value == '"') return true;
				if (static_cast<u8>(value) < 0x20) return false;
				if (value != '\\') { out += value; continue; }
				if (Position >= Text.size()) return false;
				switch (Text[Position++])
				{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					u32 codepoint;
					if (!ReadHex(codepoint)) return false;
					if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
					{
						if (Position + 2 > Text.size() || Text.substr(Position, 2) != "\\u") return false;
						Position += 2;
						u32 low;
						if (!ReadHex(low) || low < 0xDC00 || low > 0xDFFF) return false;
						codepoint = 0x10000 + (codepoint - 0xD800) * 0x400 + low - 0xDC00;
					}
					else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) return false;
					if (codepoint < 0x80) out += static_cast<char>(codepoint);
					else if (codepoint < 0x800)
					{
						out += static_cast<char>(0xC0 | (codepoint >> 6));
						out += static_cast<char>(0x80 | (codepoint & 0x3F));
					}
					else
					{
						if (codepoint >= 0x10000) out += static_cast<char>(0xF0 | (codepoint >> 18));
						out += static_cast<char>((codepoint >= 0x10000 ? 0x80 : 0xE0) | ((codepoint >> 12) & (codepoint >= 0x10000 ? 0x3F : 0x0F)));
						out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
						out += static_cast<char>(0x80 | (codepoint & 0x3F));
					}
					break;
				}
				default: return false;
				}
			}
			return false;
		}
		b8 ReadValue(JSONValue& out, i32 depth)
		{
			SkipSpace();
			if (depth > 64 || Position >= Text.size()) return false;
			if (Text[Position] == '"') { out.Kind = JSONValue::Type::String; return ReadString(out.String); }
			if (Text[Position] == '{' || Text[Position] == '[')
			{
				const b8 object = Text[Position++] == '{';
				out.Kind = object ? JSONValue::Type::Object : JSONValue::Type::Array;
				const char end = object ? '}' : ']';
				if (Take(end)) return true;
				do
				{
					std::string key;
					if (object && (!ReadString(key) || !Take(':'))) return false;
					JSONValue child;
					if (!ReadValue(child, depth + 1)) return false;
					if (object) out.Object[std::move(key)] = std::move(child);
					else out.Array.push_back(std::move(child));
					if (Take(end)) return true;
				} while (Take(','));
				return false;
			}
			for (const auto& literal : { std::string_view("true"), std::string_view("false"), std::string_view("null") })
			{
				if (Text.substr(Position, literal.size()) != literal) continue;
				Position += literal.size();
				out.Kind = literal == "null" ? JSONValue::Type::Null : JSONValue::Type::Boolean;
				out.Number = literal == "true" ? 1 : 0;
				return true;
			}
			const size_t start = Position;
			if (Text[Position] == '-') Position++;
			if (Position >= Text.size()) return false;
			if (Text[Position] == '0') Position++;
			else
			{
				if (Text[Position] < '1' || Text[Position] > '9') return false;
				while (Position < Text.size() && Text[Position] >= '0' && Text[Position] <= '9') Position++;
			}
			const auto readDigits = [&]()
			{
				const size_t first = Position;
				while (Position < Text.size() && Text[Position] >= '0' && Text[Position] <= '9') Position++;
				return Position > first;
			};
			if (Position < Text.size() && Text[Position] == '.') { Position++; if (!readDigits()) return false; }
			if (Position < Text.size() && (Text[Position] == 'e' || Text[Position] == 'E'))
			{
				Position++;
				if (Position < Text.size() && (Text[Position] == '+' || Text[Position] == '-')) Position++;
				if (!readDigits()) return false;
			}
			out.Kind = JSONValue::Type::Number;
			const auto result = std::from_chars(Text.data() + start, Text.data() + Position, out.Number);
			return result.ec == std::errc() && result.ptr == Text.data() + Position && std::isfinite(out.Number);
		}

	public:
		explicit JSONReader(std::string_view text) : Text(UTF8::HasBOM(text) ? UTF8::TrimBOM(text) : text) {}
		b8 Read(JSONValue& out) { if (!ReadValue(out, 0)) return false; SkipSpace(); return Position == Text.size(); }
	};

	static b8 ReadBeat(const JSONValue& value, f64& out)
	{
		if (value.Is(JSONValue::Type::Number)) out = value.Number;
		else if (value.Is(JSONValue::Type::Array) && value.Array.size() == 3)
		{
			for (const auto& part : value.Array)
				if (!part.Is(JSONValue::Type::Number) || std::floor(part.Number) != part.Number) return false;
			if (value.Array[2].Number <= 0) return false;
			out = value.Array[0].Number + value.Array[1].Number / value.Array[2].Number;
		}
		else return false;
		return std::isfinite(out) && std::abs(out) <= 100000;
	}

	struct TimedValue { f64 Beat; const JSONValue* Value; };
	static b8 ReadTimedValues(const JSONValue& array, std::vector<TimedValue>& out)
	{
		if (!array.Is(JSONValue::Type::Array)) return false;
		for (const auto& value : array.Array)
		{
			f64 beat;
			if (!value.Is(JSONValue::Type::Object) || !ReadBeat(value.Get("beat"), beat)) return false;
			out.push_back({ beat, &value });
		}
		std::stable_sort(out.begin(), out.end(), [](const TimedValue& first, const TimedValue& second) { return first.Beat < second.Beat; });
		return true;
	}

	static void SetDifficulty(std::string version, TJA::ParsedCourseMetadata& out)
	{
		for (char& value : version) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
		static constexpr cstr names[][8] = { { "kantan", "easy" }, { "futsuu", "normal", "futsu" }, { "muzukashii", "hard", "muzu" }, { "oni", "mania", "extreme", "extra", "ura", "inner", "+" } };
		static const std::vector<i32> levels[] = { {0,2,3,4,5}, {0,0,1,2,3,4,5,6,6,7}, {0,0,0,0,0,0,1,2,3,4,5,5,6,6,7,7,8}, {1,1,1,1,1,1,1,1,1,1,2,3,4,4,5,5,5,6,6,7,7,8,8,9,9,10} };
		i32 course = -1, level = 0;
		for (i32 candidate = 3; candidate >= 0 && course < 0; candidate--)
			for (cstr name : names[candidate]) if (name && version.find(name) != std::string::npos) { course = candidate; break; }
		std::smatch match;
		if (std::regex_search(version, match, std::regex("l(?:v|evel) *\\.? *(\\d+)")))
		{
			const std::string digits = match[1];
			const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), level);
			if (parsed.ec != std::errc()) level = I32Max;
		}
		if (course < 0)
		{
			course = 3;
			for (i32 candidate = 0; candidate < 3; candidate++)
				if (level < static_cast<i32>(levels[candidate].size()) && levels[candidate][level] != 0) { course = candidate; break; }
		}
		out.COURSE = static_cast<TJA::DifficultyType>(course);
		out.LEVEL = course == 3 && level == 0 ? 10 : std::max(1, levels[course][std::min(level, static_cast<i32>(levels[course].size()) - 1)]);
	}

	b8 ConvertToTJA(std::string_view text, TJA::ParsedTJA& out, Error& outError)
	{
		outError = Error::InvalidJSON;
		if (text.size() > 64 * 1024 * 1024) { outError = Error::SizeLimit; return false; }
		JSONValue root;
		if (!JSONReader(text).Read(root) || !root.Is(JSONValue::Type::Object)) return false;
		outError = Error::NotTaiko;
		const auto& meta = root.Get("meta");
		if (!meta.Get("mode").Is(JSONValue::Type::Number) || meta.Get("mode").Number != 5) return false;
		outError = Error::InvalidChart;
		std::vector<TimedValue> times, notes, effects;
		if (!ReadTimedValues(root.Get("time"), times) || times.empty() || !ReadTimedValues(root.Get("note"), notes)) return false;
		if (root.Has("effect") && !ReadTimedValues(root.Get("effect"), effects)) return false;
		for (const auto& time : times)
		{
			const auto& bpm = time.Value->Get("bpm");
			if (!bpm.Is(JSONValue::Type::Number) || bpm.Number < 0.001 || bpm.Number > 1000000) return false;
		}
		std::vector<std::pair<f64, f64>> signatures;
		for (const auto& effect : effects)
		{
			f64 length;
			if (effect.Value->Has("sign") && (!ReadBeat(effect.Value->Get("sign"), length) || length <= 0 || length > 64 || Beat::FromBeatsFraction(length).Ticks <= 0)) return false;
			if (effect.Value->Has("sign")) signatures.emplace_back(effect.Beat, length);
			if (effect.Value->Has("hs") && (!effect.Value->Get("hs").Is(JSONValue::Type::Number) || std::abs(effect.Value->Get("hs").Number) > 1000000)) return false;
			for (cstr key : { "ggt", "showbar" })
				if (effect.Value->Has(key) && !effect.Value->Get(key).Is(JSONValue::Type::Number) && !effect.Value->Get(key).Is(JSONValue::Type::Boolean)) return false;
		}
		const auto signatureAt = [&](f64 beat)
		{
			const auto found = std::upper_bound(signatures.begin(), signatures.end(), beat, [](f64 value, const auto& signature) { return value < signature.first; });
			return found == signatures.begin() ? 4.0 : std::prev(found)->second;
		};
		f64 start = 0;
		const auto& modeExt = meta.Get("mode_ext");
		if (modeExt.Has("bar_begin") && !ReadBeat(modeExt.Get("bar_begin"), start)) return false;
		const JSONValue* sample = nullptr;
		f64 sampleBeat = 0;
		for (const auto& note : notes)
			if (note.Value->Get("type").Is(JSONValue::Type::Number) && note.Value->Get("type").Number == 1)
			{
				if (!sample) { sample = note.Value; sampleBeat = note.Beat; }
			}
		notes.erase(std::remove_if(notes.begin(), notes.end(), [](const TimedValue& note) { return note.Value->Get("type").Is(JSONValue::Type::Number) && note.Value->Get("type").Number == 1; }), notes.end());
		if (!notes.empty())
			for (i32 measureCount = 0; start > notes.front().Beat; measureCount++)
			{
				if (measureCount >= 100000) { outError = Error::SizeLimit; return false; }
				start -= signatureAt(start);
				if (start < -100000) return false;
			}
		const auto timeAt = [&](f64 beat)
		{
			f64 seconds = 0, previous = times.front().Beat, bpm = times.front().Value->Get("bpm").Number;
			for (const auto& time : times)
			{
				if (time.Beat > beat) break;
				seconds += (time.Beat - previous) * 60 / bpm;
				previous = time.Beat;
				bpm = time.Value->Get("bpm").Number;
			}
			return seconds + (beat - previous) * 60 / bpm;
		};
		TJA::ParsedTJA parsed;
		parsed.Metadata.TITLE = meta.Get("song").Get("title").String;
		const auto& artist = meta.Get("song").Get("artist").String;
		if (!artist.empty()) parsed.Metadata.SUBTITLE = "--" + artist;
		parsed.Metadata.MAKER = meta.Get("creator").String;
		parsed.Metadata.PREIMAGE = meta.Get("background").String;
		parsed.Metadata.BPM = Tempo(static_cast<f32>(times.front().Value->Get("bpm").Number));
		for (const auto& time : times) if (time.Beat <= start) parsed.Metadata.BPM = Tempo(static_cast<f32>(time.Value->Get("bpm").Number));
		if (sample)
		{
			if (!sample->Get("sound").Is(JSONValue::Type::String) || (sample->Has("offset") && !sample->Get("offset").Is(JSONValue::Type::Number))) return false;
			parsed.Metadata.WAVE = sample->Get("sound").String;
			parsed.Metadata.OFFSET = Time::FromSec(sample->Get("offset").Number * 0.001 - (timeAt(start) - timeAt(sampleBeat)));
			parsed.Metadata.DEMOSTART = -parsed.Metadata.OFFSET;
		}
		if (meta.Has("preview"))
		{
			if (!meta.Get("preview").Is(JSONValue::Type::Number)) return false;
			parsed.Metadata.DEMOSTART = Time::FromSec(meta.Get("preview").Number * 0.001);
		}
		parsed.Metadata.Others["SCOREMODE"] = "2";
		auto& course = parsed.Courses.emplace_back();
		course.HasChart = true;
		SetDifficulty(meta.Get("version").String, course.Metadata);
		struct Note { f64 Beat; TJA::NoteType Type; };
		std::vector<Note> convertedNotes;
		f64 end = start, rollEnd = -std::numeric_limits<f64>::infinity();
		static constexpr TJA::NoteType styles[] = { TJA::NoteType::Don, TJA::NoteType::DonBig, TJA::NoteType::Ka, TJA::NoteType::KaBig, TJA::NoteType::Start_Drumroll, TJA::NoteType::Start_DrumrollBig, TJA::NoteType::Start_Balloon, TJA::NoteType::End_BalloonOrDrumroll, TJA::NoteType::Start_BaloonSpecial };
		for (const auto& note : notes)
		{
			const auto& style = note.Value->Get("style");
			if (!style.Is(JSONValue::Type::Number) || std::floor(style.Number) != style.Number || style.Number < 0 || style.Number >= ArrayCount(styles)) return false;
			if (note.Beat <= rollEnd) continue;
			const auto type = styles[static_cast<i32>(style.Number)];
			convertedNotes.push_back({ note.Beat, type });
			end = std::max(end, note.Beat);
			if (style.Number == 4 || style.Number == 5 || style.Number == 6 || style.Number == 8)
			{
				if (!ReadBeat(note.Value->Get("endbeat"), rollEnd) || rollEnd <= note.Beat) return false;
				convertedNotes.push_back({ rollEnd, TJA::NoteType::End_BalloonOrDrumroll });
				end = std::max(end, rollEnd);
				if (style.Number == 6 || style.Number == 8)
				{
					const auto& hits = note.Value->Get("hits");
					if (!hits.Is(JSONValue::Type::Number) || hits.Number < 1 || hits.Number > I32Max || std::floor(hits.Number) != hits.Number) return false;
					course.Metadata.BALLOON.push_back(static_cast<i32>(hits.Number));
				}
			}
		}
		if (!effects.empty()) end = std::max(end, effects.back().Beat);
		if (!times.empty()) end = std::max(end, times.back().Beat);
		std::vector<TJA::ConvertedMeasure> measures;
		size_t noteIndex = 0, timeIndex = 0, effectIndex = 0;
		f64 barBeat = start;
		i64 generatedSlots = 0;
		do
		{
			const f64 length = signatureAt(barBeat), next = barBeat + length;
			const auto duration = Beat::FromBeatsFraction(length);
			const auto measureStart = Beat::FromBeatsFraction(barBeat - start);
			if (next <= barBeat || barBeat - start + length > 200000 || measures.size() >= 100000) { outError = Error::SizeLimit; return false; }
			auto& measure = measures.emplace_back();
			measure.StartTime = measureStart;
			measure.TimeSignature = TimeSignature(duration.Ticks, Beat::TicksPerBeat * 4).GetSimplified();
			const auto relativeBeat = [&](f64 beat) { return Beat::FromTicks(Clamp(Beat::FromBeatsFraction(beat - barBeat).Ticks, 0, duration.Ticks - 1)); };
			for (; noteIndex < convertedNotes.size() && convertedNotes[noteIndex].Beat < next; noteIndex++)
			{
				const auto& note = convertedNotes[noteIndex];
				const auto beat = relativeBeat(note.Beat);
				if (!measure.Notes.empty() && measure.Notes.back().TimeWithinMeasure == beat) measure.Notes.back().Type = note.Type;
				else measure.Notes.push_back({ beat, note.Type });
			}
			for (; timeIndex < times.size() && times[timeIndex].Beat < next; timeIndex++)
				measure.TempoChanges.push_back({ relativeBeat(times[timeIndex].Beat), Tempo(static_cast<f32>(times[timeIndex].Value->Get("bpm").Number)) });
			for (; effectIndex < effects.size() && effects[effectIndex].Beat < next; effectIndex++)
			{
				const auto& effect = *effects[effectIndex].Value;
				const auto beat = relativeBeat(effects[effectIndex].Beat);
				if (effect.Has("hs")) measure.ScrollChanges.push_back({ beat, Complex(static_cast<f32>(effect.Get("hs").Number), 0) });
				if (effect.Has("ggt")) measure.GoGoChanges.push_back({ beat, effect.Get("ggt").Number != 0 });
				if (effect.Has("showbar")) measure.BarLineChanges.push_back({ beat, effect.Get("showbar").Number != 0 });
			}
			i32 tickPerSlot = duration.Ticks;
			for (const auto& note : measure.Notes) tickPerSlot = std::gcd(tickPerSlot, note.TimeWithinMeasure.Ticks);
			for (const auto& time : measure.TempoChanges) tickPerSlot = std::gcd(tickPerSlot, time.TimeWithinMeasure.Ticks);
			for (const auto& scroll : measure.ScrollChanges) tickPerSlot = std::gcd(tickPerSlot, scroll.TimeWithinMeasure.Ticks);
			for (const auto& gogo : measure.GoGoChanges) tickPerSlot = std::gcd(tickPerSlot, gogo.TimeWithinMeasure.Ticks);
			for (const auto& barline : measure.BarLineChanges) tickPerSlot = std::gcd(tickPerSlot, barline.TimeWithinMeasure.Ticks);
			generatedSlots += duration.Ticks / tickPerSlot;
			if (generatedSlots > 10000000) { outError = Error::SizeLimit; return false; }
			barBeat = next;
		} while (barBeat <= end);
		TJA::ConvertConvertedMeasuresToParsedCommands(measures, course.ChartCommands);
		out = std::move(parsed);
		outError = Error::None;
		return true;
	}
}
