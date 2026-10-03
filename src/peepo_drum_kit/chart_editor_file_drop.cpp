#include "chart_editor_file_drop.h"
#include "core_string.h"
#include <stb/stb_image.h>
#include <filesystem>
#include <fstream>
#include <array>
#define NOMINMAX
#include <archive.h>
#include <archive_entry.h>
#include <Windows.h>

namespace PeepoDrumKit::FileDrop
{
	namespace fs = std::filesystem;
	static constexpr u64 MaxZipBytes = 1024ull * 1024 * 1024;
	static constexpr u64 MaxEntryBytes = 256ull * 1024 * 1024;
	static constexpr u64 MaxExtractedBytes = 2ull * 1024 * 1024 * 1024;

	static b8 IsReparsePoint(const fs::path& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	}

	Result ReadFolder(std::string_view directoryPath, i32 searchDepth)
	{
		Result result;
		result.DirectoryPath = directoryPath;
		std::error_code error;
		for (fs::recursive_directory_iterator iterator(fs::u8path(directoryPath), error), end;
			!error && iterator != end; iterator.increment(error))
		{
			if (iterator.depth() >= std::max(0, searchDepth) || IsReparsePoint(iterator->path()))
				iterator.disable_recursion_pending();
			if (iterator->is_regular_file(error) && !error && ASCII::MatchesInsensitive(iterator->path().extension().u8string(), ".tja"))
				result.ChartPaths.push_back(iterator->path().u8string());
		}
		if (error) result.Failure = Error::ReadFailed;
		std::sort(result.ChartPaths.begin(), result.ChartPaths.end());
		return result;
	}

