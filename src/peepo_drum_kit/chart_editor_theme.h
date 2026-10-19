#pragma once
#include "core_types.h"
#include "chart.h"
#include "imgui/imgui_include.h"

namespace PeepoDrumKit
{
	inline f32 TimelineRowHeight = 32.0f;
	inline f32 TimelineRowHeightNotes = 48.0f;
	inline f32 TimelineAutoScrollLockContentWidthFactor = 0.35f;
	inline f32 TimelineCursorHeadHeight = 10.0f;
	inline f32 TimelineCursorHeadWidth = 11.0f;
	inline f32 TimelineBoxSelectionRadius = 9.0f;
	inline f32 TimelineBoxSelectionLineThickness = 2.0f;
	inline f32 TimelineBoxSelectionLinePadding = 4.0f;
	inline f32 TimelineBoxSelectionXorDotRadius = 3.0f;
	inline f32 TimelineSongDemoStartMarkerWidth = 9.0f;
	inline f32 TimelineSongDemoStartMarkerHeight = 9.0f;
	inline f32 TimelineSelectedNoteHitBoxSizeSmall = (16.0f * 2.0f) + 2.0f;
	inline f32 TimelineSelectedNoteHitBoxSizeBig = (22.0f * 2.0f) + 2.0f;

	// NOTE: All game coordinates are defined in world space
	constexpr f32 GameWorldStandardHeight = 1080.0f;
	constexpr f32 GameWorldSpaceDistancePerLaneBeat = 356.0f;
	constexpr f32 GameLaneStandardWidth = 1422.0f;
	constexpr f32 GameLaneBarLineThickness = 3.0f;
	struct GameLaneSliceDimensions
	{
		f32 TopBorder = 12.0f;
		f32 Content = 195.0f;
		f32 MidBorder = 6.0f;
		f32 Footer = 39.0f;
		f32 BotBorder = 12.0f;
		constexpr f32 ContentCenterY() const { return TopBorder + (Content / 2.0f); }
		constexpr f32 FooterCenterY() const { return TopBorder + Content + MidBorder + (Footer / 2.0f); }
		constexpr f32 TotalHeight() const { return TopBorder + Content + MidBorder + Footer + BotBorder; }
	};
	constexpr GameLaneSliceDimensions GameLaneSlice;
	constexpr struct
	{
		f32 Scale = 3.5f;
		vec2 WorldSize = vec2(512.0f, 384.0f) * Scale;
		f32 LaneLeft = 92.0f * Scale;
		f32 LaneTop = 154.0f * Scale;
		f32 LaneWidth = 420.0f * Scale;
		f32 HitCircleOffsetX = 35.0f * Scale;
		vec2 ComboCenter = vec2(46.0f, 195.0f) * Scale;
		// A sixteenth at SCROLL 1 is 26.25 px apart at 512x384; the 105 px note image becomes 30 px.
		f32 ScrollDistanceScale = (420.0f * Scale) / (GameWorldSpaceDistancePerLaneBeat * 4.0f);
		GameLaneSliceDimensions LaneSlice = { 2.0f * Scale, 65.0f * Scale, 2.0f * Scale, 14.0f * Scale, 2.0f * Scale };
	} GameJiroLayout;
	constexpr f32 GameLanePaddingL = 6.0f;
	constexpr f32 GameLanePaddingR = 6.0f;
	constexpr f32 GameLanePaddingTop = 64.0f;
	constexpr f32 GameLanePaddingBot = 32.0f;
	constexpr struct
	{
		vec2 Center = vec2(120.0f, GameLaneSlice.ContentCenterY());
		f32 InnerFillRadius = (78.0f / 2.0f);
		f32 InnerOutlineRadius = (100.0f / 2.0f);
		f32 InnerOutlineThickness = 5.0f;
		f32 OuterOutlineRadius = (154.0f / 2.0f);
		f32 OuterOutlineThickness = 5.0f;
	} GameHitCircle;
	constexpr struct
	{
		f32 DigitScale = 1.5f;
		f32 PaddingX = -7.0f;
		f32 PaddingY = 90.0f;
	} GameComboDisplay, GameComboDisplay3UpLanes = { 1, -14.0f, 90.0f };
	constexpr const decltype(GameComboDisplay)& GetGameComboDisplay(i32 nLanes) { return (nLanes > 2) ? GameComboDisplay3UpLanes : GameComboDisplay; }

