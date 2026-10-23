#include "pt_config.h"
#include "pt_paths.h"
#include "ra_sync.hpp"
#include "ui.hpp"

namespace {

std::string progress_text(int unlocked, int total) {
    int pct = total > 0 ? unlocked * 100 / total : 0;
    return std::to_string(unlocked) + "/" + std::to_string(total) + " (" + std::to_string(pct) + "%)";
}

std::string profile_suffix() {
    std::string p = ui::profile_name();
    return p.empty() ? std::string(tr("todos os perfis")) : p;
}

// Linha focável (o ScrollingFrame rola seguindo o foco).
brls::Box* achievement_row(const AchievementRow& a) {
    bool open = a.unlocked_at > 0;
    auto row = new brls::Box(brls::Axis::ROW);
    row->setPadding(10, 14, 10, 14);
    row->setMarginBottom(8);
    row->setCornerRadius(6);
    row->setFocusable(true);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setBackgroundColor(open ? tico::kRowOpen : tico::kRowLocked);

    if (!a.icon.empty()) {
        auto img = new brls::Image();
        img->setSize(brls::Size(64, 64));
        img->setScalingType(brls::ImageScalingType::FIT);
        img->setImageFromFile(a.icon);
        img->setMarginRight(16);
        row->addView(img);
    } else {
        // Sem ícone: quadro com uma estrela (dourado = desbloqueada).
        auto badge = new brls::Box(brls::Axis::COLUMN);
        badge->setSize(brls::Size(64, 64));
        badge->setCornerRadius(6);
        badge->setMarginRight(16);
        badge->setJustifyContent(brls::JustifyContent::CENTER);
        badge->setAlignItems(brls::AlignItems::CENTER);
        badge->setBackgroundColor(open ? nvgRGB(230, 180, 40) : tico::kBadgeLocked);
        badge->addView(new tico::Icon(tico::icon::kStar, 34, open ? nvgRGB(20, 20, 20) : tico::kMuted));
        row->addView(badge);
    }

    auto col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1);
    bool hidden = a.secret && !open;
    auto title = new brls::Label();
    title->setFontSize(19);
    title->setText(hidden ? std::string(tr("Conquista secreta")) : a.title);
    title->setTextColor(open ? tico::kText : tico::kMuted);
    col->addView(title);
    if (!hidden && !a.description.empty()) {
        auto desc = new brls::Label();
        desc->setFontSize(15);
        desc->setMarginTop(2);
        desc->setTextColor(tico::kMuted);
        desc->setText(a.description);
        col->addView(desc);
    }
    auto status = new brls::Label();
    status->setFontSize(14);
    status->setMarginTop(4);
    status->setTextColor(open ? nvgRGB(0, 190, 120) : tico::kMuted);
    status->setText(open ? trf("desbloqueada em %s", data::format_date(a.unlocked_at).c_str())
                         : std::string(tr("bloqueada")));
    col->addView(status);
    row->addView(col);

    // Troféu da raridade: colorido quando desbloqueada; cinza e apagado enquanto bloqueada.
    static const char* const kRarities[] = {"bronze", "silver", "gold", "ruby"};
    std::string rarity = "bronze";
    for (const char* r : kRarities)
        if (a.rarity == r) rarity = r;
    auto trophy = new brls::Image();
    trophy->setSize(brls::Size(40, 40));
    trophy->setMarginLeft(12);
    trophy->setScalingType(brls::ImageScalingType::FIT);
    trophy->setImageFromRes("img/trophy_" + rarity + (open ? ".png" : "_off.png"));
    row->addView(trophy);
    return row;
}

// Nome do pacote na lista de escolha: "rótulo (N conquistas)".
std::string pack_label(const AchievementPack& p) {
    return p.count > 0 ? trf("%s (%d conquistas)", p.label.c_str(), p.count) : p.label;
}

