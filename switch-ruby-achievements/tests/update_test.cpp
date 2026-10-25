// Atualização do app: versão, manifesto, assinatura Ed25519 (Monocypher, compatível com a
// ferramenta tools/release_manifest.py), SHA-256, zip da release conferido e extraído, troca com
// diário e backup, recuperação de troca interrompida e dados do usuário intocados.
//
// update_fixture/ (manifest.json, manifest.sig, o zip e test_public.hex) é gerado pela própria
// tools/release_manifest.py com VERSION 9.9.9 e uma chave de teste descartável, a partir dos
// binários falsos de update_fixture/assets/ (mais um boot2.flag e um sfx/LEIA-ME.txt no zip).
#include "pt_paths.h"
#include "pt_update.h"
#include "pt_util.h"
#include "sha256.hpp"
#include "update.hpp"

#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

s64 g_fake_now = 1000;
extern std::map<std::string, std::string> g_http;
extern int g_http_downloads;

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

static std::string slurp(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "<ausente>";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}
static void put(const std::string& path, const std::string& data) {
    std::string dir;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '/' && i > 5) mkdir(path.substr(0, i).c_str(), 0777);
    }
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}
static bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
static std::string sd(int i) {
    char p[256];
    pt_update_target_sd_path(i, p, sizeof(p));
    return p;
}
static std::string staged(int i) {
    char p[256];
    pt_update_staged_path(i, p, sizeof(p));
    return p;
}
static std::string fixture(const std::string& name) { return slurp(std::string(PT_REPO "/tests/update_fixture/") + name); }

// SD com a versão "velha" (o toolbox.json fica de fora: destino não instalado) e dados do usuário.
static const char* kUserFiles[] = {PT_DB_PATH, PT_CONFIG_PATH, PT_SECRETS_PATH, PT_COVERS_DIR "/capa.png",
                                   PT_SFX_DIR "/meu_som.wav", PT_ACH_DIR "/01007EF00011E000.json", PT_LOG_PATH};
static void reset_sd() {
    for (int i = 0; i < pt_update_target_count(); i++) unlink(sd(i).c_str());
    for (int i = 0; i < 3; i++) put(sd(i), std::string("VELHO ") + pt_update_target_name(i));
    // Como o pt_update_apply: backup vazio antes de cada troca (um diário só convive com o
    // backup da própria troca).
    for (int i = 0; i < pt_update_target_count(); i++)
        unlink((std::string(PT_UPDATE_BACKUP_DIR "/") + pt_update_target_name(i)).c_str());
    for (const char* f : kUserFiles) put(f, std::string("dados do usuario: ") + f);
    unlink(PT_UPDATE_JOURNAL_PATH);
    unlink(PT_UPDATE_PENDING_PATH);
    unlink(PT_UPDATE_DB_BACKUP_PATH);
}
static bool user_files_intact() {
    for (const char* f : kUserFiles)
        if (slurp(f) != std::string("dados do usuario: ") + f) return false;
    return true;
}
static bool all_old() {
    for (int i = 0; i < 3; i++)
        if (slurp(sd(i)) != std::string("VELHO ") + pt_update_target_name(i)) return false;
    return !exists(sd(3));
}

// Release falsa com os assets da fixture (servidos pelo http_fake).
static const std::string kZip = "ruby-achievements-v9.9.9.zip";
static const std::string kZipUrl = "https://example.test/download/" + kZip;
static upd::Check fixture_check() {
    upd::Check c;
    std::string err;
    upd::parse_manifest(fixture("manifest.json"), c.manifest, err);
    for (const std::string name : {std::string("manifest.json"), std::string("manifest.sig"), kZip}) {
        std::string url = "https://example.test/download/" + name;
        g_http[url] = fixture(name);
        c.release.assets.push_back({name, url, (uint64_t)g_http[url].size()});
    }
    c.newer = upd::compare(c.manifest.version, upd::current()) > 0;
    for (const upd::ManifestFile& f : c.manifest.files)
        if (pt_update_target_installed(f.target)) c.targets.push_back(f.target);
    return c;
}

