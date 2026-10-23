#include <atomic>
#include <ctime>
#include <list>
#include <unordered_map>

#include "ui.hpp"
#include <borealis/core/cache_helper.hpp>

#include "steamgriddb.hpp"

namespace {
constexpr int kColumns = 5;
constexpr float kThumb = 190;
constexpr size_t kMaxGrids = 40;

// Nome da Home sem ™/®/© (o SteamGridDB não os usa nos nomes).
std::string search_term(const std::string& name) {
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        if (!name.compare(i, 3, "\xE2\x84\xA2")) { i += 2; continue; }  // ™
        if (!name.compare(i, 2, "\xC2\xAE") || !name.compare(i, 2, "\xC2\xA9")) { i += 1; continue; }  // ® ©
        out += name[i];
    }
    size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
    return a == std::string::npos ? "" : out.substr(a, b - a + 1);
}

std::string year_of(int64_t epoch) {
    if (epoch <= 0) return "";
    time_t t = (time_t)epoch;
    struct tm* tm = gmtime(&t);
    return tm ? " (" + std::to_string(tm->tm_year + 1900) + ")" : "";
}
}  // namespace

void ui::forget_texture(const std::string& path) {
    auto& cache = brls::TextureCache::instance();
    int tex = cache.getCache(path);  // conta uma referência...
    if (tex > 0) {
        cache.removeCache(tex);  // ...devolvida aqui
        cache.markDirty(tex);    // próximas Images com este caminho releem o arquivo
    }
}

// Estado compartilhado com as tarefas de rede: a tela pode fechar no meio de uma
// busca, então os retornos conferem `alive` e a geração da busca.
struct CoverSearchActivity::Shared {
    std::atomic<bool> alive{true};
    std::atomic<int> generation{0};
    bool busy = false;  // só no thread da UI
    std::vector<SgdbGame> games;
    std::vector<SgdbGrid> grids;
};

CoverSearchActivity::CoverSearchActivity(GameRow game, std::string title, std::string file_base,
                                         std::function<void(const std::string&)> on_saved)
    : m_game(std::move(game)),
      m_title(std::move(title)),
      m_file_base(std::move(file_base)),
      m_on_saved(std::move(on_saved)),
      m_shared(std::make_shared<Shared>()) {
    m_term = search_term(m_game.name);
}

CoverSearchActivity::~CoverSearchActivity() { m_shared->alive = false; }

brls::View* CoverSearchActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(20, 40, 0, 40);

    m_search = new brls::DetailCell();
    m_search->setText(tr("Buscar no SteamGridDB"));
    m_search->setDetailText(m_term);
    m_search->registerClickAction([this](brls::View*) {
        ask_term();
        return true;
    });
    root->addView(m_search);

    m_match = new brls::DetailCell();
    m_match->setText(tr("Jogo encontrado"));
    m_match->setDetailText("-");
    m_match->registerClickAction([this](brls::View*) {
        choose_game();
        return true;
    });
    root->addView(m_match);

    auto key = new brls::DetailCell();
    key->setText(tr("Chave da API"));
    key->setDetailText(sgdb::api_key().empty() ? tr("não configurada") : tr("configurada"));
    key->registerClickAction([this](brls::View*) {
        ask_key(false);
        return true;
    });
    root->addView(key);

    m_status = new brls::Label();
    m_status->setFontSize(17);
    m_status->setMarginTop(10);
    m_status->setMarginBottom(10);
    m_status->setText(" ");
    root->addView(m_status);

    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_grid = new brls::Box(brls::Axis::COLUMN);
    m_grid->setPaddingBottom(30);
    tico::set_scroll_content(scroll, m_grid);
    root->addView(scroll);

    auto frame = new tico::Frame(root);
    frame->setTitle(m_title + " · " + m_game.name);
    return frame;
}

