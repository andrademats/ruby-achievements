#pragma once
// Som do pop-up de conquista: cabeçalho WAV, regras do arquivo e a pasta sfx/.
// O app valida e lista os arquivos (aba Interface); o overlay valida o escolhido
// antes de tocar. Só stdio/dirent: testável no PC.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "pt_paths.h"

namespace pt {

struct Wav {
    int channels = 0, rate = 0, bits = 0, format = 0;
    uint32_t data_offset = 0, data_size = 0;
    uint32_t ms() const {
        return channels && rate && bits ? (uint64_t)data_size * 1000 / ((uint64_t)rate * channels * bits / 8) : 0;
    }
    // O que o overlay toca: PCM 16 bits a 48 kHz, mono ou estéreo.
    bool playable() const { return format == 1 && bits == 16 && rate == 48000 && (channels == 1 || channels == 2); }
};

// Lê os blocos "fmt " e "data" do começo de um arquivo WAV (RIFF). `size` = tamanho do
// arquivo (o "data" é limitado a ele, para arquivos cortados).
inline bool parse_wav(const uint8_t* b, size_t n, uint64_t size, Wav& w) {
    auto u32 = [&](size_t at) {
        return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
    };
    auto u16 = [&](size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8; };
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return false;
    size_t at = 12;
    bool fmt = false;
    while (at + 8 <= n) {
        uint32_t len = u32(at + 4);
        if (!memcmp(b + at, "fmt ", 4) && at + 8 + 16 <= n) {
            w.format = (int)u16(at + 8);
            w.channels = (int)u16(at + 10);
            w.rate = (int)u32(at + 12);
            w.bits = (int)u16(at + 22);
            fmt = true;
        } else if (!memcmp(b + at, "data", 4)) {
            w.data_offset = (uint32_t)(at + 8);
            uint64_t avail = size > w.data_offset ? size - w.data_offset : 0;
            w.data_size = (uint32_t)std::min<uint64_t>(len, avail);
            return fmt;
        }
        at += 8 + (size_t)len + (len & 1);
    }
    return false;
}

// Cabeçalho de um arquivo aberto (deixa a posição no começo do arquivo).
inline bool read_wav(FILE* f, Wav& w) {
    uint8_t head[512];
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_SET);
    return size > 0 && parse_wav(head, n, (uint64_t)size, w);
}

inline bool read_wav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = read_wav(f, w);
    fclose(f);
    return ok;
}

// --- Som escolhido na aba Interface ----------------------------------------------------
// Regras de um som próprio (pasta PT_SFX_DIR): .wav PCM 16 bits, 48 kHz, mono ou
// estéreo, de 3 a 10 s. O pop-up dura o tempo do som (popup::total_ms).
constexpr uint32_t kSfxMinMs = 3000, kSfxMaxMs = 10000;
constexpr const char* kSfxDefault = "conquista.wav";  // o som que vem no pacote

enum class SfxCheck { OK, NOT_WAV, NOT_PCM16, RATE, CHANNELS, TOO_SHORT, TOO_LONG };

inline SfxCheck check_sfx(bool parsed, const Wav& w) {
    if (!parsed) return SfxCheck::NOT_WAV;
    if (w.format != 1 || w.bits != 16) return SfxCheck::NOT_PCM16;
    if (w.rate != 48000) return SfxCheck::RATE;
    if (w.channels != 1 && w.channels != 2) return SfxCheck::CHANNELS;
    if (w.ms() < kSfxMinMs) return SfxCheck::TOO_SHORT;
    if (w.ms() > kSfxMaxMs) return SfxCheck::TOO_LONG;
    return SfxCheck::OK;
}

// Nome guardado no config.ini: só um arquivo .wav dentro da pasta (sem "/" nem "..").
inline bool sfx_name_ok(const char* name) {
    size_t n = strlen(name);
    if (n < 5 || strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) return false;
    return !strcasecmp(name + n - 4, ".wav");
}

// Caminho do som: o escolhido (`name`) ou o padrão ("" ou nome inválido).
inline std::string sfx_path(const char* name) {
    return std::string(PT_SFX_DIR "/") + (name && sfx_name_ok(name) ? name : kSfxDefault);
}

struct SfxFile {
    std::string name;
    Wav wav;
    SfxCheck check = SfxCheck::NOT_WAV;
};

// Arquivos .wav da pasta (menos o padrão), em ordem alfabética, já verificados.
inline std::vector<SfxFile> list_sfx(const char* dir = PT_SFX_DIR) {
    std::vector<SfxFile> out;
    DIR* d = opendir(dir);
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.' || !sfx_name_ok(e->d_name) || !strcasecmp(e->d_name, kSfxDefault)) continue;
        SfxFile f;
        f.name = e->d_name;
        bool parsed = read_wav((std::string(dir) + "/" + f.name).c_str(), f.wav);
        f.check = check_sfx(parsed, f.wav);
        out.push_back(f);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const SfxFile& a, const SfxFile& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

}  // namespace pt
