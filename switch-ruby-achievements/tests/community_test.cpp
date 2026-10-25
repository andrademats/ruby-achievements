// Pacotes da comunidade (app_nro/source/community.cpp): leitura do index.json, filtro por
// title_id, build_id e hash de ROM, selos de compatibilidade, cópia sem rede e instalação
// com SHA-256 e validação do .rubydb (HTTP falso: http_fake.cpp).
#include <switch.h>

#include <sys/stat.h>
#include <unistd.h>

#include <map>

#include "community.hpp"
#include "pt_pack.h"
#include "pt_paths.h"
#include "rubydb_writer.hpp"
#include "sha256.hpp"

extern std::map<std::string, std::string> g_http;
s64 g_fake_now = 1000;

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-72s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

static std::string slurp(const std::string& path) {
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }

// Entrada do catálogo com os campos obrigatórios; `target` e `extra` em JSON.
static std::string entry(const char* id, const char* title, const char* target, const char* sha = nullptr,
                         const char* extra = "") {
    std::string s = std::string("{\"pack_id\":\"") + id + "\",\"title\":\"" + title +
                    "\",\"author\":\"Autor\",\"version\":\"1.0.0\",\"total_achievements\":3,\"target\":" + target +
                    ",\"download_url\":\"https://exemplo.invalid/" + id + ".rubydb\",\"sha256\":\"" +
                    (sha ? sha : "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") + "\"" + extra + "}";
    return s;
}

