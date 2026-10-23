#include "steamgriddb.hpp"
#include "i18n.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>

#include "pt_config.h"
#include "pt_paths.h"
#include "pt_util.h"
#include "http.hpp"

namespace {

constexpr const char* kApi = "https://www.steamgriddb.com/api/v2";
constexpr size_t kMaxJson = 2u << 20;

// Campos opcionais do JSON sem exceções: tipo errado = valor padrão.
std::string str_of(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
int64_t int_of(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int64_t>() : 0;
}
bool bool_of(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

// Corpo de resposta da API: {"success": true, "data": [...]} ou {"success": false, "errors": [...]}.
bool data_array(const std::string& body, nlohmann::json& data, std::string& error) {
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = tr("Resposta inválida do SteamGridDB.");
        return false;
    }
    if (!bool_of(j, "success")) {
        error = tr("SteamGridDB recusou o pedido");
        auto it = j.find("errors");
        if (it != j.end() && it->is_array() && !it->empty() && (*it)[0].is_string())
            error += ": " + (*it)[0].get<std::string>();
        error += ".";
        return false;
    }
    auto it = j.find("data");
    data = it != j.end() && it->is_array() ? *it : nlohmann::json::array();
    return true;
}

}  // namespace

SteamGridDBClient::SteamGridDBClient(std::string api_key) : m_key(std::move(api_key)) {}

bool SteamGridDBClient::fetch(const std::string& url, std::vector<unsigned char>& out, size_t max_bytes) {
    out.clear();
    m_error.clear();
    std::vector<std::string> headers{"Accept: application/json, image/*"};
    // A chave só vai para a API; as imagens vêm do CDN, que não precisa dela.
    if (url.rfind(kApi, 0) == 0) headers.push_back("Authorization: Bearer " + m_key);
    http::Response r;
    std::string err;
    if (!http::get(url, headers, r, err, max_bytes)) {
        m_error = err.empty() ? std::string(tr("Falha no download.")) : "SteamGridDB: " + err;
        return false;
    }
    long status = r.status;
    if (status == 401 || status == 403) {
        m_error = trf("Chave da API do SteamGridDB inválida (HTTP %d).", (int)status);
        return false;
    }
    if (status == 404) return true;  // a API responde 404 quando não encontra nada
    if (status < 200 || status >= 300) {
        m_error = trf("SteamGridDB respondeu HTTP %d.", (int)status);
        return false;
    }
    out = std::move(r.body);
    return true;
}

bool SteamGridDBClient::get_json(const std::string& path, std::string& body) {
    if (m_key.empty()) {
        m_error = tr("Falta a chave da API do SteamGridDB.");
        return false;
    }
    std::vector<unsigned char> buf;
    if (!fetch(std::string(kApi) + path, buf, kMaxJson)) return false;
    body.assign(buf.begin(), buf.end());
    if (body.empty()) body = "{\"success\":true,\"data\":[]}";  // 404
    return true;
}

bool SteamGridDBClient::search(const std::string& term, std::vector<SgdbGame>& out) {
    out.clear();
    std::string q = http::escape(term);
    if (q.empty()) {
        m_error = tr("Digite o nome do jogo.");
        return false;
    }

    std::string body;
    nlohmann::json data;
    if (!get_json("/search/autocomplete/" + q, body) || !data_array(body, data, m_error)) return false;
    for (const auto& g : data) {
        if (!g.is_object()) continue;
        SgdbGame r;
        r.id = int_of(g, "id");
        r.name = str_of(g, "name");
        r.release_date = int_of(g, "release_date");
        r.verified = bool_of(g, "verified");
        if (r.id > 0 && !r.name.empty()) out.push_back(r);
    }
    return true;
}

bool SteamGridDBClient::square_grids(int64_t game_id, std::vector<SgdbGrid>& out) {
    out.clear();
    // Só a categoria Grid, nos dois tamanhos quadrados da API, em formatos que o
    // Borealis abre (PNG/JPEG; WebP fica de fora).
    std::string path = "/grids/game/" + std::to_string(game_id) +
                       "?dimensions=512x512,1024x1024&mimes=image/png,image/jpeg&types=static&nsfw=false";
    std::string body;
    nlohmann::json data;
    if (!get_json(path, body) || !data_array(body, data, m_error)) return false;
    for (const auto& g : data) {
        if (!g.is_object()) continue;
        SgdbGrid r;
        r.id = int_of(g, "id");
        r.width = (int)int_of(g, "width");
        r.height = (int)int_of(g, "height");
        r.mime = str_of(g, "mime");
        r.url = str_of(g, "url");
        r.thumb = str_of(g, "thumb");
        auto a = g.find("author");
        if (a != g.end() && a->is_object()) r.author = str_of(*a, "name");
        // 1:1 obrigatório, mesmo se a API devolver outro tamanho.
        if (r.width <= 0 || r.width != r.height || r.url.empty()) continue;
        if (r.mime != "image/png" && r.mime != "image/jpeg") continue;
        if (r.thumb.empty()) r.thumb = r.url;
        out.push_back(r);
    }
    return true;
}

namespace sgdb {

std::string api_key() {
    PtConfig cfg;
    pt_config_load(&cfg);
    return cfg.sgdb_key;
}

bool save_api_key(const std::string& key) {
    std::string k;
    for (char ch : key)
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) k += ch;
    PtConfig cfg;
    pt_config_load(&cfg);
    if (k.empty() || k.size() >= sizeof(cfg.sgdb_key)) return false;
    snprintf(cfg.sgdb_key, sizeof(cfg.sgdb_key), "%s", k.c_str());
    return pt_secrets_save(&cfg) == 0;
}

bool save_cover(const std::string& base, const std::vector<unsigned char>& image, std::string& path_out,
                std::string& error) {
    bool png = image.size() > 8 && !memcmp(image.data(), "\x89PNG", 4);
    bool jpg = image.size() > 3 && image[0] == 0xFF && image[1] == 0xD8;
    if (!png && !jpg) {
        error = tr("A imagem baixada não é PNG nem JPEG.");
        return false;
    }
    std::string dir = PT_COVERS_DIR;
    path_out = dir + "/" + base + (png ? ".png" : ".jpg");
    if (pt_write_file_atomic(path_out.c_str(), (const char*)image.data(), image.size()) != 0) {
        error = trf("Não foi possível gravar %s.", path_out.c_str());
        return false;
    }
    remove((dir + "/" + base + (png ? ".jpg" : ".png")).c_str());  // capa antiga no outro formato
    return true;
}

}  // namespace sgdb
