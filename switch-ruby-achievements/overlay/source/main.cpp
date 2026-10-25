// Ruby Achievements - overlay Tesla/Ultrahand
//
// Menu (aberto pelo Ultrahand/Tesla): jogo em execução com o tempo total, a
// lista de conquistas dele e o atalho de volta ao menu de overlays. O tempo de
// jogo aparece só aqui: não há mais HUD sobre a tela.
// Fora do menu o overlay fica em segundo plano, sem desenhar nada e sem
// capturar botões, só para mostrar o pop-up "Conquista desbloqueada".
#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <errno.h>
#include <strings.h>

#include <atomic>
#include <string.h>
#include <string>
#include <vector>

#include "pt_config.h"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_status.h"
#include "pt_util.h"
#include "popup_anim.hpp"
#include "popup_queue.hpp"
#include "popup_sound.hpp"
#include "pt_wav.hpp"

bool load_popup_icon(const char* path, u8* out);  // popup_art.cpp

namespace {

#include "trophies.inc"


PtConfig g_cfg;
bool g_popups_active = false;  // PopupGui está no topo da pilha

// Modo do overlay (ver o laço persistente no fim do arquivo).
enum class Mode { Hidden, Menu, Popups };

#define OLOG(...) pt_log_file(PT_OVERLAY_LOG_PATH, __VA_ARGS__)

// Um "segundo" pelo contador de ticks do sistema: independe do relógio de
// parede (que pode falhar ou pular), então a tela sempre se atualiza.
bool second_elapsed(u64* last_tick) {
    u64 t = armGetSystemTick();
    if (*last_tick != 0 && armTicksToNs(t - *last_tick) < 1'000'000'000ULL) return false;
    *last_tick = t;
    return true;
}

// --- Jogo em execução ------------------------------------------------------------
struct NowPlaying {
    bool status_ok = false;  // status.txt lido (agora ou há poucas leituras)
    bool running = false;    // há um jogo aberto
    std::string name;
    int64_t total = 0;       // segundos: sessões anteriores + a sessão atual
    int64_t session = 0;     // segundos da sessão atual
    int64_t age = -1;        // segundos desde a última escrita do sysmodule
    int users = 0;
};

// Registra no overlay.log só as mudanças de estado da leitura.
void log_read_state(bool ok, int err, const PtStatus& st, int users) {
    static int last = -1;  // -1 = nada registrado, 0 = falha, 1 = ok
    static int64_t last_game = -1, last_session = -1;
    if (!ok) {
        if (last != 0) OLOG("status.txt ilegivel: %s (errno %d)", strerror(err), err);
        last = 0;
        return;
    }
    if (last == 1 && st.game_id == last_game && st.session_id == last_session) return;
    last = 1;
    last_game = st.game_id;
    last_session = st.session_id;
    OLOG("status.txt ok: titulo %s, jogo %lld \"%s\", sessao %lld, base %lld s, viva %lld s, contando %d; "
         "users.txt: %d perfis",
         st.title_id[0] ? st.title_id : "-", (long long)st.game_id, st.game_name, (long long)st.session_id,
         (long long)st.base, (long long)st.live, st.counting, users);
}

NowPlaying read_now_playing() {
    // O sysmodule troca o arquivo de forma atômica (apaga + renomeia): uma
    // leitura pode falhar por um instante. Usa o último status bom por 5 leituras.
    static PtStatus last;
    static bool have_last = false;
    static int misses = 0;
    NowPlaying out;
    PtUserEntry users[16];
    out.users = pt_users_read(users, 16);
    PtStatus st;
    errno = 0;
    bool ok = pt_status_read(&st) == 0;
    log_read_state(ok, errno, st, out.users);
    if (ok) {
        last = st;
        have_last = true;
        misses = 0;
    } else {
        if (!have_last || ++misses > 5) return out;
        st = last;
    }
    out.status_ok = true;
    int64_t now = pt_now();
    out.age = now > 0 ? now - st.written_at : -1;
    out.running = st.title_id[0] != '\0' && st.game_id > 0;
    if (!out.running) return out;
    out.name = st.game_name;
    // O sysmodule regrava a cada checkpoint (60 s); entre eles o relógio
    // avança aqui, até 2 min além da última escrita.
    int64_t live = st.live;
    if (st.counting && out.age > 0) live += std::min<int64_t>(out.age, 120);
    out.session = live;
    out.total = st.base + (st.counting ? live : 0);
    return out;
}

// Quadro com o nome do jogo e o tempo total em destaque.
class NowPlayingItem : public tsl::elm::Element {
public:
    void set(const NowPlaying& np) { m_np = np; }

    void draw(tsl::gfx::Renderer* r) override {
        s32 x = getX() + 20, w = getWidth() - 40;
        r->drawRect(getX(), getTopBound(), getWidth(), 1, a(tsl::style::color::ColorFrame));
        if (!m_np.status_ok) {
            r->drawString("Sysmodule inativo", false, x, getY() + 44, 22, a(tsl::style::color::ColorText));
            r->drawString("status.txt não foi encontrado", false, x, getY() + 74, 15,
                          a(tsl::style::color::ColorDescription));
            return;
        }
        if (!m_np.running) {
            r->drawString("Nenhum jogo em execução", false, x, getY() + 44, 22, a(tsl::style::color::ColorText));
            r->drawString("Abra um jogo e volte aqui para ver o tempo", false, x, getY() + 74, 15,
                          a(tsl::style::color::ColorDescription));
            return;
        }
        char total[32], session[32];
        pt_format_duration(total, sizeof(total), m_np.total);
        pt_format_duration(session, sizeof(session), m_np.session);
        std::string name = r->limitStringLength(m_np.name, false, 18, w);
        r->drawString(name.c_str(), false, x, getY() + 30, 18, a(tsl::style::color::ColorDescription));
        r->drawString(total, false, x, getY() + 76, 40, a(tsl::style::color::ColorHighlight));
        std::string sub = std::string("nesta sessão: ") + session;
        r->drawString(sub.c_str(), false, x, getY() + 104, 15, a(tsl::style::color::ColorDescription));
    }

    void layout(u16, u16, u16, u16) override {}

private:
    NowPlaying m_np;
};

// --- Pop-up de conquista ----------------------------------------------------------
// O sysmodule exporta a fila em PT_POPUP_PATH (popup_queue.hpp); o overlay mostra uma
// por vez e grava o último id em PT_POPUP_SEEN_PATH, para o sysmodule apagar da fila e
// não repetir depois de recarregar o overlay.
using popup::Popup;
using popup::Rarity;

int64_t g_popup_seen = -1;  // -1 = ainda não lido do arquivo
popup::Flood g_flood;       // leva atual (sobrevive ao menu, que destrói a tela dos pop-ups)

// Conquistas que esperam na fila (id > último visto).
std::vector<Popup> pending_popups() {
    if (g_popup_seen < 0) {
        char b[32];
        g_popup_seen = pt_read_file(PT_POPUP_SEEN_PATH, b, sizeof(b)) > 0 ? strtoll(b, NULL, 10) : 0;
    }
    static char buf[8192];  // até PT_POPUP_QUEUE_MAX linhas de ~460 bytes
    if (pt_read_file(PT_POPUP_PATH, buf, sizeof(buf)) <= 0) return {};
    return popup::parse_queue(buf, g_popup_seen);
}

void mark_popup_seen(int64_t id) {
    g_popup_seen = id;
    char b[32];
    int n = snprintf(b, sizeof(b), "%lld\n", (long long)id);
    pt_write_file_atomic(PT_POPUP_SEEN_PATH, b, (size_t)n);
}

// Proteção contra flood: descarta a fila se ela fecha uma leva longa. Devolve quantos
// pop-ups foram descartados (0 = nada a fazer).
size_t drop_flood(const std::vector<Popup>& queue, bool enabled, const char* why) {
    if (!g_flood.drop(enabled, (int)queue.size())) return 0;
    mark_popup_seen(queue.back().id);
    OLOG("protecao contra flood (%s): %zu pop-ups descartados (leva de %d conquistas; continuam gravadas)", why,
         queue.size(), g_flood.wave());
    return queue.size();
}

// Som do pop-up: o escolhido no app (config.ini popup_sfx) se seguir as regras de
// pt_wav.hpp; senão o padrão. Os pacotes não têm som.
std::string popup_sfx() {
    if (!g_cfg.popup_sfx[0]) return PT_POPUP_SFX_PATH;
    std::string path = pt::sfx_path(g_cfg.popup_sfx);
    pt::Wav w;
    bool parsed = pt::read_wav(path.c_str(), w);
    if (pt::check_sfx(parsed, w) == pt::SfxCheck::OK) return path;
    static std::string warned;
    if (warned != path) OLOG("som do pop-up: %s fora das regras (WAV PCM 16 bits 48 kHz, 3 a 10 s); usando o padrao",
                             path.c_str());
    warned = path;
    return PT_POPUP_SFX_PATH;
}

// --- Desenho -----------------------------------------------------------------------
using tsl::gfx::Renderer;

const tsl::Color kPopupBg(0x1, 0x1, 0x2, 0xE);      // fundo escuro
const tsl::Color kPopupTitle(0xF, 0xF, 0xF, 0xF);
const tsl::Color kPopupSub(0xF, 0xC, 0x2, 0xF);     // a mesma cor de antes ("Conquista desbloqueada")

tsl::Color faded(tsl::Color c, float k) {
    c.a = (u8)std::lround(c.a * std::clamp(k, 0.0f, 1.0f));
    return c;
}

// Retângulo arredondado com as bordas suavizadas (popup::rrect_coverage).
void fill_round(Renderer* r, float x, float y, float w, float h, float rad, tsl::Color c) {
    const int x0 = (int)std::floor(x), x1 = (int)std::ceil(x + w), y0 = (int)std::floor(y), y1 = (int)std::ceil(y + h);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float cov = popup::rrect_coverage(px + 0.5f, py + 0.5f, x, y, w, h, rad);
            if (cov > 0) r->setPixelBlendDst(px, py, Renderer::a(faded(c, cov)));
        }
}

// Bitmap RGBA8888 quadrado (size x size) centrado em (cx, cy), escalado (vizinho mais próximo).
void draw_rgba(Renderer* r, const u8* bmp, int size, float cx, float cy, float scale, float alpha) {
    const int d = (int)std::lround(size * scale);
    if (d <= 0) return;
    const int x0 = (int)std::lround(cx - d / 2.0f), y0 = (int)std::lround(cy - d / 2.0f);
    for (int y = 0; y < d; y++)
        for (int x = 0; x < d; x++) {
            const u8* p = bmp + ((size_t)(y * size / d) * size + (x * size / d)) * 4;
            u8 al = (u8)(p[3] * alpha);
            if (al < 16) continue;
            r->setPixelBlendSrc(x0 + x, y0 + y, Renderer::a(tsl::Color(p[0] >> 4, p[1] >> 4, p[2] >> 4, al >> 4)));
        }
}

const u8* trophy(Rarity r) {
    switch (r) {
        case Rarity::Silver: return kTrophySilver;
        case Rarity::Gold: return kTrophyGold;
        case Rarity::Ruby: return kTrophyRuby;
        default: return kTrophyBronze;
    }
}

// Largura do texto (cor transparente: o libtesla só mede).
u32 text_width(Renderer* r, const std::string& s, float size) {
    return r->drawString(s.c_str(), false, 0, 0, size, tsl::Color(0, 0, 0, 0)).first;
}

// Título no espaço do texto: 18 px; se não couber, 16 e 15; senão corta com "…".
std::string fit_title(Renderer* r, const std::string& s, float* size) {
    for (float fs : {18.0f, 16.0f, 15.0f}) {
        *size = fs;
        if ((s32)text_width(r, s, fs) <= popup::kTextW) return s;
    }
    std::string t = s;
    while (!t.empty()) {
        do t.pop_back();
        while (!t.empty() && ((u8)t.back() & 0xC0) == 0x80);  // UTF-8 inteiro
        if (!t.empty()) t.pop_back();
        if ((s32)text_width(r, t + "…", *size) <= popup::kTextW) return t + "…";
    }
    return "…";
}

std::atomic<bool> g_popup_busy{false};  // pop-up na tela: o laço desenha a ~60 quadros/s

class PopupBanner {
public:
    static constexpr u64 kGapMs = 1000;  // pausa entre um pop-up e o próximo

