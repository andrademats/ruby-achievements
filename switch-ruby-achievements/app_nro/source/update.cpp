#include "update.hpp"

#include "http.hpp"
#include "i18n.hpp"
#include "monocypher-ed25519.h"
#include "pt_lock.h"
#include "pt_paths.h"
#include "pt_update.h"

#include <nlohmann/json.hpp>
#include <switch.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace upd {

// Repositório público das releases (vazio = a aba App avisa que não está configurada).
const char* const kRepo = "andrademats/ruby-achievements";

namespace {

using nlohmann::json;

const uint8_t kPublicKey[32] =
#include "update_key.inc"
    ;

constexpr uint64_t kMaxFileSize = 64ull << 20;  // binário mais longo aceito no manifesto
const std::vector<std::string> kGithubHeaders = {"Accept: application/vnd.github+json",
                                                 "X-GitHub-Api-Version: 2022-11-28"};

bool is_hex(const std::string& s, size_t len) {
    if (s.size() != len) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const json* field(const json& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

std::string str_field(const json& obj, const char* key) {
    const json* v = field(obj, key);
    return v && v->is_string() ? v->get<std::string>() : std::string();
}

const ManifestFile* file_for(const Manifest& m, int target) {
    for (const ManifestFile& f : m.files)
        if (f.target == target) return &f;
    return nullptr;
}

bool fetch_text(const std::string& url, size_t max, std::string& out, std::string& error) {
    http::Response r;
    if (!http::get(url, {}, r, error, max)) return false;
    if (r.status != 200) {
        error = trf("O servidor respondeu HTTP %d.", (int)r.status);
        return false;
    }
    out.assign(r.body.begin(), r.body.end());
    return true;
}

uint64_t file_size(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

bool free_space_ok(uint64_t need, std::string& error) {
#ifdef __SWITCH__
    FsFileSystem* fs = fsdevGetDeviceFileSystem("sdmc");
    s64 free_bytes = 0;
    if (fs && R_SUCCEEDED(fsFsGetFreeSpace(fs, "/", &free_bytes)) && (uint64_t)free_bytes < need) {
        error = trf("Espaço livre insuficiente no cartão SD: faltam %d MB.",
                    (int)((need - (uint64_t)free_bytes + (1 << 20) - 1) >> 20));
        return false;
    }
#else
    (void)need;
    (void)error;
#endif
    return true;
}

}  // namespace

bool parse_version(const std::string& text, Version& out) {
    std::string s = trim(text);
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    int parts[3] = {0, 0, 0}, n = 0;
    size_t i = 0;
    while (n < 3) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
        long v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i++] - '0');
            if (v > 99999) return false;
        }
        parts[n++] = (int)v;
        if (n < 3) {
            if (i >= s.size() || s[i] != '.') return false;
            i++;
        }
    }
    if (i != s.size()) return false;
    out = {parts[0], parts[1], parts[2]};
    return true;
}

