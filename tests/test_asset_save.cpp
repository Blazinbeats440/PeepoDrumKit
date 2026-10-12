#include "peepo_drum_kit/chart_editor_asset_save.h"
#include "file_format_tja.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <cstdio>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace fs = std::filesystem;
using namespace PeepoDrumKit::AssetSave;
static int failures = 0;
static void Check(bool condition, const char* description)
{
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); failures++; }
}
static void Write(const fs::path& path, std::string_view content)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	Check(file.good(), "test fixture can be written");
}
static std::string Read(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
static bool HasTemporary(const fs::path& directory)
{
	for (const auto& entry : fs::directory_iterator(directory))
		if (entry.path().filename().u8string().find(".pdk-save-") == 0) return true;
	return false;
}

int main()
{
	// Each run owns a fresh temporary folder; it never edits a real chart.
	const fs::path root = fs::temp_directory_path() / (L"PeepoAssetSaveTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	fs::create_directory(root);
	const fs::path source = root / fs::u8path(u8"元の曲"), output = root / fs::u8path(u8"保存先");
	fs::create_directory(source); fs::create_directory(output);
	const fs::path oldChart = source / "original.tja", newChart = output / fs::u8path(u8"譜面.tja");
	Write(oldChart, "ORIGINAL TJA");
	Write(source / "song.ogg", "audio original");
	Write(source / fs::u8path(u8"表紙.png"), "image original");
	Write(source / "movie.mp4", "movie original");
	const std::vector<Input> all = { { "song.ogg" }, { u8"表紙.png" }, { "movie.mp4" } };
	Check(all[0].Selected && all[1].Selected && all[2].Selected, "all three asset types default to ON");
	Progress progress;
	Plan plan = Prepare(oldChart.u8string(), newChart.u8string(), all, progress);
	Check(plan.CanSave() && plan.Assets.size() == 3, "prepare all configured assets");
	Check(plan.Assets[0].SourcePath == (source / "song.ogg").generic_u8string(), "resolve relative source before changing chart path");
	Check(plan.Assets[0].SavedReference == "song.ogg" && plan.Assets[1].SavedReference == u8"表紙.png", "copied references are sibling names");
	TJA::ParsedTJA tja;
	tja.Metadata.TITLE = u8"テスト";
	tja.Metadata.WAVE = plan.Assets[0].SavedReference;
	tja.Metadata.PREIMAGE = plan.Assets[1].SavedReference;
	tja.Metadata.Others["BGMOVIE"] = plan.Assets[2].SavedReference;
	std::string text;
	TJA::ConvertParsedToText(tja, text, TJA::SaveFormat::Current);
	Check(Save(plan, text, false, progress).Code == Error::None, "copy assets and save UTF-8 TJA");
	Check(Read(newChart).find("WAVE:song.ogg") != std::string::npos && Read(newChart).find("BGMOVIE:movie.mp4") != std::string::npos, "TJA serializes copied references");
	Check(Read(output / "song.ogg") == "audio original" && Read(output / fs::u8path(u8"表紙.png")) == "image original", "copied contents match including Unicode path");
	Check(Read(oldChart) == "ORIGINAL TJA" && Read(source / "movie.mp4") == "movie original", "original chart and assets are preserved");
	Check(!HasTemporary(output), "successful save removes staging files");

	Progress reuseProgress;
	plan = Prepare(oldChart.u8string(), newChart.u8string(), all, reuseProgress);
	Check(plan.CopyBytes == 0 && plan.Assets[0].Operation == Action::Reuse, "identical destination content is reused");
	Progress sameProgress;
	const Plan same = Prepare(newChart.u8string(), newChart.u8string(), all, sameProgress);
	Check(same.CanSave() && same.CopyBytes == 0 && same.Assets[0].Operation == Action::Reuse, "assets already beside the chart need no copy");

	Write(output / "song.ogg", "audio altered!"); // Same length is insufficient to decide equality.
	Write(output / "song (2).ogg", "second conflict");
	Progress conflictProgress;
	plan = Prepare(oldChart.u8string(), newChart.u8string(), all, conflictProgress);
	Check(plan.Assets[0].SavedReference == "song (3).ogg", "different same-name content gets the next free number");
	Check(Save(plan, "NEW TJA", false, conflictProgress).Code == Error::None, "save with renamed audio");
	Check(Read(output / "song.ogg") == "audio altered!" && Read(output / "song (3).ogg") == "audio original", "existing different asset is never overwritten");

	Progress duplicateProgress;
	const Plan duplicate = Prepare(oldChart.u8string(), (output / "duplicate.tja").u8string(), { { "movie.mp4" }, { "./movie.mp4" } }, duplicateProgress);
	Check(duplicate.CanSave() && duplicate.CopyBytes == 0 && duplicate.Assets[0].SavedReference == duplicate.Assets[1].SavedReference, "duplicate source references share one destination");
	const fs::path fresh = root / "fresh";
	fs::create_directory(fresh);
	Progress duplicateCopyProgress;
	const Plan duplicateCopy = Prepare(oldChart.u8string(), (fresh / "duplicate.tja").u8string(), { { "movie.mp4" }, { "./movie.mp4" } }, duplicateCopyProgress);
	Check(duplicateCopy.CopyBytes == std::string("movie original").size(), "duplicate source size counted only once");
	Check(Save(duplicateCopy, "DUPLICATE", false, duplicateCopyProgress).Code == Error::None, "duplicate source is copied once");

	Progress missingProgress;
	const Plan missing = Prepare(oldChart.u8string(), newChart.u8string(), { { "missing.wav" } }, missingProgress);
	Check(!missing.CanSave() && missing.Assets[0].Missing, "selected missing source blocks save");
	Progress uncheckedProgress;
	const Plan unchecked = Prepare(oldChart.u8string(), newChart.u8string(), { { "missing.wav", {}, false }, { "song.ogg", {}, false } }, uncheckedProgress);
	Check(unchecked.CanSave() && unchecked.CopyBytes == 0, "unchecking missing source allows save without copying");
	Check(ResolveSource(unchecked.Assets[0].SavedReference, unchecked.ChartPath) == (source / "missing.wav").generic_u8string(), "unselected missing source still refers to original location");
	Check(ResolveSource(unchecked.Assets[1].SavedReference, unchecked.ChartPath) == (source / "song.ogg").generic_u8string(), "unselected existing source is correctly rebased");
	Progress reselectProgress;
	const Plan reselect = Prepare(oldChart.u8string(), newChart.u8string(), { { "missing.wav", (source / "movie.mp4").u8string() } }, reselectProgress);
	Check(reselect.CanSave() && reselect.Assets[0].SavedReference == "movie.mp4", "missing source can be replaced without editing the original chart");
	Check(RebaseReference("Z:/external/song.ogg", oldChart.u8string(), newChart.u8string()) == "Z:/external/song.ogg", "different drive falls back to an absolute reference");
	Progress importedProgress;
	const Plan imported = Prepare({}, (fresh / "imported.tja").u8string(), { { (source / "song.ogg").u8string(), {}, false } }, importedProgress);
	Check(imported.CanSave() && ResolveSource(imported.Assets[0].SavedReference, imported.ChartPath) == (source / "song.ogg").generic_u8string(), "first save rebases absolute imported assets");

	Progress changedProgress;
	Plan changed = Prepare(oldChart.u8string(), newChart.u8string(), { { "movie.mp4" } }, changedProgress);
	Write(output / "movie.mp4", "changed reuse");
	const std::string chartBeforeChange = Read(newChart);
	Check(Save(changed, "SHOULD NOT SAVE", false, changedProgress).Code == Error::DestinationChanged, "reuse content is rechecked at save time");
	Check(Read(newChart) == chartBeforeChange && Read(output / "movie.mp4") == "changed reuse", "changed reused file and chart are preserved");

	Write(source / "rollback.ogg", "rollback copy");
	Progress rollbackProgress;
	Plan rollback = Prepare(oldChart.u8string(), newChart.u8string(), { { "rollback.ogg" } }, rollbackProgress);
	HANDLE lockedChart = CreateFileW(newChart.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	Check(lockedChart != INVALID_HANDLE_VALUE, "lock old TJA for a real commit failure");
	Check(Save(rollback, "FAILED TJA", true, rollbackProgress).Code == Error::WriteFailed, "locked TJA causes commit failure");
	if (lockedChart != INVALID_HANDLE_VALUE) CloseHandle(lockedChart);
	Check(Read(newChart) == chartBeforeChange && !fs::exists(output / "rollback.ogg"), "failed commit preserves old TJA and rolls back newly published asset");
	Check(!fs::exists(fs::u8path(newChart.u8string() + ".bak")) && !HasTemporary(output), "failed save cleans its own backup and temporary files");

	Progress raceProgress;
	Plan race = Prepare(oldChart.u8string(), newChart.u8string(), { { "rollback.ogg" }, { "movie.mp4" } }, raceProgress);
	Write(output / "movie (2).mp4", "external file appeared");
	Check(Save(race, "FAILED RACE", false, raceProgress).Code == Error::DestinationChanged, "new destination collision is rejected at publication");
	Check(!fs::exists(output / "rollback.ogg") && Read(output / "movie (2).mp4") == "external file appeared" && Read(newChart) == chartBeforeChange, "collision rolls back only our new files");

	Progress backupProgress;
	Plan backup = Prepare({}, newChart.u8string(), {}, backupProgress);
	Check(Save(backup, "FINAL TJA", true, backupProgress).Code == Error::None, "ordinary save creates an original backup");
	Check(Read(fs::u8path(newChart.u8string() + ".bak")) == chartBeforeChange && Read(newChart) == "FINAL TJA", "backup preserves previous TJA bytes");

	Progress canceledProgress;
	Plan canceled = Prepare(oldChart.u8string(), newChart.u8string(), { { "rollback.ogg" } }, canceledProgress);
	canceledProgress.Cancel = true;
	Check(Save(canceled, "CANCELED", false, canceledProgress).Code == Error::Canceled && !fs::exists(output / "rollback.ogg"), "cancel before copying leaves destination untouched");
	Progress canceledPrepareProgress;
	canceledPrepareProgress.Cancel = true;
	Check(Prepare(oldChart.u8string(), newChart.u8string(), all, canceledPrepareProgress).Failed.Code == Error::Canceled, "content checking can be canceled");

	const fs::path large = source / "large.mp4";
	{
		std::ofstream file(large, std::ios::binary);
		const std::string chunk(1024 * 1024, 'x');
		for (int index = 0; index < 64; index++) file.write(chunk.data(), chunk.size());
	}
	Progress largeProgress;
	Plan largePlan = Prepare(oldChart.u8string(), newChart.u8string(), { { "large.mp4" } }, largeProgress);
	auto future = std::async(std::launch::async, [&] { return Save(largePlan, "CANCELED LARGE", false, largeProgress); });
	while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
	{
		if (largeProgress.CompletedBytes.load() > 0) { largeProgress.Cancel = true; break; }
	}
	Check(future.get().Code == Error::Canceled && largeProgress.CompletedBytes.load() > 0, "cancel after copying begins works for streaming video");
	Check(!fs::exists(output / "large.mp4") && Read(newChart) == "FINAL TJA" && !HasTemporary(output), "mid-copy cancel cleans staging and preserves saved chart");

	Progress invalidProgress;
	Check(!Prepare(oldChart.u8string(), (root / "missing-folder" / "chart.tja").u8string(), all, invalidProgress).CanSave(), "unavailable destination rejected");
	Progress aliasProgress;
	Check(!Prepare(oldChart.u8string(), (source / "song.ogg").u8string(), { { "song.ogg" } }, aliasProgress).CanSave(), "chart cannot overwrite an asset source");

	fs::remove_all(root);
	std::printf("Asset save tests: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
