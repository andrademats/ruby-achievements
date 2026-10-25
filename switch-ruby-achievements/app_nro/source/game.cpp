#include "ui.hpp"

namespace {

brls::DetailCell* action_cell(const std::string& text, const std::string& detail, std::function<void()> fn) {
    auto cell = new brls::DetailCell();
    cell->setText(text);
    cell->setDetailText(detail);
    cell->registerClickAction([fn](brls::View*) {
        fn();
        return true;
    });
    return cell;
}

}  // namespace

// --- Detalhes do jogo ----------------------------------------------------------
GameActivity::GameActivity(int64_t game_id) : m_id(game_id) {}

// Capa com cantos arredondados e a sombra suave do Tico.
class CoverCard : public brls::Box {
public:
    CoverCard() : brls::Box(brls::Axis::COLUMN) { setSize(brls::Size(256, 256)); }
    void draw(NVGcontext* vg, float x, float y, float w, float h, brls::Style style, brls::FrameContext* ctx) override {
        tico::soft_shadow(vg, x, y, w, h, 18, getAlpha() * 1.4f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 18);
        nvgFillColor(vg, a(tico::kSlot));
        nvgFill(vg);
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }
};

brls::View* GameActivity::createContentView() {
    auto scroll = tico::scroller();
    auto root = new brls::Box(brls::Axis::ROW);
    root->setPadding(6, 30, 30, 30);

    auto card = new CoverCard();
    card->setMarginRight(36);
    m_cover = new brls::Image();
    m_cover->setSize(brls::Size(256, 256));
    m_cover->setScalingType(brls::ImageScalingType::FILL);
    m_cover->setCornerRadius(18);
    card->addView(m_cover);
    root->addView(card);

    auto col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1);

    // Nome no título da tela e os dados (origem, Title ID, última sessão) logo
    // abaixo dele (Frame::setSubtitle); aqui só o total.
    m_total = new brls::Label();
    m_total->setFontSize(22);
    m_total->setTextColor(tico::kText);
    col->addView(m_total);

    m_per_user = new brls::Box(brls::Axis::COLUMN);
    m_per_user->setMarginTop(8);
    m_per_user->setMarginBottom(20);
    col->addView(m_per_user);

    // Jogo do Switch: pacotes de conquistas (os do SD e os da comunidade).
    GameRow row;
    data::game(m_id, ui::profile_filter(), row);
    if (auto community = ui::community_cell(m_id, row.name)) {
        col->addView(tico::section(tr("Conquistas")));
        auto ach = new tico::Panel();
        ach->addView(community);
        col->addView(ach);
    }

    // Sessões e conquistas têm caminhos próprios (menu + e A na tela inicial).
    auto changed = [this] { refresh(); };
    col->addView(tico::section(tr("Preferências do jogo")));
    auto panel = new tico::Panel();
    panel->addView(action_cell(tr("Editar nome"), tr("Não altera a Home do Switch"), [this, changed] {
        ui::edit_game_name(m_game, changed);
    }));
    panel->addView(action_cell(tr("Alterar capa"), tr("Arquivo local ou SteamGridDB"), [this, changed] {
        ui::change_game_cover(m_game, changed);
    }));
    panel->addView(action_cell(tr("Unificar com outro jogo"), tr("Move as sessões deste para o outro"), [this] {
        ui::merge_game(m_game, [] { brls::Application::popActivity(); });
    }));
    panel->addView(action_cell(tr("Reassociar perfil"), tr("Todas as sessões deste jogo"), [this, changed] {
        ui::reassign_game(m_game, changed);
    }));
    panel->addView(action_cell(tr("Desativar tracking de tempo"), tr("Sai da tela inicial"), [this] {
        ui::disable_tracking(m_game, [] { brls::Application::popActivity(); });
    }));
    col->addView(panel);

    root->addView(col);
    tico::set_scroll_content(scroll, root);

    m_frame = new tico::Frame(scroll);
    refresh();
    return m_frame;
}

void GameActivity::onResume() {
    brls::sync([this] { refresh(); });  // voltou da lista de sessões
}

