#include "core_types.h"
#include "core_string.h"
#include "chart_editor.h"
#include "chart_editor_settings.h"
#include "chart_editor_i18n.h"
#include "core_log.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wrl/client.h>

namespace PeepoDrumKit
{
	struct ImGuiApplication
	{
		ChartEditor ChartEditor = {};

		ImGuiApplication()
		{
			auto& io = Gui::GetIO();
			io.ConfigWindowsMoveFromTitleBarOnly = true;
			io.KeyRepeatDelay = 0.450f;
			io.KeyRepeatRate = 0.050f;

			auto& style = Gui::GetStyle();
			style.CellPadding.y = 4.0f;
			style.FramePadding.y = 4.0f;
			// NOTE: The one downside of disabling this is that is makes tab bars and docked windows look the same
			style.WindowMenuButtonPosition = ImGuiDir_None;

			GuiStyleColorPeepoDrumKit(&style);
		}

		void OnUpdate()
		{
			static constexpr ImGuiWindowFlags fullscreenWindowFlags =
				ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
				ImGuiWindowFlags_NoBackground;

			const ImGuiViewport* mainViewport = Gui::GetMainViewport();
			Gui::SetNextWindowPos(mainViewport->WorkPos);
			Gui::SetNextWindowSize(mainViewport->WorkSize);
			Gui::SetNextWindowViewport(mainViewport->ID);

			Gui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			Gui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			Gui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
			Gui::Begin("FullscreenDockSpaceWindow", nullptr, fullscreenWindowFlags);
			Gui::PopStyleVar(3);
			{
				const ImGuiID dockSpaceID = Gui::GetID("FullscreenDockSpace");
				if (Gui::DockBuilderGetNode(dockSpaceID) == nullptr)
					ChartEditor.RestoreDefaultDockSpaceLayout(dockSpaceID);

				Gui::DockSpace(dockSpaceID, { 0.0f, 0.0f }, ImGuiDockNodeFlags_PassthruCentralNode);
				ChartEditor.DrawFullscreenMenuBar();
			}
			Gui::End();

			ChartEditor.DrawGui();
		}

		ApplicationHost::CloseResponse OnWindowCloseRequest()
		{
			return ChartEditor.OnWindowCloseRequest();
		}
	};

	enum class LoadSettingsResponse { FileNotFound, AllGood, ErrorAbort, ErrorRetry, ErrorIgnore };

	template <typename T>
	static LoadSettingsResponse ReadParseSettingsIniFile(cstr iniFilePath, T& out)
	{
		auto fileContent = File::ReadAllBytes(iniFilePath);
		if (fileContent.Content == nullptr || fileContent.Size <= 0)
			return LoadSettingsResponse::AllGood;

		const SettingsParseResult parseResult = ParseSettingsIni(std::string_view(reinterpret_cast<const char*>(fileContent.Content.get()), fileContent.Size), out);
		if (!parseResult.HasError)
			return LoadSettingsResponse::AllGood;

		out.ResetDefault();

		char messageBuffer[2048];
		const int messageLength = sprintf_s(messageBuffer,
			"Failed to parse settings file:\n"
			"\"%s\"\n"
			"\n"
			"Syntax Error '%s' on Line %d\n"
			"\n"
			"[ Abort ] to exit application\n"
			"[ Retry ]  to read file again\n"
			"[ Ignore ] to *LOSE ALL SETTINGS* and restore the defaults\n"
			, iniFilePath, !parseResult.ErrorMessage.empty() ? parseResult.ErrorMessage.c_str() : "Unknown", parseResult.ErrorLineIndex + 1);

		const Shell::MessageBoxResult result = Shell::ShowMessageBox(
			std::string_view(messageBuffer, messageLength), "PeepoDrumKit Ver.B - Syntax Error",
			Shell::MessageBoxButtons::AbortRetryIgnore, Shell::MessageBoxIcon::Warning, nullptr);

		if (result == Shell::MessageBoxResult::Abort) return LoadSettingsResponse::ErrorAbort;
		if (result == Shell::MessageBoxResult::Retry) return LoadSettingsResponse::ErrorRetry;
		if (result == Shell::MessageBoxResult::Ignore) return LoadSettingsResponse::ErrorIgnore;
		assert(!"Unreachable"); return LoadSettingsResponse::ErrorAbort;
	}

