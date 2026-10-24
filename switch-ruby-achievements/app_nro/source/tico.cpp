#include "tico.hpp"

#include <chrono>
#include <cmath>
#include <ctime>

#include "prefs.hpp"
#include "ring_geom.hpp"
#include "ui.hpp"

namespace tico {

NVGcolor kBackgroundTop, kBackgroundBottom, kPanel, kSlot, kText, kMuted, kIcon, kSidebar;
NVGcolor kFocusFill, kChevron, kRowOpen, kRowLocked, kBadgeLocked;

namespace {
bool g_dark = false;

// Claro: cores tiradas das capturas do Tico. Escuro: tons de chumbo com o mesmo
// contraste (texto claro sobre cinza-escuro); a borda giratória continua prata.
void set_palette(bool dark) {
    g_dark = dark;
    if (!dark) {
        kBackgroundTop = nvgRGB(244, 246, 249);
        kBackgroundBottom = nvgRGB(233, 237, 242);
        kPanel = nvgRGB(238, 241, 245);
        kSlot = nvgRGB(243, 245, 248);
        kText = nvgRGB(46, 48, 54);
        kMuted = nvgRGB(118, 124, 134);
        kIcon = nvgRGB(84, 86, 94);
        kSidebar = nvgRGB(248, 249, 251);
        kFocusFill = nvgRGB(255, 255, 255);
        kChevron = nvgRGB(66, 68, 74);
        kRowOpen = nvgRGB(255, 255, 255);
        kRowLocked = nvgRGB(226, 229, 235);
        kBadgeLocked = nvgRGB(200, 204, 212);
    } else {
        kBackgroundTop = nvgRGB(52, 55, 61);
        kBackgroundBottom = nvgRGB(38, 40, 45);
        kPanel = nvgRGB(58, 61, 68);
        kSlot = nvgRGB(64, 67, 74);
        kText = nvgRGB(232, 234, 238);
        kMuted = nvgRGB(160, 166, 176);
        kIcon = nvgRGB(204, 208, 216);
        kSidebar = nvgRGB(44, 47, 53);
        kFocusFill = nvgRGB(80, 84, 93);
        kChevron = nvgRGB(150, 155, 165);
        kRowOpen = nvgRGB(72, 76, 84);
        kRowLocked = nvgRGB(50, 53, 59);
        kBadgeLocked = nvgRGB(88, 92, 101);
    }
}
}  // namespace

bool dark() { return g_dark; }


const char* const kGlyphA = "";
const char* const kGlyphB = "";
const char* const kGlyphX = "";
const char* const kGlyphPlus = "";
const char* const kGlyphMinus = "";

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kRowH = 64;

NVGcolor with_alpha(NVGcolor c, float a) {
    c.a *= a;
    return c;
}

// Borda giratória das células do Borealis (definida mais abaixo).
void cell_border(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha);
}  // namespace

// Relógio das animações: o mesmo do Borealis (microssegundos inteiros). Não usar
// float de steady_clock: no Switch o valor é grande e em float só muda de 128 em
// 128 s, o que congelava a borda giratória (2026-10-20).
int64_t now_usec() { return (int64_t)brls::getCPUTimeUsec(); }

float now_seconds() { return (float)((double)now_usec() / 1e6); }

void apply_theme() {
    bool dark = prefs::theme() == prefs::THEME_DARK;
    set_palette(dark);
    // O app força a variante clara do Borealis (main.cpp); no tema escuro os
    // valores dela são trocados pelos de chumbo.
    brls::Theme t = brls::Theme::getLightTheme();
    t.addColor("brls/clear", kBackgroundBottom);
    t.addColor("brls/background", dark ? nvgRGB(46, 49, 55) : nvgRGB(240, 243, 246));
    t.addColor("brls/text", kText);
    t.addColor("brls/text_disabled", dark ? nvgRGB(110, 115, 124) : nvgRGB(150, 154, 162));
    t.addColor("brls/backdrop", nvgRGBA(0, 0, 0, dark ? 190 : 178));
    t.addColor("brls/accent", dark ? nvgRGB(214, 218, 226) : nvgRGB(70, 74, 82));
    t.addColor("brls/click_pulse", dark ? nvgRGBA(220, 224, 232, 36) : nvgRGBA(140, 146, 156, 40));
    t.addColor("brls/highlight/background", dark ? nvgRGB(74, 78, 87) : nvgRGB(251, 252, 253));
    // Usadas só se o gancho da borda giratória não estiver ativo.
    t.addColor("brls/highlight/color1", nvgRGB(198, 203, 211));
    t.addColor("brls/highlight/color2", nvgRGB(112, 118, 130));
    // Sem linhas divisórias: o cartão agrupa os itens. As células do Borealis
    // (DetailCell herda de RecyclerCell) desenham um fio de 1 px embaixo de cada
    // uma com "brls/sidebar/separator"; transparente, o fio some em todas as telas.
    t.addColor("brls/applet_frame/separator", nvgRGBA(0, 0, 0, 0));
    t.addColor("brls/sidebar/background", kSidebar);
    t.addColor("brls/sidebar/active_item", kText);
    t.addColor("brls/sidebar/separator", nvgRGBA(0, 0, 0, 0));
    t.addColor("brls/header/border", nvgRGBA(0, 0, 0, 0));
    t.addColor("brls/header/rectangle", kMuted);
    t.addColor("brls/header/subtitle", kMuted);
    t.addColor("brls/list/listItem_value_color", kMuted);
    t.addColor("brls/button/primary_enabled_background", dark ? nvgRGB(200, 204, 212) : nvgRGB(70, 74, 82));
    t.addColor("brls/button/primary_enabled_text", dark ? nvgRGB(38, 40, 45) : nvgRGB(255, 255, 255));
    t.addColor("brls/button/default_enabled_background", dark ? nvgRGB(80, 84, 93) : nvgRGB(255, 255, 255));
    t.addColor("brls/button/default_enabled_text", kText);
    t.addColor("brls/button/highlight_enabled_text", kText);
    t.addColor("brls/button/highlight_disabled_text", kText);
    t.addColor("brls/slider/line_filled", dark ? nvgRGB(200, 204, 212) : nvgRGB(70, 74, 82));
    t.addColor("brls/spinner/bar_color", dark ? nvgRGBA(220, 224, 232, 90) : nvgRGBA(110, 116, 128, 90));

    brls::highlightBorderDrawer = cell_border;  // células e listas com a borda giratória

    brls::Style s = brls::getStyle();
    s.addMetric("brls/highlight/corner_radius", 12);
    s.addMetric("brls/highlight/stroke_width", kRingStroke);
    s.addMetric("brls/highlight/shadow_opacity", 40);
    s.addMetric("brls/highlight/shadow_offset", 4);
    s.addMetric("brls/sidebar/item_height", 62);
}

