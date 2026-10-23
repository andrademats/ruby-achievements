// Ícone da conquista no pop-up: PNG/JPEG do SD (o caminho vem do sysmodule) reduzido
// para popup::kIcon com os cantos arredondados.
#include <switch.h>

#include "popup_anim.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

// Imagens maiores que isto não são abertas (heap pequeno do overlay); os ícones do
// AchieveSet Studio têm 128x128.
constexpr int kMaxSide = 1024;

bool load_popup_icon(const char* path, u8* out) {
    int w = 0, h = 0, n = 0;
    if (!path || !*path || !stbi_info(path, &w, &h, &n) || w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide)
        return false;
    u8* rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba) return false;
    popup::fit_icon(rgba, w, h, out, popup::kIcon, popup::kIconRadius);
    stbi_image_free(rgba);
    return true;
}
