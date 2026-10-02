#include "peepo_drum_kit/chart_editor_file_drop.h"
#include "core_string.h"
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using namespace PeepoDrumKit;

static void Require(bool condition, const char* message)
{
	if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
static void Put16(std::string& bytes, u16 value)
{
	bytes += static_cast<char>(value); bytes += static_cast<char>(value >> 8);
}
static void Put32(std::string& bytes, u32 value)
{
	Put16(bytes, static_cast<u16>(value)); Put16(bytes, static_cast<u16>(value >> 16));
}
static u32 Checksum(std::string_view bytes)
{
	u32 result = 0xFFFFFFFF;
	for (unsigned char byte : bytes)
	{
		result ^= byte;
		for (i32 bit = 0; bit < 8; bit++) result = (result >> 1) ^ ((result & 1) ? 0xEDB88320 : 0);
	}
	return result ^ 0xFFFFFFFF;
}
static void Write(const fs::path& path, std::string_view content)
{
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output.write(content.data(), static_cast<std::streamsize>(content.size()));
	Require(static_cast<bool>(output), "write fixture");
}
static std::string Read(const fs::path& path)
{
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "read extracted file");
	return std::string(std::istreambuf_iterator<char>(input), {});
}
struct Entry
{
	std::string Name, Content, Compressed;
	u16 Method = 0, Flags = 2048;
	u32 Attributes = 0;
	b8 CorruptCRC = false;
	std::string Extra;
};
static std::string Zip(const std::vector<Entry>& entries)
{
	std::string bytes, central;
	for (const Entry& entry : entries)
	{
		const u32 localOffset = static_cast<u32>(bytes.size());
		const std::string& payload = entry.Method == 0 ? entry.Content : entry.Compressed;
		const u32 crc = Checksum(entry.Content) ^ (entry.CorruptCRC ? 1u : 0u);
		Put32(bytes, 0x04034B50); Put16(bytes, 20); Put16(bytes, entry.Flags); Put16(bytes, entry.Method);
		Put16(bytes, 0); Put16(bytes, 0);
		Put32(bytes, (entry.Flags & 8) ? 0 : crc);
		Put32(bytes, (entry.Flags & 8) ? 0 : static_cast<u32>(payload.size()));
		Put32(bytes, (entry.Flags & 8) ? 0 : static_cast<u32>(entry.Content.size()));
		Put16(bytes, static_cast<u16>(entry.Name.size())); Put16(bytes, static_cast<u16>(entry.Extra.size()));
		bytes += entry.Name; bytes += entry.Extra; bytes += payload;
		if (entry.Flags & 8) { Put32(bytes, 0x08074B50); Put32(bytes, crc); Put32(bytes, static_cast<u32>(payload.size())); Put32(bytes, static_cast<u32>(entry.Content.size())); }
		Put32(central, 0x02014B50); Put16(central, 20); Put16(central, 20); Put16(central, entry.Flags); Put16(central, entry.Method);
		Put16(central, 0); Put16(central, 0); Put32(central, crc);
		Put32(central, static_cast<u32>(payload.size())); Put32(central, static_cast<u32>(entry.Content.size()));
		Put16(central, static_cast<u16>(entry.Name.size())); Put16(central, static_cast<u16>(entry.Extra.size()));
		Put16(central, 0); Put16(central, 0); Put16(central, 0); Put32(central, entry.Attributes); Put32(central, localOffset);
		central += entry.Name; central += entry.Extra;
	}
	const u32 centralOffset = static_cast<u32>(bytes.size());
	bytes += central;
	Put32(bytes, 0x06054B50); Put16(bytes, 0); Put16(bytes, 0);
	Put16(bytes, static_cast<u16>(entries.size())); Put16(bytes, static_cast<u16>(entries.size()));
	Put32(bytes, static_cast<u32>(central.size())); Put32(bytes, centralOffset); Put16(bytes, 0);
	return bytes;
}

