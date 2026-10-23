#pragma once
// Ícone do forwarder fantasma: o instalador (forwarder/owo.cpp) só aceita JPEG
// 256x256, e as imagens do SD ou do SteamGridDB vêm em PNG/JPEG de qualquer tamanho.
#include <algorithm>
#include <string>

namespace icon {

constexpr int kSize = 256;

// Converte `src` (PNG ou JPEG) em JPEG kSize x kSize gravado em `dst`: recorte central
// quadrado, redução pela média e transparência sobre branco. Falha => `error`.
bool make_jpeg(const std::string& src, const std::string& dst, std::string& error);

// Parte pura (testada no PC): RGBA w x h -> RGB size x size. Recorta o quadrado central;
// cada pixel de saída é a média dos pixels de origem que ele cobre (o mais próximo, se a
// imagem for menor que `size`). Alfa compõe sobre branco.
inline void square_fit(const unsigned char* rgba, int w, int h, int size, unsigned char* rgb) {
    const int side = std::min(w, h), x0 = (w - side) / 2, y0 = (h - side) / 2;
    const double scale = (double)side / size;
    for (int oy = 0; oy < size; oy++) {
        int ya = y0 + (int)(oy * scale), yb = std::max(ya + 1, y0 + (int)((oy + 1) * scale));
        for (int ox = 0; ox < size; ox++) {
            int xa = x0 + (int)(ox * scale), xb = std::max(xa + 1, x0 + (int)((ox + 1) * scale));
            double acc[3] = {0, 0, 0};
            int n = 0;
            for (int y = ya; y < yb && y < y0 + side; y++)
                for (int x = xa; x < xb && x < x0 + side; x++, n++) {
                    const unsigned char* p = rgba + ((size_t)y * w + x) * 4;
                    double a = p[3] / 255.0;
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a + 255 * (1 - a);
                }
            unsigned char* o = rgb + ((size_t)oy * size + ox) * 3;
            for (int c = 0; c < 3; c++) o[c] = (unsigned char)(n ? acc[c] / n + 0.5 : 255);
        }
    }
}

}  // namespace icon
