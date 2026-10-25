#include "pt_pack.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>

#include "pt_paths.h"
#include "pt_util.h"

namespace {

bool ends_with(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool file_exists(const std::string& file) {
    std::string path = std::string(PT_ACH_DIR) + "/" + file;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

std::vector<std::string> list_dir() {
    std::vector<std::string> out;
    DIR* d = opendir(PT_ACH_DIR);
    if (!d) return out;
    while (struct dirent* e = readdir(d))
        if (e->d_type != DT_DIR && ends_with(e->d_name, kPtPackExt)) out.push_back(e->d_name);
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

// Escolhas gravadas pelo app: "<chave>|<arquivo>" por linha.
std::vector<std::pair<std::string, std::string>> read_choices() {
    std::vector<std::pair<std::string, std::string>> out;
    char buf[8192];
    long n = pt_read_file(PT_ACH_ACTIVE_PATH, buf, sizeof(buf) - 1);
    if (n <= 0) return out;
    buf[n] = '\0';
    char* save = nullptr;
    for (char* line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(nullptr, "\r\n", &save)) {
        char* bar = strchr(line, '|');
        if (bar && bar != line && bar[1]) out.push_back({std::string(line, bar - line), bar + 1});
    }
    return out;
}

bool write_choices(const std::vector<std::pair<std::string, std::string>>& choices) {
    std::string text;
    for (auto& c : choices) text += c.first + "|" + c.second + "\n";
    return pt_write_file_atomic(PT_ACH_ACTIVE_PATH, text.data(), text.size()) == 0;
}

bool exec_key(sqlite3* db, const char* sql, const std::string& prefix) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::string id_prefix(const std::string& key) { return "L:" + key + ":"; }

}  // namespace

bool pt_pack_is_title_key(const std::string& key) {
    return key.size() == 16 && key.find_first_not_of("0123456789ABCDEFabcdef") == std::string::npos;
}

std::vector<std::string> pt_pack_files(const std::string& key) {
    std::vector<std::string> out;
    if (key.empty()) return out;
    std::string main = key + kPtPackExt;
    if (!pt_pack_is_title_key(key)) {
        if (file_exists(main)) out.push_back(main);
        return out;
    }
    std::string found_main;  // o nome como está no SD (o Title ID pode estar em minúsculas)
    for (auto& f : list_dir()) {
        if (f.size() < key.size() || strncasecmp(f.c_str(), key.c_str(), key.size()) != 0) continue;
        std::string rest = f.substr(key.size());
        if (rest == kPtPackExt) found_main = f;
        else if (rest.size() > strlen(kPtPackExt) + 1 && rest[0] == '.') out.push_back(f);  // ".<variante>.rubydb"
    }
    if (!found_main.empty()) out.insert(out.begin(), found_main);
    return out;
}

std::string pt_pack_variant(const std::string& key, const std::string& file) {
    size_t ext = strlen(kPtPackExt);
    if (file.size() <= key.size() + ext + 1) return "";
    return file.substr(key.size() + 1, file.size() - key.size() - 1 - ext);
}

std::string pt_pack_active(const std::string& key) {
    std::vector<std::string> files = pt_pack_files(key);
    if (files.empty()) return "";
    for (auto& c : read_choices())
        if (c.first == key && std::find(files.begin(), files.end(), c.second) != files.end()) return c.second;
    return files[0];
}

namespace {

constexpr const char* kIconRefPrefix = "rubydb:";

// Conexão só-leitura com um pacote, fechada no destrutor.
struct PackDb {
    sqlite3* db = nullptr;
    ~PackDb() {
        if (db) sqlite3_close(db);
    }
    bool open(const std::string& file, std::string* err) {
        auto fail = [&](const char* why) {
            if (err) *err = why;
            return false;
        };
        if (file.empty() || file.find('/') != std::string::npos || file.find("..") != std::string::npos)
            return fail("nome de arquivo inválido");
        if (!file_exists(file)) return fail("arquivo não encontrado");
        std::string path = std::string(PT_ACH_DIR) + "/" + file;
        if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            return fail("não abriu como banco SQLite");
        sqlite3_exec(db, "PRAGMA cache_size=-32", nullptr, nullptr, nullptr);  // 32 KB: heap do sysmodule
        sqlite3_stmt* st = nullptr;
        int version = -1;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, nullptr) == SQLITE_OK &&
            sqlite3_step(st) == SQLITE_ROW)
            version = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        if (version != 1) return fail("não é um .rubydb do formato 1");
        return true;
    }
};

JVal jstr(const std::string& s) {
    JVal v;
    v.type = JVal::Str;
    v.str = s;
    return v;
}

std::string col_text(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? std::string((const char*)t, (size_t)sqlite3_column_bytes(st, i)) : std::string();
}

bool is_meta_key(const std::string& k) {
    for (const char* m : {"id", "title", "description", "rarity", "secret", "icon", "icon_locked"})
        if (k == m) return true;
    return false;
}

// rowid da linha de `sql` (um parâmetro de texto) ou 0.
sqlite3_int64 find_rowid(sqlite3* db, const char* sql, const std::string& arg) {
    sqlite3_stmt* st = nullptr;
    sqlite3_int64 id = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, arg.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    return id;
}

// BLOB -> arquivo em pedaços de 16 KB (via .tmp; a FS do Switch não renomeia por cima).
bool blob_to_file(sqlite3* db, const char* table, const char* column, sqlite3_int64 rowid, size_t max,
                  const std::string& out_path) {
    sqlite3_blob* b = nullptr;
    if (rowid == 0 || sqlite3_blob_open(db, "main", table, column, rowid, 0, &b) != SQLITE_OK) return false;
    int size = sqlite3_blob_bytes(b);
    std::string tmp = out_path + ".tmp";
    FILE* f = size > 0 && (size_t)size <= max ? fopen(tmp.c_str(), "wb") : nullptr;
    bool ok = f != nullptr;
    char buf[16 * 1024];
    for (int at = 0; ok && at < size;) {
        int n = size - at < (int)sizeof(buf) ? size - at : (int)sizeof(buf);
        ok = sqlite3_blob_read(b, buf, n, at) == SQLITE_OK && fwrite(buf, 1, (size_t)n, f) == (size_t)n;
        at += n;
    }
    sqlite3_blob_close(b);
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(out_path.c_str());
        ok = rename(tmp.c_str(), out_path.c_str()) == 0;
    }
    if (!ok) remove(tmp.c_str());
    return ok;
}

}  // namespace