// --- Desenho ---------------------------------------------------------------------
namespace {
// Imagens do fundo Clássico (1280x720), carregadas no 1º uso. -1 = falhou (fica o sólido).
int classic_image(NVGcontext* vg, Backdrop kind) {
    static int images[2] = {0, 0};
    static const char* const files[2] = {BRLS_ASSET("img/fundo.jpg"), BRLS_ASSET("img/fundo_settings.jpg")};
    int k = kind == Backdrop::SETTINGS ? 1 : 0;
    if (images[k] == 0) {
        images[k] = nvgCreateImage(vg, files[k], 0);
        if (images[k] <= 0) {
            images[k] = -1;
            brls::Logger::warning("fundo classico: {} nao carregou", files[k]);
        }
    }
    return images[k];
}
}  // namespace

bool classic_background() { return prefs::background() == prefs::BG_CLASSIC; }

void background(NVGcontext* vg, float x, float y, float w, float h, Backdrop kind) {
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + h, kBackgroundTop, kBackgroundBottom));
    nvgFill(vg);
    if (!classic_background()) return;
    int image = classic_image(vg, kind);
    if (image <= 0) return;
    // A imagem cobre a tela inteira (1280x720), presa ao canto da tela.
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, kScreenW, kScreenH, 0, image, 1.0f));
    nvgFill(vg);
    // Escuro: véu de chumbo por cima, para o texto claro continuar legível.
    if (g_dark) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, w, h);
        nvgFillColor(vg, nvgRGBA(30, 32, 37, 205));
        nvgFill(vg);
    }
}

void soft_shadow(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha) {
    NVGpaint shadow = nvgBoxGradient(vg, x, y + 4, w, h, r, 14, nvgRGBAf(0.08f, 0.12f, 0.24f, 0.20f * alpha),
                                     nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 24, w + 60, h + 64);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);
}

