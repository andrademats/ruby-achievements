// Geometria da borda giratória: contorno contínuo, sentido horário, no lugar
// certo em quadrados (cards da grade), retângulos e círculos; faixa escura curta.
#include "ring_geom.hpp"
#include <cmath>
#include <stdio.h>
using namespace tico::geom;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

// Distância do ponto ao contorno do retângulo arredondado (0 = em cima dele).
static float off_border(float x, float y, float w, float h, float r, Pt p) {
    r = std::min(r, std::min(w, h) / 2);  // como rrect_point
    float cx = std::min(std::max(p.x, x + r), x + w - r), cy = std::min(std::max(p.y, y + r), y + h - r);
    float dx = p.x - cx, dy = p.y - cy;
    return std::fabs(std::sqrt(dx * dx + dy * dy) - r);
}

static void shape(const char* name, float x, float y, float w, float h, float r) {
    const int n = 400;
    float per = rrect_perimeter(w, h, r);
    float max_step = 0, max_off = 0;
    double turn = 0;  // soma do produto vetorial em relação ao centro: > 0 = horário com y para baixo
    float cx = x + w / 2, cy = y + h / 2;
    Pt prev = rrect_point(x, y, w, h, r, 0);
    for (int i = 1; i <= n; i++) {
        Pt p = rrect_point(x, y, w, h, r, (float)i / n);
        float step = std::hypot(p.x - prev.x, p.y - prev.y);
        max_step = std::max(max_step, step);
        max_off = std::max(max_off, off_border(x, y, w, h, r, p));
        turn += (prev.x - cx) * (p.y - cy) - (prev.y - cy) * (p.x - cx);
        prev = p;
    }
    char buf[128];
    snprintf(buf, sizeof buf, "%s: contínuo (passo <= 2x o esperado)", name);
    check(buf, max_step <= 2.0f * per / n + 0.01f);
    snprintf(buf, sizeof buf, "%s: todos os pontos no contorno", name);
    check(buf, max_off < 0.05f);
    snprintf(buf, sizeof buf, "%s: sentido horário", name);
    check(buf, turn > 0);
    Pt a = rrect_point(x, y, w, h, r, 0), b = rrect_point(x, y, w, h, r, 1);
    snprintf(buf, sizeof buf, "%s: fecha a volta", name);
    check(buf, std::hypot(a.x - b.x, a.y - b.y) < 0.01f);
    Pt q = rrect_point(x, y, w, h, r, 0.01f);
    snprintf(buf, sizeof buf, "%s: começa indo para a direita no topo", name);
    check(buf, q.x > a.x && std::fabs(q.y - y) < 0.01f);
}

