// Simulação mínima do FS do Horizon para testar pt_vfs/pt_db no PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <string>
typedef int8_t s8; typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t s64; typedef u32 Result;
#define MAKERESULT(m,d) ((((u32)(m))&0x1FF)|((((u32)(d))&0x1FFF)<<9))
#define R_MODULE(r) ((r)&0x1FF)
#define R_DESCRIPTION(r) (((r)>>9)&0x1FFF)
#define R_VALUE(r) ((r)&0x3FFFFF)
#define R_SUCCEEDED(r) ((r)==0)
#define R_FAILED(r) ((r)!=0)

#define FS_MAX_PATH 0x301
typedef struct { int id; } FsFileSystem;
typedef struct { int fd; std::string* key; bool w; } FsFile;
typedef enum { FsDirEntryType_Dir = 0, FsDirEntryType_File = 1 } FsDirEntryType;
enum { FsOpenMode_Read = 1, FsOpenMode_Write = 2, FsOpenMode_Append = 4 };
enum { FsReadOption_None = 0 }; enum { FsWriteOption_None = 0 };
static const Result kNotFound = MAKERESULT(2,1), kExists = MAKERESULT(2,2), kLocked = MAKERESULT(2,7), kRange = MAKERESULT(2,6005);
static FsFileSystem g_sd;
struct OpenInfo { int readers = 0, writers = 0; };
static std::map<std::string, OpenInfo> g_open;
static inline std::string phys(const char* p) { return std::string("sdmc:") + p; }
static inline int fsdevTranslatePath(const char* path, FsFileSystem** dev, char* out) {
  if (strncmp(path, "sdmc:", 5) != 0) return -1; *dev = &g_sd; snprintf(out, FS_MAX_PATH, "%s", path + 5); return 0; }
static inline Result fsFsGetEntryType(FsFileSystem*, const char* p, FsDirEntryType* t) {
  struct stat st; if (stat(phys(p).c_str(), &st)) return kNotFound; *t = S_ISDIR(st.st_mode) ? FsDirEntryType_Dir : FsDirEntryType_File; return 0; }
static inline Result fsFsCreateFile(FsFileSystem* fs, const char* p, s64, u32) {
  FsDirEntryType t; if (!fsFsGetEntryType(fs, p, &t)) return kExists; int fd = open(phys(p).c_str(), O_CREAT|O_WRONLY|O_BINARY, 0666); if (fd < 0) return kNotFound; close(fd); return 0; }
static inline bool busy(const std::string& k) { auto it = g_open.find(k); return it != g_open.end() && (it->second.readers || it->second.writers); }
static inline Result fsFsDeleteFile(FsFileSystem*, const char* p) { std::string k = phys(p); if (busy(k)) return kLocked; return unlink(k.c_str()) ? kNotFound : 0; }
static inline Result fsFsRenameFile(FsFileSystem*, const char* a, const char* b) {
  std::string ka = phys(a), kb = phys(b); if (busy(ka)) return kLocked; struct stat st; if (!stat(kb.c_str(), &st)) return kExists; return rename(ka.c_str(), kb.c_str()) ? kNotFound : 0; }
static inline Result fsFsOpenFile(FsFileSystem*, const char* p, u32 mode, FsFile* f) {
  std::string k = phys(p); OpenInfo& oi = g_open[k]; bool w = mode & FsOpenMode_Write;
  if (oi.writers || (w && oi.readers)) return kLocked;   // Horizon: escrita é exclusiva
  int fd = open(k.c_str(), (w ? O_RDWR : O_RDONLY) | O_BINARY); if (fd < 0) return kNotFound;
  f->fd = fd; f->w = w; f->key = new std::string(k); if (w) oi.writers++; else oi.readers++; return 0; }
static inline void fsFileClose(FsFile* f) { OpenInfo& oi = g_open[*f->key]; if (f->w) oi.writers--; else oi.readers--; close(f->fd); delete f->key; }
static inline Result fsFileGetSize(FsFile* f, s64* out) { struct stat st; fstat(f->fd, &st); *out = st.st_size; return 0; }
static inline Result fsFileSetSize(FsFile* f, s64 sz) { return ftruncate(f->fd, sz) ? kRange : 0; }
static inline Result fsFileFlush(FsFile* f) { fsync(f->fd); return 0; }
static inline Result fsFileRead(FsFile* f, s64 off, void* buf, u64 len, u32, u64* got) {
  s64 sz; fsFileGetSize(f, &sz); if (off > sz) return kRange; ssize_t n = pread(f->fd, buf, len, off); if (n < 0) return kRange; *got = n; return 0; }
static inline Result fsFileWrite(FsFile* f, s64 off, const void* buf, u64 len, u32) {
  s64 sz; fsFileGetSize(f, &sz); if (!f->w || off > sz) return kRange;  // sem buracos
  return pwrite(f->fd, buf, len, off) == (ssize_t)len ? 0 : kRange; }
static inline void randomGet(void* b, size_t n) { for (size_t i = 0; i < n; i++) ((u8*)b)[i] = rand(); }
static inline void svcSleepThread(s64) {}
// --- relógio e conta simulados para o teste do tracker ---
typedef struct { u64 uid[2]; } AccountUid;
typedef enum { TimeType_UserSystemClock = 0 } TimeType;
extern s64 g_fake_now;
static inline Result timeGetCurrentTime(TimeType, u64* t) { *t = (u64)g_fake_now; return 0; }
// --- datas e listagem de pastas (sem abrir arquivos) para roms.cpp ---
#include <dirent.h>
typedef struct { u64 created, modified, accessed; u8 is_valid; u8 padding[7]; } FsTimeStampRaw;
typedef struct { char name[FS_MAX_PATH]; u8 pad[3]; s8 type; u8 pad2[3]; s64 file_size; } FsDirectoryEntry;
typedef struct { DIR* d; std::string* base; } FsDir;
enum { FsDirOpenMode_ReadDirs = 1, FsDirOpenMode_ReadFiles = 2, FsDirOpenMode_NoFileSize = (int)0x80000000 };
static inline FsFileSystem* fsdevGetDeviceFileSystem(const char*) { return &g_sd; }
static inline Result fsFsGetFileTimeStampRaw(FsFileSystem*, const char* p, FsTimeStampRaw* ts) {
  struct stat st; if (stat(phys(p).c_str(), &st)) return kNotFound; memset(ts, 0, sizeof(*ts));
  ts->modified = (u64)st.st_mtime; ts->is_valid = 1; return 0; }
static inline Result fsFsOpenDirectory(FsFileSystem*, const char* p, u32, FsDir* d) {
  d->d = opendir(phys(p).c_str()); if (!d->d) return kNotFound; d->base = new std::string(phys(p)); return 0; }
static inline Result fsDirRead(FsDir* d, s64* total, size_t max, FsDirectoryEntry* out) {
  *total = 0;
  while ((size_t)*total < max) {
    dirent* e = readdir(d->d); if (!e) break;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    struct stat st; std::string full = *d->base + "/" + e->d_name; stat(full.c_str(), &st);
    FsDirectoryEntry& x = out[(*total)++]; memset(&x, 0, sizeof(x)); snprintf(x.name, sizeof(x.name), "%s", e->d_name);
    x.type = S_ISDIR(st.st_mode) ? FsDirEntryType_Dir : FsDirEntryType_File;
  }
  return 0; }
static inline void fsDirClose(FsDir* d) { closedir(d->d); delete d->base; }
