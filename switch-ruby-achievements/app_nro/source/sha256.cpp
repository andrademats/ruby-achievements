#include "sha256.hpp"

#include <switch.h>

#include <cstdio>
#include <vector>

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