int compare(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::string to_string(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

Version current() {
    Version v;
    parse_version(PT_VERSION, v);
    return v;
}

const uint8_t* release_public_key() { return kPublicKey; }

bool parse_manifest(const std::string& text, Manifest& out, std::string& error) {
    out = {};
    json j = json::parse(text, nullptr, false);
    auto bad = [&error](const std::string& why) {
        error = std::string(tr("Manifesto inválido: atualização recusada.")) + " (" + why + ")";
        return false;
    };
    if (j.is_discarded() || !j.is_object()) return bad("JSON");
    if (!parse_version(str_field(j, "version"), out.version)) return bad("version");
    std::string min = str_field(j, "min_version");
    if (min.empty()) min = "0.0.0";
    if (!parse_version(min, out.min_version)) return bad("min_version");
    out.notes = str_field(j, "notes");
    const json* files = field(j, "files");
    if (!files || !files->is_array() || files->empty()) return bad("files");
    for (const json& f : *files) {
        if (!f.is_object()) return bad("files");
        ManifestFile mf;
        mf.path = str_field(f, "path");
        mf.target = pt_update_target_find(mf.path.c_str());
        if (mf.target < 0) return bad("destino fora da lista: " + mf.path);
        if (file_for(out, mf.target)) return bad("destino repetido: " + mf.path);
        const json* size = field(f, "size");
        if (!size || !size->is_number_unsigned()) return bad("size");
        mf.size = size->get<uint64_t>();
        if (mf.size == 0 || mf.size > kMaxFileSize) return bad("size");
        mf.sha256 = str_field(f, "sha256");
        if (!is_hex(mf.sha256, 64)) return bad("sha256");
        out.files.push_back(mf);
    }
    return true;
}

bool verify_signature(const std::string& manifest, const std::string& sig_hex, const uint8_t public_key[32]) {
    std::string hex = trim(sig_hex);
    if (!is_hex(hex, 128)) return false;
    uint8_t sig[64];
    for (int i = 0; i < 64; i++) sig[i] = (uint8_t)std::stoi(hex.substr(i * 2, 2), nullptr, 16);
    return crypto_ed25519_check(sig, public_key, (const uint8_t*)manifest.data(), manifest.size()) == 0;
}

bool parse_release(const std::string& text, Release& out, std::string& error) {
    out = {};
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = tr("Resposta inválida do GitHub.");
        return false;
    }
    out.tag = str_field(j, "tag_name");
    const json* assets = field(j, "assets");
    if (assets && assets->is_array())
        for (const json& a : *assets) {
            if (!a.is_object()) continue;
            Asset as;
            as.name = str_field(a, "name");
            as.url = str_field(a, "browser_download_url");
            const json* size = field(a, "size");
            if (size && size->is_number_unsigned()) as.size = size->get<uint64_t>();
            if (!as.name.empty() && as.url.rfind("https://", 0) == 0) out.assets.push_back(as);
        }
    return true;
}

const Asset* find_asset(const Release& r, const std::string& name) {
    for (const Asset& a : r.assets)
        if (a.name == name) return &a;
    return nullptr;
}

std::string sha256_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    std::vector<unsigned char> buf(64 * 1024);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) sha256ContextUpdate(&ctx, buf.data(), n);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) return "";
    uint8_t hash[32];
    sha256ContextGetHash(&ctx, hash);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (uint8_t b : hash) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

bool check(Check& out, std::string& error) {
    out = {};
    error.clear();
    if (!kRepo[0]) {
        error = tr("A atualização automática ainda não foi configurada nesta versão.");
        return false;
    }
    http::Response r;
    if (!http::get(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest", kGithubHeaders, r,
                   error, 2u << 20))
        return false;
    if (r.status == 404) {
        error = tr("Nenhuma versão publicada ainda.");
        return false;
    }
    if (r.status == 403 || r.status == 429) {
        error = tr("O GitHub limitou as consultas. Tente de novo em uma hora.");
        return false;
    }
    if (r.status != 200) {
        error = trf("O GitHub respondeu HTTP %d.", (int)r.status);
        return false;
    }
    if (!parse_release(std::string(r.body.begin(), r.body.end()), out.release, error)) return false;

    const Asset* man = find_asset(out.release, "manifest.json");
    const Asset* sig = find_asset(out.release, "manifest.sig");
    if (!man || !sig) {
        error = tr("A versão publicada não tem manifesto assinado: atualização recusada.");
        return false;
    }
    std::string mtext, stext;
    if (!fetch_text(man->url, 64u << 10, mtext, error) || !fetch_text(sig->url, 1024, stext, error)) return false;
    if (!verify_signature(mtext, stext, kPublicKey)) {
        error = tr("A assinatura do manifesto não confere: atualização recusada.");
        return false;
    }
    if (!parse_manifest(mtext, out.manifest, error)) return false;

    Version cur = current();
    out.newer = compare(out.manifest.version, cur) > 0;
    out.too_old = compare(cur, out.manifest.min_version) < 0;
    for (const ManifestFile& f : out.manifest.files)
        if (pt_update_target_installed(f.target)) out.targets.push_back(f.target);
    return true;
}

