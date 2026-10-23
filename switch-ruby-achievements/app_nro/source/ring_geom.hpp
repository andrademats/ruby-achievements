#pragma once
// Geometria da borda giratória (tico::ring): ponto do contorno a uma fração u
// (0..1) do perímetro, no sentido horário da tela (y cresce para baixo). Sem
// Borealis/NanoVG: testável no PC (ring_test).
#include <algorithm>
#include <cmath>

namespace tico {
namespace geom {

constexpr float kPi = 3.14159265f;

struct Pt {
    float x, y;
};

// Retângulo arredondado; u = 0 no fim do arco de cima à esquerda.
inline Pt rrect_point(float x, float y, float w, float h, float r, float u) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2));
    const float hw = w - 2 * r, hh = h - 2 * r, arc = kPi / 2 * r;
    const float len[8] = {hw, arc, hh, arc, hw, arc, hh, arc};
    float total = 2 * hw + 2 * hh + 4 * arc;
    float s = (u - std::floor(u)) * total;
    int k = 0;
    while (k < 7 && s > len[k]) s -= len[k++];
    float f = len[k] > 0 ? std::min(1.0f, s / len[k]) : 0;
    auto corner = [&](float cx, float cy, float a0) {
        float ang = a0 + f * kPi / 2;
        return Pt{cx + std::cos(ang) * r, cy + std::sin(ang) * r};
    };
    switch (k) {
        case 0: return {x + r + f * hw, y};
        case 1: return corner(x + w - r, y + r, -kPi / 2);
        case 2: return {x + w, y + r + f * hh};
        case 3: return corner(x + w - r, y + h - r, 0);
        case 4: return {x + w - r - f * hw, y + h};
        case 5: return corner(x + r, y + h - r, kPi / 2);
        case 6: return {x, y + h - r - f * hh};
        default: return corner(x + r, y + r, kPi);
    }
}

inline float rrect_perimeter(float w, float h, float r) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2));
    return 2 * (w - 2 * r) + 2 * (h - 2 * r) + 2 * kPi * r;
}

// Círculo; u = 0 em cima.
inline Pt circle_point(float cx, float cy, float r, float u) {
    float ang = -kPi / 2 + u * 2 * kPi;
    return Pt{cx + std::cos(ang) * r, cy + std::sin(ang) * r};
}

// --- Vértices fixos do contorno ---------------------------------------------------
// O caminho de cada trecho colorido passa SEMPRE pelos mesmos vértices (frações u
// calculadas só pela forma, nunca pela fase da luz); só as duas pontas do trecho
// andam, e elas caem exatamente sobre a curva. Antes os pontos eram amostrados a
// partir da ponta da luz: a cada quadro o polígono das quinas mudava e, nos
// retângulos compridos (passo de ~6 px num arco de ~20 px), a quina tremia e
// perdia a curvatura (2026-10-20).
constexpr int kMaxKnots = 128;
constexpr int kArcSteps = 8;  // segmentos por quarto de círculo (11,25° cada)

// Retângulo arredondado: começo e fim de cada lado reto e kArcSteps-1 pontos em
// cada arco, em ordem crescente de u (0..1). Lados retos não precisam de pontos
// no meio. Retorna quantos.
inline int rrect_knots(float w, float h, float r, float out[kMaxKnots]) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2));
    const float hw = w - 2 * r, hh = h - 2 * r, arc = kPi / 2 * r;
    const float len[8] = {hw, arc, hh, arc, hw, arc, hh, arc};
    const float total = 2 * hw + 2 * hh + 4 * arc;
    int n = 0;
    if (!(total > 0)) {
        out[n++] = 0;
        return n;
    }
    float s = 0;
    auto push = [&](float u) {
        if (n < kMaxKnots && (n == 0 || u > out[n - 1] + 1e-6f) && u < 1.0f - 1e-6f) out[n++] = u;
    };
    for (int k = 0; k < 8; k++) {
        push(s / total);  // começo do segmento
        if (k % 2 == 1 && len[k] > 0)
            for (int j = 1; j < kArcSteps; j++) push((s + len[k] * j / kArcSteps) / total);
        s += len[k];
    }
    return n;
}

// Círculo: pontos igualmente espaçados (um a cada ~4 px, entre 32 e 96).
inline int circle_knots(float r, float out[kMaxKnots]) {
    int n = (int)(2 * kPi * r / 4);
    n = std::max(32, std::min(96, n));
    for (int i = 0; i < n; i++) out[i] = (float)i / n;
    return n;
}

// Frações u do caminho de um trecho de u0 a u1 (u1 - u0 <= 1, valores fora de
// 0..1 dão a volta): u0, os vértices fixos estritamente entre as pontas (em
// ordem, somando 1 depois da volta) e u1. Retorna quantos.
constexpr int kMaxPathPoints = kMaxKnots + 2;
inline int piece_path(const float* knots, int n, float u0, float u1, float out[kMaxPathPoints]) {
    int m = 0;
    out[m++] = u0;
    const float base = std::floor(u0);
    const float eps = 1e-5f;
    for (int wrap = 0; wrap < 2; wrap++)
        for (int i = 0; i < n && m < kMaxPathPoints - 1; i++) {
            float t = base + knots[i] + wrap;
            if (t > u0 + eps && t < u1 - eps) out[m++] = t;
        }
    out[m++] = u1;
    return m;
}

