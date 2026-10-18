#include "chart_editor_widgets.h"
#include "chart_editor_test_play_display.h"
#include "core_io.h"
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace PeepoDrumKit
{
	static f32 VideoTextWidth(ImFont* font, f32 fontSize, std::string_view text)
	{
		if (text.empty()) return 0.0f;
		return font->CalcTextSizeA(fontSize, F32Max, 0.0f, text.data(), text.data() + text.size()).x;
	}

	static size_t NextVideoTextCharacter(std::string_view text, size_t index)
	{
		size_t next = index + 1;
		while (next < text.size() && (static_cast<u8>(text[next]) & 0xC0) == 0x80) ++next;
		return next;
	}

	static size_t VideoTextPrefixThatFits(ImFont* font, f32 fontSize, std::string_view text, f32 width)
	{
		size_t end = 0;
		for (size_t next = 0; end < text.size(); end = next)
		{
			next = NextVideoTextCharacter(text, end);
			if (VideoTextWidth(font, fontSize, text.substr(0, next)) > width) break;
		}
		return end;
	}

	static std::string EllipsizeVideoText(ImFont* font, f32 fontSize, std::string_view text, f32 width)
	{
		if (VideoTextWidth(font, fontSize, text) <= width) return std::string(text);
		constexpr std::string_view ellipsis = "...";
		const f32 availableWidth = width - VideoTextWidth(font, fontSize, ellipsis);
		if (availableWidth <= 0.0f) return std::string(ellipsis);
		return std::string(text.substr(0, VideoTextPrefixThatFits(font, fontSize, text, availableWidth))) + std::string(ellipsis);
	}

	static f32 VideoFadeOpacity(Time time, Time contentStart, Time contentEnd, f32 fadeInSeconds, f32 fadeOutSeconds, f32 frameSeconds)
	{
		const f64 current = time.ToSec();
		if (fadeInSeconds > 0.0f && current < contentStart.ToSec())
		{
			const f64 start = contentStart.ToSec() - fadeInSeconds;
			const f64 progress = std::clamp((current - start) / (fadeInSeconds * 0.9), 0.0, 1.0);
			return static_cast<f32>((1.0 - progress) * (1.0 - progress));
		}
		if (fadeOutSeconds > 0.0f)
		{
			const f64 start = contentEnd.ToSec() + fadeOutSeconds * 0.2;
			if (current >= start)
			{
				const f64 end = contentEnd.ToSec() + fadeOutSeconds - frameSeconds;
				const f64 progress = end > start ? std::clamp((current - start) / (end - start), 0.0, 1.0) : 1.0;
				return static_cast<f32>(progress * progress);
			}
		}
		return 0.0f;
	}

	static constexpr f32 FrameToTime(f32 frame, f32 fps = 60.0f) { return (frame / fps); }
	static constexpr BezierKeyFrame2D GameNoteHitPath[] =
	{
		BezierKeyFrame2D::Linear(FrameToTime({  0 }), vec2 {  615, 386 }),
		BezierKeyFrame2D::Linear(FrameToTime({  1 }), vec2 {  639, 342 }),
		BezierKeyFrame2D::Linear(FrameToTime({  2 }), vec2 {  664, 300 }),
		BezierKeyFrame2D::Linear(FrameToTime({  3 }), vec2 {  693, 260 }),
		BezierKeyFrame2D::Linear(FrameToTime({  4 }), vec2 {  725, 222 }),
		BezierKeyFrame2D::Linear(FrameToTime({  5 }), vec2 {  758, 186 }),
		BezierKeyFrame2D::Linear(FrameToTime({  6 }), vec2 {  793, 153 }),
		BezierKeyFrame2D::Linear(FrameToTime({  7 }), vec2 {  830, 122 }),
		BezierKeyFrame2D::Linear(FrameToTime({  8 }), vec2 {  870, 93  }),
		BezierKeyFrame2D::Linear(FrameToTime({  9 }), vec2 {  912, 66  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 10 }), vec2 {  954, 43  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 11 }), vec2 { 1001, 27  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 12 }), vec2 { 1046, 11  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 13 }), vec2 { 1094, -2  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 14 }), vec2 { 1142, -14 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 15 }), vec2 { 1192, -18 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 16 }), vec2 { 1240, -22 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 17 }), vec2 { 1292, -23 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 18 }), vec2 { 1336, -22 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 19 }), vec2 { 1385, -16 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 20 }), vec2 { 1435, -8  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 21 }), vec2 { 1479, 3   }),
		BezierKeyFrame2D::Linear(FrameToTime({ 22 }), vec2 { 1526, 16  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 23 }), vec2 { 1570, 36  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 24 }), vec2 { 1612, 56  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 25 }), vec2 { 1658, 83  }),
		BezierKeyFrame2D::Linear(FrameToTime({ 26 }), vec2 { 1696, 115 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 27 }), vec2 { 1734, 144 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 28 }), vec2 { 1770, 176 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 29 }), vec2 { 1803, 210 }),
		BezierKeyFrame2D::Linear(FrameToTime({ 30 }), vec2 { 1836, 247 }),
	};

	static constexpr BezierKeyFrame1D GameNoteHitFadeIn[] =
	{
		{ FrameToTime(30.0f), 0.0f, 0.0f, 0.0f },
		{ FrameToTime(39.0f), 1.0f, 1.0f, 1.0f },
	};
	static constexpr BezierKeyFrame1D GameNoteHitFadeOut[] =
	{
		{ FrameToTime(39.0f), 1.0f, 1.0f, 1.0f },
		{ FrameToTime(46.0f), 0.0f, 0.0f, 0.0f },
	};
	static constexpr Time GameNoteHitAnimationDuration = Time::FromSec(GameNoteHitPath[ArrayCount(GameNoteHitPath) - 1].Time); // Time::FromFrames(46.0);
	static constexpr Time GameHandNoteHitSquashDuration = Time::FromSec(0.067);
	static constexpr Time GetTotalGameNoteHitAnimationDuration(NoteType noteType) { return (IsHandNote(noteType) ? GameHandNoteHitSquashDuration + GameNoteHitAnimationDuration : GameNoteHitAnimationDuration); }

	struct NoteHitPathAnimationData
	{
		vec2 PositionOffset = {};
		f32 WhiteFadeIn = 0.0f;
		f32 AlphaFadeOut = 1.0f;
		f32 HandSquashYOffset = 0;
		f32 HandSquashScale = 1.0f;
		Angle HandRotate = Angle::FromRadians(0);
		b8 HasBeenHit = false;
	};

	static NoteHitPathAnimationData GetNoteHitPathAnimation(Time timeSinceHit, f32 extendedLaneWidthFactor, i32 nLanes, i32 iLane, NoteType noteType)
	{
		NoteHitPathAnimationData out {};
		if (out.HasBeenHit = timeSinceHit >= Time::Zero(); out.HasBeenHit) {
			if (nLanes > 2) {
				out.AlphaFadeOut = 0;
				return out;
			}

			if (IsHandNote(noteType)) {
				if (timeSinceHit < GameHandNoteHitSquashDuration) {
					static constexpr f32 squashAmountMax = 0.15;
					// y = 1 ... (1 - dy) ... 1
					// t = 0 ... 1/2      ... 1 => y = (1 - dy) + dy * (2 * (t - 1/2)) ^ 2
					f32 squashFactor = (1 - squashAmountMax) + squashAmountMax * std::pow(2 * (timeSinceHit / GameHandNoteHitSquashDuration - 0.5), 2);
					out.HandSquashScale = squashFactor;
					out.HandSquashYOffset = (GameLaneSlice.TotalHeight() / 2) * (1 - squashFactor);
					if (iLane == 1)
						out.HandSquashYOffset *= -1;
					return out;
				}

				timeSinceHit -= GameHandNoteHitSquashDuration;
			}

			const f32 animationTime = timeSinceHit.ToSec_F32();
			out.PositionOffset = SampleBezierFCurve(GameNoteHitPath, animationTime) - GameNoteHitPath[0].Value;
			out.PositionOffset.x *= extendedLaneWidthFactor;
#if 0 // TODO: Just doesn't really look that great... maybe also needs the other hit effects too
			out.WhiteFadeIn = SampleBezierFCurve(NoteHitFadeIn, animationTime);
			out.AlphaFadeOut = SampleBezierFCurve(NoteHitFadeOut, animationTime);
#endif

			if (IsHandNote(noteType)) {
				// rotation angle is roughly proportional to the height above hit position
				static constexpr Angle rotateMax = Angle::FromDegrees(45);
				static constexpr auto v0 = GameNoteHitPath[1].Value - GameNoteHitPath[0].Value;
				static constexpr auto vt = GameNoteHitPath[std::size(GameNoteHitPath) - 1].Value - GameNoteHitPath[std::size(GameNoteHitPath) - 2].Value;
				// d(cos(theta = w * t + theta0))/dt = -w * sin(theta) = w * cos(thetaV) => cos(thetaV) = sin(90 deg - thetaV) = -sin(theta) = sin(-theta) => thetaV = theta + 90deg
				static auto theta0 = std::atan2(v0.y, v0.x) - PI / 2; // atan2 range: (-pi, pi], clockwise is positive => w > 0
				static auto theta1 = std::atan2(vt.y, vt.x) - PI / 2;
				static auto sin0 = std::sin(theta0);
				f32 theta = ConvertRange(0.0f, GameNoteHitAnimationDuration.ToSec_F32(), theta0, theta1, animationTime);
				out.HandRotate = rotateMax * (sin(theta) - sin0) / ((-1) - sin0); // heightest point is most negative
			}
			if (iLane == 1) {
				out.PositionOffset.y *= -1;
				out.HandRotate *= -1;
			}
		}
		return out;
	}

	struct GogoTransitionData
	{
		f32 FireZoom = 1.0f;
		f32 FireAlphaFade = 1.0f;
		f32 LaneZoomIn = 1.0f;
		f32 LaneAlphaFade = 1.0f;
	};

	static GogoTransitionData getGogoTransition(b8 isGogo, Time timeSinceGogo, Time timeAfterGogo)
	{
		static constexpr auto tAtt = Time::FromSec(0.075); // attack
		static constexpr auto tDec = Time::FromSec(0.20); // decay
		static constexpr auto tBounce = Time::FromSec(0.10);
		static constexpr auto tDecLane = Time::FromSec(0.075);
		static constexpr auto tRel = Time::FromSec(0.15); // release
		static constexpr auto laneBeamZoomAmount = 0.0625;
		static constexpr auto fireZoomInAmount = 2.75;
		static constexpr auto fireBounceAmount = 0.1;
		static constexpr auto fireAlphaMin = 0.25;
		static constexpr auto fireAlphaStable = 0.875;
		static constexpr auto easingOutPow = [](auto x, auto power) { return 1 - pow(1 - x, power); };
		GogoTransitionData res = {};
		// attack + decay + sustain
		res.LaneZoomIn = (timeSinceGogo >= tAtt + tDecLane) ? 1
			: (timeSinceGogo >= tAtt) ? laneBeamZoomAmount + (1 - laneBeamZoomAmount) * (timeSinceGogo - tAtt) / tDecLane
			: isGogo ? laneBeamZoomAmount
			: 0;
		res.LaneAlphaFade = (timeSinceGogo >= tAtt) ? 1 : easingOutPow(timeSinceGogo / tAtt, 2);
		res.FireZoom = (timeSinceGogo >= tAtt + tDec + tBounce) ? 1
			: (timeSinceGogo >= tAtt + tDec) ? 1 + fireBounceAmount * (-cos(2 * PI * easingOutPow((timeSinceGogo - (tAtt + tDec)) / tBounce, 2)) + 1) / 2
			: (timeSinceGogo >= tAtt) ? fireZoomInAmount + (1 - fireZoomInAmount) * easingOutPow((timeSinceGogo - tAtt) / tDec, 1.5)
			: fireZoomInAmount * pow(timeSinceGogo / tAtt, 1.25);
		res.FireAlphaFade = (timeSinceGogo >= tAtt + tDec + tBounce) ? fireAlphaStable
			: (timeSinceGogo >= tAtt + tDec - +tBounce) ? 1 + (fireAlphaStable - 1) * pow((timeSinceGogo - (tAtt + tDec - tBounce)) / (2 * tBounce), 2)
			: (timeSinceGogo >= tAtt) ? 1
			: fireAlphaMin + (1 - fireAlphaMin) * (timeSinceGogo / tAtt);
		if (timeAfterGogo > Time::Zero()) {
			// release
			Time laneFadeOutTime = std::min(timeSinceGogo, tAtt + (isGogo ? Time::Zero() : timeAfterGogo));
			res.LaneAlphaFade *= (laneFadeOutTime > tAtt + tRel) ? 0
				: (laneFadeOutTime > tAtt) ? 1 - easingOutPow((laneFadeOutTime - tAtt) / tRel, 2)
				: 1;
			Time fireFadeOutTime = std::min(timeSinceGogo, tAtt + tDec - tBounce + (isGogo ? Time::Zero() : timeAfterGogo));
			res.FireZoom *= (fireFadeOutTime > tAtt + tDec - tBounce + tRel) ? 0
				: (fireFadeOutTime > tAtt + tDec - tBounce) ? 1 - pow((fireFadeOutTime - (tAtt + tDec - tBounce)) / tRel, 2)
				: 1;
			res.FireAlphaFade *= (fireFadeOutTime > tAtt + tDec - tBounce + tRel) ? 0
				: (fireFadeOutTime > tAtt + tDec - tBounce) ? 1 - pow((fireFadeOutTime - (tAtt + tDec - tBounce)) / tRel, 2)
				: 1;
		}
		return res;
	}

	static constexpr Time TimeSinceNoteHit(Time noteTime, Time cursorTime) { return (cursorTime - noteTime); }

	static u32 InterpolateDrumrollHitColor(NoteType noteType, f32 hitPercentage)
	{
		u32 hitNoteColor = 0xFFFFFFFF;
		if (hitPercentage > 0.0f)
			hitNoteColor = Gui::ColorConvertFloat4ToU32(ImLerp(Gui::ColorConvertU32ToFloat4(hitNoteColor), Gui::ColorConvertU32ToFloat4(NoteColorDrumrollHit), hitPercentage));
		return hitNoteColor;
	}

	constexpr static std::pair<Angle, bool> GetNoteFaceRotationMirror(Tempo tempo, Complex scrollSpeed, NoteType noteType)
	{
		switch (noteType) {
		default:
			return {
				Angle::FromRadians(arg(((tempo.BPM >= 0) == (scrollSpeed.GetRealPart() >= 0)) ? scrollSpeed.cpx : -scrollSpeed.cpx)),
				(scrollSpeed.GetRealPart() < 0),
			}; // prevent top-down rotation; mirror horizontally for negative scroll balloon
		case NoteType::Bomb:
			return { Angle::FromRadians(0), false }; // no rotation
		}
	}

	static void DrawGamePreviewNote(ChartGraphicsResources& gfx, const GameCamera& camera, ImDrawList* drawList, vec2 center, Tempo tempo, Complex scrollSpeed, NoteType noteType, Time currentTime,
		const NoteHitPathAnimationData& hitAnimation = {}, i32 nLanes = 1, i32 iLane = 0)
	{
		SprID spr = SprID::Count;
		switch (noteType)
		{
		case NoteType::Don: { spr = SprID::Game_Note_Don; } break;
		case NoteType::DonBig: { spr = SprID::Game_Note_DonBig; } break;
		case NoteType::DonBigHand: { spr = (hitAnimation.HasBeenHit ? SprID::Game_Note_DonBig : SprID::Game_Note_DonHand); } break;
		case NoteType::Ka: { spr = SprID::Game_Note_Ka; } break;
		case NoteType::KaBig: { spr = SprID::Game_Note_KaBig; } break;
		case NoteType::KaBigHand: { spr = (hitAnimation.HasBeenHit ? SprID::Game_Note_KaBig : SprID::Game_Note_KaHand); } break;
		case NoteType::Drumroll: { spr = SprID::Game_Note_Drumroll; } break;
		case NoteType::DrumrollBig: { spr = SprID::Game_Note_DrumrollBig; } break;
		case NoteType::Balloon: { spr = SprID::Game_Note_Balloon; } break;
		case NoteType::BalloonSpecial: { spr = SprID::Game_Note_BalloonSpecial; } break;
		case NoteType::KaDon: { spr = SprID::Game_Note_KaDon; } break;
		case NoteType::Adlib: { spr = SprID::Game_Note_Adlib; } break;
		case NoteType::Fuse: { spr = SprID::Game_Note_Fuse; } break;
		case NoteType::Bomb: { spr = SprID::Game_Note_Bomb; } break;
		}

		// TODO: Right part stretch animation
		if (noteType == NoteType::Balloon) { /* ... */ }

		if (IsHandNote(noteType)) {
			static constexpr f32 armMovePeriod = 0.25;
			static constexpr f32 armMoveMax = 20;
			static constexpr f32 armMoveHit = 5;
			f32 phase = fmod(2 * currentTime.Seconds / armMovePeriod + 1.5, 2); // expected 0 to 2, start at 1.5
			if (phase < 0)
				phase += 2;
			f32 amplitude = 2 * abs(phase - 1) - 1; // expected 1 to -1 to 1, start at increasing 0
			auto drawArm = [&](vec2 flip)
			{
				f32 armMove = hitAnimation.HasBeenHit ? flip.y * armMoveHit
					: flip.x * armMoveMax * amplitude;
				gfx.DrawSprite(drawList, SprID::Game_Note_ArmDown, SprTransform::FromCenter(
					camera.WorldToScreenSpace({ center.x, center.y + armMove }),
					flip * camera.WorldToScreenScale(1.0f), hitAnimation.HandRotate));
			};
			if (iLane != nLanes - 1 || nLanes == 1) { // always show arms when not in comparison mode
				drawArm({ 1, 1 });
				drawArm({ -1, 1 });
			}
			if (iLane != 0) {
				drawArm({ 1, -1 });
				drawArm({ -1, -1 });
			}
			center.y += hitAnimation.HandSquashYOffset;
		}

		const auto [angle, mirror] = GetNoteFaceRotationMirror(tempo, scrollSpeed, noteType);
		gfx.DrawSprite(drawList, spr, SprTransform::FromCenter(
			camera.WorldToScreenSpace(center),
			vec2(camera.WorldToScreenScale(mirror ? -1.0f : 1.0f), camera.WorldToScreenScale(hitAnimation.HandSquashScale)),
			angle));
	}

	static void DrawGamePreviewNoteDuration(ChartGraphicsResources& gfx, const GameCamera& camera, ImDrawList* drawList, vec2 centerHead, vec2 centerTail, NoteType noteType, u32 colorTint = 0xFFFFFFFF)
	{
		const SprID spr = IsFuseRoll(noteType)
			? SprID::Game_Note_FuseLong
			: (IsBigNote(noteType) 
				? SprID::Game_Note_DrumrollLongBig 
				: SprID::Game_Note_DrumrollLong);
		const SprInfo sprInfo = gfx.GetInfo(spr);

		const f32 midScaleX = (Distance(centerTail, centerHead) / (sprInfo.SourceSize.x)) * 3.0f;

		const SprStretchtOut split = StretchMultiPartSpr(gfx, spr,
			SprTransform::FromCenter(
				camera.WorldToScreenSpace((centerHead + centerTail) / 2.0f),
				vec2(camera.WorldToScreenScale(1.0f)),
				AngleBetween(centerTail, centerHead)),
				colorTint,
			SprStretchtParam { 1.0f, midScaleX, 1.0f }, 3);

		for (size_t i = 0; i < 3; i++)
			gfx.DrawSprite(drawList, split.Quads[i]);
	}

	static void DrawGamePreviewNoteSEText(ChartGraphicsResources& gfx, const GameCamera& camera, ImDrawList* drawList, vec2 centerHead, vec2 centerTail, Tempo tempo, Complex scrollSpeed, NoteSEType seType, b8 hasHead, b8 hasEnd, b8 hasBody)
	{
		const f32 contentToFooterOffsetY = camera.LaneSlice.FooterCenterY() - camera.LaneSlice.ContentCenterY();
		centerHead.y += contentToFooterOffsetY;
		centerTail.y += contentToFooterOffsetY;

		SprID spr = SprID::Count;
		switch (seType)
		{
		case NoteSEType::Do: { spr = SprID::Game_NoteTxt_Do; } break;
		case NoteSEType::Ko: { spr = SprID::Game_NoteTxt_Ko; } break;
		case NoteSEType::Don: { spr = SprID::Game_NoteTxt_Don; } break;
		case NoteSEType::DonBig: { spr = SprID::Game_NoteTxt_DonBig; } break;
		case NoteSEType::DonHand: { spr = SprID::Game_NoteTxt_DonHand; } break;
		case NoteSEType::Ka: { spr = SprID::Game_NoteTxt_Ka; } break;
		case NoteSEType::Katsu: { spr = SprID::Game_NoteTxt_Katsu; } break;
		case NoteSEType::KatsuBig: { spr = SprID::Game_NoteTxt_KatsuBig; } break;
		case NoteSEType::KatsuHand: { spr = SprID::Game_NoteTxt_KatsuHand; } break;
		case NoteSEType::KaDon: { spr = SprID::Game_NoteTxt_KaDon; } break;
		case NoteSEType::Drumroll: { spr = SprID::Game_NoteTxt_Drumroll; } break;
		case NoteSEType::DrumrollBig: { spr = SprID::Game_NoteTxt_DrumrollBig; } break;
		case NoteSEType::Balloon: { spr = SprID::Game_NoteTxt_Balloon; } break;
		case NoteSEType::BalloonSpecial: { spr = SprID::Game_NoteTxt_BalloonSpecial; } break;
		case NoteSEType::Bomb: { spr = SprID::Game_NoteTxt_Bomb; } break;
		case NoteSEType::Adlib: { spr = SprID::Game_NoteTxt_Adlib; } break;
		case NoteSEType::Fuse: { spr = SprID::Game_NoteTxt_Fuse; } break;
		default: return;
		}

		static_assert(std::extent_v<decltype(UserSettingsData::DeveloperData::SENoteOffsetX)> == EnumCount<NoteSEType>);
		const f32 horizontalOffset = *Settings.Developer.SENoteOffsetX[EnumToIndex(seType)];
		centerHead.x += horizontalOffset;
		centerTail.x += horizontalOffset;

		if (seType == NoteSEType::Drumroll || seType == NoteSEType::DrumrollBig || seType == NoteSEType::Fuse)
		{
			const SprInfo sprInfo = gfx.GetInfo(spr);

			const f32 midAlignmentOffset = (seType != NoteSEType::Drumroll) ? 136.0f : 68.0f;
			f32 distance = Distance(centerTail, centerHead);
			const vec2 mid = isfinite(distance) ? (centerHead + centerTail) / 2.0f : hasHead ? centerHead : centerTail;
			if (!isfinite(distance))
				distance = 0;
			const f32 midScaleX = (ClampBot(distance - midAlignmentOffset, 0.0f) / (sprInfo.SourceSize.x)) * 3.0f;

			// split to 3 parts
			const SprStretchtOut split = StretchMultiPartSpr(gfx, spr,
				SprTransform::FromCenter(
					camera.WorldToScreenSpace(mid),
					vec2(camera.WorldToScreenScale(1.0f)),
					(distance < 1 || !hasBody) ? Angle::FromRadians(arg((tempo.BPM >= 0) ? scrollSpeed.cpx : -scrollSpeed.cpx))
					: AngleBetween(centerTail, centerHead)), // TODO: only rotate the mid part?
				0xFFFFFFFF,
				SprStretchtParam { 1.0f, midScaleX, 1.0f }, 3);

			for (size_t i = (hasHead ? 0 : hasBody ? 1 : 2), n = (hasEnd ? 3 : hasBody ? 2 : 1); i < n; i++)
				gfx.DrawSprite(drawList, split.Quads[i]);
		}
		else if (hasHead)
		{
			gfx.DrawSprite(drawList, spr, SprTransform::FromCenter(camera.WorldToScreenSpace(centerHead), vec2(camera.WorldToScreenScale(1.0f))));
		}
	}

	static void DrawGamePreviewNumericText(ChartGraphicsResources& gfx, const GameCamera& camera, ImDrawList* drawList, SprTransform baseTransform, std::string_view text, u32 color = 0xFFFFFFFF)
	{
		// TODO: Make more generic by taking in an array of glyph rects as lookup table (?)
		static constexpr std::string_view sprFontNumericalCharSet = "0123456789+-./%";
		static constexpr f32 advanceX = 26.0f;
		const SprInfo sprInfo = gfx.GetInfo(SprID::Game_Font_Numerical);
		const vec2 perCharSprSize = vec2(sprInfo.SourceSize.x, sprInfo.SourceSize.y / static_cast<f32>(sprFontNumericalCharSet.size()));

		vec2 pivotOffset = vec2(0.0f);
		if (baseTransform.Pivot.x != 0.0f || baseTransform.Pivot.y != 0.0f)
		{
			vec2 totalSize = vec2(0.0f, sprInfo.SourceSize.y);
			for (const char c : text)
			{
				i32 charIndex = -1;
				for (i32 i = 0; i < static_cast<i32>(sprFontNumericalCharSet.size()); i++)
					if (c == sprFontNumericalCharSet[i])
						charIndex = i;

				if (charIndex < 0)
					continue;

				totalSize.x += advanceX;
			}

			pivotOffset = (totalSize * baseTransform.Pivot);
		}

		// offset the output to pivot the advance box within the character sprite box
		vec2 writeHead = vec2((advanceX - sprInfo.SourceSize.x) * (sprInfo.SourceSize.x / advanceX) * baseTransform.Pivot.x, 0);
		for (const char c : text)
		{
			i32 charIndex = -1;
			for (i32 i = 0; i < static_cast<i32>(sprFontNumericalCharSet.size()); i++)
				if (c == sprFontNumericalCharSet[i])
					charIndex = i;

			if (charIndex < 0)
				continue;

			SprTransform charTransform = SprTransform::FromTL(camera.WorldToScreenSpace(baseTransform.Position), vec2(camera.WorldToScreenScale(1.0f)), baseTransform.Rotation);
			SprUV charUV = SprUV::FromRect(
				vec2(0.0f, static_cast<f32>(charIndex + 0) * perCharSprSize.y / sprInfo.SourceSize.y),
				vec2(1.0f, static_cast<f32>(charIndex + 1) * perCharSprSize.y / sprInfo.SourceSize.y));

			// calculate character box's pivot point from advance box's pivot point
			charTransform.Pivot.x += ((pivotOffset.x - writeHead.x) * (advanceX / sprInfo.SourceSize.x) / sprInfo.SourceSize.x);
			charTransform.Pivot.y += (pivotOffset.y / sprInfo.SourceSize.y);

			charTransform.Scale *= baseTransform.Scale;
			charTransform.Scale.y /= static_cast<f32>(sprFontNumericalCharSet.size());

			gfx.DrawSprite(drawList, SprID::Game_Font_Numerical, charTransform, color, charUV);

			writeHead.x += advanceX;
		}
	}

	using ForEachBarLaneData = ChartGamePreview::BarLaneData;
	static constexpr size_t JiroBarLineLimit = 8;
	static constexpr Time JiroBarLineLifetime = Time::FromSec(1.0);

	template <typename Func>
	static void ForEachBarOnNoteLane(ChartContext& context, const ChartCourse& course, BranchType branch, Beat maxBeatDuration, std::function<Complex(Complex)> scrollTypeToView, Func perBarFunc)
	{
		const SortedScrollChangesList& scrollChanges = course.GetScrollChanges(branch);
		BeatSortedForwardIterator<TempoChange> tempoChangeIt {};
		BeatSortedForwardIterator<ScrollChange> scrollChangeIt {};
		BeatSortedForwardIterator<BarLineChange> barLineChangeIt {};
		BeatSortedForwardIterator<ScrollType> scrollTypeIt {};
		BeatSortedForwardIterator<JPOSScrollChange> JPOSscrollChangeIt {};

		for (const auto& it : context.GetBeatBarIndex(course, maxBeatDuration).Bars)
		{
			if (it.Beat > maxBeatDuration) break;

			const b8 isVisible = VisibleOrDefault(barLineChangeIt.Next(course.BarLineChanges.Sorted, it.Beat));

			const Time time = course.BeatToPlaybackTime(it.Beat, branch);
			perBarFunc(ForEachBarLaneData { it.Beat, time,
				TempoOrDefault(tempoChangeIt.Next(course.TempoMap.Tempo.Sorted, it.Beat)),
				scrollTypeToView(ScrollOrDefault(scrollChangeIt.Next(scrollChanges.Sorted, it.Beat))),
				ScrollTypeOrDefault(scrollTypeIt.Next(course.ScrollTypes.Sorted, it.Beat)),
				it.BarIndex, isVisible });

		}
	}

	ChartGamePreview::BarLaneCache& ChartGamePreview::GetCachedBarLaneData(ChartContext& context, ChartCourse& course, BranchType branch, Beat endBeat)
	{
		auto& cache = BarLaneCaches[{ &course, branch }];
		const auto key = context.GetCourseCacheKey(course, branch);
		const i32 scrollView = *Settings.General.ScrollSpeedViewType;
		if (cache.Key == key && cache.ScrollView == scrollView && cache.EndBeat == endBeat) return cache;
		cache.Bars.clear(); cache.TimeBars.clear(); cache.HBBars.clear(); cache.UnboundedBars.clear();
		cache.JiroExpirationIndex.clear();
		cache.MinimumTimeSpeed = cache.MinimumHBSpeed = F64Max;
		const auto& timing = course.GetPlaybackTiming(branch);
		ForEachBarOnNoteLane(context, course, branch, endBeat, GetScrollSpeedToView(static_cast<EScrollSpeedViewType>(scrollView)),
			[&](const ForEachBarLaneData& bar)
			{
				const size_t index = cache.Bars.size(); cache.Bars.push_back(bar);
				if (bar.IsVisible && std::isfinite(bar.Time.Seconds))
				{
					const Time expiration = bar.Time + JiroBarLineLifetime;
					// Prefix maxima allow binary search even when negative DELAY reverses arrival times.
					cache.JiroExpirationIndex.push_back({ cache.JiroExpirationIndex.empty() ? expiration
						: std::max(cache.JiroExpirationIndex.back().first, expiration), index });
				}
				const b8 usesHB = bar.ScrollType == ScrollMethod::HBSCROLL || bar.ScrollType == ScrollMethod::BMSCROLL;
				const f64 speed = usesHB
					? abs(bar.ScrollType == ScrollMethod::BMSCROLL ? 1.0 : bar.ScrollSpeed.GetRealPart()) / Beat::TicksPerBeat
					: abs((bar.Tempo.BPM * bar.ScrollSpeed.GetRealPart()) / 60.0f);
				const f64 position = usesHB ? timing.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(bar.Beat, bar.Time) : bar.Time.Seconds;
				if (speed <= 0.0 || !std::isfinite(speed) || !std::isfinite(position)) cache.UnboundedBars.push_back(index);
				else if (usesHB) { cache.HBBars.push_back({ position, index }); cache.MinimumHBSpeed = Min(cache.MinimumHBSpeed, speed); }
				else { cache.TimeBars.push_back({ position, index }); cache.MinimumTimeSpeed = Min(cache.MinimumTimeSpeed, speed); }
			});
		std::sort(cache.TimeBars.begin(), cache.TimeBars.end()); std::sort(cache.HBBars.begin(), cache.HBBars.end());
		cache.Key = key; cache.ScrollView = scrollView; cache.EndBeat = endBeat;
		return cache;
	}

	static const std::vector<size_t>& FindVisiblePreviewBars(ChartGamePreview::BarLaneCache& cache, const GameCamera& camera,
		Time cursorTime, f64 cursorHB, f32 originX, f64 pxWorldPer4Beats, b8 dynamicScrolls)
	{
		auto& visible = cache.VisibleBars; visible.clear();
		if (dynamicScrolls || !std::isfinite(originX) || !std::isfinite(pxWorldPer4Beats) || pxWorldPer4Beats == 0.0)
		{
			for (size_t index = 0; index < cache.Bars.size(); ++index) visible.push_back(index);
			return visible;
		}
		visible.insert(visible.end(), cache.UnboundedBars.begin(), cache.UnboundedBars.end());
		const f64 distance = Max(abs(-280.0 - originX), abs(camera.LaneWidth() + 280.0 - originX)) + 1.0;
		const auto append = [&](const auto& entries, f64 cursor, f64 minimumSpeed)
		{
			if (entries.empty()) return;
			const f64 range = distance / (minimumSpeed * abs(pxWorldPer4Beats / 4)) * 1.00001 + 1.0;
			const auto first = std::lower_bound(entries.begin(), entries.end(), cursor - range, [](const auto& entry, f64 value) { return entry.first < value; });
			const auto last = std::upper_bound(first, entries.end(), cursor + range, [](f64 value, const auto& entry) { return value < entry.first; });
			for (auto it = first; it != last; ++it) visible.push_back(it->second);
		};
		append(cache.TimeBars, cursorTime.Seconds, cache.MinimumTimeSpeed);
		append(cache.HBBars, cursorHB, cache.MinimumHBSpeed);
		std::sort(visible.begin(), visible.end());
		return visible;
	}

	static const std::vector<size_t>& FindJiroPreviewBars(ChartGamePreview::BarLaneCache& cache, Time cursorTime)
	{
		auto& visible = cache.VisibleBars; visible.clear();
		const auto first = std::upper_bound(cache.JiroExpirationIndex.begin(), cache.JiroExpirationIndex.end(), cursorTime,
			[](Time time, const auto& entry) { return time < entry.first; });
		for (auto it = first; it != cache.JiroExpirationIndex.end() && visible.size() < JiroBarLineLimit; ++it)
			if (cursorTime < cache.Bars[it->second].Time + JiroBarLineLifetime)
				visible.push_back(it->second);
		return visible;
	}

	using ForEachNoteLaneData = ChartGamePreview::NoteLaneData;

	template <typename Func>
	static void ForEachNoteOnNoteLane(ChartCourse& course, BranchType branch, std::function<Complex(Complex)> scrollSpeedToView, Func perNoteFunc)
	{
		const SortedScrollChangesList& scrollChanges = course.GetScrollChanges(branch);
		BeatSortedForwardIterator<TempoChange> tempoChangeIt {};
		BeatSortedForwardIterator<ScrollChange> scrollChangeIt {};
		BeatSortedForwardIterator<ScrollType> scrollTypeIt {};
		BeatSortedForwardIterator<JPOSScrollChange> JPOSscrollChangeIt {};
		BeatSortedForwardIterator<SuddenChange> SuddenChangeIt{};

		for (Note& note : course.GetNotes(branch))
		{
			const Beat beat = note.BeatTime;
			const Time head = (course.BeatToPlaybackTime(beat, branch) + note.TimeOffset);
			const Beat beatTail = (note.BeatDuration > Beat::Zero()) ? (beat + note.BeatDuration) : beat;
			const b8 hasDuration = (beatTail != beat);
			const Time tail = hasDuration ? (course.BeatToPlaybackTime(beatTail, branch) + note.TimeOffset) : head;
			const Complex scrollSpeed = ScrollOrDefault(scrollChangeIt.Next(scrollChanges.Sorted, beat));
			const Tempo tempo = TempoOrDefault(tempoChangeIt.Next(course.TempoMap.Tempo.Sorted, beat));
			const ScrollMethod scrollType = ScrollTypeOrDefault(scrollTypeIt.Next(course.ScrollTypes.Sorted, beat));
			const TJA::SuddenParams sudden = SuddenOrDefault(SuddenChangeIt.Next(course.SuddenChanges.Sorted, beat));
			const Complex scrollSpeedTail = hasDuration ? ScrollOrDefault(scrollChangeIt.Next(scrollChanges.Sorted, beatTail)) : scrollSpeed;
			perNoteFunc(ForEachNoteLaneData {
				{
					beat, head,
					tempo,
					scrollSpeed, scrollSpeedToView(scrollSpeed),
					scrollType,
					sudden,
				},
				&note,
				{
					beatTail, tail,
					hasDuration ? TempoOrDefault(tempoChangeIt.Next(course.TempoMap.Tempo.Sorted, beatTail)) : tempo,
					scrollSpeedTail, scrollSpeedToView(scrollSpeedTail),
					hasDuration ? ScrollTypeOrDefault(scrollTypeIt.Next(course.ScrollTypes.Sorted, beatTail)) : scrollType,
					hasDuration ? SuddenOrDefault(SuddenChangeIt.Next(course.SuddenChanges.Sorted, beatTail)) : sudden,
				},
			});
		}
	}

	static ChartGamePreview::NotePositionCache CreateNotePositionCache(const ChartGamePreview::NoteAttr& note)
	{
		ChartGamePreview::NotePositionCache cache;
		cache.SuddenMoveTime = note.Time - Time::FromMS(std::trunc(note.Sudden.MovementOffset.ToMS())); // TJAP3 behavior
		cache.SuddenShowTime = note.Time - note.Sudden.AppearanceOffset;
		return cache;
	}

	struct GameNotePosition { vec2 Original, Moved; };
	struct VideoPreviewRollHitCounts { i32 Total = 0, Current = 0; };

	static VideoPreviewRollHitCounts GetVideoPreviewRollHitCounts(const ChartCourse& course, const Note& note, f32 rollsPerSecond, Time cursor)
	{
		if (!std::isfinite(rollsPerSecond) || rollsPerSecond <= 0.0f) return {};
		const Time head = course.BeatToPlaybackTime(note.BeatTime) + note.TimeOffset;
		const Time tail = course.BeatToPlaybackTime(note.GetEnd()) + note.TimeOffset;
		if (!(head < tail)) return {};
		const auto getHitTime = [&](i32 index) { return head + Time::FromSec(static_cast<f64>(index) / rollsPerSecond); };
		const i32 maximum = IsBalloonNote(note.Type) ? Max(note.BalloonPopCount, 0) : I32Max - 1;
		i32 limit = static_cast<i32>(std::clamp(std::ceil((tail - head).Seconds * rollsPerSecond) + 2.0, 0.0, static_cast<f64>(maximum)));
		while (limit > 0 && limit < maximum && getHitTime(limit - 1) < tail)
			limit = static_cast<i32>(Min(static_cast<i64>(maximum), static_cast<i64>(limit) * 2));
		const i32 total = FindRollHitIndex(limit, tail, getHitTime);
		return { total, FindRollHitIndex(total, cursor, getHitTime, true) };
	}

	static std::pair<f32, f32> GetGamePreviewVisibleLaneXRange(const GameCamera& camera)
	{
		const vec2 worldMin = camera.ScreenToWorldSpace(camera.ScreenSpaceViewportRect.TL);
		const vec2 worldMax = camera.ScreenToWorldSpace(camera.ScreenSpaceViewportRect.BR);
		const f32 laneMin = camera.WorldToLaneSpace(worldMin.x, worldMin.y).x;
		const f32 laneMax = camera.WorldToLaneSpace(worldMax.x, worldMax.y).x;
		return { Min(laneMin, laneMax) - 280.0f, Max(laneMin, laneMax) + 280.0f };
	}

	static GameNotePosition GetCachedGameNotePosition(const GameCamera& camera, const ChartGamePreview::NoteAttr& note,
		const ChartGamePreview::NotePositionCache& cache, vec2 originLane, Time cursorTime, f64 cursorHBScrollBeat,
		f64 pxWorldPer4Beats, const TempoMapAccelerationStructure& timing, const SortedJPOSScrollChangesList& jposChanges)
	{
		const b8 usesHBScrollBeat = note.ScrollType == ScrollMethod::HBSCROLL || note.ScrollType == ScrollMethod::BMSCROLL;
		if (usesHBScrollBeat)
		{
			// A branch transition can render cached notes using another branch's timing map.
			if (cache.Timing != &timing)
			{
				cache.Timing = &timing;
				cache.HasNoteHBScrollBeatTick = cache.HasSuddenMoveHBScrollBeatTick = false;
			}
			if (!cache.HasNoteHBScrollBeatTick)
			{
				cache.NoteHBScrollBeatTick = timing.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(note.Beat, note.Time);
				cache.HasNoteHBScrollBeatTick = true;
			}
		}
		const auto getCoordinates = [&](Time positionTime, f64 positionHBScrollBeat)
		{
			if (!usesHBScrollBeat)
				return camera.GetNoteCoordinatesLane(originLane, positionTime, positionHBScrollBeat, note.Time, note.Beat,
					note.Tempo, note.ScrollSpeedView, note.ScrollType, pxWorldPer4Beats, timing, jposChanges);
			const Complex scroll = note.ScrollType == ScrollMethod::BMSCROLL ? Complex(1.0f, 0.0f) : note.ScrollSpeedView;
			const f64 beatDistance = (cache.NoteHBScrollBeatTick - positionHBScrollBeat) / Beat::TicksPerBeat;
			return vec2(originLane.x + static_cast<f32>(scroll.GetRealPart() * beatDistance * (pxWorldPer4Beats / 4)),
				originLane.y + static_cast<f32>(scroll.GetImaginaryPart() * beatDistance * (pxWorldPer4Beats / 4)));
		};
		GameNotePosition position;
		position.Original = position.Moved = getCoordinates(cursorTime, cursorHBScrollBeat);
		if (!(cursorTime >= cache.SuddenMoveTime))
		{
			if (usesHBScrollBeat && !cache.HasSuddenMoveHBScrollBeatTick)
			{
				const Beat moveBeat = timing.ConvertTimeToBeatWithHint(cache.SuddenMoveTime, note.Beat, true);
				cache.SuddenMoveHBScrollBeatTick = camera.GetCursorHBScrollBeatTick(moveBeat, cache.SuddenMoveTime, timing);
				cache.HasSuddenMoveHBScrollBeatTick = true;
			}
			position.Moved = getCoordinates(cache.SuddenMoveTime, usesHBScrollBeat ? cache.SuddenMoveHBScrollBeatTick : cursorHBScrollBeat);
			position.Moved.y = position.Original.y; // TJAP3 behavior
		}
		return position;
	}

	const std::vector<ChartGamePreview::NoteLaneData>& ChartGamePreview::GetCachedNoteLaneData(ChartContext& context, ChartCourse& course, BranchType branch)
	{
		auto& cache = NoteLaneCaches[{ &course, branch }];
		const auto key = context.GetCourseCacheKey(course, branch);
		const i32 scrollView = *Settings.General.ScrollSpeedViewType;
		if (cache.Key != key || cache.ScrollView != scrollView)
		{
			cache.Notes.clear();
			ForEachNoteOnNoteLane(course, branch, GetScrollSpeedToView(static_cast<EScrollSpeedViewType>(scrollView)),
				[&](const ForEachNoteLaneData& note)
				{
					auto& cached = cache.Notes.emplace_back(note);
					cached.HeadPosition = CreateNotePositionCache(cached);
					cached.TailPosition = CreateNotePositionCache(cached.Tail);
				});
			cache.Key = key;
			cache.ScrollView = scrollView;
		}
		return cache.Notes;
	}

	b8 RunGamePreviewNoteSelfTest(std::string& error)
	{
		error.clear();
		ChartContext context;
		TJA::ErrorList errors;
		const auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(
			"TITLE:Preview position test\nBPM:120\nCOURSE:Oni\n#START\n#SUDDEN 3.5 1.2349\n#SCROLL 1.5+0.4i\n1000,\n"
			"#DELAY 0.75\n#BPMCHANGE -90\n5000,\n#DELAY -1.25\n#SUDDEN 0.25 -0.0009\n#SCROLL -1.25+0.5i\n8000,\n"
			"#BPMCHANGE 180\n#SCROLL 0\n1000,\n#SUDDEN 0 0\n#SCROLL 1\n1000,\n#END\n")), errors);
		if (!errors.Errors.empty() || !CreateChartProjectFromTJA(parsed, context.Chart)) { error = "Cannot import preview position fixture"; return false; }
		auto& course = *context.Chart.Courses[0];
		context.SetSelectedChart(&course, BranchType::Normal);
		auto expertDelay = course.DelayChanges_Normal[0]; expertDelay.Duration += Time::FromSec(0.4);
		course.DelayChanges_Expert.InsertOrUpdate(expertDelay);
		ChartGamePreview preview;
		GameCamera camera;
		camera.LaneRect = { vec2(0.0f), vec2(1280.0f, 300.0f) };
		camera.ScreenSpaceViewportRect = { vec2(50.0f, 80.0f), vec2(1970.0f, 1160.0f) };
		camera.LaneContentOffset = vec2(0.0f, -4.0f);
		for (f32 scale : { 0.5f, 1.0f, 2.0f })
		{
			camera.WorldToScreenScaleFactor = scale;
			const auto [minimum, maximum] = GetGamePreviewVisibleLaneXRange(camera);
			for (f32 laneX : { -2000.0f, -600.0f, -400.0f, -280.0f, 0.0f, 800.0f, 2000.0f })
			{
				const f32 screenX = camera.LaneToScreenSpace(vec2(laneX, 0.0f)).x;
				const b8 visible = screenX >= camera.ScreenSpaceViewportRect.TL.x - 280.0f * scale
					&& screenX <= camera.ScreenSpaceViewportRect.BR.x + 280.0f * scale;
				if (visible != (laneX >= minimum && laneX <= maximum))
				{ error = "Preview culling excludes notes inside the viewport margin"; return false; }
			}
		}
		const vec2 origin(123.0f, -17.0f);
		const auto same = [](f32 first, f32 second)
		{
			return first == second || (std::isnan(first) && std::isnan(second))
				|| (std::isfinite(first) && std::isfinite(second) && Absolute(first - second) < 0.001f);
		};
		for (const auto& cached : preview.GetCachedNoteLaneData(context, course, BranchType::Normal))
			for (const auto* source : { static_cast<const ChartGamePreview::NoteAttr*>(&cached), &cached.Tail })
				for (ScrollMethod method : { ScrollMethod::NMSCROLL, ScrollMethod::HBSCROLL, ScrollMethod::BMSCROLL })
					for (Time moveOffset : { source->Sudden.MovementOffset, SuddenOrDefault(nullptr).MovementOffset })
					{
						auto note = *source; note.ScrollType = method; note.Sudden.MovementOffset = moveOffset;
						const auto cache = CreateNotePositionCache(note);
						for (BranchType timingBranch : { BranchType::Normal, BranchType::Expert, BranchType::Normal })
							for (f64 seconds : { -3.0, -0.5, 0.0, 1.0, 2.0, 3.0, 4.5, 7.0, 11.0, 20.0 })
							{
								const auto& timing = course.GetPlaybackTiming(timingBranch);
								const Time cursor = Time::FromSec(seconds);
								const f64 cursorHB = camera.GetCursorHBScrollBeatTick(timing.ConvertTimeToBeatWithHint(cursor, note.Beat, true), cursor, timing);
								const auto position = GetCachedGameNotePosition(camera, note, cache, origin, cursor, cursorHB, 800.0, timing, course.JPOSScrollChanges);
								const vec2 original = camera.GetNoteCoordinatesLane(origin, cursor, cursorHB, note.Time, note.Beat,
									note.Tempo, note.ScrollSpeedView, method, 800.0, timing, course.JPOSScrollChanges);
								Time moveTime = cursor; f64 moveHB = cursorHB;
								if (!(cursor >= cache.SuddenMoveTime))
								{
									moveTime = cache.SuddenMoveTime;
									moveHB = camera.GetCursorHBScrollBeatTick(timing.ConvertTimeToBeatWithHint(moveTime, note.Beat, true), moveTime, timing);
								}
								vec2 moved = camera.GetNoteCoordinatesLane(origin, moveTime, moveHB, note.Time, note.Beat,
									note.Tempo, note.ScrollSpeedView, method, 800.0, timing, course.JPOSScrollChanges);
								moved.y = original.y;
								if (!same(position.Original.x, original.x) || !same(position.Original.y, original.y)
									|| !same(position.Moved.x, moved.x) || !same(position.Moved.y, moved.y))
								{ error = "Cached preview position differs from the original calculation"; return false; }
								if (method == ScrollMethod::NMSCROLL && (cache.HasNoteHBScrollBeatTick || cache.HasSuddenMoveHBScrollBeatTick))
								{ error = "NMSCROLL computed unused HBSCROLL coordinates"; return false; }
							}
						// Cached timing values must be discarded even if another branch was last used to render this note.
						cached.HeadPosition.Timing = &course.GetPlaybackTiming(BranchType::Expert);
						cached.HeadPosition.HasNoteHBScrollBeatTick = cached.HeadPosition.HasSuddenMoveHBScrollBeatTick = true;
					}
		course.DelayChanges_Expert.NotifyChanged();
		const auto& refreshed = preview.GetCachedNoteLaneData(context, course, BranchType::Normal);
		if (refreshed.empty() || refreshed[0].HeadPosition.Timing != nullptr || refreshed[0].HeadPosition.HasNoteHBScrollBeatTick
			|| refreshed[0].HeadPosition.HasSuddenMoveHBScrollBeatTick)
		{ error = "Changing another branch's delay retained stale preview coordinates"; return false; }
		for (ScrollMethod method : { ScrollMethod::NMSCROLL, ScrollMethod::HBSCROLL, ScrollMethod::BMSCROLL })
		{
			ScrollType change {}; change.BeatTime = Beat::Zero(); change.Method = method;
			course.ScrollTypes.InsertOrUpdate(change);
			auto& bars = preview.GetCachedBarLaneData(context, course, BranchType::Normal, Beat::FromBars(512));
			const auto& timing = course.GetPlaybackTiming();
			for (f64 seconds : { -3.0, 0.0, 1.0, 3.0, 7.0, 11.0, 120.0, 600.0 })
			for (f32 originX : { -1000.0f, 0.0f, 3000.0f })
			for (f64 scale : { 16.0, 800.0, 1600.0 })
			{
				const Time cursor = Time::FromSec(seconds);
				const f64 cursorHB = camera.GetCursorHBScrollBeatTick(timing.ConvertTimeToBeatWithHint(cursor, Beat::Zero(), true), cursor, timing);
				const auto& visible = FindVisiblePreviewBars(bars, camera, cursor, cursorHB, originX, scale, false);
				for (size_t index = 0; index < bars.Bars.size(); ++index)
				{
					const auto& bar = bars.Bars[index];
					const vec2 position = camera.GetNoteCoordinatesLane(vec2(originX, 0.0f), cursor, cursorHB, bar.Time, bar.Beat,
						bar.Tempo, bar.ScrollSpeed, bar.ScrollType, scale, timing, course.JPOSScrollChanges);
					if (camera.IsPointVisibleOnLane(position.x) && !std::binary_search(visible.begin(), visible.end(), index))
					{ error = "Indexed bar culling omitted a visible bar"; return false; }
				}
			}
			if (FindVisiblePreviewBars(bars, camera, Time::Zero(), 0.0, 0.0f, 800.0, true).size() != bars.Bars.size())
			{ error = "Dynamic scroll changes did not retain all bar candidates"; return false; }
			if (FindVisiblePreviewBars(bars, camera, Time::Zero(), 0.0, 0.0f, 800.0, false).size() >= bars.Bars.size() / 4)
			{ error = "Indexed bar culling did not narrow a long chart"; return false; }
		}
		ChartContext jiroContext;
		const auto jiroParsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(
			"TITLE:Jiro bar lines\nBPM:120\nCOURSE:Oni\n#START\n#MEASURE 1/16\n"
			"#SCROLL 100\n0,\n#SCROLL 0\n0,\n#SCROLL -4\n0,\n#BARLINEOFF\n0,\n"
			"#BARLINEON\n#SCROLL 1\n0,\n0,\n0,\n0,\n0,\n0,\n0,\n0,\n0,\n0,\n#END\n")), errors);
		if (!errors.Errors.empty() || !CreateChartProjectFromTJA(jiroParsed, jiroContext.Chart))
		{ error = "Cannot import Jiro bar line fixture"; return false; }
		auto& jiroCourse = *jiroContext.Chart.Courses[0];
		jiroContext.SetSelectedChart(&jiroCourse, BranchType::Normal);
		const auto checkJiroBars = [&](ChartGamePreview::BarLaneCache& bars, Time cursor, std::initializer_list<size_t> expected)
		{
			const auto& actual = FindJiroPreviewBars(bars, cursor);
			if (actual == std::vector<size_t>(expected)) return true;
			error = "Jiro bar line selection differs at " + std::to_string(cursor.Seconds) + " seconds";
			for (size_t index : actual) error += " " + std::to_string(index);
			return false;
		};
		for (ScrollMethod method : { ScrollMethod::NMSCROLL, ScrollMethod::HBSCROLL, ScrollMethod::BMSCROLL })
		{
			ScrollType change {}; change.BeatTime = Beat::Zero(); change.Method = method;
			jiroCourse.ScrollTypes.InsertOrUpdate(change);
			auto& bars = preview.GetCachedBarLaneData(jiroContext, jiroCourse, BranchType::Normal, Beat::FromBars(1));
			if (!checkJiroBars(bars, Time::FromSec(0.999), { 0, 1, 2, 4, 5, 6, 7, 8 })
				|| !checkJiroBars(bars, Time::FromSec(1.0), { 1, 2, 4, 5, 6, 7, 8, 9 })
				|| !checkJiroBars(bars, Time::FromSec(1.125), { 2, 4, 5, 6, 7, 8, 9, 10 })
				|| !checkJiroBars(bars, Time::FromSec(1.25), { 4, 5, 6, 7, 8, 9, 10, 11 })
				|| !checkJiroBars(bars, Time::FromSec(100.0), {})
				|| !checkJiroBars(bars, Time::FromSec(-1.0), { 0, 1, 2, 4, 5, 6, 7, 8 })) return false;
			const Time cursor = Time::FromSec(0.999);
			const auto& timing = jiroCourse.GetPlaybackTiming();
			const f64 cursorHB = camera.GetCursorHBScrollBeatTick(timing.ConvertTimeToBeatWithHint(cursor, Beat::Zero(), true), cursor, timing);
			const auto& firstBar = bars.Bars[0];
			const vec2 position = camera.GetNoteCoordinatesLane(vec2(0.0f), cursor, cursorHB, firstBar.Time, firstBar.Beat,
				firstBar.Tempo, firstBar.ScrollSpeed, firstBar.ScrollType, 1424.0, timing, jiroCourse.JPOSScrollChanges);
			if (camera.IsPointVisibleOnLane(position.x) || !checkJiroBars(bars, cursor, { 0, 1, 2, 4, 5, 6, 7, 8 }))
			{ error = "An offscreen Jiro bar line released its slot before expiration"; return false; }
		}
		// Reversed arrival times must preserve measure priority and invalidate the cached expiration index.
		for (i32 barIndex : { 2, 3 })
		{
			DelayChange delay {}; delay.BeatTime = Beat::FromBars(1) * barIndex / 16; delay.Duration = Time::FromSec(-0.25);
			jiroCourse.DelayChanges_Normal.InsertOrUpdate(delay);
		}
		auto& reversedBars = preview.GetCachedBarLaneData(jiroContext, jiroCourse, BranchType::Normal, Beat::FromBars(1));
		if (!checkJiroBars(reversedBars, Time::FromSec(1.0), { 1, 5, 6, 7, 8, 9, 10, 11 })
			|| !checkJiroBars(reversedBars, Time::FromSec(1.125), { 6, 7, 8, 9, 10, 11, 12, 13 })) return false;

		for (const Note& note : course.Notes_Normal)
		{
			if (!IsLongNote(note.Type)) continue;
			for (NoteType type : { note.Type, NoteType::Balloon })
			for (f32 speed : { 0.0f, 1.0f, 7.0f, 30.0f, 120.0f })
			for (f64 offset : { 0.0, 1000000000.0 })
			for (f64 seconds : { -0.01, 0.0, 1.0, 2.0, 10.0 })
			{
				Note roll = note; roll.Type = type; roll.BalloonPopCount = 7; roll.TimeOffset = Time::FromSec(offset);
				const Time cursor = course.BeatToPlaybackTime(roll.BeatTime) + roll.TimeOffset + Time::FromSec(seconds);
				VideoPreviewRollHitCounts expected;
				ForEachVideoRollHit(course, roll, speed, [&](Time time, b8) { ++expected.Total; if (time <= cursor) ++expected.Current; });
				const auto actual = GetVideoPreviewRollHitCounts(course, roll, speed, cursor);
				if (actual.Total != expected.Total || actual.Current != expected.Current)
				{ error = "Video roll counts differ from original hit enumeration"; return false; }
			}
		}
		return true;
	}

	void ChartCourse::RecalculateSENotes(BranchType branch)
	{
		enum class SEFormType { Long, Short, Alternate, Final };

		// prev, curr, next, n(ext)2nd
		ForEachNoteLaneData noteDataRingBuffer[4] = {};
		i32 noteDataRingOffset = 0;
		auto getNoteData = [&](i32 idx) -> decltype(auto) { return noteDataRingBuffer[(noteDataRingOffset + idx) & 3]; };

		// distance when curr is on the judgement mark
		// other is NMScroll: visual beat distance = sec_time * visual_beat_per_second_other
		// other is HBScroll: visual beat distance = scroll_other * beat_distance
		auto getVisualBeat = [&](const auto& curr, const auto& other, f32 scrollOther, f32 vbpsOther, Time timeDistance)
		{
			return (other.OriginalNote == nullptr) ? F32Max
				: (other.ScrollType == ScrollMethod::NMSCROLL) ? vbpsOther * timeDistance.Seconds
				: (other.ScrollType == ScrollMethod::HBSCROLL) ? scrollOther * abs(curr.Beat - other.Beat).Ticks / Beat::TicksPerBeat
				: /* (prev.ScrollType == ScrollMethod::BMSCROLL) ? */ 1.0f * abs(curr.Beat - other.Beat).Ticks / Beat::TicksPerBeat;
		};

		auto getNoteDistance = [&]()
		{
			const auto& prev = getNoteData(0);
			const auto& curr = getNoteData(1);
			const auto& next = getNoteData(2);
			const auto& n2nd = getNoteData(3);
			const f32 scrollPrev = abs(prev.ScrollSpeed.cpx);
			const f32 scrollNextCapped = std::min(1.0f, abs(next.ScrollSpeed.cpx));
			// visual beat per second
			const f32 vbpsPrev = scrollPrev * prev.Tempo.BPM / 60;
			const f32 vbpsNextCapped = scrollNextCapped * next.Tempo.BPM / 60;
			// time distance
			const Time tdToPrev = (prev.OriginalNote == nullptr) ? Time::FromSec(F32Max) : (curr.Time - prev.Time);
			const Time tdToNext = (next.OriginalNote == nullptr) ? Time::FromSec(F32Max) : (next.Time - curr.Time);
			const Time tdToN2nd = (n2nd.OriginalNote == nullptr) ? Time::FromSec(F32Max) : (n2nd.Time - next.Time);
			const f32 vbdToPrev = getVisualBeat(curr, prev, scrollPrev, vbpsPrev, tdToPrev);
			const f32 vbdToNextCapped = getVisualBeat(curr, next, scrollNextCapped, vbpsNextCapped, tdToNext);
			return std::tuple{ tdToPrev, vbdToPrev, tdToNext, vbdToNextCapped, tdToN2nd };
		};

		const SortedNotesList& notes = GetNotes(branch);
		std::vector<const Note*> alterChain;
		b8 isAlterChain = true;
		Time timeIntervalAlter = Time::Zero();
		Time timeStartAlter = Time::Zero();

		auto assignSingleNote = [&]()
		{
			auto& curr = getNoteData(1);
			const Note& it = *getNoteData(1).OriginalNote;
			auto [tdToPrev, vbdToPrev, tdToNext, vbdToNextCapped, tdToN2nd] = getNoteDistance();
			const Time timeEpsilon = Time::FromMS(1e-3);
			const b8 denseToSparse = (tdToNext >= tdToPrev + timeEpsilon);
			const b8 sparseToDense = (tdToN2nd <= tdToNext - timeEpsilon);
			const f32 beatsEpsilon = 4 / 192.0;
			const b8 isLongAvoided = (vbdToPrev <= 4 / 16.0 - beatsEpsilon
				|| vbdToNextCapped <= 4 / 12.0 - beatsEpsilon); // avoid text from overlapping or extending under next note
			const b8 isPrePause = (vbdToNextCapped >= 4 / 8.0 + beatsEpsilon);
			auto se = (!isLongAvoided && (denseToSparse || sparseToDense || isPrePause)) ? SEFormType::Long : SEFormType::Short;
			if (isAlterChain) {
				if (it.Type == NoteType::Don && alterChain.empty()) {
					timeIntervalAlter = tdToNext;
					timeStartAlter = curr.Time;
					alterChain.push_back(&it);
				} else if (it.Type == NoteType::Don && abs(tdToPrev - timeIntervalAlter) < timeEpsilon && abs(timeStartAlter - curr.Time) < Time::FromSec(0.5) + timeEpsilon) {
					alterChain.push_back(&it);
				} else {
					isAlterChain = false;
					alterChain.clear();
				}
			}
			if (denseToSparse || sparseToDense) {
				if (denseToSparse && isAlterChain && !isLongAvoided && size(alterChain) % 2 != 0 && abs(timeStartAlter - curr.Time) < Time::FromSec(0.5) + timeEpsilon) {
					for (i32 ia = 0; ia < size(alterChain); ++ia) {
						if (ia % 2 == 1)
							alterChain[ia]->TempSEType = NoteSEType::Ko;
					}
				}
				alterChain.clear();
				isAlterChain = sparseToDense;
			}

			switch (it.Type)
			{
			case NoteType::Don: { it.TempSEType = (se == SEFormType::Long) ? NoteSEType::Don : NoteSEType::Do; } break;
			case NoteType::DonBig: { it.TempSEType = NoteSEType::DonBig; } break;
			case NoteType::DonBigHand: { it.TempSEType = NoteSEType::DonHand; } break;
			case NoteType::Ka: { it.TempSEType = (se == SEFormType::Long) ? NoteSEType::Katsu : NoteSEType::Ka; } break;
			case NoteType::KaBig: { it.TempSEType = NoteSEType::KatsuBig; } break;
			case NoteType::KaBigHand: { it.TempSEType = NoteSEType::KatsuHand; } break;
			case NoteType::KaDon: { it.TempSEType = NoteSEType::KaDon; } break;
			case NoteType::Drumroll: { it.TempSEType = NoteSEType::Drumroll; } break;
			case NoteType::DrumrollBig: { it.TempSEType = NoteSEType::DrumrollBig; } break;
			case NoteType::Balloon: { it.TempSEType = NoteSEType::Balloon; } break;
			case NoteType::BalloonSpecial: { it.TempSEType = NoteSEType::BalloonSpecial; } break;
			case NoteType::Bomb: { it.TempSEType = NoteSEType::Bomb; } break;
			case NoteType::Adlib: { it.TempSEType = NoteSEType::Adlib; } break;
			case NoteType::Fuse: { it.TempSEType = NoteSEType::Fuse; } break;
			default: { it.TempSEType = NoteSEType::Count; } break;
			}
		};

		// fetch 2nd next note, update current note
		i32 lastFilled = 0;
		ForEachNoteOnNoteLane(*this, branch, scrollSpeedToViews[0], [&](const ForEachNoteLaneData& dataIt)
		{
			if (getNoteData(1).OriginalNote != nullptr)
				assignSingleNote();
			noteDataRingOffset = (noteDataRingOffset + 1) & 3;
			getNoteData(3) = dataIt;
			lastFilled = 3;
		});
		for (; lastFilled >= 1; --lastFilled) {
			if (getNoteData(1).OriginalNote != nullptr)
				assignSingleNote();
			noteDataRingOffset = (noteDataRingOffset + 1) & 3;
			getNoteData(3).OriginalNote = nullptr;
		}
	}

	void ChartCourse::RecalculateComboCounts(BranchType branch)
	{
		const SortedNotesList& notes = GetNotes(branch);
		i32 comboCount = 0;
		for (const Note& note : notes)
		{
			if (IsComboNote(note.Type))
			{
				comboCount++;
				note.TempComboCount = comboCount;
			}
			else
			{
				// Long notes and special notes keep the current combo count
				note.TempComboCount = comboCount;
			}
		}
	}

	BranchType ChartGamePreview::GetTestPlayNoteBranch(Beat beat) const
	{
		if (!TestPlayAutoBranchActive || TestPlayCourse == nullptr) return TestPlayBranch;
		for (size_t index = 0; index < TestPlayCourse->Branches.size(); index++)
		{
			const BranchRange& range = TestPlayCourse->Branches[index];
			if (beat >= range.GetStart() && beat < GetBranchRangeEnd(TestPlayCourse->Branches, range))
			{
				for (const auto& decision : TestPlayBranchDecisions)
					if (decision.first == index) return decision.second;
				if (range.GetStart() < TestPlayBranchStartBeat) return TestPlayInitialBranch;
				return TestPlayVisualBranch;
			}
		}
		return BranchType::Normal;
	}

	void ChartGamePreview::RebuildTestPlayScrolls()
	{
		if (TestPlayCourse == nullptr) return;
		VideoBranchRoute route = BuildFixedVideoBranchRoute(*TestPlayCourse, TestPlayInitialBranch);
		for (const auto& decision : TestPlayBranchDecisions)
			if (decision.first < route.Branches.size()) route.Branches[decision.first] = decision.second;
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			TestPlayScrolls[EnumToIndex(branch)].Rebuild(*TestPlayCourse, route, branch, TestPlayNextDecisionIndex);
			TestPlayTransitionScrolls[EnumToIndex(branch)].Rebuild(*TestPlayCourse, route, branch, TestPlayNextDecisionIndex, TestPlayBranchTransitionIndex);
		}
	}

	void ChartGamePreview::UpdateVideoScrolls(const ChartCourse& course, Time time, VideoExportLaneState* lane)
	{
		const VideoBranchRoute* VideoExportRoute = lane ? lane->Route : this->VideoExportRoute;
		auto& VideoScrollDecisionCount = lane ? lane->ScrollDecisionCount : this->VideoScrollDecisionCount;
		auto& VideoExportScrolls = lane ? lane->Scrolls : this->VideoExportScrolls;
		auto& VideoExportTransitionScrolls = lane ? lane->TransitionScrolls : this->VideoExportTransitionScrolls;
		if (VideoExportRoute == nullptr) return;
		const auto& route = *VideoExportRoute;
		size_t decided = route.Branches.size();
		if (route.Animate)
		{
			decided = 0;
			while (decided < route.DecisionTimes.size() && route.DecisionTimes[decided] <= time) ++decided;
		}
		if (VideoScrollDecisionCount == decided) return;
		VideoScrollDecisionCount = decided;
		const size_t transitionBranch = route.GetVisualState(course, time).Index;
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		{
			VideoExportScrolls[EnumToIndex(branch)].Rebuild(course, route, branch, decided);
			VideoExportTransitionScrolls[EnumToIndex(branch)].Rebuild(course, route, branch, decided, transitionBranch);
		}
	}

	b8 ChartGamePreview::IsTestPlayNoteActive(const Note* note, BranchType branch) const
	{
		if (!TestPlayAutoBranchActive) return true;
		for (size_t index = TestPlayNextDecisionIndex; index < TestPlayCourse->Branches.size(); index++)
		{
			const BranchRange& range = TestPlayCourse->Branches[index];
			if (note->BeatTime >= range.GetStart() && note->BeatTime < GetBranchRangeEnd(TestPlayCourse->Branches, range)) return false;
		}
		return GetTestPlayNoteBranch(note->BeatTime) == branch;
	}

	void ChartGamePreview::UpdateTestPlayBranches(ChartContext& context, Beat beat)
	{
		if (!TestPlayAutoBranchActive || TestPlayCourse == nullptr) return;
		const size_t previousDecisionCount = TestPlayNextDecisionIndex;
		const auto& branches = TestPlayCourse->Branches;
		const auto& sections = TestPlayCourse->BranchSections;
		const auto& holds = TestPlayOrderedLevelHolds;
		while (true)
		{
			const Beat nextBranch = TestPlayNextBranchIndex < branches.size() ? branches[TestPlayNextBranchIndex].GetStart() : Beat::FromTicks(I32Max);
			const Beat nextDecision = TestPlayNextDecisionIndex < branches.size() ? TestPlayBranchCutoffs[TestPlayNextDecisionIndex] : Beat::FromTicks(I32Max);
			const Beat nextSection = TestPlayNextSectionIndex < sections.size() ? sections[TestPlayNextSectionIndex] : Beat::FromTicks(I32Max);
			const Beat nextHold = TestPlayNextLevelHoldIndex < holds.size() ? holds[TestPlayNextLevelHoldIndex].BeatTime : Beat::FromTicks(I32Max);
			const Beat next = std::min({ nextDecision, nextBranch, nextSection, nextHold });
			if (next > beat || next == Beat::FromTicks(I32Max)) break;
			if (nextDecision == next && (nextBranch != next || TestPlayNextDecisionIndex <= TestPlayNextBranchIndex))
			{
				const size_t index = TestPlayNextDecisionIndex++;
				const BranchRange& range = branches[index];
				BranchType decidedBranch = TestPlayBranch;
				if (!TestPlayLevelHeld)
				{
					const Beat cutoff = TestPlayBranchCutoffs[index];
					const Time cutoffTime = TestPlayCourse->BeatToPlaybackTime(cutoff, TestPlayBranch);
					i32 good = 0, ok = 0, total = 0, roll = 0;
					for (const TestPlayNoteState& note : TestPlayNotes)
						if (note.Source->BeatTime >= TestPlaySectionBeat && note.Source->BeatTime < cutoff && IsTestPlayNoteActive(note.Source, note.Branch))
						{
							++total;
							good += note.Judgement == 1;
							ok += note.Judgement == 2;
						}
					for (const TestPlayLongNoteState& note : TestPlayLongNotes)
						if (IsTestPlayNoteActive(note.Source, note.Branch))
							for (Time hit : note.HitTimes)
								if (hit >= TestPlayCourse->BeatToPlaybackTime(TestPlaySectionBeat, TestPlayBranch) && hit < cutoffTime) ++roll;
					const f64 value = range.Condition == TJA::BranchCondition::Roll ? static_cast<f64>(roll) : GetTestPlayBranchAccuracy(good, ok, total);
					decidedBranch = static_cast<BranchType>(GetTestPlayBranchIndex(value, range.RequirementExpert, range.RequirementMaster));
				}
				TestPlayBranchDecisions.emplace_back(index, decidedBranch);
				if (TestPlayVisualBranch != decidedBranch)
				{
					TestPlayBranchTransitionFrom = TestPlayVisualBranch;
					TestPlayBranchTransitionIndex = index;
					const Time decisionTime = context.GetCursorTime();
					TestPlayBranchTransitionDuration = std::min(Time::FromSec(0.1 * TestPlayPlaybackSpeed),
						TestPlayCourse->BeatToPlaybackTime(range.GetStart(), TestPlayBranch) - decisionTime);
					if (TestPlayBranchTransitionDuration > Time::Zero()) TestPlayBranchDecisionTime = decisionTime;
					else TestPlayBranchDecisionTime.reset();
				}
				TestPlayVisualBranch = decidedBranch;
			}
			else if (nextBranch == next)
			{
				const size_t index = TestPlayNextBranchIndex++;
				const auto decision = std::find_if(TestPlayBranchDecisions.begin(), TestPlayBranchDecisions.end(),
					[index](const auto& item) { return item.first == index; });
				if (decision != TestPlayBranchDecisions.end()) TestPlayBranch = decision->second;
				if (context.ChartSelectedBranch != TestPlayBranch)
				{
					context.ChartSelectedBranch = TestPlayBranch;
					context.ResetChartsCompared();
				}
			}
			else if (nextSection == next)
			{
				TestPlaySectionBeat = sections[TestPlayNextSectionIndex++];
			}
			else
			{
				const BranchLevelHold& hold = holds[TestPlayNextLevelHoldIndex++];
				if (hold.Branch == TestPlayBranch && std::any_of(branches.begin(), branches.end(), [&](const BranchRange& range)
					{ return hold.BeatTime >= range.GetStart() && hold.BeatTime < GetBranchRangeEnd(branches, range); }))
					TestPlayLevelHeld = true;
			}
		}
		if (TestPlayNextDecisionIndex != previousDecisionCount) RebuildTestPlayScrolls();
	}

	void ChartGamePreview::StartTestPlay(ChartContext& context, TestPlayStartMode mode)
	{
		if (IsTestPlaying || context.CompareMode || context.ChartSelectedCourse == nullptr || (mode == TestPlayStartMode::Marker && !context.Marker.IsActive)) return;
		if (std::any_of(context.ChartSelectedCourse->Branches.begin(), context.ChartSelectedCourse->Branches.end(),
			[](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; })) return;
		TestPlayCourse = context.ChartSelectedCourse;
		TestPlayBranchDecisions.clear();
		TestPlayBranch = context.ChartSelectedBranch;
		TestPlayAutoBranchActive = !TestPlayCourse->Branches.empty();
		TestPlayBranchCutoffs.clear();
		for (const BranchRange& range : TestPlayCourse->Branches)
			TestPlayBranchCutoffs.push_back(GetBranchJudgeCutoff(*TestPlayCourse, range.GetStart()));
		TestPlayOrderedLevelHolds = TestPlayCourse->BranchLevelHolds;
		std::sort(TestPlayOrderedLevelHolds.begin(), TestPlayOrderedLevelHolds.end(), [](const BranchLevelHold& a, const BranchLevelHold& b)
		{ return a.BeatTime != b.BeatTime ? a.BeatTime < b.BeatTime : a.Branch < b.Branch; });
		if (mode == TestPlayStartMode::Beginning)
		{
			context.RangeSelection = {};
			TestPlayStartTime = TestPlayCourse->GetPlaybackTimeBounds().first;
		}
		else if (mode == TestPlayStartMode::Marker)
		{
			const Beat markerBeat = context.Marker.BeatTime;
			context.RangeSelection = {};
			TestPlayStartTime = context.BeatToPlaybackTime(markerBeat);
		}
		else
			TestPlayStartTime = context.RangeSelection.IsActiveAndHasEnd() && context.RangeSelection.GetDuration() > Beat::Zero()
				? context.BeatToPlaybackTime(context.RangeSelection.GetMin()) : context.GetCursorTime();
		TestPlayEndTime = TestPlayCourse->GetPlaybackTimeBounds(context.GetUsedDuration(*TestPlayCourse)).second;
		TestPlayAttemptStartTime = TestPlayStartTime;
		context.TestPlaySeekTime.reset();
		context.TestPlaySmoothCursor = false;
		context.TestPlayFollowCursor = false;
		context.TestPlayJudgements.clear();
		TestPlayRecordFilter = 0;
		TestPlayRecordScope = 0;
		TestPlayNotes.clear();
		TestPlayNoteIndices.clear();
		TestPlayLongNotes.clear();
		TestPlayLongNoteIndices.clear();
		for (BranchType branch = BranchType::Normal; branch < BranchType::Count; IncrementEnum(branch))
		for (Note& note : TestPlayCourse->GetNotes(branch))
		{
			if (!TestPlayAutoBranchActive && branch != TestPlayBranch) continue;
			if (IsComboNote(note.Type))
			{
				const Time noteTime = TestPlayCourse->BeatToPlaybackTime(note.BeatTime, branch) + note.TimeOffset;
				if (noteTime <= TestPlayEndTime)
					TestPlayNotes.push_back({ &note, noteTime, 0, Time::Zero(), 0, false, branch });
			}
			if (IsLongNote(note.Type))
			{
				const Time startTime = TestPlayCourse->BeatToPlaybackTime(note.GetStart(), branch) + note.TimeOffset;
				const Time endTime = TestPlayCourse->BeatToPlaybackTime(note.GetEnd(), branch) + note.TimeOffset;
				if (endTime >= Time::Zero() && startTime <= TestPlayEndTime)
					TestPlayLongNotes.push_back({ &note, startTime, endTime, 0, {}, branch });
			}
		}
		TestPlayNoteIndices.reserve(TestPlayNotes.size());
		for (size_t index = 0; index < TestPlayNotes.size(); index++)
			TestPlayNoteIndices.emplace(TestPlayNotes[index].Source, index);
		TestPlayLongNoteIndices.reserve(TestPlayLongNotes.size());
		for (size_t index = 0; index < TestPlayLongNotes.size(); index++)
			TestPlayLongNoteIndices.emplace(TestPlayLongNotes[index].Source, index);
		TestPlayEndTime = Max(TestPlayEndTime, TestPlayStartTime);
		TestPlayPreviousPlaybackSpeed = context.GetPlaybackSpeed();
		TestPlayPlaybackSpeed = Clamp(*Settings.TestPlay.PlaybackSpeedPercent, 25, 100) / 100.0f;
		context.TestPlayActive = IsTestPlaying = true;
		context.SfxVoicePool.PauseAllFutureVoices();
		ResetTestPlayAttempt(context, TestPlayStartTime);
	}

	void ChartGamePreview::ResetTestPlayState(Time startTime)
	{
		TestPlayAttemptStartTime = startTime;
		TestPlayRecordedThrough = startTime;
		if (TestPlayAutoBranchActive)
		{
			TestPlayBranchStartBeat = TestPlayCourse->PlaybackTimeToBeat(startTime, TestPlayBranch, TestPlayBranchStartBeat);
			erase_remove_if(TestPlayBranchDecisions, [&](const auto& decision)
				{ return decision.first >= TestPlayCourse->Branches.size() || TestPlayCourse->Branches[decision.first].GetStart() >= TestPlayBranchStartBeat; });
			for (const auto& decision : TestPlayBranchDecisions)
			{
				const BranchRange& range = TestPlayCourse->Branches[decision.first];
				if (TestPlayBranchStartBeat >= range.GetStart() && TestPlayBranchStartBeat < GetBranchRangeEnd(TestPlayCourse->Branches, range))
					TestPlayInitialBranch = decision.second;
			}
			TestPlayBranch = TestPlayInitialBranch;
			TestPlayVisualBranch = TestPlayBranch;
			TestPlayBranchTransitionFrom = TestPlayBranch;
			TestPlaySectionBeat = TestPlayBranchStartBeat;
			TestPlayLevelHeld = false;
			TestPlayNextBranchIndex = 0;
			while (TestPlayNextBranchIndex < TestPlayCourse->Branches.size() && TestPlayCourse->Branches[TestPlayNextBranchIndex].GetStart() < TestPlayBranchStartBeat) ++TestPlayNextBranchIndex;
			TestPlayNextDecisionIndex = TestPlayNextBranchIndex;
			TestPlayBranchDecisionTime.reset();
			TestPlayBranchTransitionDuration = Time::Zero();
			TestPlayNextSectionIndex = 0;
			while (TestPlayNextSectionIndex < TestPlayCourse->BranchSections.size() && TestPlayCourse->BranchSections[TestPlayNextSectionIndex] < TestPlayBranchStartBeat) ++TestPlayNextSectionIndex;
			TestPlayNextLevelHoldIndex = 0;
			while (TestPlayNextLevelHoldIndex < TestPlayOrderedLevelHolds.size() && TestPlayOrderedLevelHolds[TestPlayNextLevelHoldIndex].BeatTime < TestPlayBranchStartBeat) ++TestPlayNextLevelHoldIndex;
		}
		RebuildTestPlayScrolls();
		TestPlayAttemptJudgements.clear();
		for (auto& note : TestPlayNotes) { note.Judgement = note.NoteTime < startTime ? 4 : 0; note.HitTime = Time::Zero(); note.TimingError = 0; note.WasHit = false; }
		for (auto& note : TestPlayLongNotes) { note.HitCount = 0; note.HitTimes.clear(); }
		TestPlayCombo = TestPlayMaxCombo = 0;
		TestPlayDrumrollCount = TestPlayBalloonHitCount = TestPlayBalloonPopCount = 0;
		TestPlayLastJudgement = 0; TestPlayLastWasHit = false; TestPlayFinished = false;
	}

	TestPlayInterval<Time> ChartGamePreview::GetTestPlaySelectedRange(const ChartContext& context) const
	{
		if (context.RangeSelection.IsActiveAndHasEnd() && context.RangeSelection.GetDuration() > Beat::Zero())
		{
			const auto [first, last] = context.GetRangePlaybackTimes();
			return { first, Min(last, TestPlayEndTime) };
		}
		return { Time::Zero(), Time::Zero() };
	}

	TestPlayInterval<Time> ChartGamePreview::GetTestPlayInterval(const ChartContext& context) const
	{
		const auto selectedRange = GetTestPlaySelectedRange(context);
		if (selectedRange.IsValid())
			return GetTestPlayAttemptInterval(selectedRange, TestPlayAttemptStartTime);
		return { TestPlayAttemptStartTime, TestPlayEndTime };
	}

	void ChartGamePreview::ResetTestPlayAttempt(ChartContext& context, Time startTime)
	{
		startTime = Clamp(startTime, Time::Zero(), TestPlayEndTime);
		if (TestPlayAutoBranchActive) TestPlayInitialBranch = startTime == Time::Zero() ? BranchType::Normal : context.ChartSelectedBranch;
		ResetTestPlayState(startTime);
		if (TestPlayAutoBranchActive) context.SetSelectedChart(TestPlayCourse, TestPlayBranch);
		context.SfxVoicePool.PauseAllFutureVoices();
		context.SetPlaybackSpeed(TestPlayPlaybackSpeed);
		const Time leadIn = Time::FromMS(*Settings.TestPlay.LeadInMilliseconds * TestPlayPlaybackSpeed);
		context.SetCursorTime(startTime == Time::Zero()
			? Min(Time::Zero(), context.Chart.SongOffset) - leadIn
			: Max(Time::Zero(), startTime - leadIn));
		context.SetIsPlayback(true);
	}

	void ChartGamePreview::SeekTestPlay(ChartContext& context, Time targetTime)
	{
		const Time target = Clamp(targetTime, Time::Zero(), TestPlayEndTime);
		context.SetIsPlayback(false);
		context.SetCursorTime(target);
		TestPlayAttemptStartTime = target;
		TestPlayFinished = false;
	}

	void ChartGamePreview::ToggleTestPlayPause(ChartContext& context)
	{
		const auto action = GetTestPlayPauseAction(context.GetIsPlayback(), TestPlayFinished,
			context.GetCursorTime() == TestPlayAttemptStartTime);
		if (action == TestPlayPauseAction::Pause)
		{
			context.SetIsPlayback(false);
			return;
		}
		const Time cursorTime = context.GetCursorTime();
		const TestPlayInterval<Time> selectedRange = GetTestPlaySelectedRange(context);
		if (!CanResumeTestPlay(TestPlayFinished, selectedRange.IsValid(), selectedRange, cursorTime)) return;
		const Time resumeTime = GetTestPlayResumeTime(cursorTime, selectedRange, selectedRange.IsValid());
		const b8 useLeadIn = resumeTime != cursorTime || action == TestPlayPauseAction::ResumeWithLeadIn;
		if (TestPlayAutoBranchActive) TestPlayInitialBranch = resumeTime == Time::Zero() ? BranchType::Normal : context.ChartSelectedBranch;
		ResetTestPlayState(resumeTime);
		if (useLeadIn)
		{
			const Time leadIn = Time::FromMS(*Settings.TestPlay.LeadInMilliseconds * TestPlayPlaybackSpeed);
			context.SetCursorTime(resumeTime == Time::Zero()
				? Min(Time::Zero(), context.Chart.SongOffset) - leadIn
				: Max(Time::Zero(), resumeTime - leadIn));
		}
		context.SetIsPlayback(true);
	}

	void ChartGamePreview::ExitTestPlay(ChartContext& context)
	{
		context.SetIsPlayback(false);
		context.SetPlaybackSpeed(TestPlayPreviousPlaybackSpeed);
		context.TestPlayActive = IsTestPlaying = false;
		TestPlayAutoBranchActive = false;
		context.TestPlaySeekTime.reset();
		context.TestPlaySmoothCursor = context.TestPlayFollowCursor = false;
		context.SetCursorTime(TestPlayStartTime);
		TestPlayNotes.clear();
		TestPlayNoteIndices.clear();
		TestPlayAttemptJudgements.clear();
		TestPlayLongNotes.clear();
		TestPlayLongNoteIndices.clear();
		TestPlayOrderedLevelHolds.clear();
		TestPlayBranchCutoffs.clear();
	}

	void ChartGamePreview::UpdateTestPlay(ChartContext& context)
	{
		if (!IsTestPlaying) return;

		const b8 acceptKeyboard = !Gui::GetIO().WantTextInput;
		b8 barNavigationTriggered = false;
		if (!context.GetIsPlayback() && acceptKeyboard)
		{
			const b8 previousBar = Gui::IsAnyPressed(*Settings.Input.TestPlay_KaLeft, false);
			const b8 nextBar = !previousBar && Gui::IsAnyPressed(*Settings.Input.TestPlay_KaRight, false);
			if (previousBar || nextBar)
			{
				context.SfxVoicePool.PlaySound(SoundEffectType::TaikoKa);
				barNavigationTriggered = true;
				context.TestPlaySmoothCursor = true;
				context.TestPlayFollowCursor = true;
				const Beat currentBeat = context.PlaybackTimeToBeat(context.GetCursorTime());
				Beat previousBeat = Beat::Zero();
				Beat nextBeat = context.PlaybackTimeToBeat(TestPlayEndTime);
				TestPlayCourse->TempoMap.ForEachBeatBar([&](const SortedTempoMap::ForEachBeatBarData& bar)
				{
					if (!bar.IsBar) return ControlFlow::Fallthrough;
					if (bar.Beat < currentBeat) previousBeat = bar.Beat;
					else if (bar.Beat > currentBeat) { nextBeat = bar.Beat; return ControlFlow::Break; }
					return ControlFlow::Fallthrough;
				});
				context.TestPlaySeekTime = TestPlayCourse->BeatToPlaybackTime(previousBar ? previousBeat : nextBeat, TestPlayBranch);
			}
		}
		if (context.TestPlaySeekTime)
		{
			const Time target = *context.TestPlaySeekTime;
			context.TestPlaySeekTime.reset();
			SeekTestPlay(context, target);
		}
		if (acceptKeyboard && !barNavigationTriggered)
		{
			if (Gui::IsAnyPressed(*Settings.Input.TestPlay_Exit, false)) { ExitTestPlay(context); return; }
			if (Gui::IsAnyPressed(*Settings.Input.TestPlay_Retry, false))
				ResetTestPlayAttempt(context, context.RangeSelection.IsActiveAndHasEnd() ? context.BeatToPlaybackTime(context.RangeSelection.GetMin()) : TestPlayStartTime);
			else if (Gui::IsAnyPressed(*Settings.Input.TestPlay_TogglePause, false))
				ToggleTestPlayPause(context);
		}
		if (!context.GetIsPlayback() && acceptKeyboard)
		{
			const f32 speedStep = Settings.General.PlaybackSpeedStepPercent.Value / 100.0f;
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_IncreasePlaybackSpeed, true, InputModifierBehavior::Relaxed))
				TestPlayPlaybackSpeed = Clamp(TestPlayPlaybackSpeed + speedStep, 0.25f, 1.0f);
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_DecreasePlaybackSpeed, true, InputModifierBehavior::Relaxed))
				TestPlayPlaybackSpeed = Clamp(TestPlayPlaybackSpeed - speedStep, 0.25f, 1.0f);
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_SetPlaybackSpeed_100, false)) TestPlayPlaybackSpeed = 1.0f;
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_SetPlaybackSpeed_75, false)) TestPlayPlaybackSpeed = 0.75f;
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_SetPlaybackSpeed_50, false)) TestPlayPlaybackSpeed = 0.5f;
			if (Gui::IsAnyPressed(*Settings.Input.Timeline_SetPlaybackSpeed_25, false)) TestPlayPlaybackSpeed = 0.25f;
			context.SetPlaybackSpeed(TestPlayPlaybackSpeed);
		}
		if (!context.GetIsPlayback()) return;

		const i32 okWindowMilliseconds = Clamp(*Settings.TestPlay.OkWindowMilliseconds, 1, 1000);
		const i32 goodWindowMilliseconds = Clamp(*Settings.TestPlay.GoodWindowMilliseconds, 1, okWindowMilliseconds);
		const i32 badWindowMilliseconds = Clamp(*Settings.TestPlay.BadWindowMilliseconds, okWindowMilliseconds, 1000);
		const TestPlayInterval<Time> selectedRange = GetTestPlaySelectedRange(context);
		const b8 loopRange = selectedRange.IsValid();
		if (loopRange)
		{
			if (IsTestPlayLoopDue(context.GetCursorTime(), selectedRange.End,
				Time::FromMS((badWindowMilliseconds + Clamp(*Settings.TestPlay.LoopDelayMilliseconds, 0, 5000)) * TestPlayPlaybackSpeed)))
				ResetTestPlayAttempt(context, selectedRange.Start);
		}
		if (TestPlayFinished) return;
		const TestPlayInterval<Time> interval = GetTestPlayInterval(context);

		const Time now = context.GetCursorTime() - Time::FromMS(*Settings.TestPlay.InputLatencyCompensationMilliseconds * TestPlayPlaybackSpeed);
		UpdateTestPlayBranches(context, TestPlayCourse->PlaybackTimeToBeat(now, TestPlayBranch, context.PlaybackBeatHint));
		TestPlayRecordedThrough = Max(TestPlayRecordedThrough, Min(now, interval.End));
		auto recordJudgement = [&](TestPlayNoteState& note, i32 judgement, i32 timingError, b8 wasHit)
		{
			note.Judgement = judgement;
			note.TimingError = timingError;
			note.WasHit = wasHit;
			StoreLatestTestPlayResult(context.TestPlayJudgements, note.Source,
				ChartContext::TestPlayJudgementData { judgement, timingError, wasHit });
			StoreLatestTestPlayResult(TestPlayAttemptJudgements, note.Source,
				ChartContext::TestPlayJudgementData { judgement, timingError, wasHit });
			note.HitTime = context.GetCursorTime();
			TestPlayLastJudgement = judgement;
			TestPlayLastTimingError = timingError;
			TestPlayLastWasHit = wasHit;
			TestPlayLastJudgementTime = context.GetCursorTime();
			if (judgement == 3) TestPlayCombo = 0;
			else TestPlayMaxCombo = std::max(TestPlayMaxCombo, ++TestPlayCombo);
		};
		auto judgeInput = [&](b8 don)
		{
			const SoundEffectType hitSound = don ? SoundEffectType::TaikoDon : SoundEffectType::TaikoKa;
			TestPlayNoteState* nearest = nullptr;
			Time nearestError = Time::FromSec(1000.0);
			for (auto& note : TestPlayNotes)
			{
				if (note.Judgement != 0 || !interval.Contains(note.NoteTime) || !IsTestPlayNoteActive(note.Source, note.Branch) || IsDonNote(note.Source->Type) != don) continue;
				const Time error = Time::FromSec(Absolute((now - note.NoteTime).Seconds));
				if (error < nearestError) { nearestError = error; nearest = &note; }
			}
			const i32 judgement = nearest ? GetTestPlayJudgement(nearestError.ToMS() / TestPlayPlaybackSpeed,
				goodWindowMilliseconds, okWindowMilliseconds, badWindowMilliseconds) : 0;
			if (nearest && judgement != 0)
			{
				context.SfxVoicePool.PlaySound(hitSound);
				recordJudgement(*nearest, judgement, static_cast<i32>((now - nearest->NoteTime).ToMS() / TestPlayPlaybackSpeed), true);
				return;
			}
			for (auto& longNote : TestPlayLongNotes)
			{
				if (!IsTestPlayNoteActive(longNote.Source, longNote.Branch) || !IsTestPlayLongNoteActive(now, longNote.StartTime, longNote.EndTime, interval)) continue;
				if (IsBalloonNote(longNote.Source->Type) && !don) continue;
				if (IsBalloonNote(longNote.Source->Type) && longNote.HitCount >= longNote.Source->BalloonPopCount) continue;
				++longNote.HitCount;
				longNote.HitTimes.push_back(context.GetCursorTime());
				if (IsDrumrollNote(longNote.Source->Type)) ++TestPlayDrumrollCount;
				else
				{
					++TestPlayBalloonHitCount;
					if (longNote.HitCount == longNote.Source->BalloonPopCount) ++TestPlayBalloonPopCount;
				}
				context.SfxVoicePool.PlaySound(IsBalloonNote(longNote.Source->Type) && longNote.HitCount == longNote.Source->BalloonPopCount ? SoundEffectType::Balloon : hitSound);
				return;
			}
			context.SfxVoicePool.PlaySound(hitSound);
		};
		std::vector<InputBinding> handledBindings;
		auto judgeBindings = [&](const MultiInputBinding& bindings, b8 don)
		{
			for (const InputBinding& binding : bindings)
			{
				if (!context.GetIsPlayback()) break;
				if (binding.Type != InputBindingType::None && std::find(handledBindings.begin(), handledBindings.end(), binding) == handledBindings.end() && Gui::IsPressed(binding, false))
				{
					handledBindings.push_back(binding);
					judgeInput(don);
				}
			}
		};
		if (acceptKeyboard)
		{
			judgeBindings(*Settings.Input.TestPlay_DonLeft, true);
			judgeBindings(*Settings.Input.TestPlay_DonRight, true);
			judgeBindings(*Settings.Input.TestPlay_KaLeft, false);
			judgeBindings(*Settings.Input.TestPlay_KaRight, false);
		}
		if (!context.GetIsPlayback()) return;
		for (auto& note : TestPlayNotes)
			if (note.Judgement == 0 && interval.Contains(note.NoteTime) && IsTestPlayNoteActive(note.Source, note.Branch) && IsTestPlayMissDue(now, note.NoteTime, Time::FromMS(badWindowMilliseconds * TestPlayPlaybackSpeed)))
			{
				recordJudgement(note, 3, 0, false);
				if (!context.GetIsPlayback()) return;
			}
		if (!loopRange && now > TestPlayEndTime + Time::FromMS((badWindowMilliseconds + 250) * TestPlayPlaybackSpeed))
		{
			TestPlayFinished = true;
			context.SetIsPlayback(false);
			context.SetCursorTime(TestPlayEndTime);
		}
	}

	ChartGamePreview::~ChartGamePreview()
	{
		MovieTexture.Unload();
	}

	void ChartGamePreview::UpdateBackgroundMovie(ChartContext& context, Time chartTime, b8 enabled)
	{
		MovieVisible = false;
		MovieError.clear();
		MovieStatus = BackgroundMovieStatus::Inactive;
		const auto definition = ReadBackgroundMovieDefinition(context.Chart.OtherMetadata);
		if (!enabled || definition.FileName.empty())
		{
			if (!MoviePath.empty())
			{
				BackgroundMovie.Close(); MovieTexture.Unload(); MovieFrame = {};
				MoviePath.clear(); MovieTextureSequence = 0;
			}
			return;
		}
		if (!definition.Error.empty())
		{
			MovieStatus = BackgroundMovieStatus::Failed;
			MovieError = definition.Error;
			return;
		}
		const std::string path = Path::TryMakeAbsolute(definition.FileName, context.ChartFilePath);
		if (path.empty())
		{
			MovieStatus = BackgroundMovieStatus::Failed;
			MovieError = "Could not resolve BGMOVIE path";
			return;
		}
		if (MoviePath != path)
		{
			MovieTexture.Unload(); MovieFrame = {}; MovieTextureSequence = 0;
			MoviePath = path;
		}
		const Time time = GetBackgroundMovieTime(chartTime, context.Chart.SongOffset, definition.Offset);
		BackgroundMovie.Request(path, time);
		MovieStatus = BackgroundMovie.Poll(MovieFrame);
		if (MovieStatus == BackgroundMovieStatus::Failed) MovieError = BackgroundMovie.GetError();
		if (MovieStatus == BackgroundMovieStatus::Ready)
		{
			if (!MovieTexture.IsValid() || MovieTexture.GetSize() != MovieFrame.Size)
			{
				MovieTexture.Unload();
				MovieTexture.Load({ CustomDraw::GPUPixelFormat::BGRA, CustomDraw::GPUAccessType::Dynamic, MovieFrame.Size, MovieFrame.Pixels.data() });
				MovieTextureSequence = MovieFrame.Sequence;
			}
			else if (MovieTextureSequence != MovieFrame.Sequence)
			{
				MovieTexture.UpdateDynamic(MovieFrame.Size, MovieFrame.Pixels.data());
				MovieTextureSequence = MovieFrame.Sequence;
			}
			if (!MovieTexture.IsValid())
			{
				MovieStatus = BackgroundMovieStatus::Failed;
				MovieError = "Could not create background video texture";
			}
		}
		MovieVisible = MovieTexture.IsValid() && (MovieStatus == BackgroundMovieStatus::Ready ||
			(MovieStatus == BackgroundMovieStatus::Pending && !VideoExportTime.has_value() && time >= Time::Zero() && time >= MovieFrame.Start));
	}

	void ChartGamePreview::DrawTestPlayControls(ChartContext& context)
	{
		IsTestPlayControlsFocused = false;
		const b8 hasScoreBranch = context.ChartSelectedCourse &&
			std::any_of(context.ChartSelectedCourse->Branches.begin(), context.ChartSelectedCourse->Branches.end(),
				[](const BranchRange& range) { return range.Condition == TJA::BranchCondition::Score; });
		if (!IsTestPlaying && !*Settings.TestPlay.ShowStartButtonsInPreview && !hasScoreBranch) return;
		Gui::SetNextWindowSize(vec2(GuiScale(680.0f), GuiScale(320.0f)), ImGuiCond_FirstUseEver);
		const b8 isVisible = Gui::Begin(UI_WindowName("TAB_TEST_PLAY"), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
		IsTestPlayControlsFocused = Gui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		if (!isVisible)
		{
			Gui::End();
			return;
		}
		if (!IsTestPlaying)
		{
			if (hasScoreBranch) Gui::TextUnformatted(UI_Str("TEST_PLAY_SCORE_BRANCH_UNAVAILABLE"));
			if (*Settings.TestPlay.ShowStartButtonsInPreview)
			{
				Gui::BeginDisabled(context.CompareMode || hasScoreBranch);
				if (Gui::Button(UI_Str("TEST_PLAY_START_BEGINNING"))) StartTestPlay(context, TestPlayStartMode::Beginning);
				Gui::SameLine();
				if (Gui::Button(UI_Str("TEST_PLAY_START_CURRENT"))) StartTestPlay(context, TestPlayStartMode::Current);
				Gui::SameLine();
				Gui::BeginDisabled(!context.Marker.IsActive);
				if (Gui::Button(UI_Str("TEST_PLAY_START_MARKER"))) StartTestPlay(context, TestPlayStartMode::Marker);
				Gui::EndDisabled();
				Gui::EndDisabled();
			}
		}
		else
		{
			if (Gui::Button(context.GetIsPlayback() ? UI_Str("TEST_PLAY_PAUSE") : UI_Str("TEST_PLAY_RESUME")))
				ToggleTestPlayPause(context);
			Gui::SameLine();
			if (Gui::Button(UI_Str("SETTINGS_TEST_PLAY_RETRY")))
				ResetTestPlayAttempt(context, context.RangeSelection.IsActiveAndHasEnd() ? context.BeatToPlaybackTime(context.RangeSelection.GetMin()) : TestPlayStartTime);
			Gui::SameLine();
			if (Gui::Button(UI_Str("SETTINGS_TEST_PLAY_EXIT")))
				ExitTestPlay(context);
			if (IsTestPlaying)
			{
				if (!context.GetIsPlayback())
				{
					for (size_t slot = 0; slot < RecordedVideoRoutes.size(); ++slot)
					{
						if (slot != 0) Gui::SameLine();
						if (Gui::Button(slot == 0 ? UI_Str("TEST_PLAY_RECORD_VIDEO_ROUTE_1P") : UI_Str("TEST_PLAY_RECORD_VIDEO_ROUTE_2P")))
						{
							VideoBranchTestPlayResult result;
							for (const auto& item : TestPlayAttemptJudgements) result.Judgements.emplace_back(item.first, item.second.Judgement);
							for (const auto& note : TestPlayLongNotes) result.LongNoteHitTimes.emplace_back(note.Source, note.HitTimes);
							result.Decisions = TestPlayBranchDecisions;
							auto& recording = RecordedVideoRoutes[slot];
							recording = {};
							recording.Course = TestPlayCourse;
							recording.Changes = context.Undo.NumberOfChangesMade;
							if (!BuildTestPlayVideoBranchRoute(*TestPlayCourse, result, recording.Route)) recording = {};
							++VideoRecordingVersion;
						}
					}
					for (size_t slot = 0; slot < RecordedVideoRoutes.size(); ++slot)
						if (RecordedVideoRoutes[slot].IsValid(TestPlayCourse, context.Undo.NumberOfChangesMade))
							Gui::TextUnformatted(slot == 0 ? UI_Str("TEST_PLAY_VIDEO_ROUTE_RECORDED_1P") : UI_Str("TEST_PLAY_VIDEO_ROUTE_RECORDED_2P"));
					i32 playbackSpeedPercent = static_cast<i32>(std::round(TestPlayPlaybackSpeed * 100.0f));
					if (Gui::SliderInt(UI_Str("SETTINGS_TEST_PLAY_PLAYBACK_SPEED"), &playbackSpeedPercent, 25, 100))
					{
						TestPlayPlaybackSpeed = playbackSpeedPercent / 100.0f;
						context.SetPlaybackSpeed(TestPlayPlaybackSpeed);
					}
					if (context.RangeSelection.IsActive && Gui::Button(UI_Str("TEST_PLAY_CLEAR_RANGE")))
					{
						context.RangeSelection = {};
						SeekTestPlay(context, context.GetCursorTime());
					}
				}
				const auto attemptStats = CalculateTestPlayStatistics(TestPlayAttemptJudgements);
				Gui::TextUnformatted(UI_Str("TEST_PLAY_CURRENT_ATTEMPT"));
				Gui::SameLine();
				Gui::TextUnformatted(TestPlayFinished ? UI_Str("TEST_PLAY_END_STATUS")
					: !context.GetIsPlayback() ? UI_Str("TEST_PLAY_PAUSED_STATUS")
					: GetTestPlaySelectedRange(context).IsValid()
						? UI_Str("TEST_PLAY_LOOP_STATUS") : "");
				Gui::SameLine(); Gui::Text("%s %d  %s %d  %s %d  %s %d  %s %d",
					UI_Str("TEST_PLAY_GOOD"), attemptStats.Good,
					UI_Str("TEST_PLAY_OK"), attemptStats.Ok,
					UI_Str("TEST_PLAY_MISS"), attemptStats.Bad,
					UI_Str("TEST_PLAY_COMBO"), TestPlayCombo,
					UI_Str("TEST_PLAY_MAX_COMBO"), TestPlayMaxCombo);
				if (attemptStats.TimedHits > 0)
					Gui::Text("%s: %+.1f ms   %s: %.1f ms   %s: %d   %s: %d", UI_Str("TEST_PLAY_AVERAGE_MS"),
						attemptStats.AverageMs, UI_Str("TEST_PLAY_STDDEV_MS"), attemptStats.StandardDeviationMs,
						UI_Str("TEST_PLAY_FAST"), attemptStats.Fast, UI_Str("TEST_PLAY_SLOW"), attemptStats.Slow);
				else
					Gui::Text("%s: —   %s: —   %s: 0   %s: 0", UI_Str("TEST_PLAY_AVERAGE_MS"), UI_Str("TEST_PLAY_STDDEV_MS"), UI_Str("TEST_PLAY_FAST"), UI_Str("TEST_PLAY_SLOW"));
				Gui::Text("%s: %d   %s: %d   %s: %d", UI_Str("TEST_PLAY_DRUMROLL_HITS"), TestPlayDrumrollCount,
					UI_Str("TEST_PLAY_BALLOON_HITS"), TestPlayBalloonHitCount, UI_Str("TEST_PLAY_BALLOON_POP"), TestPlayBalloonPopCount);
			}
		}
		if (IsTestPlaying && !context.GetIsPlayback() && !context.TestPlayJudgements.empty() && context.ChartSelectedCourse == TestPlayCourse && context.ChartSelectedBranch == TestPlayBranch && Gui::CollapsingHeader(UI_Str("TEST_PLAY_RECORDS")))
		{
			if (Gui::Button(UI_Str("TEST_PLAY_CLEAR_ALL_RECORDS")))
			{
				context.TestPlayJudgements.clear();
				TestPlayAttemptJudgements.clear();
			}
			if (context.RangeSelection.IsActiveAndHasEnd())
			{
				Gui::SameLine();
				if (Gui::Button(UI_Str("TEST_PLAY_CLEAR_RANGE_RECORDS")))
				{
					for (const auto& note : TestPlayNotes)
						if (note.Source->BeatTime >= context.RangeSelection.GetMin() && note.Source->BeatTime <= context.RangeSelection.GetMax())
						{
							context.TestPlayJudgements.erase(note.Source);
							TestPlayAttemptJudgements.erase(note.Source);
						}
				}
			}
			const TestPlayInterval<Time> recordRange = GetTestPlaySelectedRange(context);
			const cstr recordScopes[] = { UI_Str("TEST_PLAY_SCOPE_ALL"), UI_Str("TEST_PLAY_SCOPE_SELECTION") };
			if (!recordRange.IsValid()) TestPlayRecordScope = 0;
			if (Gui::BeginCombo("##TestPlayRecordScope", recordScopes[TestPlayRecordScope]))
			{
				for (i32 scope = 0; scope < 2; scope++)
					if (Gui::Selectable(recordScopes[scope], scope == TestPlayRecordScope, scope == 1 && !recordRange.IsValid() ? ImGuiSelectableFlags_Disabled : 0)) TestPlayRecordScope = scope;
				Gui::EndCombo();
			}
			decltype(context.TestPlayJudgements) visibleRecords;
			for (const auto& note : TestPlayNotes)
			{
				if (TestPlayRecordScope == 1 && !recordRange.Contains(note.NoteTime)) continue;
				if (const auto found = context.TestPlayJudgements.find(note.Source); found != context.TestPlayJudgements.end())
					visibleRecords.insert(*found);
			}
			const auto recordStats = CalculateTestPlayStatistics(visibleRecords);
			Gui::TextUnformatted(UI_Str("TEST_PLAY_SAVED_RECORDS_STATS"));
			Gui::SameLine(); Gui::Text("%s %d  %s %d  %s %d",
				UI_Str("TEST_PLAY_GOOD"), recordStats.Good,
				UI_Str("TEST_PLAY_OK"), recordStats.Ok,
				UI_Str("TEST_PLAY_MISS"), recordStats.Bad);
			if (recordStats.TimedHits > 0)
				Gui::Text("%s: %+.1f ms   %s: %.1f ms   %s: %d   %s: %d", UI_Str("TEST_PLAY_AVERAGE_MS"), recordStats.AverageMs,
					UI_Str("TEST_PLAY_STDDEV_MS"), recordStats.StandardDeviationMs, UI_Str("TEST_PLAY_FAST"), recordStats.Fast, UI_Str("TEST_PLAY_SLOW"), recordStats.Slow);
			else
				Gui::Text("%s: —   %s: —   %s: 0   %s: 0", UI_Str("TEST_PLAY_AVERAGE_MS"), UI_Str("TEST_PLAY_STDDEV_MS"), UI_Str("TEST_PLAY_FAST"), UI_Str("TEST_PLAY_SLOW"));
			const cstr recordFilters[] = { UI_Str("TEST_PLAY_FILTER_ALL"), UI_Str("TEST_PLAY_MISS"), UI_Str("TEST_PLAY_FAST"), UI_Str("TEST_PLAY_SLOW") };
			if (Gui::BeginCombo("##TestPlayRecordFilter", recordFilters[TestPlayRecordFilter]))
			{
				for (i32 filter = 0; filter < 4; filter++)
				{
					if (Gui::Selectable(recordFilters[filter], filter == TestPlayRecordFilter)) TestPlayRecordFilter = filter;
					if (filter == TestPlayRecordFilter) Gui::SetItemDefaultFocus();
				}
				Gui::EndCombo();
			}
			Gui::BeginChild("##TestPlayRecords", vec2(0, GuiScale(180.0f)), true);
			for (const auto& noteState : TestPlayNotes)
			{
				const auto recordIt = visibleRecords.find(noteState.Source);
				if (recordIt == visibleRecords.end()) continue;
				const auto& record = recordIt->second;
				if ((TestPlayRecordFilter == 1 && record.Judgement != 3)
					|| (TestPlayRecordFilter == 2 && (!record.WasHit || record.TimingError >= 0))
					|| (TestPlayRecordFilter == 3 && (!record.WasHit || record.TimingError <= 0))) continue;
				const char* label = record.Judgement == 1 ? UI_Str("TEST_PLAY_GOOD") : record.Judgement == 2 ? UI_Str("TEST_PLAY_OK") : UI_Str("TEST_PLAY_MISS");
				char recordText[128];
				if (record.WasHit)
					sprintf_s(recordText, "%.3f  %s  %s  %+d ms##%p", noteState.Source->BeatTime.BeatsFraction(), EnumNames<NoteType>[EnumToIndex(noteState.Source->Type)].data(), label, record.TimingError, static_cast<const void*>(noteState.Source));
				else
					sprintf_s(recordText, "%.3f  %s  %s (%s)##%p", noteState.Source->BeatTime.BeatsFraction(), EnumNames<NoteType>[EnumToIndex(noteState.Source->Type)].data(), label, UI_Str("TEST_PLAY_MISSED"), static_cast<const void*>(noteState.Source));
				if (Gui::Selectable(recordText))
				{
					context.TestPlaySeekTime = noteState.NoteTime;
					TestPlayJumpTime = noteState.NoteTime;
				}
			}
			Gui::EndChild();
		}
		Gui::End();
	}

	void ChartGamePreview::DrawGui(ChartContext& context, Time animatedCursorTime)
	{
		if (NoteLaneCacheRevision != context.Undo.Revision)
		{
			NoteLaneCaches.clear();
			BarLaneCaches.clear();
			NoteLaneCacheRevision = context.Undo.Revision;
		}
		const b8 isVideoExport = VideoExportTime.has_value();
		if (isVideoExport) animatedCursorTime = *VideoExportTime;
		IsAnyChildWindowFocused = Gui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		const b8 IsAnyChildWindowHovered = !isVideoExport && Gui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		const auto findNoteState = [&](const Note* note) -> const TestPlayNoteState*
		{
			const auto found = TestPlayNoteIndices.find(note);
			return found != TestPlayNoteIndices.end() ? &TestPlayNotes[found->second] : nullptr;
		};
		const auto findLongNoteState = [&](const Note* note) -> const TestPlayLongNoteState*
		{
			const auto found = TestPlayLongNoteIndices.find(note);
			return found != TestPlayLongNoteIndices.end() ? &TestPlayLongNotes[found->second] : nullptr;
		};
		std::vector<std::pair<ChartCourse*, BranchType>> comparedLanes;
		if (isVideoExport)
		{
			if (VideoExportLaneCount > 0)
				for (i32 lane = 0; lane < VideoExportLaneCount; ++lane)
					comparedLanes.emplace_back(VideoExportLanes[lane]->Course, VideoExportLanes[lane]->Branch);
			else comparedLanes.emplace_back(context.ChartSelectedCourse, VideoExportBranch);
		}
		else
		for (const auto& course : context.Chart.Courses)
		{
			if (auto compared = context.ChartsCompared.find(course.get()); compared != context.ChartsCompared.end())
				for (BranchType branch : compared->second)
					comparedLanes.emplace_back(course.get(), branch);
		}
		const i32 nLanes = static_cast<i32>(comparedLanes.size());
		const VideoLayout layout = isVideoExport ? VideoExportLayout : static_cast<VideoLayout>(Clamp(*Settings.General.GamePreviewLayout, 0, 2));
		const b8 isTaikoLayout = layout == VideoLayout::Taiko && nLanes > 0 && nLanes <= 2;
		const b8 isJiroLayout = layout == VideoLayout::Jiro && nLanes > 0 && nLanes <= 2;
		const b8 isPanelLayout = isTaikoLayout || isJiroLayout;
		const b8 maximizePanelPreview = !isVideoExport && isPanelLayout
			&& (isJiroLayout ? *Settings.General.GamePreviewJiroSizeMode : *Settings.General.GamePreviewArcadeSizeMode) == 1;
		const auto& laneSlice = isJiroLayout ? GameJiroLayout.LaneSlice : GameLaneSlice;
		const f32 standardLaneWidth = isJiroLayout ? GameJiroLayout.LaneWidth : GameLaneStandardWidth;
		Camera.LaneSlice = laneSlice;
		const b8 hasCommentLane = !isVideoExport && *Settings.General.GamePreviewShowComments && std::any_of(comparedLanes.begin(), comparedLanes.end(), [](const auto& lane) { return !lane.first->Comments.empty(); });
		f32 commentLaneHeight = 0.0f;
		if (hasCommentLane)
		{
			const f32 commentFontSize = static_cast<f32>(Clamp(*Settings.General.GamePreviewCommentFontSize, 12, 128));
			const f32 wrapWidth = standardLaneWidth - 40.0f - commentFontSize * 0.45f;
			commentLaneHeight = 112.0f;
			for (size_t laneIndex = 0; laneIndex < comparedLanes.size(); laneIndex++)
			{
				const ChartCourse* course = comparedLanes[laneIndex].first;
				if (std::any_of(comparedLanes.begin(), comparedLanes.begin() + laneIndex,
					[course](const auto& lane) { return lane.first == course; })) continue;
				for (const CommentChange& comment : course->Comments)
					commentLaneHeight = std::max(commentLaneHeight, Gui::GetFont()->CalcTextSizeA(commentFontSize, F32Max, wrapWidth, comment.Text.c_str()).y + 24.0f);
			}
		}
		const ImGuiID guiID = Gui::GetItemID();

		static constexpr vec2 buttonMargin = vec2(8.0f);
		vec2 minContentRectSize = vec2(128.0f, nLanes * 72.0f);
		Gui::Dummy(ClampBot(vec2(Gui::GetContentRegionAvail()), minContentRectSize));
		Camera.ScreenSpaceViewportRect = Gui::GetItemRect();
		Camera.ScreenSpaceViewportRect.TL = Camera.ScreenSpaceViewportRect.TL + buttonMargin;
		Camera.ScreenSpaceViewportRect.BR = ClampBot(Camera.ScreenSpaceViewportRect.BR - buttonMargin, Camera.ScreenSpaceViewportRect.TL + (minContentRectSize - vec2(buttonMargin.x, 0.0f)));
		Rect videoCaptureViewport = Camera.ScreenSpaceViewportRect;
		if (isVideoExport && isJiroLayout)
		{
			const vec2 referenceSize = vec2(1920.0f, 1080.0f);
			const vec2 availableSize = videoCaptureViewport.GetSize();
			const f32 scale = std::min(availableSize.x / referenceSize.x, availableSize.y / referenceSize.y);
			videoCaptureViewport = Rect::FromCenterSize(videoCaptureViewport.GetCenter(), referenceSize * scale);
			Camera.ScreenSpaceViewportRect = videoCaptureViewport;
		}
		if (isPanelLayout && !maximizePanelPreview && (!isVideoExport || isJiroLayout))
		{
			const vec2 referenceSize = isJiroLayout ? GameJiroLayout.WorldSize : vec2(1920.0f, 1080.0f);
			const vec2 availableSize = Camera.ScreenSpaceViewportRect.GetSize();
			const f32 scale = std::min(availableSize.x / referenceSize.x, availableSize.y / referenceSize.y);
			Camera.ScreenSpaceViewportRect = Rect::FromCenterSize(Camera.ScreenSpaceViewportRect.GetCenter(), referenceSize * scale);
		}
		else if (isVideoExport)
			Camera.ScreenSpaceViewportRect = FitInside(Camera.ScreenSpaceViewportRect, 16.0f / 9.0f, EFitInside::Contain);
		else if (!isPanelLayout)
		{
			f32 newAspectRatio = GetAspectRatio(Camera.ScreenSpaceViewportRect);
			if (const f32 min = GetAspectRatio(*Settings.General.GameViewportAspectRatioMin); min != 0.0f) newAspectRatio = ClampBot(newAspectRatio, min);
			if (const f32 max = GetAspectRatio(*Settings.General.GameViewportAspectRatioMax); max != 0.0f) newAspectRatio = ClampTop(newAspectRatio, max);

			if (newAspectRatio != 0.0f && newAspectRatio != GetAspectRatio(Camera.ScreenSpaceViewportRect))
				Camera.ScreenSpaceViewportRect = FitInside(Camera.ScreenSpaceViewportRect, newAspectRatio, EFitInside::Cover);
		}

		Rect laneRectBase;
		const vec2 standardSize = vec2(GameLaneStandardWidth + GameLanePaddingL + GameLanePaddingR, nLanes * (GameLaneSlice.TotalHeight() + commentLaneHeight) + GameLanePaddingTop + GameLanePaddingBot);
		const f32 standardAspectRatio = GetAspectRatio(standardSize);
		const f32 viewportAspectRatio = GetAspectRatio(Camera.ScreenSpaceViewportRect);
		if (isPanelLayout)
		{
			const vec2 referenceSize = isJiroLayout ? GameJiroLayout.WorldSize : vec2(1920.0f, 1080.0f);
			if (maximizePanelPreview)
			{
				const f32 minimumWorldHeight = nLanes * laneSlice.TotalHeight() + GameLanePaddingTop + GameLanePaddingBot;
				const vec2 viewportSize = Camera.ScreenSpaceViewportRect.GetSize();
				Camera.WorldToScreenScaleFactor = std::min(viewportSize.x / referenceSize.x, viewportSize.y / minimumWorldHeight);
				Camera.WorldSpaceSize = viewportSize / Camera.WorldToScreenScaleFactor;
			}
			else
			{
				Camera.WorldSpaceSize = referenceSize;
				Camera.WorldToScreenScaleFactor = Camera.ScreenSpaceViewportRect.GetWidth() / Camera.WorldSpaceSize.x;
			}
			const f32 laneLeft = isJiroLayout ? GameJiroLayout.LaneLeft : 498.0f;
			const f32 laneTop = maximizePanelPreview || nLanes == 2 ? (Camera.WorldSpaceSize.y - nLanes * laneSlice.TotalHeight()) * 0.5f
				: isJiroLayout ? GameJiroLayout.LaneTop : 251.0f;
			laneRectBase = Rect::FromTLSize(vec2(laneLeft, laneTop), vec2(Camera.WorldSpaceSize.x - laneLeft, laneSlice.TotalHeight()));
		}
		else if (viewportAspectRatio <= standardAspectRatio) // NOTE: Standard case of (<= 16:9) with a fixed sized lane being centered
		{
			Camera.WorldSpaceSize = vec2(standardSize.x, standardSize.x / viewportAspectRatio);
			Camera.WorldToScreenScaleFactor = Camera.ScreenSpaceViewportRect.GetWidth() / standardSize.x;
			laneRectBase = Rect::FromTLSize(vec2(GameLanePaddingL, GameLanePaddingTop), vec2(GameLaneStandardWidth, GameLaneSlice.TotalHeight()));
			laneRectBase += vec2(0.0f, (Camera.WorldSpaceSize.y * 0.5f) - (standardSize.y * 0.5f));
			if (isVideoExport && nLanes == 2)
			{
				laneRectBase.TL.y = (Camera.WorldSpaceSize.y - 2.0f * GameLaneSlice.TotalHeight()) * 0.5f;
				laneRectBase.BR.y = laneRectBase.TL.y + GameLaneSlice.TotalHeight();
			}
		}
		else // NOTE: Ultra wide case of (> 16:9) with the lane being extended further to the right
		{
			const f32 extendedLaneWidth = (standardSize.x * (viewportAspectRatio / standardAspectRatio)) - GameLanePaddingL - GameLanePaddingR;
			Camera.WorldSpaceSize = vec2(extendedLaneWidth, standardSize.y);
			Camera.WorldToScreenScaleFactor = Camera.ScreenSpaceViewportRect.GetHeight() / Camera.WorldSpaceSize.y;
			laneRectBase = Rect::FromTLSize(vec2(GameLanePaddingL, GameLanePaddingTop), vec2(extendedLaneWidth, GameLaneSlice.TotalHeight()));
		}
		// Set to false to disable the 1 px upward correction at 1080p.
		static constexpr b8 EnableVideoExportVerticalOffset = true;
		Camera.LaneContentOffset = isJiroLayout
			? vec2(GameJiroLayout.HitCircleOffsetX - GameHitCircle.Center.x, laneSlice.ContentCenterY() - GameHitCircle.Center.y)
			: vec2(0.0f);
		if (EnableVideoExportVerticalOffset && VideoExportPixelAligned)
			Camera.LaneContentOffset.y -= isJiroLayout ? Camera.WorldSpaceSize.y / 1080.0f : Camera.WorldSpaceSize.x / 1920.0f;

#if 0 // DEBUG: ...
		{
			Gui::GetForegroundDrawList()->AddRect(Camera.ScreenSpaceViewportRect.TL, Camera.ScreenSpaceViewportRect.BR, 0xFFFF00FF);
			Gui::GetForegroundDrawList()->AddRect(Camera.WorldToScreenSpace(Camera.LaneRect.TL), Camera.WorldToScreenSpace(Camera.LaneRect.BR), 0xFF00FFFF);
			char b[64];
			Gui::GetForegroundDrawList()->AddText(Camera.WorldToScreenSpace(vec2(0.0f, 0.0f)), 0xFFFF00FF, b, b + sprintf_s(b, "(%.2f, %.2f)", 0.0f, 0.0f));
			Gui::GetForegroundDrawList()->AddText(Camera.WorldToScreenSpace(Camera.WorldSpaceSize), 0xFFFF00FF, b, b + sprintf_s(b, "(%.2f, %.2f)", Camera.WorldSpaceSize.x, Camera.WorldSpaceSize.y));
		}
#endif

		f32 rasterScale = VideoExportResolutionWidth > 0
			? std::max(Camera.WorldToScreenScaleFactor, static_cast<f32>(VideoExportResolutionWidth) / standardSize.x)
			: Camera.WorldToScreenScaleFactor;
		if (isVideoExport)
			rasterScale = std::max(rasterScale, context.Gfx.GetInfo(SprID::Game_Note_Don).RasterScale);
		if (!context.Gfx.IsAsyncLoading())
			context.Gfx.Rasterize(SprGroup::Game, rasterScale);
		ImDrawList* drawList = isVideoExport && VideoExportDrawListTarget ? VideoExportDrawListTarget : Gui::GetWindowDrawList();
		const f32 videoCaptureScale = isVideoExport && VideoExportResolutionWidth > 0
			? static_cast<f32>(VideoExportResolutionWidth) / videoCaptureViewport.GetWidth() : 1.0f;
		const f32 previousFontDensity = FontMain->CurrentRasterizerDensity;
		const f32 previousFringeScale = drawList->_FringeScale;
		if (isVideoExport)
		{
			FontMain->CurrentRasterizerDensity = std::max(1.0f, videoCaptureScale);
			drawList->_FringeScale = previousFringeScale / videoCaptureScale;
		}
		defer {
			FontMain->CurrentRasterizerDensity = previousFontDensity;
			drawList->_FringeScale = previousFringeScale;
		};

		VideoExportDrawList = isVideoExport ? drawList : nullptr;
		if (isVideoExport) VideoExportViewport = videoCaptureViewport;
		drawList->ChannelsSplit(5); // 0: lane, 1: judgement mark, 2: bar lines, 3: notes & frame, 4: overlay texts
		drawList->ChannelsSetCurrent(0);

		const Rect windowClipRect = { drawList->GetClipRectMin(), drawList->GetClipRectMax() };
		drawList->PushClipRect(Camera.ScreenSpaceViewportRect.TL, Camera.ScreenSpaceViewportRect.BR, true);
		// jacket or video background
		const b8 useBackgroundMovie = isVideoExport ? VideoUseBackgroundMovie
			: IsTestPlaying ? *Settings.TestPlay.UseBackgroundMovie : *Settings.General.GamePreviewUseBackgroundMovie;
		const Time movieChartTime = isVideoExport ? *VideoExportTime
			: context.GetIsPlayback() || (IsTestPlaying && !context.TestPlaySmoothCursor) ? context.GetCursorTime() : animatedCursorTime;
		UpdateBackgroundMovie(context, movieChartTime, useBackgroundMovie);
		if (!isVideoExport && !MovieError.empty())
		{
			const vec2 errorPosition = Camera.ScreenSpaceViewportRect.TL + vec2(8.0f);
			drawList->ChannelsSetCurrent(4);
			drawList->AddText(errorPosition, 0xFF8080FF, UI_Str("BACKGROUND_MOVIE_FAILED"));
			if (Gui::IsMouseHoveringRect(errorPosition, errorPosition + Gui::CalcTextSize(UI_Str("BACKGROUND_MOVIE_FAILED"))))
				Gui::SetTooltip("%s", MovieError.c_str());
			drawList->ChannelsSetCurrent(0);
		}
		if (isVideoExport)
		{
			const Rect viewport = Camera.ScreenSpaceViewportRect;
			drawList->AddRectFilled(viewport.TL, viewport.BR, VideoBackgroundColor);
			const auto* backgroundTexture = VideoUseBackgroundMovie && MovieVisible ? &MovieTexture : VideoBackgroundTexture;
			if (backgroundTexture != nullptr && backgroundTexture->IsValid())
			{
				const vec2 imageSize = backgroundTexture->GetSizeF32();
				const vec2 viewportSize = viewport.GetSize();
				if (imageSize.x > 0.0f && imageSize.y > 0.0f)
				{
					if (VideoBackgroundImageFit == VideoBackgroundFit::ContainWithWidth)
					{
						const vec2 backgroundSize = imageSize * (viewportSize.x / imageSize.x);
						const vec2 backgroundTopLeft = viewport.TL + (viewportSize - backgroundSize) * 0.5f;
						drawList->AddImage(backgroundTexture->GetTexID(), backgroundTopLeft, backgroundTopLeft + backgroundSize,
							vec2(0.0f), vec2(1.0f), Gui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.35f, 0.35f, 1.0f)));
					}
					vec2 drawnSize = viewportSize;
					if (VideoBackgroundImageFit != VideoBackgroundFit::Stretch)
					{
						const f32 widthScale = viewportSize.x / imageSize.x;
						const f32 heightScale = viewportSize.y / imageSize.y;
						const f32 scale = VideoBackgroundImageFit == VideoBackgroundFit::Width ? widthScale
							: VideoBackgroundImageFit == VideoBackgroundFit::Height ? heightScale
							: std::min(widthScale, heightScale);
						drawnSize = imageSize * scale;
					}
					const vec2 drawnTopLeft = viewport.TL + (viewportSize - drawnSize) * 0.5f;
					drawList->AddImage(backgroundTexture->GetTexID(), drawnTopLeft, drawnTopLeft + drawnSize);
				}
			}
		}
		else if (MovieVisible)
		{
			const vec2 size = MovieTexture.GetSizeF32();
			const Rect fitted = FitInside(size, Rect::FromTLSize({}, size), Camera.ScreenSpaceViewportRect, EFitInside::Contain).first;
			drawList->AddRectFilled(Camera.ScreenSpaceViewportRect.TL, Camera.ScreenSpaceViewportRect.BR, Gui::GetColorU32(ImGuiCol_WindowBg));
			drawList->AddImage(MovieTexture.GetTexID(), fitted.TL, fitted.BR);
		}
		else if (context.JacketTexture.IsValid())
		{
			const vec2 jacketSize = context.JacketTexture.GetSizeF32();
			auto [jacketBoundRect, jacketDrawRect] = FitInside(jacketSize, Rect::FromTLSize({}, jacketSize), Camera.ScreenSpaceViewportRect, EFitInside::Cover);
			const Rect jacketUV = { jacketDrawRect.TL / jacketSize, jacketDrawRect.BR / jacketSize };
			drawList->AddImage(context.JacketTexture.GetTexID(), jacketBoundRect.TL, jacketBoundRect.BR, jacketUV.TL, jacketUV.BR, Gui::ColorU32WithNewAlpha(IM_COL32_WHITE, context.SongJacketFadeAnimationCurrent));
		}
		if (isPanelLayout)
		{
			drawList->ChannelsSetCurrent(4);
			drawList->AddRectFilled(Camera.WorldToScreenSpace(vec2(0.0f, laneRectBase.TL.y)),
				Camera.WorldToScreenSpace(vec2(laneRectBase.TL.x, laneRectBase.TL.y + nLanes * laneSlice.TotalHeight())), 0xE0282525);
			for (i32 lane = 0; lane < nLanes; ++lane)
			{
				const f32 laneTop = laneRectBase.TL.y + lane * laneSlice.TotalHeight();
				const SprID preferredBackground = isJiroLayout
					? (lane == 0 ? SprID::Game_ComboJiro_Background_1P : SprID::Game_ComboJiro_Background_2P)
					: (lane == 0 ? SprID::Game_Combo_Background_1P : SprID::Game_Combo_Background_2P);
				const SprID fallbackBackground = isJiroLayout ? SprID::Count
					: lane == 0 ? SprID::Game_Combo_Background_2P : SprID::Game_Combo_Background_1P;
				for (SprID background : { preferredBackground, fallbackBackground })
				{
					const SprInfo backgroundInfo = context.Gfx.GetInfo(background);
					if (backgroundInfo.SourceSize.x <= 0.0f || backgroundInfo.SourceSize.y <= 0.0f) continue;
					ImImageQuad backgroundQuad;
					if (context.Gfx.GetImageQuad(backgroundQuad, background,
						Camera.WorldToScreenSpace(vec2(0.0f, laneTop)),
						Camera.WorldToScreenSpace(vec2(laneRectBase.TL.x, laneTop + laneSlice.TotalHeight())), vec2(0.0f), vec2(1.0f)) && backgroundQuad.TexID != 0)
					{
						context.Gfx.DrawSprite(drawList, backgroundQuad);
						break;
					}
				}
			}
			drawList->ChannelsSetCurrent(0);
		}


		const auto MousePosThisFrame = Gui::GetMousePos();

		// NOTE: Mouse selection box

		b8 doBoxSelectThisFrame = false;
		b8 clearBoxSelectThisFrame = false;
		if (IsAnyChildWindowHovered && Gui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			BoxSelection.IsActive = true;
			BoxSelection.Action = ChartTimeline::GetBoxSelectionAction(Gui::GetIO());
			ChartTimeline::ItemOwnKeysForBoxSelectionAction(Gui::GetIO(), guiID);
			BoxSelection.WorldSpaceRect.TL = BoxSelection.WorldSpaceRect.BR = Camera.ScreenToWorldSpace(Gui::GetMousePos());
		}
		else if (BoxSelection.IsActive && Gui::IsMouseDown(ImGuiMouseButton_Right))
		{
			BoxSelection.WorldSpaceRect.BR = Camera.ScreenToWorldSpace(Gui::GetMousePos());
			BoxSelection.Action = ChartTimeline::GetBoxSelectionAction(Gui::GetIO());
			ChartTimeline::ItemOwnKeysForBoxSelectionAction(Gui::GetIO(), guiID);
		}
		else
		{
			if (BoxSelection.IsActive)
				BoxSelection.Action = ChartTimeline::GetBoxSelectionAction(Gui::GetIO());

			if (BoxSelection.IsActive && Gui::IsMouseReleased(ImGuiMouseButton_Right)) {
				ChartTimeline::ItemOwnKeysForBoxSelectionAction(Gui::GetIO(), guiID);

				static constexpr f32 minBoxSizeThreshold = 4.0f;
				const Rect screenSpaceRect = Rect(Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.TL), Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.BR));
				if (Absolute(screenSpaceRect.GetWidth()) >= minBoxSizeThreshold && Absolute(screenSpaceRect.GetHeight()) >= minBoxSizeThreshold) {
					doBoxSelectThisFrame = true;

					if (BoxSelection.Action == BoxSelectionAction::Clear) {
						// clear all selection
						auto& course = *context.ChartSelectedCourse;
						for (TimelineRowType rowType = {}; rowType < TimelineRowType::Count; IncrementEnum(rowType)) {
							if (IsNonGenericTimelineRow(rowType))
								continue;
							const GenericList list = TimelineRowToGenericList(rowType, context.ChartSelectedBranch);
							for (size_t i = 0; i < GetGenericListCount(course, list); ++i)
								TrySet<GenericMember::B8_IsSelected>(course, list, i, false);
						}
					}
				}
			}

			clearBoxSelectThisFrame = true;
		}

		defer {
			if (clearBoxSelectThisFrame) {
				BoxSelection.IsActive = false;
				BoxSelection.WorldSpaceRect.TL = BoxSelection.WorldSpaceRect.BR = vec2(0.0f);
			}
			// TODO: Animate notes when including them in selection box (?)
			if (BoxSelection.IsActive)
			{
				// BUG: Doesn't work perfectly (mostly with the scrollbar) but need to prevent other window widgets from being interactable
				Gui::SetActiveID(Gui::GetID(&BoxSelection), Gui::GetCurrentWindow());
			}
		};

		std::vector<std::function<void(f32)>> videoComboDrawCalls;
		i32 iLane = -1;
		for (const auto& [course, branch] : comparedLanes) {
			++iLane;
			if (isVideoExport) Gui::PushID(iLane);
			Gui::PushID(course);
			Gui::PushID(EnumToIndex(branch));
			defer { Gui::PopID(); Gui::PopID(); if (isVideoExport) Gui::PopID(); };
			const b8 isFocusedLane = (!isVideoExport && context.CompareMode && course == context.ChartSelectedCourse && branch == context.ChartSelectedBranch);
			VideoExportLaneState* videoLane = isVideoExport && VideoExportLaneCount > 0 ? VideoExportLanes[iLane] : nullptr;

			Camera.LaneRect = laneRectBase + vec2{ 0, iLane * (laneSlice.TotalHeight() + (isPanelLayout ? 0.0f : commentLaneHeight)) };

			const TempoMapAccelerationStructure& tempoChanges = course->GetPlaybackTiming(branch);
			const SortedJPOSScrollChangesList& jposScrollChanges = course->JPOSScrollChanges;
			const std::vector<TempoChange>& tempos = course->TempoMap.Tempo.Sorted;
			const SortedGoGoRangesList& gogoRanges = course->GoGoRanges;

			const b8 isPlayback = isVideoExport || context.GetIsPlayback();
			const BeatAndTime exactCursorBeatAndTime = isVideoExport
				? BeatAndTime { tempoChanges.ConvertTimeToBeatWithHint(*VideoExportTime, context.PlaybackBeatHint, true), *VideoExportTime }
				: context.GetCursorBeatAndTime(course, true, branch);
			const b8 useExactCursor = isPlayback || (IsTestPlaying && !context.TestPlaySmoothCursor);
			const Time cursorTimeOrAnimated = useExactCursor ? exactCursorBeatAndTime.Time : animatedCursorTime;
			const Beat cursorBeatOrAnimatedTrunc = useExactCursor ? exactCursorBeatAndTime.Beat : tempoChanges.ConvertTimeToBeatWithHint(animatedCursorTime, context.PlaybackBeatHint, true);
			const f64 cursorHBScrollBeatOrAnimated = Camera.GetCursorHBScrollBeatTick(cursorBeatOrAnimatedTrunc, cursorTimeOrAnimated, tempoChanges);
			const Beat chartBeatDuration = context.GetUsedBeatDurationFast(*course);

			const auto* lastGogo = gogoRanges.TryFindLastAtBeat(cursorBeatOrAnimatedTrunc);
			const b8 isGogo = (lastGogo != nullptr && cursorBeatOrAnimatedTrunc < lastGogo->GetEnd());
			const Time timeSinceGogo = (lastGogo == nullptr) ? Time::FromSec(F64Max)
				: TimeSinceNoteHit(course->BeatToPlaybackTime(lastGogo->BeatTime, branch), cursorTimeOrAnimated);
			const Time timeAfterGogo = (lastGogo == nullptr) ? Time::FromSec(F64Max)
				: TimeSinceNoteHit(course->BeatToPlaybackTime(lastGogo->GetEnd(), branch), cursorTimeOrAnimated);
			const auto [gogoFireZoom, gogoFireAlpha, gogoLaneZoom, gogoLaneAlpha] = getGogoTransition(isGogo, timeSinceGogo, timeAfterGogo);
			const VideoBranchRoute* videoRoute = isVideoExport ? (videoLane ? videoLane->Route : VideoExportRoute) : nullptr;
			if (videoRoute) UpdateVideoScrolls(*course, cursorTimeOrAnimated, videoLane);
			const auto* playbackScrolls = videoRoute ? (videoLane ? &videoLane->Scrolls : &VideoExportScrolls) : IsTestPlaying && course == TestPlayCourse ? &TestPlayScrolls : nullptr;
			const auto* transitionScrolls = videoRoute ? (videoLane ? &videoLane->TransitionScrolls : &VideoExportTransitionScrolls) : IsTestPlaying && course == TestPlayCourse ? &TestPlayTransitionScrolls : nullptr;
			const VideoBranchVisualState videoState = videoRoute ? videoRoute->GetVisualState(*course, cursorTimeOrAnimated) : VideoBranchVisualState {};
			const b8 autoBranchLane = videoRoute != nullptr || (IsTestPlaying && TestPlayAutoBranchActive && course == TestPlayCourse);
			const BranchType visualBranch = videoRoute ? videoState.Branch : TestPlayVisualBranch;
			const auto getScrollBranch = [&](Beat beat, BranchType noteBranch)
			{
				for (const BranchRange& range : course->Branches)
					if (beat >= range.GetStart() && beat < GetBranchRangeEnd(course->Branches, range)) return noteBranch;
				return visualBranch < BranchType::Count ? visualBranch : noteBranch;
			};
			const BranchType transitionFrom = videoRoute ? videoState.From : TestPlayBranchTransitionFrom;
			const size_t transitionIndex = videoRoute ? videoState.Index : TestPlayBranchTransitionIndex;
			const b8 hasTransition = videoRoute ? videoState.Index != SIZE_MAX : TestPlayBranchDecisionTime.has_value();
			const Time transitionTime = videoRoute ? videoState.DecisionTime : TestPlayBranchDecisionTime.value_or(Time::Zero());
			const Time transitionDuration = videoRoute ? videoState.Duration : TestPlayBranchTransitionDuration;
			b8 branchSliding = false;
			f32 branchSlidePosition = static_cast<f32>(EnumToIndex(visualBranch));
			if (autoBranchLane && hasTransition && transitionDuration > Time::Zero())
			{
				const f64 elapsed = (cursorTimeOrAnimated - transitionTime).ToSec();
				const f64 duration = transitionDuration.ToSec();
				if (elapsed >= 0.0 && elapsed < duration)
				{
					branchSliding = true;
					const f32 progress = static_cast<f32>(elapsed / duration);
					const f32 eased = progress * progress * (3.0f - 2.0f * progress);
					branchSlidePosition = static_cast<f32>(EnumToIndex(transitionFrom))
						+ (static_cast<f32>(EnumToIndex(visualBranch)) - static_cast<f32>(EnumToIndex(transitionFrom))) * eased;
				}
			}

			auto laneBorderColor = isFocusedLane ? GameLaneBorderFocusedColor : GameLaneBorderColor;
			const f32 laneBackgroundOpacity = isVideoExport ? VideoLaneBackgroundOpacity : 1.0f - *Settings.Appearance.PreviewLaneBackgroundTransparency / 100.0f;

			Rect stdLaneRectBR = { Camera.LaneRect.TL, vec2{ Camera.LaneRect.TL.x + (isPanelLayout ? Camera.LaneWidth() : standardLaneWidth), Camera.LaneRect.BR.y } };
			Rect stdLaneLeftRect = { Camera.WorldToScreenSpace(stdLaneRectBR.GetTL() - vec2(GameLanePaddingL, 0.0f)), Camera.WorldToScreenSpace(stdLaneRectBR.GetBL()) };
			// NOTE: Lane background and borders
			drawList->ChannelsSetCurrent(0);
			{
				drawList->AddRectFilled( // NOTE: Top border, truncated at standard lane width
					Camera.WorldToScreenSpace(stdLaneRectBR.TL),
					Camera.WorldToScreenSpace(vec2(stdLaneRectBR.BR.x, stdLaneRectBR.TL.y + laneSlice.TopBorder)),
					laneBorderColor);
				drawList->AddRectFilled( // NOTE: Bottom border
					Camera.WorldToScreenSpace(vec2(stdLaneRectBR.TL.x, stdLaneRectBR.TL.y + laneSlice.TopBorder + laneSlice.Content + laneSlice.MidBorder + laneSlice.Footer)),
					Camera.WorldToScreenSpace(stdLaneRectBR.BR), laneBorderColor);
				drawList->AddRectFilled( // NOTE: Keep middle border color unfocused and untruncated
					Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder + laneSlice.Content)),
					Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content + laneSlice.MidBorder)),
					GameLaneBorderColor);
					if (!branchSliding)
					drawList->AddRectFilled( // NOTE: Content
						Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder)),
						Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content)),
						Gui::ColorU32WithAlpha(GameLaneContentBackgroundColor, laneBackgroundOpacity));
					const BranchType laneBranch = autoBranchLane ? visualBranch : branch;
					if (branchSliding)
					{
						const vec2 contentTop = Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder));
						const vec2 contentBottom = Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content));
						drawList->PushClipRect(contentTop, contentBottom, true);
						for (BranchType layer = BranchType::Normal; layer < BranchType::Count; IncrementEnum(layer))
						{
							const f32 offset = (branchSlidePosition - static_cast<f32>(EnumToIndex(layer))) * laneSlice.Content;
							const vec2 top = Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder + offset));
							const vec2 bottom = Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content + offset));
							drawList->AddRectFilled(top, bottom, Gui::ColorU32WithAlpha(GameLaneContentBackgroundColor, laneBackgroundOpacity));
							const u32 color = layer == BranchType::Expert ? *Settings.Appearance.PreviewBranchExpertLaneBackgroundColor
								: layer == BranchType::Master ? *Settings.Appearance.PreviewBranchMasterLaneBackgroundColor : 0;
							if (color != 0) drawList->AddRectFilled(top, bottom, Gui::ColorU32WithAlpha(color, laneBackgroundOpacity));
						}
						drawList->PopClipRect();
					}
					else
					{
						const u32 color = laneBranch == BranchType::Expert ? *Settings.Appearance.PreviewBranchExpertLaneBackgroundColor
							: laneBranch == BranchType::Master ? *Settings.Appearance.PreviewBranchMasterLaneBackgroundColor : 0;
						if (color != 0)
							drawList->AddRectFilled(
								Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder)),
								Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content)),
								Gui::ColorU32WithAlpha(color, laneBackgroundOpacity));
					}
					if (gogoLaneAlpha > 0) {
					f32 worldHeightZoomOffset = laneSlice.Content / 2 * (1 - gogoLaneZoom);
					drawList->AddRectFilled( // NOTE: Gogo layer
						Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder + worldHeightZoomOffset)),
						Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content - worldHeightZoomOffset)),
						Gui::ColorU32WithAlpha(GameLaneContentBackgroundColorGogo, gogoLaneAlpha * laneBackgroundOpacity));
				}
				drawList->AddRectFilled( // NOTE: Footer
					Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(0.0f, laneSlice.TopBorder + laneSlice.Content + laneSlice.MidBorder)),
					Camera.WorldToScreenSpace(Camera.LaneRect.TL + vec2(Camera.LaneWidth(), laneSlice.TopBorder + laneSlice.Content + laneSlice.MidBorder + laneSlice.Footer)),
					Gui::ColorU32WithAlpha(GameLaneFooterBackgroundColor, laneBackgroundOpacity));
			}

			// click lane to select course
			Rect laneRectScreen = {
				Camera.WorldToScreenSpace(stdLaneRectBR.TL - vec2(GameLanePaddingL, 0.0f)),
				Camera.WorldToScreenSpace(stdLaneRectBR.GetBL() + vec2(Camera.LaneWidth(), 0) + vec2(GameLanePaddingR, 0.0f)),
			};
			Gui::SetCursorScreenPos(laneRectScreen.TL);
			if (Gui::InvisibleButton("##GamePreviewLane", laneRectScreen.GetSize(), ImGuiButtonFlags_AllowOverlap) && !IsTestPlaying && !isVideoExport)
				context.SetSelectedChart(course, branch);
			Gui::SetItemAllowOverlap();

			// NOTE: Lane left / right foreground borders, showing the standard lane size
			defer
			{
				drawList->ChannelsSetCurrent(3);
				drawList->AddRectFilled(stdLaneLeftRect.TL, stdLaneLeftRect.BR, laneBorderColor);
				drawList->AddRectFilled(Camera.WorldToScreenSpace(stdLaneRectBR.GetTR()), Camera.WorldToScreenSpace(stdLaneRectBR.GetBR() + vec2(GameLanePaddingR, 0.0f)), laneBorderColor);
				if (isFocusedLane) {
					drawList->AddRect( // NOTE: all-side outline
						Camera.WorldToScreenSpace(stdLaneRectBR.TL - vec2(GameLanePaddingL, 0.0f)),
						Camera.WorldToScreenSpace(stdLaneRectBR.BR + vec2(GameLanePaddingR, 0.0f)),
						GameLaneOutlineFocusedColor);
				}
			};

			// NOTE: Hit indicator circle
			const vec2 hitCirclePosJPos = Camera.GetHitCircleCoordinatesJPOSScroll(jposScrollChanges, cursorTimeOrAnimated, tempoChanges);
			const vec2 hitCirclePosLane = Camera.JPOSScrollToLaneSpace(hitCirclePosJPos);
			const vec2 hitCirclePos = Camera.LaneToScreenSpace(hitCirclePosLane);
			if (gogoFireZoom > 0) {
				// first draw outside frame space (obscured by left border), second draw only above left border
				for (const auto& [chan, clipRect, intersect] : { std::tuple{ 1, windowClipRect, false }, std::tuple{ 4, stdLaneLeftRect, true } }) {
					drawList->ChannelsSetCurrent(chan);
					drawList->PushClipRect(clipRect.TL, clipRect.BR, intersect);
					context.Gfx.DrawSprite(drawList, SprID::Game_Lane_GogoFire,
						SprTransform::FromCenter(hitCirclePos, vec2(Camera.WorldToScreenScale(gogoFireZoom))),
						Gui::ColorU32WithAlpha(0xFFFFFFFF, gogoFireAlpha));
					drawList->PopClipRect();
				}
			}
			drawList->ChannelsSetCurrent(1);
			const auto circleSegments = [&](f32 radius)
			{
				return isVideoExport ? drawList->_CalcCircleAutoSegmentCount(Camera.WorldToScreenScale(radius) * videoCaptureScale) : 0;
			};
			drawList->AddCircleFilled(
				hitCirclePos,
				Camera.WorldToScreenScale(GameHitCircle.InnerFillRadius), isGogo ? GameLaneHitCircleInnerFillColorGogo : GameLaneHitCircleInnerFillColor,
				circleSegments(GameHitCircle.InnerFillRadius));
			drawList->AddCircle(
				hitCirclePos,
				Camera.WorldToScreenScale(GameHitCircle.InnerOutlineRadius), isGogo ? GameLaneHitCircleInnerOutlineColorGogo : GameLaneHitCircleInnerOutlineColor,
				circleSegments(GameHitCircle.InnerOutlineRadius), Camera.WorldToScreenScale(GameHitCircle.InnerOutlineThickness));
			drawList->AddCircle(
				hitCirclePos,
				Camera.WorldToScreenScale(GameHitCircle.OuterOutlineRadius), isGogo ? GameLaneHitCircleOuterOutlineColorGogo : GameLaneHitCircleOuterOutlineColor,
				circleSegments(GameHitCircle.OuterOutlineRadius), Camera.WorldToScreenScale(GameHitCircle.OuterOutlineThickness));

			if (*Settings.General.GamePreviewShowJPOSPosition && hitCirclePosJPos != vec2{ 0, 0 }) {
				std::string str = Complex(hitCirclePosJPos.x, hitCirclePosJPos.y).toStringCompat("\n");
				const vec2 textSize = Gui::CalcTextSize(str);
				vec2 posTxtJPos = hitCirclePos + vec2{ -1, -1 } * (Camera.WorldToScreenScale(GameHitCircle.OuterOutlineRadius) + textSize.y / 2);
				if (str.find('\n') != str.npos)
					posTxtJPos.y -= textSize.y / 2;
				posTxtJPos = Max(Camera.ScreenSpaceViewportRect.TL, Min(posTxtJPos, Camera.ScreenSpaceViewportRect.BR - textSize));
				drawList->ChannelsSetCurrent(4);
				drawList->AddText(posTxtJPos, 0xFFFFFFFF, str.c_str(), str.c_str() + str.length());
			}
			if (IsTestPlaying && TestPlayLastJudgement != 0 && context.GetCursorTime() - TestPlayLastJudgementTime < Time::FromMS(*Settings.TestPlay.JudgementDisplayMilliseconds * TestPlayPlaybackSpeed))
			{
				const i32 displayMode = Clamp(*Settings.TestPlay.JudgementDisplayMode, 0, 3);
				const auto display = FormatTestPlayJudgement(TestPlayLastJudgement, TestPlayLastTimingError, TestPlayLastWasHit,
					displayMode, Clamp(*Settings.TestPlay.TimingNeutralWindowMilliseconds, 0, 1000));
				const f32 judgementFontSize = Gui::GetFontSize() * 3.0f;
				const vec2 judgementSize = display.Label ? vec2(Gui::CalcTextSize(display.Label)) * 3.0f : vec2(0.0f);
				const f32 judgementBottom = hitCirclePos.y - Camera.WorldToScreenScale(GameHitCircle.OuterOutlineRadius) - GuiScale(8.0f);
				drawList->ChannelsSetCurrent(4);
				if (!display.Timing.empty())
				{
					const vec2 timingSize = vec2(Gui::CalcTextSize(display.Timing.c_str())) * 2.0f;
					const f32 timingBottom = judgementBottom - (display.Label ? judgementSize.y + GuiScale(2.0f) : 0.0f);
					drawList->AddText(Gui::GetFont(), Gui::GetFontSize() * 2.0f,
						vec2(hitCirclePos.x - timingSize.x * 0.5f, timingBottom - timingSize.y), display.TimingColor, display.Timing.c_str());
				}
				if (display.Label)
					drawList->AddText(Gui::GetFont(), judgementFontSize,
						vec2(hitCirclePos.x - judgementSize.x * 0.5f, judgementBottom - judgementSize.y), 0xFFFFFFFF, display.Label);
			}

			const auto scrollSpeedToView = GetScrollSpeedToView(static_cast<EScrollSpeedViewType>(*Settings.General.ScrollSpeedViewType));
			const auto pxWorldPer4Beats = GetPx720pScrollDistanceView4Beats(context.Chart.ScrollDistance4BeatsType) * GameCamera::ScaleFrom720p
				* (isJiroLayout ? GameJiroLayout.ScrollDistanceScale : 1.0f);
			drawList->ChannelsSetCurrent(2);
			if (hasCommentLane && !course->Comments.empty())
			{
				const b8 commentsAbove = isPanelLayout && nLanes == 2 && iLane == 0;
				const vec2 commentClipTL = Camera.WorldToScreenSpace(commentsAbove
					? vec2(Camera.LaneRect.TL.x, 6.0f) : Camera.LaneRect.GetBL() + vec2(0.0f, 6.0f));
				const vec2 commentClipBR = Camera.WorldToScreenSpace(commentsAbove
					? Camera.LaneRect.GetTR() - vec2(0.0f, 6.0f)
					: isPanelLayout ? vec2(Camera.LaneRect.BR.x, Camera.WorldSpaceSize.y - 6.0f)
					: Camera.LaneRect.GetBR() + vec2(0.0f, commentLaneHeight - 6.0f));
				const f32 fontSize = Camera.WorldToScreenScale(static_cast<f32>(Clamp(*Settings.General.GamePreviewCommentFontSize, 12, 128)));
				const i32 holdMeasures = Clamp(*Settings.General.GamePreviewCommentHoldMeasures, 0, 128);
				const f32 outline = std::max(1.0f, Camera.WorldToScreenScale(1.5f));
				ImFont* font = Gui::GetFont();
				const f32 wrapWidth = Camera.WorldToScreenScale((isPanelLayout ? Camera.LaneWidth() : standardLaneWidth) - 40.0f) - fontSize * 0.45f;
				const Time fadeDuration = Time::FromMS(100.0);
				drawList->PushClipRect(commentClipTL, commentClipBR, true);
				for (size_t commentIndex = 0; commentIndex < course->Comments.size(); commentIndex++)
				{
				const CommentChange& comment = course->Comments.Sorted[commentIndex];
					const Time commentTime = course->BeatToPlaybackTime(comment.BeatTime, branch);
					f32 laneX = Camera.TimeToLaneSpace(cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated,
						commentTime, comment.BeatTime, TJA::DefaultTempo, 1.0f, ScrollMethod::HBSCROLL,
						pxWorldPer4Beats, tempoChanges);
					f32 alpha = 1.0f;
					if (holdMeasures > 0 && cursorTimeOrAnimated >= commentTime)
					{
						const TimeSignatureChange* signatureChange = course->TempoMap.Signature.TryFindLastAtBeat(comment.BeatTime);
						const TimeSignature signature = signatureChange ? signatureChange->Signature : FallbackTimeSignature;
						const Beat measureDuration = std::max(abs(signature.GetDurationPerBar()), Beat::FromTicks(1));
						const Time holdEndTime = course->BeatToPlaybackTime(comment.BeatTime + measureDuration * holdMeasures, branch);
						alpha = Clamp((holdEndTime + fadeDuration - cursorTimeOrAnimated).ToSec_F32() / fadeDuration.ToSec_F32(), 0.0f, 1.0f);
						laneX = 0.0f;
					}
					if (holdMeasures > 0 && commentIndex + 1 < course->Comments.size() && cursorTimeOrAnimated >= commentTime)
					{
						const CommentChange& nextComment = course->Comments.Sorted[commentIndex + 1];
						const Time nextTime = course->BeatToPlaybackTime(nextComment.BeatTime, branch);
						const f32 nextLaneX = Camera.TimeToLaneSpace(cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated,
							nextTime, nextComment.BeatTime, TJA::DefaultTempo, 1.0f, ScrollMethod::HBSCROLL,
							pxWorldPer4Beats, tempoChanges);
						const f32 nextScreenX = Camera.WorldToScreenSpace(Camera.LaneRect.GetBL() + vec2(GameHitCircle.Center.x + Camera.LaneContentOffset.x + nextLaneX, 0.0f)).x;
						const Time laterTime = cursorTimeOrAnimated + fadeDuration;
						const Beat laterBeat = tempoChanges.ConvertTimeToBeatWithHint(laterTime, comment.BeatTime);
						const f64 laterHBBeat = Camera.GetCursorHBScrollBeatTick(laterBeat, laterTime, tempoChanges);
						const f32 laterLaneX = Camera.TimeToLaneSpace(laterTime, laterHBBeat,
							nextTime, nextComment.BeatTime, TJA::DefaultTempo, 1.0f, ScrollMethod::HBSCROLL,
							pxWorldPer4Beats, tempoChanges);
						const f32 fadeDistance = std::max(1.0f, Camera.WorldToScreenScale(nextLaneX - laterLaneX));
						alpha = std::min(alpha, Clamp((nextScreenX - (commentClipBR.x - fadeDistance)) / fadeDistance, 0.0f, 1.0f));
					}
					if (alpha <= 0.0f) continue;
					const vec2 textSize = font->CalcTextSizeA(fontSize, F32Max, wrapWidth, comment.Text.c_str());
					vec2 screenPosition = Camera.WorldToScreenSpace((commentsAbove ? Camera.LaneRect.TL : Camera.LaneRect.GetBL())
						+ vec2(GameHitCircle.Center.x + Camera.LaneContentOffset.x + laneX, commentsAbove ? -10.0f : 10.0f));
					if (commentsAbove) screenPosition.y -= textSize.y;
					const f32 markerSize = fontSize * 0.45f;
					const vec2 textPosition = screenPosition + vec2(markerSize + Camera.WorldToScreenScale(5.0f), 0.0f);
					if (screenPosition.x > commentClipBR.x || textPosition.x + textSize.x < commentClipTL.x) continue;
					const u32 textColor = Gui::ColorU32WithAlpha(*Settings.Appearance.PreviewCommentTextColor, alpha);
					const u32 outlineColor = Gui::ColorU32WithAlpha(*Settings.Appearance.PreviewCommentOutlineColor, alpha);
					const f32 markerTipY = fontSize * (commentsAbove ? 0.65f : 0.15f);
					const f32 markerBaseY = fontSize * (commentsAbove ? 0.15f : 0.65f);
					drawList->AddTriangleFilled(screenPosition + vec2(markerSize * 0.5f, markerTipY),
						screenPosition + vec2(commentsAbove ? markerSize : 0.0f, markerBaseY),
						screenPosition + vec2(commentsAbove ? 0.0f : markerSize, markerBaseY), textColor);
					for (i32 y = -1; y <= 1; y++)
						for (i32 x = -1; x <= 1; x++)
							if (x != 0 || y != 0)
								drawList->AddText(font, fontSize, textPosition + vec2(x * outline, y * outline), outlineColor, comment.Text.c_str(), nullptr, wrapWidth);
					drawList->AddText(font, fontSize, textPosition, textColor, comment.Text.c_str(), nullptr, wrapWidth);
				}
				drawList->PopClipRect();
			}
			struct DeferredBarLineDrawData
			{
				Beat BeatTime;
				vec2 TL;
				vec2 BR;
				u32 Color;
				i32 BarIndex;
				b8 IsVisible;
			};
			const b8 barLinesBehindNotes = *Settings.General.GamePreviewBarLinesBehindNotes;
			const i32 measureNumberDisplay = Clamp(*Settings.General.GamePreviewShowMeasureNumbers, 0, 2);
			std::vector<DeferredBarLineDrawData> reverseBarLineDrawBuffer;
			auto drawBarLine = [&](const DeferredBarLineDrawData& bar)
			{
				if (bar.IsVisible)
					drawList->AddLine(Camera.WorldToScreenSpace(bar.TL), Camera.WorldToScreenSpace(bar.BR), bar.Color, Camera.WorldToScreenScale(GameLaneBarLineThickness));
				if (measureNumberDisplay != 0)
				{
					char barLineStr[32];
					DrawGamePreviewNumericText(context.Gfx, Camera, drawList, SprTransform::FromTL(bar.TL + vec2(5.0f, 1.0f), vec2(0.5f)),
						std::string_view(barLineStr, sprintf_s(barLineStr, "%d", bar.BarIndex)));
				}
			};
			auto drawPreviewBar = [&](ForEachBarLaneData it)
			{
				if (playbackScrolls)
				{
					const BranchType barBranch = videoRoute ? videoRoute->GetDisplayBranch(*course, it.Beat, cursorTimeOrAnimated) : GetTestPlayNoteBranch(it.Beat);
					if (barBranch < BranchType::Count) it.ScrollSpeed = scrollSpeedToView((*playbackScrolls)[EnumToIndex(getScrollBranch(it.Beat, barBranch))].GetScroll(it.Beat));
				}
				if (!it.IsVisible && measureNumberDisplay != 2) return;
				const vec2 lane = Camera.GetNoteCoordinatesLane(hitCirclePosLane, cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated, it.Time, it.Beat, it.Tempo, it.ScrollSpeed, it.ScrollType, pxWorldPer4Beats, tempoChanges, jposScrollChanges);
				const f32 laneX = lane.x, laneY = lane.y;

				if (Camera.IsPointVisibleOnLane(laneX))
				{
					const f32 barLineHalfLength = laneSlice.Content * 0.5f;
					const vec2 tl = Camera.LaneToWorldSpace(laneX, laneY) - Rotate(vec2(0.0f, barLineHalfLength), GetNoteFaceRotationMirror(it.Tempo, it.ScrollSpeed, NoteType::Count).first);
					const vec2 br = Camera.LaneToWorldSpace(laneX, laneY) + Rotate(vec2(0.0f, barLineHalfLength), GetNoteFaceRotationMirror(it.Tempo, it.ScrollSpeed, NoteType::Count).first);
					const b8 isBranchStart = std::any_of(course->Branches.begin(), course->Branches.end(), [&](const BranchRange& branchRange)
					{
						return branchRange.GetStart() == it.Beat;
					});
					const u32 barLineColor = isBranchStart ? GameLaneBranchStartBarLineColor : GameLaneBarLineColor;
					const DeferredBarLineDrawData bar { it.Beat, tl, br, barLineColor, it.BarIndex, it.IsVisible };
					if (barLinesBehindNotes)
						drawBarLine(bar);
					else
						reverseBarLineDrawBuffer.push_back(bar);
				}
			};
			auto& barCache = GetCachedBarLaneData(context, *course, branch, chartBeatDuration);
			const auto& visibleBars = isJiroLayout ? FindJiroPreviewBars(barCache, cursorTimeOrAnimated)
				: FindVisiblePreviewBars(barCache, Camera, cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated, hitCirclePosLane.x, pxWorldPer4Beats, playbackScrolls != nullptr);
			for (size_t index : visibleBars)
				drawPreviewBar(barCache.Bars[index]);

