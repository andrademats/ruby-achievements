#include "data.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "forwarder/owo.hpp"
#include "pt_db.h"
#include "pt_log.h"
#include "pt_pack.h"
#include "pt_paths.h"
#include "pt_status.h"
#include "pt_title.h"
#include "pt_util.h"

namespace data {
namespace {

constexpr int kLockTimeoutMs = 3000;
std::string g_error;

// Conexão curta: abre no construtor, fecha (e libera o lock) no destrutor.
struct Db {
    sqlite3* db = pt_db_open(kLockTimeoutMs);
    Db() {
        if (!db) g_error = std::string(tr("Banco ocupado ou indisponível. Tente de novo.")) + "\n" + pt_db_last_error();
    }
    ~Db() {
        if (db) pt_db_close(db);
    }
    explicit operator bool() const { return db != nullptr; }
};

struct Stmt {
    sqlite3_stmt* st = nullptr;
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
            g_error = sqlite3_errmsg(db);
            st = nullptr;
        }
    }
    ~Stmt() {
        if (st) sqlite3_finalize(st);
    }
    explicit operator bool() const { return st != nullptr; }
    Stmt& i64(int idx, int64_t v) {
        sqlite3_bind_int64(st, idx, v);
        return *this;
    }
    Stmt& text(int idx, const std::string& v) {
        sqlite3_bind_text(st, idx, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& null(int idx) {
        sqlite3_bind_null(st, idx);
        return *this;
    }
    bool row() { return sqlite3_step(st) == SQLITE_ROW; }
    bool done() { return sqlite3_step(st) == SQLITE_DONE; }
    int64_t col_i64(int c) { return sqlite3_column_int64(st, c); }
    std::string col_str(int c) {
        const unsigned char* t = sqlite3_column_text(st, c);
        return t ? reinterpret_cast<const char*>(t) : "";
    }
};

int64_t live_session_id() {
    PtStatus st;
    return pt_status_read(&st) == 0 ? st.session_id : 0;
}

bool refuse_live(int64_t session_id) {
    if (session_id != 0 && session_id == live_session_id()) {
        g_error = tr("Sessão em andamento: aguarde o jogo fechar.");
        return true;
    }
    return false;
}

bool exec_ok(Stmt& s, sqlite3* db) {
    if (!s) return false;
    if (!s.done()) {
        g_error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

// Transação curta: desfeita no destrutor se commit() não foi chamado.
struct Tx {
    sqlite3* db;
    bool open;
    explicit Tx(sqlite3* d) : db(d), open(sqlite3_exec(d, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK) {
        if (!open) g_error = sqlite3_errmsg(d);
    }
    ~Tx() {
        if (open) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    bool commit() {
        open = false;
        if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) return true;
        g_error = sqlite3_errmsg(db);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }
};

// Passa para `user` os desbloqueios de conquistas locais feitos durante as
// sessões escolhidas por `where` (com ?1 e ?2). Um desbloqueio é da sessão do
// mesmo jogo e perfil que começou por último antes dele: as conquistas de save
// são gravadas logo depois de o jogo fechar. As do RetroAchievements espelham a
// conta do site e não mudam. Chamar antes de mudar sessions.user_id.
bool move_session_unlocks(sqlite3* db, const char* where, int64_t a, int64_t b, int64_t user) {
    struct Unlock {
        std::string id;
        int64_t user, at;
    };
    std::vector<Unlock> list;
    {
        std::string sql =
            "SELECT ua.achievement_id, ua.user_id, ua.unlocked_at FROM sessions s "
            "JOIN achievements a ON a.game_id = s.game_id AND a.source = 'LOCAL' "
            "JOIN user_achievements ua ON ua.achievement_id = a.id AND ua.user_id = s.user_id "
            " AND ua.unlocked_at >= s.start_time "
            "WHERE " + std::string(where) + " AND ua.user_id <> ?3 AND NOT EXISTS (SELECT 1 FROM sessions o "
            " WHERE o.game_id = s.game_id AND o.user_id = s.user_id AND o.start_time > s.start_time "
            " AND o.start_time <= ua.unlocked_at)";
        Stmt s(db, sql.c_str());
        if (!s) return false;
        s.i64(1, a).i64(2, b).i64(3, user);
        while (s.row()) list.push_back({s.col_str(0), s.col_i64(1), s.col_i64(2)});
    }
    for (const Unlock& u : list) {
        // Se o perfil novo já tinha a conquista, fica a data mais antiga.
        Stmt ins(db,
                 "INSERT INTO user_achievements(achievement_id, user_id, unlocked_at) VALUES(?, ?, ?) "
                 "ON CONFLICT(achievement_id, user_id) DO UPDATE SET unlocked_at = MIN(unlocked_at, excluded.unlocked_at)");
        if (!ins) return false;
        ins.text(1, u.id).i64(2, user).i64(3, u.at);
        if (!exec_ok(ins, db)) return false;
        Stmt del(db, "DELETE FROM user_achievements WHERE achievement_id = ? AND user_id = ?");
        if (!del) return false;
        del.text(1, u.id).i64(2, u.user);
        if (!exec_ok(del, db)) return false;
    }
    return true;
}

}  // namespace

const std::string& last_error() { return g_error; }

bool users(std::vector<UserRow>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    Stmt s(d.db, "SELECT id, nickname, is_shared, COALESCE(account_uid,'') FROM users "
                 "ORDER BY is_shared, nickname COLLATE NOCASE");
    if (!s) return false;
    while (s.row()) out.push_back({s.col_i64(0), s.col_str(1), s.col_i64(2) != 0, s.col_str(3)});
    return true;
}

static GameRow read_game(Stmt& s) {
    GameRow g;
    g.id = s.col_i64(0);
    g.title_id = s.col_str(1);
    g.name = s.col_str(2);
    g.rom_path = s.col_str(3);
    g.source = s.col_str(4);
    g.cover = s.col_str(5);
    g.total = s.col_i64(6);
    g.session_count = s.col_i64(7);
    g.last_played = s.col_i64(8);
    return g;
}

static const char* kGameSelect =
    "SELECT g.id, g.title_id, g.display_name, g.rom_path, g.source_type, g.cover_path,"
    " COALESCE(SUM(s.duration),0), COUNT(s.id),"
    " COALESCE(MAX(COALESCE(s.end_time, s.start_time + s.duration)), g.last_played, 0) AS lp "
    "FROM games g LEFT JOIN sessions s ON s.game_id = g.id AND (?1 = 0 OR s.user_id = ?1) ";

bool games(int64_t user_filter, std::vector<GameRow>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    std::string sql = std::string(kGameSelect) +
                      "WHERE g.status NOT LIKE 'MERGED:%' "
                      "AND g.id NOT IN (SELECT game_id FROM tracking_disabled WHERE disabled = 1) GROUP BY g.id "
                      "HAVING ?1 = 0 OR COUNT(s.id) > 0 "
                      "ORDER BY lp DESC, g.display_name COLLATE NOCASE";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.i64(1, user_filter);
    while (s.row()) out.push_back(read_game(s));
    return true;
}

bool game(int64_t id, int64_t user_filter, GameRow& out) {
    Db d;
    if (!d) return false;
    std::string sql = std::string(kGameSelect) + "WHERE g.id = ?2 GROUP BY g.id";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.i64(1, user_filter).i64(2, id);
    if (!s.row()) {
        g_error = tr("Jogo não encontrado.");
        return false;
    }
    out = read_game(s);
    return true;
}

bool profile_total(int64_t user_filter, int64_t& seconds) {
    seconds = 0;
    Db d;
    if (!d) return false;
    Stmt s(d.db, "SELECT COALESCE(SUM(duration), 0) FROM sessions WHERE ?1 = 0 OR user_id = ?1");
    if (!s) return false;
    s.i64(1, user_filter);
    if (s.row()) seconds = s.col_i64(0);
    return true;
}

bool game_user_totals(int64_t game_id, std::vector<std::pair<std::string, int64_t>>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    Stmt s(d.db,
           "SELECT u.nickname, SUM(s.duration) FROM sessions s JOIN users u ON u.id = s.user_id "
           "WHERE s.game_id = ? GROUP BY u.id ORDER BY 2 DESC");
    if (!s) return false;
    s.i64(1, game_id);
    while (s.row()) out.emplace_back(s.col_str(0), s.col_i64(1));
    return true;
}

// Sessões de um jogo (?1 > 0) ou as fantasmas (?1 = 0: abertas, menos a viva ?3),
// filtradas pelo perfil ?2 (0 = todos).
static const char* kSessionFrom =
    "FROM sessions s JOIN games g ON g.id = s.game_id JOIN users u ON u.id = s.user_id "
    "WHERE ((?1 = 0 AND s.end_time IS NULL AND s.id <> ?3) OR (?1 <> 0 AND s.game_id = ?1)) "
    "AND (?2 = 0 OR s.user_id = ?2) ";

bool session_summary(int64_t game_id, int64_t user_filter, SessionSummary& out) {
    out = {};
    int64_t live = live_session_id();
    Db d;
    if (!d) return false;
    Stmt s(d.db, (std::string("SELECT COUNT(*), COALESCE(SUM(s.duration), 0) ") + kSessionFrom).c_str());
    if (!s) return false;
    s.i64(1, game_id).i64(2, user_filter).i64(3, live);
    if (s.row()) out = {s.col_i64(0), s.col_i64(1)};
    return true;
}

bool sessions(int64_t game_id, int64_t user_filter, const SessionRow* after, int limit,
              std::vector<SessionRow>& out) {
    out.clear();
    int64_t live = live_session_id();
    Db d;
    if (!d) return false;
    std::string sql = std::string(
                          "SELECT s.id, s.game_id, s.user_id, g.display_name, u.nickname, s.start_time,"
                          " COALESCE(s.end_time, 0), s.duration, COALESCE(s.sync_id, '') LIKE 'nxal:prev:%' ") +
                      kSessionFrom +
                      "AND (?4 IS NULL OR (s.start_time, s.id) < (?4, ?5)) "
                      "ORDER BY s.start_time DESC, s.id DESC LIMIT ?6";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.i64(1, game_id).i64(2, user_filter).i64(3, live).i64(6, limit);
    if (after) s.i64(4, after->start).i64(5, after->id);
    else s.null(4).null(5);
    while (s.row()) {
        SessionRow r;
        r.id = s.col_i64(0);
        r.game_id = s.col_i64(1);
        r.user_id = s.col_i64(2);
        r.game_name = s.col_str(3);
        r.user_name = s.col_str(4);
        r.start = s.col_i64(5);
        r.end = s.col_i64(6);
        r.duration = s.col_i64(7);
        r.residual = s.col_i64(8) != 0;
        r.live = r.id == live;
        r.ghost = r.end == 0 && !r.live;
        out.push_back(r);
    }
    return true;
}

bool rename_game(int64_t id, const std::string& name) {
    std::string n = name;
    n.erase(0, n.find_first_not_of(" \t"));
    n.erase(n.find_last_not_of(" \t") + 1);
    if (n.empty()) {
        g_error = tr("O nome não pode ficar vazio.");
        return false;
    }
    Db d;
    if (!d) return false;
    Stmt s(d.db, "UPDATE games SET display_name = ?, updated_at = ? WHERE id = ?");
    if (!s) return false;
    s.text(1, n).i64(2, pt_now()).i64(3, id);
    return exec_ok(s, d.db);
}

bool set_cover(int64_t id, const std::string& path) {
    Db d;
    if (!d) return false;
    Stmt s(d.db, "UPDATE games SET cover_path = ?, updated_at = ? WHERE id = ?");
    if (!s) return false;
    if (path.empty()) s.null(1);
    else s.text(1, path);
    s.i64(2, pt_now()).i64(3, id);
    return exec_ok(s, d.db);
}

bool merge_games(int64_t from, int64_t into) {
    Db d;
    if (!d) return false;
    if (pt_db_merge_games(d.db, from, into, pt_now()) != 0) {
        g_error = tr("Não foi possível unificar os jogos.");
        return false;
    }
    return true;
}

bool reassign_game_user(int64_t game_id, int64_t user_id) {
    int64_t live = live_session_id();
    Db d;
    if (!d) return false;
    Tx tx(d.db);
    if (!tx.open || !move_session_unlocks(d.db, "s.game_id = ?1 AND s.id <> ?2", game_id, live, user_id)) return false;
    Stmt s(d.db, "UPDATE sessions SET user_id = ? WHERE game_id = ? AND id <> ?");
    if (!s) return false;
    s.i64(1, user_id).i64(2, game_id).i64(3, live);
    return exec_ok(s, d.db) && tx.commit();
}

bool reassign_session_user(int64_t session_id, int64_t user_id) {
    if (refuse_live(session_id)) return false;
    Db d;
    if (!d) return false;
    Tx tx(d.db);
    if (!tx.open || !move_session_unlocks(d.db, "s.id = ?1", session_id, 0, user_id)) return false;
    Stmt s(d.db, "UPDATE sessions SET user_id = ? WHERE id = ?");
    if (!s) return false;
    s.i64(1, user_id).i64(2, session_id);
    return exec_ok(s, d.db) && tx.commit();
}

bool move_session(int64_t session_id, int64_t game_id) {
    if (refuse_live(session_id)) return false;
    Db d;
    if (!d) return false;
    int64_t now = pt_now();
    int64_t old_game = pt_db_scalar(d.db, "SELECT game_id FROM sessions WHERE id = ?", session_id, 0, 0);
    game_id = pt_db_resolve_game(d.db, game_id);
    Stmt s(d.db, "UPDATE sessions SET game_id = ? WHERE id = ?");
    if (!s) return false;
    s.i64(1, game_id).i64(2, session_id);
    if (!exec_ok(s, d.db)) return false;
    if (old_game > 0) pt_db_recalc_game(d.db, old_game, now);
    pt_db_recalc_game(d.db, game_id, now);
    return true;
}

bool delete_session(int64_t session_id) {
    if (refuse_live(session_id)) return false;
    Db d;
    if (!d) return false;
    Tx tx(d.db);
    if (!tx.open) return false;
    int64_t game = pt_db_scalar(d.db, "SELECT game_id FROM sessions WHERE id = ?", session_id, 0, 0);
    // Importada do NX Activity Log: a próxima importação não a recria.
    Stmt mark(d.db,
              "INSERT OR REPLACE INTO import_deleted(sync_id, duration) "
              "SELECT sync_id, duration FROM sessions WHERE id = ? AND sync_id LIKE 'nxal:%'");
    if (!mark) return false;
    mark.i64(1, session_id);
    if (!exec_ok(mark, d.db)) return false;
    Stmt s(d.db, "DELETE FROM sessions WHERE id = ?");
    if (!s) return false;
    s.i64(1, session_id);
    if (!exec_ok(s, d.db)) return false;
    if (game > 0) pt_db_recalc_game(d.db, game, pt_now());
    return tx.commit();
}

bool finalize_ghost(int64_t session_id) {
    if (refuse_live(session_id)) return false;
    Db d;
    if (!d) return false;
    // A duração gravada no último checkpoint é mantida; o fim é derivado dela.
    if (pt_db_close_open_sessions(d.db, session_id, 0) < 0) {
        g_error = sqlite3_errmsg(d.db);
        return false;
    }
    return true;
}

// --- Importação do tempo registrado pelo sistema (pdm, a fonte do NX Activity Log) ---
//
// Sessões: montadas a partir do log de eventos do pdm (abrir/sair/foco do app e
// abrir/fechar conta), como o NX Activity Log faz no histórico. A duração de cada
// sessão é o tempo em foco, medido pelo relógio contínuo (steady) quando ele é
// coerente com o relógio do usuário.
// Totais: o pdm também guarda o total oficial por jogo e perfil (o "All Activity"
// do NX Activity Log), e ele manda: tempo do Ruby Achievements + sessões importadas fecham
// exatamente nesse total. Importadas que somam mais são reduzidas na proporção; o
// que faltar (eventos que o pdm não devolve ou mais antigos que o log dele) vira
// uma sessão residual "tempo anterior / não detalhado".
// Duplicação: sessões que se sobrepõem a uma sessão do Ruby Achievements no mesmo jogo
// ficam de fora (esse tempo já foi registrado).
// Reimportação: cada sessão importada tem a chave estável
// "nxal:<TID>:<conta>:<início>". Uma chave que já existe (em qualquer jogo ou
// perfil, se o usuário a moveu ou reassociou) ou que está em import_deleted
// (o usuário a excluiu) não é gravada de novo, e o tempo dela conta como já
// registrado. Só eventos novos entram. A residual ("nxal:prev:<TID>:<conta>")
// é recalculada enquanto continuar no jogo e perfil de origem; editada ou
// excluída pelo usuário, fica como está.
namespace {

constexpr const char* kImportPrefix = "nxal:";
constexpr const char* kResidualPrefix = "nxal:prev:";  // sessão residual (um por jogo e perfil)

// Importações antigas usavam "nxal:<TID>:<conta>:<início>:<índice>"; o índice
// mudava quando o histórico do pdm mudava. Passa para a chave sem o índice
// (duas antigas com o mesmo início: a segunda fica com a chave velha).
bool migrate_import_keys(sqlite3* db) {
    std::vector<std::pair<int64_t, std::string>> rows;
    {
        Stmt s(db, "SELECT id, sync_id FROM sessions WHERE sync_id LIKE 'nxal:%' AND sync_id NOT LIKE 'nxal:prev:%'");
        if (!s) return false;
        while (s.row()) rows.emplace_back(s.col_i64(0), s.col_str(1));
    }
    for (auto& [id, key] : rows) {
        if (std::count(key.begin(), key.end(), ':') != 4) continue;
        Stmt up(db, "UPDATE OR IGNORE sessions SET sync_id = ? WHERE id = ?");
        if (!up) return false;
        up.text(1, key.substr(0, key.rfind(':'))).i64(2, id);
        if (!exec_ok(up, db)) return false;
    }
    return true;
}

// Chaves de importação já conhecidas de um título e conta (prefixo
// "nxal:<TID>:<conta>:"): sessões no banco + excluídas pelo usuário.
bool known_import_keys(sqlite3* db, const std::string& prefix, std::vector<std::string>& keys, int64_t& seconds) {
    keys.clear();
    seconds = 0;
    Stmt s(db,
           "SELECT sync_id, duration FROM sessions WHERE substr(sync_id, 1, length(?1)) = ?1 "
           "UNION ALL SELECT sync_id, duration FROM import_deleted WHERE substr(sync_id, 1, length(?1)) = ?1");
    if (!s) return false;
    s.text(1, prefix);
    while (s.row()) {
        keys.push_back(s.col_str(0));
        seconds += s.col_i64(1);
    }
    return true;
}

bool read_title(uint64_t tid, char* name, size_t name_cap, char* icon, size_t icon_cap) {
    u64 size = 0;
    return R_SUCCEEDED(pt_title_read(tid, name, name_cap, icon, icon_cap, &size)) && name[0];
}

// Serviços usados só pela importação (o Borealis não inicializa nenhum deles).
struct ImportServices {
    Result acc, ns, set, pdm;
    ImportServices()
        : acc(accountInitialize(AccountServiceType_Application)),
          ns(nsInitialize()),
          set(setInitialize()),
          pdm(pdmqryInitialize()) {}
    ~ImportServices() {
        if (R_SUCCEEDED(pdm)) pdmqryExit();
        if (R_SUCCEEDED(set)) setExit();
        if (R_SUCCEEDED(ns)) nsExit();
        if (R_SUCCEEDED(acc)) accountExit();
    }
};

// Títulos com o conteúdo base presente (NAND, SD ou cartucho inserido). Um jogo
// arquivado ou apagado continua com o registro na Home (nsListApplicationRecord
// e o NACP em cache), mas sem o conteúdo Application. Precisa do ns iniciado.
std::vector<uint64_t> installed_titles() {
    std::vector<uint64_t> out;
    NsApplicationRecord recs[32];
    s32 offset = 0, got = 0;
    while (R_SUCCEEDED(nsListApplicationRecord(recs, 32, offset, &got)) && got > 0) {
        for (s32 i = 0; i < got; i++) {
            u64 tid = recs[i].application_id;
            NsApplicationContentMetaStatus meta[16];
            s32 count = 0;
            bool base = false;
            if (R_FAILED(nsListApplicationContentMetaStatus(tid, 0, meta, 16, &count))) base = true;  // na dúvida
            for (s32 k = 0; k < count && !base; k++) base = meta[k].meta_type == NcmContentMetaType_Application;
            if (base) out.push_back(tid);
        }
        offset += got;
    }
    return out;
}

// Nos eventos crus do pdm, cada u64 vem com as metades de 32 bits trocadas.
uint64_t swapped(const u32 w[2]) { return ((uint64_t)w[0] << 32) | w[1]; }

struct RawSession {
    uint64_t tid;
    int user;          // índice da conta; -1 = nenhuma conta aberta na sessão
    int64_t start, end;
    int64_t focused;   // segundos em foco
};

struct ImportStats {
    int crashed = 0;     // sessões sem Exit (travou/desligou)
    int multi_user = 0;  // sessões com mais de uma conta aberta
};

// Nome que o NACP comprimido do firmware 21 deixou ilegível em importações antigas.
bool broken_name(const std::string& name) {
    int bad = 0;
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7F) bad++;
    std::string tmp = name;  // UTF-8 inválido também conta
    for (size_t i = 0; i < tmp.size();) {
        unsigned char c = tmp[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > tmp.size()) return true;
        for (size_t k = 1; k < n; k++)
            if (((unsigned char)tmp[i + k] >> 6) != 2) return true;
        i += n;
    }
    return bad > 0;
}

void import_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void import_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_log_vfile(PT_DIR "/import.log", fmt, ap);
    va_end(ap);
}

// Lê o log de eventos do pdm e monta as sessões de aplicativos com as mesmas
// regras do NX Activity Log (Application/source/nx/PlayData.cpp):
//  - só eventos de aplicativo com PlayLogPolicy_All (o total oficial, consultado
//    com flag=false, também só conta esses);
//  - uma sessão vai do Launch até o Exit ou até o próximo Launch de QUALQUER
//    título (o Switch roda um aplicativo por vez; sem Exit = travou/desligou);
//  - o perfil da sessão vem dos eventos de conta (abrir/fechar, tipo 0/1) DENTRO
//    dela, nunca de uma conta aberta antes por outro jogo;
//  - o tempo começa no primeiro InFocus e soma cada trecho InFocus -> OutFocus pelo
//    relógio steady (OutFocus repetidos contam uma vez); um trecho sem OutFocus
//    (travamento) não conta.
struct EventSession {
    uint64_t tid = 0;
    int64_t start = 0, end = 0;
    int64_t focused = 0;
    int user = -1;        // primeira conta aberta na sessão
    int other_users = 0;  // outras contas abertas na mesma sessão (só log)
    bool crashed = false; // terminou sem Exit
    bool in_focus = false;
    int64_t focus_steady = 0, focus_clock = 0;
};

int64_t focus_span(int64_t clock0, int64_t steady0, int64_t clock1, int64_t steady1) {
    int64_t ds = steady1 - steady0, dc = clock1 - clock0;
    if (ds >= 0 && (dc < 0 || ds <= dc + 60)) return ds;  // steady é a referência do NXAL
    return dc > 0 ? dc : 0;                                // steady incoerente (reset)
}

std::vector<RawSession> read_play_events(const AccountUid* uids, int user_count, int* events_read,
                                         ImportStats* stats) {
    std::vector<RawSession> out;
    *events_read = 0;
    s32 total = 0, first = 0, last = 0;
    if (R_FAILED(pdmqryGetAvailablePlayEventRange(&total, &first, &last)) || total <= 0) return out;

    bool open = false;
    EventSession cur;
    auto finish = [&](int64_t end_clock, bool crashed) {
        if (!open) return;
        open = false;
        cur.end = end_clock > cur.start ? end_clock : cur.start;
        cur.crashed = crashed;
        if (crashed) stats->crashed++;
        if (cur.other_users) stats->multi_user++;
        if (cur.focused <= 0) return;
        out.push_back({cur.tid, cur.user, cur.start, cur.end, cur.focused});
    };

    constexpr s32 kBatch = 128;
    std::vector<PdmPlayEvent> buf(kBatch);
    for (s32 idx = first; idx <= last;) {
        s32 got = 0;
        if (R_FAILED(pdmqryQueryPlayEvent(idx, buf.data(), kBatch, &got)) || got <= 0) break;
        for (s32 k = 0; k < got; k++) {
            const PdmPlayEvent& e = buf[k];
            int64_t clock = (int64_t)e.timestamp_user, steady = (int64_t)e.timestamp_steady;

            if (e.play_event_type == PdmPlayEventType_Account) {
                u8 type = e.event_data.account.type;
                if (!open || (type != 0 && type != 1)) continue;  // tipo 2 = vínculo com app (ignorado pelo NXAL)
                AccountUid uid;
                uid.uid[0] = ((uint64_t)e.event_data.account.uid[0] << 32) | e.event_data.account.uid[1];
                uid.uid[1] = ((uint64_t)e.event_data.account.uid[2] << 32) | e.event_data.account.uid[3];
                int u = -1;
                for (int j = 0; j < user_count; j++)
                    if (uids[j].uid[0] == uid.uid[0] && uids[j].uid[1] == uid.uid[1]) u = j;
                if (u < 0) continue;
                if (cur.user < 0) cur.user = u;
                else if (cur.user != u) cur.other_users++;
                continue;
            }
            if (e.play_event_type != PdmPlayEventType_Applet || e.event_data.applet.applet_id != AppletId_application)
                continue;
            if (e.event_data.applet.log_policy != PdmPlayLogPolicy_All) continue;

            uint64_t tid = swapped(e.event_data.applet.program_id);
            switch (e.event_data.applet.event_type) {
                case PdmAppletEventType_Launch:
                    finish(open ? cur.end : clock, true);  // Launch sem Exit antes: travou
                    cur = EventSession{};
                    cur.tid = tid;
                    cur.start = cur.end = clock;
                    open = true;
                    break;
                case PdmAppletEventType_InFocus:
                    if (open && tid == cur.tid && !cur.in_focus) {
                        cur.in_focus = true;
                        cur.focus_clock = clock;
                        cur.focus_steady = steady;
                    }
                    if (open && tid == cur.tid) cur.end = clock;
                    break;
                case PdmAppletEventType_OutOfFocus:
                case PdmAppletEventType_OutOfFocus4:
                    if (open && tid == cur.tid) {
                        if (cur.in_focus) cur.focused += focus_span(cur.focus_clock, cur.focus_steady, clock, steady);
                        cur.in_focus = false;  // OutFocus repetidos: só o primeiro fecha o trecho
                        cur.end = clock;
                    }
                    break;
                case PdmAppletEventType_Exit:
                case PdmAppletEventType_Exit5:
                case PdmAppletEventType_Exit6:
                    if (open && tid == cur.tid) finish(clock, false);
                    break;
                default:
                    break;
            }
        }
        *events_read += got;
        idx += got;
    }
    // Sessão ainda aberta no fim do log: jogo em execução agora; o Ruby Achievements a registra.
    return out;
}

struct Sess {
    int64_t start, end, duration;
};

}  // namespace

bool import_system_playtime(ImportReport& r) {
    r = {};
    g_error.clear();
    ImportServices svc;
    char buf[128];
    if (R_FAILED(svc.acc) || R_FAILED(svc.ns) || R_FAILED(svc.pdm)) {
        snprintf(buf, sizeof(buf), tr("Serviços do sistema indisponíveis (acc 0x%X, ns 0x%X, pdm 0x%X)."), svc.acc,
                 svc.ns, svc.pdm);
        g_error = buf;
        return false;
    }

    AccountUid uids[ACC_USER_LIST_SIZE];
    s32 user_count = 0;
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &user_count)) || user_count <= 0) {
        g_error = tr("Nenhuma conta do Switch encontrada.");
        return false;
    }

    int events = 0;
    ImportStats ist;
    std::vector<RawSession> raw = read_play_events(uids, user_count, &events, &ist);
    import_log("---- importacao: %d eventos do pdm, %zu sessoes de aplicativos, %d contas; %d sem Exit, "
               "%d com mais de uma conta ----",
               events, raw.size(), (int)user_count, ist.crashed, ist.multi_user);
    // Sem conta aberta: com uma conta só no console, é dela.
    for (RawSession& s : raw)
        if (s.user < 0 && user_count == 1) s.user = 0;

    // Títulos: instalados + os que aparecem nos eventos (inclusive desinstalados).
    // Forwarders ficam de fora: o tempo deles vai para as ROMs.
    std::vector<uint64_t> tids;
    auto add_tid = [&](uint64_t t) {
        if ((t >> 56) == 0x05 || std::find(tids.begin(), tids.end(), t) != tids.end()) return;
        tids.push_back(t);
    };
    std::vector<uint64_t> installed = installed_titles();
    for (uint64_t t : installed) add_tid(t);
    for (const RawSession& s : raw) add_tid(s.tid);
    r.titles = (int)tids.size();
    for (uint64_t t : tids)
        if (std::find(installed.begin(), installed.end(), t) == installed.end()) {
            r.uninstalled++;
            import_log("%016lX: nao instalado (vai para Desinstalados)", t);
        }

    Db d;
    if (!d) return false;
    int64_t now = pt_now();
    int64_t games_before = pt_db_scalar(d.db, "SELECT COUNT(*) FROM games", 0, 0, 0);
    sqlite3_exec(d.db, "BEGIN", nullptr, nullptr, nullptr);
    bool ok = true;

    // Jogos com sessões gravadas ou alteradas: recalculados no fim.
    std::vector<int64_t> touched;
    if (!migrate_import_keys(d.db)) ok = false;
    // Utilitários (Sphaira, Ruby Achievements): desativados e sem sessões, importadas ou não.
    int utilities = pt_db_apply_utility_filter(d.db, now);
    if (utilities < 0) ok = false;
    else if (utilities > 0) import_log("%d homebrews utilitarios com tracking desativado (sessoes apagadas)", utilities);

    std::vector<int64_t> user_ids(user_count, 0);
    std::vector<std::string> uid_strs(user_count);
    std::vector<bool> user_seen(user_count, false);

    for (uint64_t tid : tids) {
        if (!ok) break;
        for (int u = 0; u < user_count && ok; u++) {
            PdmPlayStatistics st{};
            int64_t official = 0;
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(tid, uids[u], false, &st)))
                official = (int64_t)(st.playtime / 1000000000ULL);

            std::vector<Sess> list;
            int64_t raw_sum = 0;
            for (const RawSession& s : raw)
                if (s.tid == tid && s.user == u) {
                    list.push_back({s.start, s.end, s.focused});
                    raw_sum += s.focused;
                }
            if (official <= 0 && list.empty()) continue;

            if (user_ids[u] == 0) {
                char uid_str[33], nick[64];
                pt_uid_to_string(uids[u], uid_str);
                if (!pt_account_nickname(uids[u], nick, sizeof(nick))) snprintf(nick, sizeof(nick), "Usuário %.8s", uid_str);
                bool changed = false;
                user_ids[u] = pt_db_account_user(d.db, uid_str, nick, now, &changed);
                uid_strs[u] = uid_str;
                if (user_ids[u] == 0) {
                    ok = false;
                    break;
                }
            }
            if (!user_seen[u]) {
                user_seen[u] = true;
                r.users++;
            }
            int64_t game = pt_db_title_game(d.db, tid, now, read_title);
            if (game == 0) {
                ok = false;
                break;
            }
            if (pt_db_tracking_disabled(d.db, pt_db_resolve_game(d.db, game))) {
                import_log("%016lX: tracking desativado, nada importado", tid);
                break;  // vale para todos os perfis deste título
            }
            touched.push_back(game);
            {
                Stmt q(d.db, "SELECT display_name FROM games WHERE id=?");
                q.i64(1, game);
                if (q && q.row() && broken_name(q.col_str(0))) {
                    char name[256], icon[128];
                    if (read_title(tid, name, sizeof(name), icon, sizeof(icon))) {
                        Stmt up(d.db, "UPDATE games SET display_name=? WHERE id=?");
                        up.text(1, name).i64(2, game);
                        if (exec_ok(up, d.db)) import_log("%016lX: nome corrompido corrigido para \"%s\"", tid, name);
                    }
                }
            }

            // Tempo que o Ruby Achievements já registrou neste jogo e perfil.
            int64_t existing = 0;
            {
                Stmt q(d.db,
                       "SELECT COALESCE(SUM(duration),0) FROM sessions WHERE game_id=? AND user_id=? "
                       "AND (sync_id IS NULL OR sync_id NOT LIKE 'nxal:%')");
                if (!q) {
                    ok = false;
                    break;
                }
                q.i64(1, game).i64(2, user_ids[u]);
                if (q.row()) existing = q.col_i64(0);
            }

            // Importadas antes (onde estiverem agora) e excluídas pelo usuário:
            // não voltam, e o tempo delas já conta para o total.
            char prefix[64];
            snprintf(prefix, sizeof(prefix), "%s%016lX:%s:", kImportPrefix, tid, uid_strs[u].c_str());
            std::vector<std::string> known;
            int64_t known_secs = 0;
            if (!known_import_keys(d.db, prefix, known, known_secs)) {
                ok = false;
                break;
            }
            auto key_of = [&](int64_t start) { return std::string(prefix) + std::to_string((long long)start); };

            // Sessões detalhadas novas, menos as que coincidem com uma sessão do Ruby Achievements
            // (mesmo jogo, horário sobreposto: esse tempo já está registrado).
            std::vector<Sess> kept;
            int64_t kept_sum = 0;
            int overlap = 0, already = 0;
            for (const Sess& s : list) {
                if (s.duration <= 0) continue;
                // Já importada, excluída pelo usuário ou repetida (mesmo início) nesta importação.
                std::string key = key_of(s.start);
                if (std::find(known.begin(), known.end(), key) != known.end()) {
                    already++;
                    continue;
                }
                known.push_back(key);
                int64_t end = s.end > s.start ? s.end : s.start + s.duration;
                Stmt q(d.db,
                       "SELECT COUNT(*) FROM sessions WHERE game_id=? AND (sync_id IS NULL OR sync_id NOT LIKE 'nxal:%') "
                       "AND start_time < ? AND COALESCE(end_time, start_time + duration) > ?");
                if (!q) {
                    ok = false;
                    break;
                }
                q.i64(1, game).i64(2, end).i64(3, s.start);
                if (q.row() && q.col_i64(0) > 0) {
                    overlap++;
                    continue;
                }
                kept.push_back({s.start, end, s.duration});
                kept_sum += s.duration;
            }
            if (!ok) break;

            // Fecha no total oficial: Ruby Achievements + importadas = total do sistema.
            // Sobra: reduz as importadas na proporção. Falta (eventos que o pdm não
            // devolveu ou mais antigos que o log): uma sessão residual.
            int64_t residual = 0;
            int64_t not_counted = 0;
            if (official <= 0) {
                // O sistema não conta tempo deste jogo para este perfil (o NXAL mostra 0
                // em "All Activity"): nada é importado, para os totais baterem.
                for (const Sess& k : kept) not_counted += k.duration;
                kept.clear();
                kept_sum = 0;
            } else {
                int64_t accounted = existing + known_secs;
                int64_t target = official > accounted ? official - accounted : 0;
                if (kept_sum > target) {
                    double k = (double)target / (double)kept_sum;
                    int64_t acc = 0;
                    for (size_t i = 0; i < kept.size(); i++) {
                        kept[i].duration = i + 1 == kept.size() ? target - acc : (int64_t)(kept[i].duration * k);
                        kept[i].end = kept[i].start + kept[i].duration;
                        acc += kept[i].duration;
                    }
                } else {
                    residual = target - kept_sum;
                }
            }

            int written = 0;
            int64_t written_secs = 0;
            auto insert = [&](const Sess& s, const char* key) {
                Stmt ins(d.db,
                         "INSERT INTO sessions(game_id,user_id,start_time,end_time,duration,created_at,sync_id,synced) "
                         "VALUES(?,?,?,?,?,?,?,0)");
                if (!ins) return false;
                ins.i64(1, game).i64(2, user_ids[u]).i64(3, s.start).i64(4, s.end)
                    .i64(5, s.duration).i64(6, now).text(7, key);
                if (!exec_ok(ins, d.db)) return false;
                written++;
                written_secs += s.duration;
                return true;
            };
            for (size_t i = 0; i < kept.size() && ok; i++) {
                if (kept[i].duration <= 0) continue;
                ok = insert(kept[i], key_of(kept[i].start).c_str());
            }
            if (ok) {
                // Residual: no primeiro dia jogado segundo o sistema.
                int64_t start = (int64_t)st.first_timestamp_user;
                if (start <= 0) start = kept.empty() ? now : kept.front().start;
                char key[96];
                snprintf(key, sizeof(key), "%s%016lX:%s", kResidualPrefix, tid, uid_strs[u].c_str());
                bool deleted = false;
                {
                    Stmt q(d.db, "SELECT 1 FROM import_deleted WHERE sync_id = ?");
                    if (q) q.text(1, key);
                    deleted = q && q.row();
                }
                Stmt q(d.db, "SELECT id, game_id = ? AND user_id = ? FROM sessions WHERE sync_id = ?");
                if (!q) ok = false;
                else q.i64(1, game).i64(2, user_ids[u]).text(3, key);
                if (!ok || deleted) {
                    // excluída pelo usuário: não volta
                } else if (!q.row()) {
                    if (residual > 0) ok = insert({start, start + residual, residual}, key);
                } else if (q.col_i64(1)) {
                    // Ainda no jogo e perfil de origem: acompanha o total oficial.
                    int64_t id = q.col_i64(0);
                    Stmt w(d.db, residual > 0 ? "UPDATE sessions SET start_time = ?, end_time = ?, duration = ? WHERE id = ?"
                                              : "DELETE FROM sessions WHERE id = ?4");
                    if (w) w.i64(1, start).i64(2, start + residual).i64(3, residual).i64(4, id);
                    ok = exec_ok(w, d.db);
                }  // movida ou reassociada pelo usuário: fica como está
            }
            r.sessions += written;
            r.skipped += overlap + already;
            r.seconds += written_secs;
            import_log("%016lX perfil %s: total do sistema %lld s; eventos %zu sessoes (%lld s em foco); "
                       "ja no Ruby Achievements %lld s (%d sessoes sobrepostas); ja importadas/excluidas %d (%lld s); "
                       "importadas %d (%lld s, residual %lld s)%s",
                       tid, uid_strs[u].c_str(), (long long)official, list.size(), (long long)raw_sum,
                       (long long)existing, overlap, already, (long long)known_secs, written, (long long)written_secs,
                       (long long)residual,
                       not_counted ? " ; sistema nao conta este perfil: eventos ignorados" : "");
        }
    }

    for (int64_t g : touched) pt_db_recalc_game(d.db, g, now);
    if (!ok && g_error.empty()) g_error = sqlite3_errmsg(d.db);
    sqlite3_exec(d.db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    if (ok) r.new_games = (int)(pt_db_scalar(d.db, "SELECT COUNT(*) FROM games", 0, 0, 0) - games_before);
    import_log("---- %s: %d sessoes importadas, %lld s, %d ja no Ruby Achievements ----", ok ? "concluida" : "FALHOU",
               r.sessions, (long long)r.seconds, r.skipped);
    return ok;
}

