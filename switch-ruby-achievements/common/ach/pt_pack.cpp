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
        if (e->d_type != DT_DIR && ends_with(e->d_name, ".json")) out.push_back(e->d_name);
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
    std::string main = key + ".json";
    if (!pt_pack_is_title_key(key)) {
        if (file_exists(main)) out.push_back(main);
        return out;
    }
    std::string found_main;  // o nome como está no SD (o Title ID pode estar em minúsculas)
    for (auto& f : list_dir()) {
        if (f.size() < key.size() || strncasecmp(f.c_str(), key.c_str(), key.size()) != 0) continue;
        std::string rest = f.substr(key.size());
        if (rest == ".json") found_main = f;
        else if (rest.size() > 6 && rest[0] == '.') out.push_back(f);  // ".<variante>.json"
    }
    if (!found_main.empty()) out.insert(out.begin(), found_main);
    return out;
}

std::string pt_pack_variant(const std::string& key, const std::string& file) {
    if (file.size() <= key.size() + 6) return "";
    return file.substr(key.size() + 1, file.size() - key.size() - 1 - 5);
}

std::string pt_pack_active(const std::string& key) {
    std::vector<std::string> files = pt_pack_files(key);
    if (files.empty()) return "";
    for (auto& c : read_choices())
        if (c.first == key && std::find(files.begin(), files.end(), c.second) != files.end()) return c.second;
    return files[0];
}

bool pt_pack_load(const std::string& file, JVal& root, std::string* err) {
    std::string path = std::string(PT_ACH_DIR) + "/" + file;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "arquivo não encontrado";
        return false;
    }
    std::string text;
    char buf[2048];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < kPtPackMaxFile) text.append(buf, n);
    fclose(f);
    std::string why;
    if (json_parse(text.data(), text.size(), root, &why)) {
        const JVal* list = root.get("achievements");
        if (list && list->type == JVal::Arr) return true;
        why = "sem \"achievements\"";
    }
    if (err) *err = why;
    return false;
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
        // "icon": PNG relativo a PT_ACH_DIR ("icons/<tid>/<id>.png"); nada de sair da pasta.
        std::string icon = a.get_str("icon");
        bool icon_ok = !icon.empty() && icon.find("..") == std::string::npos && icon[0] != '/' &&
                       icon.find(':') == std::string::npos;
        std::string icon_path = std::string(PT_ACH_DIR) + "/" + icon;
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
        if (f.size() < 16 + 5) continue;
        std::string key = f.substr(0, 16);
        if (!pt_pack_is_title_key(key) || (f[16] != '.')) continue;
        for (auto& c : key) c = (char)toupper((unsigned char)c);
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
