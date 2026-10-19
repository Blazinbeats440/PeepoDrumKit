#include "chart_editor_settings_gui.h"

#include "core_io.h"
#include "core_build_info.h"

namespace PeepoDrumKit
{
	void ChartSettingsSectionNavigation::RegisterSection(std::string_view section)
	{
		if (!section.empty() && std::find(Sections.begin(), Sections.end(), section) == Sections.end())
			Sections.emplace_back(section);
	}

	void ChartSettingsSectionNavigation::ScrollToSection(std::string_view section)
	{
		if (!section.empty() && RequestedSection == section)
		{
			Gui::SetScrollHereY(0.0f);
			RequestedSection.clear();
		}
	}

	void ChartSettingsSectionNavigation::DrawHeading(std::string_view section)
	{
		RegisterSection(section);
		Gui::SeparatorText(section.data());
		ScrollToSection(section);
	}

	void ChartSettingsWindow::SelectSection(std::string_view section)
	{
		sectionNavigation.RequestedSection = sectionNavigation.SelectedSection = section;
		if (selectedCategory == ChartSettingsCategory::Keys) settingsFilterInput.Clear();
		else settingsFilterMain.Clear();
		lastActiveGroup = {};
	}

	struct CategoryTextPair { std::string_view Category, Text; };
	static CategoryTextPair GuiSplitCategoryText(std::string_view textWithPrefix)
	{
		if (size_t categorySeparatorIndex = textWithPrefix.find_first_of(':'); categorySeparatorIndex != std::string_view::npos)
		{
			if ((categorySeparatorIndex + 1) < textWithPrefix.size() && textWithPrefix[categorySeparatorIndex + 1] == ' ')
				categorySeparatorIndex++;
			return { textWithPrefix.substr(0, categorySeparatorIndex + 1), textWithPrefix.substr(categorySeparatorIndex + 1) };
		}
		else
			return { "", textWithPrefix };
	}

	static void GuiTextWithCategoryHighlight(std::string_view text)
	{
		auto split = GuiSplitCategoryText(text);
		if (!split.Category.empty())
		{
			Gui::PushStyleColor(ImGuiCol_Text, Gui::GetColorU32(ImGuiCol_Text, 0.8f));
			Gui::TextUnformatted(split.Category);
			Gui::PopStyleColor();
			Gui::SameLine(0.0f, 0.0f);
		}
		if (!split.Text.empty())
			Gui::TextUnformatted(split.Text);
	}

