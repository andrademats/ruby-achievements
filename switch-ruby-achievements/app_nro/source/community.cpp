#include "community.hpp"

#include "http.hpp"
#include "i18n.hpp"
#include "pt_lock.h"
#include "pt_pack.h"
#include "pt_paths.h"
#include "pt_util.h"
#include "sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace community {

// Catálogo da comunidade (repositório andrademats/ruby-achievements-packs, ramo main).
const char* const kIndexUrl = "https://raw.githubusercontent.com/andrademats/ruby-achievements-packs/main/index.json";

namespace {

using nlohmann::json;

constexpr int kLockTimeoutMs = 5000;

bool is_hex(const std::string& s, size_t min_len, size_t max_len) {
    if (s.size() < min_len || s.size() > max_len) return false;
    return s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

std::string upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool valid_pack_id(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

// Texto do catálogo: string (outros tipos = ""), cortada em `max` bytes sem partir UTF-8.
std::string text(const json& obj, const char* key, size_t max) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return "";
    std::string s = it->get<std::string>();
    if (s.size() > max) {
        size_t n = max;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        s.resize(n);
    }
    return s;
}

bool parse_entry(const json& p, Entry& e) {
    if (!p.is_object()) return false;
    e.pack_id = text(p, "pack_id", 64);
    e.title = text(p, "title", 128);
    e.author = text(p, "author", 64);
    e.description = text(p, "description", 1024);
    e.version = text(p, "version", 32);
    e.url = text(p, "download_url", 1024);
    e.sha256 = lower(text(p, "sha256", 64));
    if (!valid_pack_id(e.pack_id) || e.title.empty() || e.url.rfind("https://", 0) != 0 || !is_hex(e.sha256, 64, 64))
        return false;
    auto total = p.find("total_achievements");
    if (total != p.end()) {
        if (!total->is_number_integer() || total->get<long long>() < 0 || total->get<long long>() > 100000)
            return false;
        e.total = (int)total->get<long long>();
    }

    auto t = p.find("target");
    if (t == p.end() || !t->is_object()) return false;
    std::string type = text(*t, "type", 16);
    e.target.version_min = text(*t, "game_version_min", 32);
    if (type == "native") {
        e.target.type = TargetType::Native;
        e.target.title_id = upper(text(*t, "title_id", 16));
        if (!is_hex(e.target.title_id, 16, 16)) return false;
        auto builds = t->find("supported_build_ids");
        if (builds != t->end()) {
            if (!builds->is_array()) return false;
            for (const json& b : *builds) {
                if (!b.is_string() || !is_hex(b.get<std::string>(), 1, 64)) return false;
                e.target.build_ids.push_back(upper(b.get<std::string>()));
            }
        }
    } else if (type == "port") {
        e.target.type = TargetType::Port;
        e.target.port_slug = lower(text(*t, "port_slug", 64));
        if (e.target.port_slug.empty()) e.target.port_slug = lower(text(*t, "unique_id", 64));
        if (e.target.port_slug.empty()) return false;
    } else if (type == "rom") {
        e.target.type = TargetType::Rom;
        e.target.platform = upper(text(*t, "platform_code", 16));
        e.target.rom_hash = lower(text(*t, "rom_hash", 40));
        size_t n = e.target.rom_hash.size();
        if (e.target.platform.empty() || !(n == 32 || n == 40) || !is_hex(e.target.rom_hash, n, n)) return false;
        e.target.version_min.clear();  // o hash já fixa a ROM
    } else {
        return false;
    }
    return true;
}

bool matches(const Entry& e, const Game& g) {
    if (e.target.type != g.type) return false;
    switch (e.target.type) {
        case TargetType::Native:
            return strcasecmp(e.target.title_id.c_str(), g.title_id.c_str()) == 0;
        case TargetType::Port:
            return !g.port_slug.empty() && e.target.port_slug == lower(g.port_slug);
        case TargetType::Rom:
            return e.target.platform == upper(g.platform) &&
                   (e.target.rom_hash == lower(g.rom_md5) || e.target.rom_hash == lower(g.rom_sha1));
    }
    return false;
}

bool read_text(const char* path, size_t max, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[16 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() <= max) out.append(buf, n);
    fclose(f);
    return out.size() <= max;
}

std::string ach_path(const std::string& file) { return std::string(PT_ACH_DIR) + "/" + file; }

}  // namespace

bool parse_index(const std::string& text_json, Index& out, std::string& error) {
    out = {};
    json root = json::parse(text_json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = tr("O catálogo de pacotes não é um JSON válido.");
        return false;
    }
    auto packs = root.find("packs");
    if (packs == root.end() || !packs->is_array()) {
        error = tr("O catálogo de pacotes não tem a lista \"packs\".");
        return false;
    }
    out.updated_at = text(root, "updated_at", 32);
    for (const json& p : *packs) {
        Entry e;
        if (parse_entry(p, e)) out.packs.push_back(std::move(e));
        else out.skipped++;
    }
    return true;
}

bool compare_versions(const std::string& a, const std::string& b, int& result) {
    auto parts = [](const std::string& v, std::vector<long>& out) {
        out.clear();
        if (v.empty()) return false;
        size_t at = 0;
        while (at <= v.size()) {
            size_t dot = v.find('.', at);
            if (dot == std::string::npos) dot = v.size();
            std::string p = v.substr(at, dot - at);
            if (p.empty() || p.size() > 9 || p.find_first_not_of("0123456789") != std::string::npos) return false;
            out.push_back(strtol(p.c_str(), nullptr, 10));
            at = dot + 1;
        }
        return true;
    };
    std::vector<long> x, y;
    if (!parts(a, x) || !parts(b, y)) return false;
    size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    result = x < y ? -1 : x > y ? 1 : 0;
    return true;
}

Compat compatibility(const Entry& e, const Game& g) {
    const Target& t = e.target;
    if (t.type == TargetType::Rom) return Compat::Ok;  // hash da ROM idêntico
    int cmp = 0;
    bool version_known = !t.version_min.empty() && compare_versions(g.version, t.version_min, cmp);
    if (version_known && cmp < 0) return Compat::Incompatible;
    if (t.type == TargetType::Port) return t.version_min.empty() || version_known ? Compat::Ok : Compat::Warning;
    if (t.build_ids.empty()) return Compat::Ok;
    for (const std::string& b : t.build_ids)
        if (!g.build_id.empty() && strncasecmp(g.build_id.c_str(), b.c_str(), b.size()) == 0) return Compat::Ok;
    return Compat::Warning;
}

std::vector<Match> filter(const Index& index, const Game& game) {
    std::vector<Match> out;
    for (const Entry& e : index.packs)
        if (matches(e, game)) out.push_back({e, compatibility(e, game)});
    std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) {
        if (a.compat != b.compat) return a.compat < b.compat;
        return strcasecmp(a.entry.title.c_str(), b.entry.title.c_str()) < 0;
    });
    return out;
}