    ~PopupBanner() {
        if (m_active) sound::stop();
        g_popup_busy = false;
    }

    // Chamado a cada segundo: pega a próxima conquista da fila.
    void poll() {
        if (m_active) return;
        if (m_end != 0 && armTicksToNs(armGetSystemTick() - m_end) / 1000000ULL < kGapMs) return;
        std::vector<Popup> queue = pending_popups();
        if (queue.empty()) {
            g_flood.drop(false, 0);  // fila vazia: a leva acabou
            return;
        }
        pt_config_load(&g_cfg);  // idioma, som e proteção contra flood podem ter mudado
        if (drop_flood(queue, g_cfg.popup_flood, "fila longa")) return;
        m_popup = queue.front();
        // O ícone vem antes de marcar: o sysmodule apaga do cache o que saiu da fila.
        m_has_icon = load_popup_icon(m_popup.icon.c_str(), m_icon);
        // Marca já ao começar: abrir o menu no meio do pop-up destrói esta tela,
        // e marcando só no fim ele aparecia de novo depois (log de 2026-10-20).
        mark_popup_seen(m_popup.id);
        g_flood.shown();
        m_subtitle = g_cfg.app_language == 1 ? "Conquista desbloqueada" : "Achievement unlocked";
        m_fitted = false;
        std::string sfx = popup_sfx();
        m_total = popup::total_ms(sound::duration_ms(sfx.c_str()));
        m_active = true;
        g_popup_busy = true;
        m_start = armGetSystemTick();
        sound::play(sfx.c_str(), m_total);
        OLOG("pop-up de conquista %lld: \"%s\" (%s, %u ms%s)", (long long)m_popup.id, m_popup.title.c_str(),
             kRarityNames[(int)m_popup.rarity], m_total, m_has_icon ? "" : ", sem ícone");
    }

