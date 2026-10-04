#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "peepo_drum_kit/chart_editor_background_movie.h"
#include "peepo_drum_kit/chart_editor_video_writer.h"
#include "peepo_drum_kit/chart_editor_settings.h"
#include "file_format_tja.h"
#include "imgui/backend/imgui_custom_draw.h"
#include "imgui/backend/imgui_impl_d3d11.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>

using namespace PeepoDrumKit;
using Microsoft::WRL::ComPtr;

static void Check(b8 condition, const char* message)
{
	if (!condition) { std::cerr << message << std::endl; std::exit(1); }
}

static BackgroundMovieStatus WaitForFrame(BackgroundMovieReader& reader, BackgroundMovieFrame& frame)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	for (;;)
	{
		const auto status = reader.Poll(frame);
		if (status != BackgroundMovieStatus::Pending) return status;
		Check(std::chrono::steady_clock::now() < deadline, "Video decoding timed out");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

static std::string CreateMovie(u32 width, u32 height, b8 longAudio = false)
{
	const std::string path = "build/bgmovie_test_" + std::to_string(GetCurrentProcessId()) + "_" +
		std::to_string(GetTickCount64()) + u8"_\u80cc\u666f.mp4";
	VideoExportWriter writer;
	Check(writer.Start(path, width, height, 5, 500000, 192000), std::string(writer.GetError()).c_str());
	std::vector<u8> pixels(static_cast<size_t>(width) * height * 4);
	std::vector<i16> silence(8820 * 2);
	for (i64 index = 0; index < 15; ++index)
	{
		for (u32 row = 0; row < height; ++row)
			for (u32 column = 0; column < width; ++column)
			{
				u8* pixel = pixels.data() + (static_cast<size_t>(row) * width + column) * 4;
				pixel[0] = row < height / 2 ? 20 : 220;
				pixel[1] = static_cast<u8>(30 + index * 10);
				pixel[2] = row < height / 2 ? 220 : 20;
				pixel[3] = 255;
			}
		Check(writer.WriteVideoFrame(pixels.data(), width * 4, index), std::string(writer.GetError()).c_str());
		Check(writer.WriteAudioSamples(silence.data(), 8820, index * 8820), std::string(writer.GetError()).c_str());
	}
	if (longAudio)
		for (i64 index = 15; index < 25; ++index)
			Check(writer.WriteAudioSamples(silence.data(), 8820, index * 8820), std::string(writer.GetError()).c_str());
	Check(writer.Finish(), std::string(writer.GetError()).c_str());
	return path;
}

static std::string CreateVariableMovie()
{
	const std::string path = "build/bgmovie_vfr_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()) + ".mp4";
	Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize test COM");
	Check(SUCCEEDED(MFStartup(MF_VERSION)), "Initialize test Media Foundation");
	defer { MFShutdown(); CoUninitialize(); };
	ComPtr<IMFAttributes> attributes;
	ComPtr<IMFSinkWriter> writer;
	ComPtr<IMFMediaType> output, input;
	DWORD stream = 0;
	Check(SUCCEEDED(MFCreateAttributes(&attributes, 1)) && SUCCEEDED(attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE)), "Disable test frame rate conversion");
	Check(SUCCEEDED(MFCreateSinkWriterFromURL(UTF8::WideArg(path).c_str(), nullptr, attributes.Get(), &writer)), "Create variable-rate test writer");
	Check(SUCCEEDED(MFCreateMediaType(&output)) && SUCCEEDED(MFCreateMediaType(&input)), "Create test video types");
	for (auto* type : { output.Get(), input.Get() })
	{
		Check(SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
			SUCCEEDED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) &&
			SUCCEEDED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, 66, 48)) &&
			SUCCEEDED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, 5, 1)) &&
			SUCCEEDED(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1)), "Configure test video dimensions");
	}
	Check(SUCCEEDED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) && SUCCEEDED(output->SetUINT32(MF_MT_AVG_BITRATE, 500000)) &&
		SUCCEEDED(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) && SUCCEEDED(input->SetUINT32(MF_MT_DEFAULT_STRIDE, 66)), "Configure NV12 test encoder");
	Check(SUCCEEDED(writer->AddStream(output.Get(), &stream)) && SUCCEEDED(writer->SetInputMediaType(stream, input.Get(), nullptr)) &&
		SUCCEEDED(writer->BeginWriting()), "Start variable-rate test writer");
	const LONGLONG times[] = { 0, 2000000, 6000000, 14000000, 22000000, 28000000, 30000000 };
	for (size_t index = 0; index < 6; ++index)
	{
		ComPtr<IMFMediaBuffer> buffer;
		ComPtr<IMFSample> sample;
		constexpr DWORD bytes = 66 * 48 * 3 / 2;
		Check(SUCCEEDED(MFCreateMemoryBuffer(bytes, &buffer)) && SUCCEEDED(MFCreateSample(&sample)), "Create variable-rate frame");
		BYTE* pixels = nullptr;
		Check(SUCCEEDED(buffer->Lock(&pixels, nullptr, nullptr)), "Lock variable-rate frame");
		memset(pixels, 30 + static_cast<int>(index) * 25, 66 * 48);
		memset(pixels + 66 * 48, 128, 66 * 48 / 2);
		buffer->Unlock();
		Check(SUCCEEDED(buffer->SetCurrentLength(bytes)) && SUCCEEDED(sample->AddBuffer(buffer.Get())) &&
			SUCCEEDED(sample->SetSampleTime(times[index])) && SUCCEEDED(sample->SetSampleDuration(times[index + 1] - times[index])) &&
			SUCCEEDED(writer->WriteSample(stream, sample.Get())), "Write variable-rate frame");
	}
	Check(SUCCEEDED(writer->Finalize()), "Finalize variable-rate movie");
	return path;
}