#if 0 // DEBUG: ...
			if (Gui::Begin("Game Preview - Debug", nullptr, ImGuiWindowFlags_NoDocking))
			{
				static char text[64] = "0123456789+-./%";
				static u32 color = 0xFFFFFFFF;
				static SprTransform transform = SprTransform::FromTL(vec2(0.0f));
				Gui::InputText("Text", text, sizeof(text));
				Gui::ColorEdit4_U32("Color", &color);

				Gui::DragFloat2("Position", &transform.Position[0], 1.0f);
				Gui::SliderFloat2("Pivot", &transform.Pivot[0], 0.0f, 1.0f);
				Gui::DragFloat2("Scale", &transform.Scale[0], 0.1f);
				if (Gui::DragFloat("Scale (Uniform)", &transform.Scale[0], 0.1f)) { transform.Scale = vec2(transform.Scale[0]); }
				Gui::SliderFloat("Rotation", &transform.Rotation.Radians, 0.0f, PI * 2.0f);

				DrawGamePreviewNumericText(context.Gfx, Camera, drawList, transform, text, color);
			}
			Gui::End();
#endif

			drawList->ChannelsSetCurrent(3);
			std::vector<Rect> testPlayLabelBounds;
			TestPlayInterval<Time> testPlayNoteInterval {};
			if (IsTestPlaying)
			{
				const auto selectedRange = GetTestPlaySelectedRange(context);
				testPlayNoteInterval = !isPlayback && selectedRange.IsValid() ? selectedRange : GetTestPlayInterval(context);
			}
			const auto [visibleNoteMinX, visibleNoteMaxX] = GetGamePreviewVisibleLaneXRange(Camera);
			auto drawTestPlayNote = [&](const ForEachNoteLaneData& cachedNote, BranchType noteBranch)
			{
				if (!IsTestPlaying && IsRegularNote(cachedNote.OriginalNote->Type) &&
					TimeSinceNoteHit(cachedNote.Time, cursorTimeOrAnimated) > GetTotalGameNoteHitAnimationDuration(cachedNote.OriginalNote->Type)) return;
				const b8 slidingNote = branchSliding && transitionIndex < course->Branches.size()
					&& cachedNote.Beat >= course->Branches[transitionIndex].GetStart()
					&& cachedNote.Beat < GetBranchRangeEnd(course->Branches, course->Branches[transitionIndex]);
				if (autoBranchLane && !slidingNote && (videoRoute ? videoRoute->GetDisplayBranch(*course, cachedNote.Beat, cursorTimeOrAnimated) : GetTestPlayNoteBranch(cachedNote.Beat)) != noteBranch) return;
				ForEachNoteLaneData scrollOverride;
				if (playbackScrolls)
				{
					scrollOverride = cachedNote;
					auto& it = scrollOverride;
					const auto& timeline = (slidingNote ? *transitionScrolls : *playbackScrolls)[EnumToIndex(getScrollBranch(it.Beat, noteBranch))];
					it.ScrollSpeed = timeline.GetScroll(it.Beat);
					it.ScrollSpeedView = scrollSpeedToView(it.ScrollSpeed);
					it.Tail.ScrollSpeed = timeline.GetScroll(it.Tail.Beat);
					it.Tail.ScrollSpeedView = scrollSpeedToView(it.Tail.ScrollSpeed);
				}
				const auto& it = playbackScrolls ? scrollOverride : cachedNote;
				const f32 slideYOffset = slidingNote ? (branchSlidePosition - static_cast<f32>(EnumToIndex(noteBranch))) * laneSlice.Content : 0.0f;
				if (IsTestPlaying && !testPlayNoteInterval.Contains(it.Time)) return;
				const b8 canCullRegularNote = !IsTestPlaying && !doBoxSelectThisFrame && !it.OriginalNote->IsSelected
					&& IsRegularNote(it.OriginalNote->Type) && cursorTimeOrAnimated < it.Time;
				if (canCullRegularNote && !(cursorTimeOrAnimated >= cachedNote.HeadPosition.SuddenShowTime)) return;
				auto headPosition = GetCachedGameNotePosition(Camera, it, cachedNote.HeadPosition, hitCirclePosLane,
					cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated, pxWorldPer4Beats, tempoChanges, jposScrollChanges);
				headPosition.Original.y += slideYOffset;
				headPosition.Moved.y += slideYOffset;
				if (canCullRegularNote && (headPosition.Moved.x < visibleNoteMinX || headPosition.Moved.x > visibleNoteMaxX)) return;
				const vec2 laneHeadOrig = headPosition.Original;
				if (IsTestPlaying && !isPlayback && IsComboNote(it.OriginalNote->Type))
				{
					const auto record = context.TestPlayJudgements.find(it.OriginalNote);
					const vec2 notePosition = Camera.LaneToScreenSpace(laneHeadOrig);
					if (record != context.TestPlayJudgements.end() && ImGui::IsMouseHoveringRect(
						notePosition - vec2(GuiScale(14.0f)), notePosition + vec2(GuiScale(14.0f))))
					{
						const auto& judgement = record->second;
						const cstr label = judgement.Judgement == 1 ? UI_Str("TEST_PLAY_GOOD") : judgement.Judgement == 2 ? UI_Str("TEST_PLAY_OK") : UI_Str("TEST_PLAY_MISS");
						if (judgement.WasHit) ImGui::SetTooltip("%s  %+d ms", label, judgement.TimingError);
						else ImGui::SetTooltip("%s (%s)", label, UI_Str("TEST_PLAY_MISSED"));
					}
				}
				auto tailPosition = headPosition;
				if (it.Beat != it.Tail.Beat)
				{
					tailPosition = GetCachedGameNotePosition(Camera, it.Tail, cachedNote.TailPosition, hitCirclePosLane,
						cursorTimeOrAnimated, cursorHBScrollBeatOrAnimated, pxWorldPer4Beats, tempoChanges, jposScrollChanges);
					tailPosition.Original.y += slideYOffset;
					tailPosition.Moved.y += slideYOffset;
				}
				if (IsTestPlaying && !isPlayback && IsComboNote(it.OriginalNote->Type))
				{
					const auto record = context.TestPlayJudgements.find(it.OriginalNote);
						if (record != context.TestPlayJudgements.end() && ShouldShowPausedTestPlayJudgement(
							record->second.Judgement, record->second.TimingError, record->second.WasHit,
							*Settings.TestPlay.PausedJudgementFilter, *Settings.TestPlay.PausedJudgementThresholdMilliseconds))
						{
							const auto& judgement = record->second;
						if (Camera.IsPointVisibleOnLane(laneHeadOrig.x))
						{
							const i32 displayMode = Clamp(*Settings.TestPlay.PausedJudgementDisplayMode, 0, 3);
							const auto display = FormatTestPlayJudgement(judgement.Judgement, judgement.TimingError, judgement.WasHit,
								displayMode, *Settings.TestPlay.TimingNeutralWindowMilliseconds, false);
							const vec2 notePosition = Camera.LaneToScreenSpace(laneHeadOrig);
							const vec2 labelSize = display.Label ? vec2(Gui::CalcTextSize(display.Label)) : vec2(0.0f);
							const vec2 timingSize = Gui::CalcTextSize(display.Timing.c_str());
							const f32 gap = display.Label && !display.Timing.empty() ? GuiScale(2.0f) : 0.0f;
							const vec2 blockSize(Max(labelSize.x, timingSize.x), labelSize.y + timingSize.y + gap);
							b8 labelPlaced = false;
							const vec2 blockTop = PlaceTestPlayJudgementLabel(testPlayLabelBounds, notePosition,
								notePosition.y - Camera.WorldToScreenScale(44.0f), blockSize, GuiScale(2.0f), &labelPlaced);
							drawList->ChannelsSetCurrent(4);
							if (labelPlaced && !display.Timing.empty()) drawList->AddText(vec2(notePosition.x - timingSize.x * 0.5f, blockTop.y), display.TimingColor, display.Timing.c_str());
							if (labelPlaced && display.Label) drawList->AddText(vec2(notePosition.x - labelSize.x * 0.5f, blockTop.y + timingSize.y + gap), 0xFFFFFFFF, display.Label);
							drawList->ChannelsSetCurrent(3);
						}
					}
				}

			Time timeSinceHeadHit = TimeSinceNoteHit(it.Time, cursorTimeOrAnimated);
			if (IsTestPlaying)
				if (const TestPlayNoteState* state = findNoteState(it.OriginalNote))
					timeSinceHeadHit = (state->Judgement > 0 && state->Judgement < 4) ? TimeSinceNoteHit(state->HitTime, cursorTimeOrAnimated) : Time::FromSec(-1.0);
				const Time timeSinceTailHit = TimeSinceNoteHit(it.Tail.Time, cursorTimeOrAnimated);

				const vec2 laneHeadMove = headPosition.Moved, laneTailMove = tailPosition.Moved;

				// sudden show
				const Time suddenShowTimeHead = cachedNote.HeadPosition.SuddenShowTime;
				const Time suddenShowTimeTail = cachedNote.TailPosition.SuddenShowTime;
				b8 isVisibleHead = (cursorTimeOrAnimated >= suddenShowTimeHead);
				b8 isVisibleTail = (cursorTimeOrAnimated >= suddenShowTimeTail);
				b8 isVisibleBody = true;
				b8 isVisible = (isVisibleHead || isVisibleTail);

				// snake in
				vec2 laneHead = laneHeadMove, laneTail = laneTailMove;
				if (it.Beat != it.Tail.Beat) {
					// Range of interpolated sudden appear offset to show
					auto shownRangeSudden = (suddenShowTimeHead <= suddenShowTimeTail) ? std::pair{ suddenShowTimeHead, cursorTimeOrAnimated }
						: std::pair{ cursorTimeOrAnimated, suddenShowTimeTail };
					// Range of roll body to show, in chart time
					auto shownRangePos = ConvertRangeInterval(suddenShowTimeHead.Seconds, suddenShowTimeTail.Seconds, it.Time, it.Tail.Time, shownRangeSudden.first.Seconds, shownRangeSudden.second.Seconds);
					if (!IntervalIntersected(shownRangePos.first, shownRangePos.second, it.Time, it.Tail.Time)) {
						isVisibleBody = false;
					} else {
						shownRangePos = std::pair{
							RClamp(shownRangePos.first, it.Time, it.Tail.Time),
							RClamp(shownRangePos.second, it.Time, it.Tail.Time),
						};
						if (IsBalloonNote(it.OriginalNote->Type)) {
							shownRangePos = std::pair{
								ClampBot(shownRangePos.first, ClampTop(cursorTimeOrAnimated, it.Tail.Time)),
								ClampBot(shownRangePos.second, ClampTop(cursorTimeOrAnimated, it.Tail.Time)),
							};
						}
						std::tie(laneHead, laneTail) = ConvertRangeInterval(it.Time.Seconds, it.Tail.Time.Seconds, laneHeadMove, laneTailMove, shownRangePos.first.Seconds, shownRangePos.second.Seconds);
					}
				}
				// finite check
				if (!(isfinite(laneHead.x) && isfinite(laneHead.y)))
					isVisibleHead = false;
				if (!(isfinite(laneTail.x) && isfinite(laneTail.y)))
					isVisibleTail = false;

				// handle normal visibility rules
				if (IsRegularNote(it.OriginalNote->Type)) {
					isVisible = isVisibleHead;
					if (timeSinceHeadHit >= Time::Zero()) {
						isVisible = isVisibleHead = isVisibleTail = true;
					}
					if (timeSinceHeadHit > GetTotalGameNoteHitAnimationDuration(it.OriginalNote->Type))
						isVisible = isVisibleHead = isVisibleTail = false;
				if (IsTestPlaying)
					if (const TestPlayNoteState* state = findNoteState(it.OriginalNote); state && state->Judgement == 3)
						isVisible = isVisibleHead = isVisibleTail = false;
				}
				else {
					if (TJA::GetSuddenActiveState(it.Sudden).HideRollActive)
						isVisibleHead = isVisibleBody = false;
					if (TJA::GetSuddenActiveState(it.Tail.Sudden).HideRollActive)
						isVisibleTail = isVisibleBody = false;
					// 0-length body as tail
					if (!isVisibleBody && !isVisibleHead && isVisibleTail) {
						isVisibleBody = true;
						laneHead = laneTail;
					}

				if (IsBalloonNote(it.OriginalNote->Type)) {
					const TestPlayLongNoteState* balloonState = IsTestPlaying ? findLongNoteState(it.OriginalNote) : nullptr;
					if (timeSinceTailHit >= Time::Zero() || (balloonState && balloonState->HitCount >= it.OriginalNote->BalloonPopCount)) {
							laneHead = laneTail;
							isVisible = isVisibleHead = isVisibleTail = isVisibleBody = false;
						}
						else if (timeSinceHeadHit >= Time::Zero() && isVisibleHead)
							laneHead = hitCirclePosLane;
					}
					else { // is bar roll note
						// flying notes in screen?
						isVisible = (timeSinceHeadHit >= Time::Zero() && timeSinceTailHit <= GameNoteHitAnimationDuration)
							// roll body in screen?
							|| (isVisibleBody && Camera.IsRangeVisibleOnLane(Min(laneHead.x, laneTail.x), Max(laneHead.x, laneTail.x)))
							|| (isVisibleHead && Camera.IsRangeVisibleOnLane(laneHead.x, laneHead.x))
							|| (isVisibleTail && Camera.IsRangeVisibleOnLane(laneTail.x, laneTail.x));
					}
				}
				if (isVisible) {
					ReverseNoteDrawBuffer.push_back(DeferredNoteDrawData{
						NoteAttr{ it }, it.OriginalNote, it.Tail,
						laneHead, laneTail,
						slideYOffset, slidingNote,
						isVisibleHead, isVisibleTail, isVisibleBody,
					});
				}
			};
			if (branchSliding || videoRoute)
			{
				for (BranchType layer = BranchType::Normal; layer < BranchType::Count; IncrementEnum(layer))
					for (const auto& note : GetCachedNoteLaneData(context, *course, layer)) drawTestPlayNote(note, layer);
			}
			else
			{
				for (const auto& note : GetCachedNoteLaneData(context, *course, branch)) drawTestPlayNote(note, branch);
				if (autoBranchLane && TestPlayVisualBranch != branch && TestPlayVisualBranch != BranchType::Normal)
					for (const auto& note : GetCachedNoteLaneData(context, *course, TestPlayVisualBranch)) drawTestPlayNote(note, TestPlayVisualBranch);
				if (autoBranchLane && branch != BranchType::Normal)
					for (const auto& note : GetCachedNoteLaneData(context, *course, BranchType::Normal)) drawTestPlayNote(note, BranchType::Normal);
			}

			if (!barLinesBehindNotes)
				std::stable_sort(ReverseNoteDrawBuffer.begin(), ReverseNoteDrawBuffer.end(), [](const DeferredNoteDrawData& a, const DeferredNoteDrawData& b) { return a.Beat < b.Beat; });

			b8 balloonPopCountDrawn = false; // prevent long pop count text from overlapping with long combo text
			const f32 rollsPerSecond = videoRoute ? VideoRollsPerSecond : *Settings.General.DrumrollPreviewRollsPerSecond;
			const Time drumrollHitInterval = rollsPerSecond > 0.0f ? Time::FromSec(1.0 / rollsPerSecond) : Time::Zero();
			auto nextBarLine = reverseBarLineDrawBuffer.rbegin();
			for (auto it = ReverseNoteDrawBuffer.rbegin(); it != ReverseNoteDrawBuffer.rend(); it++)
			{
				// Draw bar lines before notes at the same beat so the notes appear in front.
				while (nextBarLine != reverseBarLineDrawBuffer.rend() && nextBarLine->BeatTime >= it->Beat)
				{
					drawBarLine(*nextBarLine);
					++nextBarLine;
				}
				if (it->BranchSliding)
					drawList->PushClipRect(Camera.WorldToScreenSpace(Camera.LaneRect.TL), Camera.WorldToScreenSpace(Camera.LaneRect.BR), true);
				defer { if (it->BranchSliding) drawList->PopClipRect(); };
				Time timeSinceHit = TimeSinceNoteHit(it->Time, cursorTimeOrAnimated);
				if (IsTestPlaying)
					if (const TestPlayNoteState* state = findNoteState(it->OriginalNote))
						timeSinceHit = (state->Judgement > 0 && state->Judgement < 4) ? TimeSinceNoteHit(state->HitTime, cursorTimeOrAnimated) : Time::FromSec(-1.0);
				const TestPlayLongNoteState* longState = IsTestPlaying ? findLongNoteState(it->OriginalNote) : nullptr;
				vec2 laneHeadDisplay = it->LaneHead;
				vec2 laneTailDisplay = it->LaneTail;

				if (IsLongNote(it->OriginalNote->Type))
				{
					i32 videoHitCount = 0, videoTotalHits = 0;
					if (videoRoute)
					{
						const auto hits = GetVideoPreviewRollHitCounts(*course, *it->OriginalNote, rollsPerSecond, cursorTimeOrAnimated);
						videoTotalHits = hits.Total; videoHitCount = hits.Current;
					}
					if (IsBalloonNote(it->OriginalNote->Type))
					{
						if (videoRoute && it->OriginalNote->BalloonPopCount > 0 && videoHitCount >= it->OriginalNote->BalloonPopCount) continue;
						if (longState && longState->HitCount >= it->OriginalNote->BalloonPopCount)
							continue;
						b8 afterHit = (timeSinceHit >= Time::Zero());
						vec2 headPos = afterHit ? hitCirclePosLane + vec2(0.0f, it->BranchSlideYOffset) : it->LaneHead; // head might be detached
						if (IsFuseRoll(it->OriginalNote->Type) && it->HasBody)
							DrawGamePreviewNoteDuration(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(it->LaneHead.x, it->LaneHead.y), Camera.LaneToWorldSpace(it->LaneTail.x, it->LaneTail.y), it->OriginalNote->Type, 0xFFFFFFFF);
						if (it->HasHead || afterHit) {
							laneHeadDisplay = headPos;
							DrawGamePreviewNote(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(headPos.x, headPos.y), it->Tempo, it->ScrollSpeedView, it->OriginalNote->Type, cursorTimeOrAnimated);
						}
						DrawGamePreviewNoteSEText(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(headPos.x, headPos.y), {}, it->Tempo, it->ScrollSpeedView, it->OriginalNote->TempSEType, it->HasHead || afterHit, it->HasEnd, it->HasBody);
						if (afterHit) {
							drawList->ChannelsSetCurrent(4);
							DrawGamePreviewNumericText(context.Gfx, Camera, drawList, SprTransform::FromCenter(Camera.LaneToWorldSpace(headPos.x, headPos.y)),
								std::to_string(videoRoute ? std::max(0, it->OriginalNote->BalloonPopCount - videoHitCount)
									: longState ? std::max(0, it->OriginalNote->BalloonPopCount - longState->HitCount) : it->OriginalNote->BalloonPopCount).c_str(), 0xFFFFFFFF);
							balloonPopCountDrawn = true;
							drawList->ChannelsSetCurrent(3);
						}
					}
					else
					{
						const Time rollDuration = it->Tail.Time - it->Time;
						const i32 maxHitCount = videoRoute ? videoTotalHits - 1 : static_cast<i32>(Floor(rollDuration.ToSec() * rollsPerSecond));
						const i32 hitCount = Max(maxHitCount + 1, 0);
						const auto getHitTime = [&](i32 index) { return it->Time + drumrollHitInterval * index; };

						i32 drumrollHitsSoFar = 0;
						if (timeSinceHit >= Time::Zero())
						{
							if (videoRoute) drumrollHitsSoFar = videoHitCount;
							else if (IsTestPlaying)
								drumrollHitsSoFar = longState ? longState->HitCount : 0;
							else drumrollHitsSoFar = FindRollHitIndex(hitCount, cursorTimeOrAnimated, getHitTime, true);
						}

						const f32 hitPercentage = ConvertRangeClampOutput(0.0f, static_cast<f32>(ClampBot(maxHitCount, 4)), 0.0f, 1.0f, static_cast<f32>(drumrollHitsSoFar));
						const u32 hitNoteColor = InterpolateDrumrollHitColor(it->OriginalNote->Type, hitPercentage);
						if (it->HasBody)
							DrawGamePreviewNoteDuration(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(it->LaneHead.x, it->LaneHead.y), Camera.LaneToWorldSpace(it->LaneTail.x, it->LaneTail.y), it->OriginalNote->Type, hitNoteColor);
						if (it->HasHead)
							DrawGamePreviewNote(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(it->LaneHead.x, it->LaneHead.y), it->Tempo, it->ScrollSpeedView, it->OriginalNote->Type, cursorTimeOrAnimated);
						DrawGamePreviewNoteSEText(context.Gfx, Camera, drawList, Camera.LaneToWorldSpace(it->LaneHead.x, it->LaneHead.y), Camera.LaneToWorldSpace(it->LaneTail.x, it->LaneTail.y), it->Tempo, it->ScrollSpeedView, it->OriginalNote->TempSEType, it->HasHead, it->HasEnd, it->HasBody);

						if (timeSinceHit >= Time::Zero() && !IsTestPlaying)
						{
							const auto relativeHitTime = [&](i32 index) { return getHitTime(index) - cursorTimeOrAnimated; };
							const i32 firstHit = FindRollHitIndex(hitCount, -GameNoteHitAnimationDuration, relativeHitTime);
							const i32 lastHit = FindRollHitIndex(hitCount, Time::Zero(), relativeHitTime);
							for (i32 iHit = lastHit - 1; iHit >= firstHit; iHit--)
							{
								const Time subHitTime = it->Time + (drumrollHitInterval * iHit);
								const Time timeSinceSubHit = TimeSinceNoteHit(subHitTime, cursorTimeOrAnimated);
								// `>` to avoid displaying extra notes when editing (still fails sometimes)
								if (timeSinceSubHit > Time::Zero() && timeSinceSubHit <= GameNoteHitAnimationDuration)
								{
									// TODO: Scale duration, animation speed and path by extended lane width
									const auto hitAnimation = GetNoteHitPathAnimation(timeSinceSubHit, Camera.ExtendedLaneWidthFactor(), nLanes, iLane, it->OriginalNote->Type);
									const vec2 laneOrigin = Camera.GetHitCircleCoordinatesLane(jposScrollChanges, subHitTime, tempoChanges) + vec2(0.0f, it->BranchSlideYOffset);
									const vec2 noteCenter = Camera.LaneToWorldSpace(laneOrigin.x, laneOrigin.y) + hitAnimation.PositionOffset;

									if (hitAnimation.AlphaFadeOut >= 1.0f)
										DrawGamePreviewNote(context.Gfx, Camera, drawList, noteCenter, it->Tempo, it->ScrollSpeedView, ToBigNoteIf(NoteType::Don, IsBigNote(it->OriginalNote->Type)), cursorTimeOrAnimated, hitAnimation);
								}
							}
						}
					}
				}
				else
				{
					// TODO: Instead of offseting the lane x position just draw as HitCenter + PositionOffset directly (?)
					auto hitAnimation = GetNoteHitPathAnimation(timeSinceHit, Camera.ExtendedLaneWidthFactor(), nLanes, iLane, it->OriginalNote->Type);
					const vec2 noteOrigin = (timeSinceHit < Time::Zero()) ? it->LaneHead
						: Camera.GetHitCircleCoordinatesLane(jposScrollChanges, it->Time, tempoChanges) + vec2(0.0f, it->BranchSlideYOffset); // keep flying note's start position
					laneTailDisplay = laneHeadDisplay = noteOrigin + hitAnimation.PositionOffset;
					const vec2 noteCenter = Camera.LaneToWorldSpace(laneHeadDisplay.x, laneHeadDisplay.y);

					if (hitAnimation.AlphaFadeOut >= 1.0f)
						DrawGamePreviewNote(context.Gfx, Camera, drawList, noteCenter, it->Tempo, it->ScrollSpeedView, it->OriginalNote->Type, cursorTimeOrAnimated, hitAnimation, nLanes, iLane);

					if (timeSinceHit <= Time::Zero())
						DrawGamePreviewNoteSEText(context.Gfx, Camera, drawList, noteCenter, {}, it->Tempo, it->ScrollSpeedView, it->OriginalNote->TempSEType, it->HasHead, it->HasEnd, it->HasBody);

					if (const f32 whiteAlpha = (hitAnimation.WhiteFadeIn * hitAnimation.AlphaFadeOut); whiteAlpha > 0.0f)
					{
						// TODO: ...
						// const auto radii = IsBigNote(it->OriginalNote->Type) ? GameRefNoteRadiiBig : GameRefNoteRadiiSmall;
						// drawList->AddCircleFilled(Camera.RefToScreenSpace(refSpaceCenter), Camera.RefToScreenScale(radii.BlackOuter), ImColor(1.0f, 1.0f, 1.0f, whiteAlpha));
					}
				}

				// Select box
				if (!isVideoExport && (!context.CompareMode || isFocusedLane) && (it->OriginalNote->IsSelected || doBoxSelectThisFrame || *Settings.General.GamePreviewShowUnselectedNoteTooltips)) {
					const auto hitBoxSize = vec2(Camera.WorldToScreenScale((IsBigNote(it->OriginalNote->Type) ? GameSelectedNoteHitBoxSizeBig : GameSelectedNoteHitBoxSizeSmall)));
					const auto hitBoxHead = Rect::FromCenterSize(Camera.LaneToScreenSpace(laneHeadDisplay), hitBoxSize);
					const auto hitBoxTail = Rect::FromCenterSize(Camera.LaneToScreenSpace(laneTailDisplay), hitBoxSize);

					if (doBoxSelectThisFrame) {
						const Rect screenSelection = { Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.GetMin()), Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.GetMax()) };
						const b8 isInsideSelectionBox = screenSelection.Overlaps(hitBoxHead) || screenSelection.Overlaps(hitBoxTail);

						b8 isSelected = it->OriginalNote->IsSelected;
						switch (BoxSelection.Action) {
						case BoxSelectionAction::Clear: { isSelected = isInsideSelectionBox; } break;
						case BoxSelectionAction::Add: { if (isInsideSelectionBox) isSelected = true; } break;
						case BoxSelectionAction::Sub: { if (isInsideSelectionBox) isSelected = false; } break;
						case BoxSelectionAction::XOR: { isSelected ^= isInsideSelectionBox; } break;
						}
						if (it->OriginalNote->IsSelected != isSelected) { it->OriginalNote->IsSelected = isSelected; ++course->SelectionRevision; }
					}

					if (it->OriginalNote->IsSelected || *Settings.General.GamePreviewShowUnselectedNoteTooltips) {
						drawList->ChannelsSetCurrent(4); // above notes

						auto drawBox = [&](const ChartTimeline::TempDrawSelectionBox& box, b8 isHead = true)
						{
							if (it->OriginalNote->IsSelected) {
								drawList->AddRectFilled(box.ScreenSpaceRect.TL, box.ScreenSpaceRect.BR, box.FillColor);
								drawList->AddRect(box.ScreenSpaceRect.TL, box.ScreenSpaceRect.BR, box.BorderColor);
							}

							const auto buttonRect = Intersect(box.ScreenSpaceRect, Camera.ScreenSpaceViewportRect);
							if (buttonRect.GetWidth() <= 0 || buttonRect.GetHeight() <= 0)
								return;

							Gui::SetCursorScreenPos(buttonRect.TL);
							char buffer[32]; std::string_view(buffer, sprintf_s(buffer, isHead ? "Selected_%p" : "SelectedEnd_%p", it->OriginalNote));
							Gui::InvisibleButton(buffer, buttonRect.GetSize(), ImGuiButtonFlags_AllowOverlap);
							if (ImGui::BeginItemTooltip()) {
								const b8 isLong = (it->OriginalNote->BeatDuration > Beat::Zero());
								const auto relLaneHead = (it->LaneHead - hitCirclePosLane) / GameCamera::ScaleFrom720p;
								const auto relLaneTail = (it->LaneTail - hitCirclePosLane) / GameCamera::ScaleFrom720p;
								auto fmt = [&](auto&& format)
								{
									std::string res = format(*it, relLaneHead);
									if (isLong) {
										std::string right = format(it->Tail, relLaneTail);
										if (right != res)
											res += " - " + right;
									}
									return res;
								};
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_NOTE_POSITION")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return "(" + ASCII::ToString(pos.x) + ", " + ASCII::ToString(pos.y) + ") " + UI_Str("GAME_PREVIEW_POSITION_UNIT"); }));
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_NOTE_TIME")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return attr.Time.ToString(true); }));
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_INTERNAL_BEAT")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return ASCII::ToString(attr.Beat.Ticks / f32{ Beat::TicksPerBeat }) + " " + UI_Str("GAME_PREVIEW_BEAT_UNIT"); }));
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_HBSCROLL_BEAT")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{
									f32 ticksHBScrollBeat = tempoChanges.ConvertBeatAndTimeToHBScrollBeatTickUsingLookupTableIndexing(attr.Beat, attr.Time);
									return ASCII::ToString(ticksHBScrollBeat / Beat::TicksPerBeat) + " " + UI_Str("GAME_PREVIEW_BEAT_UNIT");
								}));
								ImGui::TextUnformatted(std::string(UI_Str("EVENT_TEMPO")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return ASCII::ToString(attr.Tempo.BPM) + " BPM"; }));
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_NOTE_SCROLL")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return attr.ScrollSpeed.toStringCompat("x") + "x (" + ScrollSpeedToBPM(attr.ScrollSpeed, attr.Tempo).toStringCompat(" BPM") + " BPM)"; }));
								const auto containsComplexScroll = [](const SortedScrollChangesList& changes)
								{ return std::any_of(changes.Sorted.begin(), changes.Sorted.end(), [](const ScrollChange& change) { return !change.ScrollSpeed.IsReal(); }); };
								if (containsComplexScroll(course->ScrollChanges_Normal) || containsComplexScroll(course->ScrollChanges_Expert) || containsComplexScroll(course->ScrollChanges_Master))
								{
									auto previewScroll = [](const NoteAttr& attr)
									{ return (attr.ScrollType == ScrollMethod::BMSCROLL) ? Complex(1.0f, 0.0f) : attr.ScrollSpeedView; };
									ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_SCROLL_SPEED")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
									{
										const f32 speed = std::abs(previewScroll(attr).cpx);
										char buffer[128];
										sprintf_s(buffer, "%.3fx (%.3f BPM)", speed, speed * std::abs(attr.Tempo.BPM));
										return std::string(buffer);
									}));
									ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_SCROLL_ANGLE")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
									{
										Complex direction = previewScroll(attr);
										if (attr.ScrollType == ScrollMethod::NMSCROLL)
											direction *= attr.Tempo.BPM;
										if (direction == Complex(0.0f, 0.0f))
											return std::string(UI_Str("GAME_PREVIEW_SCROLL_ANGLE_UNDEFINED"));
										f32 degrees = -Angle::FromRadians(std::arg(direction.cpx)).ToDegrees();
										if (degrees <= -180.0f) degrees += 360.0f;
										if (std::abs(degrees) < 0.05f) degrees = 0.0f;
										char buffer[64];
										sprintf_s(buffer, "%.1f", degrees);
										return std::string(buffer) + " " + UI_Str("GAME_PREVIEW_ANGLE_UNIT");
									}));
								}
								ImGui::TextUnformatted(std::string(UI_Str("EVENT_SCROLL_TYPE")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{ return UI_StrRuntime(ToI18nString(attr.ScrollType)); }));
								ImGui::TextUnformatted(std::string(UI_Str("GAME_PREVIEW_NOTE_SUDDEN")) + ": " + fmt([&](const NoteAttr& attr, const vec2& pos)
								{
									std::string res = ASCII::ToString(attr.Sudden.AppearanceOffset.Seconds) + " " + UI_Str("GAME_PREVIEW_SECONDS_UNIT") + " (" + UI_Str("GAME_PREVIEW_SUDDEN_SHOW") + "), "
										+ ASCII::ToString(attr.Sudden.MovementOffset.Seconds) + " " + UI_Str("GAME_PREVIEW_SECONDS_UNIT") + " (" + UI_Str("GAME_PREVIEW_SUDDEN_MOVE") + ")";
									if (attr.Sudden.HideRoll)
										res += std::string(", ") + UI_Str("GAME_PREVIEW_SUDDEN_HIDE_ROLL");
									return res;
								}));

								if (IsBalloonNote(it->OriginalNote->Type))
									ImGui::TextUnformatted(std::string(UI_Str("EVENT_PROP_BALLOON_POP_COUNT")) + ": " + ASCII::ToString(it->OriginalNote->BalloonPopCount)
										+ " (" + ASCII::ToString(it->OriginalNote->BalloonPopCount / (it->Tail.Time - it->Time).Seconds) + " " + UI_Str("GAME_PREVIEW_HITS_PER_SECOND_UNIT") + ")");

								ImGui::EndTooltip();
							}
						};

						drawBox({ hitBoxHead, TimelineSelectedNoteBoxBackgroundColor, TimelineSelectedNoteBoxBorderColor });
						if (it->OriginalNote->BeatDuration > Beat::Zero())
							drawBox({ hitBoxTail, TimelineSelectedNoteBoxBackgroundColor, TimelineSelectedNoteBoxBorderColor }, false);

						drawList->ChannelsSetCurrent(3);
					}
				}
			}
			for (; nextBarLine != reverseBarLineDrawBuffer.rend(); ++nextBarLine)
				drawBarLine(*nextBarLine);
			ReverseNoteDrawBuffer.clear();

			// NOTE: Draw combo near the hit circle or over the Taiko combo panel
			{
				const SortedNotesList& notes = course->GetNotes(branch);
				const Note* lastHitNote = notes.TryFindLastAtBeat(cursorBeatOrAnimatedTrunc);
				const i32 displayedCombo = isVideoExport ? (VideoShowCurrentCombo ? (videoLane ? videoLane->Combos : VideoExportCombos).GetCurrentCombo(*VideoExportTime) : 0)
					: IsTestPlaying ? TestPlayCombo : (lastHitNote != nullptr ? lastHitNote->TempComboCount : 0);
				if (displayedCombo > 0 && !(nLanes > 2 && balloonPopCountDrawn))
				{
						constexpr std::string_view sprFontComboCharSet = "0123456789";
						constexpr size_t sprFontComboCharCount = sprFontComboCharSet.size();
						constexpr f32 sprBaseScale = SprDescTable[EnumToIndex(SprID::Game_Font_Combo)].BaseScale;
						const SprInfo sprInfo = context.Gfx.GetInfo(SprID::Game_Font_Combo);
						const vec2 digitSrcSize = vec2(sprInfo.SourceSize.x / f32{ sprFontComboCharCount }, sprInfo.SourceSize.y);
						const f32 sheetW = sprInfo.SourceSize.x;

						char comboStr[16];
						const i32 comboLen = sprintf_s(comboStr, "%d", displayedCombo);

						const auto& display = GetGameComboDisplay(nLanes);
						const f32 digitScale = Camera.WorldToScreenScaleFactor * display.DigitScale / sprBaseScale;
						vec2 digitSize = digitSrcSize * digitScale;

						constexpr i32 taikoComboReferenceDigits = 4;
						constexpr vec2 taikoComboDigitSize = vec2(66.0f, 75.0f);
						const f32 taikoComboReferenceWidth = taikoComboDigitSize.x * taikoComboReferenceDigits + display.PaddingX * (taikoComboReferenceDigits - 1);
						vec2 padding = vec2{ display.PaddingX, display.PaddingY } * digitScale;
						if (isJiroLayout && digitSrcSize.x > 0.0f && digitSrcSize.y > 0.0f)
						{
							const f32 referenceWidth = Camera.LaneRect.TL.x - 16.0f;
							const f32 sourcePaddingX = display.PaddingX * digitSrcSize.x / taikoComboDigitSize.x;
							const f32 fitScale = referenceWidth / (digitSrcSize.x * taikoComboReferenceDigits + sourcePaddingX * (taikoComboReferenceDigits - 1));
							digitSize = digitSrcSize * (fitScale * Camera.WorldToScreenScaleFactor);
							padding.x = sourcePaddingX * fitScale * Camera.WorldToScreenScaleFactor;
						}
						else if (isTaikoLayout && digitSrcSize.x > 0.0f && digitSrcSize.y > 0.0f)
						{
							const f32 fitScale = std::min(taikoComboDigitSize.x / digitSrcSize.x, taikoComboDigitSize.y / digitSrcSize.y);
							digitSize = digitSrcSize * (fitScale * Camera.WorldToScreenScaleFactor);
							padding.x = display.PaddingX * digitSize.x / taikoComboDigitSize.x;
						}
						vec2 digitStep = digitSize + padding;
						const vec2 comboWorldOffset = (nLanes > 2) ? vec2{ 0, 0 }
							: (iLane == 1) ? vec2{ 0.0f, GameHitCircle.OuterOutlineRadius + laneSlice.Footer + display.PaddingY }
							: vec2{ 0.0f, -GameHitCircle.OuterOutlineRadius - display.PaddingY - (IsTestPlaying ? 110.0f : 0.0f) };
						const vec2 comboWorldPos = isJiroLayout ? GameJiroLayout.ComboCenter + vec2(0.0f, Camera.LaneRect.TL.y - GameJiroLayout.LaneTop)
							: isTaikoLayout ? vec2(Camera.LaneRect.TL.x - 8.0f - taikoComboReferenceWidth * 0.5f, Camera.LaneRect.TL.y + 107.0f)
							: Camera.LaneToWorldSpace(hitCirclePosLane.x, hitCirclePosLane.y) + comboWorldOffset;
						vec2 totalSize = vec2{ digitStep.x * comboLen, digitStep.y } - padding;
						if (isPanelLayout && comboLen > taikoComboReferenceDigits && totalSize.x > 0.0f)
						{
							const f32 referenceWidth = digitStep.x * taikoComboReferenceDigits - padding.x;
							const f32 horizontalScale = referenceWidth / totalSize.x;
							digitSize.x *= horizontalScale;
							digitStep.x *= horizontalScale;
							totalSize.x = referenceWidth;
						}

						vec2 comboScreenPos = Camera.WorldToScreenSpace(comboWorldPos);
						const Rect viewport = Camera.ScreenSpaceViewportRect;
						const f32 comboMargin = Camera.WorldToScreenScale(8.0f);
						auto drawCombo = [=, &context](f32 titleBandHeight) mutable
						{
							if (sheetW <= 0.0f) return;
							Rect comboBounds = viewport;
							if (isVideoExport && !isPanelLayout && nLanes == 2)
							{
								comboBounds.TL.y = iLane == 0 ? viewport.TL.y + (VideoTitleVerticalPosition == 0 ? titleBandHeight : 0.0f) : laneRectScreen.BR.y;
								comboBounds.BR.y = iLane == 0 ? laneRectScreen.TL.y : viewport.BR.y - (VideoTitleVerticalPosition == 1 ? titleBandHeight : 0.0f);
								comboBounds.TL.y += comboMargin;
								comboBounds.BR.y -= comboMargin;
								const f32 availableHeight = comboBounds.GetHeight();
								if (availableHeight <= 0.0f) return;
								const f32 fitScale = std::min(1.0f, availableHeight / totalSize.y);
								digitSize *= fitScale;
								digitStep *= fitScale;
								totalSize *= fitScale;
								comboScreenPos.y = iLane == 0 ? comboBounds.BR.y - totalSize.y * 0.5f : comboBounds.TL.y + totalSize.y * 0.5f;
							}
							const vec2 marginTL = totalSize * 0.5f;
							const vec2 marginBR = totalSize - marginTL;
							comboScreenPos = Max(comboBounds.TL + marginTL, Min(comboScreenPos, comboBounds.BR - marginBR));
							const vec2 startPos = comboScreenPos - marginTL;

							for (i32 i = 0; i < comboLen; i++)
							{
								const i32 digit = comboStr[i] - '0';
								const f32 u0 = (digit * digitSrcSize.x) / sheetW;
								const f32 u1 = ((digit + 1) * digitSrcSize.x) / sheetW;
								const vec2 p0 = startPos + vec2(digitStep.x * i, 0.0f);
								const vec2 p1 = p0 + vec2(digitSize.x, digitSize.y);

								const float foreOpacity = 63.0f / 255;
								drawList->ChannelsSetCurrent(isPanelLayout ? 4 : 2);
								context.Gfx.DrawSprite(drawList, SprID::Game_Font_Combo, p0, p1, vec2(u0, 0.0f), vec2(u1, 1.0f), Gui::ColorU32WithNewAlpha(IM_COL32_WHITE, 1 - std::pow(foreOpacity, 2))); // due to pre-multiplied alpha
								drawList->ChannelsSetCurrent(4);
								context.Gfx.DrawSprite(drawList, SprID::Game_Font_Combo, p0, p1, vec2(u0, 0.0f), vec2(u1, 1.0f), Gui::ColorU32WithNewAlpha(IM_COL32_WHITE, foreOpacity));
							}
						};
						if (isVideoExport && !isPanelLayout && nLanes == 2) videoComboDrawCalls.push_back(drawCombo);
						else drawCombo(0.0f);
				}
			}
		}

		drawList->PopClipRect();

		f32 titleBandHeight = 0.0f;
		if (isVideoExport && (VideoShowTitle || VideoShowDifficulty || VideoShowMaxCombo))
		{
			const ChartCourse& course = *comparedLanes.front().first;
			const f32 infoFontSize = Camera.WorldToScreenScale(34.0f) * VideoTitleScale;
			const f32 titleFontSize = Camera.WorldToScreenScale(34.0f) * VideoTitleScale;
			const f32 horizontalMargin = Camera.WorldToScreenScale(26.0f) * VideoTitlePaddingScale;
			const f32 verticalMargin = Camera.WorldToScreenScale(16.0f) * VideoTitlePaddingScale;
			const vec2 topLeft = Camera.ScreenSpaceViewportRect.TL;
			const vec2 bottomRight = Camera.ScreenSpaceViewportRect.BR;
			const f32 fullTextWidth = std::max(1.0f, bottomRight.x - topLeft.x - horizontalMargin * 2.0f);
			const b8 sameDifficulty = nLanes == 2 && course.Type == comparedLanes[1].first->Type &&
				course.Level == comparedLanes[1].first->Level && course.LevelDecimalPlaces == comparedLanes[1].first->LevelDecimalPlaces;
			const auto displayedBranch = [&](i32 lane)
			{
				const auto* state = VideoExportLaneCount > 0 ? VideoExportLanes[lane] : nullptr;
				return state && state->Route && !state->Route->Branches.empty() ? state->Route->Branches.front() : comparedLanes[lane].second;
			};
			const b8 showBranch = sameDifficulty && course.Branches.size() == 1 && comparedLanes[1].first->Branches.size() == 1 &&
				displayedBranch(0) != displayedBranch(1);
			std::string laneInfo[2];
			for (i32 lane = 0; lane < nLanes; ++lane)
			{
				const ChartCourse& laneCourse = *comparedLanes[lane].first;
				std::string& info = laneInfo[lane];
				if (VideoShowDifficulty && (lane == 0 || !sameDifficulty))
				{
					const i32 difficultyIndex = EnumToIndex(laneCourse.Type);
					info = difficultyIndex < EnumCount<DifficultyType> ? UI_StrRuntime(DifficultyTypeNames[difficultyIndex]) : "?";
					info += " \xE2\x98\x85";
					char levelText[32];
					sprintf_s(levelText, "%.0f", std::floor(laneCourse.Level));
					info += levelText;
					if (laneCourse.LevelDecimalPlaces > 0 && 10.0 * (laneCourse.Level - std::floor(laneCourse.Level)) >= DifficultyLevelDecimal::PlusThreshold) info += "+";
				}
				if (VideoShowDifficulty && showBranch)
				{
					if (!info.empty()) info += "  ";
					const char* names[] = { UI_Str("VIDEO_EXPORT_BRANCH_NAME_NORMAL"), UI_Str("VIDEO_EXPORT_BRANCH_NAME_EXPERT"), UI_Str("VIDEO_EXPORT_BRANCH_NAME_MASTER") };
					info += names[EnumToIndex(displayedBranch(lane))];
				}
				if (VideoShowMaxCombo)
				{
					if (!info.empty()) info += nLanes == 1 ? "  /  " : "  ";
					std::string maxCombo = std::to_string((VideoExportLaneCount > 0 ? VideoExportLanes[lane]->Combos : VideoExportCombos).GetMaxCombo());
					for (i32 digit = static_cast<i32>(maxCombo.size()) - 3; digit > 0; digit -= 3) maxCombo.insert(static_cast<size_t>(digit), ",");
					info += maxCombo;
					info += UI_Str("VIDEO_EXPORT_MAX_COMBO");
				}
			}
			std::string rightInfo = laneInfo[0];
			if (nLanes == 2 && !laneInfo[1].empty()) rightInfo += (rightInfo.empty() ? "" : " / ") + laneInfo[1];
			const b8 splitInfo = nLanes == 2 && !laneInfo[1].empty() && VideoTextWidth(FontMain, infoFontSize, rightInfo) > fullTextWidth;
			const std::string infoLines[] = { splitInfo ? laneInfo[0] + " /" : rightInfo, splitInfo ? laneInfo[1] : std::string{} };
			const f32 infoWidth = VideoTextWidth(FontMain, infoFontSize, rightInfo);
			const std::string_view title = context.Chart.ChartTitle;
			std::string_view subtitle = VideoShowSubtitle ? std::string_view(context.Chart.ChartSubtitle) : std::string_view{};
			if (subtitle.substr(0, 2) == "--") subtitle.remove_prefix(2);
			std::string combinedTitle(title);
			if (!subtitle.empty())
			{
				if (!combinedTitle.empty()) combinedTitle += "/";
				combinedTitle += subtitle;
			}
			const b8 hasTitle = VideoShowTitle && !combinedTitle.empty();
			const f32 sharedTitleWidth = std::max(1.0f, fullTextWidth - infoWidth - (rightInfo.empty() ? 0.0f : horizontalMargin));
			const b8 infoOnSecondRow = splitInfo || (hasTitle && !rightInfo.empty() &&
				VideoTextWidth(FontMain, titleFontSize, combinedTitle) > sharedTitleWidth);
			const f32 titleWidth = infoOnSecondRow || rightInfo.empty() ? fullTextWidth : sharedTitleWidth;
			std::string titleLines[2];
			i32 titleLineCount = 0;
			if (hasTitle)
			{
				if (VideoTextWidth(FontMain, titleFontSize, combinedTitle) <= titleWidth)
				{
					titleLines[0] = combinedTitle;
					titleLineCount = 1;
				}
				else if (!subtitle.empty() && !title.empty())
				{
					titleLines[0] = EllipsizeVideoText(FontMain, titleFontSize, title, titleWidth);
					titleLines[1] = EllipsizeVideoText(FontMain, titleFontSize, subtitle, titleWidth);
					titleLineCount = 2;
				}
				else
				{
					const std::string_view displayTitle = title.empty() ? subtitle : title;
					const size_t firstLineEnd = VideoTextPrefixThatFits(FontMain, titleFontSize, displayTitle, titleWidth);
					titleLines[0] = std::string(displayTitle.substr(0, firstLineEnd));
					titleLines[1] = EllipsizeVideoText(FontMain, titleFontSize, displayTitle.substr(firstLineEnd), titleWidth);
					titleLineCount = titleLines[0].empty() ? 1 : 2;
					if (titleLineCount == 1) titleLines[0] = titleLines[1];
				}
			}
			const f32 lineGap = titleFontSize * 0.15f;
			const f32 titleBlockHeight = titleLineCount > 0 ? titleFontSize * titleLineCount + lineGap * (titleLineCount - 1) : 0.0f;
			const f32 infoRowHeight = rightInfo.empty() ? 0.0f : splitInfo ? infoFontSize * 2.0f + lineGap : infoFontSize;
			const f32 rowGap = infoOnSecondRow ? verticalMargin * 0.3f : 0.0f;
			const f32 contentHeight = infoOnSecondRow ? titleBlockHeight + rowGap + infoRowHeight : std::max(titleBlockHeight, infoRowHeight);
			const f32 headerHeight = contentHeight + verticalMargin * 2.0f;
			titleBandHeight = headerHeight;
			const vec2 headerTopLeft = vec2(topLeft.x, VideoTitleVerticalPosition == 1 ? bottomRight.y - headerHeight : topLeft.y);
			const f32 headerBottom = headerTopLeft.y + headerHeight;
			drawList->ChannelsSetCurrent(4);
			drawList->AddRectFilled(headerTopLeft, vec2(bottomRight.x, headerBottom), VideoTitleBandColor);
			drawList->PushClipRect(headerTopLeft,
				vec2(headerTopLeft.x + horizontalMargin + titleWidth, headerBottom), true);
			const f32 titleTop = headerTopLeft.y + verticalMargin + (infoOnSecondRow ? 0.0f : (contentHeight - titleBlockHeight) * 0.5f);
			for (i32 line = 0; line < titleLineCount; ++line)
			{
				const f32 lineWidth = VideoTextWidth(FontMain, titleFontSize, titleLines[line]);
				const f32 titleLeft = headerTopLeft.x + horizontalMargin + (VideoTitleAlignment == 1 ? (titleWidth - lineWidth) * 0.5f : 0.0f);
				drawList->AddText(FontMain, titleFontSize, vec2(titleLeft, titleTop + line * (titleFontSize + lineGap)), VideoTitleColor, titleLines[line].c_str());
			}
			drawList->PopClipRect();
			if (!rightInfo.empty())
			{
				const f32 infoTop = headerTopLeft.y + verticalMargin + (infoOnSecondRow ? titleBlockHeight + rowGap : (contentHeight - infoFontSize) * 0.5f);
				for (i32 line = 0; line < (splitInfo ? 2 : 1); ++line)
				{
					const std::string displayedInfo = EllipsizeVideoText(FontMain, infoFontSize, infoLines[line], fullTextWidth);
					const f32 infoLeft = bottomRight.x - horizontalMargin - VideoTextWidth(FontMain, infoFontSize, displayedInfo);
					drawList->AddText(FontMain, infoFontSize, vec2(infoLeft, infoTop + line * (infoFontSize + lineGap)), 0xFFFFFFFF, displayedInfo.c_str());
				}
			}
		}

		for (auto& drawCombo : videoComboDrawCalls) drawCombo(titleBandHeight);

		// NOTE: Mouse selection box, draw outside frame space
		if (BoxSelection.IsActive)
		{
			drawList->ChannelsSetCurrent(4);

			drawList->AddRectFilled(
				Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.TL),
				Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.BR), TimelineBoxSelectionBackgroundColor);
			drawList->AddRect(
				Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.TL),
				Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.BR), TimelineBoxSelectionBorderColor);

			if (BoxSelection.Action != BoxSelectionAction::Clear)
			{
				const f32 radius = GuiScale(TimelineBoxSelectionRadius);
				const f32 linePadding = GuiScale(TimelineBoxSelectionLinePadding);
				const f32 lineThickness = ClampBot(Round(TimelineBoxSelectionLineThickness * GuiScaleFactorCurrent / 2.0f) * 2.0f, TimelineBoxSelectionLineThickness);

				const vec2 center = Camera.WorldToScreenSpace(BoxSelection.WorldSpaceRect.TL);
				drawList->AddCircleFilled(center, radius, TimelineBoxSelectionFillColor);
				drawList->AddCircle(center, radius, TimelineBoxSelectionBorderColor);
				{
					const Rect horizontal = Rect::FromCenterSize(center, vec2((radius - linePadding) * 2.0f, lineThickness));
					const Rect vertical = Rect::FromCenterSize(center, vec2(lineThickness, (radius - linePadding) * 2.0f));

					if (BoxSelection.Action == BoxSelectionAction::Add)
					{
						drawList->AddRectFilled(horizontal.TL, horizontal.BR, TimelineBoxSelectionInnerColor);
						drawList->AddRectFilled(vertical.TL, vertical.BR, TimelineBoxSelectionInnerColor);
					}
					else if (BoxSelection.Action == BoxSelectionAction::Sub)
					{
						drawList->AddRectFilled(horizontal.TL, horizontal.BR, TimelineBoxSelectionInnerColor);
					}
					else if (BoxSelection.Action == BoxSelectionAction::XOR)
					{
						drawList->AddCircleFilled(center, GuiScale(TimelineBoxSelectionXorDotRadius), TimelineBoxSelectionInnerColor);
					}
				}
			}
		}
		if (isVideoExport)
		{
			const f32 fadeOpacity = VideoFadeOpacity(*VideoExportTime, VideoFadeContentStart, VideoFadeContentEnd,
				VideoFadeInSeconds, VideoFadeOutSeconds, VideoFadeFrameSeconds);
			if (fadeOpacity > 0.0f)
			{
				drawList->ChannelsSetCurrent(4);
				drawList->PushClipRect(Camera.ScreenSpaceViewportRect.TL, Camera.ScreenSpaceViewportRect.BR, true);
				drawList->AddRectFilled(Camera.ScreenSpaceViewportRect.TL, Camera.ScreenSpaceViewportRect.BR,
					Gui::ColorU32WithNewAlpha(IM_COL32_BLACK, fadeOpacity));
				drawList->PopClipRect();
			}
		}
	}
}