    void draw(Renderer* r) {
        if (!m_active) return;
        u64 ms = armTicksToNs(armGetSystemTick() - m_start) / 1000000ULL;
        popup::Frame f = popup::frame_at((u32)std::min<u64>(ms, UINT32_MAX), m_total);
        if (!f.visible) {
            m_active = false;
            g_popup_busy = false;
            m_end = armGetSystemTick();
            return;
        }
        if (!m_fitted) {
            m_title = fit_title(r, m_popup.title, &m_title_size);
            m_fitted = true;
        }
        const float H = popup::kH, right = (float)tsl::cfg::FramebufferWidth - popup::kMarginX,
                    top = popup::kMarginY;
        // Troféu da raridade no centro do selo: é o que surge no estalo e fica parado ali
        // enquanto o pop-up abre e fecha (popup::kRarityPad).
        const float cx = right - H / 2, cy = top + H / 2;
        if (!f.text) {  // só o selo, crescendo ou sumindo em torno do centro
            const float d = H * f.scale;
            fill_round(r, cx - d / 2, cy - d / 2, d, d, popup::kRadius * f.scale, faded(kPopupBg, f.alpha));
            rarity(r, cx, cy, f.scale, f.alpha);
            return;
        }
        // Aberto (ou abrindo/fechando): a borda direita e o troféu ficam parados; o ícone
        // e o texto, presos à esquerda, aparecem conforme o pop-up se estende.
        const float x = right - f.width;
        fill_round(r, x, top, f.width, H, popup::kRadius, kPopupBg);
        r->enableScissoring((s32)x, (s32)top, (s32)f.width - 1, popup::kH - 1);
        icon(r, x + popup::kPad + popup::kIcon / 2.0f, cy, 1, 1);
        r->drawString(m_title.c_str(), false, (s32)x + popup::kTextX, (s32)top + 29, m_title_size, kPopupTitle);
        r->drawString(m_subtitle, false, (s32)x + popup::kTextX, (s32)top + 49, 14, kPopupSub);
        rarity(r, cx, cy, 1, 1);
        r->disableScissoring();
    }

private:
    static constexpr const char* kRarityNames[] = {"bronze", "silver", "gold", "ruby"};