	inline f32 GameSelectedNoteHitBoxSizeSmall = ((GameHitCircle.InnerOutlineRadius + 8) * 2.0f) + 2.0f;
	inline f32 GameSelectedNoteHitBoxSizeBig = ((GameHitCircle.OuterOutlineRadius + 8) * 2.0f) + 2.0f;

	inline u32 DragTextHoveredColor = 0xFFBCDDDB;
	inline u32 DragTextActiveColor = 0xFFC3F5F2;
	inline u32 InputTextWarningTextColor = 0xFF4C4CFF;

	inline u32 TimelineBackgroundColor = 0xFF282828;
	inline u32 TimelineOutOfBoundsDimColor = 0x731F1F1F;
	inline u32 TimelineWaveformBaseColor = 0x807D7D7D;

	inline u32 TimelineCursorColor = 0xB375AD85;
	inline u32 TimelineItemTextColor = 0xFFF0F0F0;
	inline u32 TimelineItemTextColorShadow = 0xFF000000;
	inline u32 TimelineItemTextColorWarning = 0xFF4C4CFF;
	inline u32 TimelineItemTextUnderlineColor = 0xFFF0F0F0;
	inline u32 TimelineItemTextUnderlineColorShadow = 0xFF000000;

	inline u32 TimelineGoGoBackgroundColorBorder = 0xFF1E1E1E;
	inline u32 TimelineGoGoBackgroundColorBorderSelected = 0xFF979797;
	inline u32 TimelineGoGoBackgroundColorOuter = 0xFF46494D;
	inline u32 TimelineGoGoBackgroundColorInner = 0xFF2D2D33;

	inline u32 TimelineLyricsTextColor = 0xFFDEE5F0;
	inline u32 TimelineLyricsTextColorShadow = 0xFF131415;
	inline u32 TimelineLyricsBackgroundColorBorder = 0xFF1E1E1E;
	inline u32 TimelineLyricsBackgroundColorBorderSelected = 0xFF979797;
	inline u32 TimelineLyricsBackgroundColorOuter = 0xFF46494D;
	inline u32 TimelineLyricsBackgroundColorInner = 0xFF323438;

	inline u32 TimelineJPOSScrollBackgroundColorBorder = 0xB01E1E1E;
	inline u32 TimelineJPOSScrollBackgroundColorBorderSelected = 0xFF979797;
	inline u32 TimelineJPOSScrollBackgroundColorOuter = 0xFF663F8F;
	inline u32 TimelineJPOSScrollBackgroundColorInner = 0xFF363138;

	inline u32 TimelineHorizontalRowLineColor = 0x2D7D7D7D;
	inline u32 TimelineGridBarLineColor = 0x807D7D7D;
	inline u32 TimelineGridBeatLineColor = 0x2D7D7D7D;
	inline u32 TimelineBranchStartLineColor = 0xFF00FFFF;
	inline u32 TimelineBranchRangeBackgroundColor = 0x1800FFFF;
	inline u32 TimelineGridSnapLineColor = 0x1A7D7D7D;
	inline u32 TimelineGridSnapTupletLineColor = 0x1A22BBBC;
	inline u32 TimelineGridSnapQuintupletLineColor = 0x1AB3BC22;
	inline u32 TimelineGridSnapSeptupletLineColor = 0x1AFF97FF;