void GameActivity::refresh() {
    if (!data::game(m_id, ui::profile_filter(), m_game)) {
        ui::report(false, "");
        return;
    }
    m_frame->setTitle(m_game.name);

    std::string info = data::source_label(m_game.source);
    if (!m_game.title_id.empty()) info += " · " + m_game.title_id;
    if (!m_game.rom_path.empty()) info += " · " + m_game.rom_path;
    info += " · " + trf("última sessão: %s", data::format_date(m_game.last_played).c_str());
    m_frame->setSubtitle(info);

    std::string profile = ui::profile_name();
    m_total->setText(profile.empty() ? trf("Total: %s", data::format_duration(m_game.total).c_str())
                                     : trf("Total de %s: %s", profile.c_str(), data::format_duration(m_game.total).c_str()));
    if (m_game.cover.empty()) m_cover->clear();
    else m_cover->setImageFromFile(m_game.cover);

    // Só labels (não focáveis): pode reconstruir com segurança. Divisão por
    // perfil só em "Todos os perfis".
    m_per_user->clearViews();
    std::vector<std::pair<std::string, int64_t>> totals;
    if (profile.empty()) data::game_user_totals(m_id, totals);
    for (auto& t : totals) {
        auto l = new brls::Label();
        l->setFontSize(17);
        l->setText(t.first + ": " + data::format_duration(t.second));
        m_per_user->addView(l);
    }
}

// --- Sessões --------------------------------------------------------------------------
SessionsActivity::SessionsActivity(int64_t game_id, std::string title)
    : m_game_id(game_id), m_title(std::move(title)) {}

brls::View* SessionsActivity::createContentView() {
    auto box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(20, 40, 0, 40);
    box->addView(new SessionList(m_game_id, [] {}));
    auto frame = new tico::Frame(box);
    std::string profile = ui::profile_name();
    frame->setTitle(std::string(tr("Sessões")) + " · " + m_title + (profile.empty() ? "" : " · " + profile));
    return frame;
}

SessionList::SessionList(int64_t game_id, std::function<void()> on_change)
    : brls::Box(brls::Axis::COLUMN), m_game_id(game_id), m_on_change(std::move(on_change)) {
    setGrow(1);
    m_summary = tico::cell();
    m_summary->setText(game_id ? tr("Sessões") : tr("Sessões fantasmas"));
    addView(m_summary);

    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_list = new brls::Box(brls::Axis::COLUMN);
    m_list->setPaddingBottom(30);
    tico::set_scroll_content(scroll, m_list);
    addView(scroll);
    rebuild();
}

void SessionList::rebuild() {
    ui::rescue_focus(m_list, m_summary);
    m_list->clearViews();
    m_has_last = false;

    data::SessionSummary sum;
    if (!data::session_summary(m_game_id, ui::profile_filter(), sum)) {
        m_summary->setDetailText(data::last_error());
        return;
    }
    std::string profile = ui::profile_name();
    m_summary->setDetailText((profile.empty() ? "" : profile + " · ") + std::to_string(sum.count) + " · " +
                             data::format_duration(sum.total));

    m_more = tico::cell();
    m_more->setText(tr("Carregar mais sessões"));
    m_more->registerClickAction([this](brls::View*) {
        // Sem linhas novas (a anterior era a última), m_more some: o foco volta ao topo.
        brls::View* first = load_page();
        brls::Application::giveFocus(first ? first : m_summary);
        return true;
    });
    m_list->addView(m_more);
    load_page();
}

brls::View* SessionList::load_page() {
    std::vector<SessionRow> rows;
    if (!data::sessions(m_game_id, ui::profile_filter(), m_has_last ? &m_last : nullptr, kPageSize, rows)) {
        brls::Application::notify(data::last_error());
        return nullptr;
    }
    // Página incompleta: não há mais nada. O foco sai de m_more antes de ocultá-la.
    if ((int)rows.size() < kPageSize) m_more->setVisibility(brls::Visibility::GONE);
    if (rows.empty()) return nullptr;
    m_last = rows.back();
    m_has_last = true;

    brls::View* first = nullptr;
    for (auto& r : rows) {
        auto cell = tico::cell();
        std::string text = data::format_date(r.start) + "  ·  " + data::format_duration(r.duration);
        if (m_game_id == 0) text = r.game_name + "  ·  " + text;
        cell->setText(text);

        std::string detail = r.user_name;
        if (r.residual) detail += std::string(" · ") + tr("tempo anterior / não detalhado");
        if (r.live) detail += std::string(" · ") + tr("em andamento");
        else if (r.ghost) detail += std::string(" · ") + tr("fantasma");
        cell->setDetailText(detail);
        if (r.ghost) cell->setDetailTextColor(nvgRGB(230, 160, 40));

        SessionRow copy = r;
        cell->registerClickAction([this, copy](brls::View*) {
            open_actions(copy);
            return true;
        });
        m_list->addView(cell, m_list->getChildren().size() - 1);  // antes de "Carregar mais"
        if (!first) first = cell;
    }
    return first;
}