static void CheckDynamicTexture(const BackgroundMovieFrame& frame)
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "Create offscreen D3D11 device");
	ImGui::CreateContext();
	Check(ImGui_ImplDX11_Init(device.Get(), context.Get()), "Initialize D3D11 texture backend");
	defer { ImGui_ImplDX11_Shutdown(); ImGui::DestroyContext(); };
	CustomDraw::GPUTexture texture;
	texture.Load({ CustomDraw::GPUPixelFormat::BGRA, CustomDraw::GPUAccessType::Dynamic, frame.Size, frame.Pixels.data() });
	Check(texture.IsValid(), "Create dynamic movie texture");
	defer { texture.Unload(); };
	std::vector<u8> pixels = frame.Pixels;
	for (size_t index = 0; index < pixels.size(); ++index) pixels[index] = static_cast<u8>(index % 251);
	texture.UpdateDynamic(frame.Size, pixels.data());
	ComPtr<ID3D11Resource> resource;
	reinterpret_cast<ID3D11ShaderResourceView*>(texture.GetTexID())->GetResource(&resource);
	ComPtr<ID3D11Texture2D> source, staging;
	Check(SUCCEEDED(resource.As(&source)), "Read dynamic texture resource");
	D3D11_TEXTURE2D_DESC description = {};
	source->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	Check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &staging)), "Create texture readback buffer");
	context->CopyResource(staging.Get(), source.Get());
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "Read back movie texture");
	defer { context->Unmap(staging.Get(), 0); };
	const size_t rowBytes = static_cast<size_t>(frame.Size.x) * 4;
	for (i32 row = 0; row < frame.Size.y; ++row)
		Check(memcmp(static_cast<const u8*>(mapped.pData) + mapped.RowPitch * row, pixels.data() + rowBytes * row, rowBytes) == 0, "Dynamic texture must preserve every row with GPU padding");
}

