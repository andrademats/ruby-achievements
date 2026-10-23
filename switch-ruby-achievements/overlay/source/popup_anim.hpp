#pragma once
// Pop-up de conquista: medidas, linha do tempo da animação (estilo Xbox 360), cantos
// arredondados e redução do ícone. Sem libtesla/libnx: testável no PC. O WAV fica em
// pt_wav.hpp (comum ao app, que valida o som escolhido).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "pt_wav.hpp"

namespace popup {

// --- Medidas (pixels do framebuffer do overlay = pixels de uma tela 1280x720) ----------
constexpr int kH = 64;           // altura do pop-up
constexpr int kW = 372;          // largura aberto (sem a descrição, mais curto que antes)
constexpr int kMarginX = 24;     // respiro da borda direita da tela
constexpr int kMarginY = 24;     // respiro da borda de cima
constexpr int kRadius = 16;      // cantos do pop-up
constexpr int kIcon = 44;        // ícone da conquista (era 48)
constexpr int kIconRadius = 10;  // cantos do ícone
constexpr int kPad = (kH - kIcon) / 2;
constexpr int kRarity = 34;      // troféu da raridade, à direita
// O troféu fica no centro do selo (kH/2 da borda direita): é ele que surge no selo e
// continua parado ali enquanto o pop-up se abre e se fecha.
constexpr int kRarityPad = (kH - kRarity) / 2;  // da borda direita até o troféu
constexpr int kTextX = kPad + kIcon + 12;       // texto, a partir da esquerda do pop-up
constexpr int kTextW = kW - kTextX - kRarityPad - kRarity - 12;  // espaço do texto até o troféu
static_assert(kRarityPad + kRarity / 2 == kH / 2, "troféu centrado no selo");

// --- Linha do tempo -------------------------------------------------------------------
// Como no Xbox 360: o selo (quadrado com o troféu da raridade) surge crescendo com um
// leve exagero, o pop-up se abre para a esquerda revelando o ícone e o texto (o troféu
// fica parado à direita), fica, se fecha de volta no selo e o selo some encolhendo. A duração total acompanha a do som (o som acaba junto).
constexpr uint32_t kPopMs = 350, kExpandMs = 420, kCollapseMs = 380, kShrinkMs = 260;
constexpr uint32_t kDefaultMs = 5000, kMinMs = pt::kSfxMinMs, kMaxMs = pt::kSfxMaxMs;

inline uint32_t total_ms(uint32_t sound_ms) {
    return sound_ms ? std::clamp(sound_ms, kMinMs, kMaxMs) : kDefaultMs;
}

inline float ease_out_cubic(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }
inline float ease_in_cubic(float t) { return t * t * t; }
// Passa um pouco do fim e volta (o "estalo" do selo).
inline float ease_out_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
}
inline float ease_in_back(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1;
    return c3 * t * t * t - c1 * t * t;
}

struct Frame {
    bool visible = false;
    float scale = 0;  // tamanho do selo (1 = normal), em torno do centro dele
    float alpha = 0;  // opacidade geral
    float width = 0;  // largura atual do pop-up (kH = só o selo, kW = aberto)
    bool text = false;  // texto e troféu (só com o pop-up abrindo/aberto/fechando)
};

inline Frame frame_at(uint32_t ms, uint32_t total) {
    Frame f;
    if (ms >= total) return f;
    f.visible = true;
    f.scale = 1;
    f.alpha = 1;
    f.width = kH;
    const uint32_t expand_end = kPopMs + kExpandMs;
    const uint32_t collapse_at = total - kShrinkMs - kCollapseMs, shrink_at = total - kShrinkMs;
    if (ms < kPopMs) {
        float t = (float)ms / kPopMs;
        f.scale = ease_out_back(t);
        f.alpha = std::min(1.0f, t * 2.5f);
    } else if (ms < expand_end) {
        f.width = kH + (kW - kH) * ease_out_cubic((float)(ms - kPopMs) / kExpandMs);
        f.text = true;
    } else if (ms < collapse_at) {
        f.width = kW;
        f.text = true;
    } else if (ms < shrink_at) {
        f.width = kH + (kW - kH) * (1 - ease_in_cubic((float)(ms - collapse_at) / kCollapseMs));
        f.text = true;
    } else {
        float t = (float)(ms - shrink_at) / kShrinkMs;
        f.scale = std::max(0.0f, 1 - ease_in_back(t));
        f.alpha = t < 0.5f ? 1 : (1 - t) * 2;
    }
    return f;
}

// --- Retângulo arredondado --------------------------------------------------------------
// Fração (0..1) do pixel de centro (px, py) coberta pelo retângulo arredondado: distância
// com sinal até a borda, suavizada em 1 px (bordas sem serrilhado).
inline float rrect_coverage(float px, float py, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    float qx = std::fabs(px - (x + w / 2)) - (w / 2 - r);
    float qy = std::fabs(py - (y + h / 2)) - (h / 2 - r);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
    return std::clamp(0.5f - d, 0.0f, 1.0f);
}

// --- Ícone --------------------------------------------------------------------------------
// RGBA w x h -> RGBA size x size: quadrado central, média por área (o mais próximo se for
// menor) e cantos arredondados no alfa.
inline void fit_icon(const uint8_t* src, int w, int h, uint8_t* dst, int size, float radius) {
    const int side = std::min(w, h), x0 = (w - side) / 2, y0 = (h - side) / 2;
    const double scale = (double)side / size;
    for (int oy = 0; oy < size; oy++) {
        int ya = y0 + (int)(oy * scale), yb = std::max(ya + 1, y0 + (int)((oy + 1) * scale));
        for (int ox = 0; ox < size; ox++) {
            int xa = x0 + (int)(ox * scale), xb = std::max(xa + 1, x0 + (int)((ox + 1) * scale));
            double acc[4] = {0, 0, 0, 0};
            int n = 0;
            for (int y = ya; y < yb && y < y0 + side; y++)
                for (int x = xa; x < xb && x < x0 + side; x++, n++) {
                    const uint8_t* p = src + ((size_t)y * w + x) * 4;
                    double a = p[3];
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a;  // cor pesada pelo alfa
                    acc[3] += a;
                }
            uint8_t* o = dst + ((size_t)oy * size + ox) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(acc[3] > 0 ? acc[c] / acc[3] + 0.5 : 0);
            float cov = rrect_coverage(ox + 0.5f, oy + 0.5f, 0, 0, (float)size, (float)size, radius);
            o[3] = (uint8_t)(n ? acc[3] / n * cov + 0.5 : 0);
        }
    }
}

}  // namespace popup