bool pt_pack_load(const std::string& file, JVal& root, std::string* err) {
    root = JVal();
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        root = JVal();
        return false;
    };
    PackDb p;
    if (!p.open(file, err)) return false;

    sqlite3_stmt* st = nullptr;
    root.type = JVal::Obj;
    if (sqlite3_prepare_v2(p.db,
                           "SELECT title, COALESCE(target_title_id,''), COALESCE(supported_build_ids,'') "
                           "FROM pack_info LIMIT 1",
                           -1, &st, nullptr) != SQLITE_OK ||
        sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return fail("sem pack_info");
    }
    root.obj.push_back({"pack_name", jstr(col_text(st, 0))});
    root.obj.push_back({"title_id", jstr(col_text(st, 1))});
    root.obj.push_back({"build_id", jstr(col_text(st, 2))});
    sqlite3_finalize(st);

    if (sqlite3_prepare_v2(p.db,
                           "SELECT id, title, description, rarity, secret, condition_json, "
                           "icon_unlocked IS NOT NULL FROM achievements ORDER BY sort_order, id",
                           -1, &st, nullptr) != SQLITE_OK)
        return fail("sem a tabela achievements");
    JVal list;
    list.type = JVal::Arr;
    size_t text = 0;
    std::string why;
    while (why.empty() && sqlite3_step(st) == SQLITE_ROW) {
        std::string aid = col_text(st, 0), title = col_text(st, 1), desc = col_text(st, 2), cond = col_text(st, 5);
        text += title.size() + desc.size() + cond.size();
        if (text > kPtPackMaxText) {
            why = "texto das conquistas acima de 64 KB";
            break;
        }
        JVal a;
        a.type = JVal::Obj;
        a.obj.push_back({"id", jstr(aid)});
        a.obj.push_back({"title", jstr(title)});
        a.obj.push_back({"description", jstr(desc)});
        a.obj.push_back({"rarity", jstr(col_text(st, 3))});
        JVal secret;
        secret.type = JVal::Bool;
        secret.b = sqlite3_column_int(st, 4) != 0;
        a.obj.push_back({"secret", secret});
        if (sqlite3_column_int(st, 6)) a.obj.push_back({"icon", jstr(pt_pack_icon_ref(file, aid))});
        JVal c;
        if (!json_parse(cond.data(), cond.size(), c) || c.type != JVal::Obj) {
            why = "condição inválida em " + aid;
            break;
        }
        for (auto& kv : c.obj)  // as colunas valem; a condição não as sobrescreve
            if (!is_meta_key(kv.first)) a.obj.push_back(std::move(kv));
        list.arr.push_back(std::move(a));
    }
    sqlite3_finalize(st);
    if (!why.empty()) return fail(why);
    root.obj.push_back({"achievements", std::move(list)});
    return true;
}

