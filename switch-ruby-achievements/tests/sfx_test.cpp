// Som do pop-up (pt_wav.hpp: regras, nomes, pasta sfx/), escolha gravada pelo app
// (prefs) e o config.ini do assistente da primeira abertura e do overlay.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "i18n.hpp"
#include "prefs.hpp"
#include "pt_config.h"
#include "pt_paths.h"
#include "pt_util.h"
#include "pt_wav.hpp"
s64 g_fake_now = 1000;

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

// WAV com cabeçalho canônico e `ms` de silêncio.
static void wav(const char* name, int format, int channels, int rate, int bits, unsigned ms) {
    uint32_t data = (uint32_t)((uint64_t)rate * channels * (bits / 8) * ms / 1000);
    std::vector<uint8_t> b(44 + data, 0);
    auto u32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i)); };
    auto u16 = [&](size_t at, uint32_t v) { b[at] = (uint8_t)v, b[at + 1] = (uint8_t)(v >> 8); };
    memcpy(&b[0], "RIFF", 4);
    u32(4, 36 + data);
    memcpy(&b[8], "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, format);
    u16(22, channels);
    u32(24, rate);
    u32(28, rate * channels * bits / 8);
    u16(32, channels * bits / 8);
    u16(34, bits);
    memcpy(&b[36], "data", 4);
    u32(40, data);
    std::string p = std::string(PT_SFX_DIR "/") + name;
    FILE* f = fopen(p.c_str(), "wb");
    fwrite(b.data(), 1, b.size(), f);
    fclose(f);
}

static pt::SfxCheck check_of(const std::vector<pt::SfxFile>& l, const char* name) {
    for (auto& f : l)
        if (f.name == name) return f.check;
    return (pt::SfxCheck)-1;
}

