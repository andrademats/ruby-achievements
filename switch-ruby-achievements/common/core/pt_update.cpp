#include "pt_update.h"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ULOG(...) pt_log_file(PT_UPDATE_LOG_PATH, __VA_ARGS__)

namespace {

struct Target {
    const char* rel;   // relativo à raiz do SD
    const char* name;  // asset da release e arquivo em staging/ e backup/
};

// Lista fixa. Sem boot2.flag de propósito: se o usuário o apagou (sysmodule sem iniciar no
// boot), a atualização não o recria.
const Target kTargets[] = {
    {"switch/ruby_achievements/ruby_achievements.nro", "ruby_achievements.nro"},
    {"switch/.overlays/ruby-achievements-overlay.ovl", "ruby-achievements-overlay.ovl"},
    {"atmosphere/contents/0100000000007A11/exefs.nsp", "exefs.nsp"},
    {"atmosphere/contents/0100000000007A11/toolbox.json", "toolbox.json"},
};
const int kCount = (int)(sizeof(kTargets) / sizeof(kTargets[0]));

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void backup_path(int i, char* out, size_t cap) {
    snprintf(out, cap, "%s/%s", PT_UPDATE_BACKUP_DIR, kTargets[i].name);
}

void ensure_dirs() {
    mkdir(PT_DIR, 0777);
    mkdir(PT_UPDATE_DIR, 0777);
    mkdir(PT_UPDATE_STAGING_DIR, 0777);
    mkdir(PT_UPDATE_BACKUP_DIR, 0777);
}

// staging/ e backup/ só recebem os nomes da lista fixa: apaga esses.
void clear_dir(const char* dir) {
    char path[256];
    for (int i = 0; i < kCount; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, kTargets[i].name);
        unlink(path);
    }
}

}  // namespace

int pt_update_target_count(void) { return kCount; }
const char* pt_update_target_rel(int i) { return i >= 0 && i < kCount ? kTargets[i].rel : ""; }
const char* pt_update_target_name(int i) { return i >= 0 && i < kCount ? kTargets[i].name : ""; }

int pt_update_target_find(const char* rel) {
    for (int i = 0; i < kCount; i++)
        if (rel && !strcmp(rel, kTargets[i].rel)) return i;
    return -1;
}

void pt_update_target_sd_path(int i, char* out, size_t cap) { snprintf(out, cap, "sdmc:/%s", pt_update_target_rel(i)); }

void pt_update_staged_path(int i, char* out, size_t cap) {
    snprintf(out, cap, "%s/%s", PT_UPDATE_STAGING_DIR, pt_update_target_name(i));
}

int pt_update_target_installed(int i) {
    if (i < 0 || i >= kCount) return 0;
    char p[256];
    pt_update_target_sd_path(i, p, sizeof(p));
    return exists(p) ? 1 : 0;
}

int pt_update_prepare_staging(void) {
    ensure_dirs();
    clear_dir(PT_UPDATE_STAGING_DIR);
    struct stat st;
    return stat(PT_UPDATE_STAGING_DIR, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

int pt_update_apply(const char* version, const int* targets, int n, char* err, size_t cap) {
    char sd[256], staged[256], bak[256];
    if (n <= 0 || n > kCount) {
        snprintf(err, cap, "nenhum arquivo para trocar");
        return -1;
    }
    for (int k = 0; k < n; k++) {
        int i = targets[k];
        if (i < 0 || i >= kCount) {
            snprintf(err, cap, "destino fora da lista (%d)", i);
            return -1;
        }
        for (int j = 0; j < k; j++)
            if (targets[j] == i) {
                snprintf(err, cap, "destino repetido: %s", kTargets[i].rel);
                return -1;
            }
        pt_update_target_sd_path(i, sd, sizeof(sd));
        pt_update_staged_path(i, staged, sizeof(staged));
        if (!exists(sd) || !exists(staged)) {
            snprintf(err, cap, "%s: %s", exists(sd) ? "arquivo baixado sumiu" : "não instalado no SD", kTargets[i].rel);
            return -1;
        }
    }

    ensure_dirs();
    clear_dir(PT_UPDATE_BACKUP_DIR);
    char journal[512];
    int len = snprintf(journal, sizeof(journal), "%s\n", version ? version : "?");
    for (int k = 0; k < n && len < (int)sizeof(journal); k++)
        len += snprintf(journal + len, sizeof(journal) - len, "%d\n", targets[k]);
    if (len >= (int)sizeof(journal) || pt_write_file_atomic(PT_UPDATE_JOURNAL_PATH, journal, (size_t)len) != 0) {
        snprintf(err, cap, "não foi possível gravar o diário da atualização");
        return -1;
    }
    ULOG("troca para a versao %s: %d arquivo(s)", version ? version : "?", n);

    for (int k = 0; k < n; k++) {
        int i = targets[k];
        pt_update_target_sd_path(i, sd, sizeof(sd));
        pt_update_staged_path(i, staged, sizeof(staged));
        backup_path(i, bak, sizeof(bak));
        if (rename(sd, bak) != 0) {
            snprintf(err, cap, "não foi possível mover %s para o backup", kTargets[i].rel);
            ULOG("falhou: %s", err);
            pt_update_recover();
            return -1;
        }
        if (rename(staged, sd) != 0) {
            snprintf(err, cap, "não foi possível instalar %s", kTargets[i].rel);
            ULOG("falhou: %s", err);
            pt_update_recover();
            return -1;
        }
        ULOG("trocado: %s", kTargets[i].rel);
    }

    unlink(PT_UPDATE_JOURNAL_PATH);
    pt_write_file_atomic(PT_UPDATE_PENDING_PATH, version ? version : "?", strlen(version ? version : "?"));
    ULOG("versao %s instalada; falta reiniciar o console", version ? version : "?");
    return 0;
}

int pt_update_recover(void) {
    char buf[512];
    if (pt_read_file(PT_UPDATE_JOURNAL_PATH, buf, sizeof(buf)) < 0) return 0;
    char* save = NULL;
    char* version = strtok_r(buf, "\n", &save);
    ULOG("diario encontrado (versao %s): desfazendo a troca interrompida", version ? version : "?");
    int restored = 0;
    bool failed = false;
    char sd[256], bak[256];
    for (char* line = strtok_r(NULL, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        int i = atoi(line);
        if (i < 0 || i >= kCount) continue;
        backup_path(i, bak, sizeof(bak));
        if (!exists(bak)) continue;  // este destino não chegou a ser movido: está intacto
        pt_update_target_sd_path(i, sd, sizeof(sd));
        unlink(sd);  // a versão nova (se chegou a entrar); o FS não sobrescreve no rename
        if (rename(bak, sd) == 0) {
            restored++;
            ULOG("restaurado: %s", kTargets[i].rel);
        } else {
            failed = true;
            ULOG("NAO restaurado: %s (backup em %s)", kTargets[i].rel, bak);
        }
    }
    if (failed) return -1;  // diário fica para tentar de novo na próxima chamada
    unlink(PT_UPDATE_JOURNAL_PATH);
    return restored;
}

int pt_update_pending(char* version, size_t cap) {
    char buf[64];
    if (pt_read_file(PT_UPDATE_PENDING_PATH, buf, sizeof(buf)) < 0) return 0;
    if (version && cap) snprintf(version, cap, "%s", pt_trim(buf));
    return 1;
}

void pt_update_boot_done(void) {
    if (!exists(PT_UPDATE_JOURNAL_PATH)) unlink(PT_UPDATE_PENDING_PATH);
}