std::string pt_pack_icon_ref(const std::string& file, const std::string& aid) {
    return kIconRefPrefix + file + "#" + aid;
}

bool pt_pack_parse_icon_ref(const std::string& ref, std::string& file, std::string& aid) {
    size_t n = strlen(kIconRefPrefix), hash = ref.rfind('#');
    if (ref.compare(0, n, kIconRefPrefix) != 0 || hash == std::string::npos || hash <= n) return false;
    file = ref.substr(n, hash - n);
    aid = ref.substr(hash + 1);
    return !aid.empty() && file.find('/') == std::string::npos && file.find("..") == std::string::npos;
}

bool pt_pack_read_icon(const std::string& file, const std::string& aid, bool locked, std::string& png) {
    png.clear();
    PackDb p;
    if (!p.open(file, nullptr)) return false;
    sqlite3_stmt* st = nullptr;
    const char* sql = locked ? "SELECT icon_locked FROM achievements WHERE id=?1"
                             : "SELECT icon_unlocked FROM achievements WHERE id=?1";
    if (sqlite3_prepare_v2(p.db, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, aid.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_BLOB) {
            int n = sqlite3_column_bytes(st, 0);
            if (n > 0 && (size_t)n <= kPtPackMaxIcon) png.assign((const char*)sqlite3_column_blob(st, 0), (size_t)n);
        }
    }
    sqlite3_finalize(st);
    return !png.empty();
}

bool pt_pack_extract_icon(const std::string& file, const std::string& aid, const std::string& out_path) {
    PackDb p;
    if (!p.open(file, nullptr)) return false;
    sqlite3_int64 row = find_rowid(p.db, "SELECT rowid FROM achievements WHERE id=?1 AND icon_unlocked IS NOT NULL", aid);
    return blob_to_file(p.db, "achievements", "icon_unlocked", row, kPtPackMaxIcon, out_path);
}

const char* pt_pack_rarity(const JVal& a) {
    std::string r = a.get_str("rarity");
    for (const char* k : {"silver", "gold", "ruby"})
        if (r == k) return k;
    return "bronze";
}

