#include "icon_image.hpp"

#include <turbojpeg.h>

#include <borealis/extern/nanovg/stb_image.h>  // implementação no nanovg.c do Borealis
#include <vector>

#include "i18n.hpp"
#include "pt_util.h"

namespace icon {

bool make_jpeg(const std::string& src, const std::string& dst, std::string& error) {
    int w = 0, h = 0, ch = 0;
    unsigned char* rgba = stbi_load(src.c_str(), &w, &h, &ch, 4);
    if (!rgba) {
        error = trf("Não foi possível abrir a imagem %s (use PNG ou JPEG).", src.c_str());
        return false;
    }
    std::vector<unsigned char> rgb((size_t)kSize * kSize * 3);
    square_fit(rgba, w, h, kSize, rgb.data());
    stbi_image_free(rgba);

    tjhandle tj = tjInitCompress();
    unsigned char* jpeg = nullptr;
    unsigned long size = 0;
    bool ok = tj && tjCompress2(tj, rgb.data(), kSize, 0, kSize, TJPF_RGB, &jpeg, &size, TJSAMP_420, 92,
                                TJFLAG_ACCURATEDCT) == 0;
    if (ok) ok = pt_write_file_atomic(dst.c_str(), (const char*)jpeg, size) == 0;
    if (jpeg) tjFree(jpeg);
    if (tj) tjDestroy(tj);
    if (!ok) error = trf("Não foi possível gravar o ícone %s.", dst.c_str());
    return ok;
}

}  // namespace icon
