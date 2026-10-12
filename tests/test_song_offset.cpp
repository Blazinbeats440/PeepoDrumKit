#include "peepo_drum_kit/chart.h"
#include <cstdlib>
#include <iostream>

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

static TJA::ParsedTJA Parse(std::string_view text)
{
	TJA::ErrorList errors;
	if (UTF8::HasBOM(text)) text = UTF8::TrimBOM(text);
	TJA::ParsedTJA parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(text)), errors);
	for (const auto& error : errors.Errors)
		std::cerr << "Line " << error.LineIndex << ": " << error.Description << '\n';
	Require(errors.Errors.empty(), "TJA parses without errors");
	return parsed;
}

static std::string Save(const TJA::ParsedTJA& parsed)
{
	std::string text;
	TJA::ConvertParsedToText(parsed, text, TJA::SaveFormat::Current);
	return text;
}

int main()
{
	struct OffsetCase { cstr Input; i64 ExpectedMS; cstr ExpectedText; };
	static constexpr OffsetCase cases[] = {
		{ "0", 0, "0.000" }, { "-0", 0, "0.000" },
		{ "1", 1000, "1.000" }, { "-1", -1000, "-1.000" },
		{ "1.2340000867843628", 1234, "1.234" },
		{ "-1.2340000867843628", -1234, "-1.234" },
		{ "1.23449", 1234, "1.234" }, { "1.23451", 1235, "1.235" },
		{ "-1.23449", -1234, "-1.234" }, { "-1.23451", -1235, "-1.235" },
		{ "1.2355", 1236, "1.236" }, { "-1.2355", -1236, "-1.236" },
		{ "0.00049", 0, "0.000" }, { "-0.00049", 0, "0.000" },
		{ "0.0005", 1, "0.001" }, { "-0.0005", -1, "-0.001" },
		{ "1.9995", 2000, "2.000" }, { "-1.9995", -2000, "-2.000" },
		{ "3000000.1234", 3000000123LL, "3000000.123" },
		{ "-3000000.1234", -3000000123LL, "-3000000.123" },
	};
	for (const auto& offsetCase : cases)
	{
		const std::string input = std::string("TITLE:Offset\nWAVE:song.ogg\nBPM:173.125\nOFFSET:") + offsetCase.Input +
			"\nDEMOSTART:7.25\nCOURSE:Oni\n#START\n1000,\n#END\n";
		const TJA::ParsedTJA parsed = Parse(input);
		const std::string expectedLine = std::string("OFFSET:") + offsetCase.ExpectedText;
		const std::string directSave = Save(parsed);
		Require(directSave.find(expectedLine + "\n") != std::string::npos || directSave.find(expectedLine + "\r\n") != std::string::npos,
			"direct save uses three decimal places and rounds to nearest millisecond");
		ChartProject chart;
		Require(CreateChartProjectFromTJA(parsed, chart), "editor chart imports");
		const Time expectedOffset = Time::FromMS(static_cast<f64>(offsetCase.ExpectedMS));
		Require(chart.SongOffset == parsed.Metadata.OFFSET, "import preserves timing until the chart is edited or saved");
		for (int iteration = 0; iteration < 3; ++iteration)
		{
			TJA::ParsedTJA exported;
			Require(ConvertChartProjectToTJA(chart, exported, false), "editor chart exports");
			const std::string text = Save(exported);
			Require(text.find(expectedLine + "\n") != std::string::npos || text.find(expectedLine + "\r\n") != std::string::npos,
				"editor save keeps offset stable across round trips");
			Require(text.find("BPM:173.125") != std::string::npos && text.find("DEMOSTART:7.25") != std::string::npos,
				"other timing metadata is preserved");
			const TJA::ParsedTJA reparsed = Parse(text);
			ChartProject reimported;
			Require(CreateChartProjectFromTJA(reparsed, reimported), "saved chart reimports");
			Require(reimported.SongOffset == expectedOffset, "reimport keeps the same whole millisecond offset");
			chart = std::move(reimported);
		}
	}
	Require(Save(Parse("TITLE:NoAudio\nCOURSE:Oni\n#START\n0000,\n#END\n")).find("OFFSET:") == std::string::npos,
		"default offset remains omitted when no audio is specified");
	std::cout << "Song offset tests passed\n";
	return 0;
}
