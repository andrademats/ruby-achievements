// Tela inicial no estilo do launcher Tico (referência: capturas do Tico,
// fora do repositório): barra superior (avatar, jogo selecionado com o tempo total, hora e
// bateria), grade 7x3 paginada com espaços vazios, indicador de páginas e
// rodapé com os botões.
//
// Botões (só estes funcionam aqui):
//   A    jogo: conquistas do jogo           avatar: próximo perfil
//   +    jogo: menu (Configurações do Item, Sessões, engrenagem, sair)
//        avatar: menu só com engrenagem e sair
//   -    jogo: editar o item (tela do jogo)  avatar: nada
//   X    pesquisar
//   Y    ordenar (jogados recentemente, A-Z, mais tempo, mais % de conquistas)
// Toque no avatar também troca o perfil; o perfil escolhido fica no config.ini.
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>

#include "ui.hpp"

namespace {

constexpr int kCols = 7;
constexpr int kRows = 3;
constexpr int kPerPage = kCols * kRows;
constexpr float kScreenW = 1280;
constexpr float kScreenH = 720;
constexpr float kCard = 132;
constexpr float kGap = 20;
constexpr float kRadius = 16;
constexpr int kPageAnimMs = 320;
constexpr float kPi = 3.14159265f;
constexpr float kAvatar = 64;

// Cores do tema (claro do Tico ou escuro): tico::k*. Estas valem nos dois.
NVGcolor dot_color() { return tico::dark() ? nvgRGB(104, 108, 117) : nvgRGB(196, 199, 205); }
const NVGcolor kPageActive = nvgRGB(52, 199, 89);
const NVGcolor kAccent = nvgRGB(214, 70, 72);

// Glifos dos botões na fonte do sistema (os mesmos das dicas do Borealis).
constexpr const char* kGlyphA = "";
constexpr const char* kGlyphX = "";
constexpr const char* kGlyphPlus = "";
constexpr const char* kGlyphY = "\uE0E3";
constexpr const char* kGlyphMinus = "";


NVGcolor with_alpha(NVGcolor c, float a) {
    c.a *= a;
    return c;
}

// Cor de fundo estável por jogo para as capas que faltam (ROMs sem capa).
NVGcolor placeholder_color(const std::string& name) {
    static const NVGcolor palette[] = {nvgRGB(255, 205, 178), nvgRGB(181, 234, 215), nvgRGB(199, 206, 234),
                                       nvgRGB(255, 218, 193), nvgRGB(226, 240, 203), nvgRGB(245, 200, 220)};
    unsigned h = 0;
    for (char c : name) h = h * 31 + (unsigned char)c;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

// --- Desenho no estilo do Tico ----------------------------------------------------
// Sombra suave embaixo de um retângulo arredondado.
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

// Borda de seleção prateada 1 px por dentro da capa (sem fresta): ver tico::ring.
void focus_ring(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha) {
    tico::ring(vg, x, y, w, h, r, alpha, 1);
}

class GridPager;

// --- Card de um jogo: capa arredondada com sombra; com foco ganha a borda ----------
class GameCard : public brls::Box {
public:
    GameCard(GridPager* pager, const GameRow& g, int index) : brls::Box(brls::Axis::COLUMN), m_pager(pager), m_index(index) {
        setSize(brls::Size(kCard, kCard));
        setMargins(kGap / 2, kGap / 2, kGap / 2, kGap / 2);
        setFocusable(true);
        setHideHighlight(true);  // a borda é desenhada aqui
        setJustifyContent(brls::JustifyContent::CENTER);
        setAlignItems(brls::AlignItems::CENTER);
        if (!g.cover.empty()) {
            auto img = new brls::Image();
            img->setSize(brls::Size(kCard, kCard));
            img->setScalingType(brls::ImageScalingType::FILL);
            img->setCornerRadius(kRadius);
            img->setImageFromFile(g.cover);
            addView(img);
        } else {
            m_fill = placeholder_color(g.name);
            auto name = new brls::Label();
            name->setText(g.name);
            name->setFontSize(17);
            name->setTextColor(tico::kText);
            name->setHorizontalAlign(brls::HorizontalAlign::CENTER);
            name->setMaxWidth(kCard - 18);
            addView(name);
            auto tag = new brls::Label();
            tag->setText(data::source_label(g.source));
            tag->setFontSize(13);
            tag->setTextColor(tico::kMuted);
            tag->setMarginTop(6);
            addView(tag);
        }
        addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    int index() const { return m_index; }
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* current) override;

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        float al = getAlpha();  // acompanha o esmaecimento da página
        soft_shadow(vg, x, y, w, h, kRadius, al * (isFocused() ? 1.6f : 1.0f));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, kRadius);
        nvgFillColor(vg, a(m_fill));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (isFocused()) focus_ring(vg, x, y, w, h, kRadius, al);
    }

private:
    GridPager* m_pager;
    int m_index;
    NVGcolor m_fill = tico::kSlot;
};