int pt_pack_sync_meta(sqlite3* db, const std::string& key, int64_t game_id, const JVal& root) {
    const JVal* list = root.get("achievements");
    if (!list || list->type != JVal::Arr) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO achievements(id,game_id,source,title,description,is_secret,sort_order,"
                           "icon_local_path,rarity) VALUES(?1,?2,'LOCAL',?3,?4,?5,?6,?7,?8) ON CONFLICT(id) DO "
                           "UPDATE SET game_id=?2,title=?3,description=?4,is_secret=?5,sort_order=?6,"
                           "icon_local_path=?7,rarity=?8",
                           -1, &st, nullptr) != SQLITE_OK)
        return 0;
    int n = 0;
    bool ruby_seen = false;
    for (size_t i = 0; i < list->arr.size(); i++) {
        const JVal& a = list->arr[i];
        std::string aid = a.get_str("id");
        if (a.type != JVal::Obj || aid.empty()) continue;
        const char* rarity = pt_pack_rarity(a);
        if (!strcmp(rarity, "ruby")) {
            if (ruby_seen) continue;  // só a primeira Ruby vale
            ruby_seen = true;
        }
        std::string id = id_prefix(key) + aid;
        std::string title = a.get_str("title", aid.c_str());
        std::string desc = a.get_str("description");
        const JVal* secret = a.get("secret");
        // "icon": referência ao ícone dentro do pacote (pt_pack_load), nunca um caminho.
        std::string icon_path = a.get_str("icon"), ref_file, ref_aid;
        bool icon_ok = pt_pack_parse_icon_ref(icon_path, ref_file, ref_aid);
        sqlite3_reset(st);
        sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, game_id);
        sqlite3_bind_text(st, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, desc.c_str(), -1, SQLITE_TRANSIENT);
        // "points" de pacotes antigos é ignorado (pontos removidos em 2026-10-20).
        sqlite3_bind_int64(st, 5, secret && secret->type == JVal::Bool && secret->b ? 1 : 0);
        sqlite3_bind_int64(st, 6, (int64_t)i);
        if (icon_ok) sqlite3_bind_text(st, 7, icon_path.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(st, 7);
        sqlite3_bind_text(st, 8, rarity, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_DONE) n++;
    }
    sqlite3_finalize(st);
    return n;
}

bool pt_pack_has_meta(sqlite3* db, const std::string& key) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM achievements WHERE substr(id, 1, length(?1)) = ?1 LIMIT 1", -1, &st,
                           nullptr) != SQLITE_OK)
        return true;  // na dúvida, não força
    std::string prefix = id_prefix(key);
    sqlite3_bind_text(st, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    bool yes = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return yes;
}

bool pt_pack_switch(sqlite3* db, const std::string& key, const std::string& file, std::string* err) {
    std::vector<std::string> files = pt_pack_files(key);
    if (std::find(files.begin(), files.end(), file) == files.end()) {
        if (err) *err = "pacote não encontrado no cartão SD";
        return false;
    }
    // Primeiro o banco (tudo ou nada), depois a escolha: com o lock do banco nas mãos
    // o sysmodule não avalia no meio, e um progresso antigo nunca vale para o pacote novo.
    const std::string prefix = id_prefix(key);
    const char* const kDeletes[] = {
        "DELETE FROM achievement_queue WHERE substr(achievement_id, 1, length(?1)) = ?1",
        "DELETE FROM user_achievements WHERE substr(achievement_id, 1, length(?1)) = ?1",
        "DELETE FROM achievements WHERE substr(id, 1, length(?1)) = ?1",
    };
    bool ok = sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK;
    for (const char* sql : kDeletes) ok = ok && exec_key(db, sql, prefix);
    if (ok) ok = sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }
    auto choices = read_choices();
    choices.erase(std::remove_if(choices.begin(), choices.end(), [&](auto& c) { return c.first == key; }),
                  choices.end());
    choices.push_back({key, file});
    if (!write_choices(choices)) {
        // O progresso já foi apagado: o pacote de antes recomeça do zero (sem misturar).
        if (err) *err = "progresso apagado, mas a escolha não foi gravada em " PT_ACH_ACTIVE_PATH;
        return false;
    }
    return true;
}

std::vector<std::string> pt_pack_title_keys() {
    std::vector<std::string> out;
    for (auto& f : list_dir()) {
        if (f.size() < 16 + strlen(kPtPackExt)) continue;
        std::string key = f.substr(0, 16);
        if (!pt_pack_is_title_key(key) || (f[16] != '.')) continue;
        for (auto& c : key) c = (char)toupper((unsigned char)c);
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