// Borda de foco: gradiente prateado que gira no sentido horário. NanoVG não tem
// gradiente cônico, então o contorno é desenhado em segmentos curtos, cada um
// com a cor da sua distância até a ponta brilhante. Vale igual para quadrados,
// retângulos arredondados e círculos (o gradiente linear antigo, num quadrado,
// mostrava quase só a parte escura). As células do Borealis usam o mesmo
// desenho pelo gancho highlightBorderDrawer (apply_theme).
namespace {
const NVGcolor kRingLight = nvgRGB(246, 248, 251);  // prata clara (ponta)
const NVGcolor kRingDark = nvgRGB(118, 124, 136);   // prata escura (lado oposto)
const NVGcolor kRingBase = nvgLerpRGBA(kRingLight, kRingDark, 0.5f);  // média: tapa as junções
constexpr float kRingPeriod = 3.0f;                 // segundos por volta (era 3,6 s)

// Cor a uma fração d (0..1) da volta atrás da ponta: claro e escuro na mesma
// proporção, transição suave em toda a volta (geom::ring_dark).
NVGcolor ring_color(float d) { return nvgLerpRGBA(kRingLight, kRingDark, geom::ring_dark(d)); }

// Fração (0..1) da volta atual, calculada com inteiros para não perder precisão.
float ring_phase() {
    return geom::phase_from_usec(now_usec(), (long long)(kRingPeriod * 1e6));
}

using geom::Pt;

// Desenha o contorno `point(u)` com o giro em no máximo geom::kMaxRingStrokes
// traços: a cor média num traço fechado e cada trecho da volta (geom::ring_pieces)
// num traço aberto com gradiente linear entre as pontas. Não usar um traço por
// segmento: o backend deko3d tem um bloco fixo de 128 KB de comandos por quadro,
// cada traço custa ~1 KB (3 passadas de estêncil) e a versão com até 180 traços
// por borda estourava o bloco e fechava o app (2026-10-20).
//
// Os vértices do caminho são fixos (`knots`, calculados só pela forma): as quinas
// têm o mesmo polígono em todo quadro e só as pontas de cada trecho andam com a
// luz (ver geom::piece_path).
//
// Os pedaços também são cortados nos `turns` (meio de cada quina do retângulo; o
// círculo não precisa): sem isso o gradiente de um trecho em U parava nos lados
// retos e dava um tranco ao passar a quina (ver geom::ring_segments).
template <typename Point>
void rotating_stroke(NVGcontext* vg, float perimeter, float alpha, const float* knots, int nk, const float* turns,
                     int nt, Point point) {
    if (!(perimeter > 1.0f) || !(alpha > 0.0f) || nk <= 0) return;
    nvgLineJoin(vg, NVG_ROUND);
    nvgStrokeWidth(vg, kRingStroke);

    // Base: traço fechado único pelos vértices fixos.
    nvgLineCap(vg, NVG_BUTT);
    nvgBeginPath(vg);
    for (int i = 0; i < nk; i++) {
        Pt q = point(knots[i]);
        if (i == 0) nvgMoveTo(vg, q.x, q.y);
        else nvgLineTo(vg, q.x, q.y);
    }
    nvgClosePath(vg);
    nvgStrokeColor(vg, with_alpha(kRingBase, alpha));
    nvgStroke(vg);

    // Pedaços coloridos, do mais longe ao mais perto da ponta (cor em d = head - u).
    float head = ring_phase();
    geom::Segment segs[geom::kMaxRingSegments];
    int ns = geom::ring_segments(head, turns, nt, segs);
    float path[geom::kMaxPathPoints];
    for (int k = 0; k < ns; k++) {
        float u0 = segs[k].u0, u1 = segs[k].u1;
        int m = geom::piece_path(knots, nk, u0, u1, path);
        Pt first = point(u0), last = point(u1);
        nvgBeginPath(vg);
        for (int i = 0; i < m; i++) {
            Pt q = point(path[i]);
            if (i == 0) nvgMoveTo(vg, q.x, q.y);
            else nvgLineTo(vg, q.x, q.y);
        }
        NVGcolor c0 = with_alpha(ring_color(head - u0), alpha);
        NVGcolor c1 = with_alpha(ring_color(head - u1), alpha);
        if (std::hypot(last.x - first.x, last.y - first.y) < 1.0f) nvgStrokeColor(vg, c1);
        else nvgStrokePaint(vg, nvgLinearGradient(vg, first.x, first.y, last.x, last.y, c0, c1));
        nvgStroke(vg);
    }
    nvgLineJoin(vg, NVG_MITER);
}

// Traço centrado no retângulo dado (o destaque do Borealis passa o seu).
void rotating_rect(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha) {
    float knots[geom::kMaxKnots];
    int nk = geom::rrect_knots(w, h, r, knots);
    float turns[geom::kMaxTurns];
    int nt = geom::rrect_turns(w, h, r, turns);
    rotating_stroke(vg, geom::rrect_perimeter(w, h, r), alpha, knots, nk, turns, nt,
                    [&](float u) { return geom::rrect_point(x, y, w, h, r, u); });
}

void cell_border(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha) {
    rotating_rect(vg, x, y, w, h, r, alpha);
}
}  // namespace

void ring(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha, float inset) {
    // Traço de kRingStroke px por fora da view; `inset` > 0 o traz para dentro
    // (sobrepõe a capa), 0 = a borda interna encosta na view.
    float o = kRingStroke / 2 - inset;
    rotating_rect(vg, x - o, y - o, w + 2 * o, h + 2 * o, r + o, alpha);
}

void ring_circle(NVGcontext* vg, float cx, float cy, float r, float alpha) {
    float rr = r + kRingStroke / 2;
    float knots[geom::kMaxKnots];
    int nk = geom::circle_knots(rr, knots);
    rotating_stroke(vg, 2 * kPi * rr, alpha, knots, nk, nullptr, 0,
                    [&](float u) { return geom::circle_point(cx, cy, rr, u); });
}