// --- Espaço vazio da página: vidro no estilo Frutiger Aero, sem marca no meio -----
// Translúcido e esbranquiçado (o fundo aparece por trás) com um fio de luz na borda,
// mais forte no alto; sem aura nem reflexo (pedido de 2026-10-20). No tema escuro o
// vidro fica mais fumê. 1 preenchimento + 1 traço por espaço.
class EmptySlot : public brls::View {
public:
    EmptySlot() {
        setSize(brls::Size(kCard, kCard));
        setMargins(kGap / 2, kGap / 2, kGap / 2, kGap / 2);
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style, brls::FrameContext*) override {
        const float al = getAlpha();
        const bool dark = tico::dark();
        auto white = [al](float alpha) { return nvgRGBAf(1, 1, 1, alpha * al); };

        // Corpo do vidro: mais leitoso em cima, mais transparente embaixo.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, kRadius);
        nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + h, white(dark ? 0.16f : 0.58f), white(dark ? 0.05f : 0.24f)));
        nvgFill(vg);

        // Fio de luz na borda: forte no alto, fraco embaixo.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 0.75f, y + 0.75f, w - 1.5f, h - 1.5f, kRadius - 0.75f);
        nvgStrokePaint(vg, nvgLinearGradient(vg, x, y, x, y + h, white(dark ? 0.38f : 0.95f), white(dark ? 0.08f : 0.35f)));
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }
};

// --- Grade paginada: uma página (kCols x kRows) por tela, desliza na troca ---
class GridPager : public brls::Box {
public:
    std::function<void(int)> on_select;    // índice do jogo com foco
    std::function<void(int)> on_open;      // A ou toque no jogo
    std::function<brls::View*()> up_target;  // "para cima" na primeira linha (o avatar)

    GridPager() : brls::Box(brls::Axis::ROW) {
        setAlignItems(brls::AlignItems::CENTER);
        addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus st, brls::Sound*) {
                if (st.state != brls::GestureState::END) return;
                float dx = st.position.x - st.startPosition.x;
                if (dx < -90) go_page(m_page + 1);
                else if (dx > 90) go_page(m_page - 1);
            },
            brls::PanAxis::HORIZONTAL));
    }

    void build(const std::vector<GameRow>& games) {
        clearViews();
        m_cards.clear();
        m_pages.clear();
        int pages = std::max(1, (int)((games.size() + kPerPage - 1) / kPerPage));
        setWidth(kScreenW * pages);
        for (int p = 0; p < pages; p++) {
            auto page = new brls::Box(brls::Axis::COLUMN);
            page->setWidth(kScreenW);
            page->setJustifyContent(brls::JustifyContent::CENTER);
            page->setAlignItems(brls::AlignItems::CENTER);
            for (int r = 0; r < kRows; r++) {
                auto row = new brls::Box(brls::Axis::ROW);
                for (int c = 0; c < kCols; c++) {
                    size_t i = (size_t)(p * kPerPage + r * kCols + c);
                    if (i >= games.size()) {
                        row->addView(new EmptySlot());
                        continue;
                    }
                    auto card = new GameCard(this, games[i], (int)i);
                    int idx = (int)i;
                    card->registerClickAction([this, idx](brls::View*) {
                        if (on_open) on_open(idx);
                        return true;
                    });
                    m_cards.push_back(card);
                    row->addView(card);
                }
                page->addView(row);
            }
            m_pages.push_back(page);
            addView(page);
        }
        set_page(std::min(m_page, pages - 1), false);
    }

    int count() const { return (int)m_cards.size(); }
    int pages() const { return (int)m_pages.size(); }
    int page() const { return m_page; }
    float position() { return m_offset.getValue(); }  // página atual, com fração durante a animação
    brls::View* card(int i) { return i >= 0 && i < count() ? m_cards[i] : nullptr; }

    // Vizinho de um card na grade (D-pad e alavanca). Fora da página, segue
    // para a mesma linha da página ao lado.
    brls::View* next(const GameCard* from, brls::FocusDirection d) {
        int i = from->index(), p = i / kPerPage, r = (i % kPerPage) / kCols, c = i % kCols;
        int target = -1;
        switch (d) {
            case brls::FocusDirection::LEFT:
                target = c > 0 ? i - 1 : (p > 0 ? (p - 1) * kPerPage + r * kCols + kCols - 1 : -1);
                break;
            case brls::FocusDirection::RIGHT:
                target = c < kCols - 1 && i + 1 < count() ? i + 1 : (p + 1 < pages() ? (p + 1) * kPerPage + r * kCols : -1);
                if (target >= count() && p + 1 < pages()) target = count() - 1;  // última página mais curta
                break;
            case brls::FocusDirection::UP:
                if (r == 0) return up_target ? up_target() : nullptr;
                target = i - kCols;
                break;
            case brls::FocusDirection::DOWN:
                if (r + 1 < kRows) {
                    target = i + kCols;
                    int row_start = p * kPerPage + (r + 1) * kCols;
                    if (target >= count()) target = row_start < count() ? count() - 1 : -1;  // linha de baixo incompleta
                }
                break;
        }
        return card(target);
    }

    void go_page(int p) {
        if (p < 0 || p >= pages() || p == m_page) return;
        brls::View* cur = brls::Application::getCurrentFocus();
        int slot = 0;
        for (auto* c : m_cards)
            if (c == cur) slot = c->index() % kPerPage;
        int target = std::min(p * kPerPage + slot, count() - 1);
        if (auto* v = card(target)) brls::Application::giveFocus(v);
    }

    void onChildFocusGained(brls::View* direct, brls::View* focused) override {
        brls::Box::onChildFocusGained(direct, focused);
        for (auto* c : m_cards)
            if (c == focused) {
                set_page(c->index() / kPerPage, true);
                if (on_select) on_select(c->index());
            }
    }

