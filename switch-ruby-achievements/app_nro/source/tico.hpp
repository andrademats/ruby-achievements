#pragma once
// Linguagem visual do launcher Tico (referência: capturas do Tico, fora do repositório),
// usada por todas as telas do app: fundo claro, cartões arredondados com
// sombra suave, borda de seleção prateada, cabeçalho com título, hora e
// bateria, e rodapé com as dicas dos botões.
//
// Botões fora da tela inicial: A = OK / confirmar / selecionar; B = voltar /
// cancelar (Frame, Tabs e os modais registram o B).
#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

#include "i18n.hpp"

namespace tico {

constexpr float kScreenW = 1280;
constexpr float kScreenH = 720;

// Paleta do tema atual (prefs::theme(): claro do Tico ou escuro em tons de
// chumbo). Preenchida por apply_theme(); as views copiam as cores ao serem
// criadas, então trocar o tema exige recriar as telas (ui::reload_interface).
extern NVGcolor kBackgroundTop, kBackgroundBottom;
extern NVGcolor kPanel, kSlot, kText, kMuted, kIcon, kSidebar;
extern NVGcolor kFocusFill;    // fundo de botão/linha/aba em foco
extern NVGcolor kChevron;      // botões (<) (>) do indicador de páginas
extern NVGcolor kRowOpen;      // conquista desbloqueada
extern NVGcolor kRowLocked;    // conquista bloqueada
extern NVGcolor kBadgeLocked;  // quadro do ícone da conquista bloqueada (sem imagem)
bool dark();                   // tema escuro ativo

// Glifos dos botões na fonte do sistema.
extern const char* const kGlyphA;
extern const char* const kGlyphB;
extern const char* const kGlyphX;
extern const char* const kGlyphPlus;
extern const char* const kGlyphMinus;

// Ícones da fonte Material (desenhados por Icon).
namespace icon {
constexpr const char* kInfo = "";
constexpr const char* kStar = "";
constexpr const char* kDelete = "";
constexpr const char* kHistory = "";
constexpr const char* kTimerOff = "";
constexpr const char* kTune = "";
constexpr const char* kKey = "\uE0DA";      // vpn_key (aba Chaves)
constexpr const char* kPalette = "\uE40A";  // palette (aba Interface)
constexpr const char* kSchedule = "\uE8B5";  // schedule (tempo total na barra lateral)
constexpr const char* kUpdate = "\uE923";    // update (aba Atualiza\u00E7\u00E3o)
}  // namespace icon

// Tempo para animações em microssegundos (relógio do Borealis) e em segundos.
int64_t now_usec();
float now_seconds();

// Paleta do tema escolhido (prefs::theme()) e tema do Borealis com as mesmas
// cores (destaque prateado em células, listas, botões e modais). Chamar antes
// de criar a primeira view e de novo ao trocar o tema.
void apply_theme();

// --- Desenho ---------------------------------------------------------------------
// Fundo das telas: degradê do tema ou, no estilo Clássico (prefs::background(),
// o padrão), uma imagem do romfs (escurecida no tema escuro): fundo.jpg em todas
// as telas e fundo_settings.jpg só nas Configurações globais.
enum class Backdrop { SCREEN, SETTINGS };
void background(NVGcontext* vg, float x, float y, float w, float h, Backdrop kind = Backdrop::SCREEN);
bool classic_background();  // a imagem do estilo Clássico está sendo desenhada
void soft_shadow(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha);
// Espessura da borda de foco (cards, botões, abas, células).
constexpr float kRingStroke = 5;
// Borda de seleção prateada que gira no sentido horário, por fora do retângulo;
// `inset` px para dentro (sobre a view), 0 = encostada.
void ring(NVGcontext* vg, float x, float y, float w, float h, float r, float alpha, float inset = 0);
void ring_circle(NVGcontext* vg, float cx, float cy, float r, float alpha);
void draw_gear(NVGcontext* vg, float cx, float cy, float r, NVGcolor color);
void draw_exit(NVGcontext* vg, float cx, float cy, float r, NVGcolor color);
void draw_triangle(NVGcontext* vg, float cx, float cy, float s, int dir, NVGcolor color);

// --- Componentes ------------------------------------------------------------------
// Bateria desenhada (contorno, carga proporcional, verde carregando).
class Battery : public brls::View {
public:
    Battery();
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;
};

// Hora "14:05", atualizada na virada do minuto.
class Clock : public brls::Label {
public:
    Clock();
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

private:
    int64_t m_minute = -1;
};

// Ícone da fonte Material centralizado na área da view.
class Icon : public brls::View {
public:
    Icon(const char* glyph, float size, NVGcolor color);
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;

private:
    std::string m_glyph;
    float m_size;
    NVGcolor m_color;
};

brls::Label* hint(const char* glyph, const std::string& text, float margin_left);

// Cartão arredondado (fundo do painel do Tico) para empilhar células.
class Panel : public brls::Box {
public:
    Panel();
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;
};
// Título de seção acima de um painel ("Scrapers", "RetroAchievements").
brls::Label* section(const std::string& text);

// --- Abas das Configurações globais: todas seguem o molde da aba Sobre -------------
// Título de seção (17 px, cinza) acima de cada cartão; cartão justo nas células (6 px
// em cima e embaixo, o texto corrido traz 12 px de respiro); textos e células em
// 18 px, com o texto alinhado ao título das
// células (22 px da borda do cartão). Sem linhas entre os itens: o cartão agrupa.
constexpr float kPageText = 18;
// Cartão de uma aba (Panel no recuo do molde).
Panel* card();
// Texto corrido dentro de um cartão.
brls::Label* card_text(const std::string& text);
// Célula (título à esquerda, detalhe à direita) no tamanho de fonte do molde.
brls::DetailCell* cell(const std::string& title = "", const std::string& detail = "");
// Conteúdo de uma aba: coluna com o recuo lateral do molde. Cartões e títulos nunca
// encolhem abaixo do conteúdo (o Yoga espremia os cartões quando a aba passava da
// altura da tela e o texto vazava das bordas).
class Page : public brls::Box {
public:
    // `scroll`: abas só de cartões; o conteúdo rola quando passa da altura da aba.
    // Sem rolagem: abas com uma lista rolável num cartão `grow`.
    explicit Page(bool scroll = false);
    // Acrescenta o título de seção e um cartão; `grow` = o cartão ocupa a altura que
    // sobra (listas roláveis; não vale numa página com `scroll`).
    Panel* add_card(const std::string& title, bool grow = false);

private:
    brls::Box* m_body;  // onde os cartões entram (a própria página ou o conteúdo da rolagem)
};

// Lista rolável com a barra de rolagem à direita do conteúdo (a do Borealis fica
// 14 px para dentro e cobria textos e cards): a lista se estende kScrollGutter px
// para a direita e o conteúdo recua o mesmo tanto. O foco entra e sai dela pelo
// direcional nas quatro direções (o ScrollingFrame do Borealis prendia o foco dentro).
// Toda lista rolável do app passa por aqui.
constexpr float kScrollGutter = 26;
brls::ScrollingFrame* scroller();
void set_scroll_content(brls::ScrollingFrame* scroll, brls::View* content);

// Tela no estilo do Tico (substitui o AppletFrame): título à esquerda, hora e
// bateria à direita, conteúdo e as dicas (B) Voltar (A) OK no rodapé. `backdrop`:
// imagem do estilo Clássico (SETTINGS nas telas de configuração, como a do RA).
class Frame : public brls::Box {
public:
    explicit Frame(brls::View* content, const std::string& title = "", Backdrop backdrop = Backdrop::SCREEN);
    void setTitle(const std::string& title);
    // Linha menor logo abaixo do título ("" esconde).
    void setSubtitle(const std::string& text);
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override;

private:
    brls::Label* m_title;
    brls::Label* m_subtitle;
    brls::View* m_content;
    Backdrop m_backdrop;
};

// Configurações com a barra lateral do Tico: avatar e perfil no topo, abas com
// ícone; o conteúdo da aba em foco fica à direita, com o nome dela no topo.
class Tabs : public brls::Box {
public:
    Tabs();
    void addTab(const char* glyph, const std::string& name, std::function<brls::View*()> make);
    void select(size_t index);  // mostra a aba `index` e a deixa como foco inicial
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override;
    brls::View* getDefaultFocus() override;

    class Holder;  // área do conteúdo (esquece o foco da aba destruída)

private:
    void show(size_t index);

    struct Tab {
        brls::View* item;
        std::string name;
        std::function<brls::View*()> make;
    };
    std::vector<Tab> m_tabs;
    size_t m_current = (size_t)-1;
    brls::Box* m_items = nullptr;
    brls::Label* m_title = nullptr;
    Holder* m_holder = nullptr;
};

// --- Modais (A = OK, B = cancelar) ------------------------------------------------
// Lista de opções; `cb` recebe o índice escolhido (no quadro seguinte). `cancelled` roda
// (no quadro seguinte) se o modal fechar sem escolha (B ou toque fora).
void pick(const std::string& title, const std::vector<std::string>& options, std::function<void(int)> cb,
          int selected = 0, std::function<void()> cancelled = {});
// Pergunta com Cancelar / `action`.
void confirm(const std::string& text, const std::string& action, std::function<void()> cb);
// Aviso com um botão OK.
void message(const std::string& text);

}  // namespace tico