// Quanto de escuro (0 = claro, 1 = escuro) a uma fração d (0..1) da volta atrás da
// ponta clara. Curva cosseno: contínua e suave em toda a volta (sem emenda onde o
// começo e o fim se encontram) e simétrica, metade clara e metade escura.
inline float ring_dark(float d) {
    d -= std::floor(d);
    return (1.0f - std::cos(d * 2 * kPi)) / 2;
}

// Fração (0..1) da volta para um relógio em microssegundos. Inteiros: com float o
// relógio do Switch (valor grande) só mudava de 128 em 128 s e a borda congelava.
inline float phase_from_usec(long long usec, long long period_usec) {
    if (period_usec <= 0) return 0;
    long long m = usec % period_usec;
    if (m < 0) m += period_usec;
    return (float)m / (float)period_usec;
}

// Trechos da volta: 8 de 1/8 cada, cobrindo a volta inteira. Cada trecho (cortado
// nas quinas, ver ring_segments) vira um traço com gradiente linear entre as cores
// das pontas (iguais às dos vizinhos, então não há degrau). Por baixo, um traço da
// cor média tapa as frestas das junções. O backend deko3d tem um bloco fixo de
// comandos por quadro e cada traço custa ~1 KB (3 passadas de estêncil): a borda
// tem de caber em poucos traços (crash de 2026-10-20 com até 180 traços por borda;
// 13 por borda = ~13 KB do bloco de 256 KB, ver dk_renderer.hpp).
struct Piece {
    float d0, d1;  // de d0 (mais perto da ponta) a d1, em fração da volta atrás da ponta
};
constexpr int kMaxRingPieces = 8;

inline int ring_pieces(Piece out[kMaxRingPieces]) {
    for (int i = 0; i < kMaxRingPieces; i++)
        out[i] = {(float)i / kMaxRingPieces, (float)(i + 1) / kMaxRingPieces};
    return kMaxRingPieces;
}

// --- Cortes fixos nas quinas ----------------------------------------------------------
// O gradiente linear de um traço pinta pela projeção na reta entre as pontas. Num
// retângulo comprido o trecho de 1/8 (~300 px num botão de 1176 px) muitas vezes dava
// a volta inteira na ponta curta, em U: a cor ficava parada nos lados retos, pulava na
// quina e a direção do gradiente virava de repente quando uma ponta passava a quina
// (o "tranco" nas quinas, 2026-10-20). Cortando os trechos também no meio de cada
// arco, todo pedaço anda sempre para a frente na direção da sua reta e a cor corre
// junto com o contorno.
constexpr int kMaxTurns = 4;

// Meio de cada arco do retângulo arredondado (frações u em ordem). Retorna quantos.
inline int rrect_turns(float w, float h, float r, float out[kMaxTurns]) {
    r = std::max(0.0f, std::min(r, std::min(w, h) / 2));
    const float hw = w - 2 * r, hh = h - 2 * r, arc = kPi / 2 * r;
    const float len[8] = {hw, arc, hh, arc, hw, arc, hh, arc};
    const float total = 2 * hw + 2 * hh + 4 * arc;
    if (!(total > 0)) return 0;
    int n = 0;
    float s = 0;
    for (int k = 0; k < 8; k++) {
        if (k % 2 == 1) out[n++] = (s + len[k] / 2) / total;
        s += len[k];
    }
    return n;
}

// Pedaços desenhados: os 8 trechos da volta (de head - 1 a head, a cada 1/8)
// cortados também nos `turns`. Cada pedaço vai de u0 a u1 (u0 < u1); a cor em
// cada ponta é a da distância até a ponta clara, d = head - u.
struct Segment {
    float u0, u1;
};
constexpr int kMaxRingSegments = kMaxRingPieces + kMaxTurns;
constexpr int kMaxRingStrokes = 1 + kMaxRingSegments;  // base + pedaços

inline int ring_segments(float head, const float* turns, int nt, Segment out[kMaxRingSegments]) {
    float cuts[kMaxRingPieces + 1 + kMaxTurns];
    int n = 0;
    for (int i = 0; i <= kMaxRingPieces; i++) cuts[n++] = head - 1 + (float)i / kMaxRingPieces;
    for (int i = 0; i < nt && i < kMaxTurns; i++) {
        float t = turns[i] + std::floor(head - turns[i]);  // mesmo ponto, dentro de (head - 1, head]
        bool near = false;
        for (int j = 0; j < n; j++) near = near || std::fabs(cuts[j] - t) < 1e-5f;
        if (!near) cuts[n++] = t;
    }
    std::sort(cuts, cuts + n);
    int m = 0;
    for (int i = 0; i + 1 < n; i++)
        if (cuts[i + 1] - cuts[i] > 1e-6f) out[m++] = {cuts[i], cuts[i + 1]};
    return m;
}

}  // namespace geom
}  // namespace tico
