// Leitura real (stb_image do Borealis) + square_fit em JPEG e PNG com alfa.
#define STB_IMAGE_IMPLEMENTATION
#include <borealis/extern/nanovg/stb_image.h>
#include <cstdio>
#include <vector>
#include "icon_image.hpp"

int main(int argc, char** argv) {
    int fails = 0;
    for (int i = 1; i < argc; i++) {
        int w = 0, h = 0, ch = 0;
        unsigned char* p = stbi_load(argv[i], &w, &h, &ch, 4);
        std::vector<unsigned char> out((size_t)icon::kSize * icon::kSize * 3);
        bool ok = p != nullptr;
        if (ok) icon::square_fit(p, w, h, icon::kSize, out.data());
        printf("%-40s %dx%d c%d -> %s\n", argv[i], w, h, ch, ok ? "ok" : "FALHOU");
        if (ok && i == argc - 1) {  // PNG de teste: canto transparente, centro vermelho
            const unsigned char* c = &out[0];
            const unsigned char* m = &out[((size_t)128 * icon::kSize + 128) * 3];
            bool okc = c[0] == 255 && c[1] == 255 && m[0] > 240 && m[1] < 15;
            printf("%-40s %s\n", "PNG: alfa sobre branco, centro vermelho", okc ? "ok" : "FALHOU");
            fails += !okc;
        }
        fails += !ok;
        stbi_image_free(p);
    }
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