void draw_gear(NVGcontext* vg, float cx, float cy, float r, NVGcolor color) {
    nvgBeginPath(vg);
    const float rin = r * 0.74f;
    for (int k = 0; k < 8; k++) {
        float a0 = k * kPi / 4;
        float pts[4][2] = {{a0 - 0.32f, rin}, {a0 - 0.21f, r}, {a0 + 0.21f, r}, {a0 + 0.32f, rin}};
        for (int j = 0; j < 4; j++) {
            float px = cx + std::cos(pts[j][0]) * pts[j][1], py = cy + std::sin(pts[j][0]) * pts[j][1];
            if (k == 0 && j == 0) nvgMoveTo(vg, px, py);
            else nvgLineTo(vg, px, py);
        }
    }
    nvgClosePath(vg);
    nvgCircle(vg, cx, cy, r * 0.34f);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void draw_exit(NVGcontext* vg, float cx, float cy, float r, NVGcolor color) {
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx + r * 0.05f, cy - r);
    nvgLineTo(vg, cx - r * 0.85f, cy - r);
    nvgLineTo(vg, cx - r * 0.85f, cy + r);
    nvgLineTo(vg, cx + r * 0.05f, cy + r);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 2.6f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx + r, cy);
    nvgLineTo(vg, cx - r * 0.25f, cy);
    nvgStrokeWidth(vg, 3.2f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - r * 0.55f, cy);
    nvgLineTo(vg, cx + r * 0.05f, cy - r * 0.5f);
    nvgLineTo(vg, cx + r * 0.05f, cy + r * 0.5f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void draw_triangle(NVGcontext* vg, float cx, float cy, float s, int dir, NVGcolor color) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - s * 0.5f * dir, cy - s * 0.6f);
    nvgLineTo(vg, cx + s * 0.6f * dir, cy);
    nvgLineTo(vg, cx - s * 0.5f * dir, cy + s * 0.6f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

// --- Componentes ------------------------------------------------------------------
Battery::Battery() { setSize(brls::Size(36, 20)); }

void Battery::draw(NVGcontext* vg, float x, float y, float, float, brls::Style, brls::FrameContext*) {
    auto* platform = brls::Application::getPlatform();
    if (!platform->canShowBatteryLevel()) return;
    int level = std::max(0, std::min(100, platform->getBatteryLevel()));
    bool charging = platform->isBatteryCharging();
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 2, 30, 16, 4);
    nvgStrokeColor(vg, a(kText));
    nvgStrokeWidth(vg, 2.2f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 32, y + 7, 3, 6, 1);
    nvgFillColor(vg, a(kText));
    nvgFill(vg);
    NVGcolor fill = charging ? nvgRGB(52, 180, 80) : level <= 15 ? nvgRGB(214, 60, 60) : kText;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 4, y + 5, std::max(2.0f, 24.0f * level / 100.0f), 10, 2);
    nvgFillColor(vg, a(fill));
    nvgFill(vg);
}

Clock::Clock() {
    setFontSize(26);
    setTextColor(kText);
    setMarginRight(18);
    setText("00:00");
}

void Clock::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    int64_t now = (int64_t)time(nullptr);
    if (now / 60 != m_minute) {
        m_minute = now / 60;
        setText(data::format_clock(now));
    }
    brls::Label::draw(vg, x, y, w, h, style, ctx);
}

Icon::Icon(const char* glyph, float size, NVGcolor color) : m_glyph(glyph), m_size(size), m_color(color) {
    setSize(brls::Size(size + 4, size + 4));
}

void Icon::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style, brls::FrameContext*) {
    int font = brls::Application::getFont(brls::FONT_MATERIAL_ICONS);
    if (font == brls::FONT_INVALID) return;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, m_size);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, a(m_color));
    nvgBeginPath(vg);
    nvgText(vg, x + w / 2, y + h / 2, m_glyph.c_str(), nullptr);
}

brls::Label* hint(const char* glyph, const std::string& text, float margin_left) {
    auto l = new brls::Label();
    l->setFontSize(19);
    l->setTextColor(kText);
    l->setText(std::string(glyph) + " " + text);
    l->setMarginLeft(margin_left);
    return l;
}

Panel::Panel() : brls::Box(brls::Axis::COLUMN) {
    setPadding(8, 8, 8, 8);
    setMarginBottom(22);
}

void Panel::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    soft_shadow(vg, x, y, w, h, 14, getAlpha());
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, 14);
    nvgFillColor(vg, a(kPanel));
    nvgFill(vg);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

brls::Label* section(const std::string& text) {
    auto l = new brls::Label();
    l->setText(text);
    l->setFontSize(17);
    l->setTextColor(kMuted);
    l->setMarginLeft(10);
    l->setMarginBottom(10);
    return l;
}

namespace {
// Recuo do texto das células do Borealis (DetailCell); o texto corrido usa o mesmo.
float cell_indent() { return brls::Application::getStyle()["brls/listitem/descriptionIndent"]; }
constexpr float kCardTextX = 22;  // texto a 22 px da borda do cartão (aba App)
// Folga vertical do cartão: justa nas células (só o espaço da borda de foco); o texto
// corrido traz o próprio respiro (kCardTextY), para o cartão de texto não ficar colado.
constexpr float kCardPadY = 6;
constexpr float kCardTextY = 12;
}  // namespace

Panel* card() {
    auto p = new Panel();
    float side = std::max(0.0f, kCardTextX - cell_indent());  // a célula soma o seu recuo
    p->setPadding(kCardPadY, side, kCardPadY, side);
    return p;
}

