// Tela dos pacotes de conquistas de um jogo do Switch: os do cartão SD (ativar, apagar) e os
// do catálogo da comunidade (community.hpp), com o selo de compatibilidade (baixar, atualizar).
#include "community.hpp"
#include "pt_pack.h"
#include "pt_paths.h"
#include "sha256.hpp"
#include "ui.hpp"

#include <switch.h>

#include <cstring>
#include <memory>

namespace {

// Versão do jogo instalado no NACP ("1.6.0"); "" se não leu.
std::string installed_version(const std::string& title_id) {
    std::string v;
    if (R_FAILED(nsInitialize())) return v;
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, strtoull(title_id.c_str(), nullptr, 16),
                                                data.get(), sizeof(*data), &size)) &&
        size >= sizeof(NacpStruct))
        v.assign(data->nacp.display_version, strnlen(data->nacp.display_version, sizeof(data->nacp.display_version)));
    nsExit();
    return v;
}

NVGcolor compat_color(community::Compat c) {
    switch (c) {
        case community::Compat::Ok: return nvgRGB(0, 190, 120);
        case community::Compat::Warning: return nvgRGB(230, 170, 40);
        default: return nvgRGB(230, 90, 70);
    }
}

}  // namespace

// Estado da tela: o catálogo baixado uma vez e o que há no SD (relido a cada mudança).
struct CommunityState {
    community::Index index;
    bool from_cache = false, index_ok = false;
    std::string index_error;
    community::Game game;
    std::vector<AchievementPack> local;
    std::vector<community::Match> matches;
    std::vector<int> installed;  // por match: 0 = não, 1 = atualizado, 2 = difere do catálogo
};

CommunityActivity::CommunityActivity(int64_t game_id, std::string name)
    : m_alive(std::make_shared<bool>(true)), m_game_id(game_id), m_name(std::move(name)),
      m_state(std::make_shared<CommunityState>()) {}

CommunityActivity::~CommunityActivity() { *m_alive = false; }

brls::View* CommunityActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(20, 40, 0, 40);

    m_summary = tico::cell(tr("Jogo instalado"), "");
    m_summary->setMarginBottom(8);
    root->addView(m_summary);

    auto spinner = new brls::ProgressSpinner();
    spinner->setMarginTop(12);
    m_spinner = spinner;
    root->addView(spinner);

    m_status = new brls::Label();
    m_status->setFontSize(16);
    m_status->setTextColor(tico::kMuted);
    m_status->setText(tr("Consultando o catálogo da comunidade..."));
    m_status->setMarginBottom(8);
    root->addView(m_status);

    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_list = new brls::Box(brls::Axis::COLUMN);
    m_list->setPaddingBottom(30);
    tico::set_scroll_content(scroll, m_list);
    root->addView(scroll);

    auto frame = new tico::Frame(root);
    frame->setTitle(std::string(tr("Pacotes de conquistas")) + " · " + m_name);
    return frame;
}

void CommunityActivity::onContentAvailable() {
    registerAction(tr("Voltar"), brls::BUTTON_B, [this](brls::View*) {
        if (!m_running) brls::Application::popActivity();
        return true;
    });
    load(true);
}

void CommunityActivity::load(bool fetch_index) {
    m_running = true;
    m_spinner->setVisibility(brls::Visibility::VISIBLE);
    auto alive = m_alive;
    auto state = m_state;
    int64_t game_id = m_game_id;
    brls::async([this, alive, state, game_id, fetch_index] {
        community::Game& g = state->game;
        g.type = community::TargetType::Native;
        g.title_id = data::achievement_pack_key(game_id);
        if (fetch_index) {
            g.build_id = data::title_build_id(g.title_id);
            g.version = installed_version(g.title_id);
            state->index_ok = community::load_index(state->index, state->from_cache, state->index_error);
        }
        data::achievement_packs(game_id, state->local);
        state->matches = g.title_id.empty() ? std::vector<community::Match>() : community::filter(state->index, g);
        state->installed.assign(state->matches.size(), 0);
        for (size_t i = 0; i < state->matches.size(); i++) {
            const community::Entry& e = state->matches[i].entry;
            std::string file = community::file_name(g.title_id, e.pack_id);
            for (auto& p : state->local)
                if (p.file == file)
                    state->installed[i] = sha256_file(std::string(PT_ACH_DIR) + "/" + file) == e.sha256 ? 1 : 2;
        }
        brls::sync([this, alive] {
            if (!*alive) return;
            m_running = false;
            m_spinner->setVisibility(brls::Visibility::GONE);
            show();
        });
    });
}

