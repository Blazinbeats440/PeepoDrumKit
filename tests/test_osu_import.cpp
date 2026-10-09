#include "file_format_osu.h"
#include "peepo_drum_kit/chart.h"
#include <fstream>
#include <iterator>

using namespace PeepoDrumKit;
namespace PeepoDrumKit::i18n { cstr HashToString(u32) { return "Test"; } }
namespace PeepoDrumKit
{
	SettingsReflectionMap StaticallyInitializeAppSettingsReflectionMap() { return {}; }
	void ChartCourse::RecalculateSENotes(BranchType) {}
	void ChartCourse::RecalculateComboCounts(BranchType) {}
}

static void Require(b8 condition, cstr message)
{
	if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}

static TJA::ParsedTJA Import(std::string_view text)
{
	TJA::ParsedTJA parsed;
	OSU::Error error;
	Require(OSU::ConvertToTJA(text, parsed, error), "OSU imports");
	Require(error == OSU::Error::None && parsed.Courses.size() == 1, "one imported course");
	Require(parsed.Courses[0].Metadata.COURSE == TJA::DifficultyType::Oni && parsed.Courses[0].Metadata.OmitLEVEL, "Oni with unspecified level");
	return parsed;
}

static void Reject(std::string_view text, OSU::Error expected)
{
	TJA::ParsedTJA parsed;
	parsed.Metadata.TITLE = "Keep current chart";
	OSU::Error error;
	Require(!OSU::ConvertToTJA(text, parsed, error) && error == expected, "invalid chart is rejected");
	Require(parsed.Metadata.TITLE == "Keep current chart", "failed import keeps output intact");
}

static ChartProject Create(const TJA::ParsedTJA& parsed)
{
	ChartProject chart;
	Require(CreateChartProjectFromTJA(parsed, chart), "editor chart imports");
	Require(chart.Courses[0]->LevelUnspecified && chart.Courses[0]->Level == 0, "editor keeps level unspecified");
	return chart;
}

static ChartProject RoundTrip(const ChartProject& chart)
{
	TJA::ParsedTJA exported;
	Require(ConvertChartProjectToTJA(chart, exported, false), "editor chart exports");
	std::string text;
	TJA::ConvertParsedToText(exported, text, TJA::SaveFormat::Current);
	Require(text.find("LEVEL:") == std::string::npos, "saved TJA omits LEVEL");
	TJA::ErrorList errors;
	const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(UTF8::TrimBOM(text))), errors);
	Require(errors.Errors.empty(), "saved TJA parses without errors");
	return Create(parsed);
}

static f64 SongMilliseconds(const ChartProject& chart, Beat beat)
{
	return (chart.Courses[0]->BeatToPlaybackTime(beat) - chart.SongOffset).ToSec() * 1000;
}

