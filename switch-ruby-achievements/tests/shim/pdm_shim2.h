// pdm/ns/acc simulados para testar a importação do app no PC.
#pragma once
#include <vector>
typedef int32_t s32; typedef uint16_t u16;
#define ACC_USER_LIST_SIZE 8
enum { AccountServiceType_Application = 0, AppletId_application = 0x01 };
typedef enum { PdmPlayEventType_Applet = 0, PdmPlayEventType_Account = 1 } PdmPlayEventType;
typedef enum {
    PdmAppletEventType_Launch = 0, PdmAppletEventType_Exit = 1, PdmAppletEventType_InFocus = 2,
    PdmAppletEventType_OutOfFocus = 3, PdmAppletEventType_OutOfFocus4 = 4, PdmAppletEventType_Exit5 = 5,
    PdmAppletEventType_Exit6 = 6
} PdmAppletEventType;
enum { PdmPlayLogPolicy_All = 0, PdmPlayLogPolicy_Unknown3 = 3 };
typedef struct {
    union {
        struct { u32 program_id[2]; u32 data; u8 applet_id; u8 storage_id; u8 log_policy; u8 event_type; u8 unused[0xc]; } applet;
        struct { u32 uid[4]; u32 application_id[2]; u8 type; } account;
        u8 data[0x1c];
    } event_data;
    u8 play_event_type;
    u8 pad[3];
    u64 timestamp_user, timestamp_network, timestamp_steady;
} PdmPlayEvent;
typedef struct {
    u64 program_id; u32 first_entry_index; u32 pad; u64 first_timestamp_user; u64 first_timestamp_network;
    u32 last_entry_index; u32 pad2; u64 last_timestamp_user; u64 last_timestamp_network; u64 playtime;
    u32 total_launches; u32 pad3;
} PdmPlayStatistics;
typedef struct { u64 application_id; u8 type; u8 unk[7]; } NsApplicationRecord;
typedef struct { u16 year; u8 month, day, hour, minute, second, pad; } TimeCalendarTime;
typedef struct { u32 a; } TimeCalendarAdditionalInfo;
static inline Result timeToCalendarTimeWithMyRule(u64, TimeCalendarTime*, TimeCalendarAdditionalInfo*) { return 1; }
#include "pt_util.h"

extern std::vector<PdmPlayEvent> g_events;
struct FakeStat { u64 tid; u64 playtime_s; u64 first; };
extern std::vector<FakeStat> g_stats;
extern AccountUid g_user, g_user2;
#include <utility>
extern std::vector<std::pair<int, FakeStat>> g_user_stats;
extern std::vector<u64> g_installed;

static inline Result accountInitialize(int) { return 0; }
static inline void accountExit() {}
static inline Result nsInitialize() { return 0; }
static inline void nsExit() {}
static inline Result setInitialize() { return 0; }
static inline void setExit() {}
static inline Result pdmqryInitialize() { return 0; }
static inline void pdmqryExit() {}
static inline Result accountListAllUsers(AccountUid* out, s32, s32* n) { out[0] = g_user; out[1] = g_user2; *n = 2; return 0; }
static inline Result nsListApplicationRecord(NsApplicationRecord* r, s32 max, s32 off, s32* got) {
    *got = 0;
    for (s32 i = off; i < (s32)g_installed.size() && *got < max; i++) r[(*got)++] = {g_installed[i], 0, {}};
    return 0;
}
typedef struct { u8 meta_type, storageID, rights_check, reserved; u32 version; u64 application_id; } NsApplicationContentMetaStatus;
enum { NcmContentMetaType_Application = 0x80, NcmContentMetaType_Patch = 0x81 };
extern std::vector<u64> g_archived;  // registro na Home, sem o conteúdo base
static inline Result nsListApplicationContentMetaStatus(u64 tid, s32, NsApplicationContentMetaStatus* l, s32, s32* n) {
    bool arch = false;
    for (u64 t : g_archived) arch |= t == tid;
    l[0] = {(u8)(arch ? NcmContentMetaType_Patch : NcmContentMetaType_Application), 0, 0, 0, 0, tid};
    *n = 1;
    return 0;
}
static inline Result pdmqryGetAvailablePlayEventRange(s32* total, s32* first, s32* last) {
    *total = (s32)g_events.size(); *first = 0; *last = (s32)g_events.size() - 1; return 0;
}
static inline Result pdmqryQueryPlayEvent(s32 idx, PdmPlayEvent* out, s32 count, s32* got) {
    *got = 0;
    for (s32 i = idx; i < (s32)g_events.size() && *got < count; i++) out[(*got)++] = g_events[i];
    return 0;
}
static inline Result pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(u64 tid, AccountUid u, bool, PdmPlayStatistics* st) {
    memset(st, 0, sizeof(*st));
    int idx = u.uid[0] == g_user.uid[0] ? 0 : 1;
    for (auto& p : g_user_stats)
        if (p.first == idx && p.second.tid == tid) { st->playtime = p.second.playtime_s * 1000000000ULL; st->first_timestamp_user = p.second.first; }
    return 0;
}
typedef enum { NcmStorageId_SdCard = 5 } NcmStorageId;

// Avatar da conta (data::user_avatar): sem imagem no PC.
typedef struct { u32 h; } AccountProfile;
#ifndef PT_SHIM_ACCOUNT_UID_VALID
#define PT_SHIM_ACCOUNT_UID_VALID
static inline bool accountUidIsValid(const AccountUid* u) { return u->uid[0] || u->uid[1]; }
#endif
static inline Result accountGetProfile(AccountProfile*, AccountUid) { return 1; }
static inline Result accountProfileGetImageSize(AccountProfile*, u32* s) { *s = 0; return 1; }
static inline Result accountProfileLoadImage(AccountProfile*, void*, u32, u32* g) { *g = 0; return 1; }
static inline void accountProfileClose(AccountProfile*) {}