// --- Tracking por jogo --------------------------------------------------------------
bool set_tracking(int64_t game_id, bool enabled) {
    Db d;
    if (!d) return false;
    if (pt_db_set_tracking(d.db, game_id, enabled, "user", pt_now()) != 0) {
        g_error = sqlite3_errmsg(d.db);
        return false;
    }
    return true;
}

// --- Jogos desinstalados e forwarder fantasma -------------------------------------
namespace {

constexpr const char* kGameCols =
    "g.id, COALESCE(g.title_id,''), g.display_name, COALESCE(g.rom_path,''), g.source_type, COALESCE(g.cover_path,''), "
    "g.total_playtime, COALESCE(g.last_played,0), (SELECT COUNT(*) FROM sessions s WHERE s.game_id=g.id)";

GameRow game_from(Stmt& s) {
    GameRow g;
    g.id = s.col_i64(0);
    g.title_id = s.col_str(1);
    g.name = s.col_str(2);
    g.rom_path = s.col_str(3);
    g.source = s.col_str(4);
    g.cover = s.col_str(5);
    g.total = s.col_i64(6);
    g.last_played = s.col_i64(7);
    g.session_count = s.col_i64(8);
    return g;
}

bool copy_file(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) ok = ok && fwrite(buf, 1, n, out) == n;
    fclose(in);
    ok = fclose(out) == 0 && ok;
    return ok;
}