void CommunityActivity::show() {
    const CommunityState& s = *m_state;
    const community::Game& g = s.game;
    ui::rescue_focus(m_list, m_summary);
    m_list->clearViews();

    std::string build = g.build_id.empty() ? std::string(tr("build ainda não lido (abra o jogo uma vez)"))
                                           : trf("build %s", g.build_id.substr(0, 16).c_str());
    m_summary->setDetailText((g.version.empty() ? std::string(tr("versão desconhecida")) : trf("versão %s", g.version.c_str())) +
                             " · " + build);

    if (g.title_id.empty()) {
        m_status->setText(tr("Só jogos do Switch (com Title ID) têm pacotes no cartão SD."));
        return;
    }
    if (!s.index_ok) {
        m_status->setTextColor(nvgRGB(230, 90, 70));
        m_status->setText(trf("Catálogo indisponível: %s", s.index_error.c_str()));
    } else {
        std::string text = trf("Catálogo de %s", s.index.updated_at.empty() ? "?" : s.index.updated_at.substr(0, 10).c_str());
        if (s.from_cache) text += " · " + trf("sem rede, cópia salva (%s)", s.index_error.c_str());
        m_status->setTextColor(s.from_cache ? nvgRGB(230, 170, 40) : tico::kMuted);
        m_status->setText(text);
    }

    // No cartão SD: ativar e apagar.
    m_list->addView(tico::section(tr("No cartão SD")));
    if (s.local.empty()) {
        auto none = new brls::Label();
        none->setFontSize(16);
        none->setTextColor(tico::kMuted);
        none->setText(tr("Nenhum pacote para este jogo."));
        m_list->addView(none);
    }
    for (const AchievementPack& p : s.local) {
        auto cell = tico::cell(p.count > 0 ? trf("%s (%d conquistas)", p.label.c_str(), p.count) : p.label,
                               p.active ? std::string(tr("ativo")) : p.file);
        if (p.active) cell->setDetailTextColor(nvgRGB(0, 190, 120));
        AchievementPack copy = p;
        cell->registerClickAction([this, copy](brls::View*) {
            local_actions(copy);
            return true;
        });
        m_list->addView(cell);
    }

    // Catálogo da comunidade.
    m_list->addView(tico::section(tr("Comunidade")));
    if (s.index_ok && s.matches.empty()) {
        auto none = new brls::Label();
        none->setFontSize(16);
        none->setTextColor(tico::kMuted);
        none->setText(tr("Nenhum pacote da comunidade para este jogo."));
        m_list->addView(none);
    }
    for (size_t i = 0; i < s.matches.size(); i++) {
        const community::Match& m = s.matches[i];
        const community::Entry& e = m.entry;
        std::string badge = m.compat == community::Compat::Ok        ? tr("Compatível")
                            : m.compat == community::Compat::Warning ? tr("Aviso: build diferente ou não conferido")
                                                                     : trf("Incompatível: exige a versão %s",
                                                                           e.target.version_min.c_str());
        std::string detail = trf("%s · %d conquistas · %s", e.author.c_str(), e.total, badge.c_str());
        if (s.installed[i] == 1) detail += " · " + std::string(tr("instalado"));
        if (s.installed[i] == 2) detail += " · " + std::string(tr("atualização disponível"));
        auto cell = tico::cell(e.version.empty() ? e.title : e.title + " · v" + e.version, detail);
        cell->setDetailTextColor(compat_color(m.compat));
        cell->registerClickAction([this, i](brls::View*) {
            entry_actions(i);
            return true;
        });
        m_list->addView(cell);
    }
}

