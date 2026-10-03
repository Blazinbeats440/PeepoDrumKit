cmake_minimum_required(VERSION 3.24)
project(PeepoBZip2 LANGUAGES C)
add_library(bz2 STATIC
    ${BZIP2_SOURCE_DIR}/blocksort.c ${BZIP2_SOURCE_DIR}/huffman.c
    ${BZIP2_SOURCE_DIR}/crctable.c ${BZIP2_SOURCE_DIR}/randtable.c
    ${BZIP2_SOURCE_DIR}/compress.c ${BZIP2_SOURCE_DIR}/decompress.c
    ${BZIP2_SOURCE_DIR}/bzlib.c)
target_compile_definitions(bz2 PRIVATE _CRT_SECURE_NO_WARNINGS)
install(TARGETS bz2 ARCHIVE DESTINATION lib)
install(FILES ${BZIP2_SOURCE_DIR}/bzlib.h DESTINATION include)