private:
    void set_page(int p, bool animate) {
        m_page = std::max(0, p);
        m_offset.reset();
        if (animate) {
            m_offset.addStep((float)m_page, kPageAnimMs, brls::EasingFunction::quadraticOut);
            m_offset.setTickCallback([this] { apply_offset(); });
            m_offset.start();
        } else {
            m_offset = (float)m_page;
        }
        apply_offset();
    }

    // Desliza a fileira de páginas e esmaece as que saem da tela.
    void apply_offset() {
        float pos = m_offset.getValue();
        setTranslationX(-pos * kScreenW);
        for (size_t i = 0; i < m_pages.size(); i++)
            m_pages[i]->setAlpha(std::max(0.0f, 1.0f - std::fabs(pos - (float)i)));
    }

    std::vector<GameCard*> m_cards;
    std::vector<brls::Box*> m_pages;
    int m_page = 0;
    brls::Animatable m_offset = 0.0f;
};

brls::View* GameCard::getNextFocus(brls::FocusDirection direction, brls::View*) { return m_pager->next(this, direction); }

// --- Indicador de páginas: (<) casinha • • • (>) ------------------------------------
class PageDots : public brls::View {
public:
    explicit PageDots(GridPager* pager) : m_pager(pager) {
        setSize(brls::Size(kScreenW, 40));
        addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus st, brls::Sound*) {
            if (st.state != brls::GestureState::END) return;
            float x = st.position.x - getX();
            int n = m_pager->pages();
            float start = kScreenW / 2 - (n - 1) * kSpacing / 2;
            if (x < start - kSpacing) m_pager->go_page(m_pager->page() - 1);
            else if (x > start + (n - 1) * kSpacing + kSpacing) m_pager->go_page(m_pager->page() + 1);
            else m_pager->go_page((int)std::lround((x - start) / kSpacing));
        }));
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style, brls::FrameContext*) override {
        int n = m_pager->pages();
        if (m_pager->count() == 0) return;
        float pos = m_pager->position();
        float cy = y + h / 2, start = x + w / 2 - (n - 1) * kSpacing / 2;
        button(vg, start - kChevron, cy, -1, m_pager->page() > 0);
        button(vg, start + (n - 1) * kSpacing + kChevron, cy, 1, m_pager->page() + 1 < n);
        for (int i = 0; i < n; i++) {
            float t = std::max(0.0f, 1.0f - std::fabs(pos - (float)i));
            NVGcolor c = nvgLerpRGBA(dot_color(), kPageActive, t);
            float cx = start + i * kSpacing;
            nvgBeginPath(vg);
            if (i == 0) {
                // Primeira página = início: casinha preta no tema claro e branca no
                // escuro, nas duas situações (só cresce quando é a página atual).
                house(vg, cx, cy, 6.5f + 1.5f * t, a(tico::dark() ? nvgRGB(255, 255, 255) : nvgRGB(0, 0, 0)));
                continue;
            }
            nvgCircle(vg, cx, cy, 3 + 1.0f * t);
            nvgFillColor(vg, a(c));
            nvgFill(vg);
        }
    }

private:
    static constexpr float kSpacing = 15;
    static constexpr float kChevron = 31;

    void button(NVGcontext* vg, float cx, float cy, int dir, bool enabled) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 11);
        NVGcolor c = tico::kChevron;
        if (!enabled) c.a = 90 / 255.0f;
        nvgFillColor(vg, a(c));
        nvgFill(vg);
        tico::draw_triangle(vg, cx + dir * 0.5f, cy, 8, dir, a(nvgRGB(255, 255, 255)));
    }

    // Casinha: telhado triangular e corpo com a porta vazada.
    static void house(NVGcontext* vg, float cx, float cy, float r, NVGcolor color) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, cy - r);                    // cume
        nvgLineTo(vg, cx + r, cy - r * 0.05f);        // beiral direito
        nvgLineTo(vg, cx + r * 0.68f, cy - r * 0.05f);
        nvgLineTo(vg, cx + r * 0.68f, cy + r * 0.85f);
        nvgLineTo(vg, cx - r * 0.68f, cy + r * 0.85f);
        nvgLineTo(vg, cx - r * 0.68f, cy - r * 0.05f);
        nvgLineTo(vg, cx - r, cy - r * 0.05f);        // beiral esquerdo
        nvgClosePath(vg);
        nvgRect(vg, cx - r * 0.2f, cy + r * 0.3f, r * 0.4f, r * 0.55f);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillColor(vg, color);
        nvgFill(vg);
    }

    GridPager* m_pager;
};

// --- Bateria: contorno com carga proporcional (o widget do Borealis vinha sem imagem) ---
class BatteryIcon : public brls::View {
public:
    BatteryIcon() { setSize(brls::Size(36, 20)); }

