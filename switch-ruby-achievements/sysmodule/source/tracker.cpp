#include "tracker.h"
#include "achieve.h"
#include "detect.h"
#include "pt_log.h"
#include "roms.h"
#include "store.h"

#include "pt_config.h"
#include "pt_status.h"
#include "pt_util.h"

#include <stdio.h>
#include <string.h>

namespace {

// Uma sessão em memória. game_id/user_id/session_id ficam 0 até a gravação.
struct Sess {
    u64 tid;
    bool forwarder;
    char rom_path[512];   // preenchido => sessão de ROM de emulador
    char rom_label[128];
    char learn_rom[512];  // forwarder de jogo carregou esta ROM (aplicada no flush)
    bool learn_done;
    bool retarget;        // a sessão inteira passa para a ROM em rom_path (aplicado no flush)
    bool has_uid;
    AccountUid uid;
    bool user_dirty;
    bool settled;         // passou a janela de abertura (metadados liberados)
    int64_t game_id, user_id, session_id;
    int64_t start, end, counted;  // end = 0 => sessão viva
    bool untracked;       // tracking do jogo desativado: nada desta sessão é gravado
};

constexpr int kMaxClosed = 8;
constexpr int kSlowCheckSeconds = 5;
constexpr int kUserRecheckWindow = 120;  // jogos que escolhem o perfil após abrir
constexpr int kLockTimeoutMs = 300;
// Nos primeiros segundos de um título (carga pesada, emuladores) o sysmodule só
// conta o tempo: NACP, conta, banco e SD ficam para depois. Jogos do sistema
// (Title ID 01..., sem ser forwarder) carregam muito do SD na abertura: a
// janela é maior. Forwarders e homebrew precisam da janela curta para ligar
// a ROM aberta ao forwarder (kForwarderRomWindow).
constexpr int kSettleSeconds = 10;
constexpr int kSettleNativeSeconds = 60;
// Sleep até este tempo com o mesmo jogo ao voltar: a sessão continua (o tempo
// dormindo não conta). Pausas maiores fecham a sessão no instante do sleep.
constexpr int64_t kSleepResumeWindow = 3 * 3600;
// Forwarder que carrega uma ROM até este tempo após abrir é o forwarder dessa
// ROM (unificado com ela). Mais tarde, é um frontend (ex.: Tico, RetroArch).
constexpr int kForwarderRomWindow = 30;
constexpr int kScanSeconds = 30;  // varredura de saves de frontends sem histórico

Sess g_live;
bool g_live_active = false;
Sess g_closed[kMaxClosed];
int g_closed_n = 0;

int64_t g_last_now = 0;
int64_t g_last_flush = 0;
int64_t g_last_slow = 0;
int64_t g_last_game = 0;  // último jogo fechado (foco na Home)
int64_t g_profile = 0;
bool g_need_flush = true;
bool g_users_dirty = true;
// Assinatura da tabela users no último users.txt gravado. O app também cria
// perfis (importação do NX Activity Log) e o sysmodule não fica sabendo.
int64_t g_users_sig = -1;
int g_flush_failures = 0;
// Sessões deixadas abertas pelo processo anterior (desligamento, bateria,
// travamento) já foram fechadas neste boot.
bool g_orphans_closed = false;

int64_t g_last_scan = 0;

int64_t g_last_achieve = 0;  // última avaliação de conquistas do jogo aberto

// Home em primeiro plano e o jogo sob o cursor (HUD só na Home).
bool g_home = false;
u64 g_focus = 0;

const char* sess_rom(const Sess& s) { return s.rom_path[0] ? s.rom_path : s.learn_rom; }

// Jogo do sistema (não forwarder, não homebrew): sem ROM para procurar.
bool native_game(const Sess& s) { return !s.forwarder && !s.rom_path[0] && (s.tid >> 56) == 0x01; }

int settle_seconds(const Sess& s) { return native_game(s) ? kSettleNativeSeconds : kSettleSeconds; }

void refresh_user(Sess& s) {
    AccountUid uid;
    bool has = detect_current_account(&uid);
    bool same = has == s.has_uid && (!has || (uid.uid[0] == s.uid.uid[0] && uid.uid[1] == s.uid.uid[1]));
    if (same) return;
    s.has_uid = has;
    if (has) s.uid = uid;
    s.user_dirty = true;
    g_need_flush = true;
}

void start_live(u64 tid, int64_t now, const char* rom_path, const char* rom_label) {
    memset(&g_live, 0, sizeof(g_live));
    g_live.tid = tid;
    g_live.forwarder = detect_is_forwarder(tid);
    if (rom_path && rom_path[0]) {
        snprintf(g_live.rom_path, sizeof(g_live.rom_path), "%s", rom_path);
        snprintf(g_live.rom_label, sizeof(g_live.rom_label), "%s", rom_label ? rom_label : "");
    }
    g_live.start = now;
    g_live.user_dirty = true;
    g_live_active = true;
    g_need_flush = true;
    roms_baseline_start();  // só datas de arquivos: leve mesmo durante a abertura
    if (g_live.rom_path[0]) {
        // Troca de ROM dentro do mesmo título: já passou da abertura.
        g_live.settled = true;
        g_live.has_uid = detect_current_account(&g_live.uid);
        pt_log("sessao: ROM \"%s\" em %016lX", g_live.rom_label, tid);
    } else {
        pt_log("sessao: titulo %016lX aberto (%s); metadados em %d s", tid,
               g_live.forwarder ? "forwarder" : "nativo", settle_seconds(g_live));
    }
}

bool settling(int64_t now) {
    return g_live_active && !g_live.settled && now - g_live.start < settle_seconds(g_live);
}

// Fim da janela de abertura: lê o usuário e libera a gravação.
void settle_live(void) {
    g_live.settled = true;
    g_live.has_uid = detect_current_account(&g_live.uid);
    g_live.user_dirty = true;
    // Jogo do sistema: nenhuma varredura das pastas de saves de frontends
    // enquanto ele estiver aberto (disputa o SD com os carregamentos).
    if (!native_game(g_live)) roms_baseline_scan();
    g_last_scan = 0;
    g_need_flush = true;
    pt_log("sessao: %016lX liberado para gravacao (conta %s)", g_live.tid, g_live.has_uid ? "detectada" : "nao detectada");
}

void close_live(int64_t end) {
    if (!g_live_active) return;
    g_live_active = false;
    g_live.end = end < g_live.start ? g_live.start : end;
    g_need_flush = true;
    if (g_live.untracked)
        pt_log("sessao: %016lX encerrada, tracking desativado (%lld s nao gravados)", g_live.tid, (long long)g_live.counted);
    else
        pt_log("sessao: %016lX encerrada, %lld s contados", g_live.tid, (long long)g_live.counted);
    // Fechou ainda na janela de abertura: o usuário não chegou a ser lido.
    if (!g_live.settled) g_live.has_uid = detect_current_account(&g_live.uid);
    // Frontend sem histórico (Tico): o save state costuma sair ao fechar o jogo
    // ou o app. Uma última varredura atribui esta sessão à ROM.
    RomHit hit;
    if (g_live.settled && !native_game(g_live) && !g_live.rom_path[0] && !g_live.learn_rom[0] && g_live.counted > 0 &&
        roms_scan_now(&hit)) {
        snprintf(g_live.rom_path, sizeof(g_live.rom_path), "%s", hit.path);
        snprintf(g_live.rom_label, sizeof(g_live.rom_label), "%s", hit.label);
        g_live.retarget = true;
        pt_log("sessao: %lld s de %016lX redirecionados para a ROM \"%s\" (save ao fechar)", (long long)g_live.counted,
               g_live.tid, hit.label);
    }
    // Sessões vazias que nunca chegaram ao banco não geram linha.
    if (g_live.counted == 0 && g_live.session_id == 0) return;
    if (g_closed_n == kMaxClosed) {
        // Banco indisponível há muito tempo: descarta a mais antiga.
        pt_log("fila cheia, descartando sessao de %lld s", (long long)g_closed[0].counted);
        memmove(&g_closed[0], &g_closed[1], sizeof(Sess) * (kMaxClosed - 1));
        g_closed_n--;
    }
    g_closed[g_closed_n++] = g_live;
}

// Grava uma sessão. Retorna true quando está totalmente persistida.
bool flush_one(sqlite3* db, Sess& s, int64_t now, bool* users_changed) {
    if (s.end != 0 && s.counted == 0) {
        // Sessão fechada sem tempo contado (abriu e fechou no mesmo segundo).
        if (s.session_id != 0) store_delete_session(db, s.session_id);
        return true;
    }
    if (s.game_id == 0) {
        s.game_id = s.rom_path[0] ? store_game_for_rom(db, s.rom_path, s.rom_label, now)
                                  : store_game_for_title(db, s.tid, now);
        if (s.game_id == 0) return false;
    }
    if (s.forwarder && s.learn_rom[0] && !s.learn_done) {
        s.learn_done = true;
        if (!store_forwarder_learn_rom(db, s.game_id, s.learn_rom, now)) {
            // O forwarder já pertence a outra ROM: a sessão vai para esta.
            snprintf(s.rom_path, sizeof(s.rom_path), "%s", s.learn_rom);
            s.learn_rom[0] = '\0';
            s.retarget = true;
        }
        s.game_id = pt_db_resolve_game(db, s.game_id);
    }
    if (s.retarget) {
        // A ROM foi identificada depois: a sessão (desde o início) passa para ela.
        int64_t g = store_game_for_rom(db, s.rom_path, s.rom_label, now);
        if (g == 0) return false;
        if (s.session_id != 0 && g != s.game_id) {
            if (store_move_session(db, s.session_id, g) != 0) return false;
            pt_db_recalc_game(db, s.game_id, now);
        }
        pt_log("sessao: tempo de %016lX transferido para a ROM \"%s\" (jogo %lld)", s.tid, s.rom_label, (long long)g);
        s.game_id = g;
        s.retarget = false;
    }
    // Tracking desativado (pelo usuário ou o filtro de utilitários): a partir
    // daqui nada é gravado. A sessão já gravada fecha com o tempo até agora.
    if (!s.untracked && pt_db_tracking_disabled(db, s.game_id)) {
        s.untracked = true;
        if (s.session_id != 0 && store_session_game(db, s.session_id) != 0) {
            store_update_session(db, s.session_id, s.user_id, s.counted, s.end ? s.end : now);
            pt_db_recalc_game(db, s.game_id, now);
        }
        pt_log("sessao: %016lX com tracking desativado (jogo %lld): tempo nao gravado", s.tid, (long long)s.game_id);
    }
    if (s.untracked) {
        s.session_id = 0;
        return true;
    }
    if (s.user_id == 0 || s.user_dirty) {
        bool created = false;
        int64_t uid = store_resolve_user(db, s.has_uid ? &s.uid : NULL, now, &created);
        if (uid == 0) return false;
        s.user_id = uid;
        s.user_dirty = false;
        *users_changed |= created;
    }
    if (s.session_id != 0) {
        // O app pode ter unificado o jogo; uma sessão apagada é recriada.
        int64_t g = store_session_game(db, s.session_id);
        if (g == 0) s.session_id = 0;
        else s.game_id = g;
    }
    if (s.session_id == 0) {
        s.session_id = store_insert_session(db, s.game_id, s.user_id, s.start, now);
        if (s.session_id == 0) return false;
    }
    if (store_update_session(db, s.session_id, s.user_id, s.counted, s.end) != 0) return false;
    pt_db_recalc_game(db, s.game_id, now);
    if (s.end) g_last_game = s.game_id;
    return true;
}

// Registra o status.txt quando o conteúdo relevante muda ou a escrita falha
// (não a cada checkpoint, para o log não crescer à toa).
void log_status(const PtStatus* st, int rc) {
    static int64_t last_game = -1, last_session = -1, last_user = -1, last_profile = -1;
    static int last_rc = 1;
    bool changed = st->game_id != last_game || st->session_id != last_session || st->user_id != last_user ||
                   st->profile_id != last_profile || rc != last_rc;
    if (!changed && rc == 0) return;
    last_game = st->game_id;
    last_session = st->session_id;
    last_user = st->user_id;
    last_profile = st->profile_id;
    last_rc = rc;
    pt_log("status.txt: %s (jogo %lld \"%s\", titulo %s, sessao %lld, usuario %lld, perfil %lld, base %lld s, "
           "viva %lld s, contando %d)",
           rc == 0 ? "gravado" : "FALHOU", (long long)st->game_id, st->game_name,
           st->title_id[0] ? st->title_id : "-", (long long)st->session_id, (long long)st->user_id,
           (long long)st->profile_id, (long long)st->base, (long long)st->live, st->counting);
}

void flush(int64_t now) {
    sqlite3* db = pt_db_open(kLockTimeoutMs);
    if (!db) {
        if (++g_flush_failures % 60 == 1) pt_log("banco indisponivel (%d tentativas): %s", g_flush_failures, pt_db_last_error());
        return;
    }
    g_flush_failures = 0;

    // Todas as gravações de sessão numa transação só: um sync no fim em vez
    // de um por comando. Se o COMMIT falhar, o estado em memória volta ao de
    // antes (ids recém-criados foram desfeitos junto) e tudo se repete depois.
    Sess live_before = g_live;
    Sess closed_before[kMaxClosed];
    memcpy(closed_before, g_closed, sizeof(g_closed));
    int closed_n_before = g_closed_n;
    int64_t last_game_before = g_last_game;
    bool in_tx = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK;

    // Primeiro flush do processo: nenhuma sessão aberta no banco é a viva (que
    // ainda não foi gravada), então todas ficaram do boot anterior. Fecham com
    // a duração do último checkpoint.
    int orphans = g_orphans_closed ? 0 : pt_db_close_open_sessions(db, 0, g_live.session_id);
    // Primeiro flush: utilitários (Sphaira, Ruby Achievements) gravados antes do filtro
    // ficam desativados e sem sessões.
    int utilities = g_orphans_closed ? 0 : pt_db_apply_utility_filter(db, now);

    bool users_changed = false;
    bool all_ok = true;
    int kept = 0;
    Sess done[kMaxClosed];  // fechadas gravadas: o save do console já pode ser lido
    int done_n = 0;
    for (int i = 0; i < g_closed_n; i++) {
        if (!flush_one(db, g_closed[i], now, &users_changed)) {
            all_ok = false;
            g_closed[kept++] = g_closed[i];
        } else {
            done[done_n++] = g_closed[i];
        }
    }
    g_closed_n = kept;
    bool live_ok = g_live_active && flush_one(db, g_live, now, &users_changed);
    if (g_live_active && !live_ok) all_ok = false;

    if (in_tx && sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        pt_log("banco: COMMIT falhou (%s); sessoes regravadas na proxima vez", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        g_live = live_before;
        memcpy(g_closed, closed_before, sizeof(g_closed));
        g_closed_n = closed_n_before;
        g_last_game = last_game_before;
        pt_db_close(db);
        g_need_flush = true;
        return;
    }
    if (!g_orphans_closed && orphans >= 0) {
        g_orphans_closed = true;
        if (orphans > 0) pt_log("boot: %d sessoes abertas do boot anterior encerradas no ultimo checkpoint", orphans);
        if (utilities > 0) pt_log("boot: %d homebrews utilitarios com tracking desativado (sessoes apagadas)", utilities);
    }

    // Conquistas: transação própria; falhar aqui não desfaz as sessões.
    bool live_check = live_ok && g_live.settled && now - g_last_achieve >= ACHIEVE_LIVE_SECONDS;
    // Sempre: apaga da fila o que o overlay já mostrou. Sem mudanças, o COMMIT
    // não grava nada no cartão.
    {
        bool atx = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK;
        for (int i = 0; i < done_n; i++)
            achieve_check(db, {done[i].game_id, done[i].user_id, done[i].tid, sess_rom(done[i]), false}, now);
        if (live_check) {
            achieve_check(db, {g_live.game_id, g_live.user_id, g_live.tid, sess_rom(g_live), true}, now);
            g_last_achieve = now;
        }
        achieve_export_popups(db);
        if (atx && sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) {
            achieve_commit();
        } else {
            if (atx) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            achieve_discard();
        }
    }
    achieve_export_current(db, g_live_active ? g_live.game_id : 0, g_live_active ? g_live.user_id : 0);

    PtStatus st;
    memset(&st, 0, sizeof(st));
    st.profile_id = g_profile;
    st.written_at = now;
    if (g_live_active && g_live.game_id > 0 && !g_live.untracked) {
        snprintf(st.title_id, sizeof(st.title_id), "%016lX", g_live.tid);
        st.game_id = g_live.game_id;
        st.session_id = g_live.session_id;
        st.user_id = g_live.user_id;
        st.base = store_profile_total(db, st.game_id, g_profile, g_live.session_id);
        st.live = g_live.counted;
        st.counting = (g_profile == 0 || g_profile == g_live.user_id) ? 1 : 0;
    } else {
        st.game_id = g_last_game > 0 ? g_last_game : store_last_played_game(db);
        if (st.game_id > 0) st.base = store_profile_total(db, st.game_id, g_profile, 0);
    }
    if (st.game_id > 0) store_game_name(db, st.game_id, st.game_name, sizeof(st.game_name));

    st.is_qlaunch = g_home ? 1 : 0;
    if (g_home) {
        // Jogo em destaque: o de PT_FOCUS_PATH, se houver; senão o jogo suspenso
        // ou, sem jogo aberto, o último jogado (st.game_id acima).
        st.focused_tid = g_focus;
        int64_t g = g_focus ? store_find_title(db, g_focus) : st.game_id;
        st.focused_game_id = g;
        if (g > 0) {
            store_game_name(db, g, st.focused_name, sizeof(st.focused_name));
            // Jogo suspenso em destaque: a sessão viva entra pelo valor em memória.
            bool live = g_live_active && g_live.game_id == g;
            st.focused_playtime = store_profile_total(db, g, g_profile, live ? g_live.session_id : 0);
            if (live && (g_profile == 0 || g_profile == g_live.user_id)) st.focused_playtime += g_live.counted;
        }
    }

    int64_t users_sig = pt_db_scalar(db,
        "SELECT COUNT(*) * 1000003 + IFNULL(SUM(id * 7919 + length(nickname) * 31 + IFNULL(unicode(nickname), 0)), 0) "
        "FROM users", 0, 0, -1);
    if (users_changed || g_users_dirty || users_sig != g_users_sig) {
        int rc = store_export_users(db);
        pt_log("users.txt: %s (%lld perfis)", rc == 0 ? "gravado" : "FALHOU",
               (long long)pt_db_scalar(db, "SELECT COUNT(*) FROM users", 0, 0, -1));
        if (rc == 0) {
            g_users_dirty = false;
            g_users_sig = users_sig;
        }
    }
    pt_db_close(db);

    log_status(&st, pt_status_write(&st));
    g_last_flush = now;
    g_need_flush = !all_ok;
}

// Uma fonte de ROM apontou a ROM ativa dentro do título em execução.
void on_rom_hit(const RomHit& hit, int64_t now) {
    if (g_live.rom_path[0] || g_live.learn_rom[0]) {
        if (strcmp(hit.path, sess_rom(g_live)) == 0) return;
        // Já há ROM: só uma fonte de início indica que outra ROM abriu agora.
        if (!hit.at_start) return;
        u64 tid = g_live.tid;
        close_live(now);
        start_live(tid, now, hit.path, hit.label);
        return;
    }

    if (g_live.forwarder && hit.at_start && now - g_live.start <= kForwarderRomWindow) {
        // Forwarder que abre a ROM direto: o forwarder é essa ROM (unifica pelo rom_path).
        snprintf(g_live.learn_rom, sizeof(g_live.learn_rom), "%s", hit.path);
        snprintf(g_live.rom_label, sizeof(g_live.rom_label), "%s", hit.label);
        g_need_flush = true;
        pt_log("sessao: forwarder %016lX abriu a ROM \"%s\" (forwarder dessa ROM)", g_live.tid, hit.label);
        return;
    }

    // Emulador/frontend: a ROM vira o jogo principal da sessão inteira.
    snprintf(g_live.rom_path, sizeof(g_live.rom_path), "%s", hit.path);
    snprintf(g_live.rom_label, sizeof(g_live.rom_label), "%s", hit.label);
    g_live.retarget = true;
    g_need_flush = true;
    if (!hit.at_start) {
        // O save foi gravado ao sair do jogo: a ROM terminou e o tempo seguinte
        // volta a ser do frontend até a próxima ROM.
        u64 tid = g_live.tid;
        close_live(now);
        start_live(tid, now, NULL, NULL);
        g_live.settled = true;
        roms_baseline_scan();
    }
}

void slow_checks(int64_t now) {
    PtConfig cfg;
    pt_config_load(&cfg);
    if (cfg.profile != g_profile) {
        g_profile = cfg.profile;
        g_need_flush = true;
    }
    if (!g_live_active || settling(now)) return;

    if (now - g_live.start < kUserRecheckWindow) refresh_user(g_live);

    // Jogos do sistema: só a varredura ao fechar (close_live), sem I/O no SD
    // durante a partida. Homebrew e forwarders: a cada kScanSeconds.
    bool scan = !native_game(g_live) && now - g_last_scan >= kScanSeconds;
    if (scan) g_last_scan = now;
    RomHit hit;
    if (roms_poll(&hit, scan)) on_rom_hit(hit, now);
}

}  // namespace

void tracker_init(int64_t now) {
    g_last_now = now;
    g_last_slow = 0;
    PtConfig cfg;
    pt_config_load(&cfg);
    g_profile = cfg.profile;
    g_need_flush = true;
}

void tracker_tick(int64_t now) {
    int64_t dt = now - g_last_now;
    bool gap = dt < 0 || dt > TRACKER_GAP_SECONDS;
    g_last_now = now;

    if (g_live_active && !gap) g_live.counted += dt;

    u64 tid = detect_running_title();
    if (tid && detect_is_ghost(tid)) tid = 0;  // forwarder fantasma: só abre o app
    if (gap) {
        // Sleep (ou relógio ajustado): o intervalo nunca conta. Mesmo jogo ao
        // voltar, dentro da janela: a sessão continua. Senão fecha no instante
        // do sleep.
        int64_t before = now - dt;
        bool keep = g_live_active && tid == g_live.tid && dt > 0 && dt <= kSleepResumeWindow;
        pt_log("pausa de %lld s (sleep ou ajuste do relogio): %s", (long long)dt,
               !g_live_active ? "sem jogo aberto"
               : keep         ? "mesma sessao continua"
                              : "sessao encerrada no inicio da pausa");
        if (g_live_active && !keep) close_live(before);
        g_need_flush = true;  // status.txt com o horário novo
    }
    if (g_live_active && tid != g_live.tid) close_live(now);
    if (!g_live_active && tid != 0) start_live(tid, now, NULL, NULL);
    if (g_live_active && !g_live.settled && !settling(now)) settle_live();

    // HUD: só na Home. Mudou a Home ou o jogo sob o cursor => status.txt novo.
    bool home = detect_home_in_front(tid);
    u64 focus = home ? detect_focused_title() : 0;
    if (home != g_home || focus != g_focus) {
        if (home != g_home) pt_log("home %s", home ? "em primeiro plano" : "fora de foco (jogo na tela)");
        g_home = home;
        g_focus = focus;
        g_need_flush = true;
    }

    if (gap || now - g_last_slow >= kSlowCheckSeconds) {
        g_last_slow = now;
        slow_checks(now);
    }
    // Pacote de conquistas trocado no app: grava e reavalia já (o overlay vê a lista nova).
    if (achieve_packs_changed()) {
        g_need_flush = true;
        g_last_achieve = 0;
    }

    bool due = g_need_flush || (g_live_active && now - g_last_flush >= TRACKER_CHECKPOINT_SECONDS) ||
               (g_live_active && g_live.settled && now - g_last_achieve >= ACHIEVE_LIVE_SECONDS);
    if (due && !settling(now)) flush(now);
}