	int EntryPoint()
	{
		Log::Write("EntryPoint begin");
		auto [argc, argv] = CommandLine::GetCommandLineUTF8();
		for (size_t i = 1; i < argc; i++)
		{
			if (argv[i] == "--test-d3d11-recovery")
				return ApplicationHost::RunD3D11DeviceRecoverySelfTest() ? 0 : 1;
			if (argv[i] == "--test-aac-capabilities")
			{
				if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
				defer { CoUninitialize(); };
				if (FAILED(MFStartup(MF_VERSION))) return 1;
				defer { MFShutdown(); };
				MFT_REGISTER_TYPE_INFO outputFilter { MFMediaType_Audio, MFAudioFormat_AAC };
				IMFActivate** encoders = nullptr;
				UINT32 count = 0;
				if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
					nullptr, &outputFilter, &encoders, &count))) return 1;
				defer { for (UINT32 index = 0; index < count; ++index) encoders[index]->Release(); CoTaskMemFree(encoders); };
				if (count == 0) return 1;
				Microsoft::WRL::ComPtr<IMFTransform> encoder;
				if (FAILED(encoders[0]->ActivateObject(IID_PPV_ARGS(encoder.GetAddressOf())))) return 1;
				for (DWORD index = 0;; ++index)
				{
					Microsoft::WRL::ComPtr<IMFMediaType> type;
					const HRESULT result = encoder->GetOutputAvailableType(0, index, type.GetAddressOf());
					if (result == MF_E_NO_MORE_TYPES) break;
					if (FAILED(result)) return 1;
					UINT32 sampleRate = 0, channels = 0, bytesPerSecond = 0;
					if (FAILED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)) ||
						FAILED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
						FAILED(type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond))) return 1;
					if (channels == 2 && (sampleRate == 44100 || sampleRate == 48000))
						Log::Write("AAC supported stereo format: %u Hz, %u kbps", sampleRate, bytesPerSecond * 8 / 1000);
				}
				for (UINT32 bitRate : { 256000u, 320000u })
				{
					Microsoft::WRL::ComPtr<IMFMediaType> type;
					if (FAILED(MFCreateMediaType(type.GetAddressOf())) ||
						FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
						FAILED(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
						FAILED(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bitRate / 8))) return 1;
					const HRESULT result = encoder->SetOutputType(0, type.Get(), MFT_SET_TYPE_TEST_ONLY);
					Log::Write("AAC stereo %u kbps probe: HRESULT 0x%08X", bitRate / 1000, static_cast<u32>(result));
				}
				return 0;
			}
			if (argv[i] == "--test-video-branches")
			{
				std::string error;
				if (!RunVideoBranchSelfTest(error))
				{
					Log::Write("Video branch self-test failed: %s", error.c_str());
					return 1;
				}
				Log::Write("Video branch self-test passed");
				return 0;
			}
			if (argv[i] == "--test-video-layout")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!RunVideoLayoutSelfTest(error))
				{
					Log::Write("Video layout self-test failed: %s", error.c_str());
					return 1;
				}
				Log::Write("Video layout self-test passed");
				return 0;
			}
			if (argv[i] == "--test-screenshot-png")
			{
				const std::vector<u8> pixels = { 0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255 };
				std::string firstPath, secondPath;
				if (!SaveScreenshotPNG(2, 2, pixels, firstPath) || !SaveScreenshotPNG(2, 2, pixels, secondPath) || firstPath == secondPath)
				{
					Log::Write("Screenshot PNG self-test failed");
					return 1;
				}
				Log::Write("Screenshot PNG self-test passed: %s, %s", firstPath.c_str(), secondPath.c_str());
				return 0;
			}
			if (argv[i] == "--test-video-writer")
			{
				Audio::PCMSampleBuffer hitSound {};
				hitSound.ChannelCount = 1;
				hitSound.SampleRate = Audio::Engine.OutputSampleRate;
				hitSound.FrameCount = 2;
				hitSound.InterleavedSamples = std::make_unique<i16[]>(2);
				hitSound.InterleavedSamples[0] = 1000;
				hitSound.InterleavedSamples[1] = 500;
				VideoExportAudioSources testSources;
				testSources.SoundEffects[EnumToIndex(SoundEffectType::TaikoDon)] = &hitSound;
				VideoExportSoundTimeline testSounds;
				testSounds.Events.push_back({ Time::FromSec(2.0 / Audio::Engine.OutputSampleRate), SoundEffectType::TaikoDon });
				std::vector<i16> mixedSamples;
				RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
				if (mixedSamples.size() != 10 || mixedSamples[2 * 2] != 1000 || mixedSamples[2 * 2 + 1] != 1000 ||
					mixedSamples[3 * 2] != 500 || mixedSamples[0] != 0 || mixedSamples[4 * 2] != 0)
				{
					Log::Write("Video writer self-test audio mixing failed");
					return 1;
				}
				for (f32 pan : { -1.0f, 1.0f })
				{
					testSounds.Events[0].Pan = pan;
					RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
					const auto gain = Audio::GetPanGain(pan, Audio::AudioEngine::PanLaw);
					for (u32 channel = 0; channel < 2; ++channel)
						if (mixedSamples[4 + channel] != static_cast<i16>(std::round(1000.0f * gain[channel])))
						{
							Log::Write("Video writer self-test stereo balance failed");
							return 1;
						}
				}
				testSounds.Events[0].Pan = -1.0f;
				testSounds.Events.push_back({ testSounds.Events[0].HitTime, SoundEffectType::TaikoDon, 1.0f });
				RenderVideoExportAudio(testSources, testSounds, Time::Zero(), 0, 5, mixedSamples);
				if (mixedSamples[4] != 1414 || mixedSamples[5] != 1414)
				{
					Log::Write("Video writer self-test simultaneous stereo hits failed");
					return 1;
				}
				VideoExportAudioSources fadeSources;
				fadeSources.Song = &hitSound;
				fadeSources.ContentStart = Time::FromSec(1.0);
				fadeSources.ContentEnd = Time::FromSec(2.0);
				fadeSources.FadeInSeconds = fadeSources.FadeOutSeconds = 1.0f;
				const f64 fadeTimes[] = { 0.0, 0.45, 0.9, 1.5, 2.2, 2.6, 3.0 };
				const i32 expectedLevels[] = { 0, 750, 1000, 1000, 1000, 750, 0 };
				for (size_t sample = 0; sample < ArrayCount(fadeTimes); ++sample)
				{
					fadeSources.SongOffset = Time::FromSec(fadeTimes[sample]);
					RenderVideoExportAudio(fadeSources, {}, fadeSources.SongOffset, 0, 1, mixedSamples);
					if (std::abs(static_cast<i32>(mixedSamples[0]) - expectedLevels[sample]) > 1 || mixedSamples[0] != mixedSamples[1])
					{
						Log::Write("Video writer self-test audio fade failed at %g seconds", fadeTimes[sample]);
						return 1;
					}
				}
				fadeSources.FadeInSeconds = fadeSources.FadeOutSeconds = 0.0f;
				RenderVideoExportAudio(fadeSources, {}, fadeSources.SongOffset, 0, 1, mixedSamples);
				if (mixedSamples[0] != 1000 || mixedSamples[1] != 1000) return 1;
				std::vector<u8> pixels(640 * 360 * 4, 0);
				std::vector<i16> samples(1470 * 2, 0);
				for (const auto& preset : VideoAudioBitRatePresets)
				{
					PersistentAppData settings, restored;
					settings.VideoExport.AudioBitRate = preset.BitRate;
					std::string ini;
					SettingsToIni(settings, ini);
					if (ParseSettingsIni(ini, restored).HasError || restored.VideoExport.AudioBitRate != preset.BitRate) return 1;
					VideoExportWriter writer;
					const std::string outputPath = "build/video-writer-self-test-" + std::to_string(preset.BitRate / 1000) + ".mp4";
					if (!writer.Start(outputPath, 640, 360, 30, 2000000, preset.BitRate))
					{
						Log::Write("Video writer self-test setup failed at %u bps: %s", preset.BitRate, writer.GetError().data());
						return 1;
					}
					for (i32 frame = 0; frame < 30; ++frame)
					{
						for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
						{
							pixels[pixel + 0] = static_cast<u8>(frame * 8);
							pixels[pixel + 1] = 64;
							pixels[pixel + 2] = 128;
							pixels[pixel + 3] = 255;
						}
						for (i32 sample = 0; sample < 1470; ++sample)
						{
							const i16 value = static_cast<i16>(10000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * (frame * 1470 + sample) / 44100.0));
							samples[sample * 2] = samples[sample * 2 + 1] = value;
						}
						if (!writer.WriteVideoFrame(pixels.data(), 640 * 4, frame) ||
							!writer.WriteAudioSamples(samples.data(), 1470, frame * 1470))
						{
							Log::Write("Video writer self-test frame failed: %s", writer.GetError().data());
							return 1;
						}
					}
					if (!writer.Finish())
					{
						Log::Write("Video writer self-test finalize failed: %s", writer.GetError().data());
						return 1;
					}
					if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
					defer { CoUninitialize(); };
					if (FAILED(MFStartup(MF_VERSION))) return 1;
					defer { MFShutdown(); };
					Microsoft::WRL::ComPtr<IMFSourceReader> reader;
					Microsoft::WRL::ComPtr<IMFMediaType> audioType;
					if (FAILED(MFCreateSourceReaderFromURL(UTF8::WideArg(outputPath).c_str(), nullptr, reader.GetAddressOf())) ||
						FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, audioType.GetAddressOf()))) return 1;
					UINT32 bytesPerSecond = 0, sampleRate = 0, channels = 0;
					GUID subtype = {};
					if (FAILED(audioType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond)) || bytesPerSecond * 8 != preset.BitRate ||
						FAILED(audioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)) || sampleRate != 44100 ||
						FAILED(audioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) || channels != 2 ||
						FAILED(audioType->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFAudioFormat_AAC)
					{
						Log::Write("Video writer self-test audio format mismatch: requested %u, got %u bps", preset.BitRate, bytesPerSecond * 8);
						return 1;
					}
					Log::Write("Video writer self-test verified %u kbps AAC stereo at 44100 Hz", preset.BitRate / 1000);
				}
				PersistentAppData legacySettings;
				if (ParseSettingsIni("[video_export]\nframes_per_second = 60\n", legacySettings).HasError || legacySettings.VideoExport.AudioBitRate != 192000) return 1;
				VideoExportWriter invalidWriter;
				if (invalidWriter.Start("build/video-writer-invalid.mp4", 640, 360, 30, 2000000, 0) || invalidWriter.GetError().empty()) return 1;
				Log::Write("Video writer self-test passed");
				return 0;
			}
			if (argv[i] == "--test-tja-branches")
			{
				std::string error;
				if (RunTJAChartBranchSelfTest(error))
				{
					Log::Write("TJA branch self-test passed");
					return 0;
				}
				Log::Write("TJA branch self-test failed: %s", error.c_str());
				return 1;
			}
			if (argv[i] == "--test-timeline-delay")
			{
				i18n::InitBuiltinLocale();
				std::string error;
				if (!ChartTimeline::RunPlaybackTimelineSelfTest(error))
				{
					Log::Write("Timeline DELAY self-test failed: %s", error.c_str());
					return 1;
				}
				Log::Write("Timeline DELAY self-test passed");
				return 0;
			}
		}

		while (true)
		{
			const auto response = ReadParseSettingsIniFile<PersistentAppData>(PersistentAppIniFileName, PersistentApp);
			if (response == LoadSettingsResponse::FileNotFound) { break; }
			if (response == LoadSettingsResponse::AllGood) { break; }
			if (response == LoadSettingsResponse::ErrorAbort) { return -1; }
			if (response == LoadSettingsResponse::ErrorRetry) { continue; }
			if (response == LoadSettingsResponse::ErrorIgnore) { break; }
			assert(!"Unreachable"); break;
		}

		while (true)
		{
			// TODO: Also set IsDirty in case of (older) version mismatch (?)
			const auto response = ReadParseSettingsIniFile<UserSettingsData>(SettingsIniFileName, Settings_Mutable);
			if (response == LoadSettingsResponse::FileNotFound) { Settings_Mutable.IsDirty = true; break; }
			if (response == LoadSettingsResponse::AllGood) { break; }
			if (response == LoadSettingsResponse::ErrorAbort) { return -1; }
			if (response == LoadSettingsResponse::ErrorRetry) { continue; }
			if (response == LoadSettingsResponse::ErrorIgnore) { break; }
			assert(!"Unreachable"); break;
		}

		if (!Settings.General.FolderDropSearchDepth.HasValue)
			Settings_Mutable.IsDirty = true;

		static std::unique_ptr<ImGuiApplication> app;
		ApplicationHost::StartupParam startupParam = {};
		ApplicationHost::UserCallbacks callbacks = {};

		GuiScaleFactorTarget = ClampRoundGuiScaleFactor(PersistentApp.LastSession.GuiScale);
		SelectedGuiLanguage = PersistentApp.LastSession.GuiLanguage;
		SelectedGuiLanguageTJA = ASCII::IETFLangTagToTJALangTag(SelectedGuiLanguage);

		ApplicationHost::GlobalState.SwapInterval = PersistentApp.LastSession.OSWindow_SwapInterval;
		ApplicationHost::GlobalState.VSyncOffFPSLimit = Max(0, *Settings.General.VSyncOffFPSLimit);
		startupParam.WaitableSwapChain = false;
		startupParam.AllowSwapChainTearing = true;
		startupParam.WindowTitle = PeepoDrumKitApplicationTitle;
		// TODO: ...
		// startupParam.WindowPosition = ...;
		// startupParam.WindowSize = ...;

		callbacks.OnStartup = []
		{
			Log::Write("User startup begin");
			i18n::RefreshLocales();
			i18n::InitBuiltinLocale();
			i18n::ReloadLocaleFile(SelectedGuiLanguage.c_str());
			Audio::Engine.ApplicationStartup();
			app = std::make_unique<ImGuiApplication>();
			Log::Write("User startup complete");
		};
		callbacks.OnBeforeUpdate = []
		{
			app->OnUpdate();
		};
		callbacks.OnUpdate = []
		{
			ApplicationHost::GlobalState.VSyncOffFPSLimit = Max(0, *Settings.General.VSyncOffFPSLimit);
			app->OnUpdate();
		};
		callbacks.OnAfterRender = [] { app->ChartEditor.OnAfterRender(); };
		callbacks.OnShutdown = []
		{
			Log::Write("User shutdown begin");
			app = nullptr;
			Audio::Engine.ApplicationShutdown();

			PersistentApp.LastSession.GuiScale = GuiScaleFactorTarget;
			PersistentApp.LastSession.GuiLanguage = SelectedGuiLanguage;
			PersistentApp.LastSession.OSWindow_SwapInterval = ApplicationHost::GlobalState.SwapInterval;
			PersistentApp.LastSession.OSWindow_Region = Rect::FromTLSize(vec2(ApplicationHost::GlobalState.WindowPosition), vec2(ApplicationHost::GlobalState.WindowSize));
			// TODO: PersistentApp.LastSession.OSWindow_RegionRestore = ...;
			PersistentApp.LastSession.OSWindow_IsFullscreen = ApplicationHost::GlobalState.IsBorderlessFullscreen;
			// TODO: PersistentApp.LastSession.OSWindow_IsMaximized = ...;

			std::string iniFileContent; iniFileContent.reserve(4096);
			SettingsToIni(PersistentApp, iniFileContent); File::WriteAllBytes(PersistentAppIniFileName, iniFileContent);

			if (Settings.IsDirty)
			{
				iniFileContent.clear();
				SettingsToIni(Settings, iniFileContent); File::WriteAllBytes(SettingsIniFileName, iniFileContent);
				Settings_Mutable.IsDirty = false;
			}
			Log::Write("User shutdown complete");
		};
		callbacks.OnWindowCloseRequest = []
		{
			return app->OnWindowCloseRequest();
		};

		return ApplicationHost::EnterProgramLoop(startupParam, callbacks);
	}
}

#if 1
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
static void Win32SetupConsoleMagic()
{
	::SetConsoleOutputCP(CP_UTF8);
	::_setmode(::_fileno(stdout), _O_BINARY);
	// TODO: Maybe overwrite the current locale too (?)
}
#else
static void Win32SetupConsoleMagic() { return; }
#endif

#if PEEPO_DEBUG
int main(int, const char**) { Win32SetupConsoleMagic(); Log::Initialize(); Log::InstallCrashHandler(); const int result = PeepoDrumKit::EntryPoint(); Log::Shutdown(); return result; }
#elif PEEPO_RELEASE

#if 1
#include <Windows.h>
int WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { Win32SetupConsoleMagic(); Log::Initialize(); Log::InstallCrashHandler(); const int result = PeepoDrumKit::EntryPoint(); Log::Shutdown(); return result; }
#else
int WinMain(void*, void*, const wchar_t*, int) { Win32SetupConsoleMagic(); return PeepoDrumKit::EntryPoint(); }
#endif

#else
#error "Neither PEEPO_DEBUG nor PEEPO_RELEASE are defined :monkaS:"
#endif