	static u16 Read16(std::string_view data, size_t offset)
	{
		return static_cast<u16>(static_cast<u8>(data[offset]) | (static_cast<u16>(static_cast<u8>(data[offset + 1])) << 8));
	}
	static u32 Read32(std::string_view data, size_t offset)
	{
		return static_cast<u32>(Read16(data, offset)) | (static_cast<u32>(Read16(data, offset + 2)) << 16);
	}
	static b8 HasBytes(std::string_view data, size_t offset, size_t count)
	{
		return offset <= data.size() && count <= data.size() - offset;
	}
	static u32 CRC32(std::string_view data)
	{
		static const std::array<u32, 256> table = []
		{
			std::array<u32, 256> values {};
			for (u32 index = 0; index < values.size(); index++)
			{
				u32 value = index;
				for (i32 bit = 0; bit < 8; bit++) value = (value >> 1) ^ ((value & 1) ? 0xEDB88320u : 0);
				values[index] = value;
			}
			return values;
		}();
		u32 value = 0xFFFFFFFFu;
		for (unsigned char byte : data) value = table[(value ^ byte) & 255] ^ (value >> 8);
		return value ^ 0xFFFFFFFFu;
	}
	static b8 IsUTF8(std::string_view value)
	{
		return !value.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) > 0;
	}
	static b8 IsSafeRelativeName(std::string_view name)
	{
		if (name.empty() || name.front() == '/' || name.find('\0') != std::string_view::npos) return false;
		const fs::path path = fs::u8path(name);
		if (path.has_root_path()) return false;
		for (const fs::path& part : path)
		{
			const std::string component = part.u8string();
			if (component.empty()) continue; // trailing slash on directory entries
			if (component == "." || component == ".." || component.back() == '.' || component.back() == ' ') return false;
			for (unsigned char character : component)
				if (character < 32 || std::string_view(":<>\"|?*\\").find(character) != std::string_view::npos) return false;
			const std::string_view base(component.data(), component.find('.') == std::string::npos ? component.size() : component.find('.'));
			if (ASCII::MatchesInsensitive(base, "CON") || ASCII::MatchesInsensitive(base, "PRN") || ASCII::MatchesInsensitive(base, "AUX") || ASCII::MatchesInsensitive(base, "NUL")
				|| ASCII::MatchesInsensitive(base, "CONIN$") || ASCII::MatchesInsensitive(base, "CONOUT$")) return false;
			if (base.size() == 4 && (ASCII::StartsWithInsensitive(base, "COM") || ASCII::StartsWithInsensitive(base, "LPT")) && base[3] >= '1' && base[3] <= '9') return false;
			if (base.size() == 5 && (ASCII::StartsWithInsensitive(base, "COM") || ASCII::StartsWithInsensitive(base, "LPT")))
			{
				const std::string_view digit = base.substr(3);
				if (digit == "\xC2\xB9" || digit == "\xC2\xB2" || digit == "\xC2\xB3") return false;
			}
		}
		return true;
	}

	struct ZipEntry
	{
		std::string Name;
		u32 CompressedSize, Size, CRC;
		size_t DataOffset;
		u16 Method;
		b8 IsDirectory;
	};
	static Error ParseZip(std::string_view data, std::vector<ZipEntry>& entries)
	{
		if (data.size() < 22) return Error::InvalidZip;
		size_t endOffset = data.size() - 22;
		const size_t searchStart = data.size() > 65557 ? data.size() - 65557 : 0;
		for (;; endOffset--)
		{
			if (Read32(data, endOffset) == 0x06054B50 && endOffset + 22 + Read16(data, endOffset + 20) == data.size()) break;
			if (endOffset == searchStart) return Error::InvalidZip;
		}
		const u16 count = Read16(data, endOffset + 10);
		if (Read16(data, endOffset + 4) != 0 || Read16(data, endOffset + 6) != 0 || Read16(data, endOffset + 8) != count
			|| count == 0xFFFF || Read32(data, endOffset + 12) == 0xFFFFFFFF || Read32(data, endOffset + 16) == 0xFFFFFFFF) return Error::UnsupportedZip;
		size_t offset = Read32(data, endOffset + 16);
		const size_t centralSize = Read32(data, endOffset + 12);
		if (offset > endOffset || centralSize != endOffset - offset) return Error::InvalidZip;
		u64 totalSize = 0;
		for (u32 index = 0; index < count; index++)
		{
			if (!HasBytes(data, offset, 46) || Read32(data, offset) != 0x02014B50) return Error::InvalidZip;
			const u16 flags = Read16(data, offset + 8), method = Read16(data, offset + 10);
			if ((flags & (1 | 64 | 8192)) != 0) return Error::PasswordProtected;
			if ((flags & 32) != 0 || (method != 0 && method != 8) || Read16(data, offset + 34) != 0) return Error::UnsupportedZip;
			const size_t nameSize = Read16(data, offset + 28), extraSize = Read16(data, offset + 30), commentSize = Read16(data, offset + 32);
			const size_t recordSize = 46 + nameSize + extraSize + commentSize;
			if (!HasBytes(data, offset, recordSize) || recordSize > endOffset - offset) return Error::InvalidZip;
			const std::string_view rawName = data.substr(offset + 46, nameSize);
			if (rawName.empty()) return Error::UnsafePath;
			std::string name;
			if ((flags & 2048) != 0)
			{
				if (!IsUTF8(rawName)) return Error::InvalidZip;
				name = rawName;
			}
			else name = IsUTF8(rawName) ? std::string(rawName) : UTF8::FromShiftJIS(rawName);
			for (size_t extra = offset + 46 + nameSize, extraEnd = extra + extraSize; extra < extraEnd;)
			{
				if (extraEnd - extra < 4) return Error::InvalidZip;
				const u16 tag = Read16(data, extra), size = Read16(data, extra + 2);
				extra += 4;
				if (size > extraEnd - extra) return Error::InvalidZip;
				if (tag == 0x0001) return Error::UnsupportedZip; // ZIP64
				if (tag == 0x7075 && size >= 5 && data[extra] == 1 && Read32(data, extra + 1) == CRC32(rawName))
				{
					const std::string_view unicodeName = data.substr(extra + 5, size - 5);
					if (!IsUTF8(unicodeName)) return Error::InvalidZip;
					name = unicodeName;
				}
				extra += size;
			}
			std::replace(name.begin(), name.end(), '\\', '/');
			if (!IsSafeRelativeName(name)) return Error::UnsafePath;
			const u32 attributes = Read32(data, offset + 38), unixType = (attributes >> 16) & 0170000;
			if (unixType != 0 && unixType != 0100000 && unixType != 0040000) return Error::UnsafePath;
			ZipEntry entry { name, Read32(data, offset + 20), Read32(data, offset + 24), Read32(data, offset + 16), 0, method,
				name.back() == '/' || (attributes & 16) != 0 || unixType == 0040000 };
			if (entry.CompressedSize == 0xFFFFFFFF || entry.Size == 0xFFFFFFFF || Read32(data, offset + 42) == 0xFFFFFFFF) return Error::UnsupportedZip;
			totalSize += entry.Size;
			if (entry.Size > MaxEntryBytes || entry.CompressedSize > MaxZipBytes || totalSize > MaxExtractedBytes) return Error::SizeLimit;
			if (entry.IsDirectory && entry.Size != 0) return Error::InvalidZip;
			const size_t local = Read32(data, offset + 42);
			if (!HasBytes(data, local, 30) || Read32(data, local) != 0x04034B50 || Read16(data, local + 8) != method || Read16(data, local + 6) != flags) return Error::InvalidZip;
			const size_t localNameSize = Read16(data, local + 26), localExtraSize = Read16(data, local + 28);
			entry.DataOffset = local + 30 + localNameSize + localExtraSize;
			if (!HasBytes(data, local + 30, localNameSize + localExtraSize) || data.substr(local + 30, localNameSize) != rawName
				|| !HasBytes(data, entry.DataOffset, entry.CompressedSize) || entry.DataOffset > Read32(data, endOffset + 16)
				|| entry.CompressedSize > Read32(data, endOffset + 16) - entry.DataOffset) return Error::InvalidZip;
			entries.push_back(std::move(entry));
			offset += recordSize;
		}
		return offset == endOffset ? Error::None : Error::InvalidZip;
	}

	static b8 EnsureSafeDirectories(const fs::path& root, const fs::path& relative)
	{
		fs::path current = root;
		std::error_code error;
		for (const fs::path& part : relative)
		{
			if (part.empty()) continue;
			current /= part;
			fs::create_directory(current, error);
			if (error || IsReparsePoint(current) || !fs::is_directory(current, error) || error) return false;
		}
		return true;
	}
	static b8 WriteNewFile(const fs::path& path, std::string_view content)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		DWORD written = 0;
		const b8 success = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) && written == content.size();
		const b8 closed = CloseHandle(file) != 0;
		return success && closed;
	}

	static Error CreateDestination(std::string_view archivePath, std::string_view extractionRoot, fs::path& destination)
	{
		const fs::path root = fs::u8path(extractionRoot);
		std::error_code error;
		fs::create_directories(root, error);
		if (error || IsReparsePoint(root)) return Error::ExtractionFailed;
		const std::string stem = fs::u8path(archivePath).stem().u8string();
		if (!IsSafeRelativeName(stem)) return Error::UnsafePath;
		for (i32 suffix = 0; ; suffix++)
		{
			destination = root / fs::u8path(stem + (suffix == 0 ? "" : " (" + std::to_string(suffix) + ")"));
			if (fs::create_directory(destination, error)) return Error::None;
			if (error || suffix == 10000) return Error::ExtractionFailed;
		}
	}

	Result ExtractZip(std::string_view zipPath, std::string_view extractionRoot, i32 searchDepth)
	{
		Result result;
		result.DirectoryPath = zipPath;
		std::ifstream input(fs::u8path(zipPath), std::ios::binary | std::ios::ate);
		if (!input || input.tellg() < 0) { result.Failure = Error::ReadFailed; return result; }
		const u64 size = static_cast<u64>(input.tellg());
		if (size > MaxZipBytes) { result.Failure = Error::SizeLimit; return result; }
		std::string data(static_cast<size_t>(size), '\0');
		input.seekg(0);
		if (!input.read(data.data(), static_cast<std::streamsize>(size))) { result.Failure = Error::ReadFailed; return result; }
		std::vector<ZipEntry> entries;
		result.Failure = ParseZip(data, entries);
		if (result.Failure != Error::None) return result;

		fs::path destination;
		result.Failure = CreateDestination(zipPath, extractionRoot, destination);
		if (result.Failure != Error::None) return result;
		result.DirectoryPath = destination.u8string();
		for (const ZipEntry& entry : entries)
		{
			const fs::path relative = fs::u8path(entry.Name);
			if (!EnsureSafeDirectories(destination, entry.IsDirectory ? relative : relative.parent_path()))
				{ result.Failure = Error::ExtractionFailed; return result; }
			std::string decoded;
			const std::string_view compressed(data.data() + entry.DataOffset, entry.CompressedSize);
			if (entry.Method == 0)
			{
				if (entry.CompressedSize != entry.Size) { result.Failure = Error::InvalidZip; return result; }
				decoded = compressed;
			}
			else
			{
				decoded.resize(std::max<size_t>(1, entry.Size));
				const int decodedSize = stbi_zlib_decode_noheader_buffer(decoded.data(), static_cast<int>(decoded.size()), compressed.data(), static_cast<int>(compressed.size()));
				if (decodedSize < 0 || static_cast<u32>(decodedSize) != entry.Size) { result.Failure = Error::InvalidZip; return result; }
				decoded.resize(entry.Size);
			}
			if (CRC32(decoded) != entry.CRC) { result.Failure = Error::InvalidZip; return result; }
			if (!entry.IsDirectory && !WriteNewFile(destination / relative, decoded)) { result.Failure = Error::ExtractionFailed; return result; }
		}
		return ReadFolder(result.DirectoryPath, searchDepth);
	}

	b8 IsArchivePath(std::string_view path)
	{
		const std::string extension = fs::u8path(path).extension().u8string();
		return ASCII::MatchesInsensitive(extension, ".zip") || ASCII::MatchesInsensitive(extension, ".7z")
			|| ASCII::MatchesInsensitive(extension, ".rar") || ASCII::MatchesInsensitive(extension, ".lzh");
	}

	struct ArchiveReader
	{
		archive* Handle = archive_read_new();
		~ArchiveReader() { if (Handle != nullptr) archive_read_free(Handle); }
	};
	static Error ArchiveFailure(archive* reader)
	{
		if (archive_format(reader) != 0 && archive_read_has_encrypted_entries(reader) > 0) return Error::PasswordProtected;
		const char* message = archive_error_string(reader);
		if (message != nullptr)
		{
			std::string lower = message;
			for (char& character : lower) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
			// Some encrypted headers fail before the library can set the encryption flag.
			if (lower.find("encrypt") != std::string::npos || lower.find("passphrase") != std::string::npos || lower.find("password") != std::string::npos)
				return Error::PasswordProtected;
			if (lower.find("unsupported") != std::string::npos || lower.find("not supported") != std::string::npos)
				return Error::UnsupportedArchive;
		}
		return Error::InvalidArchive;
	}

	Result ExtractArchive(std::string_view archivePath, std::string_view extractionRoot, i32 searchDepth)
	{
		if (ASCII::MatchesInsensitive(fs::u8path(archivePath).extension().u8string(), ".zip"))
			return ExtractZip(archivePath, extractionRoot, searchDepth);
		Result result;
		result.DirectoryPath = archivePath;
		if (!IsArchivePath(archivePath)) { result.Failure = Error::UnsupportedArchive; return result; }
		std::ifstream input(fs::u8path(archivePath), std::ios::binary | std::ios::ate);
		if (!input || input.tellg() < 0) { result.Failure = Error::ReadFailed; return result; }
		const u64 size = static_cast<u64>(input.tellg());
		if (size > MaxZipBytes) { result.Failure = Error::SizeLimit; return result; }
		std::string data(static_cast<size_t>(size), '\0');
		input.seekg(0);
		if (!input.read(data.data(), static_cast<std::streamsize>(size))) { result.Failure = Error::ReadFailed; return result; }
		ArchiveReader reader;
		if (reader.Handle == nullptr) { result.Failure = Error::ExtractionFailed; return result; }
		archive_read_support_filter_none(reader.Handle);
		archive_read_support_format_7zip(reader.Handle);
		archive_read_support_format_rar(reader.Handle);
		archive_read_support_format_rar5(reader.Handle);
		archive_read_support_format_lha(reader.Handle);
		archive_read_set_format_option(reader.Handle, "lha", "hdrcharset", "CP932");
		archive_read_set_format_option(reader.Handle, "rar", "hdrcharset", "CP932");
		if (archive_read_open_memory(reader.Handle, data.data(), data.size()) != ARCHIVE_OK)
			{ result.Failure = ArchiveFailure(reader.Handle); return result; }
		fs::path destination;
		u64 totalSize = 0;
		archive_entry* entry = nullptr;
		for (;;)
		{
			const int status = archive_read_next_header(reader.Handle, &entry);
			if ((archive_format(reader.Handle) != 0 && archive_read_has_encrypted_entries(reader.Handle) > 0) || (entry != nullptr && archive_entry_is_encrypted(entry) > 0))
				{ result.Failure = Error::PasswordProtected; return result; }
			if (status == ARCHIVE_EOF) break;
			if (status != ARCHIVE_OK) { result.Failure = ArchiveFailure(reader.Handle); return result; }
			const char* rawName = archive_entry_pathname_utf8(entry);
			if (rawName == nullptr || !IsUTF8(rawName)) { result.Failure = Error::InvalidArchive; return result; }
			std::string name = rawName;
			std::replace(name.begin(), name.end(), '\\', '/');
			const bool isDirectory = archive_entry_filetype(entry) == AE_IFDIR;
			if (!IsSafeRelativeName(name) || archive_entry_symlink(entry) != nullptr || archive_entry_hardlink(entry) != nullptr
				|| (!isDirectory && archive_entry_filetype(entry) != AE_IFREG))
				{ result.Failure = Error::UnsafePath; return result; }
			const la_int64_t declaredSize = archive_entry_size(entry);
			if (declaredSize < 0 || (!isDirectory && !archive_entry_size_is_set(entry)) || (isDirectory && declaredSize != 0))
				{ result.Failure = Error::InvalidArchive; return result; }
			if (static_cast<u64>(declaredSize) > MaxEntryBytes || static_cast<u64>(declaredSize) > MaxExtractedBytes - totalSize)
				{ result.Failure = Error::SizeLimit; return result; }
			std::string decoded;
			std::array<char, 65536> buffer;
			for (;;)
			{
				const la_ssize_t count = archive_read_data(reader.Handle, buffer.data(), buffer.size());
				if (count < 0) { result.Failure = ArchiveFailure(reader.Handle); return result; }
				if (count == 0) break;
				if (decoded.size() + static_cast<u64>(count) > MaxEntryBytes || totalSize + static_cast<u64>(count) > MaxExtractedBytes)
					{ result.Failure = Error::SizeLimit; return result; }
				decoded.append(buffer.data(), static_cast<size_t>(count));
				totalSize += static_cast<u64>(count);
			}
			if (decoded.size() != static_cast<u64>(declaredSize)) { result.Failure = Error::InvalidArchive; return result; }
			if (destination.empty())
			{
				result.Failure = CreateDestination(archivePath, extractionRoot, destination);
				if (result.Failure != Error::None) return result;
				result.DirectoryPath = destination.u8string();
			}
			const fs::path relative = fs::u8path(name);
			if (!EnsureSafeDirectories(destination, isDirectory ? relative : relative.parent_path())
				|| (!isDirectory && !WriteNewFile(destination / relative, decoded)))
				{ result.Failure = Error::ExtractionFailed; return result; }
		}
		if (archive_read_close(reader.Handle) != ARCHIVE_OK) { result.Failure = ArchiveFailure(reader.Handle); return result; }
		if (destination.empty())
		{
			result.Failure = CreateDestination(archivePath, extractionRoot, destination);
			if (result.Failure != Error::None) return result;
			result.DirectoryPath = destination.u8string();
		}
		return ReadFolder(result.DirectoryPath, searchDepth);
	}
}
