#pragma once
// Cliente da API v2 do SteamGridDB (https://www.steamgriddb.com/api/v2): busca
// o jogo pelo nome e lista só grids quadrados (1:1). Chamadas bloqueantes: use
// fora do thread da UI (brls::async). Cada chamada usa o próprio handle do curl.
#include <cstdint>
#include <string>
#include <vector>

struct SgdbGame {
    int64_t id = 0;
    std::string name;
    int64_t release_date = 0;  // epoch; 0 = desconhecida
    bool verified = false;
};

struct SgdbGrid {
    int64_t id = 0;
    int width = 0, height = 0;
    std::string mime;   // image/png ou image/jpeg
    std::string url;    // imagem completa
    std::string thumb;  // miniatura
    std::string author;
};

class SteamGridDBClient {
public:
    explicit SteamGridDBClient(std::string api_key);

    // GET /search/autocomplete/{term}
    bool search(const std::string& term, std::vector<SgdbGame>& out);
    // GET /grids/game/{id}, só 512x512 e 1024x1024 em PNG/JPEG, sem NSFW.
    bool square_grids(int64_t game_id, std::vector<SgdbGrid>& out);
    // Baixa qualquer URL (miniatura ou imagem) para a memória.
    bool fetch(const std::string& url, std::vector<unsigned char>& out, size_t max_bytes = 16u << 20);

    const std::string& error() const { return m_error; }

private:
    bool get_json(const std::string& path, std::string& body);

    std::string m_key;
    std::string m_error;
};

namespace sgdb {
// Chave salva em config.ini ([app] steamgriddb_key).
std::string api_key();
bool save_api_key(const std::string& key);
// Grava a imagem em PT_COVERS_DIR/<nome>.png (ou .jpg, conforme o conteúdo) e
// devolve o caminho. `base` = Title ID, ou "game<id>" para ROMs.
bool save_cover(const std::string& base, const std::vector<unsigned char>& image, std::string& path_out,
                std::string& error);
}  // namespace sgdb