brls::Label* card_text(const std::string& text) {
    auto l = new brls::Label();
    l->setText(text.empty() ? std::string(" ") : text);
    l->setFontSize(kPageText);
    l->setTextColor(kText);
    l->setMargins(kCardTextY, cell_indent(), kCardTextY, cell_indent());
    return l;
}

brls::DetailCell* cell(const std::string& title, const std::string& detail) {
    auto c = new brls::DetailCell();
    c->title->setFontSize(kPageText);
    c->detail->setFontSize(kPageText);
    if (!title.empty()) c->setText(title);
    if (!detail.empty()) c->setDetailText(detail);
    return c;
}

Page::Page(bool scroll) : brls::Box(brls::Axis::COLUMN), m_body(this) {
    setGrow(1);
    setPadding(0, 30, 0, 30);
    if (scroll) {
        auto s = scroller();
        s->setGrow(1);
        m_body = new brls::Box(brls::Axis::COLUMN);
        set_scroll_content(s, m_body);
        addView(s);
    }
}

Panel* Page::add_card(const std::string& title, bool grow) {
    auto label = section(title);
    label->setShrink(0);
    m_body->addView(label);
    auto c = card();
    if (grow && m_body == this) c->setGrow(1);
    else c->setShrink(0);
    m_body->addView(c);
    return c;
}

namespace {

// ScrollingFrame que deixa o foco sair por cima e por baixo. O do Borealis (modo
// natural) foi feito para ocupar a tela inteira: quando a lista acaba ele devolve o
// próprio foco (getParentNavigationDecision com from == contentView) e o direcional
// nunca alcança o que está fora dele, como o cartão de cima de uma aba ou as células
// acima da lista de conquistas — só o toque saía. Aqui, com o conteúdo sem mais nada
// focável naquela direção, a lista rola até a ponta (mostra o texto que sobrou) e a
// navegação continua nos vizinhos dela. Dentro do conteúdo nada muda.
class Scroller : public brls::ScrollingFrame {
public:
    brls::View* getParentNavigationDecision(brls::View* from, brls::View* newFocus,
                                            brls::FocusDirection dir) override {
        if (!newFocus) return nullptr;  // conteúdo acabou: decide getNextFocus
        return brls::ScrollingFrame::getParentNavigationDecision(from, newFocus, dir);
    }

    brls::View* getNextFocus(brls::FocusDirection dir, brls::View* current) override {
        if (dir != brls::FocusDirection::UP && dir != brls::FocusDirection::DOWN)
            return brls::ScrollingFrame::getNextFocus(dir, current);
        float edge = dir == brls::FocusDirection::UP ? 0 : std::max(0.0f, getContentHeight() - getScrollingAreaHeight());
        bool more = std::fabs(getContentOffsetY() - edge) > 0.5f;
        if (current == this && more) return this;  // foco na própria lista (sem itens): ela rola
        if (more) setContentOffsetY(edge, true);
        return hasParent() ? getParent()->getNextFocus(dir, this) : nullptr;
    }
};

}  // namespace

brls::ScrollingFrame* scroller() {
    auto s = new Scroller();
    s->setMarginRight(-kScrollGutter);
    return s;
}

void set_scroll_content(brls::ScrollingFrame* scroll, brls::View* content) {
    // O ScrollingFrame força a largura do conteúdo igual à dele (setWidth em
    // setContentView/onLayout) e ignora margens: o recuo fica num contêiner
    // por fora, e o conteúdo mantém a largura que tinha antes do scroller().
    auto gutter = new brls::Box(brls::Axis::COLUMN);
    gutter->setPaddingRight(kScrollGutter);
    gutter->addView(content);
    scroll->setContentView(gutter);
}

