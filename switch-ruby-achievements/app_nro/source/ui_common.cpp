#include "pt_config.h"
#include "ui.hpp"

namespace ui {

void pick(const std::string& title, const std::vector<std::string>& options, std::function<void(int)> cb,
          int selected) {
    tico::pick(title, options, std::move(cb), selected);
}

void confirm(const std::string& text, const std::string& action, std::function<void()> cb) {
    tico::confirm(text, action, std::move(cb));
}

bool report(bool ok, const std::string& success) {
    brls::Application::notify(ok ? success : data::last_error());
    return ok;
}

void pick_user(const std::string& title, std::function<void(int64_t)> cb) {
    auto users = std::make_shared<std::vector<UserRow>>();
    if (!data::users(*users) || users->empty()) {
        report(false, "");
        return;
    }
    std::vector<std::string> names;
    for (auto& u : *users) names.push_back(u.nickname);
    pick(title, names, [users, cb](int i) {
        if (i >= 0 && i < (int)users->size()) cb((*users)[i].id);
    });
}

void pick_game(const std::string& title, int64_t exclude, std::function<void(int64_t, std::string)> cb) {
    auto games = std::make_shared<std::vector<GameRow>>();
    std::vector<GameRow> all;
    if (!data::games(0, all)) {
        report(false, "");
        return;
    }
    std::vector<std::string> names;
    for (auto& g : all) {
        if (g.id == exclude) continue;
        games->push_back(g);
        names.push_back(g.name + "  (" + data::source_label(g.source) + ")");
    }
    if (names.empty()) {
        brls::Application::notify(tr("Nenhum outro jogo disponível."));
        return;
    }
    pick(title, names, [games, cb](int i) {
        if (i >= 0 && i < (int)games->size()) cb((*games)[i].id, (*games)[i].name);
    });
}

namespace {
int64_t g_profile = -1;  // users.id; 0 = todos os perfis; -1 = ainda não lido do config.ini
}

// O último perfil escolhido fica em config.ini ([app] app_profile) e volta ao reabrir o app.
int64_t profile_filter() {
    if (g_profile < 0) {
        PtConfig c;
        pt_config_load(&c);
        g_profile = c.app_profile;
    }
    return g_profile;
}

void set_profile_filter(int64_t user_id) {
    if (user_id < 0) user_id = 0;
    if (user_id == profile_filter()) return;
    g_profile = user_id;
    PtConfig c;
    pt_config_load(&c);
    c.app_profile = user_id;
    if (pt_config_save_app(&c) != 0) brls::Logger::warning("config.ini: perfil não gravado");
}

std::string profile_name() {
    if (profile_filter() == 0) return "";
    std::vector<UserRow> users;
    data::users(users);
    for (auto& u : users)
        if (u.id == g_profile) return u.nickname;
    return "";
}

namespace {
// Fecha as atividades acima da tela inicial, uma por quadro, e depois roda `then`.
void pop_to_home(std::function<void()> then) {
    if (brls::Application::getActivitiesStack().size() <= 1) {
        then();
        return;
    }
    brls::Application::popActivity(brls::TransitionAnimation::NONE, [then] {
        // Roda antes de a atividade fechada ser apagada: continua no próximo quadro.
        brls::sync([then] { pop_to_home(then); });
    });
}
}  // namespace

void reload_interface(int settings_tab) {
    tico::apply_theme();
    pop_to_home([settings_tab] {
        HomeActivity::reload();
        if (settings_tab >= 0)
            brls::Application::pushActivity(new SettingsActivity(settings_tab), brls::TransitionAnimation::NONE);
    });
}

void rescue_focus(brls::View* container, brls::View* fallback) {
    for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
        if (v == container) {
            brls::Application::giveFocus(fallback);
            return;
        }
    }
}

}  // namespace ui