    void draw(NVGcontext* vg, float x, float y, float, float, brls::Style, brls::FrameContext*) override {
        auto* platform = brls::Application::getPlatform();
        if (!platform->canShowBatteryLevel()) return;
        int level = std::max(0, std::min(100, platform->getBatteryLevel()));
        bool charging = platform->isBatteryCharging();
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 1, y + 2, 30, 16, 4);
        nvgStrokeColor(vg, a(tico::kText));
        nvgStrokeWidth(vg, 2.2f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 32, y + 7, 3, 6, 1);
        nvgFillColor(vg, a(tico::kText));
        nvgFill(vg);
        NVGcolor fill = charging ? nvgRGB(52, 180, 80) : level <= 15 ? nvgRGB(214, 60, 60) : tico::kText;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 4, y + 5, std::max(2.0f, 24.0f * level / 100.0f), 10, 2);
        nvgFillColor(vg, a(fill));
        nvgFill(vg);
    }
};

// --- Avatar do perfil: círculo com anel branco e sombra; com foco ganha a borda -----
class AvatarButton : public brls::Box {
public:
    std::function<brls::View*()> down_target;
    std::function<void(bool)> on_focus;

    AvatarButton() : brls::Box(brls::Axis::COLUMN) {
        setSize(brls::Size(kAvatar, kAvatar));
        setFocusable(true);
        setHideHighlight(true);
        setJustifyContent(brls::JustifyContent::CENTER);
        setAlignItems(brls::AlignItems::CENTER);
    }

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View*) override {
        if (direction == brls::FocusDirection::DOWN && down_target) return down_target();
        return nullptr;
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (on_focus) on_focus(true);
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        if (on_focus) on_focus(false);
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        float cx = x + w / 2, cy = y + h / 2, r = w / 2;
        NVGpaint shadow = nvgRadialGradient(vg, cx, cy + 3, r, r + 10, nvgRGBAf(0.08f, 0.12f, 0.24f, 0.22f),
                                            nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy + 3, r + 12);
        nvgFillPaint(vg, shadow);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r + 4);
        nvgFillColor(vg, a(nvgRGB(255, 255, 255)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, a(kAccent));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        if (isFocused()) tico::ring_circle(vg, cx, cy, r + 4, 1.0f);  // colado no anel branco
    }
};

brls::Label* footer_hint(const char* glyph, const std::string& text, float margin_left) {
    auto l = new brls::Label();
    l->setFontSize(19);
    l->setTextColor(tico::kText);
    l->setText(std::string(glyph) + " " + text);
    l->setMarginLeft(margin_left);
    return l;
}

// --- Menu do + (modal do Tico) ------------------------------------------------------
// Coluna de ícones à esquerda (engrenagem = Configurações globais, sair = fecha
// o app) e, com um jogo em foco, as opções do item à direita. Fundo escurecido;
// B, + ou toque fora fecham.
struct MenuEntry {
    std::string label;
    std::function<void()> action;
};

class MenuButton : public brls::Box {
public:
    enum Kind { ROW, GEAR, EXIT };

    MenuButton(Kind kind, const std::string& text, std::function<void()> action, float w, float h)
        : brls::Box(brls::Axis::ROW), m_kind(kind) {
        setSize(brls::Size(w, h));
        setFocusable(true);
        setHideHighlight(true);
        setAlignItems(brls::AlignItems::CENTER);
        if (kind == ROW) {
            setPadding(0, 18, 0, 18);
            auto l = new brls::Label();
            l->setText(text);
            l->setFontSize(20);
            l->setTextColor(tico::kText);
            l->setSingleLine(true);
            addView(l);
        }
        registerClickAction([action](brls::View*) {
            action();
            return true;
        });
        addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        const float r = 12;
        if (isFocused()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, r);
            nvgFillColor(vg, a(tico::kFocusFill));
            nvgFill(vg);
        }
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        float cx = x + w / 2, cy = y + h / 2;
        if (m_kind == GEAR) tico::draw_gear(vg, cx, cy, 14, a(tico::kIcon));
        else if (m_kind == EXIT) tico::draw_exit(vg, cx, cy, 12, a(tico::kIcon));  // ROW: só o texto
        if (isFocused()) tico::ring(vg, x + 2, y + 2, w - 4, h - 4, r - 2, 1.0f);
    }

private:
    Kind m_kind;
};

class TicoMenu : public brls::Box {
public:
    static constexpr float kRowH = 76;
    static constexpr float kIconW = 76;
    static constexpr float kListW = 500;
    static constexpr float kSpace = 12;