    // Ícone da conquista; sem ele, fundo branco com uma estrela (kStarIcon). Nunca o
    // troféu: ele já aparece à direita.
    void icon(Renderer* r, float cx, float cy, float scale, float alpha) {
        draw_rgba(r, m_has_icon ? m_icon : kStarIcon, popup::kIcon, cx, cy, scale, alpha);
    }

    // Troféu da raridade (bronze, prata, ouro, ruby) no tamanho popup::kRarity.
    void rarity(Renderer* r, float cx, float cy, float scale, float alpha) {
        draw_rgba(r, trophy(m_popup.rarity), kTrophySize, cx, cy, scale * popup::kRarity / kTrophySize, alpha);
    }

    Popup m_popup;
    u8 m_icon[popup::kIcon * popup::kIcon * 4];
    bool m_has_icon = false;
    const char* m_subtitle = "";
    std::string m_title;
    float m_title_size = 18;
    bool m_fitted = false;
    bool m_active = false;
    u32 m_total = popup::kDefaultMs;
    u64 m_start = 0;
    u64 m_end = 0;  // fim do último pop-up (pausa até o próximo)
};

// --- Segundo plano: só os pop-ups de conquista ------------------------------------
class PopupGui : public tsl::Gui {
public:
    PopupGui() { g_popups_active = true; }
    ~PopupGui() override { g_popups_active = false; }