// Escolha do pacote ativo do jogo; trocar apaga o progresso (confirmado antes).
void choose_pack(int64_t game_id, const std::string& name) {
    std::vector<AchievementPack> packs;
    if (!data::achievement_packs(game_id, packs)) {
        ui::report(false, "");
        return;
    }
    if (packs.size() < 2) {
        brls::Application::notify(tr("Só há um pacote de conquistas para este jogo no cartão SD."));
        return;
    }
    std::vector<std::string> labels;
    int current = 0;
    for (size_t i = 0; i < packs.size(); i++) {
        labels.push_back(pack_label(packs[i]));
        if (packs[i].active) current = (int)i;
    }
    ui::pick(tr("Pacote de conquistas ativo"), labels, [game_id, name, packs, current](int i) {
        if (i == current) return;
        AchievementPack chosen = packs[i];
        ui::confirm(trf("Ativar o pacote \"%s\"?\n\nTodo o progresso de conquistas deste jogo (todos os perfis) "
                        "será apagado. O pacote atual continua no cartão SD; se for ativado de novo, começa do zero.",
                        chosen.label.c_str()),
                    tr("Ativar"), [game_id, name, chosen] {
                        if (!ui::report(data::set_active_pack(game_id, chosen.file),
                                        trf("Pacote \"%s\" ativo", chosen.label.c_str())))
                            return;
                        AchievementsTab::request_refresh();
                        HomeActivity::request_refresh();
                        // Recria a tela com o pacote novo (sem reaproveitar views com foco).
                        brls::Application::popActivity(brls::TransitionAnimation::FADE, [game_id, name] {
                            brls::sync([game_id, name] {
                                brls::Application::pushActivity(new AchievementListActivity(game_id, name));
                            });
                        });
                    });
    }, current);
}

}  // namespace

// --- Aba: jogos com conquistas ------------------------------------------------------
AchievementsTab* AchievementsTab::s_current = nullptr;

AchievementsTab::AchievementsTab() {
    s_current = this;
    auto progress = add_card(tr("Progresso"));
    m_summary = tico::cell();
    progress->addView(m_summary);

    m_sync = tico::cell(tr("Sincronizar RetroAchievements"), tr("Conquistas das ROMs"));
    m_sync->registerClickAction([](brls::View*) {
        PtConfig cfg;
        pt_config_load(&cfg);
        if (!cfg.ra_user[0] || !cfg.ra_key[0]) brls::Application::pushActivity(new RaSettingsActivity());
        else brls::Application::pushActivity(new RaSyncActivity());
        return true;
    });
    progress->addView(m_sync);

    auto games = add_card(tr("Jogos"), true);
    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_list = new brls::Box(brls::Axis::COLUMN);
    tico::set_scroll_content(scroll, m_list);
    games->addView(scroll);
    rebuild();
}

AchievementsTab::~AchievementsTab() {
    if (s_current == this) s_current = nullptr;
}

void AchievementsTab::request_refresh() {
    brls::sync([] {
        if (s_current) s_current->rebuild();
    });
}

void AchievementsTab::rebuild() {
    ui::rescue_focus(m_list, m_summary);
    m_list->clearViews();
    m_summary->setText(std::string(tr("Conquistas")) + " · " + profile_suffix());

    std::vector<AchievementGameRow> games;
    if (!data::achievement_games(ui::profile_filter(), games)) {
        m_summary->setDetailText(data::last_error());
        return;
    }
    int total = 0, unlocked = 0;
    for (auto& g : games) {
        total += g.total;
        unlocked += g.unlocked;
    }
    m_summary->setDetailText(progress_text(unlocked, total));

    if (games.empty()) {
        m_list->addView(tico::card_text(
            trf("Nenhum jogo com conquistas ainda. Sincronize o RetroAchievements (ROMs) ou crie as "
                "definições em %s/<Title ID>.json (jogos do Switch) ou <nome da ROM>.json "
                "(sem a extensão): o sysmodule avalia enquanto você joga.",
                PT_ACH_DIR)));
        return;
    }
    for (auto& g : games) {
        auto cell = tico::cell(g.name, progress_text(g.unlocked, g.total));
        if (g.unlocked == g.total) cell->setDetailTextColor(nvgRGB(230, 180, 40));  // completo
        int64_t id = g.game_id;
        std::string name = g.name;
        cell->registerClickAction([id, name](brls::View*) {
            brls::Application::pushActivity(new AchievementListActivity(id, name));
            return true;
        });
        m_list->addView(cell);
    }
}

// --- Sincronização com o RetroAchievements ---------------------------------------------
RaSyncActivity::RaSyncActivity() : m_alive(std::make_shared<bool>(true)) {}

RaSyncActivity::~RaSyncActivity() { *m_alive = false; }

brls::View* RaSyncActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(40, 50, 40, 50);

    auto info = new brls::Label();
    info->setFontSize(16);
    info->setTextColor(tico::kMuted);
    info->setText(trf("Para cada ROM da biblioteca: hash do arquivo, jogo correspondente no RetroAchievements e o "
                      "que a sua conta já desbloqueou. Só leitura: nada é enviado ao RetroAchievements. Detalhes em "
                      "%s/retroachievements.log.",
                      PT_DIR));
    root->addView(info);

    auto spinner = new brls::ProgressSpinner();
    spinner->setMarginTop(24);
    m_spinner = spinner;
    root->addView(spinner);

    m_step = new brls::Label();
    m_step->setFontSize(20);
    m_step->setMarginTop(12);
    m_step->setText(tr("Preparando..."));
    root->addView(m_step);

    m_result = new brls::Label();
    m_result->setFontSize(18);
    m_result->setMarginTop(16);
    m_result->setText(" ");
    root->addView(m_result);

    m_actions = new brls::Box(brls::Axis::COLUMN);
    m_actions->setMarginTop(16);
    root->addView(m_actions);

    auto frame = new tico::Frame(root);
    frame->setTitle(tr("Sincronizar RetroAchievements"));
    return frame;
}