bool write_text(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

}  // namespace

bool uninstalled_games(std::vector<GameRow>& out) {
    out.clear();
    // Títulos instalados agora (com o conteúdo base, não só o registro na Home).
    std::vector<std::string> installed;
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        char buf[64];
        snprintf(buf, sizeof(buf), tr("Serviço ns indisponível (0x%X)."), rc);
        g_error = buf;
        return false;
    }
    for (uint64_t t : installed_titles()) {
        char tid[17];
        snprintf(tid, sizeof(tid), "%016lX", t);
        installed.push_back(tid);
    }
    nsExit();

    Db d;
    if (!d) return false;
    std::string sql = std::string("SELECT ") + kGameCols +
                      " FROM games g WHERE g.title_id IS NOT NULL AND g.source_type IN ('NATIVE','FORWARDER') "
                      "AND g.status NOT LIKE 'MERGED:%' AND g.total_playtime > 0 ORDER BY g.last_played DESC";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    while (s.row()) {
        GameRow g = game_from(s);
        if (std::find(installed.begin(), installed.end(), g.title_id) == installed.end()) out.push_back(g);
    }
    return true;
}

bool disabled_games(std::vector<GameRow>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    std::string sql = std::string("SELECT ") + kGameCols +
                      ", t.reason FROM games g JOIN tracking_disabled t ON t.game_id = g.id "
                      "WHERE t.disabled = 1 AND g.status NOT LIKE 'MERGED:%' ORDER BY t.updated_at DESC";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    while (s.row()) {
        GameRow g = game_from(s);
        g.auto_disabled = s.col_str(9) == "auto";
        out.push_back(g);
    }
    return true;
}