    TicoMenu(std::vector<MenuEntry> items, std::function<void()> on_close)
        // Eixo em linha: para cima/baixo não sai das colunas; esquerda/direita
        // seguem as rotas explícitas abaixo (mesma altura).
        : brls::Box(brls::Axis::ROW), m_on_close(std::move(on_close)) {
        setSize(brls::Size(kScreenW, kScreenH));

        auto title = new brls::Label();
        title->setText(tr("Menu"));
        title->setFontSize(24);
        title->setTextColor(nvgRGB(255, 255, 255));
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setWidth(kScreenW);
        title->setPositionType(brls::PositionType::ABSOLUTE);
        title->setPositionTop(38);
        title->setPositionLeft(0);
        addView(title);

        // Posição das capturas do Tico (x = 302 com a lista; centrado só com os ícones).
        int rows = std::max(2, (int)items.size());
        float group_w = items.empty() ? kIconW : kIconW + kSpace + kListW;
        float left = items.empty() ? (kScreenW - group_w) / 2 : 302;
        float top = (kScreenH - rows * kRowH) / 2;

        auto icons = new brls::Box(brls::Axis::COLUMN);
        icons->setPositionType(brls::PositionType::ABSOLUTE);
        icons->setPositionTop(top);
        icons->setPositionLeft(left);
        icons->setSize(brls::Size(kIconW, 2 * kRowH));
        icons->setBackgroundColor(tico::kPanel);
        icons->setCornerRadius(14);
        m_gear = new MenuButton(MenuButton::GEAR, "", [this] {
            close([] { brls::Application::pushActivity(new SettingsActivity()); });
        }, kIconW, kRowH);
        icons->addView(m_gear);
        auto exit_btn = new MenuButton(MenuButton::EXIT, "", [this] {
            close([] { brls::Application::quit(); });
        }, kIconW, kRowH);
        icons->addView(exit_btn);
        addView(icons);
        m_panels.push_back(icons);
        brls::View* icon_at[2] = {m_gear, exit_btn};

        if (!items.empty()) {
            auto list = new brls::Box(brls::Axis::COLUMN);
            list->setPositionType(brls::PositionType::ABSOLUTE);
            list->setPositionTop(top);
            list->setPositionLeft(left + kIconW + kSpace);
            list->setSize(brls::Size(kListW, rows * kRowH));
            list->setBackgroundColor(tico::kPanel);
            list->setCornerRadius(14);
            std::vector<brls::View*> row_views;
            for (auto& it : items) {
                auto fn = it.action;
                auto b = new MenuButton(MenuButton::ROW, it.label, [this, fn] { close(fn); }, kListW, kRowH);
                if (!m_first) m_first = b;
                list->addView(b);
                row_views.push_back(b);
            }
            addView(list);
            m_panels.push_back(list);
            for (size_t i = 0; i < row_views.size(); i++)
                row_views[i]->setCustomNavigationRoute(brls::FocusDirection::LEFT, icon_at[std::min<size_t>(i, 1)]);
            for (size_t j = 0; j < 2; j++)
                icon_at[j]->setCustomNavigationRoute(brls::FocusDirection::RIGHT,
                                                     row_views[std::min(j, row_views.size() - 1)]);
        }

        // Toque fora dos painéis fecha (o toque num botão é do botão).
        addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus st, brls::Sound*) {
            if (st.state != brls::GestureState::END) return;
            for (auto* p : m_panels) {
                brls::Rect f = p->getFrame();
                if (f.pointInside(st.position)) return;
            }
            close({});
        }));
        registerAction(tr("Voltar"), brls::BUTTON_B, [this](brls::View*) {
            close({});
            return true;
        }, true);
        registerAction(tr("Menu"), brls::BUTTON_START, [this](brls::View*) {
            close({});
            return true;
        }, true);
    }

    bool isTranslucent() override { return true; }

    brls::View* getDefaultFocus() override { return m_first ? m_first : m_gear; }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, w, h);
        nvgFillColor(vg, a(nvgRGBA(12, 14, 18, 190)));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

private:
    // Fecha o menu e, no quadro seguinte, roda a ação (a atividade do menu já saiu da pilha).
    void close(std::function<void()> then) {
        if (m_closing) return;
        m_closing = true;
        auto on_close = m_on_close;
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [on_close, then] {
            brls::sync([on_close, then] {
                if (on_close) on_close();
                if (then) then();
            });
        });
    }

    std::function<void()> m_on_close;
    std::vector<brls::View*> m_panels;
    brls::View* m_first = nullptr;
    brls::View* m_gear = nullptr;
    bool m_closing = false;
};

// --- A tela ---------------------------------------------------------------------
class HomeView : public brls::Box {
public:
    HomeView() : brls::Box(brls::Axis::COLUMN) {
        s_current = this;
        setSize(brls::Size(kScreenW, kScreenH));
        setHideHighlight(true);
        build();
    }

    // Tema ou idioma novo: recria os filhos no lugar. Enquanto isso o foco fica
    // na própria HomeView (getDefaultFocus devolve `this`), que não é apagada.
    static void reload() {
        if (s_current) s_current->rebuild_views();
    }

    void rebuild_views() {
        m_rebuilding = true;
        setFocusable(true);
        brls::Application::giveFocus(this);
        m_ready = false;
        m_clock_minute = -1;
        clearViews();
        build();
        setFocusable(false);
        m_rebuilding = false;
        brls::Application::giveFocus(getDefaultFocus());
    }