namespace {

brls::Box* footer(bool with_ok) {
    auto f = new brls::Box(brls::Axis::ROW);
    f->setHeight(60);
    f->setPadding(0, 56, 18, 56);
    f->setJustifyContent(brls::JustifyContent::FLEX_END);
    f->setAlignItems(brls::AlignItems::CENTER);
    f->addView(hint(kGlyphB, tr("Voltar"), 0));
    if (with_ok) f->addView(hint(kGlyphA, tr("OK"), 22));
    return f;
}

brls::Box* header(brls::Label** title_out, float left_padding, brls::Label** subtitle_out = nullptr) {
    auto top = new brls::Box(brls::Axis::ROW);
    top->setHeight(104);
    top->setPadding(10, 46, 0, left_padding);
    top->setAlignItems(brls::AlignItems::CENTER);
    auto texts = new brls::Box(brls::Axis::COLUMN);
    texts->setGrow(1);
    texts->setShrink(1);
    auto title = new brls::Label();
    title->setFontSize(30);
    title->setTextColor(kText);
    title->setSingleLine(true);
    texts->addView(title);
    auto subtitle = new brls::Label();
    subtitle->setFontSize(15);
    subtitle->setTextColor(kMuted);
    subtitle->setSingleLine(true);
    subtitle->setMarginTop(10);
    subtitle->setVisibility(brls::Visibility::GONE);
    texts->addView(subtitle);
    top->addView(texts);
    top->addView(new Clock());
    top->addView(new Battery());
    *title_out = title;
    if (subtitle_out) *subtitle_out = subtitle;
    return top;
}

void register_back(brls::View* v) {
    v->registerAction(tr("Voltar"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);
}

}  // namespace

// --- Frame ------------------------------------------------------------------------
Frame::Frame(brls::View* content, const std::string& title, Backdrop backdrop)
    : brls::Box(brls::Axis::COLUMN), m_content(content), m_backdrop(backdrop) {
    setSize(brls::Size(kScreenW, kScreenH));
    addView(header(&m_title, 52, &m_subtitle));
    m_title->setText(title);
    auto body = new brls::Box(brls::Axis::COLUMN);
    body->setGrow(1);
    body->setPadding(0, 12, 0, 12);
    content->setGrow(1);
    body->addView(content);
    addView(body);
    addView(footer(true));
    register_back(this);
}

void Frame::setTitle(const std::string& title) { m_title->setText(title); }

void Frame::setSubtitle(const std::string& text) {
    m_subtitle->setText(text.empty() ? " " : text);
    m_subtitle->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void Frame::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    tico::background(vg, x, y, w, h, m_backdrop);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

brls::View* Frame::getDefaultFocus() {
    brls::View* v = m_content->getDefaultFocus();
    return v ? v : brls::Box::getDefaultFocus();
}

// --- Tabs -------------------------------------------------------------------------
class Tabs::Holder : public brls::Box {
public:
    Holder() : brls::Box(brls::Axis::COLUMN) {}
    void forget() { setLastFocusedView(nullptr); }
};

namespace {

// Item da barra lateral: ícone e nome; com foco, fundo branco e a borda prateada.
class TabItem : public brls::Box {
public:
    TabItem(const char* glyph, const std::string& name, std::function<void()> on_focus)
        : brls::Box(brls::Axis::ROW), m_on_focus(std::move(on_focus)) {
        setSize(brls::Size(232, 60));
        setMargins(6, 0, 6, 24);
        setPadding(0, 16, 0, 16);
        setAlignItems(brls::AlignItems::CENTER);
        setFocusable(true);
        setHideHighlight(true);
        addView(new Icon(glyph, 26, kIcon));
        auto l = new brls::Label();
        l->setText(name);
        l->setFontSize(18);
        l->setTextColor(kText);
        l->setMarginLeft(12);
        l->setSingleLine(true);
        addView(l);
        addGestureRecognizer(new brls::TapGestureRecognizer(this, [this] { brls::Application::giveFocus(this); }));
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (m_on_focus) m_on_focus();
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        if (isFocused()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, 14);
            nvgFillColor(vg, a(kFocusFill));
            nvgFill(vg);
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (isFocused()) ring(vg, x, y, w, h, 14, getAlpha());
    }

private:
    std::function<void()> m_on_focus;
};

// Avatar do perfil filtrado (ou a inicial) e o apelido, no topo da barra lateral.
brls::Box* profile_header() {
    auto box = new brls::Box(brls::Axis::ROW);
    box->setHeight(116);
    box->setPadding(16, 20, 0, 34);
    box->setAlignItems(brls::AlignItems::CENTER);
    auto avatar = new brls::Box(brls::Axis::COLUMN);
    avatar->setSize(brls::Size(62, 62));
    avatar->setCornerRadius(31);
    avatar->setBackgroundColor(nvgRGB(214, 70, 72));
    avatar->setBorderColor(nvgRGB(255, 255, 255));
    avatar->setBorderThickness(3);
    avatar->setJustifyContent(brls::JustifyContent::CENTER);
    avatar->setAlignItems(brls::AlignItems::CENTER);

    std::string name = ui::profile_name();
    std::vector<UserRow> users;
    data::users(users);
    std::vector<unsigned char> jpeg;
    for (auto& u : users)
        if (u.id == ui::profile_filter() && !u.uid.empty()) data::user_avatar(u.uid, jpeg);
    if (!jpeg.empty()) {
        auto img = new brls::Image();
        img->setSize(brls::Size(62, 62));
        img->setCornerRadius(31);
        img->setScalingType(brls::ImageScalingType::FILL);
        img->setImageFromMem(jpeg.data(), (int)jpeg.size());
        avatar->addView(img);
    } else {
        auto initial = new brls::Label();
        std::string n = name.empty() ? "T" : name;
        size_t len = 1;
        while (len < n.size() && ((unsigned char)n[len] & 0xC0) == 0x80) len++;
        initial->setText(n.substr(0, len));
        initial->setFontSize(26);
        initial->setTextColor(nvgRGB(255, 255, 255));
        avatar->addView(initial);
    }
    box->addView(avatar);
    auto label = new brls::Label();
    label->setText(name.empty() ? tr("Todos os perfis") : name);
    label->setFontSize(19);
    label->setTextColor(kText);
    label->setMarginLeft(18);
    label->setSingleLine(true);
    label->setMaxWidth(150);
    box->addView(label);
    return box;
}

// Tempo total jogado do mesmo perfil do topo (todos os jogos e apps; "Todos os perfis"
// soma todos), logo abaixo da última aba. A tela é recriada ao abrir, então segue o
// perfil escolhido na tela inicial.
brls::Box* profile_total() {
    int64_t seconds = 0;
    data::profile_total(ui::profile_filter(), seconds);
    auto box = new brls::Box(brls::Axis::ROW);
    box->setSize(brls::Size(232, 60));
    box->setMargins(14, 0, 0, 24);
    box->setPadding(0, 16, 0, 16);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->addView(new Icon(icon::kSchedule, 26, kIcon));
    auto text = new brls::Box(brls::Axis::COLUMN);
    text->setMarginLeft(12);
    auto caption = new brls::Label();
    caption->setText(tr("Tempo total jogado"));
    caption->setFontSize(15);
    caption->setTextColor(kMuted);
    caption->setSingleLine(true);
    text->addView(caption);
    auto value = new brls::Label();
    value->setText(data::format_duration(seconds));
    value->setFontSize(19);
    value->setTextColor(kText);
    value->setSingleLine(true);
    text->addView(value);
    box->addView(text);
    return box;
}

}  // namespace

Tabs::Tabs() : brls::Box(brls::Axis::ROW) {
    setSize(brls::Size(kScreenW, kScreenH));

    auto side = new brls::Box(brls::Axis::COLUMN);
    side->setWidth(280);
    side->addView(profile_header());
    m_items = new brls::Box(brls::Axis::COLUMN);
    m_items->setMarginTop(8);
    side->addView(m_items);
    side->addView(profile_total());
    addView(side);

    auto right = new brls::Box(brls::Axis::COLUMN);
    right->setGrow(1);
    right->addView(header(&m_title, 52));
    m_holder = new Holder();
    m_holder->setGrow(1);
    m_holder->setPadding(0, 12, 0, 12);
    right->addView(m_holder);
    right->addView(footer(true));
    addView(right);
    register_back(this);
}

void Tabs::addTab(const char* glyph, const std::string& name, std::function<brls::View*()> make) {
    size_t index = m_tabs.size();
    auto item = new TabItem(glyph, name, [this, index] { show(index); });
    m_items->addView(item);
    m_tabs.push_back({item, name, std::move(make)});
    if (index == 0) show(0);
}

void Tabs::select(size_t index) { show(index); }

void Tabs::show(size_t index) {
    if (index == m_current || index >= m_tabs.size()) return;
    m_current = index;
    m_title->setText(m_tabs[index].name);
    m_holder->clearViews();
    m_holder->forget();
    brls::View* v = m_tabs[index].make();
    v->setGrow(1);
    m_holder->addView(v);
}

void Tabs::draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) {
    tico::background(vg, x, y, w, h, Backdrop::SETTINGS);
    // Barra lateral mais clara, com uma sombra leve na borda direita. Sobre a
    // imagem do fundo Clássico ela fica translúcida, deixando a imagem aparecer.
    nvgBeginPath(vg);
    nvgRect(vg, x + 280, y, 16, h);
    nvgFillPaint(vg, nvgLinearGradient(vg, x + 280, y, x + 296, y, g_dark ? nvgRGBA(0, 0, 0, 70) : nvgRGBA(20, 30, 60, 22),
                                       nvgRGBA(0, 0, 0, 0)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, 280, h);
    nvgFillColor(vg, with_alpha(kSidebar, classic_background() ? 0.82f : 1.0f));
    nvgFill(vg);
    brls::Box::draw(vg, x, y, w, h, style, ctx);
}

brls::View* Tabs::getDefaultFocus() {
    if (m_current < m_tabs.size()) return m_tabs[m_current].item;
    return brls::Box::getDefaultFocus();
}

// --- Modais -----------------------------------------------------------------------
namespace {

// Linha de um modal: só o texto, sem seta (alinhado à esquerda nas listas, centrado nos
// botões); com foco, fundo branco e borda prateada.
class Row : public brls::Box {
public:
    Row(const std::string& text, bool list, float w, std::function<void()> action) : brls::Box(brls::Axis::ROW) {
        setSize(brls::Size(w, kRowH));
        setPadding(0, 18, 0, 18);
        setAlignItems(brls::AlignItems::CENTER);
        setJustifyContent(list ? brls::JustifyContent::FLEX_START : brls::JustifyContent::CENTER);
        setFocusable(true);
        setHideHighlight(true);
        auto l = new brls::Label();
        l->setText(text);
        l->setFontSize(19);
        l->setTextColor(kText);
        l->setSingleLine(true);
        addView(l);
        registerClickAction([action](brls::View*) {
            action();
            return true;
        });
        addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        if (isFocused()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, 12);
            nvgFillColor(vg, a(kFocusFill));
            nvgFill(vg);
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (isFocused()) ring(vg, x, y, w, h, 12, getAlpha());
    }
};

// Fundo escurecido com um painel no centro e o título em branco no topo.
class Modal : public brls::Box {
public:
    Modal(const std::string& title, float width) : brls::Box(brls::Axis::COLUMN) {
        setSize(brls::Size(kScreenW, kScreenH));
        setAlignItems(brls::AlignItems::CENTER);
        setJustifyContent(brls::JustifyContent::CENTER);
        if (!title.empty()) {
            auto t = new brls::Label();
            t->setText(title);
            t->setFontSize(24);
            t->setTextColor(nvgRGB(255, 255, 255));
            t->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            t->setWidth(kScreenW - 120);
            t->setSingleLine(true);
            t->setPositionType(brls::PositionType::ABSOLUTE);
            t->setPositionTop(38);
            t->setPositionLeft(60);
            addView(t);
        }
        m_panel = new Panel();
        m_panel->setWidth(width);
        m_panel->setMarginBottom(0);
        addView(m_panel);
        registerAction(tr("Cancelar"), brls::BUTTON_B, [this](brls::View*) {
            close(m_cancel);
            return true;
        }, true);
        addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus st, brls::Sound*) {
            if (st.state != brls::GestureState::END) return;
            brls::Rect f = m_panel->getFrame();
            if (!f.pointInside(st.position)) close(m_cancel);
        }));
    }

