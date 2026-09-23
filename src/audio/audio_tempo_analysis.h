#pragma once
#include "audio_common.h"
#include <array>

namespace Audio
{
	struct TempoAnalysisCandidate
	{
		f32 BPM = 0.0f;
		Time Offset = Time::Zero();
		f32 Confidence = 0.0f;
	};

	struct TempoAnalysisResult
	{
		std::array<TempoAnalysisCandidate, 3> Candidates {};
		size_t CandidateCount = 0;
		Time AnalyzedDuration = Time::Zero();

		inline b8 IsValid() const { return CandidateCount != 0; }
	};

	// Estimates constant-tempo candidates from decoded PCM without external libraries.
	// Offset is the audio time of the estimated first beat. To apply it to
	// ChartProject::SongOffset, use the negative of this value.
	// Set fixedBPM to a positive value to estimate only the offset at that tempo.
	TempoAnalysisResult AnalyzeTempo(const PCMSampleBuffer& buffer, Time start = Time::Zero(), Time duration = Time::Zero(), f64 fixedBPM = 0.0);
}