std::string file_name(const std::string& title_key, const std::string& pack_id) {
    return upper(title_key) + "." + pack_id + kPtPackExt;
}

bool load_index(Index& out, bool& from_cache, std::string& error) {
    from_cache = false;
    error.clear();
    http::Response r;
    std::string net_error;
    if (!http::get(kIndexUrl, {}, r, net_error, kMaxIndexBytes)) {
        error = net_error;
    } else if (r.status != 200) {
        error = trf("O servidor respondeu HTTP %d.", (int)r.status);
    } else {
        std::string body(r.body.begin(), r.body.end());
        if (parse_index(body, out, error)) {
            mkdir(PT_ACH_CACHE_DIR, 0777);
            pt_write_file_atomic(PT_COMMUNITY_INDEX_PATH, body.data(), body.size());  // cópia para usar sem rede
            return true;
        }
    }
    std::string cached, cache_error;
    if (read_text(PT_COMMUNITY_INDEX_PATH, kMaxIndexBytes, cached) && parse_index(cached, out, cache_error)) {
        from_cache = true;
        return true;
    }
    return false;
}

bool install(const Entry& e, const std::string& title_key, std::string& error) {
    if (!pt_pack_is_title_key(title_key) || !valid_pack_id(e.pack_id)) {
        error = tr("Pacote ou jogo inválido.");
        return false;
    }
    const std::string final_file = file_name(title_key, e.pack_id);
    const std::string part_file = final_file + ".part";  // fora de pt_pack_files até conferir
    const std::string part = ach_path(part_file);
    mkdir(PT_DIR, 0777);
    mkdir(PT_ACH_DIR, 0777);
    unlink(part.c_str());
    std::string net_error;
    if (!http::download(e.url, {}, part, kMaxPackBytes, net_error)) {
        error = trf("Download falhou: %s", net_error.c_str());
        return false;
    }
    auto fail = [&](const std::string& why) {
        unlink(part.c_str());
        error = why;
        return false;
    };
    if (sha256_file(part) != e.sha256)
        return fail(tr("O arquivo baixado não confere com o catálogo (SHA-256): pacote recusado."));
    JVal root;
    std::string why;
    if (!pt_pack_load(part_file, root, &why)) return fail(trf("Pacote inválido: %s", why.c_str()));
    std::string tid = root.get_str("title_id");
    if (!tid.empty() && strcasecmp(tid.c_str(), title_key.c_str()) != 0)
        return fail(trf("O pacote é de outro jogo (%s).", tid.c_str()));

    // Com o lock do banco o sysmodule não está lendo pacotes: troca o arquivo de uma vez.
    if (!pt_lock_acquire(kLockTimeoutMs)) return fail(tr("Banco ocupado. Tente de novo."));
    const std::string dest = ach_path(final_file);
    unlink(dest.c_str());  // atualização do mesmo pack_id (o FS do Switch não sobrescreve no rename)
    bool ok = rename(part.c_str(), dest.c_str()) == 0;
    pt_lock_release();
    if (!ok) return fail(trf("Não foi possível gravar %s.", dest.c_str()));
    return true;
}

}  // namespace community