    tsl::elm::Element* createUI() override {
        // Sem OverlayFrame: nada de fundo. O CustomDrawer do libtesla recorta o
        // desenho pelos seus limites; como elemento de topo ninguém os define
        // (0x0 = nada aparece), então ele ocupa o framebuffer inteiro.
        auto drawer = new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32, s32, s32, s32) {
            r->clearScreen();
            m_popup.draw(r);
        });
        drawer->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
        return drawer;
    }

    void update() override {
        if (second_elapsed(&m_tick)) m_popup.poll();
    }

private:
    PopupBanner m_popup;
    u64 m_tick = 0;
};

// --- Conquistas do jogo aberto ---------------------------------------------------
// Lista exportada pelo sysmodule em PT_ACH_CURRENT_PATH (ver achieve.h).
struct AchRow {
    bool unlocked = false;
    std::string title, desc;
};

struct AchList {
    std::string game;  // vazio = nenhum jogo aberto
    std::vector<AchRow> rows;
};

std::vector<std::string> split(const std::string& line, size_t max_parts) {
    std::vector<std::string> out;
    size_t start = 0;
    while (out.size() + 1 < max_parts) {
        size_t bar = line.find('|', start);
        if (bar == std::string::npos) break;
        out.push_back(line.substr(start, bar - start));
        start = bar + 1;
    }
    out.push_back(line.substr(start));
    return out;
}

AchList read_achievements() {
    AchList list;
    static char buf[48 * 1024];  // 150 linhas no máximo
    if (pt_read_file(PT_ACH_CURRENT_PATH, buf, sizeof(buf)) <= 0) return list;
    std::string text = buf;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.rfind("G|", 0) == 0) {
            list.game = line.substr(2);
        } else if (line.rfind("A|", 0) == 0) {
            // A|<desbloqueada>|<unlocked_at>|<título>|<descrição>
            auto f = split(line.substr(2), 4);
            if (f.size() < 4) continue;
            AchRow r;
            r.unlocked = f[0] == "1";
            r.title = f[2].empty() ? "Conquista secreta" : f[2];
            r.desc = f[2].empty() ? "Desbloqueie para ver" : f[3];
            list.rows.push_back(r);
        }
    }
    return list;
}

// Linha da lista: título e descrição; marcador dourado se desbloqueada.
class AchItem : public tsl::elm::Element {
public:
    explicit AchItem(const AchRow& r) : m_row(r) {}

    void draw(tsl::gfx::Renderer* r) override {
        if (m_cut_w != getWidth()) {
            m_cut_w = getWidth();
            s32 max = getWidth() - 130;
            m_title = r->limitStringLength(m_row.title, false, 20, max);
            m_desc = r->limitStringLength(m_row.desc.empty() ? " " : m_row.desc, false, 15, max);
        }
        r->drawRect(getX(), getTopBound(), getWidth(), 1, a(tsl::style::color::ColorFrame));
        tsl::Color gold(0xF, 0xB, 0x2, 0xF);
        tsl::Color title = m_row.unlocked ? tsl::style::color::ColorText : tsl::Color(0x9, 0x9, 0x9, 0xF);
        // Marcador à esquerda: cheio = desbloqueada.
        r->drawRect(getX() + 12, getY() + 18, 6, getHeight() - 36, a(m_row.unlocked ? gold : tsl::Color(0x5, 0x5, 0x5, 0xF)));
        r->drawString(m_title.c_str(), false, getX() + 30, getY() + 33, 20, a(title));
        r->drawString(m_desc.c_str(), false, getX() + 30, getY() + 60, 15, a(tsl::style::color::ColorDescription));
    }