int main()
{
	UserSettingsData settings;
	Check(!*settings.General.GamePreviewUseBackgroundMovie && !*settings.TestPlay.UseBackgroundMovie, "Both movie settings must default to off");
	const auto settingsResult = ParseSettingsIni("[general]\ngame_preview_use_background_movie = true\n[test_play]\nuse_background_movie = false\n", settings);
	Check(!settingsResult.HasError && *settings.General.GamePreviewUseBackgroundMovie && !*settings.TestPlay.UseBackgroundMovie, "Preview and test play settings must be independent");
	settings.General.GamePreviewUseBackgroundMovie.Value = false;
	settings.General.GamePreviewUseBackgroundMovie.SetHasValueIfNotDefault();
	settings.TestPlay.UseBackgroundMovie.Value = true;
	settings.TestPlay.UseBackgroundMovie.SetHasValueIfNotDefault();
	std::string settingsText;
	SettingsToIni(settings, settingsText);
	UserSettingsData reloadedSettings;
	Check(!ParseSettingsIni(settingsText, reloadedSettings).HasError && !*reloadedSettings.General.GamePreviewUseBackgroundMovie && *reloadedSettings.TestPlay.UseBackgroundMovie, "Movie settings must survive serialization");
	TJA::ErrorList errors;
	const std::string input = "TITLE:Movie\nBGMOVIE:background.mp4\nMOVIEOFFSET:-1.25\nOFFSET:0.5\nCOURSE:Oni\n#START\n1000,\n#END\n";
	auto parsed = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(input)), errors);
	Check(errors.Errors.empty(), "BGMOVIE headers should parse");
	std::string saved;
	TJA::ConvertParsedToText(parsed, saved, TJA::SaveFormat::Current);
	TJA::ErrorList savedErrors;
	const auto reloaded = TJA::ParseTokens(TJA::TokenizeLines(TJA::SplitLines(saved)), savedErrors);
	const auto definition = ReadBackgroundMovieDefinition(reloaded.Metadata.Others);
	Check(definition.FileName == "background.mp4" && definition.Offset.Seconds == -1.25, "Movie headers must survive saving");
	Check(GetBackgroundMovieTime(Time::FromSec(2), Time::FromSec(0.5), definition.Offset).Seconds == 2.75, "Negative movie offset");
	Check(GetBackgroundMovieTime(Time::FromSec(2), Time::FromSec(-0.5), Time::FromSec(1)).Seconds == 1.5, "Negative song offset");
	Check(ReadBackgroundMovieDefinition({ { "bgmovie", " movie.mp4 " }, { "movieoffset", " 2 " } }).Offset.Seconds == 2, "Case-insensitive headers");
	Check(!ReadBackgroundMovieDefinition({ { "MOVIEOFFSET", "nan" } }).Error.empty(), "Non-finite movie offset must fail");
	Check(!ReadBackgroundMovieDefinition({ { "MOVIEOFFSET", "invalid" } }).Error.empty(), "Invalid movie offset must fail");

	const std::string path = CreateMovie(64, 48);
	BackgroundMovieReader reader, exportReader;
	BackgroundMovieFrame frame, exportFrame;
	for (const f64 time : { 0.0, 0.1999999, 0.2, 0.3999999, 0.4, 1.9, 2.0, 0.2, 2.8 })
	{
		reader.Request(path, Time::FromSec(time));
		Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready, reader.GetError().c_str());
		Check(time >= frame.Start.Seconds && time < frame.End.Seconds, "Frame must cover requested presentation time");
		Check(frame.Size == ivec2(64, 48) && frame.Pixels.size() == 64 * 48 * 4, "Decoded frame size");
		const u8* top = frame.Pixels.data() + (8 * 64 + 32) * 4;
		const u8* bottom = frame.Pixels.data() + (40 * 64 + 32) * 4;
		Check(top[2] > top[0] + 100 && bottom[0] > bottom[2] + 100, "BGRA channels and row orientation");
		Check(std::abs(static_cast<i32>(top[1]) - (30 + static_cast<i32>(std::floor(time * 5)) * 10)) < 30, "Decoded image must match requested frame");
		Check(top[3] == 255 && bottom[3] == 255, "Decoded video must be opaque");
	}
	const u64 pausedSequence = frame.Sequence;
	reader.Request(path, Time::FromSec(2.8));
	Check(reader.Poll(frame) == BackgroundMovieStatus::Ready && frame.Sequence == pausedSequence, "Paused frame should be reused");
	exportReader.Request(path, Time::FromSec(0.8));
	Check(WaitForFrame(exportReader, exportFrame) == BackgroundMovieStatus::Ready, exportReader.GetError().c_str());
	Check(reader.Poll(frame) == BackgroundMovieStatus::Ready && frame.Sequence == pausedSequence, "Independent export reader must not seek preview");
	for (i32 index = 0; index < 90; ++index)
	{
		const f64 time = index / 30.0;
		exportReader.Request(path, Time::FromSec(time));
		Check(WaitForFrame(exportReader, exportFrame) == BackgroundMovieStatus::Ready, exportReader.GetError().c_str());
		Check(time >= exportFrame.Start.Seconds && time < exportFrame.End.Seconds, "30 fps export must select correct 5 fps source frame");
	}
	reader.Request(path, Time::FromSec(-0.1));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Inactive, "Before video start");
	reader.Request(path, Time::FromSec(3.5));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Inactive, "After video end");
	reader.Request(path, Time::FromSec(0.1));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready, "Seeking after EOF must resume decoding");
	reader.Request(path + ".missing", Time::Zero());
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Failed && !reader.GetError().empty(), "Missing movie must report error");
	reader.Close();
	Check(reader.Poll(frame) == BackgroundMovieStatus::Inactive, "Disabled reader must be inactive");
	reader.Request(path, Time::FromSec(0.4));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready, "Reader must recover after reopening");
	reader.Request(path, Time::FromSec(F64Max));
	Check(reader.Poll(frame) == BackgroundMovieStatus::Failed, "Out-of-range time must not overflow");
	reader.Request(path, Time::FromSec(0.4));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready, "Invalid time must recover even at the same position");
	reader.Close();
	reader.Request(CreateMovie(48, 64), Time::FromSec(0.2));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready && frame.Size == ivec2(48, 64), "Portrait video and size change");
	const std::string variablePath = CreateVariableMovie();
	const f64 presentationTimes[] = { 0.0, 0.2, 0.6, 1.4, 2.2, 2.8, 3.0 };
	for (size_t index = 0; index < 6; ++index)
	{
		const f64 time = (presentationTimes[index] + presentationTimes[index + 1]) * 0.5;
		reader.Request(variablePath, Time::FromSec(time));
		Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Ready, reader.GetError().c_str());
		Check(std::abs(frame.Start.Seconds - presentationTimes[index]) < 0.0001 && std::abs(frame.End.Seconds - presentationTimes[index + 1]) < 0.0001, "Variable-rate video must use timestamps rather than frame count");
		Check(frame.Size == ivec2(66, 48), "Video width with row padding");
	}
	CheckDynamicTexture(frame);
	reader.Request(CreateMovie(64, 48, true), Time::FromSec(3.1));
	Check(WaitForFrame(reader, frame) == BackgroundMovieStatus::Inactive, "Long audio must not extend the last video frame");
	std::cout << "Background movie tests passed" << std::endl;
	return 0;
}
