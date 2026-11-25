
#pragma once
#ifdef _MSC_VER
#include <io.h>
#include <intrin.h>
#include <process.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 0
#define F_OK 0
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define STDIN_FILENO 0
#endif
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define getc_unlocked _fgetc_nolock
#define putc_unlocked _fputc_nolock
#define pclose _pclose
#define popen _popen
#define __PRETTY_FUNCTION__ __FUNCSIG__
#define __attribute__(attribute)
#define __builtin_prefetch(address, rw, locality) ((void)0)
#define __builtin_clz(value) satrar_count_leading_zeroes(value)
inline int satrar_count_leading_zeroes(unsigned value) {
    unsigned long index = 0;
    return _BitScanReverse(&index, value) ? 31 - static_cast<int>(index) : 32;
}
#endif
