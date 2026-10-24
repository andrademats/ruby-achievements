// Ações de um jogo, usadas pela tela do jogo e pelos menus + / - da tela inicial.
#include "ui.hpp"

namespace {

std::string base_name(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

void change_cover_local(const GameRow& g, std::function<void()> changed) {
    auto paths = std::make_shared<std::vector<std::string>>();
    std::vector<std::string> names;

    std::string icon = data::system_icon(g.title_id);
    if (!icon.empty()) {
        paths->push_back(icon);
        names.push_back(tr("Ícone do sistema"));
    }
    for (auto& f : data::cover_files()) {
        paths->push_back(f);
        names.push_back(base_name(f));
    }
    paths->push_back("");
    names.push_back(tr("Sem capa"));

    if (names.size() == 1)
        brls::Application::notify(tr("Copie imagens .png/.jpg para /switch/ruby_achievements/covers para escolher uma capa."));

    int64_t id = g.id;
    ui::pick(trf("Capa de %s", g.name.c_str()), names, [id, paths, changed](int i) {
        if (ui::report(data::set_cover(id, (*paths)[i]), tr("Capa atualizada"))) {
            ui::forget_texture((*paths)[i]);  // o arquivo pode ter sido trocado no SD
            changed();
        }
    });
}

}  // namespace

namespace ui {

void edit_game_name(const GameRow& g, std::function<void()> changed) {
    int64_t id = g.id;
    brls::Application::getImeManager()->openForText(
        [id, changed](std::string text) {
            brls::sync([id, text, changed] {
                if (ui::report(data::rename_game(id, text), tr("Nome atualizado"))) changed();
            });
        },
        tr("Nome do jogo"), tr("Apenas neste app"), 128, g.name);
}

void change_game_cover(const GameRow& g, std::function<void()> changed) {
    ui::pick(trf("Capa de %s", g.name.c_str()), {tr("Arquivo local (.png/.jpg)"), tr("Baixar do SteamGridDB")},
             [g, changed](int i) {
        if (i == 0) {
            change_cover_local(g, changed);
            return;
        }
        std::string base = g.title_id.empty() ? "game" + std::to_string(g.id) : g.title_id;
        int64_t id = g.id;
        brls::Application::pushActivity(
            new CoverSearchActivity(g, tr("Capa do SteamGridDB"), base, [id, changed](const std::string& path) {
                if (!ui::report(data::set_cover(id, path), tr("Capa atualizada"))) return;
                changed();
                HomeActivity::request_refresh();
            }));
    });
}

void merge_game(const GameRow& g, std::function<void()> gone) {
    int64_t id = g.id;
    std::string name = g.name;
    ui::pick_game(trf("Unificar \"%s\" em...", name.c_str()), id, [id, name, gone](int64_t target, std::string target_name) {
        ui::confirm(trf("Mover todas as sessões de \"%s\" para \"%s\"? O jogo atual deixa de aparecer na biblioteca.",
                        name.c_str(), target_name.c_str()),
                    tr("Unificar"), [id, target, gone] {
                        if (ui::report(data::merge_games(id, target), tr("Jogos unificados"))) gone();
                    });
    });
}

void reassign_game(const GameRow& g, std::function<void()> changed) {
    int64_t id = g.id;
    ui::pick_user(trf("Reassociar sessões de %s", g.name.c_str()), [id, changed](int64_t user) {
        ui::confirm(tr("Atribuir todas as sessões deste jogo a este perfil?"), tr("Reassociar"), [id, user, changed] {
            if (ui::report(data::reassign_game_user(id, user), tr("Sessões reassociadas"))) changed();
        });
    });
}

void open_sessions(const GameRow& g) { brls::Application::pushActivity(new SessionsActivity(g.id, g.name)); }

void disable_tracking(const GameRow& g, std::function<void()> changed) {
    int64_t id = g.id;
    std::string name = g.name;
    ui::confirm(trf("Desativar o tracking de tempo de %s?\n\nO tempo dele deixa de ser contado e ele sai da tela "
                    "inicial. As sessões já gravadas ficam guardadas. Para reativar: engrenagem do menu + > Gerenciamento > Jogos desativados.",
                    name.c_str()),
                tr("Desativar"), [id, name, changed] {
                    if (!ui::report(data::set_tracking(id, false), trf("%s desativado", name.c_str()))) return;
                    HomeActivity::request_refresh();
                    changed();
                });
}

}  // namespace ui