	inline u32 TimelineBoxSelectionBackgroundColor = 0x262E8F5E;
	inline u32 TimelineBoxSelectionBorderColor = 0xFF2E8F5E;
	inline u32 TimelineBoxSelectionFillColor = 0xFF293730;
	inline u32 TimelineBoxSelectionInnerColor = 0xFFF0F0F0;
	inline u32 TimelineRangeSelectionBackgroundColor = 0x0AD0D0D0;
	inline u32 TimelineRangeSelectionBorderColor = 0x60E0E0E0;
	inline u32 TimelineRangeSelectionHeaderHighlightColor = 0x6075AD85;
	inline u32 TimelineSelectedNoteBoxBackgroundColor = 0x0AD0D0D0;
	inline u32 TimelineSelectedNoteBoxBorderColor = 0x60E0E0E0;

	inline u32 TimelineDefaultLineColor = 0xDCFFFFFF;
	inline u32 TimelineTempoChangeLineColor = 0xDC314CD8;
	inline u32 TimelineSignatureChangeLineColor = 0xDC22BEE2;
	inline u32 TimelineScrollChangeLineColor = 0xDC6CA71E;
	inline u32 TimelineScrollChangeComplexLineColor = 0xDC1DA386;
	inline u32 TimelineBarLineChangeLineColor = 0xDCBE9E2C;
	inline u32 TimelineScrollTypeLineColor = 0xDCE2E222;
	inline u32 TimelineSuddenChangeLineColor = 0xDC2691FF;
	inline u32 TimelineSuddenChangeDelayMoveLineColor = 0xDC22BEE2;
	inline u32 TimelineSelectedItemLineColor = 0xDCFFFFFF;
	inline u32 TimelineSongDemoStartMarkerColorFill = 0x3B75AD85;
	inline u32 TimelineSongDemoStartMarkerColorBorder = 0xB375AD85;

	inline u32 GameLaneOutlineFocusedColor = 0xFF2E8F5E;
	inline u32 GameLaneBorderFocusedColor = 0xFF293730;
	inline u32 GameLaneBorderColor = 0xFF000000;
	inline u32 GameLaneBarLineColor = 0xBFDADADA;
	inline u32 GameLaneBranchStartBarLineColor = 0xFF00FFFF;
	inline u32 GameLaneContentBackgroundColor = 0xFF282828;
	inline u32 GameLaneContentBackgroundColorGogo = 0x3C3838FF;
	inline u32 GameLaneFooterBackgroundColor = 0xFF848484;
	inline u32 GameLaneHitCircleInnerFillColor = 0x32FFFFFF;
	inline u32 GameLaneHitCircleInnerOutlineColor = 0x72FFFFFF;
	inline u32 GameLaneHitCircleOuterOutlineColor = 0x47FFFFFF;
	inline u32 GameLaneHitCircleInnerFillColorGogo = 0x923FFFFF;
	inline u32 GameLaneHitCircleInnerOutlineColorGogo = 0xD23FFFFF;
	inline u32 GameLaneHitCircleOuterOutlineColorGogo = 0xA73FFFFF;

