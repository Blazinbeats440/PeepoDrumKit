#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

namespace PeepoDrumKit
{
	enum class JPOSEasing { Linear, EaseIn, EaseOut, EaseInOut, EaseOutIn };
	enum class JPOSEasingError { None, InvalidRange, InvalidGrid, InvalidMove, InvalidStrength, InvalidTime, IntervalTooShort, InvalidMotion, InvalidMotionGrid, InvalidMotionTiming, InvalidCurveMotion, InvalidWaypoints };
	inline constexpr int MaxJPOSEasingGrid = 4096;
	inline constexpr double MaxJPOSEasingStrength = 4.0;
	enum class JPOSMotionType { OneWay, RoundTrip, Shake, Ellipse, Bounce, Zigzag, Polygon, Waypoints };
	enum class JPOSPolygonShape { Rectangle, Triangle };
	struct JPOSWaypoint
	{
		float ArrivalPercent = 100.0f;
		std::complex<float> Position = {};
		JPOSEasing Easing = JPOSEasing::EaseOut;
		float Strength = 1.0f;
	};
	struct JPOSMotionSettings
	{
		JPOSMotionType Type = JPOSMotionType::RoundTrip;
		int RoundTrips = 1;
		float OutboundRatio = 0.5f;
		bool UseSameEasing = true;
		JPOSEasing ReturnEasing = JPOSEasing::EaseOut;
		float ReturnStrength = 1.0f;
		int Repetitions = 1;
		bool Damped = false;
		float DampingStrength = 1.0f;
		float RadiusX = 100.0f, RadiusY = 100.0f;
		float StartAngle = 0.0f;
		bool Clockwise = true;
		bool EasingPerCycle = false;
		float JumpHeight = 100.0f;
		bool JumpDamped = false;
		float JumpDampingStrength = 1.0f;
		float LateralAmplitude = 50.0f;
		bool FirstSideLeft = true;
		bool SmoothZigzag = false;
		float ReferenceAngle = 0.0f;
		bool ZigzagDamped = false;
		float ZigzagDampingStrength = 1.0f;
		JPOSPolygonShape PolygonShape = JPOSPolygonShape::Rectangle;
		float Width = 200.0f, Height = 100.0f;
		float RotationAngle = 0.0f;
		bool WaypointSameEasing = true;
		std::vector<JPOSWaypoint> Waypoints = { { 0.0f, {} }, { 100.0f, { 100.0f, 0.0f } } };
	};

	inline std::vector<std::complex<double>> GetJPOSPolygonVertices(const JPOSMotionSettings& motion)
	{
		std::vector<std::complex<double>> vertices = { {}, { motion.Width, 0.0 } };
		if (motion.PolygonShape == JPOSPolygonShape::Rectangle)
		{
			vertices.push_back({ motion.Width, motion.Height });
			vertices.push_back({ 0.0, motion.Height });
		}
		else vertices.push_back({ motion.Width * 0.5, motion.Height });
		vertices.push_back({});
		if (!motion.Clockwise) std::reverse(vertices.begin(), vertices.end());
		const double angle = std::remainder(static_cast<double>(motion.RotationAngle), 360.0) * 3.14159265358979323846 / 180.0;
		for (auto& vertex : vertices) vertex *= std::polar(1.0, angle);
		return vertices;
	}

