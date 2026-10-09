#include "file_format_mc.h"
#include "core_string.h"
#include <fstream>
#include <iterator>

static TJA::ParsedTJA Import(std::string_view text)
{
	TJA::ParsedTJA parsed;
	MC::Error error;
	assert(MC::ConvertToTJA(text, parsed, error));
	assert(error == MC::Error::None);
	assert(parsed.Courses.size() == 1);
	assert(parsed.Courses[0].HasChart);
	return parsed;
}

static void Reject(std::string_view text, MC::Error expected)
{
	TJA::ParsedTJA parsed;
	parsed.Metadata.TITLE = "Keep current chart";
	MC::Error error;
	assert(!MC::ConvertToTJA(text, parsed, error));
	assert(error == expected);
	assert(parsed.Metadata.TITLE == "Keep current chart");
}

int main(int argc, char** argv)
{
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	const auto parsed = Import(R"({
		"meta":{"mode":5,"song":{"title":"\u66f2\u540d\ud83e\udd41","artist":"Artist"},"creator":"Creator","background":"cover.png","version":"Taiko Oni Lv.25","preview":15000,"mode_ext":{"bar_begin":2}},
		"time":[{"beat":[2,0,1],"bpm":240},{"beat":[0,0,1],"bpm":120}],
		"effect":[{"beat":[2,0,1],"sign":3},{"beat":[3,0,1],"hs":1.5,"ggt":1,"showbar":false},{"beat":[4,0,1],"ggt":0,"showbar":true}],
		"note":[{"beat":[0,0,1],"type":1,"sound":"song.ogg","offset":250},{"beat":[2,0,1],"style":0},{"beat":[2,72,288],"style":1},{"beat":[2,144,288],"style":2},{"beat":[2,216,288],"style":3},{"beat":[3,0,1],"style":6,"hits":12,"endbeat":[7,0,1]}]
	})");
	assert(parsed.Metadata.TITLE == u8"曲名🥁");
	assert(parsed.Metadata.SUBTITLE == "--Artist");
	assert(parsed.Metadata.MAKER == "Creator");
	assert(parsed.Metadata.PREIMAGE == "cover.png");
	assert(parsed.Metadata.WAVE == "song.ogg");
	assert(parsed.Metadata.BPM.BPM == 240);
	assert(std::abs(parsed.Metadata.OFFSET.Seconds + 0.75) < 1e-9);
	assert(parsed.Metadata.DEMOSTART.Seconds == 15);
	assert(parsed.Courses[0].Metadata.COURSE == TJA::DifficultyType::Oni);
	assert(parsed.Courses[0].Metadata.LEVEL == 10);
	assert(parsed.Courses[0].Metadata.BALLOON == std::vector<i32>{12});
	const auto converted = TJA::ConvertParsedToConvertedCourse(parsed, parsed.Courses[0]);
	assert(converted.Measures.size() == 3);
	assert(converted.Measures[0].TimeSignature == TimeSignature(3, 4));
	assert(converted.Measures[0].Notes.size() == 5);
	const TJA::NoteType expected[] = {TJA::NoteType::Don, TJA::NoteType::DonBig, TJA::NoteType::Ka, TJA::NoteType::KaBig, TJA::NoteType::Start_Balloon};
	for (size_t index = 0; index < 5; index++) assert(converted.Measures[0].Notes[index].Type == expected[index]);
	assert(converted.Measures[0].Notes[1].TimeWithinMeasure == Beat::FromBeatsFraction(0.25));
	assert(converted.Measures[1].Notes[0].Type == TJA::NoteType::End_BalloonOrDrumroll);
	assert(converted.Measures[1].Notes[0].TimeWithinMeasure == Beat::FromBeats(2));
	assert(converted.Measures[0].ScrollChanges[0].ScrollSpeed == Complex(1.5f, 0));
	assert(converted.GoGoRanges.size() == 1);
	assert(converted.GoGoRanges[0].StartTime == Beat::FromBeats(1));
	assert(converted.GoGoRanges[0].EndTime == Beat::FromBeats(2));
	assert(converted.Measures[0].BarLineChanges.size() == 2);
	std::string tja;
	TJA::ConvertParsedToText(parsed, tja, TJA::SaveFormat::Current);
	TJA::ErrorList errors;
	const auto reparsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(UTF8::TrimBOM(tja))), errors);
	for (const auto& error : errors.Errors) std::cerr << error.LineIndex << ": " << error.Description << '\n';
	assert(errors.Errors.empty());
	assert(reparsed.Metadata.TITLE == parsed.Metadata.TITLE);
	assert(reparsed.Courses[0].Metadata.BALLOON == parsed.Courses[0].Metadata.BALLOON);
	const auto roundTrip = TJA::ConvertParsedToConvertedCourse(reparsed, reparsed.Courses[0]);
	assert(roundTrip.Measures[1].Notes[0].TimeWithinMeasure == converted.Measures[1].Notes[0].TimeWithinMeasure);

	const auto bpmChange = Import(R"({"meta":{"mode":5,"version":"Hard Lv.16"},"time":[{"beat":[0,0,1],"bpm":120},{"beat":[1,1,2],"bpm":180}],"note":[{"beat":[0,0,1],"style":4,"endbeat":[4,0,1]}]})");
	const auto bpmCourse = TJA::ConvertParsedToConvertedCourse(bpmChange, bpmChange.Courses[0]);
	assert(bpmCourse.Measures[0].TempoChanges[1].TimeWithinMeasure == Beat::FromBeatsFraction(1.5));
	assert(bpmCourse.Measures[1].Notes[0].Type == TJA::NoteType::End_BalloonOrDrumroll);
	assert(bpmChange.Courses[0].Metadata.COURSE == TJA::DifficultyType::Hard);
	assert(bpmChange.Courses[0].Metadata.LEVEL == 8);

	const auto beforeBegin = Import(R"({"meta":{"mode":5,"mode_ext":{"bar_begin":4}},"time":[{"beat":[0,0,1],"bpm":120}],"note":[{"beat":[0,0,1],"type":1,"sound":"song.ogg","offset":0},{"beat":[2,0,1],"style":0}]})");
	assert(beforeBegin.Metadata.OFFSET == Time::Zero());
	const auto beforeCourse = TJA::ConvertParsedToConvertedCourse(beforeBegin, beforeBegin.Courses[0]);
	assert(beforeCourse.Measures[0].Notes[0].TimeWithinMeasure == Beat::FromBeats(2));
	const auto fractional = Import(R"({"meta":{"mode":5},"time":[{"beat":[0,0,1],"bpm":120}],"effect":[{"beat":[0,0,1],"sign":[1,1,2]}],"note":[{"beat":[0,0,1],"style":5,"endbeat":[0,1,2]},{"beat":[1,0,1],"style":8,"endbeat":[2,0,1],"hits":9}]})");
	const auto fractionalCourse = TJA::ConvertParsedToConvertedCourse(fractional, fractional.Courses[0]);
	assert(fractionalCourse.Measures[0].TimeSignature == TimeSignature(3, 8));
	assert(fractionalCourse.Measures[0].Notes[0].Type == TJA::NoteType::Start_DrumrollBig);
	assert(fractionalCourse.Measures[0].Notes[2].Type == TJA::NoteType::Start_BaloonSpecial);
	assert(fractional.Courses[0].Metadata.BALLOON == std::vector<i32>{9});
	const auto lateAudio = Import(R"({"meta":{"mode":5,"mode_ext":{"bar_begin":2}},"time":[{"beat":[0,0,1],"bpm":120},{"beat":[1,0,1],"bpm":240}],"note":[{"beat":[1,1,2],"type":1,"sound":"song.ogg","offset":200},{"beat":[2,0,1],"style":0}]})");
	assert(std::abs(lateAudio.Metadata.OFFSET.Seconds - 0.075) < 1e-9);

	const auto empty = Import("\xEF\xBB\xBF" R"({"meta":{"mode":5},"time":[{"beat":[0,0,1],"bpm":160}],"note":[],"extra":{"ignored":[true,false,null]}})");
	assert(!empty.Courses[0].ChartCommands.empty());
	Reject("{", MC::Error::InvalidJSON);
	Reject(R"({"meta":{"mode":5},})", MC::Error::InvalidJSON);
	Reject(R"({"meta":{"mode":5},"value":01})", MC::Error::InvalidJSON);
	Reject(R"({"meta":{"mode":5},"value":"\ud800"})", MC::Error::InvalidJSON);
	Reject(R"({"meta":{"mode":0}})", MC::Error::NotTaiko);
	Reject(R"({"meta":{"mode":5},"time":[],"note":[]})", MC::Error::InvalidChart);
	Reject(R"({"meta":{"mode":5},"time":[{"beat":[0,0,0],"bpm":120}],"note":[]})", MC::Error::InvalidChart);
	Reject(R"({"meta":{"mode":5},"time":[{"beat":[0,0,1],"bpm":0}],"note":[]})", MC::Error::InvalidChart);
	Reject(R"({"meta":{"mode":5},"time":[{"beat":[0,0,1],"bpm":120}],"note":[{"beat":[0,0,1],"style":6,"hits":12}]})", MC::Error::InvalidChart);
	Reject(R"({"meta":{"mode":5},"time":[{"beat":[0,0,1],"bpm":120}],"note":[{"beat":[0,0,1],"style":99}]})", MC::Error::InvalidChart);
	Reject(R"({"meta":{"mode":5,"mode_ext":{"bar_begin":100}},"time":[{"beat":[0,0,1],"bpm":120}],"effect":[{"beat":[0,0,1],"sign":[0,1,10000]}],"note":[{"beat":[0,0,1],"style":0}]})", MC::Error::SizeLimit);
	for (i32 index = 1; index < argc; index++)
	{
		std::ifstream file(argv[index], std::ios::binary);
		assert(file);
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const auto sample = Import(text);
		assert(!sample.Courses[0].ChartCommands.empty());
		std::string serialized;
		TJA::ConvertParsedToText(sample, serialized, TJA::SaveFormat::Current);
		TJA::ErrorList sampleErrors;
		const auto sampleReparsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(UTF8::TrimBOM(serialized))), sampleErrors);
		assert(sampleErrors.Errors.empty());
		assert(sampleReparsed.Metadata.TITLE == sample.Metadata.TITLE);
		assert(sampleReparsed.Courses[0].Metadata.BALLOON == sample.Courses[0].Metadata.BALLOON);
		std::cout << "Imported and round-tripped: " << argv[index] << '\n';
	}
	std::cout << "MC-import tests passed.\n";
	return 0;
}
