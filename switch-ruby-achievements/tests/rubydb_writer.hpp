#pragma once
// Pacotes .rubydb para os testes do PC: grava a partir do JSON antigo o mesmo que
// tools/json_to_rubydb.py (esquema lido de rubydb_schema.sql), para os casos continuarem
// escritos em JSON. Ícones e som entram como bytes.
#include <stdio.h>

#include <map>
#include <string>

#include "mjson.h"
#include "sqlite3.h"

namespace rubydb {

inline void dump(const JVal& v, std::string& out) {
    char num[64];
    switch (v.type) {
        case JVal::Null: out += "null"; break;
        case JVal::Bool: out += v.b ? "true" : "false"; break;
        case JVal::Num:
            if (v.num == (double)(long long)v.num) snprintf(num, sizeof(num), "%lld", (long long)v.num);
            else snprintf(num, sizeof(num), "%.17g", v.num);
            out += num;
            break;
        case JVal::Str:
            out += '"';
            for (char c : v.str) {
                if (c == '"' || c == '\\') out += '\\';
                if (c == '\n') out += "\\n";
                else out += c;
            }
            out += '"';
            break;
        case JVal::Arr:
            out += '[';
            for (size_t i = 0; i < v.arr.size(); i++) {
                if (i) out += ',';
                dump(v.arr[i], out);
            }
            out += ']';
            break;
        case JVal::Obj:
            out += '{';
            for (size_t i = 0; i < v.obj.size(); i++) {
                if (i) out += ',';
                dump(JVal{JVal::Str, false, 0, v.obj[i].first, {}, {}}, out);
                out += ':';
                dump(v.obj[i].second, out);
            }
            out += '}';
            break;
    }
}

inline std::string schema() {
    FILE* f = fopen(PT_REPO "/rubydb_schema.sql", "rb");
    std::string s;
    char buf[4096];
    size_t n;
    while (f && (n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    if (f) fclose(f);
    return s;
}

// `path` = caminho do .rubydb; `icons`/`locked` = id -> PNG.
inline bool write(const std::string& path, const std::string& json, const std::map<std::string, std::string>& icons = {},
                  const std::map<std::string, std::string>& locked = {}) {
    JVal root;
    if (!json_parse(json.data(), json.size(), root) || !root.get("achievements")) return false;
    remove(path.c_str());
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    bool ok = sqlite3_exec(db, schema().c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    std::string stem = path.substr(path.rfind('/') + 1);
    stem = stem.substr(0, stem.size() - 7);
    std::string tid = root.get_str("title_id");
    if (tid.empty() && stem.size() >= 16 && stem.find_first_not_of("0123456789ABCDEFabcdef") >= 16) tid = stem.substr(0, 16);
    std::string title = root.get_str("pack_name");
    if (title.empty()) title = root.get_str("pack_variant");
    if (title.empty()) title = root.get_str("game_name");
    if (title.empty()) title = stem;
    sqlite3_stmt* st = nullptr;
    ok = ok && sqlite3_prepare_v2(db, "INSERT INTO pack_info VALUES(?1,?2,'teste',NULL,?3,?4,?5,'1.0.0')", -1, &st,
                                  nullptr) == SQLITE_OK;
    if (ok) {
        std::string build = root.get_str("build_id");
        sqlite3_bind_text(st, 1, stem.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, tid.empty() ? "emulator" : "native", -1, SQLITE_STATIC);
        if (tid.empty()) sqlite3_bind_null(st, 4);
        else sqlite3_bind_text(st, 4, tid.c_str(), -1, SQLITE_TRANSIENT);
        if (build.empty()) sqlite3_bind_null(st, 5);
        else sqlite3_bind_text(st, 5, build.c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    sqlite3_finalize(st);
    st = nullptr;
    ok = ok && sqlite3_prepare_v2(db,
                                  "INSERT INTO achievements(id,sort_order,title,description,rarity,secret,"
                                  "condition_json,icon_unlocked,icon_locked) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)",
                                  -1, &st, nullptr) == SQLITE_OK;
    const JVal* list = root.get("achievements");
    for (size_t i = 0; ok && i < list->arr.size(); i++) {
        const JVal& a = list->arr[i];
        std::string aid = a.get_str("id"), rarity = a.get_str("rarity", "bronze");
        JVal cond;
        cond.type = JVal::Obj;
        if (rarity != "ruby")
            for (auto& kv : a.obj)
                if (kv.first != "id" && kv.first != "title" && kv.first != "description" && kv.first != "rarity" &&
                    kv.first != "secret" && kv.first != "icon" &&
                    kv.first != "icon_locked" && kv.first != "points")
                    cond.obj.push_back(kv);
        std::string cond_text, title_a = a.get_str("title", aid.c_str());
        dump(cond, cond_text);
        const JVal* secret = a.get("secret");
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        sqlite3_bind_text(st, 1, aid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, (int)i);
        sqlite3_bind_text(st, 3, title_a.c_str(), -1, SQLITE_TRANSIENT);
        std::string desc = a.get_str("description");
        sqlite3_bind_text(st, 4, desc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, rarity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 6, secret && secret->type == JVal::Bool && secret->b ? 1 : 0);
        sqlite3_bind_text(st, 7, cond_text.c_str(), -1, SQLITE_TRANSIENT);
        auto it = icons.find(aid);
        if (it != icons.end()) sqlite3_bind_blob(st, 8, it->second.data(), (int)it->second.size(), SQLITE_TRANSIENT);
        auto lk = locked.find(aid);
        if (lk != locked.end()) sqlite3_bind_blob(st, 9, lk->second.data(), (int)lk->second.size(), SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return ok;
}

// Lê um arquivo inteiro ("" se não existe).
inline std::string slurp(const char* path) {
    std::string s;
    FILE* f = fopen(path, "rb");
    char buf[4096];
    size_t n;
    while (f && (n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    if (f) fclose(f);
    return s;
}

// Troca o "<nome>.json" que o teste acabou de gravar por "<nome>.rubydb" (apaga o .json).
inline bool from_json_file(const std::string& json_path) {
    std::string out = json_path.substr(0, json_path.size() - 5) + ".rubydb";
    bool ok = write(out, slurp(json_path.c_str()));
    remove(json_path.c_str());
    if (!ok) printf("rubydb::from_json_file(%s) falhou\n", json_path.c_str());
    return ok;
}

// Copia um arquivo (pacotes .rubydb do repositório, gravados por tools/json_to_rubydb.py).
inline bool copy(const char* from, const char* to) {
    std::string data = slurp(from);
    FILE* f = data.empty() ? nullptr : fopen(to, "wb");
    bool ok = f && fwrite(data.data(), 1, data.size(), f) == data.size();
    if (f) fclose(f);
    if (!ok) printf("rubydb::copy(%s) falhou\n", from);
    return ok;
}

}  // namespace rubydb