bool game_by_title(const std::string& title_id, GameRow& out) {
    Db d;
    if (!d) return false;
    std::string sql = std::string("SELECT ") + kGameCols +
                      " FROM games g WHERE g.title_id = ? AND g.source_type IN ('NATIVE','FORWARDER') ORDER BY g.id LIMIT 1";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.text(1, title_id);
    if (!s.row()) {
        g_error = trf("Título %s não está no banco do Ruby Achievements.", title_id.c_str());
        return false;
    }
    int64_t id = pt_db_resolve_game(d.db, s.col_i64(0));
    out = game_from(s);
    out.id = id;
    if (id != s.col_i64(0)) {  // unificado: mostra o jogo de destino
        Stmt t(d.db, (std::string("SELECT ") + kGameCols + " FROM games g WHERE g.id = ?").c_str());
        t.i64(1, id);
        if (t && t.row()) out = game_from(t);
    }
    return true;
}

std::string ghost_args(const std::string& title_id) { return "--ghost " + title_id; }

std::string ghost_title_id(const std::string& title_id) {
    // Mesma fórmula do Sphaira (faixa 0x05...): fixo por jogo e não colide
    // com o jogo original se ele for reinstalado.
    char buf[17];
    snprintf(buf, sizeof(buf), "%016lX", pt_fwd::forwarder_title_id(PT_APP_NRO, ghost_args(title_id)));
    return buf;
}