void CoverSearchActivity::onContentAvailable() {
    if (sgdb::api_key().empty()) {
        set_status(tr("Informe a chave da API (steamgriddb.com > Preferences > API) ou grave-a em "
                      "secrets.ini como steamgriddb_key=..."),
                   true);
        ask_key(true);
        return;
    }
    search(m_term);  // sugestão automática: o nome atual do jogo
}

void CoverSearchActivity::set_status(const std::string& text, bool error) {
    m_status->setTextColor(error ? nvgRGB(230, 90, 70) : tico::kMuted);
    m_status->setText(text.empty() ? " " : text);
}

void CoverSearchActivity::ask_key(bool then_search) {
    brls::Application::getImeManager()->openForText(
        [this, then_search](std::string key) {
            brls::sync([this, then_search, key] {
                if (!sgdb::save_api_key(key)) {
                    set_status(tr("Chave inválida: use só letras e números (até 63)."), true);
                    return;
                }
                set_status(tr("Chave salva."));
                if (then_search) search(m_term);
            });
        },
        tr("Chave da API do SteamGridDB"), tr("Gerada em steamgriddb.com > Preferences > API"), 63, "");
}

void CoverSearchActivity::ask_term() {
    brls::Application::getImeManager()->openForText(
        [this](std::string term) {
            brls::sync([this, term] {
                std::string t = search_term(term);
                if (t.empty()) return;
                m_term = t;
                m_search->setDetailText(t);
                search(t);
            });
        },
        tr("Buscar capa"), tr("Nome do jogo no SteamGridDB"), 100, m_term);
}

void CoverSearchActivity::search(const std::string& term) {
    std::string key = sgdb::api_key();
    if (key.empty()) {
        ask_key(true);
        return;
    }
    int gen = ++m_shared->generation;
    ui::rescue_focus(m_grid, m_search);
    m_grid->clearViews();
    m_thumbs.clear();
    m_match->setDetailText("-");
    set_status(trf("Buscando \"%s\"...", term.c_str()));
    auto sh = m_shared;
    brls::async([this, sh, gen, key, term] {
        SteamGridDBClient api(key);
        std::vector<SgdbGame> games;
        bool ok = api.search(term, games);
        std::string err = api.error();
        brls::sync([this, sh, gen, ok, games, err, term] {
            if (!sh->alive || gen != sh->generation) return;
            if (!ok) {
                set_status(err, true);
                return;
            }
            sh->games = games;
            if (games.empty()) {
                set_status(trf("Nada encontrado para \"%s\". Tente outro nome em \"Buscar\".", term.c_str()), true);
                return;
            }
            load_grids(0);
        });
    });
}

void CoverSearchActivity::choose_game() {
    if (m_shared->games.size() < 2) return;
    std::vector<std::string> names;
    for (auto& g : m_shared->games) names.push_back(g.name + year_of(g.release_date) + (g.verified ? std::string(" · ") + tr("verificado") : std::string()));
    ui::pick(tr("Qual destes?"), names, [this](int i) { load_grids((size_t)i); });
}

