# Archive test fixtures

These uuencoded fixtures are copied unchanged from `libarchive-3.8.9/libarchive/test/` in the official source distribution vendored at `3rdparty/archive/vendor/libarchive-3.8.9.tar.xz`.

They exercise 7z and RAR encryption (including encrypted filenames), RAR4/RAR5 compression, and LHA/LZH headers and compression. The corresponding upstream tests and copyright/license notices remain in the vendored source distribution; see also `license_txt/libarchive.txt`.

Run `build/bin/x64-Release/TestFileDrop.exe` from the repository root. It decodes the fixtures into a unique directory under `build/file-drop-tests/` and preserves generated/extracted files for inspection.
