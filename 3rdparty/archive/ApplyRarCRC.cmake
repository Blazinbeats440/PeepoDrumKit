# libarchive 3.8.9 returns EOF before read_data_stored can verify its CRC.
# Keep the original distribution intact and patch only the extracted build source.
set(path "${SOURCE_DIR}/libarchive/archive_read_support_format_rar.c")
file(READ "${path}" source)
set(original [=[  if (rar->entry_eof || rar->offset_seek >= rar->unp_size) {
    *size = 0;
    *offset = rar->offset;
    return (ARCHIVE_EOF);
  }]=])
set(patched [=[  if (rar->entry_eof || rar->offset_seek >= rar->unp_size) {
#ifndef DONT_FAIL_ON_CRC_ERROR
    if (!rar->entry_eof && rar->compression_method == COMPRESS_METHOD_STORE &&
        rar->file_crc != rar->crc_calculated) {
      archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
                        "File CRC error");
      return (ARCHIVE_FAILED);
    }
#endif
    *size = 0;
    *offset = rar->offset;
    return (ARCHIVE_EOF);
  }]=])
string(FIND "${source}" "${patched}" already_patched)
if(NOT already_patched EQUAL -1)
    return()
endif()
string(FIND "${source}" "${original}" position)
if(position EQUAL -1)
    message(FATAL_ERROR "libarchive RAR CRC patch context changed; review the patch before updating libarchive.")
endif()
string(REPLACE "${original}" "${patched}" source "${source}")
file(WRITE "${path}" "${source}")