	static void GuiRightAlignedTextDisabled(std::string_view text)
	{
		Gui::SetCursorScreenPos(vec2(Gui::GetCursorScreenPos()) + vec2(Gui::GetContentRegionAvail().x - Gui::CalcTextSize(text).x, 0.0f));
		Gui::PushStyleColor(ImGuiCol_Text, Gui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		Gui::TextUnformatted(text.data());
		Gui::PopStyleColor();
	}

	struct BoolLabels { cstr True = "True", False = "False"; inline cstr operator[](b8 v) const { return v ? True : False; } };
	static b8 GuiBoolCombo(cstr label, b8* inOutValue, b8 defaultValue, BoolLabels boolLabels = {})
	{
		b8 changesWereMade = false;
		if (Gui::BeginCombo(label, boolLabels[*inOutValue], ImGuiComboFlags_None))
		{
			for (i32 i = 0; i <= 1; i++)
			{
				const b8 it = (i == 0);
				const b8 isSelected = (*inOutValue == it);
				if (Gui::Selectable(boolLabels[it], isSelected)) { *inOutValue = it; changesWereMade = true; }
				if (isSelected) { Gui::SetItemDefaultFocus(); }
				if (it == defaultValue) { Gui::SameLine(); GuiRightAlignedTextDisabled(UI_Str("SETTINGS_VALUE_DEFAULT")); }
			}
			Gui::EndCombo();
		}
		return changesWereMade;
	}

	static b8 GuiBoolCombo(cstr label, WithDefault<b8>* inOutValue, BoolLabels boolLabels = {})
	{
		return GuiBoolCombo(label, &inOutValue->Value, inOutValue->Default, boolLabels);
	}

	static b8 GuiAssignableInputBindingButton(InputBinding& inOutBinding, vec2 buttonSize, ImGuiButtonFlags buttonFlags, InputBinding*& inOutAwaitInputBinding, CPUStopwatch& inOutAwaitInputStopwatch)
	{
		static constexpr u32 activeTextColor = 0xFF3DEBD2;
		static constexpr Time timeoutThreshold = Time::FromSec(4.0);
		static constexpr Time mouseClickThreshold = Time::FromMS(200.0);
		static constexpr auto animateBlink = [&](Time elapsed)
		{
			constexpr f32 frequency = (timeoutThreshold.ToSec_F32() * 1.2f);
			constexpr f32 low = 0.25f, high = 1.45f, min = 0.25f, max = 1.00f;
			return Clamp(ConvertRange(-1.0f, +1.0f, low, high, ::sinf((elapsed.ToSec_F32() * frequency) + 1.0f)), min, max);
		};

		auto requestAssignment = [&](InputBinding* inOut)
		{
			inOutAwaitInputBinding = inOut;
			inOutAwaitInputStopwatch.Restart();
		};

		auto finishAsignment = [&](b8 hasNewBinding, InputBinding newBinding = {})
		{
			inOutAwaitInputBinding = nullptr;
			inOutAwaitInputStopwatch.Stop();
			if (hasNewBinding)
				inOutBinding = newBinding;
		};

		const InputBinding inBindingCopy = inOutBinding;
		InputFormatBuffer buffer = ToShortcutString(inOutBinding);

		if (buffer.Data[0] == '\0')
			strcpy_s(buffer.Data, UI_Str("SETTINGS_BINDING_NONE"));
		if (inOutAwaitInputBinding == &inOutBinding)
			strcpy_s(buffer.Data, UI_Str("SETTINGS_BINDING_PRESS_KEY"));
		strcat_s(buffer.Data, "###BindingButton");

		const b8 isBeingAsigned = (inOutAwaitInputBinding == &inOutBinding);
		Gui::PushStyleColor(ImGuiCol_Text, isBeingAsigned ? Gui::ColorU32WithAlpha(activeTextColor, animateBlink(inOutAwaitInputStopwatch.GetElapsed())) : Gui::GetColorU32(ImGuiCol_Text));
		Gui::PushStyleColor(ImGuiCol_Button, Gui::GetStyleColorVec4(isBeingAsigned ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
		if (Gui::ButtonEx(buffer.Data, buttonSize, buttonFlags))
		{
			if (inOutAwaitInputBinding != &inOutBinding)
				requestAssignment(&inOutBinding);
		}
		const b8 buttonHovered = Gui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		Gui::PopStyleColor(2);

		if (inOutAwaitInputBinding == &inOutBinding)
		{
			const b8 timedOut = (inOutAwaitInputStopwatch.GetElapsed() > timeoutThreshold);
			const b8 mouseClickCancelRequest = (inOutAwaitInputStopwatch.GetElapsed() > mouseClickThreshold && !buttonHovered && (Gui::IsMouseClicked(ImGuiMouseButton_Left, false) || Gui::IsMouseClicked(ImGuiMouseButton_Right, false)));

			if (timedOut || mouseClickCancelRequest)
				finishAsignment(false);

			Gui::SetActiveID(Gui::GetID(&inOutBinding), Gui::GetCurrentWindow());

			for (int keyCode = ImGuiKey_NamedKey_BEGIN; keyCode < ImGuiKey_NamedKey_END; keyCode++)
			{
				if (Gui::IsKeyReleased((ImGuiKey)keyCode) && !Gui::IsMouseKey((ImGuiKey)keyCode))
					finishAsignment(true, KeyBinding((ImGuiKey)keyCode, Gui::GetIO().KeyMods));
			}
		}

		return (inOutBinding != inBindingCopy);
	}

	namespace SettingsGui
	{
		enum class DataType : u32 { Invalid, B8, I32, I32Vector, F32, StdString, };
		template <typename T> constexpr DataType TemplateToDataType() = delete;
		template <> constexpr DataType TemplateToDataType<b8>() { return DataType::B8; }
		template <> constexpr DataType TemplateToDataType<i32>() { return DataType::I32; }
		template <> constexpr DataType TemplateToDataType<std::vector<i32>>() { return DataType::I32Vector; }
		template <> constexpr DataType TemplateToDataType<f32>() { return DataType::F32; }
		template <> constexpr DataType TemplateToDataType<std::string>() { return DataType::StdString; }

		enum class WidgetType : u32 {
			Default,
			B8_ChartSongSpaceComboBox, B8_ExclusiveAudioComboBox, B8_RecentFilesDisplay, StdString_ChartOpenDirectory, StdString_TaikojiroExecutable, I32_BarDivisionComboBox, F32_DrumrollRollsPerSecond, F32_BalloonExpectedHitsPerSecond, F32_AudioMasterVolume,
			F32_TimelineScrollSensitivity, F32_ExponentialSpeed, F32_SENoteOffset, I32_TJAFileSaveFormat, I32_VSyncOffFPSLimit,
			I32_AudioBufferFrameSize, I32_ScrollSpeedViewType, I32_TestPlayJudgementDisplayMode, I32_TestPlayPausedJudgementFilter, I32_GamePreviewMeasureNumbers, I32_GamePreviewLayout, I32_GamePreviewSizeMode, I32_ColorTheme, I32_ThemeAccent,
		};

		struct SettingsEntry
		{
			DataType DataType;
			WidgetType Widget;
			ChartSettingsCategory Category = ChartSettingsCategory::Count;
			std::string_view Section;
			void* ValuePtr;
			b8* HasValuePtr;
			std::string_view Header;
			std::string_view Description;
			std::string* InputTextValue;
			void(*ResetToDefaultFunc)(void* valuePtr);
			void(*SetHasValueIfNotDefaultFunc)(void* valuePtr);

			inline void ResetToDefault() { ResetToDefaultFunc(ValuePtr); }
			inline void SetHasValueIfNotDefault() { SetHasValueIfNotDefaultFunc(ValuePtr); }

			template <typename T>
			inline WithDefault<T>* ValueAs() { return (DataType == TemplateToDataType<T>()) ? static_cast<WithDefault<T>*>(ValuePtr) : nullptr; }

			template <typename T>
			SettingsEntry(WithDefault<T>& v, std::string_view header, std::string_view description, WidgetType widgetType = WidgetType::Default, std::string* inputTextValue = nullptr)
				: DataType(TemplateToDataType<T>()), Widget(widgetType), ValuePtr(&v), HasValuePtr(&v.HasValue), Header(header), Description(description), InputTextValue(inputTextValue)
			{
				ResetToDefaultFunc = [](void* valuePtr) { static_cast<WithDefault<T>*>(valuePtr)->ResetToDefault(); };
				SetHasValueIfNotDefaultFunc = [](void* valuePtr) { static_cast<WithDefault<T>*>(valuePtr)->SetHasValueIfNotDefault(); };
			}

			template <typename T>
			SettingsEntry(ChartSettingsCategory category, std::string_view section, WithDefault<T>& v, std::string_view header, std::string_view description, WidgetType widgetType = WidgetType::Default, std::string* inputTextValue = nullptr)
				: SettingsEntry(v, header, description, widgetType, inputTextValue)
			{
				Category = category;
				Section = section;
			}
		};

		static b8 DrawEntryWidgetGroupGui(SettingsEntry& in)
		{
			auto addGroupPadding = [](f32 padding) { Gui::InvisibleButton("", vec2(1.0f, 1.0f + padding)); };
			const auto& style = Gui::GetStyle();

			assert(in.DataType != DataType::Invalid && !in.Header.empty() && in.ValuePtr != nullptr);
			b8 changesWereMade = false;

			Gui::BeginGroup();
			addGroupPadding(GuiScale(2.0f));
			{
				Gui::AlignTextToFramePadding();
				Gui::PushFont(FontMain, GuiScaleI32_AtTarget(FontBaseSizes::Medium));
				if (in.Section.empty())
					GuiTextWithCategoryHighlight(in.Header);
				else
					Gui::TextUnformatted(GuiSplitCategoryText(in.Header).Text);
				Gui::PopFont();

				if (!in.Description.empty())
				{
					assert(in.Description.back() == '.' ||
						(in.Description.size() >= 3 && in.Description.substr(in.Description.size() - 3) == u8"。"));
					Gui::PushStyleColor(ImGuiCol_Text, Gui::GetColorU32(ImGuiCol_Text, 0.8f));
					Gui::PushTextWrapPos(ClampBot(Gui::GetCursorPosX() + Gui::GetContentRegionAvail().x, Gui::GetFrameHeight() * 12.0f));
					Gui::TextWrapped(in.Description.data());
					Gui::PopTextWrapPos();
					Gui::PopStyleColor();
				}

				auto* inOutB8 = in.ValueAs<b8>();
				auto* inOutI32 = in.ValueAs<i32>();
				auto* inOutI32Vector = in.ValueAs<std::vector<i32>>();
				auto* inOutF32 = in.ValueAs<f32>();
				auto* inOutStr = in.ValueAs<std::string>();

				if (inOutB8 != nullptr)
				{
					if (in.Widget == WidgetType::B8_ChartSongSpaceComboBox)
						changesWereMade |= GuiBoolCombo("##", inOutB8, { UI_Str("SETTINGS_SONG_SPACE"), UI_Str("SETTINGS_CHART_SPACE") });
					else if (in.Widget == WidgetType::B8_ExclusiveAudioComboBox)
						changesWereMade |= GuiBoolCombo("##", inOutB8, { UI_Str("SETTINGS_TRUE_EXCLUSIVE"), UI_Str("SETTINGS_FALSE") });
					else if (in.Widget == WidgetType::B8_RecentFilesDisplay)
						changesWereMade |= GuiBoolCombo("##", inOutB8, { UI_Str("SETTINGS_RECENT_FULL_PATH"), UI_Str("SETTINGS_RECENT_FILE_NAME") });
					else
						changesWereMade |= GuiBoolCombo("##", inOutB8);
				}
				else if (inOutI32Vector != nullptr)
				{
					assert(in.InputTextValue != nullptr);
					std::string formattedValue;
					for (size_t i = 0; i < inOutI32Vector->Value.size(); i++)
					{
						if (i > 0) formattedValue += ", ";
						formattedValue += std::to_string(inOutI32Vector->Value[i]);
					}

					const ImGuiID inputID = Gui::GetID("##");
					if (Gui::GetActiveID() != inputID)
						*in.InputTextValue = formattedValue;

					const f32 resetButtonWidth = Gui::CalcTextSize(UI_Str("SETTINGS_RESET_DEFAULT")).x + style.FramePadding.x * 2.0f;
					Gui::SetNextItemWidth(Max(1.0f, Gui::CalcItemWidth() - resetButtonWidth - style.ItemInnerSpacing.x));
					Gui::InputTextWithHint("##", "1, 2, 4, 8", in.InputTextValue);
					if (Gui::IsItemDeactivatedAfterEdit())
					{
						std::vector<i32> parsedDivisions;
						b8 isValid = true;
						ASCII::ForEachInCommaSeparatedList(*in.InputTextValue, [&](std::string_view part)
						{
							i32 division = 0;
							if (!ASCII::TryParse(ASCII::Trim(part), division) || division <= 0)
								isValid = false;
							else
								parsedDivisions.push_back(division);
						});

						if (isValid && !parsedDivisions.empty())
						{
							std::sort(parsedDivisions.begin(), parsedDivisions.end());
							parsedDivisions.erase(std::unique(parsedDivisions.begin(), parsedDivisions.end()), parsedDivisions.end());
							if (inOutI32Vector->Value != parsedDivisions)
							{
								inOutI32Vector->Value = std::move(parsedDivisions);
								changesWereMade = true;
							}
						}
					}

					Gui::SameLine(0.0f, style.ItemInnerSpacing.x);
					if (Gui::Button(UI_Str("SETTINGS_RESET_DEFAULT")))
					{
						in.ResetToDefault();
						*in.InputTextValue = formattedValue;
						changesWereMade = true;
					}
				}
				else if (inOutI32 != nullptr)
				{
					if (in.Widget == WidgetType::I32_BarDivisionComboBox)
					{
						char buffer[64]; sprintf_s(buffer, "Bar Division 1/%d", inOutI32->Value);
						if (Gui::BeginCombo("##", buffer, ImGuiComboFlags_None))
						{
							static constexpr i32 barDivisions[] = { 4, 8, 12, 16, 24, 32, 48, 64 };
							for (const i32 it : barDivisions)
							{
								sprintf_s(buffer, "Bar Division 1/%d", it);
								const b8 isSelected = (it == inOutI32->Value);
								if (Gui::Selectable(buffer, isSelected)) { inOutI32->Value = it; changesWereMade = true; }
								if (isSelected) { Gui::SetItemDefaultFocus(); }
								if (it == inOutI32->Default) { Gui::SameLine(); GuiRightAlignedTextDisabled(UI_Str("SETTINGS_VALUE_DEFAULT")); }
							}
							Gui::EndCombo();
						}
					}
					else if (in.Widget == WidgetType::I32_TJAFileSaveFormat)
					{
						static constexpr cstr formatNames[] = { "Current (UTF-8 BOM, LF)", "ANSI (Shift-JIS, CRLF)" };
						const i32 formatCount = static_cast<i32>(std::size(formatNames));
						inOutI32->Value = std::clamp(inOutI32->Value, 0, formatCount - 1);
						if (Gui::BeginCombo("##", formatNames[inOutI32->Value], ImGuiComboFlags_None))
						{
							for (i32 it = 0; it < formatCount; it++)
							{
								const b8 isSelected = (it == inOutI32->Value);
								if (Gui::Selectable(formatNames[it], isSelected)) { inOutI32->Value = it; changesWereMade = true; }
								if (isSelected) Gui::SetItemDefaultFocus();
							}
							Gui::EndCombo();
						}
					}
					else if (in.Widget == WidgetType::I32_AudioBufferFrameSize)
					{
						// see AudioTestWindow::AudioEngineTabContent()
						Gui::BeginDisabled();
						u32 currentBufferFrameCount = Audio::Engine.GetBufferFrameSize();
						Gui::SetNextItemWidth(-1.0f);
						Gui::InputScalar("##CurrentBufferSize", ImGuiDataType_U32, &currentBufferFrameCount, PtrArg<u32>(8), PtrArg<u32>(64), "%u Frames (Current)");
						Gui::EndDisabled();

						Gui::SetNextItemWidth(-1.0f);
						changesWereMade |= Gui::InputScalar("##NewBufferSize", ImGuiDataType_U32, &inOutI32->Value, PtrArg<u32>(8), PtrArg<u32>(64), "%u Frames (Request)");
						if (changesWereMade)
							inOutI32->Value = std::clamp(inOutI32->Value, 0, i32{ Audio::Engine.MaxBufferFrameCount });
					}
					else if (in.Widget == WidgetType::I32_ThemeAccent)
					{
						const cstr accents[] = { UI_Str("SETTINGS_ACCENT_STANDARD"), UI_Str("SETTINGS_ACCENT_GREEN"), UI_Str("SETTINGS_ACCENT_BLUE"),
							UI_Str("SETTINGS_ACCENT_CYAN"), UI_Str("SETTINGS_ACCENT_PURPLE"), UI_Str("SETTINGS_ACCENT_ORANGE"),
							UI_Str("SETTINGS_ACCENT_PINK"), UI_Str("SETTINGS_ACCENT_CUSTOM") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 7);
						changesWereMade |= Gui::Combo("##", &inOutI32->Value, accents, ArrayCountI32(accents));
					}
					else if (in.Widget == WidgetType::I32_ColorTheme)
					{
						const cstr themes[] = { UI_Str("SETTINGS_THEME_DEFAULT"), UI_Str("SETTINGS_THEME_DARK"), UI_Str("SETTINGS_THEME_LIGHT") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 2);
						changesWereMade |= Gui::Combo("##", &inOutI32->Value, themes, ArrayCountI32(themes));
					}
					else if (in.Widget == WidgetType::I32_GamePreviewLayout)
					{
						const cstr layouts[] = { UI_Str("VIDEO_EXPORT_LAYOUT_ORIGINAL"), UI_Str("VIDEO_EXPORT_LAYOUT_TAIKO"), UI_Str("VIDEO_EXPORT_LAYOUT_JIRO") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 2);
						changesWereMade |= Gui::Combo("##", &inOutI32->Value, layouts, ArrayCountI32(layouts));
					}
					else if (in.Widget == WidgetType::I32_GamePreviewSizeMode)
					{
						const cstr modes[] = { UI_Str("SETTINGS_PREVIEW_SIZE_KEEP_ASPECT"), UI_Str("SETTINGS_PREVIEW_SIZE_MAXIMIZE") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 1);
						changesWereMade |= Gui::Combo("##", &inOutI32->Value, modes, ArrayCountI32(modes));
					}
					else if (in.Widget == WidgetType::I32_GamePreviewMeasureNumbers)
					{
						const cstr displayModes[] = { UI_Str("SETTINGS_PREVIEW_MEASURE_NUMBERS_HIDDEN"), UI_Str("SETTINGS_PREVIEW_MEASURE_NUMBERS_VISIBLE"), UI_Str("SETTINGS_PREVIEW_MEASURE_NUMBERS_ALL") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 2);
						if (Gui::BeginCombo("##", displayModes[inOutI32->Value]))
						{
							const i32 displayOrder[] = { 1, 0, 2 };
							for (i32 mode : displayOrder)
							{
								const b8 isSelected = (mode == inOutI32->Value);
								if (Gui::Selectable(displayModes[mode], isSelected)) { inOutI32->Value = mode; changesWereMade = true; }
								if (isSelected) Gui::SetItemDefaultFocus();
							}
							Gui::EndCombo();
						}
					}
					else if (in.Widget == WidgetType::I32_ScrollSpeedViewType)
					{
						inOutI32->Value = std::clamp(inOutI32->Value, 0, static_cast<i32>(EnumCount<EScrollSpeedViewType>) - 1);
						if (Gui::BeginCombo("##", strScrollSpeedViewType[inOutI32->Value], ImGuiComboFlags_None))
						{
							for (i32 it = 0; it < static_cast<i32>(EnumCount<EScrollSpeedViewType>); it++)
							{
								const b8 isSelected = (it == inOutI32->Value);
								if (Gui::Selectable(strScrollSpeedViewType[it], isSelected)) { inOutI32->Value = it; changesWereMade = true; }
								if (isSelected) Gui::SetItemDefaultFocus();
							}
							Gui::EndCombo();
						}
					}
					else if (in.Widget == WidgetType::I32_TestPlayJudgementDisplayMode)
					{
						const cstr displayModes[] = { UI_Str("SETTINGS_TEST_PLAY_DISPLAY_JUDGEMENT"), UI_Str("SETTINGS_TEST_PLAY_DISPLAY_FAST_SLOW"), UI_Str("SETTINGS_TEST_PLAY_DISPLAY_MS"), UI_Str("SETTINGS_TEST_PLAY_DISPLAY_COMPACT_MS") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 3);
						if (Gui::BeginCombo("##", displayModes[inOutI32->Value]))
						{
							for (i32 mode = 0; mode < 4; mode++)
							{
								const b8 isSelected = (mode == inOutI32->Value);
								if (Gui::Selectable(displayModes[mode], isSelected)) { inOutI32->Value = mode; changesWereMade = true; }
								if (isSelected) Gui::SetItemDefaultFocus();
							}
							Gui::EndCombo();
						}
					}
					else if (in.Widget == WidgetType::I32_TestPlayPausedJudgementFilter)
					{
						const cstr filters[] = { UI_Str("TEST_PLAY_FILTER_ALL"), UI_Str("TEST_PLAY_FILTER_OK_BAD"), UI_Str("TEST_PLAY_FILTER_TIMING_THRESHOLD") };
						inOutI32->Value = Clamp(inOutI32->Value, 0, 2);
						if (Gui::BeginCombo("##", filters[inOutI32->Value]))
						{
							for (i32 filter = 0; filter < 3; filter++)
								if (Gui::Selectable(filters[filter], filter == inOutI32->Value)) { inOutI32->Value = filter; changesWereMade = true; }
							Gui::EndCombo();
						}
					}
					else
					{
						changesWereMade |= Gui::InputInt("##", &inOutI32->Value, 1, 10);
						if (in.Widget == WidgetType::I32_VSyncOffFPSLimit)
							inOutI32->Value = Max(0, inOutI32->Value);
					}
				}
				else if (inOutF32 != nullptr)
				{
					if (in.Widget == WidgetType::F32_SENoteOffset)
					{
						changesWereMade |= Gui::InputFloat("##", &inOutF32->Value, 0.1f, 1.0f, "%.2f");
						if (!std::isfinite(inOutF32->Value)) inOutF32->ResetToDefault();
					}
					else if (in.Widget == WidgetType::F32_DrumrollRollsPerSecond)
					{
						Gui::SetNextItemWidth(-1.0f);
						changesWereMade |= Gui::InputFloat("##", &inOutF32->Value, 1.0f, 10.0f, "%.2f rolls/s");
						if (changesWereMade)
							inOutF32->Value = Clamp(inOutF32->Value, 0.1f, 100.0f);
					}
					else if (in.Widget == WidgetType::F32_BalloonExpectedHitsPerSecond)
					{
						Gui::SetNextItemWidth(-1.0f);
						changesWereMade |= Gui::InputFloat("##", &inOutF32->Value, 1.0f, 10.0f, "%.2f hits/s");
						if (changesWereMade)
							inOutF32->Value = Clamp(inOutF32->Value, 0.1f, 100.0f);
					}
					else if (in.Widget == WidgetType::F32_AudioMasterVolume)
					{
						f32 volumePercent = ToPercent(inOutF32->Value);
						Gui::SetNextItemWidth(-1.0f);
						if (Gui::SliderFloat("##", &volumePercent, ToPercent(Audio::AudioEngine::MinVolume), ToPercent(Audio::AudioEngine::MaxVolume), "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
						{
							inOutF32->Value = FromPercent(volumePercent);
							changesWereMade = true;
						}
					}
					else if (in.Widget == WidgetType::F32_TimelineScrollSensitivity)
					{
						Gui::SetNextItemWidth(Max(1.0f, Gui::CalcItemWidth() - ((Gui::GetFrameHeight() + style.ItemInnerSpacing.x) * 3)));
						changesWereMade |= Gui::SliderFloat("##", &inOutF32->Value, 50.0f, 350.0f, "%.0f", ImGuiSliderFlags_None);
						Gui::SameLine(0, style.ItemInnerSpacing.x);
						changesWereMade |= Gui::ButtonPlusMinusFloat('-', &inOutF32->Value, 1.0f, 10.0f);
						Gui::SameLine(0, style.ItemInnerSpacing.x);
						changesWereMade |= Gui::ButtonPlusMinusFloat('+', &inOutF32->Value, 1.0f, 10.0f);
						Gui::SameLine(0.0f, style.ItemInnerSpacing.x);
						if (Gui::BeginCombo("##Combo", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft))
						{
							if (Gui::Selectable(UI_Str("SETTINGS_RESET_DEFAULT"), false, !inOutF32->HasValue ? ImGuiSelectableFlags_Disabled : 0)) { in.ResetToDefault(); changesWereMade = true; }
							Gui::EndCombo();
						}
						if (changesWereMade) { inOutF32->Value = Clamp(inOutF32->Value, 50.0f, 5000.0f); }
					}
					else if (in.Widget == WidgetType::F32_ExponentialSpeed)
					{
						const b8 isDisabled = (inOutF32->Value <= 0.0f);
						Gui::PushStyleColor(ImGuiCol_Text, Gui::GetStyleColorVec4(isDisabled ? ImGuiCol_TextDisabled : ImGuiCol_Text));
						Gui::SetNextItemWidth(Max(1.0f, Gui::CalcItemWidth() - ((Gui::GetFrameHeight() + style.ItemInnerSpacing.x) * 3)));
						changesWereMade |= Gui::SliderFloat("##", &inOutF32->Value, 0.0f, 50.0f, isDisabled ? "(Disabled)" : "%.0f", ImGuiSliderFlags_None);
						Gui::PopStyleColor();
						Gui::SameLine(0, style.ItemInnerSpacing.x);
						changesWereMade |= Gui::ButtonPlusMinusFloat('-', &inOutF32->Value, 1.0f, 10.0f);
						Gui::SameLine(0, style.ItemInnerSpacing.x);
						changesWereMade |= Gui::ButtonPlusMinusFloat('+', &inOutF32->Value, 1.0f, 10.0f);
						Gui::SameLine(0.0f, style.ItemInnerSpacing.x);
						if (Gui::BeginCombo("##Combo", "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft))
						{
							if (Gui::Selectable(UI_Str("SETTINGS_RESET_DEFAULT"), false, !inOutF32->HasValue ? ImGuiSelectableFlags_Disabled : 0)) { in.ResetToDefault(); changesWereMade = true; }
							if (Gui::Selectable(UI_Str("SETTINGS_DISABLE_ANIMATION"), false, (inOutF32->Value == 0.0f) ? ImGuiSelectableFlags_Disabled : 0)) { inOutF32->Value = 0.0f; changesWereMade = true; }
							Gui::EndCombo();
						}
						if (changesWereMade) { inOutF32->Value = Clamp(inOutF32->Value, 0.0f, 1000.0f); }
					}
					else
					{
						changesWereMade |= Gui::InputFloat("##", &inOutF32->Value, 1.0f, 10.0f);
					}
				}
				else if (inOutStr != nullptr)
				{
					if (in.Widget == WidgetType::StdString_ChartOpenDirectory)
					{
						const std::string executableDirectory = Directory::GetExecutableDirectory();
						const char* browseLabel = UI_Str("SETTINGS_OPEN_FOLDER_BROWSE");
						const f32 browseWidth = Gui::CalcTextSize(browseLabel).x + style.FramePadding.x * 2.0f;
						Gui::SetNextItemWidth(Max(1.0f, Gui::CalcItemWidth() - browseWidth - style.ItemInnerSpacing.x));
						changesWereMade |= Gui::InputTextWithHint("##", executableDirectory.c_str(), &inOutStr->Value);
						Gui::SameLine(0.0f, style.ItemInnerSpacing.x);
						if (Gui::Button(browseLabel))
						{
							const std::string initialDirectory = GetChartOpenDirectory(inOutStr->Value);
							Shell::FileDialog folderDialog {};
							folderDialog.InTitle = UI_Str("SETTINGS_OPEN_FOLDER");
							folderDialog.InInitialDirectory = initialDirectory;
							folderDialog.InParentWindowHandle = ApplicationHost::GlobalState.NativeWindowHandle;
							if (folderDialog.OpenSelectFolder() == Shell::FileDialogResult::OK)
							{
								inOutStr->Value = Path::NormalizeInPlace(folderDialog.OutFilePath);
								changesWereMade = true;
							}
						}
					}
					else if (in.Widget == WidgetType::StdString_TaikojiroExecutable)
					{
						const char* browseLabel = UI_Str("SETTINGS_OPEN_FOLDER_BROWSE");
						const f32 browseWidth = Gui::CalcTextSize(browseLabel).x + style.FramePadding.x * 2.0f;
						Gui::SetNextItemWidth(Max(1.0f, Gui::CalcItemWidth() - browseWidth - style.ItemInnerSpacing.x));
						changesWereMade |= Gui::InputTextWithHint("##", "taikojiro.exe", &inOutStr->Value);
						Gui::SameLine(0.0f, style.ItemInnerSpacing.x);
						if (Gui::Button(browseLabel))
						{
							const std::string executablePath = GetTaikojiroExecutablePath(inOutStr->Value);
							const std::string initialDirectory = GetChartOpenDirectory(Path::GetDirectoryName(executablePath));
							Shell::FileDialog fileDialog {};
							fileDialog.InTitle = UI_Str("SETTINGS_TAIKOJIRO_EXECUTABLE");
							fileDialog.InInitialDirectory = initialDirectory;
							fileDialog.InFileName = Path::GetFileName(executablePath);
							fileDialog.InFilters = { { UI_Str("SETTINGS_EXECUTABLE_FILTER"), "*.exe" } };
							fileDialog.InDefaultExtension = ".exe";
							fileDialog.InParentWindowHandle = ApplicationHost::GlobalState.NativeWindowHandle;
							if (fileDialog.OpenRead() == Shell::FileDialogResult::OK)
							{
								inOutStr->Value = Path::NormalizeInPlace(fileDialog.OutFilePath);
								changesWereMade = true;
							}
						}
					}
					else
						changesWereMade |= Gui::InputTextWithHint("##", "n/a", &inOutStr->Value);
				}
				else
				{
					assert(false);
				}

				if (changesWereMade)
					in.SetHasValueIfNotDefault();
			}
			addGroupPadding(GuiScale(2.0f));
			Gui::EndGroup();

			return changesWereMade;
		}

		static b8 DrawEntriesListTableGui(SettingsEntry* entries, size_t entriesCount, ImGuiTextFilter* filter, ChartSettingsWindowTempActiveWidgetGroup& lastActiveGroup, ChartSettingsSectionNavigation& navigation, ChartSettingsCategory category = ChartSettingsCategory::Count)
		{
			const auto& style = Gui::GetStyle();
			b8 changesWereMade = false;

			Gui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { style.ItemSpacing.x, GuiScale(8.0f) });

			if (filter != nullptr)
			{
				Gui::SetNextItemWidth(-1.0f);
				if (Gui::InputTextWithHint("##Filter", UI_Str("SETTINGS_SEARCH_HINT"), filter->InputBuf, ArrayCount(filter->InputBuf)))
					filter->Build();
			}

			Gui::PushStyleVar(ImGuiStyleVar_CellPadding, GuiScale(vec2(8.0f, 4.0f)));
			const b8 beginTable = Gui::BeginTable("InnerTable", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersOuter);
			Gui::PopStyleVar(2);
			if (beginTable)
			{
				Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize(UI_Str("SETTINGS_VALUE_DEFAULT")).x + Gui::GetFrameHeight());

				Gui::PushStyleVar(ImGuiStyleVar_ItemSpacing, GuiScale(vec2(8.0f, 8.0f)));

				std::string_view lastSection;
				b8 isAnyItemGroupHoveredOrActive = false;
				for (size_t entryIndex = 0; entryIndex < entriesCount; entryIndex++)
				{
					SettingsEntry& entry = entries[entryIndex];
					if (category != ChartSettingsCategory::Count && entry.Category != category)
						continue;
					navigation.RegisterSection(entry.Section);
					if (filter != nullptr)
					{
						if (!filter->PassFilter(Gui::StringViewStart(entry.Header), Gui::StringViewEnd(entry.Header)) &&
							!filter->PassFilter(Gui::StringViewStart(entry.Description), Gui::StringViewEnd(entry.Description)))
							continue;
					}

					if (!entry.Section.empty() && entry.Section != lastSection)
					{
						Gui::TableNextRow();
						Gui::TableSetColumnIndex(1);
						navigation.DrawHeading(entry.Section);
						lastSection = entry.Section;
					}

					Gui::TableNextRow();
					Gui::PushID(entry.ValuePtr);

					Gui::TableSetColumnIndex(0);
					Gui::AlignTextToFramePadding();
					(*entry.HasValuePtr) ? Gui::TextUnformatted(UI_Str("SETTINGS_VALUE_USER")) : Gui::TextDisabled("%s", UI_Str("SETTINGS_VALUE_DEFAULT"));

					Gui::TableSetColumnIndex(1);

					if (lastActiveGroup.ValuePtr == entry.ValuePtr)
					{
						const vec2 padding = { GuiScale(12.0f), 0.0f };
						Gui::GetWindowDrawList()->AddRectFilled(
							lastActiveGroup.GroupRect.TL + vec2(-padding.x, 0.0f),
							lastActiveGroup.GroupRect.BR + vec2(+padding.x, +padding.y),
							Gui::GetColorU32(ImGuiCol_ButtonHovered, 0.15f));
					}

					if (DrawEntryWidgetGroupGui(entry))
						changesWereMade = true;

					const Rect itemGroupRect = Gui::GetItemRect();
					const b8 isItemGroupHovered = Gui::IsItemHovered();
					b8 isPopupOpen = false;

					if (isPopupOpen = Gui::BeginPopupContextItem("SettingsEntryContextMenu"))
					{
						GuiTextWithCategoryHighlight(entry.Header);
						Gui::Separator();
						if (Gui::MenuItem(UI_Str("SETTINGS_RESET_DEFAULT"), "", nullptr))
						{
							entry.ResetToDefault();
							changesWereMade = true;
						}

						Gui::EndPopup();
					}

					if (isItemGroupHovered || isPopupOpen)
					{
						isAnyItemGroupHoveredOrActive = true;
						lastActiveGroup = { entry.ValuePtr, itemGroupRect };
					}

					Gui::PopID();

					if (ArrayItToIndex(&entry, entries) + 1 < entriesCount)
						Gui::Separator();
				}

				if (!isAnyItemGroupHoveredOrActive)
					lastActiveGroup = {};

				Gui::PopStyleVar(1);
				Gui::EndTable();
			}

			return changesWereMade;
		}

		struct InputSettingsEntry
		{
			WithDefault<MultiInputBinding>* Binding;
			std::string_view Name;
			b8 IgnoreDuplicateHighlight = false;
		};

		static b8 HasDuplicateInputBinding(const InputSettingsEntry& entryToCheck, InputSettingsEntry* entries, size_t entriesCount)
		{
			if (entryToCheck.Binding == nullptr || entryToCheck.IgnoreDuplicateHighlight)
				return false;

			for (size_t otherEntryIndex = 0; otherEntryIndex < entriesCount; otherEntryIndex++)
			{
				const InputSettingsEntry& otherEntry = entries[otherEntryIndex];
				if (otherEntry.Binding == nullptr || otherEntry.Binding == entryToCheck.Binding || otherEntry.IgnoreDuplicateHighlight)
					continue;

				for (size_t bindingIndex = 0; bindingIndex < entryToCheck.Binding->Value.Count; bindingIndex++)
				{
					const InputBinding& binding = entryToCheck.Binding->Value.Slots[bindingIndex];
					if (binding.Type == InputBindingType::None)
						continue;

					for (size_t otherBindingIndex = 0; otherBindingIndex < otherEntry.Binding->Value.Count; otherBindingIndex++)
					{
						if (binding == otherEntry.Binding->Value.Slots[otherBindingIndex])
							return true;
					}
				}
			}

			return false;
		}

		static b8 DrawInputEntriesListTableGui(InputSettingsEntry* entries, size_t entriesCount, ImGuiTextFilter* filter, ChartSettingsWindowTempInputState& state, ChartSettingsSectionNavigation& navigation)
		{
			const auto& style = Gui::GetStyle();
			b8 changesWereMade = false;

			Gui::PushStyleVar(ImGuiStyleVar_CellPadding, { style.CellPadding.x, GuiScale(4.0f) });
			Gui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { style.ItemSpacing.x, GuiScale(8.0f) });
			Gui::PushStyleColor(ImGuiCol_Header, Gui::GetColorU32(ImGuiCol_Header, 0.5f));
			Gui::PushStyleColor(ImGuiCol_HeaderHovered, Gui::GetColorU32(ImGuiCol_HeaderHovered, 0.5f));
			defer { Gui::PopStyleColor(2); Gui::PopStyleVar(2); };

			// TODO: CTRL+F keybinding for all search fields and also match shortcut strings themselves (and draw in different color on match?)
			if (filter != nullptr)
			{
				Gui::SetNextItemWidth(-1.0f);
				if (Gui::InputTextWithHint("##Filter", UI_Str("SETTINGS_SEARCH_HINT"), filter->InputBuf, ArrayCount(filter->InputBuf)))
					filter->Build();
			}

			// TODO: Allow sorting table (?)
			b8 openMultiBindingPopupAtEndOfFrame = false;
			if (Gui::BeginTable("BindingsTable", 3, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, Gui::GetContentRegionAvail()))
			{
				Gui::UpdateSmoothScrollWindow();

				Gui::PushFont(FontMain, GuiScaleI32_AtTarget(FontBaseSizes::Medium));
				Gui::TableSetupScrollFreeze(0, 1);
				Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize(UI_Str("SETTINGS_VALUE_DEFAULT")).x + Gui::GetFrameHeight());
				Gui::TableSetupColumn(UI_Str("SETTINGS_ACTION"), ImGuiTableColumnFlags_None);
				Gui::TableSetupColumn(UI_Str("SETTINGS_BINDING"), ImGuiTableColumnFlags_None);
				Gui::TableHeadersRow();
				Gui::PopFont();

				Gui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, ConvertRangeClampInput(0.0f, 1.0f, 1.0f, style.DisabledAlpha, state.MultiBindingPopupFadeCurrent));
				Gui::BeginDisabled(state.MultiBindingPopupFadeTarget >= 0.001f);

				for (size_t entryIndex = 0; entryIndex < entriesCount; entryIndex++)
				{
					InputSettingsEntry& entry = entries[entryIndex];
					std::string_view section = GuiSplitCategoryText(entry.Name).Category;
					while (!section.empty() && (section.back() == ':' || section.back() == ' ')) section.remove_suffix(1);
					if (entry.Binding != nullptr) navigation.RegisterSection(section);
					if (filter != nullptr)
					{
						if (!filter->PassFilter(Gui::StringViewStart(entry.Name), Gui::StringViewEnd(entry.Name)))
							continue;

						if (entry.Binding == nullptr)
						{
							if (!filter->IsActive()) { Gui::TableNextRow(); Gui::TableSetColumnIndex(0); Gui::Selectable("##", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled); }
							continue;
						}
					}

					if (entry.Binding != nullptr)
						Gui::PushID(entry.Binding);
					else
						Gui::PushID(("##InputSettingsEmptyLine_" + std::to_string(entryIndex)).c_str());
					Gui::TableNextRow();
					if (HasDuplicateInputBinding(entry, entries, entriesCount))
						Gui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(190, 135, 25, 190));

					Gui::TableSetColumnIndex(0);
					(entry.Binding->HasValue) ? Gui::TextUnformatted(UI_Str("SETTINGS_VALUE_USER")) : Gui::TextDisabled("%s", UI_Str("SETTINGS_VALUE_DEFAULT"));

					Gui::TableSetColumnIndex(1);

					const b8 clicked = Gui::Selectable("##", false, ImGuiSelectableFlags_SpanAllColumns);
					if (Gui::BeginPopupContextItem("InputSettingsEntryContextMenu"))
					{
						GuiTextWithCategoryHighlight(entry.Name);
						Gui::Separator();
						if (Gui::MenuItem(UI_Str("SETTINGS_CLEAR_ALL"), "", nullptr)) { entry.Binding->Value.ClearAll(); entry.Binding->SetHasValueIfNotDefault(); changesWereMade = true; }
						if (Gui::MenuItem(UI_Str("SETTINGS_RESET_DEFAULT"), "", nullptr)) { entry.Binding->ResetToDefault(); changesWereMade = true; }
						Gui::EndPopup();
					}

					Gui::SameLine(0.0f, 0.0f);
					GuiTextWithCategoryHighlight(entry.Name);
					navigation.ScrollToSection(section);
					if (clicked) { state.SelectedMultiBinding = entry.Binding; memcpy(&state.SelectedMultiBindingOnOpenCopy, entry.Binding, sizeof(state.SelectedMultiBindingOnOpenCopy)); openMultiBindingPopupAtEndOfFrame = true; }

					Gui::TableSetColumnIndex(2);
					if (entry.Binding->Value.Count > 0)
					{
						for (size_t i = 0; i < entry.Binding->Value.Count; i++)
						{
							if (i > 0) { Gui::SameLine(0.0f, 0.0f); Gui::Text(" , "); Gui::SameLine(0.0f, 0.0f); }
							const auto& binding = entry.Binding->Value.Slots[i];

							Gui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, GuiScale(1.0f));
							Gui::PushStyleColor(ImGuiCol_Border, Gui::GetColorU32(ImGuiCol_ButtonActive));
							{
								Gui::SmallButton(ToShortcutString(binding).Data);
							}
							Gui::PopStyleColor();
							Gui::PopStyleVar();
						}
					}
					else
					{
						// NOTE: Use a chouonpu here instead of a regular minus for a more readable and thicker glyph at smaller font sizes
						Gui::TextDisabled(u8"ー"); // u8"ー" // "-" // "(None)"
					}

					Gui::PopID();
				}
				Gui::EndDisabled();
				Gui::PopStyleVar();

				Gui::EndTable();
			}

			Gui::AnimateExponential(&state.MultiBindingPopupFadeCurrent, state.MultiBindingPopupFadeTarget, 20.0f);
			if (openMultiBindingPopupAtEndOfFrame)
			{
				state.MultiBindingPopupFadeCurrent = state.MultiBindingPopupFadeTarget;
				state.MultiBindingPopupFadeTarget = 1.0f;
			}

			if (state.MultiBindingPopupFadeCurrent >= 0.001f)
			{
				cstr selectedBindingName = UI_Str("SETTINGS_BINDING_UNNAMED");
				for (size_t i = 0; i < entriesCount; i++)
				{
					if (entries[i].Binding == state.SelectedMultiBinding)
						selectedBindingName = entries[i].Name.data();
				}
				assert(state.SelectedMultiBinding != nullptr);

				const ImGuiWindowFlags allowInputWindowFlag = (state.MultiBindingPopupFadeCurrent < 0.9f) ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;

				Gui::PushStyleVar(ImGuiStyleVar_Alpha, state.MultiBindingPopupFadeCurrent);
				Gui::PushStyleVar(ImGuiStyleVar_WindowPadding, GuiScale(vec2(12.0f, 8.0f)));
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiScale(vec2(8.0f, 6.0f)));
				Gui::PushStyleColor(ImGuiCol_Border, Gui::GetStyleColorVec4(ImGuiCol_TableBorderStrong));
				Gui::SetNextWindowPos(Gui::GetMousePos(), ImGuiCond_Appearing, vec2(0.0f, 0.0f));
				Gui::PushFont(FontMain, GuiScaleI32_AtTarget(FontBaseSizes::Medium));
				Gui::Begin(selectedBindingName, nullptr, allowInputWindowFlag | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
				{
					if ((Gui::IsMouseClicked(ImGuiMouseButton_Left, false) && !Gui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) ||
						(Gui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && Gui::GetActiveID() == 0 && state.TempAssignedBinding == nullptr && Gui::IsAnyPressed(*Settings.Input.Dialog_Cancel, false)))
					{
						state.MultiBindingPopupFadeTarget = 0.0f;
						state.AssignedBindingStopwatch.Stop();
						state.TempAssignedBinding = nullptr;
					}

					GuiTextWithCategoryHighlight(selectedBindingName);

					if (Gui::BeginTable("SelectedBindingTable", 4, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
					{
						Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize("xxx").x * 9.0f + Gui::GetFrameHeight());
						Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize("xxx").x * 1.0f + Gui::GetFrameHeight());
						Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize("xxx").x * 1.0f + Gui::GetFrameHeight());
						Gui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Gui::CalcTextSize("xxx").x * 2.0f + Gui::GetFrameHeight());

						i32 indexToMoveUp = -1, indexToMoveDown = -1, indexToRemove = -1;
						for (i32 i = 0; i < state.SelectedMultiBinding->Value.Count; i++)
						{
							auto& it = state.SelectedMultiBinding->Value.Slots[i];
							Gui::PushID(&it);
							Gui::TableNextRow();
							{
								Gui::TableSetColumnIndex(0);
								if (GuiAssignableInputBindingButton(it, { Gui::GetContentRegionAvail().x, 0.0f }, ImGuiButtonFlags_None, state.TempAssignedBinding, state.AssignedBindingStopwatch))
								{
									state.SelectedMultiBinding->SetHasValueIfNotDefault();
									changesWereMade = true;
								}
							}
							Gui::PushStyleColor(ImGuiCol_Button, Gui::GetStyleColorVec4(ImGuiCol_Header));
							Gui::PushStyleColor(ImGuiCol_ButtonHovered, Gui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
							Gui::PushStyleColor(ImGuiCol_ButtonActive, Gui::GetStyleColorVec4(ImGuiCol_HeaderActive));
							{
								Gui::TableSetColumnIndex(1);
						if (Gui::Button(UI_Str("SETTINGS_UP"), { Gui::GetContentRegionAvail().x, 0.0f })) { indexToMoveUp = i; }

								Gui::TableSetColumnIndex(2);
						if (Gui::Button(UI_Str("SETTINGS_DOWN"), { Gui::GetContentRegionAvail().x, 0.0f })) { indexToMoveDown = i; }

								Gui::TableSetColumnIndex(3);
						if (Gui::Button(UI_Str("SETTINGS_REMOVE"), { Gui::GetContentRegionAvail().x, 0.0f })) { indexToRemove = i; }
							}
							Gui::PopStyleColor(3);

							Gui::PopID();
						}

						Gui::TableNextRow();
						{
							Gui::TableSetColumnIndex(0);
							Gui::BeginDisabled(state.SelectedMultiBinding->Value.Count >= MultiInputBinding::MaxCount);
							Gui::PushStyleColor(ImGuiCol_Button, Gui::GetStyleColorVec4(ImGuiCol_Header));
							Gui::PushStyleColor(ImGuiCol_ButtonHovered, Gui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
							Gui::PushStyleColor(ImGuiCol_ButtonActive, Gui::GetStyleColorVec4(ImGuiCol_HeaderActive));
							if (InputBinding newBinding {}; GuiAssignableInputBindingButton(newBinding, { Gui::GetContentRegionAvail().x, 0.0f }, ImGuiButtonFlags_None, state.TempAssignedBinding, state.AssignedBindingStopwatch))
							{
								state.SelectedMultiBinding->Value.Slots[state.SelectedMultiBinding->Value.Count++] = newBinding;
								state.SelectedMultiBinding->SetHasValueIfNotDefault();
								changesWereMade = true;
							}
							Gui::PopStyleColor(3);
							Gui::EndDisabled();
						}

						if (indexToMoveUp >= 0 || indexToMoveDown >= 0 || indexToRemove >= 0)
						{
							auto& inOutCount = state.SelectedMultiBinding->Value.Count;
							auto& inOutSlots = state.SelectedMultiBinding->Value.Slots;

							if (indexToMoveUp >= 1 && indexToMoveUp <= inOutCount)
							{
								std::swap(inOutSlots[indexToMoveUp], inOutSlots[indexToMoveUp - 1]);
								state.SelectedMultiBinding->SetHasValueIfNotDefault();
								changesWereMade = true;
							}
							else if (indexToMoveDown >= 0 && (indexToMoveDown + 1) < inOutCount)
							{
								std::swap(inOutSlots[indexToMoveDown], inOutSlots[indexToMoveDown + 1]);
								state.SelectedMultiBinding->SetHasValueIfNotDefault();
								changesWereMade = true;
							}
							else if (indexToRemove >= 0)
							{
								state.SelectedMultiBinding->Value.RemoveAt(indexToRemove);
								state.SelectedMultiBinding->SetHasValueIfNotDefault();
								changesWereMade = true;
							}
						}

						Gui::EndTable();
					}

					Gui::PushMultiItemsWidths(2, GuiScale(260.0f));
					{
						const b8 isSameAsOnOpen =
							(state.SelectedMultiBinding->HasValue == state.SelectedMultiBindingOnOpenCopy.HasValue) &&
							(state.SelectedMultiBinding->Value == state.SelectedMultiBindingOnOpenCopy.Value);

						Gui::BeginDisabled(isSameAsOnOpen);
						if (Gui::Button(UI_Str("SETTINGS_REVERT_CHANGES"), { Gui::CalcItemWidth(), 0.0f }))
						{
							state.SelectedMultiBinding->HasValue = state.SelectedMultiBindingOnOpenCopy.HasValue;
							state.SelectedMultiBinding->Value = state.SelectedMultiBindingOnOpenCopy.Value;
							changesWereMade = true;
						}
						Gui::EndDisabled();
						Gui::PopItemWidth();
						Gui::SameLine();

						Gui::BeginDisabled(!state.SelectedMultiBinding->HasValue);
						if (Gui::Button(UI_Str("SETTINGS_RESET_DEFAULT"), { Gui::CalcItemWidth(), 0.0f }))
						{
							state.SelectedMultiBinding->ResetToDefault();
							changesWereMade = true;
						}
						Gui::EndDisabled();
						Gui::PopItemWidth();
					}
				}
				Gui::End();
				Gui::PopFont();
				Gui::PopStyleColor(1);
				Gui::PopStyleVar(3);
			}

			return changesWereMade;
		}
	}