int main() {
    // Card da grade: 132 px + borda (traço de 5 px, 1 px para dentro).
    shape("card 7x3", -1.5f, -1.5f, 135, 135, 17.5f);
    shape("célula larga", 0, 0, 1176, 62, 12);
    shape("quadrado sem raio", 0, 0, 100, 100, 0);
    shape("raio maior que o lado", 0, 0, 40, 20, 50);

    Pt top = circle_point(0, 0, 10, 0), right = circle_point(0, 0, 10, 0.25f);
    check("círculo: começa em cima", std::fabs(top.x) < 0.01f && std::fabs(top.y + 10) < 0.01f);
    check("círculo: 1/4 de volta vai para a direita (horário)", std::fabs(right.x - 10) < 0.01f);

    // Cor: suave em toda a volta (sem emenda) e claro/escuro na mesma proporção.
    int dark = 0;
    float max_jump = 0;
    for (int i = 0; i < 1000; i++) {
        float d = i / 1000.0f;
        if (ring_dark(d) > 0.5f) dark++;
        max_jump = std::max(max_jump, std::fabs(ring_dark(d + 0.001f) - ring_dark(d)));
    }
    check("claro e escuro na mesma proporção (50% +- 1%)", dark >= 490 && dark <= 510);
    check("sem degrau em nenhum ponto (variação < 1% por 0,1% da volta)", max_jump < 0.01f);
    check("emenda invisível (d = 1 -> 0 com a mesma cor)", std::fabs(ring_dark(0.99999f) - ring_dark(0.0f)) < 1e-3f);
    check("ponta clara e lado oposto escuro", ring_dark(0) < 1e-6f && ring_dark(0.5f) > 0.999f);

    // Orçamento de traços (crash de 2026-10-20: até 180 traços por borda estouravam
    // o bloco de comandos do deko3d por quadro).
    Piece pieces[kMaxRingPieces];
    int np = ring_pieces(pieces);
    check("no máximo 13 traços por borda (base + 8 trechos + 4 cortes)", np <= kMaxRingPieces && kMaxRingStrokes <= 13);
    bool ordered = true;
    for (int i = 0; i < np; i++) {
        if (!(pieces[i].d0 >= 0 && pieces[i].d1 <= 1 && pieces[i].d0 < pieces[i].d1)) ordered = false;
        if (i && std::fabs(pieces[i].d0 - pieces[i - 1].d1) > 1e-6f) ordered = false;  // sem buraco nem sobra
    }
    check("trechos contíguos, em ordem e dentro da volta", ordered);
    check("trechos cobrem a volta inteira", std::fabs(pieces[0].d0) < 1e-6f && std::fabs(pieces[np - 1].d1 - 1) < 1e-6f);
    // Gradiente linear por trecho: o erro contra a curva fica pequeno.
    float max_err = 0;
    for (int k = 0; k < np; k++)
        for (int j = 0; j <= 20; j++) {
            float t = j / 20.0f, d = pieces[k].d0 + (pieces[k].d1 - pieces[k].d0) * t;
            float lin = ring_dark(pieces[k].d0) + (ring_dark(pieces[k].d1) - ring_dark(pieces[k].d0)) * t;
            max_err = std::max(max_err, std::fabs(lin - ring_dark(d)));
        }
    check("trechos lineares seguem a curva (erro < 6%)", max_err < 0.06f);
    // Relógio: a fase precisa andar a cada quadro mesmo com valores grandes.
    const long long period = 3600000;  // 3,6 s
    long long big = 1792481947LL * 1000000LL;  // relógio "desde 1970" em us
    float p0 = phase_from_usec(big, period), p1 = phase_from_usec(big + 16667, period);  // 1 quadro depois
    check("fase anda 1 quadro com relógio grande", p1 > p0 && std::fabs((p1 - p0) - 16667.0f / period) < 1e-4f);
    float f0 = (float)(big / 1e6), f1 = (float)((big + 16667) / 1e6);
    check("(prova do bug) float de segundos grandes não anda", f0 == f1);
    check("fase volta a 0 a cada período", phase_from_usec(period * 5, period) == 0.0f);
    // Quinas estáticas (2026-10-20): os vértices do caminho não dependem da fase.
    struct Box { const char* name; float w, h, r; };
    const Box boxes[] = {{"botão largo 1176x62", 1176, 62, 14.5f}, {"card 135x135", 135, 135, 17.5f},
                         {"linha de modal 624x64", 629, 69, 14.5f}, {"sem raio", 300, 40, 0}};
    for (const Box& b : boxes) {
        float knots[kMaxKnots];
        int nk = rrect_knots(b.w, b.h, b.r, knots);
        bool sorted = nk > 0 && knots[0] == 0.0f;
        for (int i = 1; i < nk; i++) sorted = sorted && knots[i] > knots[i - 1] && knots[i] < 1.0f;
        // Maior distância entre o meio de um segmento e a curva verdadeira (flecha).
        float sag = 0;
        for (int i = 0; i < nk; i++) {
            float ua = knots[i], ub = i + 1 < nk ? knots[i + 1] : 1.0f;
            Pt pa = rrect_point(0, 0, b.w, b.h, b.r, ua), pb = rrect_point(0, 0, b.w, b.h, b.r, ub);
            Pt mid{(pa.x + pb.x) / 2, (pa.y + pb.y) / 2};
            sag = std::max(sag, off_border(0, 0, b.w, b.h, b.r, mid));
        }
        // Vértices internos de cada trecho, para 50 fases diferentes: todos são vértices fixos.
        bool fixed = true, ends_on_curve = true, monotonic = true;
        Piece ps[kMaxRingPieces];
        int np2 = ring_pieces(ps);
        for (int f = 0; f < 50; f++) {
            float head = f / 50.0f + 0.0123f;
            for (int k = 0; k < np2; k++) {
                float path[kMaxPathPoints];
                float u0 = head - ps[k].d1, u1 = head - ps[k].d0;
                int m = piece_path(knots, nk, u0, u1, path);
                if (path[0] != u0 || path[m - 1] != u1) ends_on_curve = false;
                for (int i = 1; i < m; i++) monotonic = monotonic && path[i] > path[i - 1];
                for (int i = 1; i + 1 < m; i++) {
                    float t = path[i] - std::floor(path[i]);
                    bool found = false;
                    for (int j = 0; j < nk; j++) found = found || std::fabs(knots[j] - t) < 1e-6f;
                    if (!found) fixed = false;
                }
            }
        }
        char buf[128];
        snprintf(buf, sizeof buf, "%s: vértices fixos em ordem (%d)", b.name, nk);
        check(buf, sorted && nk <= kMaxKnots);
        snprintf(buf, sizeof buf, "%s: quina lisa (flecha %.3f px < 0,1)", b.name, sag);
        check(buf, sag < 0.1f);
        snprintf(buf, sizeof buf, "%s: só as pontas do trecho andam com a luz", b.name);
        check(buf, fixed && ends_on_curve && monotonic);
    }
    {
        // (prova do bug) amostragem antiga a partir da ponta: passo de perímetro/6
        // no botão largo deixa flecha visível e muda com a fase.
        float w = 1176, h = 62, r = 14.5f, per = rrect_perimeter(w, h, r);
        int full = std::max(24, std::min(240, (int)(per / 6)));
        float worst = 0;
        for (int f = 0; f < 20; f++) {
            float u0 = f / 20.0f;
            for (int i = 0; i < full; i++) {
                Pt a = rrect_point(0, 0, w, h, r, u0 + (float)i / full), c = rrect_point(0, 0, w, h, r, u0 + (float)(i + 1) / full);
                worst = std::max(worst, off_border(0, 0, w, h, r, Pt{(a.x + c.x) / 2, (a.y + c.y) / 2}));
            }
        }
        check("(prova do bug) amostragem pela fase deformava a quina (> 0,5 px)", worst > 0.5f);
    }
    // Luz corrida nas quinas (2026-10-20, 10ª etapa): cada pedaço desenhado com
    // gradiente linear precisa andar sempre para a frente na direção da sua reta
    // (senão a cor para nos lados retos e pula na quina) e a cor projetada precisa
    // acompanhar o comprimento do contorno.
    struct Shape { const char* name; float w, h, r; };
    const Shape shapes[] = {{"botão largo 1176x62", 1176, 62, 14.5f}, {"linha de modal 629x69", 629, 69, 14.5f},
                            {"aba lateral 237x65", 237, 65, 16.5f}, {"card 135x135", 135, 135, 17.5f},
                            {"sem raio 300x40", 300, 40, 0}};
    // Pior recuo (px) e pior desvio de cor (fração do pedaço) num conjunto de pedaços.
    auto measure = [](const Shape& s, const float* knots, int nk, float u0, float u1, float& back, float& skew) {
        float path[kMaxPathPoints];
        int m = piece_path(knots, nk, u0, u1, path);
        Pt a = rrect_point(0, 0, s.w, s.h, s.r, u0), b = rrect_point(0, 0, s.w, s.h, s.r, u1);
        float cx = b.x - a.x, cy = b.y - a.y, len = std::hypot(cx, cy);
        if (len < 1.0f) return;  // pedaço minúsculo: cor única
        cx /= len;
        cy /= len;
        float per = rrect_perimeter(s.w, s.h, s.r), arc_total = (u1 - u0) * per, prev = 0;
        for (int i = 0; i < m; i++) {
            Pt p = rrect_point(0, 0, s.w, s.h, s.r, path[i]);
            float proj = (p.x - a.x) * cx + (p.y - a.y) * cy;
            back = std::max(back, prev - proj);
            prev = std::max(prev, proj);
            float along = (path[i] - u0) * per;
            skew = std::max(skew, std::fabs(proj / len - along / arc_total));
        }
    };
    for (const Shape& s : shapes) {
        float knots[kMaxKnots], turns[kMaxTurns];
        int nk = rrect_knots(s.w, s.h, s.r, knots), nt = rrect_turns(s.w, s.h, s.r, turns);
        float back = 0, skew = 0;
        bool budget = true, cover = true;
        for (int f = 0; f < 400; f++) {
            float head = f / 400.0f + 0.00071f;
            Segment segs[kMaxRingSegments];
            int ns = ring_segments(head, turns, nt, segs);
            budget = budget && ns >= 1 && 1 + ns <= kMaxRingStrokes;
            cover = cover && std::fabs(segs[0].u0 - (head - 1)) < 1e-5f && std::fabs(segs[ns - 1].u1 - head) < 1e-5f;
            for (int k = 0; k < ns; k++) {
                if (k && std::fabs(segs[k].u0 - segs[k - 1].u1) > 1e-6f) cover = false;
                measure(s, knots, nk, segs[k].u0, segs[k].u1, back, skew);
            }
        }
        char buf[160];
        snprintf(buf, sizeof buf, "%s: pedaços cobrem a volta, até %d traços", s.name, kMaxRingStrokes);
        check(buf, budget && cover);
        snprintf(buf, sizeof buf, "%s: luz sempre para a frente (recuo %.2f px)", s.name, back);
        check(buf, back < 0.01f);
        snprintf(buf, sizeof buf, "%s: cor acompanha o contorno (desvio %.1f%% < 8%%)", s.name, skew * 100);
        check(buf, skew < 0.08f);
    }
    {
        // (prova do bug) sem os cortes nas quinas, o trecho de 1/8 no botão largo
        // passa em U pela ponta curta: a cor anda para trás e fica fora do contorno.
        const Shape& s = shapes[0];
        float knots[kMaxKnots];
        int nk = rrect_knots(s.w, s.h, s.r, knots);
        float back = 0, skew = 0;
        Piece ps[kMaxRingPieces];
        int np3 = ring_pieces(ps);
        for (int f = 0; f < 400; f++) {
            float head = f / 400.0f + 0.00071f;
            for (int k = 0; k < np3; k++) measure(s, knots, nk, head - ps[k].d1, head - ps[k].d0, back, skew);
        }
        char buf[160];
        snprintf(buf, sizeof buf, "(prova do bug) sem cortes: recuo %.0f px, desvio %.0f%%", back, skew * 100);
        check(buf, back > 10 && skew > 0.3f);
    }
    float cknots[kMaxKnots];
    int cn = circle_knots(37, cknots);
    check("círculo do avatar: 32 a 96 vértices fixos", cn >= 32 && cn <= 96);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
