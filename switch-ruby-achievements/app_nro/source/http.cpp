#include "http.hpp"
#include "i18n.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>
#include <unistd.h>

namespace http {
namespace {

struct Sink {
    std::vector<unsigned char>* buf = nullptr;  // memória (get)
    FILE* file = nullptr;                       // arquivo (download)
    size_t max = 0;
    size_t total = 0;
    bool overflow = false;
    bool write_failed = false;
};

size_t on_data(char* ptr, size_t size, size_t n, void* user) {
    Sink* s = static_cast<Sink*>(user);
    size_t len = size * n;
    if (s->total + len > s->max) {
        s->overflow = true;
        return 0;  // aborta a transferência
    }
    s->total += len;
    if (s->file) {
        if (fwrite(ptr, 1, len, s->file) != len) {
            s->write_failed = true;
            return 0;
        }
    } else {
        s->buf->insert(s->buf->end(), ptr, ptr + len);
    }
    return len;
}

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// GET comum a get() e download(): só HTTPS (também nos redirecionamentos, como os do GitHub
// para o servidor de arquivos), certificado e nome do servidor verificados pelo serviço ssl.
CURLcode perform(const std::string& url, const std::vector<std::string>& headers, Sink& sink, long& status) {
    CURL* c = curl_easy_init();
    if (!c) return CURLE_FAILED_INIT;
    curl_slist* list = nullptr;
    for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "RubyAchievements/1.0 (libnx)");
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, sink.file ? 600L : 60L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);

    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(list);
    curl_easy_cleanup(c);
    return rc;
}

}  // namespace

bool get(const std::string& url, const std::vector<std::string>& headers, Response& out, std::string& error,
         size_t max_bytes) {
    global_init();
    out = {};
    error.clear();
    Sink sink;
    sink.buf = &out.body;
    sink.max = max_bytes;
    CURLcode rc = perform(url, headers, sink, out.status);
    if (rc == CURLE_FAILED_INIT) {
        error = tr("Falha ao iniciar o libcurl.");
        return false;
    }
    if (sink.overflow) {
        error = tr("Resposta grande demais.");
        return false;
    }
    if (rc != CURLE_OK) {
        error = trf("Sem conexão (%s).", curl_easy_strerror(rc));
        return false;
    }
    return true;
}

bool download(const std::string& url, const std::vector<std::string>& headers, const std::string& path,
              size_t max_bytes, std::string& error) {
    global_init();
    error.clear();
    Sink sink;
    sink.file = fopen(path.c_str(), "wb");
    sink.max = max_bytes;
    if (!sink.file) {
        error = trf("Não foi possível criar %s.", path.c_str());
        return false;
    }
    long status = 0;
    CURLcode rc = perform(url, headers, sink, status);
    bool closed = fclose(sink.file) == 0;
    if (rc == CURLE_OK && status == 200 && closed && !sink.write_failed) return true;

    unlink(path.c_str());
    if (sink.overflow) error = tr("Arquivo maior que o esperado: download recusado.");
    else if (sink.write_failed || !closed) error = tr("Falha ao gravar no cartão SD (espaço livre?).");
    else if (rc != CURLE_OK) error = trf("Sem conexão (%s).", curl_easy_strerror(rc));
    else error = trf("O servidor respondeu HTTP %d.", (int)status);
    return false;
}

std::string escape(const std::string& text) {
    global_init();
    CURL* c = curl_easy_init();
    if (!c) return "";
    char* esc = curl_easy_escape(c, text.c_str(), (int)text.size());
    std::string out = esc ? esc : "";
    if (esc) curl_free(esc);
    curl_easy_cleanup(c);
    return out;
}

}  // namespace http