    void layout(u16, u16, u16, u16) override {}

    // Focável só para a lista rolar com o direcional.
    tsl::elm::Element* requestFocus(tsl::elm::Element*, tsl::FocusDirection) override { return this; }

private:
    AchRow m_row;
    std::string m_title, m_desc;
    s32 m_cut_w = -1;
};

class AchievementsGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        AchList data = read_achievements();
        int got = 0;
        for (auto& r : data.rows) got += r.unlocked;
        std::string sub = data.game.empty() ? "Nenhum jogo aberto"
                          : data.rows.empty()
                              ? "Sem conquistas definidas"
                              : std::to_string(got) + "/" + std::to_string(data.rows.size()) + " conquistas";
        auto frame = new tsl::elm::OverlayFrame(data.game.empty() ? "Conquistas" : data.game, sub);
        auto list = new tsl::elm::List();
        if (data.game.empty()) {
            list->addItem(new tsl::elm::CategoryHeader("Abra um jogo para ver as conquistas dele"));
        } else if (data.rows.empty()) {
            list->addItem(new tsl::elm::CategoryHeader("Crie " PT_ACH_DIR "/<Title ID>.rubydb"));
            list->addItem(new tsl::elm::CategoryHeader("ou sincronize o RetroAchievements no app"));
        } else {
            for (auto& r : data.rows) list->addItem(new AchItem(r), 84);
        }
        frame->setContent(list);
        return frame;
    }
};

// --- Menu principal (a aba do Ultrahand) -------------------------------------------
class MenuGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Ruby Achievements", "Tempo de jogo");
        auto list = new tsl::elm::List();

        list->addItem(new tsl::elm::CategoryHeader("Jogo em execução"));
        m_now = new NowPlayingItem();
        m_now->set(read_now_playing());
        list->addItem(m_now, 120);

        auto ach = new tsl::elm::ListItem("Conquistas do jogo");
        ach->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<AchievementsGui>();
            return true;
        });
        list->addItem(ach);

        list->addItem(new tsl::elm::CategoryHeader("Pop-ups de conquista"));
        pt_config_load(&g_cfg);
        auto flood = new tsl::elm::ToggleListItem("Proteção contra flood", g_cfg.popup_flood, "Ligado", "Desligado");
        flood->setStateChangedListener([](bool on) {
            PtConfig c;
            pt_config_load(&c);
            c.popup_flood = on;
            g_cfg.popup_flood = on;
            if (pt_config_save_app(&c) != 0) OLOG("config.ini: protecao contra flood nao gravada");
            OLOG("protecao contra flood: %s", on ? "ligada" : "desligada");
            // Ligada no meio de uma leva longa: o resto da fila some na hora.
            if (on) drop_flood(pending_popups(), true, "ligada no meio da leva");
        });
        list->addItem(flood);
        list->addItem(new tsl::elm::CategoryHeader("10+ de uma vez: sem pop-ups (ficam salvas)"));

        // Os pop-ups deixam este overlay carregado; daqui se volta ao menu de overlays.
        auto back = new tsl::elm::ListItem("Abrir o menu de overlays");
        back->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            OLOG("voltando ao menu de overlays (os pop-ups param ate o Ruby Achievements ser aberto de novo)");
            tsl::setNextOverlay("sdmc:/switch/.overlays/ovlmenu.ovl", "");
            tsl::Overlay::get()->close();
            return true;
        });
        list->addItem(back);

        m_diag = new tsl::elm::CategoryHeader("");
        list->addItem(m_diag);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        if (!second_elapsed(&m_tick)) return;
        NowPlaying np = read_now_playing();
        m_now->set(np);
        m_diag->setText(diag_text(np));
    }