	inline u32 NoteColorRed = 0xFF2B41F3;
	inline u32 NoteColorBlue = 0xFFC2C351;
	inline u32 NoteColorOrange = 0xFF0078FD;
	inline u32 NoteColorPurple = 0xFF780078;
	inline u32 NoteColorYellow = 0xFF13B7F4;
	inline u32 NoteColorWhite = 0xFFDEEDF5;
	inline u32 NoteColorBlack = 0xFF1E1E1E;
	inline u32 NoteColorDrumrollHit = 0xFF0000F6;
	inline u32 NoteBalloonTextColor = 0xFF000000;
	inline u32 NoteBalloonTextColorShadow = 0xFFFFFFFF;
	inline u32 NoteComboTextColor = 0xFFFFFFFF;
	inline u32 NoteComboTextColorShadow = 0xFF1E1E1E;
	inline u32 NoteComboTextColorNotCombo = 0x7FFFFFFF;
	inline u32 NoteComboTextColorShadowNotCombo = 0x7F1E1E1E;
	inline u32* NoteTypeToColorMap[] =
	{
		&NoteColorRed,		// Don
		&NoteColorRed,		// DonBig
		&NoteColorBlue,		// Ka
		&NoteColorBlue,		// KaBig
		&NoteColorYellow,	// Start_Drumroll
		&NoteColorYellow,	// Start_DrumrollBig
		&NoteColorOrange,	// Start_Balloon
		&NoteColorOrange,	// Start_SpecialBaloon
		&NoteColorRed,		// DonBigHand
		&NoteColorBlue,		// KaBigHand
		&NoteColorPurple,	// KaDon
		&NoteColorBlack,	// Bomb
		&NoteColorWhite,	// Adlib
		&NoteColorPurple,	// Fuse
	};
	static_assert(ArrayCount(NoteTypeToColorMap) == EnumCount<NoteType>);