static std::string hex_bytes(const uint8_t* b, int n) {
    std::string s;
    char t[3];
    for (int i = 0; i < n; i++) {
        snprintf(t, sizeof(t), "%02x", b[i]);
        s += t;
    }
    return s;
}
static bool from_hex(const std::string& hex, uint8_t* out, int n) {
    if ((int)hex.size() < n * 2) return false;
    for (int i = 0; i < n; i++) out[i] = (uint8_t)strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
    return true;
}

int main() {
    mkdir("sdmc:", 0777);
    mkdir("sdmc:/switch", 0777);
    pt_ensure_dirs();

    // --- SHA-256 (shim do PC com a API do libnx) ---
    put("sdmc:/vazio.bin", "");
    put("sdmc:/abc.bin", "abc");
    check("sha256(\"\")", sha256_file("sdmc:/vazio.bin") ==
                              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("sha256(\"abc\")", sha256_file("sdmc:/abc.bin") ==
                                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check("sha256 de arquivo ausente = \"\"", sha256_file("sdmc:/nao_existe.bin").empty());

    // --- Versões ---
    upd::Version v;
    check("versão 1.2.3", upd::parse_version("1.2.3", v) && v.major == 1 && v.minor == 2 && v.patch == 3);
    check("versão v10.0.1 e espaços", upd::parse_version(" v10.0.1\n", v) && v.major == 10 && v.patch == 1);
    check("versões inválidas recusadas", !upd::parse_version("1.2", v) && !upd::parse_version("1.2.3.4", v) &&
                                             !upd::parse_version("a.b.c", v) && !upd::parse_version("", v) &&
                                             !upd::parse_version("1..2", v) && !upd::parse_version("1.2.-3", v));
    upd::Version a{1, 9, 9}, b{1, 10, 0};
    check("comparação numérica (1.9.9 < 1.10.0)", upd::compare(a, b) < 0 && upd::compare(b, a) > 0 && upd::compare(a, a) == 0);
    upd::parse_version(slurp(PT_REPO "/VERSION"), v);
    check("current() = arquivo VERSION", upd::compare(upd::current(), v) == 0);

    // --- Ed25519: vetores da RFC 8032 e a assinatura da ferramenta Python ---
    uint8_t pk1[32], pk2[32];
    from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk1, 32);
    from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", pk2, 32);
    const std::string sig1 = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
                             "d25bf5f0595bbe24655141438e7a100b";
    const std::string sig2 = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c"
                             "387b2eaeb4302aeeb00d291612bb0c00";
    check("RFC 8032 vetor 1", upd::verify_signature("", sig1, pk1));
    check("RFC 8032 vetor 2", upd::verify_signature("\x72", sig2, pk2));
    check("RFC 8032: mensagem trocada falha", !upd::verify_signature("\x73", sig2, pk2));
    check("RFC 8032: chave trocada falha", !upd::verify_signature("\x72", sig2, pk1));
    uint8_t test_pk[32];
    from_hex(fixture("test_public.hex"), test_pk, 32);
    std::string man = fixture("manifest.json"), sig = fixture("manifest.sig");
    check("manifesto assinado pela ferramenta Python confere", upd::verify_signature(man, sig, test_pk));
    std::string tampered = man;
    tampered[tampered.find("9.9.9")] = '8';
    check("manifesto adulterado (1 byte) não confere", !upd::verify_signature(tampered, sig, test_pk));
    check("assinatura curta/sem hex recusada",
          !upd::verify_signature(man, sig.substr(0, 100), test_pk) && !upd::verify_signature(man, std::string(128, 'z'), test_pk));
    check("chave da release não aceita a assinatura de teste", !upd::verify_signature(man, sig, upd::release_public_key()));
    check("chave da release embutida (não zerada)", hex_bytes(upd::release_public_key(), 32) != std::string(64, '0'));

    // --- Manifesto ---
    upd::Manifest m;
    std::string err;
    check("manifesto da fixture válido", upd::parse_manifest(man, m, err) && m.files.size() == 4 &&
                                             upd::to_string(m.version) == "9.9.9" && m.notes == "Versão de teste." &&
                                             m.package.name == kZip && m.package.size == fixture(kZip).size());
    const std::string sha = std::string(64, 'a');
    const std::string good_pkg = "{\"name\":\"pacote-v1.zip\",\"size\":100,\"sha256\":\"" + sha + "\"}";
    auto with_file = [&](const std::string& entry) {
        return "{\"version\":\"9.9.9\",\"package\":" + good_pkg + ",\"files\":[" + entry + "]}";
    };
    const std::string ovl_entry = "{\"path\":\"switch/.overlays/ruby-achievements-overlay.ovl\",\"size\":10,\"sha256\":\"" + sha + "\"}";
    auto with_pkg = [&](const std::string& pkg) {
        return "{\"version\":\"9.9.9\"" + (pkg.empty() ? std::string() : ",\"package\":" + pkg) + ",\"files\":[" + ovl_entry + "]}";
    };
    auto pkg = [&](const std::string& name, const std::string& size = "100", const std::string& h = "") {
        return "{\"name\":\"" + name + "\",\"size\":" + size + ",\"sha256\":\"" + (h.empty() ? sha : h) + "\"}";
    };
    check("pacote válido aceito", upd::parse_manifest(with_pkg(good_pkg), m, err));
    check("recusa manifesto sem pacote", !upd::parse_manifest(with_pkg(""), m, err));
    check("recusa nome de pacote com caminho ou sem .zip",
          !upd::parse_manifest(with_pkg(pkg("../x.zip")), m, err) && !upd::parse_manifest(with_pkg(pkg("a/x.zip")), m, err) &&
              !upd::parse_manifest(with_pkg(pkg("x.exe")), m, err) && !upd::parse_manifest(with_pkg(pkg(".zip")), m, err));
    check("recusa pacote com tamanho 0 ou > 128 MB e sha256 ruim",
          !upd::parse_manifest(with_pkg(pkg("x.zip", "0")), m, err) &&
              !upd::parse_manifest(with_pkg(pkg("x.zip", "134217729")), m, err) &&
              !upd::parse_manifest(with_pkg(pkg("x.zip", "100", std::string(63, 'a'))), m, err));
    auto entry = [&](const std::string& path, const std::string& size = "10", const std::string& h = "") {
        return "{\"path\":\"" + path + "\",\"size\":" + size + ",\"sha256\":\"" + (h.empty() ? sha : h) + "\"}";
    };
    check("min_version ausente = 0.0.0", upd::parse_manifest(with_file(entry("switch/.overlays/ruby-achievements-overlay.ovl")), m, err) &&
                                            upd::to_string(m.min_version) == "0.0.0");
    check("recusa o banco como destino", !upd::parse_manifest(with_file(entry("switch/ruby_achievements/ruby_achievements.db")), m, err));
    check("recusa config.ini e secrets.ini", !upd::parse_manifest(with_file(entry("switch/ruby_achievements/config.ini")), m, err) &&
                                                !upd::parse_manifest(with_file(entry("switch/ruby_achievements/secrets.ini")), m, err));
    check("recusa ../ e sdmc:/", !upd::parse_manifest(with_file(entry("switch/ruby_achievements/../ruby_achievements/ruby_achievements.nro")), m, err) &&
                                    !upd::parse_manifest(with_file(entry("sdmc:/switch/ruby_achievements/ruby_achievements.nro")), m, err));
    check("recusa boot2.flag (não está na lista)", !upd::parse_manifest(with_file(entry("atmosphere/contents/0100000000007A11/flags/boot2.flag")), m, err));
    check("recusa destino repetido", !upd::parse_manifest(with_file(entry("atmosphere/contents/0100000000007A11/exefs.nsp") + "," +
                                                                    entry("atmosphere/contents/0100000000007A11/exefs.nsp")), m, err));
    const std::string nro = "switch/ruby_achievements/ruby_achievements.nro";
    check("recusa sha256 maiúsculo ou curto", !upd::parse_manifest(with_file(entry(nro, "10", std::string(64, 'A'))), m, err) &&
                                                 !upd::parse_manifest(with_file(entry(nro, "10", std::string(63, 'a'))), m, err));
    check("recusa tamanho 0, negativo, texto ou > 64 MB",
          !upd::parse_manifest(with_file(entry(nro, "0")), m, err) && !upd::parse_manifest(with_file(entry(nro, "-5")), m, err) &&
              !upd::parse_manifest(with_file(entry(nro, "\"10\"")), m, err) &&
              !upd::parse_manifest(with_file(entry(nro, "67108865")), m, err));
    check("recusa sem arquivos, versão ruim ou JSON quebrado",
          !upd::parse_manifest("{\"version\":\"9.9.9\",\"package\":" + good_pkg + ",\"files\":[]}", m, err) &&
              !upd::parse_manifest("{\"version\":\"9.9\",\"package\":" + good_pkg + ",\"files\":[" + entry(nro) + "]}", m, err) &&
              !upd::parse_manifest("{\"version\":", m, err) && !upd::parse_manifest("[]", m, err));

    // --- Release do GitHub ---
    upd::Release rel;
    check("release: assets https lidos, http ignorado",
          upd::parse_release("{\"tag_name\":\"v9.9.9\",\"assets\":[{\"name\":\"manifest.json\",\"size\":5,"
                             "\"browser_download_url\":\"https://x/manifest.json\"},{\"name\":\"exefs.nsp\","
                             "\"browser_download_url\":\"http://x/exefs.nsp\"}]}",
                             rel, err) &&
              rel.tag == "v9.9.9" && rel.assets.size() == 1 && upd::find_asset(rel, "manifest.json") &&
              !upd::find_asset(rel, "exefs.nsp"));
    check("release: JSON inválido", !upd::parse_release("<html>", rel, err));

    // --- Lista fixa de destinos ---
    bool unique = true;
    for (int i = 0; i < pt_update_target_count(); i++) {
        for (int j = 0; j < i; j++)
            if (!strcmp(pt_update_target_name(i), pt_update_target_name(j))) unique = false;
        if (pt_update_target_find(pt_update_target_rel(i)) != i) unique = false;
    }
    check("destinos: 4, nomes únicos, busca exata", pt_update_target_count() == 4 && unique &&
                                                       pt_update_target_find("switch/ruby_achievements/ruby_achievements.db") == -1);

    // --- Extração do zip da release (sem compressão) ---
    const std::string zip = fixture(kZip), zpath = "sdmc:/pacote.zip", out = "sdmc:/extraido.bin";
    put(zpath, zip);
    bool all_ok = true;
    for (int i = 0; i < pt_update_target_count(); i++) {
        std::string asset = fixture(std::string("assets/") + pt_update_target_name(i));
        unlink(out.c_str());
        all_ok = all_ok && upd::extract_stored(zpath, pt_update_target_rel(i), out, asset.size(), err) && slurp(out) == asset;
    }
    check("zip: os 4 binários extraídos iguais aos originais", all_ok);
    const std::string nro_rel = "switch/ruby_achievements/ruby_achievements.nro";
    const uint64_t nro_size = fixture("assets/ruby_achievements.nro").size();
    check("zip: entrada ausente recusada", !upd::extract_stored(zpath, "switch/nao_existe.nro", out, 10, err) &&
                                               err.find("nao_existe") != std::string::npos);
    check("zip: tamanho diferente do manifesto recusado", !upd::extract_stored(zpath, nro_rel, out, nro_size + 1, err));
    std::string compressed = zip;
    size_t cdh = compressed.find("PK\x01\x02");
    while (cdh != std::string::npos && compressed.compare(cdh + 46, nro_rel.size(), nro_rel) != 0)
        cdh = compressed.find("PK\x01\x02", cdh + 4);
    check("zip: fixture tem a entrada do nro no diretório central", cdh != std::string::npos);
    if (cdh != std::string::npos) {
        compressed[cdh + 10] = 8;  // método deflate
        put(zpath, compressed);
        check("zip: entrada comprimida recusada", !upd::extract_stored(zpath, nro_rel, out, nro_size, err));
    }
    put(zpath, zip.substr(0, zip.size() / 2));
    check("zip: arquivo cortado recusado", !upd::extract_stored(zpath, nro_rel, out, nro_size, err));
    put(zpath, "isto não é um zip, só um texto qualquer");
    check("zip: arquivo que não é zip recusado", !upd::extract_stored(zpath, nro_rel, out, nro_size, err));
    check("zip: arquivo ausente recusado", !upd::extract_stored("sdmc:/nao_existe.zip", nro_rel, out, nro_size, err));
    unlink(zpath.c_str());
    unlink(out.c_str());

    // --- Instalação de ponta a ponta (HTTP falso) ---
    reset_sd();
    upd::Check c = fixture_check();
    check("check: versão nova, 3 destinos instalados (toolbox.json não)", c.newer && c.targets.size() == 3);
    std::vector<std::string> steps;
    bool ok = upd::install(c, [&](const std::string& s) { steps.push_back(s); }, err);
    check("install: ok", ok);
    if (!ok) printf("  erro: %s\n", err.c_str());
    bool replaced = true;
    for (int i = 0; i < 3; i++) replaced = replaced && slurp(sd(i)) == fixture(std::string("assets/") + pt_update_target_name(i));
    check("install: app, overlay e sysmodule trocados", replaced);
    check("install: toolbox.json ausente não foi criado", !exists(sd(3)));
    bool backed = true;
    for (int i = 0; i < 3; i++)
        backed = backed && slurp(std::string(PT_UPDATE_BACKUP_DIR "/") + pt_update_target_name(i)) ==
                               std::string("VELHO ") + pt_update_target_name(i);
    check("install: versão anterior no backup", backed);
    check("install: banco copiado para o backup", slurp(PT_UPDATE_DB_BACKUP_PATH) == slurp(PT_DB_PATH));
    check("install: dados do usuário intocados", user_files_intact());
    char pend[32] = "";
    check("install: diário apagado, reinício pendente 9.9.9",
          !exists(PT_UPDATE_JOURNAL_PATH) && pt_update_pending(pend, sizeof(pend)) == 1 && !strcmp(pend, "9.9.9"));
    check("install: passos mostrados (baixar + extrair + conferir + banco + instalar)", steps.size() == 5);
    check("install: zip apagado do staging", !exists(PT_UPDATE_PACKAGE_PATH));
    pt_update_boot_done();
    check("boot do sysmodule: marca de reinício sai", pt_update_pending(pend, sizeof(pend)) == 0);

    // Download adulterado (mesmo tamanho, outro conteúdo): nada é trocado.
    reset_sd();
    c = fixture_check();
    std::string& z = g_http[kZipUrl];
    z[z.find("binario falso")] ^= 1;  // um byte dentro de um dos binários falsos
    check("adulterado: recusado", !upd::install(c, [](const std::string&) {}, err) && err.find(kZip) != std::string::npos);
    check("adulterado: nada trocado, sem diário nem reinício pendente",
          all_old() && !exists(PT_UPDATE_JOURNAL_PATH) && !pt_update_pending(nullptr, 0) && user_files_intact());
    check("adulterado: zip apagado do staging", !exists(PT_UPDATE_PACKAGE_PATH));

    // Zip maior que o manifesto: o download para no limite.
    reset_sd();
    c = fixture_check();
    g_http[kZipUrl] += "lixo a mais";
    check("zip grande demais: recusado, nada trocado", !upd::install(c, [](const std::string&) {}, err) && all_old());

    // Release sem o zip do manifesto.
    reset_sd();
    c = fixture_check();
    for (size_t i = 0; i < c.release.assets.size(); i++)
        if (c.release.assets[i].name == kZip) c.release.assets.erase(c.release.assets.begin() + i);
    int before_missing = g_http_downloads;
    check("zip ausente na release: recusado sem baixar, nada trocado",
          !upd::install(c, [](const std::string&) {}, err) && all_old() && g_http_downloads == before_missing);

    // Sem versão nova / versão instalada velha demais: nem baixa.
    reset_sd();
    c = fixture_check();
    c.newer = false;
    int before = g_http_downloads;
    check("mesma versão: não baixa nada", !upd::install(c, [](const std::string&) {}, err) && g_http_downloads == before);
    c = fixture_check();
    c.too_old = true;
    check("min_version acima da instalada: não baixa nada",
          !upd::install(c, [](const std::string&) {}, err) && g_http_downloads == before && !upd::blocked_reason(c).empty());

    // --- pt_update_apply recusa antes de mexer em qualquer coisa ---
    reset_sd();
    pt_update_prepare_staging();
    for (int i = 0; i < 4; i++) put(staged(i), "NOVO");
    int t_missing[] = {3}, t_dup[] = {0, 0}, t_out[] = {9};
    char e[256];
    check("apply: destino não instalado recusado", pt_update_apply("9.9.9", t_missing, 1, e, sizeof(e)) == -1 && all_old());
    check("apply: repetido e fora da lista recusados",
          pt_update_apply("9.9.9", t_dup, 2, e, sizeof(e)) == -1 && pt_update_apply("9.9.9", t_out, 1, e, sizeof(e)) == -1 && all_old());
    unlink(staged(1).c_str());
    int t_two[] = {0, 1};
    check("apply: arquivo baixado sumido recusado", pt_update_apply("9.9.9", t_two, 2, e, sizeof(e)) == -1 && all_old() &&
                                                       !exists(PT_UPDATE_JOURNAL_PATH));

    // --- Troca interrompida (queda de energia) em cada ponto ---
    // A: destino 0 já trocado, destino 1 ainda intacto.
    reset_sd();
    pt_update_prepare_staging();
    put(staged(0), "NOVO");
    put(PT_UPDATE_JOURNAL_PATH, "9.9.9\n0\n1\n");
    rename(sd(0).c_str(), (std::string(PT_UPDATE_BACKUP_DIR "/") + pt_update_target_name(0)).c_str());
    rename(staged(0).c_str(), sd(0).c_str());
    check("queda após 1º destino: recover devolve 1 arquivo", pt_update_recover() == 1);
    check("queda após 1º destino: tudo na versão anterior, diário apagado", all_old() && !exists(PT_UPDATE_JOURNAL_PATH));
    // B: destino movido para o backup, novo ainda não entrou (destino faltando).
    reset_sd();
    put(PT_UPDATE_JOURNAL_PATH, "9.9.9\n2\n");
    rename(sd(2).c_str(), (std::string(PT_UPDATE_BACKUP_DIR "/") + pt_update_target_name(2)).c_str());
    check("queda com destino faltando: pt_ensure_dirs recupera", (pt_ensure_dirs(), all_old()) && !exists(PT_UPDATE_JOURNAL_PATH));
    // C: diário gravado, nada movido ainda.
    reset_sd();
    put(PT_UPDATE_JOURNAL_PATH, "9.9.9\n0\n1\n2\n");
    check("queda antes de mover: nada a devolver", pt_update_recover() == 0 && all_old() && !exists(PT_UPDATE_JOURNAL_PATH));
    check("sem diário: recover não faz nada", pt_update_recover() == 0 && all_old() && user_files_intact());

    // --- Repositório ainda não configurado ---
    if (!upd::kRepo[0]) {
        upd::Check none;
        check("sem repositório: check recusa sem rede", !upd::check(none, err) && !err.empty());
    }

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