bool ghost_installed(const std::string& title_id) {
    char buf[4096];
    if (pt_read_file(PT_GHOST_TIDS_PATH, buf, sizeof(buf)) <= 0) return false;
    return strstr(buf, ghost_title_id(title_id).c_str()) != nullptr;
}

bool install_ghost_forwarder(const GameRow& g, const std::string& name, const std::string& icon,
                             const std::string& keys_path, const GhostProgress& progress, std::string& message,
                             bool& need_keys) {
    need_keys = false;
    if (g.title_id.empty()) {
        message = tr("Só títulos do Switch (com Title ID) têm forwarder fantasma.");
        return false;
    }
    pt_fwd::Config cfg;
    cfg.nro_path = PT_APP_NRO;
    cfg.args = ghost_args(g.title_id);
    cfg.name = name.empty() ? g.name : name;
    cfg.author = "Ruby Achievements";
    cfg.keys_path = keys_path;
    FILE* f = fopen(icon.c_str(), "rb");
    if (!f) {
        message = trf("Não foi possível ler o ícone %s.", icon.c_str());
        return false;
    }
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) cfg.icon.insert(cfg.icon.end(), chunk, chunk + n);
    fclose(f);

    // O sysmodule não conta tempo para o forwarder fantasma (ver PT_GHOST_TIDS_PATH).
    std::string tid = ghost_title_id(g.title_id);
    if (!ghost_installed(g.title_id)) {
        FILE* t = fopen(PT_GHOST_TIDS_PATH, "ab");
        if (t) {
            fprintf(t, "%s\n", tid.c_str());
            fclose(t);
        }
    }

    u64 installed = 0;
    std::string error;
    Result rc = pt_fwd::install_forwarder(cfg, NcmStorageId_SdCard, progress, &installed, &error);
    char buf[64];
    snprintf(buf, sizeof(buf), "%016lX", installed);
    pt_log_file(PT_DIR "/forwarder.log", "fantasma de %s (%s) como %s: %s", g.title_id.c_str(), cfg.name.c_str(),
                tid.c_str(), R_SUCCEEDED(rc) ? "instalado" : error.c_str());
    if (R_FAILED(rc)) {
        need_keys = error.find("prod.keys") != std::string::npos;
        message = error;
        return false;
    }
    message = trf("\"%s\" foi instalado na Home (Title ID %s).", cfg.name.c_str(), buf);
    return true;
}