void CommunityActivity::local_actions(const AchievementPack& p) {
    if (m_running) return;  // download em andamento
    std::vector<std::string> labels;
    std::vector<int> actions;  // 0 = ativar, 1 = apagar
    if (!p.active) {
        labels.push_back(tr("Ativar"));
        actions.push_back(0);
    }
    labels.push_back(tr("Apagar do cartão SD"));
    actions.push_back(1);
    int64_t game_id = m_game_id;
    ui::pick(p.label, labels, [this, p, actions, game_id](int i) {
        if (actions[i] == 0) {
            ui::activate_pack(game_id, p, [this] { changed(); });
            return;
        }
        ui::confirm(trf("Apagar \"%s\" do cartão SD?\n\nO progresso e o histórico deste jogo continuam salvos.",
                        p.label.c_str()),
                    tr("Apagar"), [this, p, game_id] {
                        if (ui::report(data::delete_achievement_pack(game_id, p.file), tr("Pacote apagado"))) changed();
                    });
    });
}

void CommunityActivity::entry_actions(size_t index) {
    if (m_running || index >= m_state->matches.size()) return;
    const community::Match m = m_state->matches[index];
    int installed = m_state->installed[index];
    std::string file = community::file_name(m_state->game.title_id, m.entry.pack_id);
    const AchievementPack* local = nullptr;
    for (auto& p : m_state->local)
        if (p.file == file) local = &p;

    std::vector<std::string> labels;
    std::vector<int> actions;  // 0 = baixar/atualizar, 1 = local
    if (installed != 1) {
        labels.push_back(installed == 2 ? tr("Atualizar") : tr("Baixar"));
        actions.push_back(0);
    }
    if (local) {
        labels.push_back(tr("Ativar ou apagar"));
        actions.push_back(1);
    }
    if (labels.empty()) return;
    std::string title = m.entry.title + (m.entry.description.empty() ? "" : "\n" + m.entry.description);
    AchievementPack local_copy = local ? *local : AchievementPack();
    ui::pick(title, labels, [this, m, actions, local_copy](int i) {
        if (actions[i] == 1) {
            local_actions(local_copy);
            return;
        }
        if (m.compat == community::Compat::Ok) {
            download(m.entry);
            return;
        }
        std::string why = m.compat == community::Compat::Incompatible
                              ? trf("Este pacote exige a versão %s do jogo.", m.entry.target.version_min.c_str())
                              : std::string(tr("O build deste jogo não está na lista do pacote (ou ainda não foi lido: "
                                               "abra o jogo uma vez). Condições de memória ficam desligadas em outro build."));
        ui::confirm(why + "\n\n" + tr("Baixar mesmo assim?"), tr("Baixar"), [this, m] { download(m.entry); });
    });
}

void CommunityActivity::download(const community::Entry& e) {
    m_running = true;
    m_spinner->setVisibility(brls::Visibility::VISIBLE);
    m_status->setTextColor(tico::kMuted);
    m_status->setText(trf("Baixando \"%s\"...", e.title.c_str()));
    brls::Application::giveFocus(m_summary);
    auto alive = m_alive;
    std::string key = m_state->game.title_id;
    brls::async([this, alive, e, key] {
        std::string error;
        bool ok = community::install(e, key, error);
        if (ok) data::sync_achievement_packs();  // jogo e galeria já com o pacote, se for o ativo
        bool active = ok && pt_pack_active(key) == community::file_name(key, e.pack_id);
        brls::sync([this, alive, ok, active, error, e] {
            if (!*alive) return;
            m_running = false;
            if (!ok) {
                m_spinner->setVisibility(brls::Visibility::GONE);
                m_status->setTextColor(nvgRGB(230, 90, 70));
                m_status->setText(error);
                return;
            }
            brls::Application::notify(active ? trf("\"%s\" baixado e ativo.", e.title.c_str())
                                             : trf("\"%s\" baixado. Para usá-lo, ative-o.", e.title.c_str()));
            changed();
        });
    });
}

void CommunityActivity::changed() {
    AchievementsTab::request_refresh();
    HomeActivity::request_refresh();
    load(false);
}
