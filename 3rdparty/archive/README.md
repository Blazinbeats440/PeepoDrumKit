# Archive dependencies

Official source distributions are vendored in `vendor/`; the build does not download dependencies.

| Library | Version | Source | License |
| --- | --- | --- | --- |
| libarchive | 3.8.9 | https://www.libarchive.org/downloads/libarchive-3.8.9.tar.xz | BSD; see `license_txt/libarchive.txt` |
| XZ / liblzma | 5.8.4 | https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz | 0BSD for liblzma; see `license_txt/xz*.txt` |
| zlib | 1.3.2 | https://zlib.net/zlib-1.3.2.tar.gz | zlib; see `license_txt/zlib.txt` |
| bzip2 | 1.0.8 | https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz | bzip2; see `license_txt/bzip2.txt` |

`archive.props` builds static x64 libraries using the CMake tools bundled with Visual Studio 2022, then links them into the application/test executable. Build outputs stay in `build/archive-x64-<Configuration>/`. The first build takes longer because it builds these libraries. `ArchiveCMake` can override the CMake executable path.

The application enables 7z, RAR/RAR5 and LHA/LZH readers only, with LZMA/LZMA2, Deflate and bzip2 support. Encrypted archives are rejected without a password prompt. RAR formats are subject to libarchive's implementation limitations; unsupported compression and multi-volume archives may fail. Zstandard-compressed 7z archives are not supported.

`ApplyRarCRC.cmake` patches the extracted libarchive 3.8.9 source to verify stored RAR file CRCs before its early EOF return. The official source distribution remains unchanged. The patch checks its exact context and fails if an upgrade changes that code. The file-drop tests cover this corruption case.