bool create_ghost_package(const GameRow& g, const std::string& name, const std::string& icon, std::string& dir_out) {
    if (g.title_id.empty()) {
        g_error = tr("Só títulos do Switch (com Title ID) têm forwarder fantasma.");
        return false;
    }
    std::string base = std::string(PT_GHOSTS_DIR);
    mkdir(base.c_str(), 0777);
    std::string dir = base + "/" + g.title_id;
    mkdir(dir.c_str(), 0777);

    bool icon_ok = icon.empty() || copy_file(icon, dir + "/icon.jpg");
    std::string ghost_id = ghost_title_id(g.title_id);
    std::string args = ghost_args(g.title_id);
    std::string info =
        "nome=" + name + "\n"
        "editora=Ruby Achievements\n"
        "title_id=" + ghost_id + "\n"
        "nro=" PT_APP_NRO "\n"
        "argumentos=" + args + "\n"
        "argv=" PT_APP_NRO " " + args + "\n"
        "icone=" + (icon.empty() ? std::string("(nenhum)") : dir + "/icon.jpg") + "\n"
        "jogo_original=" + g.title_id + "\n";
    std::string readme =
        "Forwarder fantasma de \"" + name + "\" (" + g.title_id + ")\n\n"
        "Ao ser aberto na Home, este forwarder abre o Ruby Achievements em uma tela\n"
        "dizendo que o jogo está desinstalado, com o tempo total jogado.\n\n"
        "O NSP do forwarder é montado no PC (as chaves do console e a instalação\n"
        "de conteúdo não oficial ficam fora do Ruby Achievements). Com o NSP Forwarder Studio\n"
        "ou outra ferramenta de forwarder para NRO:\n\n"
        "  1. Copie esta pasta para o PC.\n"
        "  2. Nome do app: " + name + "\n"
        "     Editora:     Ruby Achievements\n"
        "     Title ID:    " + ghost_id + "\n"
        "     Ícone:       icon.jpg (JPEG 256x256; se a ferramenta reclamar,\n"
        "                  converta para 256x256)\n"
        "  3. Caminho do NRO no SD: " PT_APP_NRO "\n"
        "     Argumentos:           " + args + "\n"
        "     (ferramentas que pedem o argv completo: " PT_APP_NRO " " + args + ")\n"
        "  4. Gere o NSP e instale com o seu instalador habitual.\n\n"
        "Os mesmos dados estão em forwarder.txt.\n";
    if (!write_text(dir + "/forwarder.txt", info) || !write_text(dir + "/LEIA-ME.txt", readme)) {
        g_error = trf("Não foi possível gravar em %s.", dir.c_str());
        return false;
    }
    if (!icon_ok) {
        g_error = trf("Pacote criado, mas o ícone não pôde ser copiado de %s.", icon.c_str());
        dir_out = dir;
        return false;
    }
    dir_out = dir;
    return true;
}

