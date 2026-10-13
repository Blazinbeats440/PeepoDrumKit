#include "peepo_drum_kit/chart_editor_asset_paths.h"
#include "peepo_drum_kit/chart_editor_asset_save.h"
#include "core_io.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

using namespace PeepoDrumKit;
namespace fs = std::filesystem;
static int failures = 0;
static void Check(b8 condition, const char* description)
{
	if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); failures++; }
}

int main()
{
	const fs::path root = fs::temp_directory_path() / fs::u8path("PeepoAssetPathsTest-" + std::to_string(CPUTime::GetNow().Ticks));
	fs::create_directory(root);
	const fs::path charts = root / "charts", audio = root / fs::u8path(u8"音源フォルダ"), jacket = root / "jackets", movies = root / "movies", saved = root / "saved";
	for (const auto& path : { charts, audio, jacket, movies, saved }) fs::create_directory(path);
	std::string chartPath = (charts / "original.tja").generic_u8string();
	const std::string audioPath = (audio / "song.ogg").generic_u8string(), jacketPath = (jacket / "cover.png").generic_u8string();
	const std::string executableDirectory = fs::u8path(Directory::GetExecutableDirectory()).generic_u8string();
	Check(GetRelatedFileOpenDirectory(audioPath, chartPath, jacket.u8string()) == audio.generic_u8string(), "assigned asset directory takes priority");
	Check(GetRelatedFileOpenDirectory(AssetSave::RebaseReference(audioPath, {}, chartPath), chartPath, {}) == audio.generic_u8string(), "relative asset uses current chart as its base");
	Check(GetRelatedFileOpenDirectory(audioPath, {}, jacket.u8string()) == audio.generic_u8string(), "imported absolute asset directory is used before first save");
	Check(GetRelatedFileOpenDirectory({}, chartPath, jacket.u8string()) == charts.generic_u8string(), "unassigned asset defaults to TJA directory");
	Check(GetRelatedFileOpenDirectory("missing-folder/song.ogg", chartPath, jacket.u8string()) == charts.generic_u8string(), "missing source directory falls back to TJA directory");
	Check(GetRelatedFileOpenDirectory({}, {}, jacket.u8string()) == jacket.generic_u8string(), "unsaved chart uses last directory of this type");
	Check(GetRelatedFileOpenDirectory({}, {}, {}) == executableDirectory, "empty defaults use executable directory");
	Check(GetRelatedFileOpenDirectory({}, {}, (root / "unavailable").u8string()) == executableDirectory, "removed remembered directory falls back");
	Check(GetRelatedFileOpenDirectory({}, {}, ".") == executableDirectory, "relative remembered directory is based on executable directory");
	{
		std::ofstream dummy(fs::u8path(audioPath)); dummy << "fixture";
	}
	Check(GetRelatedFileOpenDirectory({}, {}, audioPath) == executableDirectory, "remembered file instead of directory is rejected");
	std::string lastAudio, lastJacket = jacket.generic_u8string();
	RememberRelatedFileDirectory(audioPath, chartPath, lastAudio);
	Check(lastAudio == audio.generic_u8string() && lastJacket == jacket.generic_u8string(), "asset types remember independent directories");
	RememberRelatedFileDirectory({}, chartPath, lastAudio);
	Check(lastAudio == audio.generic_u8string(), "clearing a reference keeps last chosen directory");
	RememberRelatedFileDirectory("missing-folder/song.ogg", chartPath, lastAudio);
	Check(lastAudio == audio.generic_u8string(), "invalid directory does not replace a valid remembered directory");
	Check(NormalizeChartAssetReference(audioPath, chartPath) == AssetSave::RebaseReference(audioPath, {}, chartPath), "absolute selection is stored relative to saved TJA");
	Check(NormalizeChartAssetReference(audioPath, {}) == audioPath, "absolute imported asset stays absolute before first save");
	Check(NormalizeChartAssetReference("Z:/other/song.ogg", chartPath) == "Z:/other/song.ogg", "cross-drive selection remains absolute");

	Undo::UndoHistory history;
	std::string reference = AssetSave::RebaseReference(audioPath, {}, chartPath);
	const std::string oldReference = reference;
	const std::string nextPath = (audio / "second.ogg").generic_u8string();
	const std::string newReference = NormalizeChartAssetReference(nextPath, chartPath);
	std::vector<std::pair<std::string, b8>> reloads;
	auto reload = [&](std::string_view path, b8 firstApply) { reloads.emplace_back(path, firstApply); };
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, newReference, reload, u8"音源ファイルを変更");
	Check(reference == newReference && history.UndoStack.size() == 1 && history.HasPendingChanges, "file selection becomes one undoable edit");
	Check(reloads.back().first == nextPath && reloads.back().second, "initial file selection reloads correct absolute source");
	Check(history.UndoStack.back()->GetInfo().Description == u8"音源ファイルを変更", "localized description is retained");
	history.ClearChangesWereMade();
	history.Undo();
	Check(reference == oldReference && history.HasPendingChanges, "undo restores original reference and makes saved chart dirty");
	Check(reloads.back().first == audioPath && !reloads.back().second, "undo reloads old source without metadata autofill");
	history.Redo();
	Check(reference == newReference && reloads.back().first == nextPath && !reloads.back().second, "redo reloads new source without unrelated metadata changes");

	const std::string oldChartPath = chartPath;
	chartPath = (saved / "new.tja").generic_u8string();
	reference = AssetSave::RebaseReference(reference, oldChartPath, chartPath);
	history.Undo();
	Check(AssetSave::ResolveSource(reference, chartPath) == audioPath && reloads.back().first == audioPath, "undo after Save As preserves physical source");
	history.Redo();
	Check(AssetSave::ResolveSource(reference, chartPath) == nextPath, "redo after Save As preserves physical source");
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, "", reload, "Clear audio");
	Check(reference.empty() && reloads.back().first.empty(), "clearing a file unloads it");
	history.Undo();
	Check(AssetSave::ResolveSource(reference, chartPath) == nextPath, "undo clearing restores source");
	history.Redo();
	Check(reference.empty(), "redo clearing restores empty reference");

	history.ClearAll();
	reference = oldReference;
	chartPath = oldChartPath;
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, newReference, reload, "Audio selection");
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, "third.ogg", reload, "Audio selection");
	Check(history.UndoStack.size() == 2, "consecutive selections remain separate undo steps");
	history.Undo();
	Check(reference == newReference, "undo one rapid selection keeps previous selected file");
	history.Undo();
	Check(reference == oldReference, "undo both rapid selections restores original");
	history.Redo();
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, "fourth.ogg", reload, "Audio selection");
	Check(!history.CanRedo(), "new selection after undo replaces redo branch");

	history.ClearAll();
	chartPath.clear();
	reference = audioPath;
	history.Execute<Commands::ChangeChartAssetReference>(&reference, &chartPath, nextPath, reload, "Imported audio");
	chartPath = (saved / "imported.tja").generic_u8string();
	reference = AssetSave::RebaseReference(reference, {}, chartPath);
	history.Undo();
	Check(AssetSave::ResolveSource(reference, chartPath) == audioPath, "undo after imported chart's first save restores source");
	history.Redo();
	Check(AssetSave::ResolveSource(reference, chartPath) == nextPath, "redo after imported chart's first save restores source");

	history.ClearAll();
	chartPath = oldChartPath;
	std::string jacketReference = NormalizeChartAssetReference(jacketPath, chartPath);
	history.Execute<Commands::ChangeChartAssetReference>(&jacketReference, &chartPath, "new.png", reload, "Jacket");
	history.Undo();
	Check(reloads.back().first == jacketPath && AssetSave::ResolveSource(jacketReference, chartPath) == jacketPath, "jacket undo restores path and invokes reload");

	history.ClearAll();
	const std::string moviePath = (movies / "original.mp4").generic_u8string();
	const std::string movieReference = NormalizeChartAssetReference(moviePath, chartPath);
	std::map<std::string, std::string> metadata = { { "BgMoViE", "  " + movieReference + "  " }, { "BGMOVIE", "other.mp4" }, { "MOVIEOFFSET", "1.25" }, { "CUSTOM", "keep" } };
	const auto originalMetadata = metadata;
	history.Execute<Commands::ChangeBackgroundMovieReference>(&metadata, &chartPath, "new.mp4", "Video");
	Check(metadata.count("BgMoViE") == 0 && metadata.at("BGMOVIE") == "new.mp4", "new video uses one canonical BGMOVIE key");
	metadata["MOVIEOFFSET"] = "2.5";
	history.Undo();
	Check(metadata.at("BgMoViE") == originalMetadata.at("BgMoViE") && metadata.at("BGMOVIE") == "other.mp4", "undo restores original key casing and duplicate metadata");
	Check(metadata.at("MOVIEOFFSET") == "2.5" && metadata.at("CUSTOM") == "keep", "movie undo preserves independent metadata edits");
	history.Redo();
	Check(metadata.at("BGMOVIE") == "new.mp4" && metadata.at("MOVIEOFFSET") == "2.5", "movie redo preserves offset");
	history.Execute<Commands::ChangeBackgroundMovieReference>(&metadata, &chartPath, "", "Clear video");
	Check(metadata.count("BGMOVIE") == 0, "clearing video removes only the file reference");
	history.Undo();
	Check(metadata.at("BGMOVIE") == "new.mp4", "undo clearing video restores its reference");
	history.Undo();
	chartPath = (saved / "movie.tja").generic_u8string();
	history.Redo();
	Check(AssetSave::ResolveSource(metadata.at("BGMOVIE"), chartPath) == (charts / "new.mp4").generic_u8string(), "video redo after Save As keeps source stable");
	history.Undo();
	Check(AssetSave::ResolveSource(metadata.at("BgMoViE"), chartPath) == moviePath, "video undo rebases whitespace-trimmed original source");

	fs::remove_all(root);
	std::printf("Asset path tests: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