bool verify_staged(const Manifest& m, const std::vector<int>& targets, std::string& error) {
    char staged[256];
    for (int t : targets) {
        const ManifestFile* f = file_for(m, t);
        if (!f) {
            error = tr("Manifesto inválido: atualização recusada.");
            return false;
        }
        pt_update_staged_path(t, staged, sizeof(staged));
        if (file_size(staged) != f->size || sha256_file(staged) != f->sha256) {
            error = trf("%s chegou diferente do manifesto (download corrompido ou adulterado): atualização recusada.",
                        pt_update_target_name(t));
            return false;
        }
    }
    return true;
}

bool backup_database(std::string& error) {
    struct stat st;
    if (stat(PT_DB_PATH, &st) != 0) return true;  // sem banco ainda: nada a guardar
    if (!pt_lock_acquire(10000)) {
        error = tr("O banco de dados está ocupado. Tente de novo em alguns segundos.");
        return false;
    }
    const char* tmp = PT_UPDATE_DB_BACKUP_PATH ".tmp";
    FILE* in = fopen(PT_DB_PATH, "rb");
    FILE* out = in ? fopen(tmp, "wb") : nullptr;
    bool ok = in && out;
    std::vector<unsigned char> buf(64 * 1024);
    size_t n;
    while (ok && (n = fread(buf.data(), 1, buf.size(), in)) > 0) ok = fwrite(buf.data(), 1, n, out) == n;
    if (in) ok = !ferror(in) && ok;
    if (in) fclose(in);
    if (out) ok = fclose(out) == 0 && ok;
    pt_lock_release();
    if (ok) {
        unlink(PT_UPDATE_DB_BACKUP_PATH);
        ok = rename(tmp, PT_UPDATE_DB_BACKUP_PATH) == 0;
    }
    if (!ok) {
        unlink(tmp);
        error = tr("Não foi possível copiar o banco de dados para o backup: atualização cancelada.");
    }
    return ok;
}

std::string blocked_reason(const Check& c) {
    if (!c.newer) return tr("Você já está na versão mais recente.");
    if (c.too_old)
        return trf("Esta versão precisa da %s ou mais nova instalada. Atualize copiando o pacote completo para o "
                   "cartão SD.",
                   to_string(c.manifest.min_version).c_str());
    if (c.targets.empty()) return tr("Nenhum dos arquivos desta versão está instalado neste cartão SD.");
    return "";
}

bool install(const Check& c, const Progress& progress, std::string& error) {
    error = blocked_reason(c);
    if (!error.empty()) return false;
    uint64_t need = file_size(PT_DB_PATH) + (1u << 20);
    for (int t : c.targets) need += file_for(c.manifest, t)->size;
    if (!free_space_ok(need, error)) return false;
    if (pt_update_prepare_staging() != 0) {
        error = trf("Não foi possível criar %s.", PT_UPDATE_STAGING_DIR);
        return false;
    }

    char staged[256];
    int n = (int)c.targets.size();
    for (int k = 0; k < n; k++) {
        int t = c.targets[k];
        const char* name = pt_update_target_name(t);
        const Asset* a = find_asset(c.release, name);
        if (!a) {
            error = trf("A versão publicada não traz %s: atualização recusada.", name);
            return false;
        }
        progress(trf("Baixando %s (%d/%d)", name, k + 1, n));
        pt_update_staged_path(t, staged, sizeof(staged));
        if (!http::download(a->url, {}, staged, file_for(c.manifest, t)->size, error)) return false;
    }
    progress(tr("Conferindo os arquivos..."));
    if (!verify_staged(c.manifest, c.targets, error)) return false;
    progress(tr("Copiando o banco de dados..."));
    if (!backup_database(error)) return false;
    progress(tr("Instalando..."));
    char err[256];
    if (pt_update_apply(to_string(c.manifest.version).c_str(), c.targets.data(), n, err, sizeof(err)) != 0) {
        error = trf("A troca dos arquivos falhou e foi desfeita: %s", err);
        return false;
    }
    return true;
}

}  // namespace upd
