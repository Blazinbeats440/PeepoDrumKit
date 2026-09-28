#include "core_types.h"
#include "core_string.h"
#include "chart_editor.h"
#include "chart_editor_settings.h"
#include "chart_editor_i18n.h"
#include "core_log.h"

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
			std::string_view(messageBuffer, messageLength), "Peepo Drum Kit - Syntax Error",
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
				VideoExportWriter writer;
				if (!writer.Start("build/video-writer-self-test.mp4", 640, 360, 30, 2000000))
				{
					Log::Write("Video writer self-test setup failed: %s", writer.GetError().data());
					return 1;
				}
				std::vector<u8> pixels(640 * 360 * 4, 0);
				std::vector<i16> samples(1470 * 2, 0);
				for (i32 frame = 0; frame < 30; ++frame)
				{
					for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
					{
						pixels[pixel + 0] = static_cast<u8>(frame * 8);
						pixels[pixel + 1] = 64;
						pixels[pixel + 2] = 128;
						pixels[pixel + 3] = 255;
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