    void build() {
        // Barra superior: avatar | jogo selecionado e tempo | hora e bateria.
        auto top = new brls::Box(brls::Axis::ROW);
        top->setHeight(108);
        top->setPadding(18, 46, 0, 46);
        top->setAlignItems(brls::AlignItems::CENTER);

        auto left = new brls::Box(brls::Axis::ROW);
        left->setWidth(300);
        left->setAlignItems(brls::AlignItems::CENTER);
        m_avatar = new AvatarButton();
        m_avatar_img = new brls::Image();
        m_avatar_img->setSize(brls::Size(kAvatar, kAvatar));
        m_avatar_img->setCornerRadius(kAvatar / 2);
        m_avatar_img->setScalingType(brls::ImageScalingType::FILL);
        m_avatar_initial = new brls::Label();
        m_avatar_initial->setFontSize(28);
        m_avatar_initial->setTextColor(nvgRGB(255, 255, 255));
        m_avatar->addView(m_avatar_initial);
        m_avatar->addView(m_avatar_img);
        m_avatar->registerClickAction([this](brls::View*) {
            cycle_profile();
            return true;
        });
        m_avatar->addGestureRecognizer(new brls::TapGestureRecognizer(m_avatar, [this] { cycle_profile(); }));
        m_avatar->down_target = [this]() -> brls::View* { return m_pager->card(std::max(0, m_sel)); };
        m_avatar->on_focus = [this](bool focused) {
            m_profile->setVisibility(focused ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        };
        left->addView(m_avatar);
        // Apelido do perfil: só com o avatar em foco (o Tico não mostra o nome na grade).
        m_profile = new brls::Label();
        m_profile->setFontSize(18);
        m_profile->setTextColor(tico::kText);
        m_profile->setMarginLeft(20);
        m_profile->setVisibility(brls::Visibility::INVISIBLE);
        left->addView(m_profile);
        top->addView(left);

        auto center = new brls::Box(brls::Axis::COLUMN);
        center->setGrow(1);
        center->setAlignItems(brls::AlignItems::CENTER);
        m_title = new brls::Label();
        m_title->setFontSize(24);
        m_title->setTextColor(tico::kText);
        m_title->setSingleLine(true);
        m_title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        center->addView(m_title);
        m_subtitle = new brls::Label();
        m_subtitle->setFontSize(17);
        m_subtitle->setTextColor(kAccent);
        m_subtitle->setMarginTop(10);
        m_subtitle->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        center->addView(m_subtitle);
        top->addView(center);

        auto right = new brls::Box(brls::Axis::ROW);
        right->setWidth(300);
        right->setJustifyContent(brls::JustifyContent::FLEX_END);
        right->setAlignItems(brls::AlignItems::CENTER);
        m_clock = new brls::Label();
        m_clock->setFontSize(26);
        m_clock->setTextColor(tico::kText);
        m_clock->setMarginRight(18);
        right->addView(m_clock);
        right->addView(new BatteryIcon());
        top->addView(right);
        addView(top);

        // Grade.
        auto area = new brls::Box(brls::Axis::ROW);
        area->setGrow(1);
        area->setAlignItems(brls::AlignItems::CENTER);
        m_pager = new GridPager();
        m_pager->on_select = [this](int i) { select(i); };
        m_pager->on_open = [this](int i) { open_achievements(i); };
        m_pager->up_target = [this]() -> brls::View* { return m_avatar; };
        area->addView(m_pager);
        m_empty = new brls::Label();
        m_empty->setFontSize(20);
        m_empty->setTextColor(tico::kMuted);
        m_empty->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        m_empty->setWidth(kScreenW);
        m_empty->setVisibility(brls::Visibility::GONE);
        area->addView(m_empty);
        addView(area);

        addView(new PageDots(m_pager));

        // Rodapé: (+) Menu (-) Editar à esquerda; (X) Pesquisar (Y) Ordenar (A) Selecionar à direita.
        auto footer = new brls::Box(brls::Axis::ROW);
        footer->setHeight(56);
        footer->setPadding(0, 56, 30, 56);
        footer->setAlignItems(brls::AlignItems::CENTER);
        footer->addView(footer_hint(kGlyphPlus, tr("Menu"), 0));
        footer->addView(footer_hint(kGlyphMinus, tr("Editar"), 20));
        auto spacer = new brls::Box(brls::Axis::ROW);
        spacer->setGrow(1);
        footer->addView(spacer);
        footer->addView(footer_hint(kGlyphX, tr("Pesquisar"), 0));
        footer->addView(footer_hint(kGlyphY, tr("Ordenar"), 20));
        footer->addView(footer_hint(kGlyphA, tr("Selecionar"), 20));
        addView(footer);

        registerAction(tr("Pesquisar"), brls::BUTTON_X, [this](brls::View*) {
            search();
            return true;
        }, true);
        registerAction(tr("Menu"), brls::BUTTON_START, [this](brls::View*) {
            open_menu();
            return true;
        }, true);
        registerAction(tr("Ordenar"), brls::BUTTON_Y, [this](brls::View*) {
            open_sort();
            return true;
        }, true);
        registerAction(tr("Editar"), brls::BUTTON_BACK, [this](brls::View*) {
            edit_item();
            return true;
        }, true);

        refresh();
        m_ready = true;  // antes disto a atividade ainda não existe: o foco vem de getDefaultFocus()
    }

    ~HomeView() override {
        if (s_current == this) s_current = nullptr;
    }

    static void request_refresh() {
        brls::sync([] {
            if (s_current) s_current->refresh();
        });
    }

    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        int64_t now = (int64_t)time(nullptr);
        if (now / 60 != m_clock_minute) {  // hora: atualiza na virada do minuto
            m_clock_minute = now / 60;
            m_clock->setText(data::format_clock(now));
        }
        tico::background(vg, x, y, w, h);  // degradê do tema ou fundo Clássico
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }

