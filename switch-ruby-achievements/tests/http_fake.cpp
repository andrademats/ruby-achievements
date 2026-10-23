// http.hpp falso para o update_test: serve URLs de um mapa em memória (sem rede).
#include "http.hpp"

#include <cstdio>
#include <map>
#include <unistd.h>

std::map<std::string, std::string> g_http;  // URL -> corpo (ausente = HTTP 404)
int g_http_downloads = 0;

namespace http {

bool get(const std::string& url, const std::vector<std::string>&, Response& out, std::string& error, size_t max_bytes) {
    out = {};
    auto it = g_http.find(url);
    if (it == g_http.end()) {
        out.status = 404;
        return true;
    }
    if (it->second.size() > max_bytes) {
        error = "grande demais";
        return false;
    }
    out.status = 200;
    out.body.assign(it->second.begin(), it->second.end());
    return true;
}

bool download(const std::string& url, const std::vector<std::string>&, const std::string& path, size_t max_bytes,
              std::string& error) {
    g_http_downloads++;
    auto it = g_http.find(url);
    if (it == g_http.end()) {
        error = "HTTP 404";
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        error = "sem arquivo";
        return false;
    }
    size_t n = it->second.size() > max_bytes ? max_bytes : it->second.size();
    fwrite(it->second.data(), 1, n, f);
    fclose(f);
    if (it->second.size() > max_bytes) {  // como o http.cpp: aborta e apaga o parcial
        unlink(path.c_str());
        error = "Arquivo maior que o esperado: download recusado.";
        return false;
    }
    return true;
}

std::string escape(const std::string& text) { return text; }

}  // namespace http