int main()
{
	const fs::path root = fs::current_path() / "build" / "file-drop-tests" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const fs::path folder = root / "folder";
	Write(folder / "root.TJA", "root");
	Write(folder / "song" / "chart.tja", "child");
	Write(folder / "song" / "deep" / "chart.tja", "grandchild");
	Write(folder / "looks.tja" / "notes.txt", "not a chart");
	Require(FileDrop::ReadFolder(folder.u8string(), 0).ChartPaths.size() == 1, "depth zero");
	Require(FileDrop::ReadFolder(folder.u8string(), 1).ChartPaths.size() == 2, "depth one");
	Require(FileDrop::ReadFolder(folder.u8string(), 2).ChartPaths.size() == 3, "depth two");
	Require(FileDrop::ReadFolder(folder.u8string(), -1).ChartPaths.size() == 1, "negative depth clamps to zero");
	Require(FileDrop::ReadFolder((root / "missing").u8string(), 1).Failure == FileDrop::Error::ReadFailed, "missing folder");

	const fs::path extractionRoot = root / "Extracted zip";
	auto extract = [&](const std::string& name, const std::string& bytes, i32 depth = 1)
	{
		const fs::path zipPath = root / fs::u8path(name + ".zip");
		Write(zipPath, bytes);
		return FileDrop::ExtractZip(zipPath.u8string(), extractionRoot.u8string(), depth);
	};
	const std::string chart = "TITLE:Test\nWAVE:song.wav\n#START\n1000,\n#END\n";
	const u8 deflate[] = { 0x0B, 0xF1, 0x0C, 0xF1, 0x71, 0xB5, 0x0A, 0x49, 0x2D, 0x2E, 0xE1, 0x0A, 0x77, 0x0C, 0x73, 0xB5, 0x2A, 0xCE, 0xCF, 0x4B, 0xD7, 0x2B, 0x4F, 0x2C, 0xE3, 0x52, 0x0E, 0x0E, 0x71, 0x0C, 0x0A, 0xE1, 0x32, 0x34, 0x30, 0x30, 0xD0, 0xE1, 0x52, 0x76, 0xF5, 0x73, 0xE1, 0x02, 0x00 };
	Entry compressed { u8"曲/譜面.tja", chart, std::string(reinterpret_cast<const char*>(deflate), sizeof(deflate)), 8 };
	const std::string audio("\0\1\2\3", 4);
	const std::string bundle = Zip({ compressed, { u8"曲/song.wav", audio }, { u8"曲/deep/other.tja", chart }, { "empty/", "", "", 0, 2048, 16 } });
	FileDrop::Result result = extract(u8"日本語", bundle);
	Require(result.Failure == FileDrop::Error::None && result.ChartPaths.size() == 1, "Deflate and UTF-8 ZIP with depth one");
	Require(Read(fs::u8path(result.ChartPaths.front())) == chart, "Deflate content");
	Require(Read(fs::u8path(result.DirectoryPath) / fs::u8path(u8"曲/song.wav")) == audio, "audio content");
	Require(fs::is_directory(fs::u8path(result.DirectoryPath) / "empty"), "empty directory");
	const FileDrop::Result second = extract(u8"日本語", bundle, 2);
	Require(second.Failure == FileDrop::Error::None && second.ChartPaths.size() == 2 && second.DirectoryPath != result.DirectoryPath, "repeated ZIP gets unique directory");
	Require(Read(fs::u8path(result.ChartPaths.front())) == chart, "existing extraction unchanged");
	Require(extract("stored", Zip({ { "chart.tja", chart } })).ChartPaths.size() == 1, "Store ZIP");
	compressed.Flags |= 8;
	Require(extract("descriptor", Zip({ compressed })).Failure == FileDrop::Error::None, "data descriptor");
	Entry empty { "empty.tja", "", std::string("\x03\x00", 2), 8 };
	Require(extract("empty-deflate", Zip({ empty })).Failure == FileDrop::Error::None, "empty Deflate file");
	Entry legacy { ShiftJIS::FromUTF8(u8"日本語.tja"), chart, "", 0, 0 };
	result = extract("shift-jis", Zip({ legacy }));
	Require(result.Failure == FileDrop::Error::None && fs::u8path(result.ChartPaths.front()).filename().u8string() == u8"日本語.tja", "Shift-JIS filename");
	Entry unicode { "legacy.tja", chart, "", 0, 0 };
	std::string unicodeExtra;
	unicodeExtra += '\1'; Put32(unicodeExtra, Checksum(unicode.Name)); unicodeExtra += u8"追加.tja";
	Put16(unicode.Extra, 0x7075); Put16(unicode.Extra, static_cast<u16>(unicodeExtra.size())); unicode.Extra += unicodeExtra;
	result = extract("unicode-extra", Zip({ unicode }));
	Require(result.Failure == FileDrop::Error::None && fs::u8path(result.ChartPaths.front()).filename().u8string() == u8"追加.tja", "Unicode path extra field");
	Require(extract("no-charts", Zip({ { "song.wav", audio } })).ChartPaths.empty(), "no charts");
	Require(extract("empty-zip", Zip({})).Failure == FileDrop::Error::None, "empty ZIP");

	for (const char* name : { "../escaped.tja", "..\\escaped.tja", "/absolute.tja", "C:/absolute.tja", "chart.tja:stream", "CON.tja", "folder./chart.tja", "CONIN$", "COM\xC2\xB9.tja", "LPT\xC2\xB2.tja" })
		Require(extract("unsafe", Zip({ { name, chart } })).Failure == FileDrop::Error::UnsafePath, "unsafe path rejected");
	Require(!fs::exists(extractionRoot / "escaped.tja") && !fs::exists(root / "escaped.tja"), "no escaped file created");
	Entry link { "link.tja", "target", "", 0, 2048, 0120777u << 16 };
	Require(extract("symlink", Zip({ link })).Failure == FileDrop::Error::UnsafePath, "symlink rejected");
	Entry encrypted { "chart.tja", chart, "", 0, 2049 };
	Require(extract("encrypted", Zip({ encrypted })).Failure == FileDrop::Error::UnsupportedZip, "encrypted ZIP rejected");
	Entry badMethod { "chart.tja", chart, "payload", 9 };
	Require(extract("unsupported", Zip({ badMethod })).Failure == FileDrop::Error::UnsupportedZip, "unsupported compression");
	Entry badCRC { "chart.tja", chart, "", 0, 2048, 0, true };
	Require(extract("bad-crc", Zip({ badCRC })).Failure == FileDrop::Error::InvalidZip, "CRC failure");
	compressed.Compressed = "bad";
	Require(extract("bad-deflate", Zip({ compressed })).Failure == FileDrop::Error::InvalidZip, "bad Deflate");
	Require(extract("duplicates", Zip({ { "chart.tja", "one" }, { "chart.tja", "two" } })).Failure == FileDrop::Error::ExtractionFailed, "duplicate files never overwrite");
	std::string truncated = Zip({ { "chart.tja", chart } }); truncated.resize(truncated.size() - 1);
	Require(extract("truncated", truncated).Failure == FileDrop::Error::InvalidZip, "truncated ZIP");
	std::string zip64 = Zip({}); zip64[zip64.size() - 12] = '\xFF'; zip64[zip64.size() - 11] = '\xFF';
	Require(extract("zip64", zip64).Failure == FileDrop::Error::UnsupportedZip, "ZIP64 rejected");
	std::string tooLarge = Zip({ { "chart.tja", "x" } });
	const size_t central = 30 + std::string("chart.tja").size() + 1;
	tooLarge[central + 24] = '\x01'; tooLarge[central + 25] = '\0'; tooLarge[central + 26] = '\0'; tooLarge[central + 27] = '\x10';
	Require(extract("too-large", tooLarge).Failure == FileDrop::Error::SizeLimit, "declared file size limit");
	const fs::path blockedRoot = root / "not-a-directory"; Write(blockedRoot, "blocked");
	Require(FileDrop::ExtractZip((root / "stored.zip").u8string(), blockedRoot.u8string(), 1).Failure == FileDrop::Error::ExtractionFailed, "unwritable extraction root");
	std::cout << "File-drop tests passed. Fixtures kept at: " << root.u8string() << '\n';
}
