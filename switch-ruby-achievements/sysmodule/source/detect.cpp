#include "detect.h"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_title.h"
#include "pt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

u64 detect_running_title(void) {
    u64 pid = 0, tid = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) return 0;
    if (R_FAILED(pminfoGetProgramId(&tid, pid))) return 0;
    return tid;
}

bool detect_build_id(char out[65]) {
    out[0] = '\0';
    u64 pid = 0;
    if (R_FAILED(pmdmntGetApplicationProcessId(&pid))) return false;
    if (R_FAILED(ldrDmntInitialize())) return false;
    // Como o dmnt (cheats): pede só 2 módulos; com 2, o principal é o segundo (o primeiro é o rtld).
    LoaderModuleInfo mods[2];
    s32 n = 0;
    Result rc = ldrDmntGetProcessModuleInfo(pid, mods, 2, &n);
    ldrDmntExit();
    if (R_FAILED(rc) || n < 1 || n > 2) return false;
    const LoaderModuleInfo& m = mods[n == 2 ? 1 : 0];
    for (int i = 0; i < 0x20; i++) snprintf(out + 2 * i, 3, "%02X", m.build_id[i]);
    return true;
}

bool detect_home_in_front(u64 app_tid) {
    if (app_tid == 0) return true;
    // Sessão curta: o app (importação do NX Activity Log) também usa o pdm:qry.
    if (R_FAILED(pdmqryInitialize())) return false;
    bool home = false;
    s32 total = 0, first = 0, last = 0;
    if (R_SUCCEEDED(pdmqryGetAvailablePlayEventRange(&total, &first, &last)) && total > 0) {
        constexpr s32 kRecent = 16;
        PdmPlayEvent ev[kRecent];
        s32 start = last - kRecent + 1 > first ? last - kRecent + 1 : first, got = 0;
        if (R_SUCCEEDED(pdmqryQueryPlayEvent(start, ev, kRecent, &got))) {
            for (s32 i = got - 1; i >= 0; i--) {  // do mais novo para o mais antigo
                const PdmPlayEvent& e = ev[i];
                if (e.play_event_type != PdmPlayEventType_Applet) continue;
                // Nos eventos do pdm, as metades de 32 bits do u64 vêm trocadas.
                u64 tid = ((u64)e.event_data.applet.program_id[0] << 32) | e.event_data.applet.program_id[1];
                u8 type = e.event_data.applet.event_type;
                bool in = type == PdmAppletEventType_InFocus;
                bool out = type == PdmAppletEventType_OutOfFocus || type == PdmAppletEventType_OutOfFocus4;
                if (tid == DETECT_QLAUNCH_TID && in) { home = true; break; }
                if (tid == app_tid && (in || out)) { home = out; break; }
            }
        }
    }
    pdmqryExit();
    return home;
}

u64 detect_focused_title(void) {
    char buf[64];
    if (pt_read_file(PT_FOCUS_PATH, buf, sizeof(buf)) <= 0) return 0;
    u64 tid = strtoull(pt_trim(buf), NULL, 16);
    // Só aplicativos (0100...): a grade da Home também tem atalhos do sistema.
    return (tid >> 56) == 0x01 || (tid >> 56) == 0x05 ? tid : 0;
}

bool detect_is_forwarder(u64 tid) {
    return (tid >> 56) == 0x05;
}

bool detect_is_ghost(u64 tid) {
    // Cache do último título consultado: chamado a cada segundo.
    static u64 last_tid = 0;
    static bool last_ghost = false;
    if (tid == last_tid) return last_ghost;
    last_tid = tid;
    last_ghost = false;
    if (!detect_is_forwarder(tid)) return false;
    char buf[4096], hex[17];
    snprintf(hex, sizeof(hex), "%016lX", tid);
    if (pt_read_file(PT_GHOST_TIDS_PATH, buf, sizeof(buf)) > 0 && strstr(buf, hex)) {
        last_ghost = true;
        pt_log("titulo %s e um forwarder fantasma do Ruby Achievements: nao conta tempo", hex);
    }
    return last_ghost;
}

// Títulos cujo NACP travou o console numa leitura anterior.
constexpr int kMaxSkip = 32;
static u64 g_skip[kMaxSkip];
static int g_skip_n = 0;

static bool is_skipped(u64 tid) {
    for (int i = 0; i < g_skip_n; i++)
        if (g_skip[i] == tid) return true;
    return false;
}

static void write_synced(const char* path, const char* mode, const char* text) {
    FILE* f = fopen(path, mode);
    if (!f) return;
    fputs(text, f);
    fflush(f);
    fsync(fileno(f));  // a marca precisa estar no cartão antes da chamada arriscada
    fclose(f);
}

void detect_init(void) {
    char buf[1024];
    if (pt_read_file(PT_NACP_SKIP_PATH, buf, sizeof(buf)) > 0) {
        char* save = NULL;
        for (char* line = strtok_r(buf, "\n", &save); line && g_skip_n < kMaxSkip;
             line = strtok_r(NULL, "\n", &save)) {
            u64 tid = strtoull(pt_trim(line), NULL, 16);
            if (tid) g_skip[g_skip_n++] = tid;
        }
        if (g_skip_n) pt_log("titulos sem leitura de NACP (%s): %d", PT_NACP_SKIP_PATH, g_skip_n);
    }

    // Marca que sobrou: o console travou (ou desligou) lendo o NACP desse título.
    if (pt_read_file(PT_PROBE_PATH, buf, sizeof(buf)) > 0) {
        u64 tid = strtoull(pt_trim(buf), NULL, 16);
        if (tid && !is_skipped(tid) && g_skip_n < kMaxSkip) {
            g_skip[g_skip_n++] = tid;
            char line[24];
            snprintf(line, sizeof(line), "%016lX\n", tid);
            write_synced(PT_NACP_SKIP_PATH, "a", line);
            pt_log("ATENCAO: o console reiniciou durante a leitura do NACP de %016lX. Esse titulo fica sem "
                   "nome/icone automaticos (edite no app; apague a linha em %s para tentar de novo)",
                   tid, PT_NACP_SKIP_PATH);
        }
        unlink(PT_PROBE_PATH);
    }
}

bool detect_title_info(u64 tid, char* name, size_t name_cap, char* icon_path, size_t icon_cap) {
    name[0] = '\0';
    icon_path[0] = '\0';
    if (is_skipped(tid)) {
        pt_log("NACP %016lX: pulado (travou o console antes)", tid);
        return false;
    }

    char mark[24];
    snprintf(mark, sizeof(mark), "%016lX\n", tid);
    write_synced(PT_PROBE_PATH, "w", mark);
    pt_log("NACP %016lX: lendo (nsGetApplicationControlData)", tid);
    u64 size = 0;
    Result rc = pt_title_read(tid, name, name_cap, icon_path, icon_cap, &size);
    unlink(PT_PROBE_PATH);
    pt_log("NACP %016lX: rc=0x%X, %llu bytes, nome \"%s\", icone %s", tid, rc, (unsigned long long)size, name,
           icon_path[0] ? "salvo" : "nao salvo");
    return R_SUCCEEDED(rc) && name[0] != '\0';
}

bool detect_current_account(AccountUid* uid) {
    if (R_SUCCEEDED(accountGetLastOpenedUser(uid)) && accountUidIsValid(uid)) return true;
    if (R_SUCCEEDED(accountGetPreselectedUser(uid)) && accountUidIsValid(uid)) return true;
    return false;
}