void SessionList::open_actions(const SessionRow& s) {
    if (s.live) {
        brls::Application::notify(tr("Sessão em andamento: aguarde o jogo fechar."));
        return;
    }
    enum Action { Reassign, Move, Finalize, Delete };
    auto actions = std::make_shared<std::vector<Action>>();
    std::vector<std::string> labels;
    actions->push_back(Reassign);
    labels.push_back(tr("Reassociar a outro perfil"));
    actions->push_back(Move);
    labels.push_back(tr("Mover para outro jogo"));
    if (s.ghost) {
        actions->push_back(Finalize);
        labels.push_back(tr("Finalizar (manter duração)"));
    }
    actions->push_back(Delete);
    labels.push_back(tr("Excluir sessão"));

    auto done = [this](bool ok, const char* msg) {
        if (ui::report(ok, msg)) {
            rebuild();
            m_on_change();
        }
    };
    int64_t sid = s.id;
    int64_t gid = s.game_id;
    ui::pick(data::format_date(s.start), labels, [actions, done, sid, gid](int i) {
        switch ((*actions)[i]) {
            case Reassign:
                ui::pick_user(tr("Novo perfil da sessão"), [done, sid](int64_t user) {
                    done(data::reassign_session_user(sid, user), tr("Sessão reassociada"));
                });
                break;
            case Move:
                ui::pick_game(tr("Mover sessão para..."), gid, [done, sid](int64_t game, std::string) {
                    done(data::move_session(sid, game), tr("Sessão movida"));
                });
                break;
            case Finalize:
                done(data::finalize_ghost(sid), tr("Sessão finalizada"));
                break;
            case Delete:
                ui::confirm(tr("Excluir esta sessão? O tempo dela sai do total."), tr("Excluir"),
                            [done, sid] { done(data::delete_session(sid), tr("Sessão excluída")); });
                break;
        }
    });
}

// --- Tela do forwarder fantasma ------------------------------------------------
GhostActivity::GhostActivity(std::string title_id) : m_title_id(std::move(title_id)) {}

brls::View* GhostActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::ROW);
    root->setPadding(40, 50, 40, 50);
    auto col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1);

    GameRow g;
    bool found = data::game_by_title(m_title_id, g);
    std::string icon = found && !g.cover.empty() ? g.cover : data::system_icon(m_title_id);
    if (!icon.empty()) {
        auto img = new brls::Image();
        img->setSize(brls::Size(256, 256));
        img->setScalingType(brls::ImageScalingType::FIT);
        img->setMarginRight(36);
        img->setImageFromFile(icon);
        root->addView(img);
    }

    auto name = new brls::Label();
    name->setFontSize(30);
    name->setText(found ? g.name : m_title_id);
    col->addView(name);

    auto status = new brls::Label();
    status->setFontSize(20);
    status->setTextColor(nvgRGB(230, 160, 40));
    status->setMarginTop(10);
    status->setText(tr("Este jogo está desinstalado."));
    col->addView(status);

    auto total = new brls::Label();
    total->setFontSize(26);
    total->setTextColor(nvgRGB(0, 190, 220));
    total->setMarginTop(18);
    total->setText(found ? trf("Tempo total jogado: %s", data::format_duration(g.total).c_str())
                         : std::string(tr("Sem tempo registrado no Ruby Achievements.")));
    col->addView(total);

    if (found) {
        std::vector<std::pair<std::string, int64_t>> totals;
        data::game_user_totals(g.id, totals);
        for (auto& t : totals) {
            auto l = new brls::Label();
            l->setFontSize(18);
            l->setMarginTop(4);
            l->setText(t.first + ": " + data::format_duration(t.second));
            col->addView(l);
        }
        auto last = new brls::Label();
        last->setFontSize(16);
        last->setTextColor(tico::kMuted);
        last->setMarginTop(12);
        last->setText(trf("Jogado pela última vez em %s · %d sessões", data::format_date(g.last_played).c_str(),
                          (int)g.session_count));
        col->addView(last);

        int64_t id = g.id;
        auto open = action_cell(tr("Ver o jogo na biblioteca"), tr("Sessões, perfis e capa"), [id] {
            brls::Application::pushActivity(new GameActivity(id));
        });
        open->setMarginTop(24);
        col->addView(open);
    }
    root->addView(col);

    auto frame = new tico::Frame(root);
    frame->setTitle(std::string("Ruby Achievements · ") + tr("jogo desinstalado"));
    return frame;
}