void RaSyncActivity::onContentAvailable() {
    registerAction(tr("Voltar"), brls::BUTTON_B, [this](brls::View*) {
        if (!m_running) brls::Application::popActivity();
        return true;
    });
    m_running = true;
    auto alive = m_alive;
    brls::async([this, alive] {
        ra::SyncReport rep;
        bool ok = ra::sync(
            [this, alive](int done, int total, const std::string& what) {
                std::string text = std::to_string(done) + "/" + std::to_string(total) + (what.empty() ? "" : "  " + what);
                brls::sync([this, alive, text] {
                    if (*alive) m_step->setText(text);
                });
            },
            rep);
        brls::sync([this, alive, ok, rep] {
            if (!*alive) return;
            m_running = false;
            m_spinner->setVisibility(brls::Visibility::GONE);
            m_step->setText(ok ? tr("Concluído") : tr("Interrompido"));
            std::string text = trf("%d de %d ROMs reconhecidas · %d conquistas · %d desbloqueios novos", (int)rep.matched,
                                   (int)rep.roms, (int)rep.achievements, (int)rep.new_unlocks);
            if (rep.unknown) text += "\n" + trf("%d sem jogo no RetroAchievements (hash diferente)", (int)rep.unknown);
            if (rep.unsupported) text += "\n" + trf("%d em formato sem suporte (discos, .zip)", (int)rep.unsupported);
            if (rep.missing) text += "\n" + trf("%d não encontradas no cartão SD", (int)rep.missing);
            if (!ok) text = rep.error + "\n\n" + text;
            m_result->setTextColor(ok ? nvgRGB(0, 190, 120) : nvgRGB(230, 90, 70));
            m_result->setText(text);

            auto back = new brls::DetailCell();
            back->setText(tr("Voltar"));
            back->registerClickAction([](brls::View*) {
                brls::Application::popActivity();
                return true;
            });
            m_actions->addView(back);
            brls::Application::giveFocus(back);
        });
    });
}

// --- Conquistas de um jogo -------------------------------------------------------------
AchievementListActivity::AchievementListActivity(int64_t game_id, std::string name)
    : m_game_id(game_id), m_name(std::move(name)) {}

brls::View* AchievementListActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(20, 40, 0, 40);

    std::vector<AchievementRow> rows;
    bool ok = data::achievements(m_game_id, ui::profile_filter(), rows);

    // Célula focável: a tela tem foco mesmo sem conquistas.
    auto summary = new brls::DetailCell();
    summary->setText(profile_suffix());
    summary->setMarginBottom(8);
    root->addView(summary);

    // Jogos do Switch com pacote no SD: qual está ativo (A troca, se houver outros).
    std::vector<AchievementPack> packs;
    if (data::achievement_packs(m_game_id, packs) && !packs.empty()) {
        std::string active;
        for (auto& p : packs)
            if (p.active) active = p.label;
        auto pack = new brls::DetailCell();
        pack->setText(tr("Pacote de conquistas"));
        pack->setDetailText(packs.size() > 1 ? trf("%s · %d no cartão SD", active.c_str(), (int)packs.size()) : active);
        pack->setMarginBottom(8);
        int64_t id = m_game_id;
        std::string name = m_name;
        pack->registerClickAction([id, name](brls::View*) {
            choose_pack(id, name);
            return true;
        });
        root->addView(pack);
    }

    auto scroll = tico::scroller();
    scroll->setGrow(1);
    auto list = new brls::Box(brls::Axis::COLUMN);
    list->setPaddingBottom(30);
    tico::set_scroll_content(scroll, list);
    root->addView(scroll);

    if (!ok) {
        summary->setDetailText(data::last_error());
    } else {
        int unlocked = 0;
        for (auto& a : rows) unlocked += a.unlocked_at > 0;
        summary->setDetailText(progress_text(unlocked, (int)rows.size()));
        for (auto& a : rows) list->addView(achievement_row(a));
        if (rows.empty()) {
            auto none = new brls::Label();
            none->setText(tr("Sem conquistas para este jogo."));
            list->addView(none);
        }
    }

    auto frame = new tico::Frame(root);
    frame->setTitle(std::string(tr("Conquistas")) + " · " + m_name);
    return frame;
}
