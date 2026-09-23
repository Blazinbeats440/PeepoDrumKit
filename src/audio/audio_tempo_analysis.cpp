#include "audio_tempo_analysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace Audio
{
	namespace
	{
		struct Onset
		{
			f64 Frame = 0.0;
			f32 Strength = 0.0f;
		};

		constexpr f64 MinBPM = 60.0;
		constexpr f64 MaxBPM = 300.0;
		constexpr i64 AnalysisWindow = 1024;
		constexpr i64 AnalysisHop = 512;
		constexpr i64 MinimumOnsetDistanceMS = 80;
		constexpr i64 GapWindowSamples = 2048;
		constexpr f64 OffsetRefinementRangeMS = 40.0;
		constexpr f64 OffsetRefinementStepMS = 0.5;
		constexpr f64 CoarseCandidateMinimumDistanceBPM = 4.0;

		static std::vector<Onset> FindOnsets(const PCMSampleBuffer& buffer, i64 firstFrame, i64 frameCount)
		{
			const i64 lastFrame = firstFrame + frameCount;
			const i64 energyBegin = std::max<i64>(0, firstFrame - AnalysisHop - AnalysisWindow);
			const i64 energyFrameCount = lastFrame - energyBegin;
			std::vector<f64> energyPrefix(static_cast<size_t>(energyFrameCount + 1), 0.0);
			for (i64 frame = 0; frame < energyFrameCount; ++frame)
			{
				f64 mixed = 0.0;
				const i64 sourceFrame = energyBegin + frame;
				for (u32 c = 0; c < buffer.ChannelCount; ++c)
					mixed += ConvertSampleI16ToF32(buffer.InterleavedSamples[sourceFrame * buffer.ChannelCount + c]);
				mixed /= static_cast<f64>(buffer.ChannelCount);
				energyPrefix[static_cast<size_t>(frame + 1)] = energyPrefix[static_cast<size_t>(frame)] + mixed * mixed;
			}

			const auto frameEnergy = [&](i64 frame, i64 count)
			{
				const i64 begin = Clamp(frame, energyBegin, lastFrame);
				const i64 end = Clamp(frame + count, begin, lastFrame);
				if (end <= begin)
					return 0.0f;
				const f64 sum = energyPrefix[static_cast<size_t>(end - energyBegin)] - energyPrefix[static_cast<size_t>(begin - energyBegin)];
				return static_cast<f32>(std::sqrt(sum / static_cast<f64>(end - begin)));
			};

			const auto bandEnergy = [&](i64 frame, i64 count)
			{
				const i64 begin = Clamp(frame, energyBegin, lastFrame);
				const i64 end = Clamp(frame + count, begin, lastFrame);
				if (end <= begin)
					return std::array<f32, 3> {};

				const f64 lowCoefficient = std::exp(-2.0 * 3.141592653589793 * 180.0 / buffer.SampleRate);
				const f64 midCoefficient = std::exp(-2.0 * 3.141592653589793 * 2200.0 / buffer.SampleRate);
				f64 lowPass = 0.0;
				f64 midPass = 0.0;
				std::array<f64, 3> sum = {};
				for (i64 sourceFrame = begin; sourceFrame < end; ++sourceFrame)
				{
					f64 energy = 0.0;
					for (u32 c = 0; c < buffer.ChannelCount; ++c)
					{
						const f64 sample = ConvertSampleI16ToF32(buffer.InterleavedSamples[sourceFrame * buffer.ChannelCount + c]);
						energy += sample * sample;
					}
					const f64 sample = std::sqrt(energy / buffer.ChannelCount);
					lowPass = lowCoefficient * lowPass + (1.0 - lowCoefficient) * sample;
					midPass = midCoefficient * midPass + (1.0 - midCoefficient) * sample;
					const f64 low = lowPass;
					const f64 mid = midPass - lowPass;
					const f64 high = sample - midPass;
					sum[0] += low * low;
					sum[1] += mid * mid;
					sum[2] += high * high;
				}
				const f64 inverseCount = 1.0 / static_cast<f64>(end - begin);
				return std::array<f32, 3> {
					static_cast<f32>(std::sqrt(sum[0] * inverseCount)),
					static_cast<f32>(std::sqrt(sum[1] * inverseCount)),
					static_cast<f32>(std::sqrt(sum[2] * inverseCount)),
				};
			};

			std::vector<f32> flux;
			for (i64 frame = firstFrame; frame < lastFrame; frame += AnalysisHop)
			{
				const f32 current = frameEnergy(frame, AnalysisWindow);
				const f32 previous = frameEnergy(frame - AnalysisHop, AnalysisWindow);
				const auto currentBands = bandEnergy(frame, AnalysisWindow);
				const auto previousBands = bandEnergy(frame - AnalysisHop, AnalysisWindow);
				f32 bandFlux = 0.0f;
				for (size_t band = 0; band < currentBands.size(); ++band)
					bandFlux += std::max(0.0f, currentBands[band] - previousBands[band]);
				flux.push_back(std::max(0.0f, current - previous) + bandFlux);
			}

			if (flux.size() < 3)
				return {};

			const f32 mean = std::accumulate(flux.begin(), flux.end(), 0.0f) / static_cast<f32>(flux.size());
			f32 variance = 0.0f;
			for (f32 value : flux)
				variance += (value - mean) * (value - mean);
			const f32 threshold = mean + std::sqrt(variance / static_cast<f32>(flux.size())) * 0.45f;
			const i64 minimumDistance = std::max<i64>(1, static_cast<i64>(buffer.SampleRate) * MinimumOnsetDistanceMS / 1000 / AnalysisHop);

			std::vector<Onset> onsets;
			for (size_t i = 1; i + 1 < flux.size(); ++i)
			{
				if (flux[i] < threshold || flux[i] < flux[i - 1] || flux[i] < flux[i + 1])
					continue;

				f64 peakOffset = 0.0;
				const f64 curvature = static_cast<f64>(flux[i - 1]) - 2.0 * static_cast<f64>(flux[i]) + static_cast<f64>(flux[i + 1]);
				if (std::abs(curvature) > 0.000001)
					peakOffset = Clamp(0.5 * (static_cast<f64>(flux[i - 1]) - static_cast<f64>(flux[i + 1])) / curvature, -0.5, 0.5);
				const Onset onset { static_cast<f64>(firstFrame + static_cast<i64>(i) * AnalysisHop) + peakOffset * AnalysisHop, flux[i] };
				if (!onsets.empty() && onset.Frame - onsets.back().Frame < minimumDistance * AnalysisHop)
				{
					if (onset.Strength > onsets.back().Strength)
						onsets.back() = onset;
				}
				else
					onsets.push_back(onset);
			}
			return onsets;
		}

		static f64 CircularWindowSupport(const std::vector<f64>& doubledPrefix, i64 histogramSize, i64 center, i64 windowSize)
		{
			if (histogramSize <= 0)
				return 0.0;
			const i64 halfWindow = std::min<i64>(windowSize / 2, (histogramSize - 1) / 2);
			const i64 normalizedCenter = (center % histogramSize + histogramSize) % histogramSize;
			i64 begin = normalizedCenter - halfWindow;
			if (begin < 0)
				begin += histogramSize;
			const i64 end = begin + halfWindow * 2 + 1;
			if (begin < 0 || end < begin || end >= static_cast<i64>(doubledPrefix.size()))
				return 0.0;
			return doubledPrefix[static_cast<size_t>(end)] - doubledPrefix[static_cast<size_t>(begin)];
		}

		static TempoAnalysisCandidate EvaluateInterval(const std::vector<Onset>& onsets, u32 sampleRate, i64 interval, i32 downsample)
		{
			const i64 reducedInterval = std::max<i64>(1, interval >> downsample);
			const i64 sampleStep = i64(1) << downsample;
			std::vector<f64> histogram(static_cast<size_t>(reducedInterval), 0.0);
			f64 totalStrength = 0.0;
			for (const Onset& onset : onsets)
			{
				const i64 phase = static_cast<i64>(std::fmod(onset.Frame, static_cast<f64>(interval)));
				const i64 position = Clamp<i64>(phase / sampleStep, 0, reducedInterval - 1);
				histogram[static_cast<size_t>(position)] += onset.Strength;
				totalStrength += onset.Strength;
			}
			std::vector<f64> doubledPrefix(static_cast<size_t>(reducedInterval * 2 + 1), 0.0);
			for (i64 i = 0; i < reducedInterval * 2; ++i)
				doubledPrefix[static_cast<size_t>(i + 1)] = doubledPrefix[static_cast<size_t>(i)] + histogram[static_cast<size_t>(i % reducedInterval)];

			const i64 windowSize = std::min<i64>(reducedInterval, std::max<i64>(8, GapWindowSamples >> downsample));
			f64 bestScore = 0.0;
			i64 bestPosition = 0;
			for (const Onset& onset : onsets)
			{
				const i64 phase = static_cast<i64>(std::fmod(onset.Frame, static_cast<f64>(interval)));
				const i64 position = Clamp<i64>(phase / sampleStep, 0, reducedInterval - 1);
				const i64 offbeatPosition = (position + reducedInterval / 2) % reducedInterval;
				const f64 score = CircularWindowSupport(doubledPrefix, reducedInterval, position, windowSize) + CircularWindowSupport(doubledPrefix, reducedInterval, offbeatPosition, windowSize) * 0.5;
				if (score > bestScore)
				{
					bestScore = score;
					bestPosition = position;
				}
			}

			TempoAnalysisCandidate result;
			result.BPM = static_cast<f32>(static_cast<f64>(sampleRate) * 60.0 / static_cast<f64>(interval));
			result.Offset = Time::FromSec(static_cast<f64>(bestPosition * sampleStep) / static_cast<f64>(sampleRate));
			result.Confidence = (totalStrength > 0.0) ? static_cast<f32>(bestScore / totalStrength) : 0.0f;
			return result;
		}

		static void RefineTempoWithOnsets(const std::vector<Onset>& onsets, u32 sampleRate, f64 fixedBPM, TempoAnalysisCandidate& candidate)
		{
			if (onsets.size() < 2 || candidate.BPM <= 0.0f)
				return;

			const b8 refineBPM = fixedBPM <= 0.0;
			f64 interval = static_cast<f64>(sampleRate) * 60.0 / (refineBPM ? candidate.BPM : fixedBPM);
			f64 offset = candidate.Offset.ToSec() * sampleRate;
			for (i32 pass = 0; pass < 2; ++pass)
			{
				f64 totalWeight = 0.0;
				f64 meanBeat = 0.0;
				f64 meanFrame = 0.0;
				for (const Onset& onset : onsets)
				{
					const f64 beat = std::round((onset.Frame - offset) / interval);
					const f64 residual = onset.Frame - (offset + beat * interval);
					if (std::abs(residual) > interval * 0.15)
						continue;
					const f64 weight = onset.Strength;
					totalWeight += weight;
					meanBeat += weight * beat;
					meanFrame += weight * onset.Frame;
				}
				if (totalWeight <= 0.0)
					return;
				meanBeat /= totalWeight;
				meanFrame /= totalWeight;

				f64 covariance = 0.0;
				f64 variance = 0.0;
				for (const Onset& onset : onsets)
				{
					const f64 beat = std::round((onset.Frame - offset) / interval);
					const f64 residual = onset.Frame - (offset + beat * interval);
					if (std::abs(residual) > interval * 0.15)
						continue;
					const f64 weight = onset.Strength;
					const f64 beatDelta = beat - meanBeat;
					covariance += weight * beatDelta * (onset.Frame - meanFrame);
					variance += weight * beatDelta * beatDelta;
				}
				if (refineBPM && variance > 0.0)
					interval = covariance / variance;
				offset = meanFrame - interval * meanBeat;
			}

			if (interval <= 0.0)
				return;
			candidate.BPM = static_cast<f32>(refineBPM ? static_cast<f64>(sampleRate) * 60.0 / interval : fixedBPM);
			candidate.Offset = Time::FromSec(std::fmod(std::fmod(offset, interval) + interval, interval) / sampleRate);
		}

		static std::vector<f32> ComputeWaveformSlopes(const PCMSampleBuffer& buffer, i64 firstFrame, i64 frameCount)
		{
			std::vector<f32> slopes(static_cast<size_t>(frameCount), 0.0f);
			const i64 halfWindow = std::max<i64>(1, buffer.SampleRate / 20);
			const i64 lastFrame = firstFrame + frameCount;
			if (frameCount < halfWindow * 2)
				return slopes;

			const auto absoluteSample = [&](i64 frame)
			{
				f64 mixed = 0.0;
				for (u32 c = 0; c < buffer.ChannelCount; ++c)
					mixed += std::abs(static_cast<f64>(ConvertSampleI16ToF32(buffer.InterleavedSamples[frame * buffer.ChannelCount + c])));
				return mixed / static_cast<f64>(buffer.ChannelCount);
			};

			f64 left = 0.0;
			f64 right = 0.0;
			for (i64 i = 0; i < halfWindow; ++i)
			{
				left += absoluteSample(firstFrame + i);
				right += absoluteSample(firstFrame + i + halfWindow);
			}
			for (i64 frame = firstFrame + halfWindow; frame < lastFrame - halfWindow; ++frame)
			{
				slopes[static_cast<size_t>(frame - firstFrame)] = static_cast<f32>(std::max(0.0, (right - left) / static_cast<f64>(halfWindow)));
				const f64 current = absoluteSample(frame);
				left += current - absoluteSample(frame - halfWindow);
				right += absoluteSample(frame + halfWindow) - current;
			}
			return slopes;
		}

		static f64 SampleSlope(const std::vector<f32>& slopes, i64 firstFrame, f64 frame)
		{
			const f64 localFrame = frame - firstFrame;
			if (slopes.empty() || localFrame < 0.0 || localFrame >= static_cast<f64>(slopes.size() - 1))
				return 0.0;
			const i64 index = static_cast<i64>(localFrame);
			const f64 t = localFrame - static_cast<f64>(index);
			return slopes[static_cast<size_t>(index)] * (1.0 - t) + slopes[static_cast<size_t>(index + 1)] * t;
		}

		static f64 ScoreOffsetWithWaveform(const std::vector<f32>& slopes, i64 firstFrame, f64 offset, f64 interval)
		{
			f64 score = 0.0;
			const f64 firstPosition = offset + std::ceil((firstFrame - offset) / interval) * interval;
			const f64 lastFrame = static_cast<f64>(firstFrame) + static_cast<f64>(slopes.size());
			for (f64 position = firstPosition; position < lastFrame; position += interval)
				score += SampleSlope(slopes, firstFrame, position);
			return score;
		}

		static void RefineOffsetWithWaveform(const std::vector<f32>& slopes, i64 firstFrame, u32 sampleRate, TempoAnalysisCandidate& candidate)
		{
			const f64 secondsPerBeat = 60.0 / candidate.BPM;
			const f64 interval = secondsPerBeat * sampleRate;
			const f64 initialOffset = candidate.Offset.ToSec() * sampleRate;
			const f64 range = OffsetRefinementRangeMS * sampleRate / 1000.0;
			const f64 step = OffsetRefinementStepMS * sampleRate / 1000.0;
			f64 bestOffset = initialOffset;
			f64 bestScore = -1.0;

			// The histogram can only locate an onset within its support window.
			// Search around both the inferred beat and its offbeat so that the
			// selected offset is aligned to the strongest repeated waveform rise.
			for (const f64 baseOffset : { initialOffset, std::fmod(initialOffset + interval * 0.5, interval) })
			{
				for (f64 adjustment = -range; adjustment <= range; adjustment += step)
				{
					const f64 offset = std::fmod(baseOffset + adjustment + interval, interval);
					const f64 score = ScoreOffsetWithWaveform(slopes, firstFrame, offset, interval);
					if (score > bestScore)
					{
						bestScore = score;
						bestOffset = offset;
					}
				}
			}
			candidate.Offset = Time::FromMS(std::round(Time::FromSec(bestOffset / sampleRate).ToMS()));
		}
	}

	TempoAnalysisResult AnalyzeTempo(const PCMSampleBuffer& buffer, Time start, Time duration, f64 fixedBPM)
	{
		TempoAnalysisResult result;
		if (buffer.ChannelCount == 0 || buffer.SampleRate == 0 || buffer.FrameCount <= AnalysisWindow)
			return result;

		const i64 firstFrame = Clamp<i64>(TimeToFrames(start, buffer.SampleRate), 0, buffer.FrameCount - 1);
		const i64 availableFrames = buffer.FrameCount - firstFrame;
		if (availableFrames < AnalysisWindow)
			return result;
		const i64 requestedFrames = (duration > Time::Zero()) ? TimeToFrames(duration, buffer.SampleRate) : availableFrames;
		const i64 frameCount = std::min(requestedFrames, availableFrames);
		if (frameCount < AnalysisWindow)
			return result;
		result.AnalyzedDuration = FramesToTime(frameCount, buffer.SampleRate);

		const std::vector<Onset> onsets = FindOnsets(buffer, firstFrame, frameCount);
		if (onsets.size() < 2)
			return result;

		const i64 minInterval = static_cast<i64>(std::ceil(static_cast<f64>(buffer.SampleRate) * 60.0 / MaxBPM));
		const i64 maxInterval = static_cast<i64>(std::floor(static_cast<f64>(buffer.SampleRate) * 60.0 / MinBPM));
		const b8 useFixedBPM = fixedBPM > 0.0;
		if (useFixedBPM && (fixedBPM < MinBPM || fixedBPM > MaxBPM))
			return result;
		std::vector<TempoAnalysisCandidate> candidates;
		if (useFixedBPM)
		{
			const i64 interval = static_cast<i64>(std::llround(static_cast<f64>(buffer.SampleRate) * 60.0 / fixedBPM));
			candidates.push_back(EvaluateInterval(onsets, buffer.SampleRate, interval, 0));
		}
		else
		{
			for (i64 interval = minInterval; interval <= maxInterval; interval += 10)
				candidates.push_back(EvaluateInterval(onsets, buffer.SampleRate, interval, 3));
		}
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.Confidence > b.Confidence; });

		// Refine several promising sample intervals at full resolution. This is
		// the same interval-first search strategy used by ArrowVortex.
		std::vector<TempoAnalysisCandidate> coarseCandidates;
		for (const TempoAnalysisCandidate& candidate : candidates)
		{
			b8 duplicate = false;
			for (const TempoAnalysisCandidate& existing : coarseCandidates)
				duplicate |= Absolute(candidate.BPM - existing.BPM) < CoarseCandidateMinimumDistanceBPM;
			if (!duplicate)
				coarseCandidates.push_back(candidate);
			if (coarseCandidates.size() >= 8)
				break;
		}
		const size_t coarseCandidateCount = coarseCandidates.size();
		std::vector<TempoAnalysisCandidate> refinedCandidates;
		for (size_t i = 0; i < coarseCandidateCount; ++i)
		{
			const i64 centerInterval = static_cast<i64>(std::llround(static_cast<f64>(buffer.SampleRate) * 60.0 / coarseCandidates[i].BPM));
			const i64 begin = useFixedBPM ? centerInterval : std::max(minInterval, centerInterval - 10);
			const i64 end = useFixedBPM ? centerInterval : std::min(maxInterval, centerInterval + 10);
			for (i64 interval = begin; interval <= end; ++interval)
				refinedCandidates.push_back(EvaluateInterval(onsets, buffer.SampleRate, interval, 0));
		}
		std::stable_sort(refinedCandidates.begin(), refinedCandidates.end(), [](const auto& a, const auto& b) { return a.Confidence > b.Confidence; });

		for (const TempoAnalysisCandidate& candidate : refinedCandidates)
		{
			if (result.CandidateCount >= result.Candidates.size())
				break;
			b8 duplicate = false;
			for (size_t i = 0; i < result.CandidateCount; ++i)
				duplicate |= Absolute(candidate.BPM - result.Candidates[i].BPM) < 4.0f;
			if (!duplicate)
			{
				TempoAnalysisCandidate refinedCandidate = candidate;
				RefineTempoWithOnsets(onsets, buffer.SampleRate, fixedBPM, refinedCandidate);
				result.Candidates[result.CandidateCount++] = refinedCandidate;
			}
		}

		if (result.IsValid())
		{
			const std::vector<f32> slopes = ComputeWaveformSlopes(buffer, firstFrame, frameCount);
			for (size_t i = 0; i < result.CandidateCount; ++i)
				RefineOffsetWithWaveform(slopes, firstFrame, buffer.SampleRate, result.Candidates[i]);
		}
		return result;
	}
}