	inline double EvaluateJPOSEasing(JPOSEasing easing, double progress, double strength = 1.0)
	{
		const double t = std::clamp(progress, 0.0, 1.0);
		const double safeStrength = std::isfinite(strength) ? std::clamp(strength, 0.0, MaxJPOSEasingStrength) : 1.0;
		if (safeStrength == 0.0) return t;
		const double power = 1.0 + safeStrength;
		switch (easing)
		{
		case JPOSEasing::EaseIn: return std::pow(t, power);
		case JPOSEasing::EaseOut: return 1.0 - std::pow(1.0 - t, power);
		case JPOSEasing::EaseInOut: return (t < 0.5) ? 0.5 * std::pow(2.0 * t, power) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - t), power);
		case JPOSEasing::EaseOutIn: return (t < 0.5) ? 0.5 * (1.0 - std::pow(1.0 - 2.0 * t, power)) : 0.5 + 0.5 * std::pow(2.0 * t - 1.0, power);
		default: return t;
		}
	}

	inline double InverseJPOSEasing(JPOSEasing easing, double progress, double strength)
	{
		const double t = std::clamp(progress, 0.0, 1.0);
		const double power = 1.0 / (1.0 + strength);
		switch (easing)
		{
		case JPOSEasing::EaseIn: return std::pow(t, power);
		case JPOSEasing::EaseOut: return 1.0 - std::pow(1.0 - t, power);
		case JPOSEasing::EaseInOut: return (t < 0.5) ? 0.5 * std::pow(2.0 * t, power) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - t), power);
		case JPOSEasing::EaseOutIn: return (t < 0.5) ? 0.5 * (1.0 - std::pow(1.0 - 2.0 * t, power)) : 0.5 + 0.5 * std::pow(2.0 * t - 1.0, power);
		default: return t;
		}
	}

	inline std::complex<double> EvaluateJPOSCurvePosition(const JPOSMotionSettings& motion, std::complex<float> move, double progress)
	{
		constexpr double pi = 3.14159265358979323846;
		const double t = std::clamp(progress, 0.0, 1.0);
		if (t == 0.0) return {};
		if (t == 1.0) return motion.Type == JPOSMotionType::Bounce || motion.Type == JPOSMotionType::Zigzag ? std::complex<double>(move) : std::complex<double>{};
		const double cycles = motion.Repetitions * t;
		if (motion.Type == JPOSMotionType::Zigzag)
		{
			const double length = std::abs(std::complex<double>(move));
			const auto direction = length > 0.0 ? std::complex<double>(move) / length :
				std::polar(1.0, std::remainder(static_cast<double>(motion.ReferenceAngle), 360.0) * pi / 180.0);
			const double wave = motion.SmoothZigzag ? std::sin(2.0 * pi * cycles) : 2.0 / pi * std::asin(std::sin(2.0 * pi * cycles));
			const double envelope = motion.ZigzagDamped ? std::pow(1.0 - t, motion.ZigzagDampingStrength) : 1.0;
			return std::complex<double>(move) * t + direction * std::complex<double>(0.0, motion.FirstSideLeft ? -1.0 : 1.0) * (motion.LateralAmplitude * wave * envelope);
		}
		if (motion.Type == JPOSMotionType::Polygon)
		{
			const auto vertices = GetJPOSPolygonVertices(motion);
			double perimeter = 0.0;
			for (size_t edge = 1; edge < vertices.size(); ++edge) perimeter += std::abs(vertices[edge] - vertices[edge - 1]);
			double distance = (cycles - std::floor(cycles)) * perimeter;
			for (size_t edge = 1; edge < vertices.size(); ++edge)
			{
				const double length = std::abs(vertices[edge] - vertices[edge - 1]);
				if (distance <= length || edge + 1 == vertices.size()) return vertices[edge - 1] + (vertices[edge] - vertices[edge - 1]) * std::clamp(distance / length, 0.0, 1.0);
				distance -= length;
			}
		}
		if (motion.Type == JPOSMotionType::Shake)
		{
			const double envelope = motion.Damped ? std::pow(1.0 - t, motion.DampingStrength) : 1.0;
			return std::complex<double>(move) * (std::sin(2.0 * pi * cycles) * envelope);
		}
		if (motion.Type == JPOSMotionType::Ellipse)
		{
			const double start = std::remainder(static_cast<double>(motion.StartAngle), 360.0) * pi / 180.0;
			const double angle = start + (motion.Clockwise ? 1.0 : -1.0) * 2.0 * pi * std::remainder(cycles, 1.0);
			return { motion.RadiusX * (std::cos(angle) - std::cos(start)), motion.RadiusY * (std::sin(angle) - std::sin(start)) };
		}
		const double heightEnvelope = motion.JumpDamped ? std::pow(1.0 - t, motion.JumpDampingStrength) : 1.0;
		return std::complex<double>(move) * t + std::complex<double>(0.0, -motion.JumpHeight * std::abs(std::sin(pi * cycles)) * heightEnvelope);
	}

	struct JPOSEasingSegment
	{
		int32_t BeatTicks;
		std::complex<float> Move;
		float DurationSeconds;
	};

	struct JPOSEasingResult
	{
		JPOSEasingError Error = JPOSEasingError::None;
		std::vector<JPOSEasingSegment> Segments;
		int ErrorInterval = 0;
		int RequiredDivisions = 0;
	};

	template <typename BeatToSeconds>
	JPOSEasingResult GenerateJPOSEasing(int32_t startTicks, int32_t endTicks, std::complex<float> totalMove,
		int divisionCount, JPOSEasing easing, BeatToSeconds beatToSeconds, double strength = 1.0)
	{
		JPOSEasingResult result;
		const auto fail = [&](JPOSEasingError error)
		{
			result.Error = error;
			result.Segments.clear();
			return result;
		};
		if (startTicks < 0 || endTicks <= startTicks) return fail(JPOSEasingError::InvalidRange);
		const int64_t tickDuration = static_cast<int64_t>(endTicks) - startTicks;
		if (divisionCount < 1 || divisionCount > MaxJPOSEasingGrid || divisionCount > tickDuration)
			return fail(JPOSEasingError::InvalidGrid);
		if (!std::isfinite(totalMove.real()) || !std::isfinite(totalMove.imag()))
			return fail(JPOSEasingError::InvalidMove);
		if (!std::isfinite(strength) || strength < 0.0 || strength > MaxJPOSEasingStrength)
			return fail(JPOSEasingError::InvalidStrength);

		const double startSeconds = beatToSeconds(startTicks);
		const double endSeconds = beatToSeconds(endTicks);
		const double durationSeconds = endSeconds - startSeconds;
		if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || !std::isfinite(durationSeconds) || durationSeconds <= 0.0)
			return fail(JPOSEasingError::InvalidTime);

		result.Segments.reserve(divisionCount);
		int32_t previousTicks = startTicks;
		double previousSeconds = startSeconds;
		std::complex<double> accumulatedMove = {};
		for (int division = 1; division <= divisionCount; ++division)
		{
			const int32_t nextTicks = static_cast<int32_t>(startTicks + (tickDuration * division + divisionCount / 2) / divisionCount);
			const double nextSeconds = (division == divisionCount) ? endSeconds : beatToSeconds(nextTicks);
			const double intervalSeconds = nextSeconds - previousSeconds;
			if (!std::isfinite(nextSeconds) || nextSeconds > endSeconds || intervalSeconds <= 0.0)
				return fail(JPOSEasingError::InvalidTime);
			// Finish each move before the next command, including OpenTaiko's integer-ms timestamps.
			const double intervalMilliseconds = std::floor(intervalSeconds * 1000.0);
			if (intervalMilliseconds < 1.0) return fail(JPOSEasingError::IntervalTooShort);
			if (intervalMilliseconds > std::numeric_limits<int32_t>::max()) return fail(JPOSEasingError::InvalidTime);
			float moveSeconds = static_cast<float>(intervalMilliseconds / 1000.0);
			if (moveSeconds > intervalSeconds) moveSeconds = std::nextafter(moveSeconds, 0.0f);

			const double progress = (nextSeconds - startSeconds) / durationSeconds;
			const std::complex<double> targetMove = std::complex<double>(totalMove) * EvaluateJPOSEasing(easing, progress, strength);
			const std::complex<float> segmentMove = static_cast<std::complex<float>>(targetMove - accumulatedMove);
			if (!std::isfinite(segmentMove.real()) || !std::isfinite(segmentMove.imag()))
				return fail(JPOSEasingError::InvalidMove);
			result.Segments.push_back({ previousTicks, segmentMove, moveSeconds });
			accumulatedMove += std::complex<double>(segmentMove);
			previousTicks = nextTicks;
			previousSeconds = nextSeconds;
		}
		return result;
	}

	template <typename BeatToSeconds>
	JPOSEasingResult GenerateJPOSCurvedMotion(int32_t startTicks, int32_t endTicks, std::complex<float> move,
		int divisionCount, JPOSEasing easing, BeatToSeconds beatToSeconds, double strength, const JPOSMotionSettings& motion)
	{
		JPOSEasingResult result;
		const auto fail = [&](JPOSEasingError error)
		{
			result.Error = error;
			result.Segments.clear();
			return result;
		};
		if (startTicks < 0 || endTicks <= startTicks) return fail(JPOSEasingError::InvalidRange);
		const int64_t tickDuration = static_cast<int64_t>(endTicks) - startTicks;
		if (divisionCount < 1 || divisionCount > MaxJPOSEasingGrid || divisionCount > tickDuration) return fail(JPOSEasingError::InvalidGrid);
		if (!std::isfinite(strength) || strength < 0.0 || strength > MaxJPOSEasingStrength) return fail(JPOSEasingError::InvalidStrength);
		const bool polygon = motion.Type == JPOSMotionType::Polygon, zigzag = motion.Type == JPOSMotionType::Zigzag;
		if (motion.Type != JPOSMotionType::Ellipse && !polygon && (!std::isfinite(move.real()) || !std::isfinite(move.imag()))) return fail(JPOSEasingError::InvalidMove);
		const int phasesPerCycle = motion.Type == JPOSMotionType::Bounce ? 2 : polygon && motion.PolygonShape == JPOSPolygonShape::Triangle ? 3 : 4;
		if (motion.Repetitions < 1 || motion.Repetitions > MaxJPOSEasingGrid / phasesPerCycle) return fail(JPOSEasingError::InvalidCurveMotion);
		if (motion.Type == JPOSMotionType::Shake && motion.Damped && (!std::isfinite(motion.DampingStrength) || motion.DampingStrength < 0.0f || motion.DampingStrength > MaxJPOSEasingStrength))
			return fail(JPOSEasingError::InvalidCurveMotion);
		if (motion.Type == JPOSMotionType::Ellipse && (!std::isfinite(motion.RadiusX) || !std::isfinite(motion.RadiusY) ||
			motion.RadiusX < 0.0f || motion.RadiusY < 0.0f || !std::isfinite(motion.StartAngle))) return fail(JPOSEasingError::InvalidCurveMotion);
		if (motion.Type == JPOSMotionType::Bounce && (!std::isfinite(motion.JumpHeight) || motion.JumpHeight < 0.0f)) return fail(JPOSEasingError::InvalidCurveMotion);
		if (motion.Type == JPOSMotionType::Bounce && motion.JumpDamped && (!std::isfinite(motion.JumpDampingStrength) || motion.JumpDampingStrength < 0.0f || motion.JumpDampingStrength > MaxJPOSEasingStrength))
			return fail(JPOSEasingError::InvalidCurveMotion);
		if (zigzag && (!std::isfinite(motion.LateralAmplitude) || motion.LateralAmplitude < 0.0f || !std::isfinite(motion.ReferenceAngle) ||
			(motion.ZigzagDamped && (!std::isfinite(motion.ZigzagDampingStrength) || motion.ZigzagDampingStrength < 0.0f || motion.ZigzagDampingStrength > MaxJPOSEasingStrength))))
			return fail(JPOSEasingError::InvalidCurveMotion);
		if (polygon && (!std::isfinite(motion.Width) || !std::isfinite(motion.Height) || motion.Width <= 0.0f || motion.Height <= 0.0f ||
			!std::isfinite(motion.RotationAngle) || (motion.PolygonShape != JPOSPolygonShape::Rectangle && motion.PolygonShape != JPOSPolygonShape::Triangle)))
			return fail(JPOSEasingError::InvalidCurveMotion);
		int phaseCount = motion.Repetitions * phasesPerCycle;
		const double startSeconds = beatToSeconds(startTicks), endSeconds = beatToSeconds(endTicks);
		if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds) || !std::isfinite(endSeconds - startSeconds) || endSeconds <= startSeconds) return fail(JPOSEasingError::InvalidTime);
		const bool perCycle = (motion.Type == JPOSMotionType::Ellipse || polygon) && motion.EasingPerCycle;
		std::vector<double> cycleTimes;
		if (perCycle)
		{
			for (int cycle = 0; cycle <= motion.Repetitions; ++cycle)
			{
				const int32_t ticks = static_cast<int32_t>(startTicks + (tickDuration * cycle + motion.Repetitions / 2) / motion.Repetitions);
				const double time = beatToSeconds(ticks);
				if (!std::isfinite(time) || (!cycleTimes.empty() && time <= cycleTimes.back())) return fail(JPOSEasingError::InvalidTime);
				cycleTimes.push_back(time);
			}
		}
		const auto phaseAtTime = [&](double time)
		{
			if (!perCycle) return EvaluateJPOSEasing(easing, (time - startSeconds) / (endSeconds - startSeconds), strength);
			const int cycle = std::clamp(static_cast<int>(std::upper_bound(cycleTimes.begin(), cycleTimes.end(), time) - cycleTimes.begin()) - 1, 0, motion.Repetitions - 1);
			return (cycle + EvaluateJPOSEasing(easing, (time - cycleTimes[cycle]) / (cycleTimes[cycle + 1] - cycleTimes[cycle]), strength)) / motion.Repetitions;
		};
		std::vector<int32_t> boundaries = { startTicks };
		std::vector<double> phases = { 0.0 };
		std::vector<double> polygonPhases = { 0.0 };
		if (polygon)
		{
			const auto vertices = GetJPOSPolygonVertices(motion);
			for (size_t edge = 1; edge < vertices.size(); ++edge) polygonPhases.push_back(polygonPhases.back() + std::abs(vertices[edge] - vertices[edge - 1]));
			const double perimeter = polygonPhases.back();
			for (auto& progress : polygonPhases) progress /= perimeter;
		}
		const bool damped = zigzag ? motion.ZigzagDamped : motion.Type == JPOSMotionType::Shake ? motion.Damped : motion.Type == JPOSMotionType::Bounce && motion.JumpDamped && motion.JumpHeight > 0.0f;
		const double dampingStrength = zigzag ? motion.ZigzagDampingStrength : motion.Type == JPOSMotionType::Shake ? motion.DampingStrength : motion.JumpDampingStrength;
		for (int phase = 1; phase <= phaseCount; ++phase)
		{
			double progress = static_cast<double>(phase) / phaseCount;
			if (polygon) progress = (phase / phasesPerCycle + polygonPhases[phase % phasesPerCycle]) / motion.Repetitions;
			if (damped && dampingStrength > 0.0 && phase % 2 == 1)
			{
				// Decay shifts each extremum towards the preceding zero crossing.
				constexpr double pi = 3.14159265358979323846;
				const double frequency = (motion.Type == JPOSMotionType::Shake || zigzag ? 2.0 : 1.0) * pi * motion.Repetitions;
				double left = static_cast<double>(phase - 1) / phaseCount, right = progress;
				const double sign = ((phase / 2) % 2 == 0) ? 1.0 : -1.0;
				if (zigzag && !motion.SmoothZigzag) progress = std::clamp((1.0 + dampingStrength * left) / (1.0 + dampingStrength), left, right);
				else
				{
					for (int step = 0; step < 48; ++step)
					{
						const double middle = (left + right) * 0.5;
						const double derivative = frequency * std::cos(frequency * middle) * (1.0 - middle) - dampingStrength * std::sin(frequency * middle);
						if (derivative * sign > 0.0) left = middle; else right = middle;
					}
					progress = (left + right) * 0.5;
				}
			}
			phases.push_back(progress);
			if (zigzag && !motion.SmoothZigzag && phase % 2 == 1 && progress < static_cast<double>(phase) / phaseCount)
				phases.push_back(static_cast<double>(phase) / phaseCount);
		}
		phaseCount = static_cast<int>(phases.size()) - 1;
		result.RequiredDivisions = phaseCount;
		if (divisionCount < phaseCount) return fail(JPOSEasingError::InvalidMotionGrid);
		for (int phase = 1; phase <= phaseCount; ++phase)
		{
			const double progress = phases[phase];
			if (phase == phaseCount) { boundaries.push_back(endTicks); continue; }
			double time;
			if (perCycle)
			{
				const int cycle = std::min(static_cast<int>(std::floor(progress * motion.Repetitions + 1e-12)), motion.Repetitions - 1);
				const double local = (progress * motion.Repetitions) - cycle;
				time = cycleTimes[cycle] + (cycleTimes[cycle + 1] - cycleTimes[cycle]) * InverseJPOSEasing(easing, local, strength);
			}
			else time = startSeconds + (endSeconds - startSeconds) * InverseJPOSEasing(easing, progress, strength);
			int32_t left = startTicks, right = endTicks;
			while (static_cast<int64_t>(right) - left > 1)
			{
				const int32_t middle = static_cast<int32_t>(left + (static_cast<int64_t>(right) - left) / 2);
				const double middleTime = beatToSeconds(middle);
				if (!std::isfinite(middleTime)) return fail(JPOSEasingError::InvalidTime);
				if (middleTime < time) left = middle; else right = middle;
			}
			const int32_t ticks = time - beatToSeconds(left) <= beatToSeconds(right) - time ? left : right;
			if (ticks <= boundaries.back() || ticks >= endTicks) return fail(JPOSEasingError::InvalidMotionTiming);
			boundaries.push_back(ticks);
		}
		result.Segments.reserve(divisionCount);
		int assignedDivisions = 0;
		std::complex<double> accumulatedMove = {};
		for (int phase = 0; phase < phaseCount; ++phase)
		{
			const int32_t phaseStart = boundaries[phase], phaseEnd = boundaries[phase + 1];
			const int remaining = divisionCount - assignedDivisions;
			const int64_t minCount = std::max<int64_t>(1, remaining - (static_cast<int64_t>(endTicks) - phaseEnd));
			const int64_t maxCount = std::min<int64_t>(static_cast<int64_t>(phaseEnd) - phaseStart, remaining - (phaseCount - phase - 1));
			if (minCount > maxCount) return fail(JPOSEasingError::InvalidMotionTiming);
			const int64_t idealEndCount = ((static_cast<int64_t>(phaseEnd) - startTicks) * divisionCount + tickDuration / 2) / tickDuration;
			const int count = static_cast<int>(std::clamp<int64_t>(idealEndCount - assignedDivisions, minCount, maxCount));
			const double phaseStartTime = beatToSeconds(phaseStart), phaseEndTime = beatToSeconds(phaseEnd);
			if (!std::isfinite(phaseStartTime) || !std::isfinite(phaseEndTime) || phaseEndTime <= phaseStartTime) return fail(JPOSEasingError::InvalidTime);
			const double rawStart = phaseAtTime(phaseStartTime), rawEnd = phaseAtTime(phaseEndTime);
			int32_t previousTicks = phaseStart;
			double previousSeconds = phaseStartTime;
			for (int division = 1; division <= count; ++division)
			{
				const int32_t nextTicks = static_cast<int32_t>(phaseStart + ((static_cast<int64_t>(phaseEnd) - phaseStart) * division + count / 2) / count);
				const double nextSeconds = beatToSeconds(nextTicks), intervalSeconds = nextSeconds - previousSeconds;
				if (!std::isfinite(nextSeconds) || nextSeconds > endSeconds || intervalSeconds <= 0.0) return fail(JPOSEasingError::InvalidTime);
				const double milliseconds = std::floor(intervalSeconds * 1000.0);
				if (milliseconds < 1.0) return fail(JPOSEasingError::IntervalTooShort);
				if (milliseconds > std::numeric_limits<int32_t>::max()) return fail(JPOSEasingError::InvalidTime);
				float duration = static_cast<float>(milliseconds / 1000.0);
				if (duration > intervalSeconds) duration = std::nextafter(duration, 0.0f);
				// Preserve exact geometric landmarks after rounding their times to beat ticks.
				const double fraction = rawEnd > rawStart ? std::clamp((phaseAtTime(nextSeconds) - rawStart) / (rawEnd - rawStart), 0.0, 1.0) :
					(nextSeconds - phaseStartTime) / (phaseEndTime - phaseStartTime);
				const double progress = division == count ? phases[phase + 1] : phases[phase] + (phases[phase + 1] - phases[phase]) * fraction;
				const std::complex<double> target = EvaluateJPOSCurvePosition(motion, move, progress);
				const auto segmentMove = static_cast<std::complex<float>>(target - accumulatedMove);
				if (!std::isfinite(segmentMove.real()) || !std::isfinite(segmentMove.imag())) return fail(JPOSEasingError::InvalidMove);
				result.Segments.push_back({ previousTicks, segmentMove, duration });
				accumulatedMove += std::complex<double>(segmentMove);
				previousTicks = nextTicks; previousSeconds = nextSeconds;
			}
			assignedDivisions += count;
		}
		return result;
	}

	template <typename BeatToSeconds>
	JPOSEasingResult GenerateJPOSWaypointMotion(int32_t startTicks, int32_t endTicks, int divisionCount,
		JPOSEasing easing, BeatToSeconds beatToSeconds, double strength, const JPOSMotionSettings& motion)
	{
		JPOSEasingResult result;
		const auto fail = [&](JPOSEasingError error, int interval = 0)
		{
			result.Error = error; result.ErrorInterval = interval; result.Segments.clear(); return result;
		};
		if (startTicks < 0 || endTicks <= startTicks) return fail(JPOSEasingError::InvalidRange);
		const int64_t tickDuration = static_cast<int64_t>(endTicks) - startTicks;
		if (divisionCount < 1 || divisionCount > MaxJPOSEasingGrid || divisionCount > tickDuration) return fail(JPOSEasingError::InvalidGrid);
		const auto& points = motion.Waypoints;
		if (points.size() < 2 || points.size() > MaxJPOSEasingGrid + 1 || points.front().ArrivalPercent != 0.0f ||
			points.front().Position != std::complex<float>{} || points.back().ArrivalPercent != 100.0f) return fail(JPOSEasingError::InvalidWaypoints);
		const int intervalCount = static_cast<int>(points.size()) - 1;
		result.RequiredDivisions = intervalCount;
		if (divisionCount < intervalCount) return fail(JPOSEasingError::InvalidMotionGrid);
		std::vector<int32_t> boundaries;
		for (size_t index = 0; index < points.size(); ++index)
		{
			const auto& point = points[index];
			const int interval = static_cast<int>(std::max<size_t>(1, index));
			if (!std::isfinite(point.ArrivalPercent) || point.ArrivalPercent < 0.0f || point.ArrivalPercent > 100.0f ||
				(index > 0 && point.ArrivalPercent <= points[index - 1].ArrivalPercent)) return fail(JPOSEasingError::InvalidWaypoints, interval);
			if (!std::isfinite(point.Position.real()) || !std::isfinite(point.Position.imag())) return fail(JPOSEasingError::InvalidMove, interval);
			const int32_t ticks = static_cast<int32_t>(startTicks + std::llround(tickDuration * (point.ArrivalPercent / 100.0)));
			if (!boundaries.empty() && ticks <= boundaries.back()) return fail(JPOSEasingError::InvalidMotionTiming, interval);
			boundaries.push_back(ticks);
		}
		int assignedDivisions = 0;
		std::complex<double> accumulated = {};
		for (int interval = 0; interval < intervalCount; ++interval)
		{
			const int32_t begin = boundaries[interval], end = boundaries[interval + 1];
			const int remaining = divisionCount - assignedDivisions;
			const int64_t minimum = std::max<int64_t>(1, remaining - (static_cast<int64_t>(endTicks) - end));
			const int64_t maximum = std::min<int64_t>(static_cast<int64_t>(end) - begin, remaining - (intervalCount - interval - 1));
			if (minimum > maximum) return fail(JPOSEasingError::InvalidMotionTiming, interval + 1);
			const int64_t ideal = ((static_cast<int64_t>(end) - startTicks) * divisionCount + tickDuration / 2) / tickDuration;
			const int count = static_cast<int>(std::clamp<int64_t>(ideal - assignedDivisions, minimum, maximum));
			const auto& destination = points[interval + 1];
			const auto delta = static_cast<std::complex<float>>(std::complex<double>(destination.Position) - std::complex<double>(points[interval].Position));
			auto leg = GenerateJPOSEasing(begin, end, delta, count, motion.WaypointSameEasing ? easing : destination.Easing,
				beatToSeconds, motion.WaypointSameEasing ? strength : destination.Strength);
			if (leg.Error != JPOSEasingError::None) return fail(leg.Error, interval + 1);
			// Anchor every arrival to its absolute relative position, including after float rounding.
			std::complex<double> local = std::complex<double>(points[interval].Position);
			for (size_t index = 0; index < leg.Segments.size(); ++index)
			{
				auto segment = leg.Segments[index];
				local += std::complex<double>(segment.Move);
				const auto target = index + 1 == leg.Segments.size() ? std::complex<double>(destination.Position) : local;
				segment.Move = static_cast<std::complex<float>>(target - accumulated);
				if (!std::isfinite(segment.Move.real()) || !std::isfinite(segment.Move.imag())) return fail(JPOSEasingError::InvalidMove, interval + 1);
				accumulated += std::complex<double>(segment.Move);
				result.Segments.push_back(segment);
			}
			assignedDivisions += count;
		}
		return result;
	}

	template <typename BeatToSeconds>
	JPOSEasingResult GenerateJPOSMotion(int32_t startTicks, int32_t endTicks, std::complex<float> move,
		int divisionCount, JPOSEasing easing, BeatToSeconds beatToSeconds, double strength, const JPOSMotionSettings& motion)
	{
		if (motion.Type == JPOSMotionType::OneWay)
			return GenerateJPOSEasing(startTicks, endTicks, move, divisionCount, easing, beatToSeconds, strength);
		if (motion.Type == JPOSMotionType::Waypoints)
			return GenerateJPOSWaypointMotion(startTicks, endTicks, divisionCount, easing, beatToSeconds, strength, motion);
		if (motion.Type == JPOSMotionType::Shake || motion.Type == JPOSMotionType::Ellipse || motion.Type == JPOSMotionType::Bounce ||
			motion.Type == JPOSMotionType::Zigzag || motion.Type == JPOSMotionType::Polygon)
			return GenerateJPOSCurvedMotion(startTicks, endTicks, move, divisionCount, easing, beatToSeconds, strength, motion);
		JPOSEasingResult result;
		const auto fail = [&](JPOSEasingError error)
		{
			result.Error = error;
			result.Segments.clear();
			return result;
		};
		if (startTicks < 0 || endTicks <= startTicks) return fail(JPOSEasingError::InvalidRange);
		const int64_t tickDuration = static_cast<int64_t>(endTicks) - startTicks;
		if (divisionCount < 1 || divisionCount > MaxJPOSEasingGrid || divisionCount > tickDuration)
			return fail(JPOSEasingError::InvalidGrid);
		if (motion.Type != JPOSMotionType::RoundTrip || motion.RoundTrips < 1 || motion.RoundTrips > MaxJPOSEasingGrid / 2 ||
			!std::isfinite(motion.OutboundRatio) || motion.OutboundRatio <= 0.0f || motion.OutboundRatio >= 1.0f)
			return fail(JPOSEasingError::InvalidMotion);
		const int legCount = motion.RoundTrips * 2;
		if (divisionCount < legCount) return fail(JPOSEasingError::InvalidMotionGrid);
		result.Segments.reserve(divisionCount);
		int assignedDivisions = 0;
		std::complex<double> accumulatedMove = {};
		for (int leg = 0; leg < legCount; ++leg)
		{
			const int cycle = leg / 2;
			const int32_t cycleStart = static_cast<int32_t>(startTicks + (tickDuration * cycle + motion.RoundTrips / 2) / motion.RoundTrips);
			const int32_t cycleEnd = static_cast<int32_t>(startTicks + (tickDuration * (cycle + 1) + motion.RoundTrips / 2) / motion.RoundTrips);
			const int32_t turn = static_cast<int32_t>(cycleStart + std::llround((static_cast<int64_t>(cycleEnd) - cycleStart) * static_cast<double>(motion.OutboundRatio)));
			if (turn <= cycleStart || turn >= cycleEnd) return fail(JPOSEasingError::InvalidMotionTiming);
			const bool outbound = (leg % 2 == 0);
			const int32_t legStart = outbound ? cycleStart : turn, legEnd = outbound ? turn : cycleEnd;
			const int remainingDivisions = divisionCount - assignedDivisions;
			const int remainingLegs = legCount - leg - 1;
			const int64_t minCount = std::max<int64_t>(1, remainingDivisions - (static_cast<int64_t>(endTicks) - legEnd));
			const int64_t maxCount = std::min<int64_t>(static_cast<int64_t>(legEnd) - legStart, remainingDivisions - remainingLegs);
			if (minCount > maxCount) return fail(JPOSEasingError::InvalidMotionTiming);
			const int64_t idealEndCount = ((static_cast<int64_t>(legEnd) - startTicks) * divisionCount + tickDuration / 2) / tickDuration;
			const int count = static_cast<int>(std::clamp<int64_t>(idealEndCount - assignedDivisions, minCount, maxCount));
			const auto legResult = GenerateJPOSEasing(legStart, legEnd, outbound ? move : -move, count,
				(outbound || motion.UseSameEasing) ? easing : motion.ReturnEasing, beatToSeconds,
				(outbound || motion.UseSameEasing) ? strength : motion.ReturnStrength);
			if (legResult.Error != JPOSEasingError::None) return fail(legResult.Error);
			for (size_t index = 0; index < legResult.Segments.size(); ++index)
			{
				JPOSEasingSegment segment = legResult.Segments[index];
				// Anchor every turn and return to prevent drift across repeated trips.
				if (index + 1 == legResult.Segments.size())
					segment.Move = static_cast<std::complex<float>>((outbound ? std::complex<double>(move) : std::complex<double>{}) - accumulatedMove);
				accumulatedMove += std::complex<double>(segment.Move);
				result.Segments.push_back(segment);
			}
			assignedDivisions += count;
		}
		return result;
	}
}