    bool isTranslucent() override { return true; }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, w, h);
        nvgFillColor(vg, a(nvgRGBA(12, 14, 18, 190)));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

    brls::View* getDefaultFocus() override { return m_focus ? m_focus : brls::Box::getDefaultFocus(); }

    // Fecha e roda `then` no quadro seguinte (a atividade do modal já saiu da pilha).
    void close(std::function<void()> then) {
        if (m_closing) return;
        m_closing = true;
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [then] {
            if (then) brls::sync(then);
        });
    }

    static void open(Modal* m) { brls::Application::pushActivity(new brls::Activity(m), brls::TransitionAnimation::FADE); }

    brls::Box* panel() { return m_panel; }
    void set_focus(brls::View* v) { m_focus = v; }
    void set_cancel(std::function<void()> fn) { m_cancel = std::move(fn); }  // B ou toque fora

private:
    Panel* m_panel;
    brls::View* m_focus = nullptr;
    std::function<void()> m_cancel;
    bool m_closing = false;
};

brls::Label* body_text(const std::string& text, float width) {
    auto l = new brls::Label();
    l->setText(text);
    l->setFontSize(19);
    l->setTextColor(kText);
    l->setWidth(width);
    l->setMargins(18, 18, 18, 18);
    return l;
}

}  // namespace

