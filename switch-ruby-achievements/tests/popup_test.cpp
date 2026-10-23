// Pop-up do overlay (popup_anim.hpp): linha do tempo estilo Xbox 360, medidas, cantos,
// WAV provisório e ícone. Uso: popup_test <conquista.wav>
#include <cstdio>
#include <vector>
#include "popup_anim.hpp"
typedef uint8_t u8;
namespace art {
#include "trophies.inc"
}

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

int main(int argc, char** argv) {
    using namespace popup;
    // --- medidas
    check("cabe na camada do overlay (448 px) com o respiro", kW + kMarginX <= 448);
    check("texto com espaço para títulos longos (>= 240 px)", kTextW >= 240);
    check("ícone menor que o antigo (48) e com cantos", kIcon < 48 && kIconRadius > 0);
    check("texto não invade o troféu", kTextX + kTextW + 12 + kRarity + kRarityPad == kW);
    // O selo mostra o troféu da raridade e ele fica parado ao abrir: o centro do troféu
    // no pop-up aberto (kW - kRarityPad - kRarity/2 a partir da esquerda) é o centro do selo.
    check("troféu no centro do selo e no mesmo lugar aberto",
          kRarityPad * 2 + kRarity == kH && kW - kRarityPad - kRarity / 2 == kW - kH / 2);
    check("troféu cabe no selo", kRarity <= kH && kRarityPad >= 10);

    // --- linha do tempo
    const uint32_t T = total_ms(5000);
    check("duração = a do som (5000 ms)", T == 5000);
    check("sem som: 5 s; som curto: 3 s; longo: 10 s",
          total_ms(0) == kDefaultMs && total_ms(800) == kMinMs && total_ms(60000) == kMaxMs);
    float max_scale = 0;
    bool grows = true;
    for (uint32_t ms = 0; ms < kPopMs; ms++) max_scale = std::max(max_scale, frame_at(ms, T).scale);
    check("selo surge com estalo (passa de 1 e volta)", max_scale > 1.05f && max_scale < 1.2f);
    check("selo começa do zero, sem texto", frame_at(0, T).scale < 0.01f && !frame_at(0, T).text);
    float prev = kH;
    for (uint32_t ms = kPopMs; ms < kPopMs + kExpandMs; ms++) {
        float w = frame_at(ms, T).width;
        grows &= w >= prev - 1e-3f;
        prev = w;
    }
    check("abre para a esquerda sem voltar (largura só cresce)", grows);
    check("aberto no meio, largura cheia, com texto",
          frame_at(T / 2, T).width == kW && frame_at(T / 2, T).text && frame_at(T / 2, T).scale == 1);
    bool shrinks = true;
    prev = kW;
    for (uint32_t ms = T - kShrinkMs - kCollapseMs; ms < T - kShrinkMs; ms++) {
        float w = frame_at(ms, T).width;
        shrinks &= w <= prev + 1e-3f;
        prev = w;
    }
    check("fecha de volta no selo (largura só diminui)", shrinks && prev < kH + 3);
    check("some no fim exato do som", frame_at(T - 1, T).visible && !frame_at(T, T).visible);
    // Sem saltos: de 1 em 1 ms nada anda mais que 3 px (a 60 quadros/s, ~16 ms por quadro).
    float jump = 0;
    Frame a = frame_at(0, T);
    for (uint32_t ms = 1; ms < T; ms++) {
        Frame b = frame_at(ms, T);
        if (a.text == b.text) jump = std::max(jump, std::fabs(b.width - a.width) + kH * std::fabs(b.scale - a.scale));
        a = b;
    }
    check("movimento contínuo (máx. 3 px por ms)", jump <= 3);
    check("emendas das fases: selo cheio ao começar a abrir e ao terminar de fechar",
          frame_at(kPopMs - 1, T).scale > 0.99f && frame_at(kPopMs, T).width == kH &&
              frame_at(T - kShrinkMs, T).scale == 1);

    // --- cantos
    check("cobertura: centro 1, fora 0",
          rrect_coverage(50, 32, 0, 0, 100, 64, 16) == 1 && rrect_coverage(-2, 32, 0, 0, 100, 64, 16) == 0);
    check("cobertura: canto vivo cortado", rrect_coverage(0.5f, 0.5f, 0, 0, 100, 64, 16) == 0);
    float edge = rrect_coverage(0.5f, 32, 0, 0, 100, 64, 16);
    check("cobertura: borda reta suavizada (meio pixel)", edge > 0.9f);
    float diag = rrect_coverage(16 - 16 * 0.7071f, 16 - 16 * 0.7071f, 0, 0, 100, 64, 16);
    check("cobertura: arco com meio pixel na curva", diag > 0.3f && diag < 0.7f);

    // --- WAV
    if (argc > 1) {
        FILE* f = fopen(argv[1], "rb");
        std::vector<uint8_t> d;
        if (f) {
            uint8_t b[4096];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
            fclose(f);
        }
        pt::Wav w;
        bool ok = !d.empty() && pt::parse_wav(d.data(), std::min<size_t>(512, d.size()), d.size(), w);
        check("som provisório: WAV PCM 16 bits 48 kHz tocável", ok && w.playable());
        check("som provisório dura exatamente o pop-up padrão", ok && w.ms() == kDefaultMs);
        check("som provisório termina em silêncio",
              ok && d.size() >= 4 && d[d.size() - 1] == 0 && d[d.size() - 2] == 0);
        pt::Wav cut;
        pt::parse_wav(d.data(), 512, d.size() / 2, cut);
        check("arquivo cortado: dados limitados ao tamanho", cut.data_size < w.data_size);
    }
    pt::Wav bad;
    const uint8_t junk[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '};
    check("arquivo que não é WAV recusado", !pt::parse_wav(junk, sizeof junk, 16, bad));
    std::vector<uint8_t> w44(64, 0);
    memcpy(&w44[0], "RIFF", 4);
    memcpy(&w44[8], "WAVEfmt ", 8);
    w44[16] = 16;
    w44[20] = 1;
    w44[22] = 2;
    w44[24] = 0x44, w44[25] = 0xAC;  // 44100
    w44[34] = 16;
    memcpy(&w44[36], "data", 4);
    pt::Wav x;
    check("44,1 kHz lido mas não tocado (mudo, sem chiado)",
          pt::parse_wav(w44.data(), w44.size(), w44.size(), x) && x.rate == 44100 && !x.playable());

    // --- ícone
    std::vector<uint8_t> src(128 * 128 * 4, 255), out(kIcon * kIcon * 4);
    fit_icon(src.data(), 128, 128, out.data(), kIcon, kIconRadius);
    auto px = [&](int x, int y, int c) { return out[((size_t)y * kIcon + x) * 4 + c]; };
    check("ícone: canto transparente (arredondado)", px(0, 0, 3) == 0);
    check("ícone: miolo opaco", px(kIcon / 2, kIcon / 2, 3) == 255 && px(kIcon / 2, 0, 3) > 200);
    check("ícone: cor mantida", px(kIcon / 2, kIcon / 2, 0) == 255);

    // --- ícone padrão (conquista sem imagem): fundo branco com estrela, não o troféu
    auto sp = [&](int x, int y, int c) { return (int)art::kStarIcon[((size_t)y * kIcon + x) * 4 + c]; };
    check("ícone padrão: canto transparente (arredondado)", sp(0, 0, 3) == 0);
    check("ícone padrão: fundo branco", sp(kIcon / 2, 3, 0) > 245 && sp(kIcon / 2, 3, 1) > 245 && sp(3, kIcon / 2, 2) > 245);
    check("ícone padrão: estrela dourada no centro", sp(kIcon / 2, kIcon / 2, 0) > 200 && sp(kIcon / 2, kIcon / 2, 2) < 90);
    check("ícone padrão diferente dos troféus", sizeof(art::kStarIcon) == (size_t)kIcon * kIcon * 4 &&
                                                   memcmp(art::kStarIcon, art::kTrophyBronze, 64) != 0);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