    brls::View* getDefaultFocus() override {
        if (m_rebuilding) return this;
        brls::View* v = m_pager->card(m_sel);
        return v ? v : m_avatar;
    }

private:
    bool avatar_focused() const { return brls::Application::getCurrentFocus() == m_avatar; }

    void refresh() {
        int64_t keep = !m_keep_first && m_sel >= 0 && m_sel < (int)m_games.size() ? m_games[m_sel].id : 0;
        m_keep_first = false;
        bool on_avatar = m_ready && avatar_focused();
        if (m_ready) ui::rescue_focus(m_pager, m_avatar);
        data::users(m_users);
        update_profile();

        std::vector<GameRow> all;
        m_games.clear();
        if (!data::games(ui::profile_filter(), all)) {
            show_empty(data::last_error());
            return;
        }
        for (auto& g : all)
            if (m_query.empty() || contains(g.name, m_query)) m_games.push_back(g);

        m_achievements.clear();
        std::vector<AchievementGameRow> ach;
        if (data::achievement_games(ui::profile_filter(), ach))
            for (auto& a : ach) m_achievements[a.game_id] = {a.unlocked, a.total};
        ui::sort_games(m_games, m_achievements, ui::sort_mode());

        m_pager->build(m_games);
        if (m_games.empty()) {
            show_empty(!m_query.empty()
                           ? trf("Nenhum jogo com \"%s\". Aperte X e apague o texto para limpar a busca.", m_query.c_str())
                       : ui::profile_filter() ? std::string(tr("Este perfil ainda não tem sessões."))
                                              : std::string(tr("Nenhum jogo registrado ainda. Jogue algo com o sysmodule ativo.")));
            return;
        }
        m_empty->setVisibility(brls::Visibility::GONE);
        m_pager->setVisibility(brls::Visibility::VISIBLE);
        m_sel = 0;
        for (size_t i = 0; i < m_games.size(); i++)
            if (m_games[i].id == keep) m_sel = (int)i;
        // Trocar de perfil pelo avatar mantém o foco no avatar.
        if (m_ready && !on_avatar) brls::Application::giveFocus(m_pager->card(m_sel));
        select(m_sel);
    }

    void show_empty(const std::string& text) {
        m_sel = -1;
        m_pager->setVisibility(brls::Visibility::GONE);
        m_empty->setText(text);
        m_empty->setVisibility(brls::Visibility::VISIBLE);
        m_title->setText("Ruby Achievements");
        m_subtitle->setText(m_query.empty() ? std::string(" ") : trf("Busca: %s", m_query.c_str()));
        if (m_ready) brls::Application::giveFocus(m_avatar);
    }

    static bool contains(const std::string& hay, const std::string& needle) {
        auto lower = [](std::string s) {
            for (auto& c : s) c = (char)tolower((unsigned char)c);
            return s;
        };
        return lower(hay).find(lower(needle)) != std::string::npos;
    }

    void select(int i) {
        if (i < 0 || i >= (int)m_games.size()) return;
        m_sel = i;
        const GameRow& g = m_games[i];
        m_title->setText(g.name);
        std::string sub = trf("%s jogados", data::format_duration(g.total).c_str());
        auto it = m_achievements.find(g.id);
        if (it != m_achievements.end()) sub += " · " + trf("%d/%d conquistas", it->second.first, it->second.second);
        if (!m_query.empty()) sub += " · " + trf("busca: %s", m_query.c_str());
        m_subtitle->setText(sub);
    }

    // Perfil ativo: avatar da conta (ou inicial) e o apelido.
    void update_profile() {
        int64_t id = ui::profile_filter();
        const UserRow* u = nullptr;
        for (auto& x : m_users)
            if (x.id == id) u = &x;
        if (!u && id != 0) ui::set_profile_filter(0);  // perfil gravado não existe mais
        std::string name = u ? u->nickname : std::string(tr("Todos os perfis"));
        m_profile->setText(name);
        std::vector<unsigned char>* jpeg = nullptr;
        if (u && !u->uid.empty()) {
            auto it = m_avatars.find(u->uid);
            if (it == m_avatars.end()) {
                std::vector<unsigned char> data_;
                data::user_avatar(u->uid, data_);
                it = m_avatars.emplace(u->uid, std::move(data_)).first;
            }
            if (!it->second.empty()) jpeg = &it->second;
        }
        if (jpeg) {
            m_avatar_img->setImageFromMem(jpeg->data(), (int)jpeg->size());
            m_avatar_img->setVisibility(brls::Visibility::VISIBLE);
            m_avatar_initial->setVisibility(brls::Visibility::GONE);
        } else {
            m_avatar_img->setVisibility(brls::Visibility::GONE);
            m_avatar_initial->setVisibility(brls::Visibility::VISIBLE);
            size_t n = 1;  // primeiro caractere UTF-8 inteiro
            while (n < name.size() && ((unsigned char)name[n] & 0xC0) == 0x80) n++;
            m_avatar_initial->setText(name.substr(0, n));
        }
    }