// --- Conquistas ----------------------------------------------------------------------
namespace {
// Primeiro desbloqueio de cada conquista para o perfil (?1 = 0: qualquer perfil).
constexpr const char* kUnlocks =
    "LEFT JOIN (SELECT achievement_id, MIN(unlocked_at) AS at FROM user_achievements "
    "WHERE ?1 = 0 OR user_id = ?1 GROUP BY achievement_id) u ON u.achievement_id = a.id ";
}  // namespace

bool achievement_games(int64_t user_filter, std::vector<AchievementGameRow>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    std::string sql = std::string(
                          "SELECT g.id, g.display_name, COALESCE(g.cover_path,''), COUNT(a.id), COUNT(u.at),"
                          " COALESCE(MAX(u.at),0) "
                          "FROM achievements a JOIN games g ON g.id = a.game_id ") +
                      kUnlocks + "GROUP BY g.id ORDER BY 6 DESC, g.display_name COLLATE NOCASE";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.i64(1, user_filter);
    while (s.row()) {
        AchievementGameRow r;
        r.game_id = s.col_i64(0);
        r.name = s.col_str(1);
        r.cover = s.col_str(2);
        r.total = (int)s.col_i64(3);
        r.unlocked = (int)s.col_i64(4);
        r.last_unlock = s.col_i64(5);
        out.push_back(r);
    }
    return true;
}

bool achievements(int64_t game_id, int64_t user_filter, std::vector<AchievementRow>& out) {
    out.clear();
    Db d;
    if (!d) return false;
    std::string sql = std::string(
                          "SELECT a.id, a.title, COALESCE(a.description,''), COALESCE(a.icon_local_path,''),"
                          " a.is_secret, COALESCE(u.at,0), COALESCE(a.rarity,'bronze') FROM achievements a ") +
                      kUnlocks + "WHERE a.game_id = ?2 ORDER BY u.at IS NULL, u.at DESC, a.sort_order, a.title";
    Stmt s(d.db, sql.c_str());
    if (!s) return false;
    s.i64(1, user_filter).i64(2, game_id);
    while (s.row()) {
        AchievementRow r;
        r.id = s.col_str(0);
        r.title = s.col_str(1);
        r.description = s.col_str(2);
        r.icon = s.col_str(3);
        r.secret = s.col_i64(4) != 0;
        r.unlocked_at = s.col_i64(5);
        r.rarity = s.col_str(6);
        out.push_back(r);
    }
    return true;
}

namespace {
// Chave de pacote de um jogo nativo: o Title ID dele (16 hex), "" se não tiver.
std::string pack_key(sqlite3* db, int64_t game_id) {
    Stmt s(db, "SELECT COALESCE(title_id,'') FROM games WHERE id=? AND source_type IN ('NATIVE','FORWARDER')");
    if (!s) return "";
    s.i64(1, game_id);
    std::string key = s.row() ? s.col_str(0) : "";
    for (auto& c : key) c = (char)toupper((unsigned char)c);
    return pt_pack_is_title_key(key) ? key : "";
}

// Lê o pacote ativo da chave e grava os metadados no jogo.
void load_active_pack(sqlite3* db, const std::string& key, int64_t game_id) {
    JVal root;
    std::string file = pt_pack_active(key);
    if (!file.empty() && pt_pack_load(file, root)) pt_pack_sync_meta(db, key, game_id, root);
}
}  // namespace

