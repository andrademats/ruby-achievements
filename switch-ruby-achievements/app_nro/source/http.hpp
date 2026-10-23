#pragma once
// GET via libcurl (SSL pelo serviço do sistema). Bloqueante: use fora do thread
// da UI. Usado pelo SteamGridDB e pelo RetroAchievements.
#include <string>
#include <vector>

namespace http {

struct Response {
    long status = 0;
    std::vector<unsigned char> body;
};

// false = falha de rede ou corpo maior que max_bytes (error explica). Um status
// HTTP de erro não é falha: confira `status`.
bool get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error,
         size_t max_bytes = 16u << 20);

// Baixa a URL direto para `path` (status 200 exigido; nada além de max_bytes). Em falha o
// arquivo parcial é apagado. Usado pela atualização do app.
bool download(const std::string& url, const std::vector<std::string>& headers, const std::string& path,
              size_t max_bytes, std::string& error);

// Escapa um texto para usar na URL.
std::string escape(const std::string& text);

}  // namespace http
