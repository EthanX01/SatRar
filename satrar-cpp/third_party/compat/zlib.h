#ifndef SATRAR_ZLIB_STUB_H
#define SATRAR_ZLIB_STUB_H

/* MiniSat headers include zlib though we never parse gzip DIMACS in-process. */
typedef struct gzFile_s* gzFile;

#ifdef __cplusplus
extern "C" {
#endif

int gzread(gzFile file, void* buf, unsigned len);

#ifdef __cplusplus
}
#endif

#endif