int main() {
    // --- regras do arquivo
    wav("conquista.wav", 1, 2, 48000, 16, 5000);  // o padrão: fora da lista
    wav("ok_estereo.wav", 1, 2, 48000, 16, 4200);
    wav("Ok_mono.WAV", 1, 1, 48000, 16, 3000);
    wav("limite_10s.wav", 1, 2, 48000, 16, 10000);
    wav("curto.wav", 1, 2, 48000, 16, 2900);
    wav("longo.wav", 1, 2, 48000, 16, 10100);
    wav("44khz.wav", 1, 2, 44100, 16, 5000);
    wav("8bits.wav", 1, 2, 48000, 8, 5000);
    wav("float.wav", 3, 2, 48000, 32, 5000);
    wav("surround.wav", 1, 6, 48000, 16, 4000);
    FILE* f = fopen(PT_SFX_DIR "/lixo.wav", "wb");
    fputs("isto nao e um wav", f);
    fclose(f);
    f = fopen(PT_SFX_DIR "/musica.mp3", "wb");
    fputs("ID3", f);
    fclose(f);

    auto l = pt::list_sfx();
    check("lista: só .wav, sem o padrão (conquista.wav)", l.size() == 10 && check_of(l, "conquista.wav") == (pt::SfxCheck)-1);
    bool sorted = true;
    for (size_t i = 1; i < l.size(); i++) sorted &= strcasecmp(l[i - 1].name.c_str(), l[i].name.c_str()) < 0;
    check("lista em ordem alfabética (sem diferenciar maiúsculas)", sorted);
    check("PCM 16 bits 48 kHz estéreo de 4,2 s: aceito", check_of(l, "ok_estereo.wav") == pt::SfxCheck::OK);
    check("mono de exatamente 3 s: aceito (.WAV maiúsculo)", check_of(l, "Ok_mono.WAV") == pt::SfxCheck::OK);
    check("exatamente 10 s: aceito", check_of(l, "limite_10s.wav") == pt::SfxCheck::OK);
    check("2,9 s: curto demais", check_of(l, "curto.wav") == pt::SfxCheck::TOO_SHORT);
    check("10,1 s: longo demais", check_of(l, "longo.wav") == pt::SfxCheck::TOO_LONG);
    check("44,1 kHz: taxa errada", check_of(l, "44khz.wav") == pt::SfxCheck::RATE);
    check("8 bits: não é PCM 16", check_of(l, "8bits.wav") == pt::SfxCheck::NOT_PCM16);
    check("float 32: não é PCM 16", check_of(l, "float.wav") == pt::SfxCheck::NOT_PCM16);
    check("6 canais: recusado", check_of(l, "surround.wav") == pt::SfxCheck::CHANNELS);
    check("texto com extensão .wav: não é WAV", check_of(l, "lixo.wav") == pt::SfxCheck::NOT_WAV);

    // --- nomes guardados no config.ini
    check("nome: arquivo .wav simples", pt::sfx_name_ok("meu som.wav"));
    check("nome: sem pasta, '..' ou outra extensão",
          !pt::sfx_name_ok("../x.wav") && !pt::sfx_name_ok("a/b.wav") && !pt::sfx_name_ok("x.mp3") && !pt::sfx_name_ok(".wav"));
    check("caminho: vazio ou inválido = padrão",
          pt::sfx_path("") == PT_POPUP_SFX_PATH && pt::sfx_path("../../x.wav") == PT_POPUP_SFX_PATH);
    check("caminho: escolhido dentro de sfx/", pt::sfx_path("ok_estereo.wav") == PT_SFX_DIR "/ok_estereo.wav");

    // --- assistente da 1ª abertura: config.ini novo
    remove(PT_CONFIG_PATH);
    PtConfig c;
    pt_config_load(&c);
    check("1ª abertura: import_asked=0 (assistente aparece)", c.import_asked == 0);
    check("1ª abertura: padrões English, MM/DD/YYYY, som padrão, flood desligado",
          c.app_language == i18n::LANG_EN && c.app_date_format == i18n::DATE_MDY && !c.popup_sfx[0] && c.popup_flood == 0);
    // Passo 1 e 2 gravam na hora; a importação (passo 3) já sai no idioma escolhido.
    prefs::set_language(i18n::LANG_PT);
    prefs::set_date_format(i18n::DATE_DMY);
    pt_config_load(&c);
    check("passos 1 e 2 gravados, import_asked ainda 0 (fechar repete o assistente)",
          c.app_language == i18n::LANG_PT && c.app_date_format == i18n::DATE_DMY && c.import_asked == 0);
    check("passo 3 em português", !strcmp(tr("Importar"), "Importar") && i18n::format_day(2026, 10, 21, i18n::date_format()) == "21/10/2026");
    i18n::set_language(i18n::LANG_EN);
    check("passo 3 em inglês (se escolhido)", !strcmp(tr("Importar"), "Import"));
    c.import_asked = 1;
    pt_config_save_app(&c);

    // --- som escolhido no app e opção do overlay
    check("app grava o som escolhido", prefs::set_popup_sfx("ok_estereo.wav") && prefs::popup_sfx() == "ok_estereo.wav");
    check("app recusa nome com pasta", !prefs::set_popup_sfx("../x.wav") && prefs::popup_sfx() == "ok_estereo.wav");
    PtConfig o;
    pt_config_load(&o);
    o.popup_flood = 1;  // o overlay relê e grava
    check("overlay grava a proteção contra flood", pt_config_save_app(&o) == 0);
    pt_config_load(&c);
    check("tudo mantido: flood, som, idioma, data e import_asked",
          c.popup_flood == 1 && !strcmp(c.popup_sfx, "ok_estereo.wav") && c.app_language == i18n::LANG_PT &&
              c.app_date_format == i18n::DATE_DMY && c.import_asked == 1);
    check("app volta ao padrão", prefs::set_popup_sfx("") && prefs::popup_sfx().empty());
    pt_config_load(&c);
    check("flood continua ligado depois do app gravar", c.popup_flood == 1);

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