int main(int argc, char** argv)
{
	const std::string header = "osu file format v14\n[General]\nAudioFilename:song.ogg\nPreviewTime:15000\nMode:1\n"
		"[Metadata]\nTitle:Romanised\nTitleUnicode:曲名🥁\nArtist:Artist\nArtistUnicode:作者\nCreator:Mapper\nVersion:Easy\n"
		"[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n1000,500,4,0,0,100,1,0\n[HitObjects]\n";
	const auto basic = Import(header + "256,192,1000,1,0\n256,192,1123,5,2\n256,192,1246,1,4\n256,192,1369,1,12\n");
	Require(basic.Metadata.TITLE == u8"曲名🥁" && basic.Metadata.SUBTITLE == u8"--作者", "Unicode metadata");
	Require(basic.Metadata.WAVE == "song.ogg" && basic.Metadata.MAKER == "Mapper" && basic.Metadata.DEMOSTART.ToSec() == 15, "audio and metadata");
	Require(basic.Metadata.OFFSET.ToSec() == -1, "original offset without 15ms adjustment");
	const auto chart = Create(basic);
	const NoteType types[] = { NoteType::Don, NoteType::Ka, NoteType::DonBig, NoteType::KaBig };
	Require(chart.Courses[0]->Notes_Normal.size() == 4, "all circles retained");
	for (size_t index = 0; index < 4; index++)
	{
		const auto& note = chart.Courses[0]->Notes_Normal[index];
		Require(note.Type == types[index], "hitsound note conversion");
		Require(std::abs(SongMilliseconds(chart, note.BeatTime) - (1000 + 123 * index)) < 0.025, "fine circle timing");
	}
	const auto restored = RoundTrip(chart);
	for (size_t index = 0; index < 4; index++) Require(std::abs(SongMilliseconds(restored, restored.Courses[0]->Notes_Normal[index].BeatTime) - (1000 + 123 * index)) < 0.025, "circle timing survives save");
	ChartProject numbered = Create(basic);
	numbered.Courses[0]->Level = 8;
	TJA::ParsedTJA numberedOutput;
	Require(ConvertChartProjectToTJA(numbered, numberedOutput, false) && !numberedOutput.Courses[0].Metadata.OmitLEVEL && numberedOutput.Courses[0].Metadata.LEVEL == 8, "user can set level later");

	const auto sliders = Create(Import(header + "256,192,1000,2,0,L|300:192,2,50,0|2|4\n"
		"256,192,2000,2,0,L|300:192,1,200\n256,192,3500,2,2,L|300:192,1,100\n"
		"256,192,4500,8,0,5000\n"));
	const auto& sliderNotes = sliders.Courses[0]->Notes_Normal;
	Require(sliderNotes.size() == 6, "slider edges and rolls");
	Require(sliderNotes[0].Type == NoteType::Don && sliderNotes[1].Type == NoteType::Ka && sliderNotes[2].Type == NoteType::DonBig, "short slider edge sounds");
	Require(sliderNotes[3].Type == NoteType::Drumroll && sliderNotes[4].Type == NoteType::DrumrollBig, "original roll size rule");
	Require(std::abs(SongMilliseconds(sliders, sliderNotes[3].GetEnd()) - 3000) < 0.025, "roll duration");
	Require(sliderNotes[5].Type == NoteType::Balloon && sliderNotes[5].BalloonPopCount == 4, "original spinner hit count");
	RoundTrip(sliders);

	const std::string precise = "osu file format v128\n[General]\nMode:1\nAudioFilename:song.ogg\n[TimingPoints]\n"
		"1000.123456789,487.123456,3,0,0,100,1,0\n1750.23456789,-50,4,0,0,100,0,1\n"
		"2333.3456789,333.333333,5,0,0,100,1,0\n[HitObjects]\n"
		"256,192,1500.234567,1,0\n256,192,2333.3456789,1,2\n256,192,2999.456789,1,4\n256,192,90000.987654,1,0\n";
	const auto timingChart = Create(Import(precise));
	const auto timingRestored = RoundTrip(timingChart);
	const f64 timestamps[] = { 1500.234567, 2333.3456789, 2999.456789, 90000.987654 };
	for (const auto* current : { &timingChart, &timingRestored })
	{
		Require(current->Courses[0]->Notes_Normal.size() == 4, "notes across tempo changes");
		for (size_t index = 0; index < 4; index++) Require(std::abs(SongMilliseconds(*current, current->Courses[0]->Notes_Normal[index].BeatTime) - timestamps[index]) < 0.025, "fractional times and BPM boundaries remain accurate");
	}
	Require(!timingChart.Courses[0]->GoGoRanges.empty() && !timingChart.Courses[0]->DelayChanges_Normal.empty(), "gogo and BPM timing corrections");
	const auto triple = Create(Import("osu file format v14\n[TimingPoints]\n0,500,3\n[HitObjects]\n256,192,0,1,0\n256,192,1500,1,2\n"));
	Require(triple.Courses[0]->TempoMap.Signature[0].Signature == TimeSignature(3, 4), "initial non-4/4 signature");
	std::string closeChanges = "osu file format v128\n[TimingPoints]\n0,500\n";
	for (i32 index = 0; index < 8; index++) closeChanges += std::to_string(1000 + 0.001 * index) + ",500\n";
	closeChanges += "[HitObjects]\n256,192,1250.007,1,0\n";
	const auto closeChart = RoundTrip(Create(Import(closeChanges)));
	Require(std::abs(SongMilliseconds(closeChart, closeChart.Courses[0]->Notes_Normal[0].BeatTime) - 1250.007) < 0.025, "sub-tick timing changes keep accumulated corrections");

	for (i32 version : {0, 1, 2, 3, 4, 5, 8, 14, 128, 129, 999})
	{
		const auto old = Create(Import("osu file format v" + std::to_string(version) + "\n[TimingPoints]\n1000,500\n[HitObjects]\n256,192,750,1,0\n"));
		Require(std::abs(SongMilliseconds(old, old.Courses[0]->Notes_Normal[0].BeatTime) - 750) < 0.025, "legacy timing and unrestricted versions");
	}
	const auto stdSlider = Create(Import("osu file format v3\n[Difficulty]\nSliderMultiplier:1\n[TimingPoints]\n0,500\n[HitObjects]\n256,192,0,2,0,L|300:192,1,150\n"));
	Require(stdSlider.Courses[0]->Notes_Normal.size() == 2 && stdSlider.Courses[0]->Notes_Normal[1].Type == NoteType::Don, "standard-mode slider threshold");
	Create(Import("\xEF\xBB\xBF" + header + "256,192,1000,1,1\n"));
	Create(Import("osu file format v14\n[TimingPoints]\n0,500\n[HitObjects]\n"));
	for (i32 mode : {2, 3}) Reject("osu file format v14\n[General]\nMode:" + std::to_string(mode) + "\n[TimingPoints]\n0,500\n[HitObjects]\n256,192,0,1,0\n", OSU::Error::UnsupportedMode);
	Reject("not an osu file", OSU::Error::InvalidChart);
	Reject("osu file format v14\n[HitObjects]\n256,192,0,1,0\n", OSU::Error::InvalidChart);
	Reject("osu file format v14\n[TimingPoints]\n0,0\n", OSU::Error::InvalidChart);
	Reject("osu file format v14\n[TimingPoints]\n0,nan\n", OSU::Error::InvalidChart);
	Reject(header + "256,192,1000,2,0,L|300:192,0,100\n", OSU::Error::InvalidChart);
	Reject(header + "256,192,1000,8,0,999\n", OSU::Error::InvalidChart);
	Reject(header + "256,192,1000000000,1,0\n", OSU::Error::SizeLimit);
	for (i32 index = 1; index < argc; index++)
	{
		std::ifstream file(argv[index], std::ios::binary);
		Require(static_cast<bool>(file), "sample opens");
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		RoundTrip(Create(Import(text)));
		std::cout << "Imported and saved: " << argv[index] << '\n';
	}
	std::cout << "OSU-import tests passed.\n";
}
