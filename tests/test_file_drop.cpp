#define NOMINMAX
#include "peepo_drum_kit/chart_editor_file_drop.h"
#include "core_string.h"
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <archive.h>
#include <archive_entry.h>
#include <sstream>

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

static std::string SevenZip(const std::vector<Entry>& entries, const char* compression = "lzma2")
{
	std::string bytes(1024 * 1024, '\0');
	size_t size = 0;
	archive* writer = archive_write_new();
	Require(writer != nullptr && archive_write_set_format_7zip(writer) == ARCHIVE_OK, "create 7z writer");
	Require(archive_write_set_format_option(writer, "7zip", "compression", compression) == ARCHIVE_OK, "set 7z compression");
	Require(archive_write_open_memory(writer, bytes.data(), bytes.size(), &size) == ARCHIVE_OK, "open 7z output");
	for (const Entry& entry : entries)
	{
		archive_entry* header = archive_entry_new();
		const std::wstring wideName = UTF8::Widen(entry.Name);
		archive_entry_copy_pathname_w(header, wideName.c_str());
		archive_entry_set_filetype(header, entry.Name.back() == '/' ? AE_IFDIR : AE_IFREG);
		archive_entry_set_perm(header, 0644);
		archive_entry_set_size(header, entry.Content.size());
		const int headerResult = archive_write_header(writer, header);
		if (headerResult != ARCHIVE_OK) std::cerr << entry.Name << ": " << archive_error_string(writer) << '\n';
		Require(headerResult == ARCHIVE_OK, "write 7z entry header");
		Require(archive_write_data(writer, entry.Content.data(), entry.Content.size()) == entry.Content.size(), "write 7z content");
		archive_entry_free(header);
	}
	Require(archive_write_close(writer) == ARCHIVE_OK && archive_write_free(writer) == ARCHIVE_OK, "close 7z writer");
	bytes.resize(size);
	return bytes;
}
static std::string StoredRAR(const std::vector<Entry>& entries)
{
	std::string bytes("Rar!\x1A\x07\x00", 7);
	auto header = [&](const std::string& value) { Put16(bytes, static_cast<u16>(Checksum(value))); bytes += value; };
	std::string main;
	main += '\x73'; Put16(main, 0); Put16(main, 13); main.append(6, '\0'); header(main);
	for (const Entry& entry : entries)
	{
		std::string file;
		file += '\x74'; Put16(file, 0x8000); Put16(file, static_cast<u16>(32 + entry.Name.size()));
		Put32(file, static_cast<u32>(entry.Content.size())); Put32(file, static_cast<u32>(entry.Content.size()));
		file += '\x02'; Put32(file, Checksum(entry.Content)); Put32(file, 0);
		file += '\x14'; file += '\x30'; Put16(file, static_cast<u16>(entry.Name.size())); Put32(file, 32); file += entry.Name;
		header(file); bytes += entry.Content;
	}
	std::string end; end += '\x7B'; Put16(end, 0); Put16(end, 7); header(end);
	return bytes;
}
static std::string StoredLZH(const std::vector<Entry>& entries)
{
	std::string bytes;
	for (const Entry& entry : entries)
	{
		std::string header = "-lh0-";
		Put32(header, static_cast<u32>(entry.Content.size())); Put32(header, static_cast<u32>(entry.Content.size())); Put32(header, 0);
		header += '\x20'; header += '\0'; header += static_cast<char>(entry.Name.size()); header += entry.Name;
		u16 crc = 0;
		for (unsigned char byte : entry.Content)
		{
			crc ^= byte;
			for (i32 bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
		}
		Put16(header, crc);
		u8 checksum = 0; for (unsigned char byte : header) checksum += byte;
		bytes += static_cast<char>(header.size()); bytes += static_cast<char>(checksum); bytes += header; bytes += entry.Content;
	}
	bytes += '\0';
	return bytes;
}
static std::string ReferenceArchive(const char* name)
{
	std::istringstream input(Read(fs::path("tests/fixtures/archives") / (std::string(name) + ".uu")));
	std::string line, bytes;
	bool started = false;
	while (std::getline(input, line))
	{
		if (!started) { started = line.find("begin ") == 0; continue; }
		if (line == "end" || line.empty()) continue;
		const size_t count = (static_cast<unsigned char>(line[0]) - 32) & 63;
		for (size_t index = 0; index < count; index += 3)
		{
			const size_t offset = 1 + index / 3 * 4;
			Require(offset + 4 <= line.size(), "valid uuencoded reference");
			u32 value = 0;
			for (size_t digit = 0; digit < 4; digit++) value = (value << 6) | ((static_cast<unsigned char>(line[offset + digit]) - 32) & 63);
			for (size_t byte = 0; byte < 3 && index + byte < count; byte++) bytes += static_cast<char>(value >> (16 - byte * 8));
		}
	}
	Require(started, "reference archive header");
	return bytes;
}

int main()
{
	std::cout << "Archive libraries: " << archive_version_details() << std::endl;
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
	Write(folder / "import.MC", "{}");
	Require(FileDrop::ReadFolder(folder.u8string(), 0).ChartPaths.size() == 2, "MC chart with uppercase extension");
	Write(folder / "import.OSU", "osu file format v14");
	Require(FileDrop::ReadFolder(folder.u8string(), 0).ChartPaths.size() == 3, "OSU chart with uppercase extension");

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
	const auto mcArchive = extract("malody", Zip({ { "chart.mc", "{}" }, { "song.wav", audio } }));
	Require(mcArchive.Failure == FileDrop::Error::None && mcArchive.ChartPaths.size() == 1, "MC chart in ZIP");
	Require(fs::u8path(mcArchive.ChartPaths.front()).extension() == ".mc", "MC extension preserved");
	const fs::path oszPath = root / "beatmaps.OSZ";
	Write(oszPath, Zip({ { "easy.osu", "osu file format v3" }, { "oni.OSU", "osu file format v128" }, { "song.ogg", audio } }));
	Require(FileDrop::IsArchivePath(oszPath.u8string()), "OSZ is an archive");
	const auto oszArchive = FileDrop::ExtractArchive(oszPath.u8string(), extractionRoot.u8string(), 1);
	Require(oszArchive.Failure == FileDrop::Error::None && oszArchive.ChartPaths.size() == 2, "OSZ uses ZIP extraction and lists OSU charts");
	Require(Read(fs::u8path(oszArchive.DirectoryPath) / "song.ogg") == audio, "OSZ audio extracted");
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
	Require(extract("encrypted", Zip({ encrypted })).Failure == FileDrop::Error::PasswordProtected, "encrypted ZIP gets password message");
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
	for (const char* extension : { ".zip", ".ZIP", ".7z", ".7Z", ".rar", ".RAR", ".lzh", ".LZH" })
		Require(FileDrop::IsArchivePath(std::string("song") + extension), "supported archive extension");
	for (const char* extension : { ".tar", ".tar.gz", ".gz", ".cab", ".exe", ".tja" })
		Require(!FileDrop::IsArchivePath(std::string("song") + extension), "other archive extensions excluded");
	auto extractArchive = [&](const std::string& name, const std::string& bytes, i32 depth = 2)
	{
		std::cerr << "Testing archive: " << name << '\n';
		const fs::path path = root / fs::u8path(name);
		Write(path, bytes);
		return FileDrop::ExtractArchive(path.u8string(), extractionRoot.u8string(), depth);
	};
	const std::vector<Entry> archiveEntries = { { u8"曲/譜面.tja", chart }, { u8"曲/song.wav", audio }, { u8"曲/deep/other.tja", chart }, { "empty/", "" } };
	const std::string sevenZip = SevenZip(archiveEntries);
	result = extractArchive(u8"日本語.7Z", sevenZip, 1);
	Require(result.Failure == FileDrop::Error::None && result.ChartPaths.size() == 1, "7z LZMA2 with UTF-8 filenames and depth one");
	Require(Read(fs::u8path(result.ChartPaths.front())) == chart && Read(fs::u8path(result.DirectoryPath) / fs::u8path(u8"曲/song.wav")) == audio, "7z chart and audio content");
	Require(fs::is_directory(fs::u8path(result.DirectoryPath) / "empty"), "7z empty directory");
	const FileDrop::Result emoji = extractArchive("unicode.7z", SevenZip({ { u8"曲/🥁.tja", chart } }));
	Require(emoji.Failure == FileDrop::Error::None && emoji.ChartPaths.size() == 1 && fs::u8path(emoji.ChartPaths.front()).filename().u8string() == u8"🥁.tja", "7z filename outside legacy code page");
	const FileDrop::Result repeated = extractArchive(u8"日本語.7Z", sevenZip, 2);
	Require(repeated.Failure == FileDrop::Error::None && repeated.ChartPaths.size() == 2 && repeated.DirectoryPath != result.DirectoryPath, "7z repeat and depth two");
	Require(Read(fs::u8path(result.ChartPaths.front())) == chart, "previous 7z extraction preserved");
	for (const char* compression : { "lzma1", "deflate", "bzip2", "ppmd", "copy" })
	{
		result = extractArchive(std::string(compression) + ".7z", SevenZip({ { "chart.tja", chart } }, compression));
		Require(result.Failure == FileDrop::Error::None && result.ChartPaths.size() == 1 && Read(fs::u8path(result.ChartPaths.front())) == chart, "7z compression modes");
	}
	Require(extractArchive("empty.7z", SevenZip({})).Failure == FileDrop::Error::None, "empty 7z");
	const std::vector<Entry> legacyEntries = { { ShiftJIS::FromUTF8(u8"曲\\譜面.tja"), chart }, { ShiftJIS::FromUTF8(u8"曲\\song.wav"), audio }, { "song\\deep\\chart.tja", chart } };
	for (const char* extension : { ".rar", ".lzh" })
	{
		result = extractArchive(std::string("stored") + extension, extension[1] == 'r' ? StoredRAR(legacyEntries) : StoredLZH(legacyEntries), 1);
		Require(result.Failure == FileDrop::Error::None && result.ChartPaths.size() == 1, "RAR/LZH depth one and Japanese filenames");
		Require(fs::u8path(result.ChartPaths.front()).filename().u8string() == u8"譜面.tja" && Read(fs::u8path(result.ChartPaths.front())) == chart, "RAR/LZH chart content");
		Require(Read(fs::u8path(result.DirectoryPath) / fs::u8path(u8"曲/song.wav")) == audio, "RAR/LZH audio content");
	}
	for (const char* name : { "../escaped.tja", "C:/absolute.tja", "CON.tja", "chart.tja:stream" })
	{
		Require(extractArchive("unsafe.7z", SevenZip({ { name, chart } })).Failure == FileDrop::Error::UnsafePath, "7z unsafe path rejected");
		Require(extractArchive("unsafe.rar", StoredRAR({ { name, chart } })).Failure == FileDrop::Error::UnsafePath, "RAR unsafe path rejected");
		Require(extractArchive("unsafe.lzh", StoredLZH({ { name, chart } })).Failure == FileDrop::Error::UnsafePath, "LZH unsafe path rejected");
	}
	Require(extractArchive("duplicate.7z", SevenZip({ { "chart.tja", "one" }, { "chart.tja", "two" } })).Failure == FileDrop::Error::ExtractionFailed, "7z duplicate cannot overwrite");
	const FileDrop::Result duplicateRAR = extractArchive("duplicate.rar", StoredRAR({ { "chart.tja", "one" }, { "chart.tja", "two" } }));
	Require(Read(fs::u8path(duplicateRAR.DirectoryPath) / "chart.tja") == "one", "RAR duplicate cannot overwrite");
	Require(extractArchive("duplicate.lzh", StoredLZH({ { "chart.tja", "one" }, { "chart.tja", "two" } })).Failure == FileDrop::Error::ExtractionFailed, "LZH duplicate cannot overwrite");
	std::string corruptLZH = StoredLZH({ { "chart.tja", chart } });
	corruptLZH[static_cast<unsigned char>(corruptLZH[0]) + 2] ^= 1;
	Require(extractArchive("bad-crc.lzh", corruptLZH).Failure == FileDrop::Error::InvalidArchive, "LZH CRC failure");
	std::string corruptRAR = StoredRAR({ { "chart.tja", chart } });
	corruptRAR[20 + 32 + std::string("chart.tja").size()] ^= 1;
	Require(extractArchive("bad-crc.rar", corruptRAR).Failure == FileDrop::Error::InvalidArchive, "RAR CRC failure");
	std::string largeLZH = StoredLZH({ { "chart.tja", "x" } });
	largeLZH[11] = '\1'; largeLZH[12] = '\0'; largeLZH[13] = '\0'; largeLZH[14] = '\x10';
	u8 headerChecksum = 0;
	for (size_t index = 2; index < static_cast<unsigned char>(largeLZH[0]) + 2; index++) headerChecksum += static_cast<u8>(largeLZH[index]);
	largeLZH[1] = static_cast<char>(headerChecksum);
	Require(extractArchive("too-large.lzh", largeLZH).Failure == FileDrop::Error::SizeLimit, "LZH declared size limit");
	std::string largeRAR = StoredRAR({ { "chart.tja", "x" } });
	largeRAR[31] = '\1'; largeRAR[32] = '\0'; largeRAR[33] = '\0'; largeRAR[34] = '\x10';
	const u16 rarHeaderCRC = static_cast<u16>(Checksum(std::string_view(largeRAR).substr(22, 30 + std::string("chart.tja").size())));
	largeRAR[20] = static_cast<char>(rarHeaderCRC); largeRAR[21] = static_cast<char>(rarHeaderCRC >> 8);
	Require(extractArchive("too-large.rar", largeRAR).Failure == FileDrop::Error::SizeLimit, "RAR declared size limit");
	Require(extractArchive("truncated.7z", sevenZip.substr(0, sevenZip.size() - 1)).Failure == FileDrop::Error::InvalidArchive, "truncated 7z");
	for (const char* name : { "broken.7z", "broken.rar", "broken.lzh" })
		Require(extractArchive(name, "invalid archive").Failure == FileDrop::Error::InvalidArchive, "invalid archives rejected");
	Require(extractArchive("ignored.tar", "tar").Failure == FileDrop::Error::UnsupportedArchive, "TAR not supported");
	Require(extractArchive("renamed.7z", Zip({ { "chart.tja", chart } })).Failure == FileDrop::Error::InvalidArchive, "only requested archive formats enabled");
	Require(FileDrop::ExtractArchive((root / "missing.7z").u8string(), extractionRoot.u8string(), 2).Failure == FileDrop::Error::ReadFailed, "missing archive");
	Require(FileDrop::ExtractArchive((root / "stored.rar").u8string(), blockedRoot.u8string(), 1).Failure == FileDrop::Error::ExtractionFailed, "RAR blocked extraction root");
	Require(extractArchive("dispatch.zip", Zip({ { "chart.tja", chart } })).ChartPaths.size() == 1, "ZIP dispatcher");
	for (const char* name : { "test_read_format_7zip_encryption.7z", "test_read_format_7zip_encryption_header.7z", "test_read_format_7zip_encryption_partially.7z", "test_read_format_rar4_encrypted.rar", "test_read_format_rar4_encrypted_filenames.rar", "test_read_format_rar5_encrypted.rar", "test_read_format_rar5_encrypted_filenames.rar" })
		Require(extractArchive(name, ReferenceArchive(name)).Failure == FileDrop::Error::PasswordProtected, name);
	result = extractArchive("test_read_format_rar5_compressed.rar", ReferenceArchive("test_read_format_rar5_compressed.rar"));
	std::string rar5Content;
	for (i32 index = 1; index <= 300; index++) Put32(rar5Content, static_cast<u32>(std::max(0, index * index - 3 * index + 1)));
	Require(result.Failure == FileDrop::Error::None && Read(fs::u8path(result.DirectoryPath) / "test.bin") == rar5Content, "compressed RAR5 reference content");
	result = extractArchive("test_read_format_rar_compress_normal.rar", ReferenceArchive("test_read_format_rar_compress_normal.rar"));
	Require(result.Failure == FileDrop::Error::UnsafePath && Read(fs::u8path(result.DirectoryPath) / "LibarchiveAddingTest.html").size() == 20111, "compressed RAR4 content and link rejection");
	for (const char* name : { "test_read_format_lha_header0.lzh", "test_read_format_lha_lh6.lzh", "test_read_format_lha_lh7.lzh" })
	{
		result = extractArchive(name, ReferenceArchive(name));
		Require(result.Failure == FileDrop::Error::UnsafePath, "LZH link rejection");
	}
	result = extractArchive("test_read_format_lha_header3.lzh", ReferenceArchive("test_read_format_lha_header3.lzh"));
	Require(result.Failure == FileDrop::Error::None && Read(fs::u8path(result.DirectoryPath) / "file1") == "                          file 1 contents\nhello\nhello\nhello\n", "LZH compressed content with Windows header");
	Require(fs::is_directory(fs::u8path(result.DirectoryPath) / "dir") && fs::is_directory(fs::u8path(result.DirectoryPath) / "dir2"), "LZH empty directories with unset size");
	result = extractArchive("test_read_format_lha_bugfix_0.lzh", ReferenceArchive("test_read_format_lha_bugfix_0.lzh"));
	Require(result.Failure == FileDrop::Error::None && Read(fs::u8path(result.DirectoryPath) / "f").size() == 776, "LZH compressed reference");
	std::cout << "File-drop tests passed. Fixtures kept at: " << root.u8string() << '\n';
}
