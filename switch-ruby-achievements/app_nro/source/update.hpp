#pragma once
// Atualização do app (aba Atualização): última release do GitHub + manifesto assinado.
//
// 1. check(): consulta releases/latest de kRepo, baixa os assets manifest.json e manifest.sig,
//    confere a assinatura Ed25519 com a chave pública embutida (update_key.inc) e valida o
//    manifesto (versão, destinos só da lista fixa de pt_update.h, tamanho e SHA-256 de cada um).
// 2. install(): baixa cada binário instalado neste SD para staging/ (sem passar do tamanho do
//    manifesto), confere tamanho e SHA-256 de todos, copia o banco para o backup (com o lock) e
//    só então troca os arquivos (pt_update_apply, com diário e recuperação).
// A versão nova vale depois de reiniciar o console (sysmodule e overlay ficam carregados).
//
// Bloqueante: chamar fora do thread da UI.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace upd {

// Repositório das releases ("dono/repositório" no GitHub). Vazio = atualização desligada.
extern const char* const kRepo;

struct Version {
    int major = 0, minor = 0, patch = 0;
};
bool parse_version(const std::string& text, Version& out);  // "1.2.3" ou "v1.2.3"
int compare(const Version& a, const Version& b);            // <0, 0, >0
std::string to_string(const Version& v);
Version current();  // a deste build (arquivo VERSION)

struct ManifestFile {
    int target = -1;  // índice em pt_update
    std::string path, sha256;
    uint64_t size = 0;
};
struct Manifest {
    Version version, min_version;
    std::string notes;
    std::vector<ManifestFile> files;
};
// Valida tudo; qualquer coisa fora do esperado recusa o manifesto inteiro.
bool parse_manifest(const std::string& json, Manifest& out, std::string& error);

// Assinatura Ed25519 (128 caracteres hex) dos bytes exatos do manifesto.
bool verify_signature(const std::string& manifest, const std::string& sig_hex, const uint8_t public_key[32]);
const uint8_t* release_public_key();

struct Asset {
    std::string name, url;
    uint64_t size = 0;
};
struct Release {
    std::string tag;
    std::vector<Asset> assets;
};
bool parse_release(const std::string& json, Release& out, std::string& error);
const Asset* find_asset(const Release& r, const std::string& name);

// SHA-256 de um arquivo em hex minúsculo; "" se não leu.
std::string sha256_file(const std::string& path);

struct Check {
    Release release;
    Manifest manifest;
    bool newer = false;        // versão da release > a instalada
    bool too_old = false;      // a instalada é menor que min_version: atualizar à mão
    std::vector<int> targets;  // destinos do manifesto que existem neste SD
};
bool check(Check& out, std::string& error);
// Por que não dá para instalar o resultado de check() ("" = dá).
std::string blocked_reason(const Check& c);

// Passo atual, para a tela (já traduzido).
using Progress = std::function<void(const std::string& step)>;
bool install(const Check& c, const Progress& progress, std::string& error);

// Partes do install() sem rede (testadas no PC).
bool verify_staged(const Manifest& m, const std::vector<int>& targets, std::string& error);
bool backup_database(std::string& error);  // PT_DB_PATH -> PT_UPDATE_DB_BACKUP_PATH, com o lock

}  // namespace upd