bool sync_achievement_packs() {
    std::vector<std::string> keys = pt_pack_title_keys();
    if (keys.empty()) return true;
    Db d;
    if (!d) return false;
    bool ns_tried = false;
    Result ns_rc = 1;
    std::vector<uint64_t> installed;
    int64_t now = pt_now();
    sqlite3_exec(d.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    for (auto& key : keys) {
        uint64_t tid = strtoull(key.c_str(), nullptr, 16);
        JVal root;
        if (!pt_pack_load(pt_pack_active(key), root)) continue;  // pacote inválido: nem cria o jogo
        Stmt s(d.db, "SELECT 1 FROM games WHERE title_id=? AND source_type IN ('NATIVE','FORWARDER') LIMIT 1");
        if (!s) continue;
        s.text(1, key);
        if (!s.row()) {
            // Ainda não está no banco: só entra se estiver instalado (senão, nada a mostrar).
            if (!ns_tried) {
                ns_tried = true;
                ns_rc = nsInitialize();
                if (R_SUCCEEDED(ns_rc)) installed = installed_titles();
            }
            if (std::find(installed.begin(), installed.end(), tid) == installed.end()) continue;
        }
        int64_t game = pt_db_title_game(d.db, tid, now, read_title);  // segue unificações; cria pelo NACP
        if (game > 0) pt_pack_sync_meta(d.db, key, game, root);
    }
    sqlite3_exec(d.db, "COMMIT", nullptr, nullptr, nullptr);
    if (ns_tried && R_SUCCEEDED(ns_rc)) nsExit();
    return true;
}

bool achievement_packs(int64_t game_id, std::vector<AchievementPack>& out) {
    out.clear();
    std::string key;
    {
        Db d;
        if (!d) return false;
        key = pack_key(d.db, game_id);
    }
    if (key.empty()) return true;
    std::string active = pt_pack_active(key);
    for (auto& f : pt_pack_files(key)) {
        AchievementPack p;
        p.file = f;
        p.active = f == active;
        JVal root;
        std::string variant = pt_pack_variant(key, f);
        if (pt_pack_load(f, root)) {
            p.label = root.get_str("pack_name");
            p.count = (int)root.get("achievements")->arr.size();
        }
        if (p.label.empty()) p.label = variant.empty() ? std::string(tr("principal")) : variant;
        out.push_back(p);
    }
    return true;
}

bool set_active_pack(int64_t game_id, const std::string& file) {
    Db d;
    if (!d) return false;
    std::string key = pack_key(d.db, game_id);
    if (key.empty()) {
        g_error = tr("Só jogos do Switch (com Title ID) têm pacotes no cartão SD.");
        return false;
    }
    std::string err;
    if (!pt_pack_switch(d.db, key, file, &err)) {
        g_error = err;
        return false;
    }
    // A galeria já mostra o pacote novo, do zero (uma transação: uma gravação no SD).
    sqlite3_exec(d.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    load_active_pack(d.db, key, game_id);
    sqlite3_exec(d.db, "COMMIT", nullptr, nullptr, nullptr);
    return true;
}

bool rom_hash_lookup(const std::string& path, int64_t size, int64_t mtime, std::string& md5) {
    md5.clear();
    Db d;
    if (!d) return false;
    Stmt s(d.db, "SELECT md5_hash FROM rom_hash_cache WHERE file_path=? AND file_size=? AND mtime=?");
    if (!s) return false;
    s.text(1, path).i64(2, size).i64(3, mtime);
    if (s.row()) md5 = s.col_str(0);
    return !md5.empty();
}

bool rom_hash_store(const std::string& path, int64_t size, int64_t mtime, const std::string& md5) {
    Db d;
    if (!d) return false;
    Stmt s(d.db, "INSERT OR REPLACE INTO rom_hash_cache(file_path,file_size,mtime,md5_hash) VALUES(?,?,?,?)");
    if (!s) return false;
    s.text(1, path).i64(2, size).i64(3, mtime).text(4, md5);
    return exec_ok(s, d.db);
}

int64_t main_user_for_game(int64_t game_id) {
    Db d;
    if (!d) return 0;
    int64_t u = pt_db_scalar(d.db,
                             "SELECT user_id FROM sessions WHERE game_id=? GROUP BY user_id "
                             "ORDER BY SUM(duration) DESC LIMIT 1",
                             game_id, 0, 0);
    return u > 0 ? u : pt_db_shared_user(d.db, pt_now());
}

bool store_ra_achievements(int64_t game_id, int64_t user_id, const std::vector<RaAchievement>& list,
                           int& new_unlocks) {
    new_unlocks = 0;
    Db d;
    if (!d) return false;
    sqlite3_exec(d.db, "BEGIN", nullptr, nullptr, nullptr);
    bool ok = true;
    for (const RaAchievement& a : list) {
        std::string id = "RA:" + std::to_string(a.id);
        Stmt up(d.db,
                "INSERT INTO achievements(id,game_id,source,title,description,icon_url,icon_local_path,"
                "sort_order) VALUES(?1,?2,'RA',?3,?4,?5,?6,?7) ON CONFLICT(id) DO UPDATE SET game_id=?2,"
                "title=?3,description=?4,icon_url=?5,icon_local_path=COALESCE(?6,icon_local_path),"
                "sort_order=?7");
        if (!up) { ok = false; break; }
        up.text(1, id).i64(2, game_id).text(3, a.title).text(4, a.description).text(5, a.icon_url);
        if (a.icon_path.empty()) up.null(6);
        else up.text(6, a.icon_path);
        up.i64(7, a.order);
        if (!exec_ok(up, d.db)) { ok = false; break; }
        if (a.unlocked_at <= 0) continue;
        Stmt ins(d.db, "INSERT OR IGNORE INTO user_achievements(achievement_id,user_id,unlocked_at) VALUES(?,?,?)");
        if (!ins) { ok = false; break; }
        ins.text(1, id).i64(2, user_id).i64(3, a.unlocked_at);
        if (!exec_ok(ins, d.db)) { ok = false; break; }
        new_unlocks += sqlite3_changes(d.db);
    }
    sqlite3_exec(d.db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

std::vector<std::string> cover_files() {
    std::vector<std::string> out;
    DIR* dir = opendir(PT_COVERS_DIR);
    if (!dir) return out;
    while (dirent* e = readdir(dir)) {
        std::string n = e->d_name;
        if (n.size() < 5) continue;
        std::string ext = n.substr(n.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".png" || n.size() > 5 && n.substr(n.size() - 5) == ".jpeg")
            out.push_back(std::string(PT_COVERS_DIR) + "/" + n);
    }
    closedir(dir);
    std::sort(out.begin(), out.end());
    return out;
}

std::string system_icon(const std::string& title_id) {
    if (title_id.empty()) return "";
    std::string p = std::string(PT_ICONS_DIR) + "/" + title_id + ".jpg";
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? p : "";
}

std::string format_duration(int64_t seconds) {
    char buf[32];
    pt_format_duration(buf, sizeof(buf), seconds);
    return buf;
}

std::string format_date(int64_t epoch) {
    if (epoch <= 0) return "-";
    TimeCalendarTime ct;
    TimeCalendarAdditionalInfo info;
    char buf[32];
    // Formato escolhido na aba Interface (MM/DD/YYYY, DD/MM/YYYY ou YYYY/MM/DD).
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)epoch, &ct, &info)))
        return i18n::format_datetime(ct.year, ct.month, ct.day, ct.hour, ct.minute, i18n::date_format());
    time_t t = (time_t)epoch;
    struct tm* tm = gmtime(&t);
    if (!tm) return "-";
    (void)buf;
    return i18n::format_datetime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
                                 i18n::date_format()) +
           " UTC";
}

std::string format_clock(int64_t epoch) {
    TimeCalendarTime ct;
    TimeCalendarAdditionalInfo info;
    char buf[8];
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)epoch, &ct, &info))) {
        snprintf(buf, sizeof(buf), "%02d:%02d", ct.hour, ct.minute);
    } else {
        time_t t = (time_t)epoch;
        strftime(buf, sizeof(buf), "%H:%M", gmtime(&t));
    }
    return buf;
}

bool user_avatar(const std::string& uid_text, std::vector<unsigned char>& jpeg) {
    jpeg.clear();
    if (uid_text.size() != 32) return false;
    AccountUid uid;
    uid.uid[1] = strtoull(uid_text.substr(0, 16).c_str(), nullptr, 16);  // formato de pt_uid_to_string
    uid.uid[0] = strtoull(uid_text.substr(16).c_str(), nullptr, 16);
    if (!accountUidIsValid(&uid) || R_FAILED(accountInitialize(AccountServiceType_Application))) return false;
    AccountProfile profile;
    if (R_SUCCEEDED(accountGetProfile(&profile, uid))) {
        u32 size = 0, got = 0;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &size)) && size > 0 && size <= 512 * 1024) {
            jpeg.resize(size);
            if (R_FAILED(accountProfileLoadImage(&profile, jpeg.data(), size, &got))) got = 0;
            jpeg.resize(got);
        }
        accountProfileClose(&profile);
    }
    accountExit();
    return !jpeg.empty();
}

std::string source_label(const std::string& source) {
    if (source == "FORWARDER") return "Forwarder";
    if (source == "EMULATOR_ROM") return tr("ROM de emulador");
    return tr("Nativo");
}

}  // namespace data