private:
    // Ex.: "status: há 12 s | 2 perfis".
    static std::string diag_text(const NowPlaying& np) {
        std::string users = std::to_string(np.users) + (np.users == 1 ? " perfil" : " perfis");
        if (!np.status_ok) return "status.txt ilegível | " + users;
        std::string age = np.age < 0 ? "relógio indisponível" : "há " + std::to_string(np.age) + " s";
        return "status: " + age + " | " + users;
    }

    NowPlayingItem* m_now = nullptr;
    tsl::elm::CategoryHeader* m_diag = nullptr;
    u64 m_tick = 0;
};

class RubyAchievementsOverlay : public tsl::Overlay {
public:
    void initServices() override {
        Result sd = fsdevMountSdmc();
        Result tm = timeInitialize();
        pt_config_load(&g_cfg);
        OLOG("overlay iniciado (build " __DATE__ " " __TIME__ "): sd rc=0x%X, time rc=0x%X, relogio=%lld, perfil %lld",
             sd, tm, (long long)pt_now(), (long long)g_cfg.profile);
        sound::init();
    }

    void exitServices() override {
        OLOG("overlay encerrado");
        sound::exit();
        timeExit();
        fsdevUnmountAll();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MenuGui>(); }
};

// --- Laço persistente ---------------------------------------------------------
// O tsl::loop padrão esconde o overlay no Home, no Power, no combo e ao tocar
// fora da camada. Para os pop-ups aparecerem dentro dos jogos este overlay usa
// um laço próprio com três modos:
//   Hidden: nada na tela; o combo abre o menu.
//   Menu:   o menu; combo/Home/Power/B/toque fora fecham o menu.
//   Popups: tela transparente sem capturar botões; só o pop-up de conquista.
//           O combo abre o menu. Home e Power não o tiram.
// Ao fechar o menu, vai para Popups.

// Sinais do thread de entrada para o laço principal.
std::atomic<bool> g_sig_combo{false}, g_sig_home{false}, g_sig_power{false};

void input_poller(void* arg) {
    auto* sh = static_cast<tsl::impl::SharedThreadData*>(arg);

    Event home = {}, power = {};
    hidsysAcquireHomeButtonEventHandle(&home, false);
    hidsysAcquireSleepButtonEventHandle(&power, false);
    eventClear(&home);
    eventClear(&power);
    tsl::impl::parseOverlaySettings();  // combo do Tesla/Ultrahand

    padConfigureInput(8, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt);
    PadState pad;
    padInitializeAny(&pad);
    hidInitializeTouchScreen();
    padUpdate(&pad);

    Waiter objects[2] = {waiterForEvent(&home), waiterForEvent(&power)};
    while (sh->running) {
        padUpdate(&pad);
        {
            std::scoped_lock lock(sh->dataMutex);
            sh->keysDown = padGetButtonsDown(&pad);
            sh->keysHeld = padGetButtons(&pad);
            sh->joyStickPosLeft = padGetStickPos(&pad, 0);
            sh->joyStickPosRight = padGetStickPos(&pad, 1);
            if (hidGetTouchScreenStates(&sh->touchState, 1) == 0) sh->touchState = {0};
            u64 combo = tsl::cfg::launchCombo;
            if ((sh->keysHeld & combo) == combo && (sh->keysDown & combo)) {
                g_sig_combo = true;
                eventFire(&sh->comboEvent);  // acorda o modo Hidden
            }
            sh->keysDownPending |= sh->keysDown;
        }
        s32 idx = 0;
        Result rc = waitObjects(&idx, objects, 2, 20'000'000ULL);
        if (R_SUCCEEDED(rc)) {
            if (idx == 0) {
                eventClear(&home);
                g_sig_home = true;
            } else {
                eventClear(&power);
                g_sig_power = true;
            }
        }
    }
    eventClose(&home);
    eventClose(&power);
}

const char* mode_name(Mode m) { return m == Mode::Popups ? "pop-ups" : m == Mode::Menu ? "menu" : "escondido"; }

}  // namespace

