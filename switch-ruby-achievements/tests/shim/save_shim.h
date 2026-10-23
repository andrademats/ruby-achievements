#pragma once
typedef enum { FsSaveDataType_Account = 1 } FsSaveDataType;
typedef enum { FsSaveDataSpaceId_User = 1 } FsSaveDataSpaceId;
typedef struct { u64 application_id; AccountUid uid; u64 system_save_data_id; u8 save_data_type; u8 rank; uint16_t index; } FsSaveDataAttribute;
extern int g_save_opens;
static inline Result fsOpenSaveDataFileSystem(FsFileSystem* fs, FsSaveDataSpaceId, const FsSaveDataAttribute*) { g_save_opens++; fs->id = 7; return 0; }
static inline void fsFsClose(FsFileSystem*) {}
#ifndef PT_SHIM_ACCOUNT_UID_VALID
#define PT_SHIM_ACCOUNT_UID_VALID
static inline bool accountUidIsValid(const AccountUid* u) { return u->uid[0] || u->uid[1]; }
#endif
extern Result g_save_rc;  // != 0 simula o FS recusando a montagem
static inline Result fsOpenReadOnlySaveDataFileSystem(FsFileSystem* fs, FsSaveDataSpaceId, const FsSaveDataAttribute*) { g_save_opens++; fs->id = 7; return g_save_rc; }