static const char* kNative =
    "{\"type\":\"native\",\"title_id\":\"0100aaaa00000000\",\"supported_build_ids\":[\"2e819f30c4110a5b\"],"
    "\"game_version_min\":\"1.6.0\"}";

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (const char* d : {"sdmc:", "sdmc:/switch", PT_DIR, PT_ACH_DIR}) mkdir(d, 0777);
    using namespace community;

    // --- versões
    int cmp = 9;
    check("versões: 1.6.0 < 1.10", compare_versions("1.6.0", "1.10", cmp) && cmp < 0);
    check("versões: 1.6 == 1.6.0", compare_versions("1.6", "1.6.0", cmp) && cmp == 0);
    check("versões: 2 > 1.99.99", compare_versions("2", "1.99.99", cmp) && cmp > 0);
    check("versões: não numérica = desconhecida", !compare_versions("1.6b", "1.6", cmp) && !compare_versions("", "1", cmp));

    // --- catálogo
    std::string json = std::string("{\"updated_at\":\"2026-10-24T20:00:00Z\",\"packs\":[") +
                       entry("botw_hard", "Desafios", kNative) + "," +
                       entry("botw_livre", "Livre", "{\"type\":\"native\",\"title_id\":\"0100AAAA00000000\"}") + "," +
                       entry("botw_novo", "Versão nova",
                             "{\"type\":\"native\",\"title_id\":\"0100AAAA00000000\",\"game_version_min\":\"1.7\"}") + "," +
                       entry("outro_jogo", "Outro", "{\"type\":\"native\",\"title_id\":\"0100BBBB00000000\"}") + "," +
                       entry("soh_extra", "SoH", "{\"type\":\"port\",\"port_slug\":\"SOH_OOT\",\"game_version_min\":\"8.0\"}") + "," +
                       entry("mario_gba", "Mario GBA",
                             "{\"type\":\"rom\",\"platform_code\":\"gba\",\"rom_hash\":\"0123456789ABCDEF0123456789ABCDEF\"}") + "," +
                       entry("Maiuscula", "id inválido", kNative) + "," +
                       entry("sem_https", "x", kNative, nullptr, ",\"download_url\":\"http://x\"") + "," +
                       entry("sha_curto", "x", kNative, "abc") + "," +
                       entry("build_ruim", "x", "{\"type\":\"native\",\"title_id\":\"0100AAAA00000000\",\"supported_build_ids\":[\"xyz\"]}") + "," +
                       entry("tid_ruim", "x", "{\"type\":\"native\",\"title_id\":\"123\"}") + "," +
                       entry("tipo_ruim", "x", "{\"type\":\"nds\"}") + "," +
                       entry("hash_ruim", "x", "{\"type\":\"rom\",\"platform_code\":\"GBA\",\"rom_hash\":\"abcd\"}") +
                       ",42]}";
    Index idx;
    std::string err;
    check("catálogo lido", parse_index(json, idx, err));
    check("6 entradas válidas, 8 puladas (id, https, sha, build, title_id, tipo, hash, não-objeto)",
          idx.packs.size() == 6 && idx.skipped == 8);
    check("updated_at lido", idx.updated_at == "2026-10-24T20:00:00Z");
    const Entry& hard = idx.packs[0];
    check("normalização: title_id e build ids maiúsculos, slug e hash minúsculos, plataforma maiúscula",
          hard.target.title_id == "0100AAAA00000000" && hard.target.build_ids == std::vector<std::string>{"2E819F30C4110A5B"} &&
              idx.packs[4].target.port_slug == "soh_oot" && idx.packs[5].target.platform == "GBA" &&
              idx.packs[5].target.rom_hash == "0123456789abcdef0123456789abcdef");
    check("campos do item (autor, versão, conquistas)", hard.author == "Autor" && hard.version == "1.0.0" && hard.total == 3);
    check("JSON inválido recusado", !parse_index("{", idx, err) && !parse_index("{\"packs\":{}}", idx, err));
    parse_index(json, idx, err);

    // --- filtro: jogo nativo
    Game g;
    g.type = TargetType::Native;
    g.title_id = "0100aaaa00000000";
    g.version = "1.6.0";
    g.build_id = "2E819F30C4110A5B" + std::string(48, '0');
    auto m = filter(idx, g);
    check("nativo: só os 3 pacotes do title_id", m.size() == 3);
    check("build e versão batem: Desafios compatível (verde)",
          m.size() == 3 && m[0].entry.pack_id == "botw_hard" && m[0].compat == Compat::Ok);
    check("sem supported_build_ids: compatível", m.size() == 3 && m[1].entry.pack_id == "botw_livre" && m[1].compat == Compat::Ok);
    check("versão abaixo do mínimo: incompatível (vermelho) e por último",
          m.size() == 3 && m[2].entry.pack_id == "botw_novo" && m[2].compat == Compat::Incompatible);
    g.build_id = "FA123B90CD41102A" + std::string(48, '0');
    check("build diferente: aviso (amarelo)", compatibility(idx.packs[0], g) == Compat::Warning);
    g.build_id.clear();
    check("build ainda não visto pelo sysmodule: aviso", compatibility(idx.packs[0], g) == Compat::Warning);
    g.build_id = "2e819f30c4110a5b" + std::string(48, '0');
    g.version = "1.5.0";
    check("build bate mas versão abaixo do mínimo: incompatível", compatibility(idx.packs[0], g) == Compat::Incompatible);
    g.version = "";
    check("versão desconhecida: decide pelo build", compatibility(idx.packs[0], g) == Compat::Ok);
    g.title_id = "0100CCCC00000000";
    check("title_id sem pacote: lista vazia", filter(idx, g).empty());

    // --- filtro: port e ROM
    Game port;
    port.type = TargetType::Port;
    port.port_slug = "soh_oot";
    port.version = "8.0.4";
    m = filter(idx, port);
    check("port: pelo slug, versão acima do mínimo", m.size() == 1 && m[0].compat == Compat::Ok);
    port.version = "7.9";
    check("port: versão abaixo do mínimo", compatibility(idx.packs[4], port) == Compat::Incompatible);
    port.version = "";
    check("port: versão desconhecida com mínimo = aviso", compatibility(idx.packs[4], port) == Compat::Warning);
    Game rom;
    rom.type = TargetType::Rom;
    rom.platform = "gba";
    rom.rom_md5 = "0123456789ABCDEF0123456789ABCDEF";
    check("ROM: plataforma + MD5 batem", filter(idx, rom).size() == 1 && filter(idx, rom)[0].compat == Compat::Ok);
    rom.platform = "SNES";
    check("ROM: mesmo hash em outra plataforma não aparece", filter(idx, rom).empty());
    rom.platform = "GBA";
    rom.rom_md5 = "ffffffffffffffffffffffffffffffff";
    check("ROM: hash diferente não aparece", filter(idx, rom).empty());
    Game native_same_tid = g;
    native_same_tid.type = TargetType::Rom;
    check("tipo do alvo precisa bater", filter(idx, native_same_tid).empty());

    // --- catálogo baixado e cópia sem rede
    bool cached = true;
    g_http[kIndexUrl] = json;
    Index got;
    check("catálogo baixado", load_index(got, cached, err) && !cached && got.packs.size() == 6);
    check("cópia salva em cache/index.json", slurp(PT_COMMUNITY_INDEX_PATH) == json);
    g_http.erase(kIndexUrl);
    check("sem rede: usa a cópia salva", load_index(got, cached, err) && cached && got.packs.size() == 6 && !err.empty());
    g_http[kIndexUrl] = "<html>erro</html>";
    check("resposta inválida: cópia salva continua valendo", load_index(got, cached, err) && cached &&
                                                              slurp(PT_COMMUNITY_INDEX_PATH) == json);
    unlink(PT_COMMUNITY_INDEX_PATH);
    check("sem rede e sem cópia: falha", !load_index(got, cached, err));

    // --- instalação
    const std::string key = "0100AAAA00000000";
    const std::string fixture = std::string(PT_DIR) + "/fixture.rubydb";
    rubydb::write(fixture, "{\"pack_name\":\"Desafios\",\"title_id\":\"0100AAAA00000000\",\"achievements\":["
                           "{\"id\":\"a\",\"title\":\"A\",\"type\":\"playtime_stat\",\"target_seconds\":60}]}");
    std::string body = slurp(fixture);
    Entry e = idx.packs[0];
    e.url = "https://exemplo.invalid/desafios.rubydb";
    e.sha256 = sha256_file(fixture);
    g_http[e.url] = body;
    const std::string dest = std::string(PT_ACH_DIR) + "/" + file_name(key, e.pack_id);
    check("nome no SD: <TID>.<pack_id>.rubydb", file_name(key, e.pack_id) == "0100AAAA00000000.botw_hard.rubydb");

    Entry bad_sha = e;
    bad_sha.sha256 = std::string(64, '0');
    check("SHA-256 diferente do catálogo: recusado, nada no SD",
          !install(bad_sha, key, err) && !exists(dest) && !exists(dest + ".part"));
    check("pacote baixado", install(e, key, err) && slurp(dest) == body && !exists(dest + ".part"));
    check("vira variante do jogo (lista de pacotes)", pt_pack_files(key) == std::vector<std::string>{"0100AAAA00000000.botw_hard.rubydb"});

    // Atualização do mesmo pack_id: substitui o arquivo.
    rubydb::write(fixture, "{\"pack_name\":\"Desafios 2\",\"title_id\":\"0100AAAA00000000\",\"achievements\":["
                           "{\"id\":\"a\",\"title\":\"A\",\"type\":\"playtime_stat\",\"target_seconds\":60},"
                           "{\"id\":\"b\",\"title\":\"B\",\"type\":\"playtime_stat\",\"target_seconds\":90}]}");
    e.sha256 = sha256_file(fixture);
    g_http[e.url] = slurp(fixture);
    check("atualização substitui o arquivo", install(e, key, err) && sha256_file(dest) == e.sha256 &&
                                                  pt_pack_files(key).size() == 1);

    // Pacote de outro jogo e arquivo que não é .rubydb.
    rubydb::write(fixture, "{\"title_id\":\"0100BBBB00000000\",\"achievements\":[{\"id\":\"a\",\"title\":\"A\"}]}");
    Entry other = e;
    other.pack_id = "de_outro";
    other.sha256 = sha256_file(fixture);
    g_http[other.url] = slurp(fixture);
    check("Title ID do pacote diferente do jogo: recusado", !install(other, key, err) &&
                                                             !exists(std::string(PT_ACH_DIR) + "/" + file_name(key, "de_outro")) &&
                                                             !exists(std::string(PT_ACH_DIR) + "/" + file_name(key, "de_outro") + ".part"));
    Entry junk = e;
    junk.pack_id = "lixo";
    g_http[junk.url] = "isto não é SQLite";
    FILE* f = fopen(fixture.c_str(), "wb");
    fputs("isto não é SQLite", f);
    fclose(f);
    junk.sha256 = sha256_file(fixture);
    check("arquivo que não é .rubydb: recusado", !install(junk, key, err) &&
                                                   !exists(std::string(PT_ACH_DIR) + "/" + file_name(key, "lixo") + ".part"));
    Entry missing = e;
    missing.url = "https://exemplo.invalid/404.rubydb";
    check("download que falha: recusado", !install(missing, key, err));
    Entry evil = e;
    evil.pack_id = "../fora";
    check("pack_id fora do padrão: recusado", !install(evil, key, err));
    check("jogo sem Title ID: recusado", !install(e, "JogoROM", err));

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