void pick(const std::string& title, const std::vector<std::string>& options, std::function<void(int)> cb,
          int selected, std::function<void()> cancelled) {
    if (options.empty()) return;
    constexpr float kW = 640;
    auto m = new Modal(title, kW);
    m->set_cancel(std::move(cancelled));
    brls::Box* list = new brls::Box(brls::Axis::COLUMN);
    for (size_t i = 0; i < options.size(); i++) {
        int idx = (int)i;
        auto row = new Row(options[i], true, kW - 16, [m, cb, idx] { m->close([cb, idx] { cb(idx); }); });
        list->addView(row);
        if (idx == selected) m->set_focus(row);
    }
    // Muitas opções: a lista rola seguindo o foco. A área de rolagem recorta o que
    // passa dela, e a borda de foco fica kRingStroke px por fora da linha: a área avança
    // sobre a folga do painel e a lista recua o mesmo tanto, então as linhas ficam no
    // mesmo lugar do modal sem rolagem e a borda aparece inteira (corte no "Unificar com
    // outro jogo", 2026-10-21).
    if (options.size() > 7) {
        constexpr float kInset = kRingStroke + 1;
        auto scroll = scroller();
        scroll->setHeight(7 * kRowH + 2 * kInset);
        scroll->setMargins(-kInset, -kScrollGutter, -kInset, -kInset);
        list->setPadding(kInset, 0, kInset, kInset);
        set_scroll_content(scroll, list);
        m->panel()->addView(scroll);
    } else {
        m->panel()->addView(list);
    }
    Modal::open(m);
}

namespace {

// Texto e uma linha de botões (o último recebe o foco).
void dialog(const std::string& text, std::vector<std::pair<std::string, std::function<void()>>> buttons) {
    constexpr float kW = 720;
    auto m = new Modal("", kW);
    m->panel()->addView(body_text(text, kW - 52));
    auto row = new brls::Box(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setMarginTop(4);
    float bw = (kW - 16 - 12 * (buttons.size() - 1)) / buttons.size();
    brls::View* last = nullptr;
    for (size_t i = 0; i < buttons.size(); i++) {
        auto fn = buttons[i].second;
        auto b = new Row(buttons[i].first, false, bw, [m, fn] { m->close(fn); });
        if (i > 0) b->setMarginLeft(12);
        row->addView(b);
        last = b;
    }
    m->panel()->addView(row);
    m->set_focus(last);
    Modal::open(m);
}

}  // namespace

void confirm(const std::string& text, const std::string& action, std::function<void()> cb) {
    dialog(text, {{tr("Cancelar"), {}}, {action, cb}});
}

void message(const std::string& text) { dialog(text, {{tr("OK"), {}}}); }

}  // namespace tico
