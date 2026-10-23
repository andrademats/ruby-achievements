// Parte pura do ícone do forwarder fantasma (icon::square_fit).
#include <cstdio>
#include <vector>
#include "icon_image.hpp"

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

static std::vector<unsigned char> img(int w, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    std::vector<unsigned char> v((size_t)w * h * 4);
    for (size_t i = 0; i < v.size(); i += 4) v[i] = r, v[i + 1] = g, v[i + 2] = b, v[i + 3] = a;
    return v;
}

int main() {
    const int S = icon::kSize;
    std::vector<unsigned char> out((size_t)S * S * 3);
    auto px = [&](int x, int y, int c) { return (int)out[((size_t)y * S + x) * 3 + c]; };

    // 1024x1024 vermelho opaco -> tudo vermelho.
    auto a = img(1024, 1024, 200, 10, 10, 255);
    icon::square_fit(a.data(), 1024, 1024, S, out.data());
    check("redução 1024 -> 256 mantém a cor", px(0, 0, 0) == 200 && px(255, 255, 1) == 10 && px(128, 77, 2) == 10);

    // Transparente -> branco.
    auto t = img(512, 512, 0, 0, 0, 0);
    icon::square_fit(t.data(), 512, 512, S, out.data());
    check("transparência vira branco", px(10, 10, 0) == 255 && px(200, 100, 2) == 255);

    // Meio transparente preto -> cinza médio.
    auto h = img(512, 512, 0, 0, 0, 128);
    icon::square_fit(h.data(), 512, 512, S, out.data());
    check("alfa 50% preto sobre branco ~127", px(50, 50, 0) >= 125 && px(50, 50, 0) <= 129);

    // Retângulo 600x300: metade esquerda azul, direita verde; recorte central 300x300 (x 150..449).
    auto r = img(600, 300, 0, 0, 255, 255);
    for (int y = 0; y < 300; y++)
        for (int x = 300; x < 600; x++) {
            unsigned char* p = &r[((size_t)y * 600 + x) * 4];
            p[0] = 0, p[1] = 255, p[2] = 0;
        }
    icon::square_fit(r.data(), 600, 300, S, out.data());
    check("recorte central: esquerda azul", px(5, 128, 2) == 255 && px(5, 128, 1) == 0);
    check("recorte central: direita verde", px(250, 128, 1) == 255 && px(250, 128, 2) == 0);
    check("recorte central: divisa no meio", px(126, 10, 2) == 255 && px(130, 10, 1) == 255);

    // Imagem menor (64x64) -> amplia (vizinho mais próximo), sem ler fora.
    auto s = img(64, 64, 30, 60, 90, 255);
    icon::square_fit(s.data(), 64, 64, S, out.data());
    check("ampliação 64 -> 256", px(0, 0, 0) == 30 && px(255, 255, 2) == 90);

    // Faixa estreita 1x900 -> recorta 1x1 e preenche.
    auto n = img(1, 900, 5, 6, 7, 255);
    icon::square_fit(n.data(), 1, 900, S, out.data());
    check("imagem 1x900 não estoura", px(255, 255, 0) == 5);

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