    // A no avatar ou toque nele: próximo perfil (Todos -> cada perfil -> Todos).
    void cycle_profile() {
        std::vector<int64_t> order{0};
        for (auto& u : m_users) order.push_back(u.id);
        size_t i = 0;
        while (i < order.size() && order[i] != ui::profile_filter()) i++;
        ui::set_profile_filter(order[(i + 1) % order.size()]);
        brls::Application::getAudioPlayer()->play(brls::SOUND_CLICK);
        refresh();
    }

    const GameRow* selected() const { return m_sel >= 0 && m_sel < (int)m_games.size() ? &m_games[m_sel] : nullptr; }

    void open_achievements(int i) {
        if (i < 0 || i >= (int)m_games.size()) return;
        brls::Application::pushActivity(new AchievementListActivity(m_games[i].id, m_games[i].name));
    }

    // - : editar o item (tela do jogo: nome, capa, unificar, perfil, sessões).
    void edit_item() {
        const GameRow* g = selected();
        if (!g || avatar_focused()) return;
        brls::Application::pushActivity(new GameActivity(g->id));
    }

    // X: pesquisa; o teclado abre com a busca atual, e texto vazio limpa.
    void search() {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                brls::sync([this, text] {
                    m_query = text;
                    refresh();
                });
            },
            tr("Pesquisar jogo"), tr("Parte do nome (vazio limpa a busca)"), 64, m_query);
    }

    // Y: escolhe a ordem da grade (fica no config.ini).
    void open_sort() {
        ui::pick(tr("Ordenar"), {tr("Jogados recentemente"), tr("A-Z (ordem alfabética)"), tr("Mais tempo de jogo"),
                                 tr("Mais % de conquistas desbloqueadas")},
                 [this](int i) {
                     ui::set_sort_mode(i);
                     m_keep_first = true;  // a grade recomeça no primeiro jogo da nova ordem
                     refresh();
                 },
                 ui::sort_mode());
    }

    // + : menu modal. Com um jogo em foco: Configurações do Item e Sessões; no
    // avatar (ou sem jogos), só a engrenagem e o sair.
    void open_menu() {
        std::vector<MenuEntry> items;
        const GameRow* g = avatar_focused() ? nullptr : selected();
        if (g) {
            GameRow game = *g;
            items.push_back({tr("Configurações do Item"), [game] { brls::Application::pushActivity(new GameActivity(game.id)); }});
            items.push_back({tr("Sessões"), [game] { ui::open_sessions(game); }});
        }
        // O título da barra dá lugar ao "Menu" do modal.
        m_title->setAlpha(0);
        m_subtitle->setAlpha(0);
        auto restore = [] {
            if (s_current) {
                s_current->m_title->setAlpha(1);
                s_current->m_subtitle->setAlpha(1);
            }
        };
        brls::Application::pushActivity(new brls::Activity(new TicoMenu(std::move(items), restore)),
                                        brls::TransitionAnimation::FADE);
    }

    static HomeView* s_current;

    GridPager* m_pager = nullptr;
    AvatarButton* m_avatar = nullptr;
    brls::Image* m_avatar_img = nullptr;
    brls::Label* m_avatar_initial = nullptr;
    brls::Label* m_profile = nullptr;
    brls::Label* m_title = nullptr;
    brls::Label* m_subtitle = nullptr;
    brls::Label* m_clock = nullptr;
    brls::Label* m_empty = nullptr;
    std::vector<UserRow> m_users;
    std::vector<GameRow> m_games;
    std::map<int64_t, std::pair<int, int>> m_achievements;  // game_id -> (desbloqueadas, total)
    std::map<std::string, std::vector<unsigned char>> m_avatars;
    std::string m_query;
    int m_sel = -1;
    bool m_keep_first = false;  // a próxima refresh foca o primeiro jogo (troca de ordem)
    int64_t m_clock_minute = -1;
    bool m_ready = false;
    bool m_rebuilding = false;
};

HomeView* HomeView::s_current = nullptr;

}  // namespace

// --- Atividade ----------------------------------------------------------------------
brls::View* HomeActivity::createContentView() {
    // Pacotes de conquistas do SD: os jogos nativos aparecem sem precisar abrir o jogo
    // (uma vez por execução; a tela inicial é recriada ao trocar tema ou idioma).
    static bool packs_synced = false;
    if (!packs_synced) {
        packs_synced = true;
        if (!data::sync_achievement_packs()) brls::Logger::warning("pacotes de conquistas: %s", data::last_error().c_str());
    }
    return new HomeView();
}

void HomeActivity::onResume() {
    // Voltou de um jogo, das conquistas ou das configurações: totais podem ter mudado.
    request_refresh();
}

void HomeActivity::request_refresh() { HomeView::request_refresh(); }

void HomeActivity::reload() { HomeView::reload(); }