	b8 ChartSettingsWindow::DrawGui(ChartContext& context, UserSettingsData& settings)
	{
		b8 changesWereMade = false;
		if (Gui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && inputState.TempAssignedBinding == nullptr &&
			Gui::GetIO().KeyMods == ImGuiMod_CtrlShiftAlt && Gui::IsKeyPressed(ImGuiKey_D, false))
		{
			showDeveloperSettings = !showDeveloperSettings;
			if (showDeveloperSettings || selectedCategory == ChartSettingsCategory::Developer)
			{
				SelectCategory(showDeveloperSettings ? ChartSettingsCategory::Developer : ChartSettingsCategory::General);
				lastActiveGroup = {};
				inputState.SelectedMultiBinding = nullptr;
				inputState.MultiBindingPopupFadeCurrent = inputState.MultiBindingPopupFadeTarget = 0.0f;
				testPlayCalibrationActive = false;
			}
		}
		if (!showDeveloperSettings && selectedCategory == ChartSettingsCategory::Developer)
			SelectCategory(ChartSettingsCategory::General);

		const ImVec2 originalFramePadding = Gui::GetStyle().FramePadding;
		cstr categoryLabels[] =
		{
			UI_Str("SETTINGS_CATEGORY_GENERAL"),
			UI_Str("SETTINGS_CATEGORY_FILE"),
			UI_Str("SETTINGS_CATEGORY_TIMELINE"),
			UI_Str("SETTINGS_CATEGORY_EDITOR"),
			UI_Str("SETTINGS_TAB_AUDIO"),
			UI_Str("SETTINGS_TAB_TEST_PLAY"),
			UI_Str("SETTINGS_CATEGORY_APPEARANCE"),
			UI_Str("SETTINGS_CATEGORY_KEYS"),
			UI_Str("SETTINGS_CATEGORY_DEVELOPER"),
			UI_Str("SETTINGS_CATEGORY_ABOUT"),
		};
		static_assert(ArrayCount(categoryLabels) == static_cast<size_t>(ChartSettingsCategory::Count));

		f32 categoryWidth = 0.0f;
		const size_t visibleCategoryCount = ArrayCount(categoryLabels);
		for (size_t categoryIndex = 0; categoryIndex < visibleCategoryCount; ++categoryIndex)
			if (showDeveloperSettings || categoryIndex != static_cast<size_t>(ChartSettingsCategory::Developer))
				categoryWidth = Max(categoryWidth, Gui::CalcTextSize(categoryLabels[categoryIndex]).x);
		for (const std::string& section : sectionNavigation.Sections)
			categoryWidth = Max(categoryWidth, Gui::CalcTextSize(section).x + GuiScale(14.0f));
		categoryWidth += GuiScale(24.0f) + Gui::GetStyle().ScrollbarSize;
		categoryWidth = Min(categoryWidth, Max(1.0f, Gui::GetContentRegionAvail().x * 0.38f));

		if (Gui::BeginChild("SettingsCategories", vec2(categoryWidth, 0.0f), ImGuiChildFlags_Borders))
		{
			Gui::PushStyleVar(ImGuiStyleVar_FramePadding, GuiScale(vec2(10.0f, 5.0f)));
			for (size_t categoryIndex = 0; categoryIndex < visibleCategoryCount; categoryIndex++)
			{
				const auto category = static_cast<ChartSettingsCategory>(categoryIndex);
				if (!showDeveloperSettings && category == ChartSettingsCategory::Developer) continue;
				if (Gui::Selectable(categoryLabels[categoryIndex], selectedCategory == category, ImGuiSelectableFlags_None, vec2(0.0f, Gui::GetFrameHeight())) && selectedCategory != category)
				{
					SelectCategory(category);
					lastActiveGroup = {};
					inputState.SelectedMultiBinding = nullptr;
					inputState.TempAssignedBinding = nullptr;
					inputState.MultiBindingPopupFadeCurrent = inputState.MultiBindingPopupFadeTarget = 0.0f;
					testPlayCalibrationActive = false;
				}
				if (selectedCategory == category)
				{
					Gui::PushID(static_cast<i32>(category));
					Gui::Indent(GuiScale(14.0f));
					for (const std::string& section : sectionNavigation.Sections)
						if (Gui::Selectable(section.c_str(), sectionNavigation.SelectedSection == section)) SelectSection(section);
					Gui::Unindent(GuiScale(14.0f));
					Gui::PopID();
				}
			}
			Gui::PopStyleVar();
		}
		Gui::EndChild();
		Gui::SameLine();

		Gui::PushID(static_cast<i32>(selectedCategory));
		if (Gui::BeginChild("SettingsContent", vec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			sectionNavigation.Sections.clear();
			Gui::UpdateSmoothScrollWindow();
			Gui::SeparatorText(categoryLabels[static_cast<size_t>(selectedCategory)]);
			if (selectedCategory == ChartSettingsCategory::About)
			{
				Gui::TextUnformatted("PeepoDrumKit");
				Gui::Spacing();
				if (Gui::BeginTable("AboutInformation", 2, ImGuiTableFlags_SizingFixedFit))
				{
					const struct { cstr Label; cstr Value; } information[] =
					{
						{ UI_Str("INFO_HELP_COPYRIGHT_YEAR"), "Samyuu" },
						{ UI_Str("INFO_HELP_MODIFIED_BY"), "Blazinbeats440" },
						{ UI_Str("INFO_HELP_BUILD_TIME"), BuildInfo::CompilationTime() },
						{ UI_Str("INFO_HELP_BUILD_DATE"), BuildInfo::CompilationDate() },
						{ UI_Str("INFO_HELP_BUILD_CONFIGURATION"), UI_Str(BuildInfo::BuildConfiguration()) },
						{ UI_Str("INFO_HELP_CURRENT_VERSION"), BuildInfo::CurrentVersion() },
					};
					for (const auto& row : information)
					{
						Gui::TableNextRow();
						Gui::TableSetColumnIndex(0); Gui::TextUnformatted(row.Label);
						Gui::TableSetColumnIndex(1); Gui::TextUnformatted(row.Value);
					}
					Gui::EndTable();
				}
			}

			if (selectedCategory == ChartSettingsCategory::General)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				{
					sectionNavigation.DrawHeading(UI_Str("SETTINGS_TAB_LANGUAGE"));
					std::string selectedLanguageName = SelectedGuiLanguage;
					for (const auto& it : i18n::LocaleEntries)
					{
						if (SelectedGuiLanguage == it.id)
						{
							selectedLanguageName = it.name + " (" + it.id + ")";
							break;
						}
					}

					Gui::AlignTextToFramePadding();
					Gui::TextUnformatted(UI_Str("SETTINGS_GENERAL_LANGUAGE"));
					Gui::SameLine();
					Gui::SetNextItemWidth(-1.0f);
					if (Gui::BeginCombo("##SettingsLanguage", selectedLanguageName.c_str(), ImGuiComboFlags_None))
					{
						for (const auto& it : i18n::LocaleEntries)
						{
							std::string languageName = it.name + " (" + it.id + ")";
							const b8 isSelected = (SelectedGuiLanguage == it.id);
							if (Gui::Selectable(languageName.c_str(), isSelected))
							{
								SelectedGuiLanguage = it.id;
								SelectedGuiLanguageTJA = ASCII::IETFLangTagToTJALangTag(SelectedGuiLanguage);
								i18n::ReloadLocaleFile(SelectedGuiLanguage.c_str());
							}
							if (isSelected) Gui::SetItemDefaultFocus();
						}
						Gui::EndCombo();
					}
					if (Gui::Button(UI_Str("SETTINGS_GENERAL_EXPORT_BUILTIN_LOCALE_FILES"), { Gui::CalcItemWidth(), 0.0f }))
						i18n::ExportBuiltinLocaleFiles();
				}
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::General || selectedCategory == ChartSettingsCategory::File ||
				selectedCategory == ChartSettingsCategory::Timeline || selectedCategory == ChartSettingsCategory::Editor || selectedCategory == ChartSettingsCategory::Appearance)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				{
					constexpr size_t SizeOfUserSettingsData = sizeof(UserSettingsData);
					//static_assert(PEEPO_RELEASE || SizeOfUserSettingsData == 6120, "TODO: Add missing settings entries for newly added UserSettingsData fields");
					if (selectedCategory == ChartSettingsCategory::Appearance)
					{
						SettingsGui::SettingsEntry themeEntries[] =
						{
							SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_THEME"), settings.Appearance.ColorTheme,
								UI_Str("SETTINGS_APPEARANCE_COLOR_THEME"), UI_Str("SETTINGS_APPEARANCE_COLOR_THEME_DESC"), SettingsGui::WidgetType::I32_ColorTheme),
							SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_THEME"), settings.Appearance.ColorThemeAccent,
								UI_Str("SETTINGS_APPEARANCE_ACCENT_COLOR"), UI_Str("SETTINGS_APPEARANCE_ACCENT_COLOR_DESC"), SettingsGui::WidgetType::I32_ThemeAccent),
						};
						Gui::PushID("ThemeSettings");
						changesWereMade |= SettingsGui::DrawEntriesListTableGui(themeEntries, ArrayCount(themeEntries), nullptr, lastActiveThemeGroup, sectionNavigation, selectedCategory);
						if (*settings.Appearance.ColorThemeAccent == 7)
						{
							auto& color = settings.Appearance.ColorThemeAccentColor;
							Gui::PushID("CustomAccentColor");
							Gui::AlignTextToFramePadding();
							Gui::TextUnformatted(UI_Str("SETTINGS_APPEARANCE_CUSTOM_ACCENT_COLOR"));
							if (color.HasValue)
							{
								Gui::SameLine();
								if (Gui::SmallButton(UI_Str("SETTINGS_RESET_DEFAULT")))
								{
									color.ResetToDefault();
									changesWereMade = true;
								}
							}
							Gui::SetNextItemWidth(-1.0f);
							changesWereMade |= Gui::ColorEdit3_U32("##Color", &color.Value);
							color.SetHasValueIfNotDefault();
							Gui::PopID();
						}
						Gui::PopID();
					}

					SettingsGui::SettingsEntry settingsEntriesMain[] =
					{
						SettingsGui::SettingsEntry(ChartSettingsCategory::General, UI_Str("SETTINGS_SECTION_DEFAULTS"),
							settings.General.DefaultCreatorName,
							UI_Str("SETTINGS_GENERAL_DEFAULT_CREATOR"),
							UI_Str("SETTINGS_GENERAL_DEFAULT_CREATOR_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_OPEN"),
							settings.General.ChartOpenDirectory,
							UI_Str("SETTINGS_OPEN_FOLDER"),
							UI_Str("SETTINGS_OPEN_FOLDER_DESC"),
							SettingsGui::WidgetType::StdString_ChartOpenDirectory),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_OPEN"),
							settings.General.RecentFilesShowFullPath,
							UI_Str("SETTINGS_RECENT_DISPLAY"),
							UI_Str("SETTINGS_RECENT_DISPLAY_DESC"),
							SettingsGui::WidgetType::B8_RecentFilesDisplay),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_OPEN"),
							settings.General.TaikojiroExecutablePath,
							UI_Str("SETTINGS_TAIKOJIRO_EXECUTABLE"),
							UI_Str("SETTINGS_TAIKOJIRO_EXECUTABLE_DESC"),
							SettingsGui::WidgetType::StdString_TaikojiroExecutable),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_SAVE"),
							settings.General.TJAFileSaveFormat,
							UI_Str("SETTINGS_TJA_SAVE_FORMAT"),
							UI_Str("SETTINGS_TJA_SAVE_FORMAT_DESC"),
							SettingsGui::WidgetType::I32_TJAFileSaveFormat),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("VIDEO_EXPORT_WINDOW"),
							settings.General.VideoExportHardwareEncoding,
							UI_Str("SETTINGS_VIDEO_HARDWARE_ENCODING"),
							UI_Str("SETTINGS_VIDEO_HARDWARE_ENCODING_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_SAVE"),
							settings.General.IncludePeepoDrumKitComment,
							UI_Str("SETTINGS_TJA_INCLUDE_HEADER"),
							UI_Str("SETTINGS_TJA_INCLUDE_HEADER_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::File, UI_Str("SETTINGS_SECTION_COMPATIBILITY"),
							settings.General.WarnTaikojiroIncompatibleCharts,
							UI_Str("SETTINGS_TJA_WARN_TAIKOJIRO_INCOMPATIBLE"),
							UI_Str("SETTINGS_TJA_WARN_TAIKOJIRO_INCOMPATIBLE_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.BalloonExpectedHitsPerSecond,
							UI_Str("SETTINGS_GENERAL_BALLOON_EXPECTED_HITS"),
							UI_Str("SETTINGS_GENERAL_BALLOON_EXPECTED_HITS_DESC"),
							SettingsGui::WidgetType::F32_BalloonExpectedHitsPerSecond),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.TimelineAutoStepAfterNoteInput,
							UI_Str("SETTINGS_TIMELINE_AUTO_STEP_AFTER_NOTE_INPUT"),
							UI_Str("SETTINGS_TIMELINE_AUTO_STEP_AFTER_NOTE_INPUT_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.TimelineGridStartsAtBar,
							UI_Str("SETTINGS_TIMELINE_GRID_STARTS_AT_BAR"),
							UI_Str("SETTINGS_TIMELINE_GRID_STARTS_AT_BAR_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_SCROLL"),
							settings.General.TimelinePlaybackTimeExpanded,
							UI_Str("SETTINGS_TIMELINE_PLAYBACK_TIME_EXPANDED"),
							UI_Str("SETTINGS_TIMELINE_PLAYBACK_TIME_EXPANDED_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.GridBarDivisions,
							UI_Str("SETTINGS_GRID_DIVISIONS"),
							UI_Str("SETTINGS_GRID_DIVISIONS_DESC"),
							SettingsGui::WidgetType::Default, &inputState.GridDivisionTexts[0]),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.GridBarDivisionsRough,
							UI_Str("SETTINGS_GRID_DIVISIONS_ROUGH"),
							UI_Str("SETTINGS_GRID_DIVISIONS_ROUGH_DESC"),
							SettingsGui::WidgetType::Default, &inputState.GridDivisionTexts[1]),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_INPUT"),
							settings.General.GridBarDivisionsPrecise,
							UI_Str("SETTINGS_GRID_DIVISIONS_PRECISE"),
							UI_Str("SETTINGS_GRID_DIVISIONS_PRECISE_DESC"),
							SettingsGui::WidgetType::Default, &inputState.GridDivisionTexts[2]),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_SCROLL"),
							settings.General.TimelineScrollInvertMouseWheel,
							UI_Str("SETTINGS_TIMELINE_INVERT_SCROLL"),
							UI_Str("SETTINGS_TIMELINE_INVERT_SCROLL_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_SCROLL"),
							settings.General.TimelineScrollDistancePerMouseWheelTick,
							UI_Str("SETTINGS_TIMELINE_SCROLL_SENSITIVITY"),
							UI_Str("SETTINGS_TIMELINE_SCROLL_SENSITIVITY_DESC"),
							SettingsGui::WidgetType::F32_TimelineScrollSensitivity),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_SCROLL"),
							settings.General.TimelineScrollDistancePerMouseWheelTickFast,
							UI_Str("SETTINGS_TIMELINE_SCROLL_SENSITIVITY_SHIFT"),
							UI_Str("SETTINGS_TIMELINE_SCROLL_SENSITIVITY_SHIFT_DESC"),
							SettingsGui::WidgetType::F32_TimelineScrollSensitivity),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_PLAYBACK"),
							settings.General.TimelinePlaybackCursorFollow,
							UI_Str("SETTINGS_TIMELINE_PLAYBACK_CURSOR_FOLLOW"),
							UI_Str("SETTINGS_TIMELINE_PLAYBACK_CURSOR_FOLLOW_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_PLAYBACK"),
							settings.General.TimelineLoopPlayback,
							UI_Str("SETTINGS_TIMELINE_LOOP_PLAYBACK"),
							UI_Str("SETTINGS_TIMELINE_LOOP_PLAYBACK_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_DISPLAY"),
							settings.General.DisplayTimeInSongSpace,
							UI_Str("SETTINGS_GENERAL_TIME_DISPLAY"),
							UI_Str("SETTINGS_GENERAL_TIME_DISPLAY_DESC"),
							SettingsGui::WidgetType::B8_ChartSongSpaceComboBox),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Timeline, UI_Str("SETTINGS_SECTION_DISPLAY"),
							settings.General.TimelineShowBranchStartLines,
							UI_Str("SETTINGS_TIMELINE_SHOW_BRANCH_START_LINES"),
							UI_Str("SETTINGS_TIMELINE_SHOW_BRANCH_START_LINES_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PROPERTIES"),
							settings.General.ShowChartTitleLocalized,
							UI_Str("SETTINGS_PROPERTIES_SHOW_TITLE_TRANSLATIONS"),
							UI_Str("SETTINGS_PROPERTIES_SHOW_TITLE_TRANSLATIONS_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PROPERTIES"),
							settings.General.ShowChartSubtitleLocalized,
							UI_Str("SETTINGS_PROPERTIES_SHOW_SUBTITLE_TRANSLATIONS"),
							UI_Str("SETTINGS_PROPERTIES_SHOW_SUBTITLE_TRANSLATIONS_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PROPERTIES"),
							settings.General.AutoPrefixChartSubtitle,
							UI_Str("SETTINGS_PROPERTIES_AUTO_PREFIX_SUBTITLE"),
							UI_Str("SETTINGS_PROPERTIES_AUTO_PREFIX_SUBTITLE_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PROPERTIES"),
							settings.General.ShowChartOtherMetadata,
							UI_Str("SETTINGS_PROPERTIES_SHOW_CHART_METADATA"),
							UI_Str("SETTINGS_PROPERTIES_SHOW_CHART_METADATA_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PROPERTIES"),
							settings.General.ShowCourseOtherMetadata,
							UI_Str("SETTINGS_PROPERTIES_SHOW_COURSE_METADATA"),
							UI_Str("SETTINGS_PROPERTIES_SHOW_COURSE_METADATA_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_EVENTS"),
							settings.General.ShowForcedBranchButtons,
							UI_Str("SETTINGS_BRANCH_SHOW_FORCED_BRANCH_BUTTONS"),
							UI_Str("SETTINGS_BRANCH_SHOW_FORCED_BRANCH_BUTTONS_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_EVENTS"),
							settings.General.EventShowSudden,
							UI_Str("SETTINGS_EVENT_SHOW_SUDDEN"),
							UI_Str("SETTINGS_EVENT_SHOW_SUDDEN_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_EVENTS"),
							settings.General.EventShowJPOSScroll,
							UI_Str("SETTINGS_EVENT_SHOW_JPOS_SCROLL"),
							UI_Str("SETTINGS_EVENT_SHOW_JPOS_SCROLL_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_EVENTS"),
							settings.General.EventShowScrollType,
							UI_Str("SETTINGS_EVENT_SHOW_SCROLL_TYPE"),
							UI_Str("SETTINGS_EVENT_SHOW_SCROLL_TYPE_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_EVENTS"),
							settings.General.EventShowDelay,
							UI_Str("SETTINGS_EVENT_SHOW_DELAY"),
							UI_Str("SETTINGS_EVENT_SHOW_DELAY_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewLayout,
							UI_Str("SETTINGS_PREVIEW_LAYOUT"),
							UI_Str("SETTINGS_PREVIEW_LAYOUT_DESC"),
							SettingsGui::WidgetType::I32_GamePreviewLayout),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewArcadeSizeMode,
							UI_Str("SETTINGS_PREVIEW_ARCADE_SIZE_MODE"),
							UI_Str("SETTINGS_PREVIEW_SIZE_MODE_DESC"),
							SettingsGui::WidgetType::I32_GamePreviewSizeMode),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewJiroSizeMode,
							UI_Str("SETTINGS_PREVIEW_JIRO_SIZE_MODE"),
							UI_Str("SETTINGS_PREVIEW_SIZE_MODE_DESC"),
							SettingsGui::WidgetType::I32_GamePreviewSizeMode),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.ScrollSpeedViewType,
							UI_Str("SETTINGS_SCROLL_SPEED_VIEW_TYPE"),
							UI_Str("SETTINGS_SCROLL_SPEED_VIEW_TYPE_DESC"),
							SettingsGui::WidgetType::I32_ScrollSpeedViewType),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewShowMeasureNumbers,
							UI_Str("SETTINGS_PREVIEW_SHOW_MEASURE_NUMBERS"),
							UI_Str("SETTINGS_PREVIEW_SHOW_MEASURE_NUMBERS_DESC"),
							SettingsGui::WidgetType::I32_GamePreviewMeasureNumbers),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewBarLinesBehindNotes,
							UI_Str("SETTINGS_PREVIEW_BAR_LINES_BEHIND_NOTES"),
							UI_Str("SETTINGS_PREVIEW_BAR_LINES_BEHIND_NOTES_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewShowJPOSPosition,
							UI_Str("SETTINGS_PREVIEW_SHOW_JPOS_POSITION"),
							UI_Str("SETTINGS_PREVIEW_SHOW_JPOS_POSITION_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewUseBackgroundMovie,
							UI_Str("SETTINGS_PREVIEW_USE_BACKGROUND_MOVIE"),
							UI_Str("SETTINGS_USE_BACKGROUND_MOVIE_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewShowUnselectedNoteTooltips,
							UI_Str("SETTINGS_PREVIEW_UNSELECTED_NOTE_TOOLTIPS"),
							UI_Str("SETTINGS_PREVIEW_UNSELECTED_NOTE_TOOLTIPS_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Editor, UI_Str("SETTINGS_SECTION_PREVIEW"),
							settings.General.GamePreviewCommentHoldMeasures,
							UI_Str("SETTINGS_PREVIEW_COMMENT_HOLD_MEASURES"),
							UI_Str("SETTINGS_PREVIEW_COMMENT_HOLD_MEASURES_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_ANIMATION"), settings.Animation.EnableGuiScaleAnimation,
							UI_Str("SETTINGS_ANIMATION_SMOOTH_ZOOM"),
							UI_Str("SETTINGS_ANIMATION_SMOOTH_ZOOM_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_ANIMATION"), settings.Animation.TimelineSmoothScrollSpeed,
							UI_Str("SETTINGS_ANIMATION_SCROLL_SPEED"),
							UI_Str("SETTINGS_ANIMATION_SCROLL_SPEED_DESC"),
							SettingsGui::WidgetType::F32_ExponentialSpeed),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_ANIMATION"), settings.Animation.TimelineWorldSpaceCursorXSpeed,
							UI_Str("SETTINGS_ANIMATION_CURSOR_SPEED"),
							UI_Str("SETTINGS_ANIMATION_CURSOR_SPEED_DESC"),
							SettingsGui::WidgetType::F32_ExponentialSpeed),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_ANIMATION"), settings.Animation.TimelineRangeSelectionExpansionSpeed,
							UI_Str("SETTINGS_ANIMATION_RANGE_SPEED"),
							UI_Str("SETTINGS_ANIMATION_RANGE_SPEED_DESC"),
							SettingsGui::WidgetType::F32_ExponentialSpeed),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Appearance, UI_Str("SETTINGS_SECTION_PERFORMANCE"),
							settings.General.VSyncOffFPSLimit,
							UI_Str("SETTINGS_VSYNC_OFF_FPS_LIMIT"),
							UI_Str("SETTINGS_VSYNC_OFF_FPS_LIMIT_DESC"),
							SettingsGui::WidgetType::I32_VSyncOffFPSLimit),
					};

					changesWereMade |= SettingsGui::DrawEntriesListTableGui(settingsEntriesMain, ArrayCount(settingsEntriesMain), selectedCategory == ChartSettingsCategory::Appearance ? nullptr : &settingsFilterMain, lastActiveGroup, sectionNavigation, selectedCategory);
				}
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::Developer)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				Gui::TextWrapped(UI_Str("SETTINGS_DEVELOPER_HINT"));
				Gui::TextWrapped(UI_Str("SETTINGS_SENOTES_OFFSET_DESC"));
				cstr labels[] =
				{
					UI_Str("SETTINGS_SENOTES_DO"), UI_Str("SETTINGS_SENOTES_KO"), UI_Str("SETTINGS_SENOTES_DON"),
					UI_Str("SETTINGS_SENOTES_DON_BIG"), UI_Str("SETTINGS_SENOTES_DON_HAND"), UI_Str("SETTINGS_SENOTES_KA"),
					UI_Str("SETTINGS_SENOTES_KATSU"), UI_Str("SETTINGS_SENOTES_KATSU_BIG"), UI_Str("SETTINGS_SENOTES_KATSU_HAND"),
					UI_Str("SETTINGS_SENOTES_KADON"), UI_Str("SETTINGS_SENOTES_DRUMROLL"), UI_Str("SETTINGS_SENOTES_DRUMROLL_BIG"),
					UI_Str("SETTINGS_SENOTES_BALLOON"), UI_Str("SETTINGS_SENOTES_BALLOON_SPECIAL"), UI_Str("SETTINGS_SENOTES_BOMB"),
					UI_Str("SETTINGS_SENOTES_ADLIB"), UI_Str("SETTINGS_SENOTES_FUSE"),
				};
				static_assert(ArrayCount(labels) == std::extent_v<decltype(UserSettingsData::DeveloperData::SENoteOffsetX)>);
				std::vector<SettingsGui::SettingsEntry> entries;
				entries.reserve(ArrayCount(labels));
				for (size_t index = 0; index < ArrayCount(labels); ++index)
					entries.emplace_back(ChartSettingsCategory::Developer, UI_Str("SETTINGS_SECTION_SENOTES"),
						settings.Developer.SENoteOffsetX[index], labels[index], "", SettingsGui::WidgetType::F32_SENoteOffset);
				changesWereMade |= SettingsGui::DrawEntriesListTableGui(entries.data(), entries.size(), &settingsFilterMain, lastActiveGroup, sectionNavigation);
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::Audio)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				{
					SettingsGui::SettingsEntry settingsEntriesAudio[] =
					{
						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_VOLUME"),
							settings.Audio.MasterVolume,
							UI_Str("SETTINGS_AUDIO_MASTER_VOLUME"),
							UI_Str("SETTINGS_AUDIO_MASTER_VOLUME_DESC"),
							SettingsGui::WidgetType::F32_AudioMasterVolume),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_VOLUME"),
							settings.Audio.BalloonVolume,
							UI_Str("SETTINGS_AUDIO_BALLOON_VOLUME"),
							UI_Str("SETTINGS_AUDIO_BALLOON_VOLUME_DESC"),
							SettingsGui::WidgetType::F32_AudioMasterVolume),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_VOLUME"),
							settings.Audio.MetronomeVolume,
							UI_Str("SETTINGS_AUDIO_METRONOME_VOLUME"),
							UI_Str("SETTINGS_AUDIO_METRONOME_VOLUME_DESC"),
							SettingsGui::WidgetType::F32_AudioMasterVolume),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_VOLUME"),
							settings.General.DrumrollPreviewRollsPerSecond,
							UI_Str("SETTINGS_GENERAL_DRUMROLL_PREVIEW"),
							UI_Str("SETTINGS_GENERAL_DRUMROLL_PREVIEW_DESC"),
							SettingsGui::WidgetType::F32_DrumrollRollsPerSecond),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_DEVICE"),
							settings.Audio.OpenDeviceOnStartup,
							UI_Str("SETTINGS_AUDIO_OPEN_STARTUP"),
							UI_Str("SETTINGS_AUDIO_OPEN_STARTUP_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_DEVICE"),
							settings.Audio.CloseDeviceOnIdleFocusLoss,
							UI_Str("SETTINGS_AUDIO_CLOSE_FOCUS_LOSS"),
							UI_Str("SETTINGS_AUDIO_CLOSE_FOCUS_LOSS_DESC")),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_DEVICE"),
							settings.Audio.RequestExclusiveDeviceAccess,
							UI_Str("SETTINGS_AUDIO_EXCLUSIVE_MODE"),
							UI_Str("SETTINGS_AUDIO_EXCLUSIVE_MODE_DESC"),
							SettingsGui::WidgetType::B8_ExclusiveAudioComboBox),

						SettingsGui::SettingsEntry(ChartSettingsCategory::Audio, UI_Str("SETTINGS_SECTION_DEVICE"),
							settings.Audio.BufferFrameSize,
							UI_Str("SETTINGS_AUDIO_BUFFER_SIZE"),
							UI_Str("SETTINGS_AUDIO_BUFFER_SIZE_DESC"),
							SettingsGui::WidgetType::I32_AudioBufferFrameSize),
					};

					changesWereMade |= SettingsGui::DrawEntriesListTableGui(settingsEntriesAudio, ArrayCount(settingsEntriesAudio), nullptr, lastActiveGroup, sectionNavigation);
				}
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::TestPlay)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				SettingsGui::SettingsEntry settingsEntriesTestPlay[] =
				{
					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_PLAYBACK"), settings.TestPlay.LeadInMilliseconds, UI_Str("SETTINGS_TEST_PLAY_LEAD_IN"), UI_Str("SETTINGS_TEST_PLAY_LEAD_IN_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_PLAYBACK"), settings.TestPlay.LoopDelayMilliseconds, UI_Str("SETTINGS_TEST_PLAY_LOOP_DELAY"), ""),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_PLAYBACK"), settings.TestPlay.PlaybackSpeedPercent, UI_Str("SETTINGS_TEST_PLAY_PLAYBACK_SPEED"), UI_Str("SETTINGS_TEST_PLAY_PLAYBACK_SPEED_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_PLAYBACK"), settings.TestPlay.ShowStartButtonsInPreview, UI_Str("SETTINGS_TEST_PLAY_SHOW_START_BUTTONS"), ""),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_PLAYBACK"), settings.TestPlay.UseBackgroundMovie, UI_Str("SETTINGS_TEST_PLAY_USE_BACKGROUND_MOVIE"), UI_Str("SETTINGS_USE_BACKGROUND_MOVIE_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.JudgementDisplayMode, UI_Str("SETTINGS_TEST_PLAY_DISPLAY_MODE"), "", SettingsGui::WidgetType::I32_TestPlayJudgementDisplayMode),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.JudgementDisplayMilliseconds, UI_Str("SETTINGS_TEST_PLAY_DISPLAY_DURATION"), ""),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.PausedJudgementDisplayMode, UI_Str("SETTINGS_TEST_PLAY_PAUSED_DISPLAY_MODE"), "", SettingsGui::WidgetType::I32_TestPlayJudgementDisplayMode),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.PausedJudgementFilter, UI_Str("SETTINGS_TEST_PLAY_PAUSED_FILTER"), "", SettingsGui::WidgetType::I32_TestPlayPausedJudgementFilter),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.PausedJudgementThresholdMilliseconds, UI_Str("SETTINGS_TEST_PLAY_PAUSED_THRESHOLD"), ""),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_DISPLAY"), settings.TestPlay.TimingNeutralWindowMilliseconds, UI_Str("SETTINGS_TEST_PLAY_NEUTRAL_WINDOW"), UI_Str("SETTINGS_TEST_PLAY_NEUTRAL_WINDOW_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_WINDOWS"), settings.TestPlay.GoodWindowMilliseconds, UI_Str("SETTINGS_TEST_PLAY_GOOD_WINDOW"), UI_Str("SETTINGS_TEST_PLAY_GOOD_WINDOW_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_WINDOWS"), settings.TestPlay.OkWindowMilliseconds, UI_Str("SETTINGS_TEST_PLAY_OK_WINDOW"), UI_Str("SETTINGS_TEST_PLAY_OK_WINDOW_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_JUDGEMENT_WINDOWS"), settings.TestPlay.BadWindowMilliseconds, UI_Str("SETTINGS_TEST_PLAY_BAD_WINDOW"), UI_Str("SETTINGS_TEST_PLAY_BAD_WINDOW_DESC")),

					SettingsGui::SettingsEntry(ChartSettingsCategory::TestPlay, UI_Str("SETTINGS_SECTION_INPUT_COMPENSATION"), settings.TestPlay.InputLatencyCompensationMilliseconds, UI_Str("SETTINGS_TEST_PLAY_INPUT_OFFSET"), UI_Str("SETTINGS_TEST_PLAY_INPUT_OFFSET_DESC")),
				};
				changesWereMade |= SettingsGui::DrawEntriesListTableGui(settingsEntriesTestPlay, ArrayCount(settingsEntriesTestPlay), nullptr, lastActiveGroup, sectionNavigation);
				const i32 pausedThreshold = Clamp(settings.TestPlay.PausedJudgementThresholdMilliseconds.Value, 0, 1000);
				if (settings.TestPlay.PausedJudgementThresholdMilliseconds.Value != pausedThreshold)
				{
					settings.TestPlay.PausedJudgementThresholdMilliseconds.Value = pausedThreshold;
					settings.TestPlay.PausedJudgementThresholdMilliseconds.SetHasValueIfNotDefault();
					changesWereMade = true;
				}
				const i32 loopDelay = Clamp(settings.TestPlay.LoopDelayMilliseconds.Value, 0, 5000);
				if (settings.TestPlay.LoopDelayMilliseconds.Value != loopDelay)
				{
					settings.TestPlay.LoopDelayMilliseconds.Value = loopDelay;
					settings.TestPlay.LoopDelayMilliseconds.SetHasValueIfNotDefault();
					changesWereMade = true;
				}
				const i32 goodWindow = Clamp(settings.TestPlay.GoodWindowMilliseconds.Value, 1, 500);
				const i32 okWindow = Clamp(settings.TestPlay.OkWindowMilliseconds.Value, goodWindow, 1000);
				const i32 badWindow = Clamp(settings.TestPlay.BadWindowMilliseconds.Value, okWindow, 1000);
				if (settings.TestPlay.GoodWindowMilliseconds.Value != goodWindow || settings.TestPlay.OkWindowMilliseconds.Value != okWindow || settings.TestPlay.BadWindowMilliseconds.Value != badWindow)
				{
					settings.TestPlay.GoodWindowMilliseconds.Value = goodWindow;
					settings.TestPlay.OkWindowMilliseconds.Value = okWindow;
					settings.TestPlay.BadWindowMilliseconds.Value = badWindow;
					settings.TestPlay.GoodWindowMilliseconds.SetHasValueIfNotDefault();
					settings.TestPlay.OkWindowMilliseconds.SetHasValueIfNotDefault();
					settings.TestPlay.BadWindowMilliseconds.SetHasValueIfNotDefault();
					changesWereMade = true;
				}
				sectionNavigation.DrawHeading(UI_Str("SETTINGS_SECTION_CALIBRATION"));
				Gui::TextWrapped("%s", UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_DESC"));
				if (Gui::Button(testPlayCalibrationActive ? UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_STOP") : UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_START")))
				{
					testPlayCalibrationActive = !testPlayCalibrationActive;
					if (testPlayCalibrationActive)
					{
						testPlayCalibrationFirstTick = Gui::GetTime() + 1.0;
						testPlayCalibrationLastCue = testPlayCalibrationLastTap = -1;
						testPlayCalibrationErrors.clear();
						testPlayCalibrationHasCandidate = false;
					}
				}
				if (testPlayCalibrationActive)
				{
					static constexpr i32 calibrationCountInCues = 4;
					const f64 elapsedTicks = (Gui::GetTime() - testPlayCalibrationFirstTick) * 2.0;
					const f64 measurementTicks = elapsedTicks - calibrationCountInCues;
					const i32 cueIndex = static_cast<i32>(Floor(elapsedTicks));
					if (cueIndex >= 0 && cueIndex < calibrationCountInCues + 8 && cueIndex > testPlayCalibrationLastCue)
					{
						context.SfxVoicePool.PlaySound(cueIndex < calibrationCountInCues ? SoundEffectType::MetronomeBeat : SoundEffectType::TaikoDon);
						testPlayCalibrationLastCue = cueIndex;
					}
					if (Gui::IsKeyPressed(ImGuiKey_Space, false))
					{
						const i32 tapIndex = static_cast<i32>(Round(measurementTicks));
						const i32 error = static_cast<i32>(Round((measurementTicks - tapIndex) * 500.0));
						if (tapIndex >= 0 && tapIndex < 8 && tapIndex != testPlayCalibrationLastTap && Absolute(error) <= 160)
						{
							testPlayCalibrationErrors.push_back(error);
							testPlayCalibrationLastTap = tapIndex;
						}
					}
					if (elapsedTicks < calibrationCountInCues)
						Gui::Text(UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_COUNT_IN"), Clamp(testPlayCalibrationLastCue + 1, 0, calibrationCountInCues), calibrationCountInCues);
					else
						Gui::Text("%d / 8", static_cast<i32>(testPlayCalibrationErrors.size()));
					if (measurementTicks > 7.8)
					{
						testPlayCalibrationActive = false;
						if (testPlayCalibrationErrors.size() >= 4)
						{
							i32 sum = 0;
							for (i32 error : testPlayCalibrationErrors) sum += error;
							testPlayCalibrationCandidate = Clamp(static_cast<i32>(Round(static_cast<f64>(sum) / testPlayCalibrationErrors.size())), -500, 500);
							testPlayCalibrationHasCandidate = true;
						}
					}
				}
				if (testPlayCalibrationHasCandidate)
				{
					Gui::Text("%s: %+d ms", UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_CANDIDATE"), testPlayCalibrationCandidate);
					if (Gui::Button(UI_Str("SETTINGS_TEST_PLAY_CALIBRATION_APPLY")))
					{
						settings.TestPlay.InputLatencyCompensationMilliseconds.Value = testPlayCalibrationCandidate;
						settings.TestPlay.InputLatencyCompensationMilliseconds.SetHasValueIfNotDefault();
						changesWereMade = true;
					}
				}
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::Appearance)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				{
					sectionNavigation.DrawHeading(UI_Str("SETTINGS_SECTION_COLORS"));
					static constexpr ImGuiColorEditFlags colorEditFlags = ImGuiColorEditFlags_AlphaPreviewHalf;
					auto drawColorSetting = [&](cstr label, WithDefault<u32>& setting)
					{
						u32& color = setting.Value;
						Gui::PushID(label);
						Gui::AlignTextToFramePadding();
						Gui::TextUnformatted(label);
						if (setting.HasValue)
						{
							Gui::SameLine();
							if (Gui::SmallButton(UI_Str("SETTINGS_RESET_DEFAULT")))
								setting.ResetToDefault();
						}
						Gui::SetNextItemWidth(-1.0f);
						changesWereMade |= Gui::ColorEdit4_U32("##Color", &color, colorEditFlags);
						setting.SetHasValueIfNotDefault();
						Gui::PopID();
					};

					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_MARKER_LINE_COLOR"), settings.Appearance.MarkerLineColor);
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_BRANCH_START_LINE_COLOR"), settings.Appearance.BranchStartLineColor);
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_BRANCH_AREA_COLOR"), settings.Appearance.BranchAreaBackgroundColor);
					auto& laneTransparency = settings.Appearance.PreviewLaneBackgroundTransparency;
					Gui::AlignTextToFramePadding();
					Gui::TextUnformatted(UI_Str("SETTINGS_APPEARANCE_PREVIEW_LANE_TRANSPARENCY"));
					if (laneTransparency.HasValue)
					{
						Gui::SameLine();
						Gui::PushID("PreviewLaneBackgroundTransparency");
						if (Gui::SmallButton(UI_Str("SETTINGS_RESET_DEFAULT")))
						{
							laneTransparency.ResetToDefault();
							changesWereMade = true;
						}
						Gui::PopID();
					}
					Gui::SetNextItemWidth(-1.0f);
					changesWereMade |= Gui::SliderFloat("##PreviewLaneBackgroundTransparency", &laneTransparency.Value,
						0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
					laneTransparency.SetHasValueIfNotDefault();
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_PREVIEW_BRANCH_EXPERT_LANE_COLOR"), settings.Appearance.PreviewBranchExpertLaneBackgroundColor);
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_PREVIEW_BRANCH_MASTER_LANE_COLOR"), settings.Appearance.PreviewBranchMasterLaneBackgroundColor);
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_PREVIEW_COMMENT_TEXT_COLOR"), settings.Appearance.PreviewCommentTextColor);
					drawColorSetting(UI_Str("SETTINGS_APPEARANCE_PREVIEW_COMMENT_OUTLINE_COLOR"), settings.Appearance.PreviewCommentOutlineColor);
				}
				Gui::PopStyleVar();
			}

			if (selectedCategory == ChartSettingsCategory::Keys)
			{
				Gui::PushStyleVar(ImGuiStyleVar_FramePadding, originalFramePadding);
				{
					constexpr size_t SizeOfUserSettingsInputData = sizeof(UserSettingsData::InputData);
					//static_assert(PEEPO_RELEASE || SizeOfUserSettingsInputData == 5530, "TODO: Add missing settings entries for newly added UserSettingsData::InputData bindings");

					SettingsGui::InputSettingsEntry settingsEntriesInput[] =
					{
						{ &settings.Input.Editor_ToggleFullscreen, UI_Str("SETTINGS_KEY_EDITOR_TOGGLE_FULLSCREEN"), },
						{ &settings.Input.Editor_ToggleVSync, UI_Str("SETTINGS_KEY_EDITOR_TOGGLE_V_SYNC"), },
						{ &settings.Input.Editor_IncreaseGuiScale, UI_Str("SETTINGS_KEY_EDITOR_INCREASE_GUI_SCALE"), },
						{ &settings.Input.Editor_DecreaseGuiScale, UI_Str("SETTINGS_KEY_EDITOR_DECREASE_GUI_SCALE"), },
						{ &settings.Input.Editor_ResetGuiScale, UI_Str("SETTINGS_KEY_EDITOR_RESET_GUI_SCALE"), },
						{ &settings.Input.Editor_Undo, UI_Str("SETTINGS_KEY_EDITOR_UNDO"), },
						{ &settings.Input.Editor_Redo, UI_Str("SETTINGS_KEY_EDITOR_REDO"), },
						{ &settings.Input.Editor_OpenHelp, UI_Str("SETTINGS_KEY_EDITOR_OPEN_HELP"), },
						{ &settings.Input.Editor_OpenUpdateNotes, UI_Str("SETTINGS_KEY_EDITOR_OPEN_UPDATE_NOTES"), },
						{ &settings.Input.Editor_OpenChartStats, UI_Str("SETTINGS_KEY_EDITOR_OPEN_CHART_STATS"), },
						{ &settings.Input.Editor_OpenLyrics, UI_Str("SETTINGS_KEY_EDITOR_OPEN_LYRICS"), },
						{ &settings.Input.Editor_OpenTextEditor, UI_Str("SETTINGS_KEY_EDITOR_OPEN_TEXT_EDITOR"), },
						{ &settings.Input.Editor_OpenUtilities, UI_Str("SHORTCUT_OPEN_UTILITIES"), },
						{ &settings.Input.Editor_OpenSettings, UI_Str("SETTINGS_KEY_EDITOR_OPEN_SETTINGS"), },
						{ &settings.Input.Editor_OpenTemplate, UI_Str("SETTINGS_KEY_EDITOR_OPEN_TEMPLATE"), },
						{ &settings.Input.Editor_OpenChartBranches, UI_Str("SETTINGS_KEY_EDITOR_OPEN_CHART_BRANCHES"), },
						{ &settings.Input.Editor_IncreaseMasterVolume10, UI_Str("SETTINGS_KEY_EDITOR_INCREASE_MASTER_VOLUME10"), },
						{ &settings.Input.Editor_DecreaseMasterVolume10, UI_Str("SETTINGS_KEY_EDITOR_DECREASE_MASTER_VOLUME10"), },
						{ &settings.Input.Editor_ChartNew, UI_Str("SETTINGS_KEY_EDITOR_CHART_NEW"), },
						{ &settings.Input.Editor_ChartOpen, UI_Str("SETTINGS_KEY_EDITOR_CHART_OPEN"), },
						{ &settings.Input.Editor_ChartOpenDirectory, UI_Str("SETTINGS_KEY_EDITOR_CHART_OPEN_DIRECTORY"), },
						{ &settings.Input.Editor_ChartSave, UI_Str("SETTINGS_KEY_EDITOR_CHART_SAVE"), },
						{ &settings.Input.Editor_ChartSaveAs, UI_Str("SETTINGS_KEY_EDITOR_CHART_SAVE_AS"), },
						{},
						{ &settings.Input.Timeline_PlaceNoteDon, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_NOTE_DON"), },
						{ &settings.Input.Timeline_PlaceNoteKa, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_NOTE_KA"), },
						{ &settings.Input.Timeline_PlaceNoteBalloon, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_NOTE_BALLOON"), },
						{ &settings.Input.Timeline_PlaceNoteDrumroll, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_NOTE_DRUMROLL"), },
						{ &settings.Input.Timeline_PlaceNoteAdlib, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_ADLIB"), },
						{ &settings.Input.Timeline_PlaceNoteKaDon, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_KADON"), },
						{ &settings.Input.Timeline_PlaceNoteBomb, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_BOMB"), },
						{ &settings.Input.Timeline_PlaceNoteFuse, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_FUSE"), },
						{ &settings.Input.Timeline_PlaceRestAndStepCursor, UI_Str("SETTINGS_KEY_TIMELINE_PLACE_REST_AND_STEP_CURSOR"), },
						{ &settings.Input.Timeline_SelectBranchNormal, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_BRANCH_NORMAL"), },
						{ &settings.Input.Editor_TogglePreviewComments, UI_Str("SETTINGS_KEY_EDITOR_TOGGLE_PREVIEW_COMMENTS"), },
						{ &settings.Input.Editor_TogglePreviewLayout, UI_Str("SETTINGS_KEY_PREVIEW_LAYOUT"), },
						{ &settings.Input.Editor_ScreenshotWindow, UI_Str("SCREENSHOT_WINDOW"), },
						{ &settings.Input.Editor_ScreenshotPreview, UI_Str("SCREENSHOT_PREVIEW"), },
						{ &settings.Input.Timeline_SelectBranchExpert, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_BRANCH_EXPERT"), },
						{ &settings.Input.Timeline_SelectBranchMaster, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_BRANCH_MASTER"), },
						{ &settings.Input.Timeline_Cut, UI_Str("SETTINGS_KEY_TIMELINE_CUT"), },
						{ &settings.Input.Timeline_Copy, UI_Str("SETTINGS_KEY_TIMELINE_COPY"), },
						{ &settings.Input.Timeline_Paste, UI_Str("SETTINGS_KEY_TIMELINE_PASTE"), },
						{ &settings.Input.Timeline_DeleteSelection, UI_Str("SETTINGS_KEY_TIMELINE_DELETE_SELECTION"), },
						{ &settings.Input.Timeline_ToggleMarkerAtCursor, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_MARKER_AT_CURSOR"), },
						{ &settings.Input.Timeline_JumpToMarker, UI_Str("SETTINGS_KEY_TIMELINE_JUMP_TO_MARKER"), },
						{ &settings.Input.Timeline_SelectRangeToMarker, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_RANGE_TO_MARKER"), },
						{ &settings.Input.Timeline_StartEndRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_START_END_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_SelectAll, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ALL"), },
						{ &settings.Input.Timeline_ClearSelection, UI_Str("SETTINGS_KEY_TIMELINE_CLEAR_SELECTION"), },
						{ &settings.Input.Timeline_InvertSelection, UI_Str("SETTINGS_KEY_TIMELINE_INVERT_SELECTION"), },
						{ &settings.Input.Timeline_SelectToChartEnd, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_TO_CHART_END"), },
						{ &settings.Input.Timeline_SelectAllWithinRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ALL_WITHIN_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_ShiftSelectionLeft, UI_Str("SETTINGS_KEY_TIMELINE_SHIFT_SELECTION_LEFT"), },
						{ &settings.Input.Timeline_ShiftSelectionRight, UI_Str("SETTINGS_KEY_TIMELINE_SHIFT_SELECTION_RIGHT"), },
						{ &settings.Input.Timeline_SelectItemPattern_xo, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_XO"), },
						{ &settings.Input.Timeline_SelectItemPattern_xoo, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_XOO"), },
						{ &settings.Input.Timeline_SelectItemPattern_xooo, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_XOOO"), },
						{ &settings.Input.Timeline_SelectItemPattern_xxoo, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_XXOO"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomA, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_A"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomB, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_B"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomC, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_C"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomD, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_D"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomE, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_E"), },
						{ &settings.Input.Timeline_SelectItemPattern_CustomF, UI_Str("SETTINGS_KEY_TIMELINE_SELECT_ITEM_PATTERN_CUSTOM_F"), },
						{ &settings.Input.Timeline_InsertTempoChangeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_TEMPO_CHANGE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertTimeSignatureChangeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_TIME_SIGNATURE_CHANGE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertScrollChangeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_SCROLL_CHANGE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertBarLineChangeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_BAR_LINE_CHANGE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertScrollTypeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_SCROLL_TYPE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertJPOSScrollAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_JPOS_SCROLL_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertSuddenAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_SUDDEN_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertGoGoRangeAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_GO_GO_RANGE_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_InsertLyricAtSelectedItems, UI_Str("SETTINGS_KEY_TIMELINE_INSERT_LYRIC_AT_SELECTED_ITEMS"), },
						{ &settings.Input.Timeline_SetTimeSignatureFromRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SET_TIME_SIGNATURE_FROM_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_SetJPOSScrollDurationFromRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SET_JPOS_SCROLL_DURATION_FROM_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_SetSuddenAppearanceOffsetFromRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SET_SUDDEN_APPEARANCE_OFFSET_FROM_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_SetSuddenMovementOffsetFromRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SET_SUDDEN_MOVEMENT_OFFSET_FROM_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_SetGoGoRangeFromRangeSelection, UI_Str("SETTINGS_KEY_TIMELINE_SET_GO_GO_RANGE_FROM_RANGE_SELECTION"), },
						{ &settings.Input.Timeline_ConvertSelectionToScrollChanges, UI_Str("SETTINGS_KEY_TIMELINE_CONVERT_SELECTION_TO_SCROLL_CHANGES"), },
						{ &settings.Input.Timeline_FlipNoteType, UI_Str("SETTINGS_KEY_TIMELINE_FLIP_NOTE_TYPE"), },
						{ &settings.Input.Timeline_ToggleNoteSize, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_NOTE_SIZE"), },
						{ &settings.Input.Timeline_ExpandItemTime_2To1, UI_Str("SETTINGS_KEY_TIMELINE_EXPAND_ITEM_TIME_2_TO1"), },
						{ &settings.Input.Timeline_ExpandItemTime_3To2, UI_Str("SETTINGS_KEY_TIMELINE_EXPAND_ITEM_TIME_3_TO2"), },
						{ &settings.Input.Timeline_ExpandItemTime_4To3, UI_Str("SETTINGS_KEY_TIMELINE_EXPAND_ITEM_TIME_4_TO3"), },
						{ &settings.Input.Timeline_CompressItemTime_1To2, UI_Str("SETTINGS_KEY_TIMELINE_COMPRESS_ITEM_TIME_1_TO2"), },
						{ &settings.Input.Timeline_CompressItemTime_2To3, UI_Str("SETTINGS_KEY_TIMELINE_COMPRESS_ITEM_TIME_2_TO3"), },
						{ &settings.Input.Timeline_CompressItemTime_3To4, UI_Str("SETTINGS_KEY_TIMELINE_COMPRESS_ITEM_TIME_3_TO4"), },
						{ &settings.Input.Timeline_CompressItemTime_0To1, UI_Str("SETTINGS_KEY_TIMELINE_COMPRESS_ITEM_TIME_0_TO1"), },
						{ &settings.Input.Timeline_QuantizeItemTime_1To1, UI_Str("SETTINGS_KEY_TIMELINE_QUANTIZE_ITEM_TIME_1_TO1"), },
						{ &settings.Input.Timeline_ConvertRangeToDelay, UI_Str("SETTINGS_KEY_CONVERT_RANGE_DELAY"), },
						{ &settings.Input.Timeline_ReverseItemTime_N1To1, UI_Str("SETTINGS_KEY_TIMELINE_REVERSE_ITEM_TIME_N1_TO1"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomA, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_A"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomB, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_B"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomC, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_C"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomD, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_D"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomE, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_E"), },
						{ &settings.Input.Timeline_ScaleItemTime_CustomF, UI_Str("SETTINGS_KEY_TIMELINE_SCALE_ITEM_TIME_CUSTOM_F"), },
						{ &settings.Input.Timeline_StepCursorLeft, UI_Str("SETTINGS_KEY_TIMELINE_STEP_CURSOR_LEFT"), },
						{ &settings.Input.Timeline_StepCursorRight, UI_Str("SETTINGS_KEY_TIMELINE_STEP_CURSOR_RIGHT"), },
						{ &settings.Input.Timeline_JumpToTimelineStart, UI_Str("SETTINGS_KEY_TIMELINE_JUMP_TO_TIMELINE_START"), },
						{ &settings.Input.Timeline_JumpToTimelineEnd, UI_Str("SETTINGS_KEY_TIMELINE_JUMP_TO_TIMELINE_END"), },
						{ &settings.Input.Timeline_IncreaseGridDivision, UI_Str("SETTINGS_KEY_TIMELINE_INCREASE_GRID_DIVISION"), },
						{ &settings.Input.Timeline_DecreaseGridDivision, UI_Str("SETTINGS_KEY_TIMELINE_DECREASE_GRID_DIVISION"), },
						{ &settings.Input.Timeline_SetGridDivision_1_4, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_4"), },
						{ &settings.Input.Timeline_SetGridDivision_1_5, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_5"), },
						{ &settings.Input.Timeline_SetGridDivision_1_6, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_6"), },
						{ &settings.Input.Timeline_SetGridDivision_1_7, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_7"), },
						{ &settings.Input.Timeline_SetGridDivision_1_8, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_8"), },
						{ &settings.Input.Timeline_SetGridDivision_1_9, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_9"), },
						{ &settings.Input.Timeline_SetGridDivision_1_10, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_10"), },
						{ &settings.Input.Timeline_SetGridDivision_1_12, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_12"), },
						{ &settings.Input.Timeline_SetGridDivision_1_14, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_14"), },
						{ &settings.Input.Timeline_SetGridDivision_1_16, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_16"), },
						{ &settings.Input.Timeline_SetGridDivision_1_18, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_18"), },
						{ &settings.Input.Timeline_SetGridDivision_1_20, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_20"), },
						{ &settings.Input.Timeline_SetGridDivision_1_24, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_24"), },
						{ &settings.Input.Timeline_SetGridDivision_1_28, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_28"), },
						{ &settings.Input.Timeline_SetGridDivision_1_32, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_32"), },
						{ &settings.Input.Timeline_SetGridDivision_1_36, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_36"), },
						{ &settings.Input.Timeline_SetGridDivision_1_40, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_40"), },
						{ &settings.Input.Timeline_SetGridDivision_1_48, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_48"), },
						{ &settings.Input.Timeline_SetGridDivision_1_64, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_64"), },
						{ &settings.Input.Timeline_SetGridDivision_1_96, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_96"), },
						{ &settings.Input.Timeline_SetGridDivision_1_192, UI_Str("SETTINGS_KEY_TIMELINE_SET_GRID_DIVISION_1_192"), },
						{ &settings.Input.Timeline_IncreasePlaybackSpeed, UI_Str("SETTINGS_KEY_TIMELINE_INCREASE_PLAYBACK_SPEED"), },
						{ &settings.Input.Timeline_DecreasePlaybackSpeed, UI_Str("SETTINGS_KEY_TIMELINE_DECREASE_PLAYBACK_SPEED"), },
						{ &settings.Input.Timeline_SetPlaybackSpeed_100, UI_Str("SETTINGS_KEY_TIMELINE_SET_PLAYBACK_SPEED_100"), },
						{ &settings.Input.Timeline_SetPlaybackSpeed_75, UI_Str("SETTINGS_KEY_TIMELINE_SET_PLAYBACK_SPEED_75"), },
						{ &settings.Input.Timeline_SetPlaybackSpeed_50, UI_Str("SETTINGS_KEY_TIMELINE_SET_PLAYBACK_SPEED_50"), },
						{ &settings.Input.Timeline_SetPlaybackSpeed_25, UI_Str("SETTINGS_KEY_TIMELINE_SET_PLAYBACK_SPEED_25"), },
						{ &settings.Input.Timeline_TogglePlayback, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_PLAYBACK"), },
						{ &settings.Input.Timeline_TogglePlaybackCursorFollow, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_PLAYBACK_CURSOR_FOLLOW"), },
						{ &settings.Input.Timeline_ToggleAutoStepAfterNoteInput, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_AUTO_STEP_AFTER_NOTE_INPUT"), },
						{ &settings.Input.Timeline_ToggleGridStartsAtBar, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_GRID_STARTS_AT_BAR"), },
						{ &settings.Input.Timeline_ToggleLoopPlayback, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_LOOP_PLAYBACK"), },
						{ &settings.Input.Timeline_ToggleMetronome, UI_Str("SETTINGS_KEY_TIMELINE_TOGGLE_METRONOME"), },
						{},
						{ &settings.Input.TestPlay_DonLeft, UI_Str("SETTINGS_KEY_TEST_PLAY_DON_LEFT"), true },
						{ &settings.Input.TestPlay_DonRight, UI_Str("SETTINGS_KEY_TEST_PLAY_DON_RIGHT"), true },
						{ &settings.Input.TestPlay_KaLeft, UI_Str("SETTINGS_KEY_TEST_PLAY_KA_LEFT"), true },
						{ &settings.Input.TestPlay_KaRight, UI_Str("SETTINGS_KEY_TEST_PLAY_KA_RIGHT"), true },
						{ &settings.Input.TestPlay_StartBeginning, UI_Str("SETTINGS_KEY_TEST_PLAY_START_BEGINNING"), },
						{ &settings.Input.TestPlay_StartCurrent, UI_Str("SETTINGS_KEY_TEST_PLAY_START_CURRENT"), },
						{ &settings.Input.TestPlay_StartMarker, UI_Str("SETTINGS_KEY_TEST_PLAY_START_MARKER"), },
						{ &settings.Input.TestPlay_TogglePause, UI_Str("SETTINGS_KEY_TEST_PLAY_PAUSE"), },
						{ &settings.Input.TestPlay_Retry, UI_Str("SETTINGS_KEY_TEST_PLAY_RETRY"), },
						{ &settings.Input.TestPlay_Exit, UI_Str("SETTINGS_KEY_TEST_PLAY_EXIT"), },
						{},
						{ &settings.Input.TempoCalculator_Tap, UI_Str("SETTINGS_KEY_TEMPO_CALCULATOR_TAP"), },
						{ &settings.Input.TempoCalculator_Reset, UI_Str("SETTINGS_KEY_TEMPO_CALCULATOR_RESET"), },
						{},
						{ &settings.Input.Dialog_YesOrOk, UI_Str("SETTINGS_KEY_DIALOG_YES_OR_OK"), },
						{ &settings.Input.Dialog_No, UI_Str("SETTINGS_KEY_DIALOG_NO"), },
						{ &settings.Input.Dialog_Cancel, UI_Str("SETTINGS_KEY_DIALOG_CANCEL"), },
						{ &settings.Input.Dialog_SelectNextTab, UI_Str("SETTINGS_KEY_DIALOG_SELECT_NEXT_TAB"), },
						{ &settings.Input.Dialog_SelectPreviousTab, UI_Str("SETTINGS_KEY_DIALOG_SELECT_PREVIOUS_TAB"), },
					};

					changesWereMade |= SettingsGui::DrawInputEntriesListTableGui(settingsEntriesInput, ArrayCount(settingsEntriesInput), &settingsFilterInput, inputState, sectionNavigation);
				}
				Gui::PopStyleVar();
			}

			sectionNavigation.RequestedSection.clear();
		}
		Gui::EndChild();
		Gui::PopID();

		return changesWereMade;
	}
}