	constexpr void GuiStyleColorPeepoDrumKit(ImGuiStyle* destination = &Gui::GetStyle())
	{
		ImVec4* colors = destination->Colors;
		colors[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		colors[ImGuiCol_Border] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.21f, 0.21f, 0.21f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.27f, 0.27f, 0.64f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 0.54f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.53f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.56f, 0.18f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.52f, 0.18f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.33f, 0.49f, 0.17f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.45f, 0.45f, 0.45f, 0.40f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.59f, 0.59f, 0.59f, 0.40f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.36f, 0.36f, 0.40f);
		colors[ImGuiCol_Header] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.49f, 0.49f, 0.49f, 0.50f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.32f, 0.49f, 0.21f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.39f, 0.67f, 0.18f, 0.78f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.44f, 0.44f, 0.44f, 0.20f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.39f, 0.60f, 0.24f, 0.20f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.43f, 0.65f, 0.27f, 0.20f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.25f, 0.18f, 1.00f);
		colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabSelected] = ImVec4(0.30f, 0.41f, 0.16f, 1.00f);
		colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.52f, 0.71f, 0.26f, 1.00f);
		colors[ImGuiCol_TabDimmed] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
		colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.52f, 0.68f, 0.46f, 0.70f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.34f, 0.45f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.35f, 0.49f, 0.18f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.34f, 0.45f, 0.20f, 1.00f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.31f, 0.78f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.23f, 0.78f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		colors[ImGuiCol_TextLink] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.76f, 0.76f, 0.76f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(0.50f, 0.75f, 0.21f, 1.00f);
		colors[ImGuiCol_NavCursor] = ImVec4(0.50f, 0.75f, 0.21f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.50f);
	}

	inline u32 GetEditorThemeAccentColor(i32 accent, u32 customColor)
	{
		static constexpr u32 presets[] = { 0, 0xFF66B34D, 0xFFFA963F, 0xFFD1B22A, 0xFFE084B0, 0xFF3DA6ED, 0xFFB075ED };
		return accent > 0 && accent < ArrayCountI32(presets) ? presets[accent] : customColor;
	}

	inline void ApplyEditorAccentColor(ImGuiStyle& style, u32 accentColor)
	{
		const ImVec4 accent = Gui::ColorConvertU32ToFloat4(accentColor);
		const ImVec4 reference = style.Colors[ImGuiCol_SliderGrab];
		f32 hue, saturation, value, referenceHue, referenceSaturation, referenceValue;
		Gui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation, value);
		Gui::ColorConvertRGBtoHSV(reference.x, reference.y, reference.z, referenceHue, referenceSaturation, referenceValue);
		const auto recolor = [&](ImVec4 color, b8 surface, b8 tintNeutral = false)
		{
			f32 originalHue, originalSaturation, originalValue;
			Gui::ColorConvertRGBtoHSV(color.x, color.y, color.z, originalHue, originalSaturation, originalValue);
			if (originalSaturation < 0.01f)
			{
				if (!tintNeutral) return color;
				originalSaturation = referenceSaturation * 0.5f;
			}
			const f32 newSaturation = Clamp(originalSaturation * saturation / Max(referenceSaturation, 0.01f), 0.0f, 1.0f);
			const f32 newValue = Clamp(originalValue * value / Max(referenceValue, 0.01f), 0.0f, surface ? originalValue : 1.0f);
			Gui::ColorConvertHSVtoRGB(hue, newSaturation, newValue, color.x, color.y, color.z);
			return color;
		};
		const ImGuiCol accentSlots[] =
		{
			ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive,
			ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_TitleBgActive,
			ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
			ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive,
			ImGuiCol_ResizeGrip, ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive,
			ImGuiCol_Tab, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabSelectedOverline,
			ImGuiCol_DockingPreview, ImGuiCol_PlotLinesHovered, ImGuiCol_PlotHistogram, ImGuiCol_PlotHistogramHovered,
			ImGuiCol_TextLink, ImGuiCol_TextSelectedBg, ImGuiCol_DragDropTarget, ImGuiCol_NavCursor,
		};
		for (ImGuiCol slot : accentSlots)
		{
			const b8 foreground = slot == ImGuiCol_CheckMark || slot == ImGuiCol_SliderGrab || slot == ImGuiCol_SliderGrabActive
				|| slot == ImGuiCol_TabSelectedOverline || slot == ImGuiCol_PlotLinesHovered || slot == ImGuiCol_PlotHistogram
				|| slot == ImGuiCol_PlotHistogramHovered || slot == ImGuiCol_TextLink || slot == ImGuiCol_DragDropTarget || slot == ImGuiCol_NavCursor;
			const b8 tintNeutral = slot == ImGuiCol_Header || slot == ImGuiCol_HeaderHovered || slot == ImGuiCol_HeaderActive;
			style.Colors[slot] = recolor(style.Colors[slot], !foreground, tintNeutral);
		}
		u32* timelineAccents[] =
		{
			&DragTextHoveredColor, &DragTextActiveColor, &TimelineCursorColor,
			&TimelineGoGoBackgroundColorBorderSelected, &TimelineLyricsBackgroundColorBorderSelected, &TimelineJPOSScrollBackgroundColorBorderSelected,
			&TimelineBoxSelectionBackgroundColor, &TimelineBoxSelectionBorderColor, &TimelineBoxSelectionFillColor,
			&TimelineRangeSelectionBackgroundColor, &TimelineRangeSelectionBorderColor, &TimelineRangeSelectionHeaderHighlightColor,
			&TimelineSelectedNoteBoxBackgroundColor, &TimelineSelectedNoteBoxBorderColor,
			&TimelineSongDemoStartMarkerColorFill, &TimelineSongDemoStartMarkerColorBorder,
		};
		for (u32* color : timelineAccents)
			*color = Gui::ColorConvertFloat4ToU32(recolor(Gui::ColorConvertU32ToFloat4(*color), color == &TimelineBoxSelectionFillColor));
	}

	inline void ApplyEditorColorTheme(i32 theme, ImGuiStyle* destination = &Gui::GetStyle(), i32 accent = 0, u32 accentColor = 0xFFFA963F)
	{
		struct ThemeColor { u32* Target; u32 Default, Dark, Light; };
		static const ThemeColor colors[] =
		{
			{ &DragTextHoveredColor, DragTextHoveredColor, 0xFFFFC18A, 0xFFA76020 },
			{ &DragTextActiveColor, DragTextActiveColor, 0xFFFFD6AC, 0xFF804510 },
			{ &InputTextWarningTextColor, InputTextWarningTextColor, 0xFF7070FF, 0xFF2525C0 },
			{ &TimelineBackgroundColor, TimelineBackgroundColor, 0xFF241B15, 0xFFF5F2F0 },
			{ &TimelineOutOfBoundsDimColor, TimelineOutOfBoundsDimColor, 0x73201812, 0x608C8073 },
			{ &TimelineWaveformBaseColor, TimelineWaveformBaseColor, 0x809E8A77, 0x80806D5A },
			{ &TimelineCursorColor, TimelineCursorColor, 0xCCFF9E42, 0xCCA75D20 },
			{ &TimelineItemTextColor, TimelineItemTextColor, 0xFFF5EDE5, 0xFF423224 },
			{ &TimelineItemTextColorShadow, TimelineItemTextColorShadow, 0xFF100C08, 0x80FFFFFF },
			{ &TimelineItemTextColorWarning, TimelineItemTextColorWarning, 0xFF7070FF, 0xFF2525C0 },
			{ &TimelineItemTextUnderlineColor, TimelineItemTextUnderlineColor, 0xFFF5EDE5, 0xFF423224 },
			{ &TimelineItemTextUnderlineColorShadow, TimelineItemTextUnderlineColorShadow, 0xFF100C08, 0x80FFFFFF },
			{ &TimelineGoGoBackgroundColorBorder, TimelineGoGoBackgroundColorBorder, 0xFF16100D, 0xFF9B8974 },
			{ &TimelineGoGoBackgroundColorBorderSelected, TimelineGoGoBackgroundColorBorderSelected, 0xFFFF9E42, 0xFFA75D20 },
			{ &TimelineGoGoBackgroundColorOuter, TimelineGoGoBackgroundColorOuter, 0xFF514334, 0xFFDCDFE8 },
			{ &TimelineGoGoBackgroundColorInner, TimelineGoGoBackgroundColorInner, 0xFF32291F, 0xFFF0F2FA },
			{ &TimelineLyricsTextColor, TimelineLyricsTextColor, 0xFFF5EDE5, 0xFF423224 },
			{ &TimelineLyricsTextColorShadow, TimelineLyricsTextColorShadow, 0xFF100C08, 0x80FFFFFF },
			{ &TimelineLyricsBackgroundColorBorder, TimelineLyricsBackgroundColorBorder, 0xFF16100D, 0xFF9B8974 },
			{ &TimelineLyricsBackgroundColorBorderSelected, TimelineLyricsBackgroundColorBorderSelected, 0xFFFF9E42, 0xFFA75D20 },
			{ &TimelineLyricsBackgroundColorOuter, TimelineLyricsBackgroundColorOuter, 0xFF514334, 0xFFE8DFD5 },
			{ &TimelineLyricsBackgroundColorInner, TimelineLyricsBackgroundColorInner, 0xFF382E24, 0xFFFAF4ED },
			{ &TimelineJPOSScrollBackgroundColorBorder, TimelineJPOSScrollBackgroundColorBorder, 0xB016100D, 0xB09B8974 },
			{ &TimelineJPOSScrollBackgroundColorBorderSelected, TimelineJPOSScrollBackgroundColorBorderSelected, 0xFFFF9E42, 0xFFA75D20 },
			{ &TimelineJPOSScrollBackgroundColorOuter, TimelineJPOSScrollBackgroundColorOuter, 0xFF663F8F, 0xFFE8CEDE },
			{ &TimelineJPOSScrollBackgroundColorInner, TimelineJPOSScrollBackgroundColorInner, 0xFF382C34, 0xFFF8EBF2 },
			{ &TimelineHorizontalRowLineColor, TimelineHorizontalRowLineColor, 0x408A7868, 0x405F5144 },
			{ &TimelineGridBarLineColor, TimelineGridBarLineColor, 0xA09E8A77, 0xA06B5948 },
			{ &TimelineGridBeatLineColor, TimelineGridBeatLineColor, 0x408A7868, 0x405F5144 },
			{ &TimelineGridSnapLineColor, TimelineGridSnapLineColor, 0x308A7868, 0x305F5144 },
			{ &TimelineGridSnapTupletLineColor, TimelineGridSnapTupletLineColor, 0x3022BBBC, 0x602E8888 },
			{ &TimelineGridSnapQuintupletLineColor, TimelineGridSnapQuintupletLineColor, 0x30B3BC22, 0x60888022 },
			{ &TimelineGridSnapSeptupletLineColor, TimelineGridSnapSeptupletLineColor, 0x30FF97FF, 0x60974997 },
			{ &TimelineBoxSelectionBackgroundColor, TimelineBoxSelectionBackgroundColor, 0x26FF9E42, 0x26A75D20 },
			{ &TimelineBoxSelectionBorderColor, TimelineBoxSelectionBorderColor, 0xFFFF9E42, 0xFFA75D20 },
			{ &TimelineBoxSelectionFillColor, TimelineBoxSelectionFillColor, 0xFF493729, 0xFFE8D5C3 },
			{ &TimelineBoxSelectionInnerColor, TimelineBoxSelectionInnerColor, 0xFFF5EDE5, 0xFF423224 },
			{ &TimelineRangeSelectionBackgroundColor, TimelineRangeSelectionBackgroundColor, 0x12FF9E42, 0x18A75D20 },
			{ &TimelineRangeSelectionBorderColor, TimelineRangeSelectionBorderColor, 0x80FF9E42, 0x80A75D20 },
			{ &TimelineRangeSelectionHeaderHighlightColor, TimelineRangeSelectionHeaderHighlightColor, 0x60FF9E42, 0x60A75D20 },
			{ &TimelineSelectedNoteBoxBackgroundColor, TimelineSelectedNoteBoxBackgroundColor, 0x12FF9E42, 0x18A75D20 },
			{ &TimelineSelectedNoteBoxBorderColor, TimelineSelectedNoteBoxBorderColor, 0x80FF9E42, 0x80A75D20 },
			{ &TimelineDefaultLineColor, TimelineDefaultLineColor, 0xDCF5EDE5, 0xDC423224 },
			{ &TimelineSelectedItemLineColor, TimelineSelectedItemLineColor, 0xDCF5EDE5, 0xDC423224 },
			{ &TimelineSongDemoStartMarkerColorFill, TimelineSongDemoStartMarkerColorFill, 0x3BFF9E42, 0x3BA75D20 },
			{ &TimelineSongDemoStartMarkerColorBorder, TimelineSongDemoStartMarkerColorBorder, 0xB3FF9E42, 0xB3A75D20 },
		};
		for (const auto& color : colors)
			*color.Target = theme == 1 ? color.Dark : theme == 2 ? color.Light : color.Default;
		if (theme == 2) Gui::StyleColorsLight(destination);
		else
		{
			Gui::StyleColorsDark(destination);
			if (theme != 1) GuiStyleColorPeepoDrumKit(destination);
		}
		if (accent > 0) ApplyEditorAccentColor(*destination, GetEditorThemeAccentColor(accent, accentColor));
	}

	struct EditorColorThemeState
	{
		i32 AppliedTheme = -1;
		i32 AppliedAccent = -1;
		u32 AppliedAccentColor = 0;
		f32 AppliedGuiScale = 0.0f;
		b8 Update(i32 theme, f32 guiScale, ImGuiStyle* destination = &Gui::GetStyle(), i32 accent = 0, u32 accentColor = 0xFFFA963F)
		{
			theme = Clamp(theme, 0, 2);
			accent = Clamp(accent, 0, 7);
			if (theme == AppliedTheme && guiScale == AppliedGuiScale && accent == AppliedAccent && accentColor == AppliedAccentColor) return false;
			ApplyEditorColorTheme(theme, destination, accent, accentColor);
			AppliedTheme = theme;
			AppliedAccent = accent;
			AppliedAccentColor = accentColor;
			AppliedGuiScale = guiScale;
			return true;
		}
	};
}
