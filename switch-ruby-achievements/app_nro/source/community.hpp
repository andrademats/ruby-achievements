#pragma once
// Pacotes da comunidade: catálogo index.json no GitHub (kIndexUrl), filtro por jogo e
// instalação no SD.
//
// O catálogo lista pacotes .rubydb com o alvo de cada um:
//   native: title_id (16 hex) + supported_build_ids (prefixos hex) + game_version_min
//   port:   port_slug (ex.: soh_oot) + game_version_min (versão do executável do port)
//   rom:    platform (ex.: GBA) + rom_hash (MD5 ou SHA-1 da ROM descompactada)
// Entradas com campo inválido são puladas (Index::skipped), sem recusar o catálogo inteiro.
//
// Um pacote baixado vira variante do jogo em PT_ACH_DIR: "<TID>.<pack_id>.rubydb" (ver
// pt_pack.h), com o SHA-256 do catálogo conferido e o pacote validado antes de entrar no
// lugar. Ativar e apagar usam data::set_active_pack e data::delete_achievement_pack.
// Por enquanto a tela só oferece pacotes de jogos do Switch (native).
//
// load_index e install são bloqueantes (rede): chamar fora do thread da UI.
#include <string>
#include <vector>

namespace community {

extern const char* const kIndexUrl;
constexpr size_t kMaxIndexBytes = 2u << 20;
constexpr size_t kMaxPackBytes = 32u << 20;

enum class TargetType { Native, Port, Rom };

struct Target {
    TargetType type = TargetType::Native;
    std::string title_id;                // native: 16 hex maiúsculos
    std::vector<std::string> build_ids;  // native: prefixos hex maiúsculos (vazio = qualquer build)
    std::string port_slug;               // port
    std::string platform;                // rom: maiúsculas (GBA, N64...)
    std::string rom_hash;                // rom: MD5 (32 hex) ou SHA-1 (40 hex), minúsculos
    std::string version_min;             // native/port: "" = sem mínimo
};

struct Entry {
    std::string pack_id;  // [a-z0-9_-], até 48: vira parte do nome do arquivo
    std::string title, author, description, version;
    int total = 0;
    Target target;
    std::string url;     // https://
    std::string sha256;  // 64 hex minúsculos
};

struct Index {
    std::string updated_at;
    std::vector<Entry> packs;
    int skipped = 0;  // entradas inválidas
};
bool parse_index(const std::string& json, Index& out, std::string& error);

// O jogo para o qual se procura pacote.
struct Game {
    TargetType type = TargetType::Native;
    std::string title_id;  // native
    std::string build_id;  // native: build id do executável principal ("" = ainda não visto pelo sysmodule)
    std::string version;   // native: versão do NACP ("1.6.0"); port: versão do executável; "" = desconhecida
    std::string port_slug;
    std::string platform;
    std::string rom_md5, rom_sha1;
};

enum class Compat {
    Ok,            // alvo e build/hash batem
    Warning,       // mesmo jogo, mas o build difere ou ainda não é conhecido
    Incompatible,  // versão instalada abaixo de game_version_min
};
struct Match {
    Entry entry;
    Compat compat = Compat::Ok;
};
// Pacotes do catálogo para o jogo: compatíveis primeiro, depois aviso, depois incompatíveis
// (cada grupo pelo título).
std::vector<Match> filter(const Index& index, const Game& game);
Compat compatibility(const Entry& e, const Game& game);

// Compara versões numéricas pontuadas ("1.6.0" < "1.10"). false = alguma não é numérica.
bool compare_versions(const std::string& a, const std::string& b, int& result);

// Nome do arquivo do pacote no SD: "<TID>.<pack_id>.rubydb".
std::string file_name(const std::string& title_key, const std::string& pack_id);

// Baixa o catálogo para PT_COMMUNITY_INDEX_PATH; sem rede (ou resposta inválida), usa a
// cópia salva da última vez (from_cache = true). false = nenhum dos dois.
bool load_index(Index& out, bool& from_cache, std::string& error);

// Baixa o pacote para o jogo `title_key`, confere tamanho e SHA-256, valida o .rubydb
// (pt_pack_load, Title ID do pacote = title_key) e só então o põe no lugar (substitui a
// versão anterior do mesmo pack_id). Nada fica no SD se algo falhar.
bool install(const Entry& e, const std::string& title_key, std::string& error);

}  // namespace community
