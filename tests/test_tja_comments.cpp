#include "file_format_tja.h"
#include <cassert>
#include <string>

int main()
{
	const std::string input =
		"// song\nTITLE:Test\nCOURSE:Oni\n// course before\n#START\n"
		"// first\n// second\n1200, // inline\n0000,\n#END\n// course after\n";
	TJA::ErrorList errors;
	const TJA::ParsedTJA parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(input)), errors);
	assert(errors.Errors.empty());
	assert(parsed.Comments.size() == 1 && parsed.Comments[0] == "song");
	assert(parsed.Courses.size() == 1);
	assert(parsed.Courses[0].Comments.size() == 2);
	assert(parsed.Courses[0].Comments[0] == "course before");
	assert(parsed.Courses[0].Comments[1] == "course after");
	const TJA::ConvertedCourse converted = TJA::ConvertParsedToConvertedCourse(parsed, parsed.Courses[0]);
	assert(!converted.Measures.empty());
	assert(converted.Measures[0].Comments.size() == 3);
	for (const auto& comment : converted.Measures[0].Comments)
		assert(comment.TimeWithinMeasure == Beat::Zero());
	assert(converted.Measures[0].Comments[2].Text == "inline");
	TJA::ParsedTJA normalized;
	normalized.Metadata = parsed.Metadata;
	normalized.Comments = parsed.Comments;
	normalized.Courses.emplace_back().Metadata = parsed.Courses[0].Metadata;
	normalized.Courses[0].Comments = parsed.Courses[0].Comments;
	TJA::ConvertConvertedMeasuresToParsedCommands(converted.Measures, normalized.Courses[0].ChartCommands);
	std::string output;
	TJA::ConvertParsedToText(normalized, output, TJA::SaveFormat::Current);
	if (UTF8::HasBOM(output)) output.erase(0, sizeof(UTF8::BOM_UTF8));
	TJA::ErrorList outputErrors;
	const TJA::ParsedTJA reparsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(output)), outputErrors);
	assert(outputErrors.Errors.empty());
	assert(reparsed.Comments == parsed.Comments);
	assert(reparsed.Courses[0].Comments == parsed.Courses[0].Comments);
	const TJA::ConvertedCourse reconverted = TJA::ConvertParsedToConvertedCourse(reparsed, reparsed.Courses[0]);
	assert(reconverted.Measures[0].Comments.size() == 3);
	for (const auto& comment : reconverted.Measures[0].Comments)
		assert(comment.TimeWithinMeasure == Beat::Zero());
	const std::string halfwayInput = "TITLE:Test\nCOURSE:Oni\n#START\n1200\n// halfway\n3400,\n#END\n";
	TJA::ErrorList halfwayErrors;
	const TJA::ParsedTJA halfwayParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(halfwayInput)), halfwayErrors);
	assert(halfwayErrors.Errors.empty());
	const TJA::ConvertedCourse halfwayConverted = TJA::ConvertParsedToConvertedCourse(halfwayParsed, halfwayParsed.Courses[0]);
	assert(halfwayConverted.Measures[0].Comments.size() == 1);
	assert(halfwayConverted.Measures[0].Comments[0].TimeWithinMeasure == Beat::FromTicks(Beat::TicksPerBeat * 2));
	const std::string branchInput = "TITLE:Test\nCOURSE:Oni\n#START\n#BRANCHSTART p,50,60\n"
		"#N\n1000,\n#E\n// expert\n2000,\n#M\n3000,\n#BRANCHEND\n#END\n";
	TJA::ErrorList branchErrors;
	const TJA::ParsedTJA branchParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(branchInput)), branchErrors);
	assert(branchErrors.Errors.empty());
	const TJA::ConvertedCourse branchConverted = TJA::ConvertParsedToConvertedCourse(branchParsed, branchParsed.Courses[0]);
	assert(!branchConverted.Measures.empty());
	assert(branchConverted.Measures[0].Comments.size() == 1);
	assert(branchConverted.Measures[0].Comments[0].Text == "expert");
	const std::string betweenInput = "TITLE:Test\nCOURSE:Easy\n#START\n0000,\n#END\n"
		"// between courses\nCOURSE:Oni\n#START\n0000,\n#END\n";
	TJA::ErrorList betweenErrors;
	const TJA::ParsedTJA betweenParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(betweenInput)), betweenErrors);
	assert(betweenErrors.Errors.empty());
	assert(betweenParsed.Courses.size() == 2);
	assert(betweenParsed.Courses[0].Comments.size() == 1);
	assert(betweenParsed.Courses[0].Comments[0] == "between courses");
	assert(betweenParsed.Courses[1].Comments.empty());
	return 0;
}