void CoverSearchActivity::load_grids(size_t index) {
    if (index >= m_shared->games.size()) return;
    const SgdbGame g = m_shared->games[index];
    int gen = ++m_shared->generation;
    std::string detail = g.name + year_of(g.release_date);
    if (m_shared->games.size() > 1) detail += "  ·  " + trf("%d resultados", (int)m_shared->games.size());
    m_match->setDetailText(detail);
    ui::rescue_focus(m_grid, m_search);
    m_grid->clearViews();
    m_thumbs.clear();
    set_status(trf("Carregando capas quadradas de %s...", g.name.c_str()));

    auto sh = m_shared;
    std::string key = sgdb::api_key();
    brls::async([this, sh, gen, key, g] {
        SteamGridDBClient api(key);
        std::vector<SgdbGrid> grids;
        bool ok = api.square_grids(g.id, grids);
        std::string err = api.error();
        if (grids.size() > kMaxGrids) grids.resize(kMaxGrids);
        brls::sync([this, sh, gen, ok, grids, err, g] {
            if (!sh->alive || gen != sh->generation) return;
            if (!ok) {
                set_status(err, true);
                return;
            }
            sh->grids = grids;
            if (grids.empty()) {
                set_status(trf("%s não tem capas quadradas (512x512 ou 1024x1024) no SteamGridDB.", g.name.c_str()) +
                               (sh->games.size() > 1 ? std::string(" ") + tr("Escolha outro em \"Jogo encontrado\".") : ""),
                           true);
                return;
            }
            show_grids();
        });

        // Miniaturas uma a uma, enquanto a tela e a busca forem as mesmas.
        if (!ok) return;
        for (size_t i = 0; i < grids.size() && sh->alive && gen == sh->generation; i++) {
            std::vector<unsigned char> img;
            if (!api.fetch(grids[i].thumb, img, 4u << 20) || img.empty()) continue;
            brls::sync([this, sh, gen, i, img] {
                if (!sh->alive || gen != sh->generation || i >= m_thumbs.size()) return;
                m_thumbs[i]->setImageFromMem(img.data(), (int)img.size());
            });
        }
    });
}

void CoverSearchActivity::show_grids() {
    const auto& grids = m_shared->grids;
    set_status(trf("%d capas quadradas. Escolha uma com A.", (int)grids.size()));
    brls::Box* row = nullptr;
    for (size_t i = 0; i < grids.size(); i++) {
        if (i % kColumns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(14);
            m_grid->addView(row);
        }
        auto card = new brls::Box(brls::Axis::COLUMN);
        card->setPadding(8);
        card->setMarginRight(12);
        card->setCornerRadius(6);
        card->setFocusable(true);
        card->setAlignItems(brls::AlignItems::CENTER);

        auto img = new brls::Image();
        img->setSize(brls::Size(kThumb, kThumb));
        img->setScalingType(brls::ImageScalingType::FIT);
        card->addView(img);
        m_thumbs.push_back(img);

        auto label = new brls::Label();
        label->setFontSize(14);
        label->setTextColor(tico::kMuted);
        label->setMarginTop(4);
        label->setText(std::to_string(grids[i].width) + "x" + std::to_string(grids[i].height) +
                       (grids[i].mime == "image/png" ? " PNG" : " JPEG"));
        card->addView(label);

        card->registerClickAction([this, i](brls::View*) {
            apply(i);
            return true;
        });
        card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
        row->addView(card);
    }
}

void CoverSearchActivity::apply(size_t index) {
    if (m_shared->busy || index >= m_shared->grids.size()) return;
    SgdbGrid grid = m_shared->grids[index];
    ui::confirm(trf("Usar esta imagem (%dx%d) em %s?", grid.width, grid.height, m_game.name.c_str()),
                tr("Baixar"), [this, grid] {
                    m_shared->busy = true;
                    set_status(tr("Baixando a imagem..."));
                    auto sh = m_shared;
                    std::string key = sgdb::api_key();
                    std::string base = m_file_base;
                    brls::async([this, sh, key, grid, base] {
                        SteamGridDBClient api(key);
                        std::vector<unsigned char> img;
                        std::string path, err;
                        bool ok = api.fetch(grid.url, img) && sgdb::save_cover(base, img, path, err);
                        if (!ok && err.empty()) err = api.error().empty() ? std::string(tr("Download vazio.")) : api.error();
                        brls::sync([this, sh, ok, path, err] {
                            if (!sh->alive) return;
                            sh->busy = false;
                            if (!ok) {
                                set_status(err, true);
                                return;
                            }
                            ui::forget_texture(path);
                            // Fecha antes: quem recebe o arquivo pode abrir um modal.
                            auto saved = m_on_saved;
                            brls::Application::popActivity(brls::TransitionAnimation::FADE, [saved, path] {
                                if (saved) brls::sync([saved, path] { saved(path); });
                            });
                        });
                    });
                });
}