// Especialização do laço do libtesla para este overlay (tsl::loop é friend de
// tsl::Overlay, então o laço acessa a pilha de telas e a renderização).
namespace tsl {
template <>
inline int loop<RubyAchievementsOverlay, impl::LaunchFlags::CloseOnExit>(int argc, char** argv) {
    impl::SharedThreadData sh;
    sh.running = true;
    eventCreate(&sh.comboEvent, false);
    Thread thread;
    threadCreate(&thread, input_poller, &sh, nullptr, 0x1000, 0x2c, -2);
    threadStart(&thread);

    auto*& ov = Overlay::s_overlayInstance;
    ov = new RubyAchievementsOverlay();
    ov->m_closeOnExit = false;  // B na raiz do menu fecha só o menu
    hlp::doWithSmSession([] { Overlay::get()->initServices(); });
    ov->initScreen();
    ov->changeTo(ov->loadInitialGui());

    bool skip_combo = false;  // aberto pelo Ultrahand/Tesla: mostrar já
    for (int i = 0; i < argc; i++)
        if (strcasecmp(argv[i], "--skipCombo") == 0) skip_combo = true;

    Mode mode = Mode::Hidden;
    auto enter = [&](Mode next, const char* why) {
        OLOG("modo %s -> %s (%s)", mode_name(mode), mode_name(next), why);
        // Pop-ups no canto superior direito da tela; o menu volta para a esquerda.
        gfx::Renderer::setLayerPosition(next == Mode::Popups ? cfg::ScreenWidth - cfg::LayerWidth : 0, 0);
        if (next == Mode::Popups) {
            if (!g_popups_active) changeTo<PopupGui>();
            hlp::requestForeground(false);  // o jogo/Home recebe os botões
            ov->disableNextAnimation();
            ov->show();
        } else if (next == Mode::Menu) {
            if (g_popups_active) ov->goBack();  // tira a tela dos pop-ups de cima do menu
            hlp::requestForeground(true);
            ov->show();
        } else {
            if (g_popups_active) ov->goBack();
            hlp::requestForeground(false);
            ov->clearScreen();
        }
        ov->resetFlags();
        g_sig_combo = g_sig_home = g_sig_power = false;
        eventClear(&sh.comboEvent);
        mode = next;
    };
    auto leave_menu = [&](const char* why) { enter(Mode::Popups, why); };

    enter(skip_combo ? Mode::Menu : Mode::Popups, skip_combo ? "aberto pelo menu de overlays" : "inicio");

    while (sh.running) {
        if (mode == Mode::Hidden) {
            eventWait(&sh.comboEvent, UINT64_MAX);
            if (!sh.running) break;
            enter(Mode::Menu, "combo");
            continue;
        }

        ov->loop();  // update + draw da tela atual
        {
            std::scoped_lock lock(sh.dataMutex);
            if (mode == Mode::Menu && !ov->fadeAnimationPlaying())
                ov->handleInput(sh.keysDownPending, sh.keysHeld, sh.touchState.count, sh.touchState.touches[0],
                                sh.joyStickPosLeft, sh.joyStickPosRight);
            sh.keysDownPending = 0;  // fora do menu os botões são só do jogo
        }

        if (ov->shouldClose()) {
            OLOG("overlay fechado pelo menu");
            sh.running = false;
            break;
        }

        if (mode == Mode::Menu) {
            if (g_sig_combo.exchange(false)) leave_menu("combo no menu");
            else if (g_sig_home.exchange(false)) leave_menu("botao Home");
            else if (g_sig_power.exchange(false)) leave_menu("botao Power/sleep");
            else if (ov->shouldHide()) leave_menu("menu fechado (B ou toque fora)");
        } else {  // pop-ups
            if (g_sig_combo.exchange(false)) enter(Mode::Menu, "combo");
            g_sig_home = g_sig_power = false;
            if (ov->shouldHide()) ov->resetFlags();
            // Animação do pop-up a ~60 quadros/s; parado, ~20 bastam para ver a fila.
            svcSleepThread(g_popup_busy ? 16'000'000ULL : 50'000'000ULL);
        }
    }

    ov->clearScreen();
    hlp::requestForeground(false);
    eventFire(&sh.comboEvent);
    threadWaitForExit(&thread);
    threadClose(&thread);
    eventClose(&sh.comboEvent);
    ov->exitScreen();
    ov->exitServices();
    delete ov;
    ov = nullptr;
    return 0;
}
}  // namespace tsl

int main(int argc, char** argv) {
    return tsl::loop<RubyAchievementsOverlay>(argc, argv);
}
