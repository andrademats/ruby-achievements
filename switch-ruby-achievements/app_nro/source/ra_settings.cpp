#include "pt_config.h"
#include "pt_paths.h"
#include "ui.hpp"

namespace {

// Usuário do RetroAchievements: letras, números, "_", "-" e "." (2 a 32).
bool valid_user(const std::string& s) {
    if (s.size() < 2 || s.size() > 32) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') return false;
    return true;
}

// Chave da Web API: só letras e números (a do site tem 32).
bool valid_key(const std::string& s) {
    if (s.size() < 16 || s.size() > 63) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c)) return false;
    return true;
}

std::string trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    return s;
}

std::string masked(const std::string& key) {
    if (key.empty()) return tr("não configurada");
    return "****" + key.substr(key.size() > 4 ? key.size() - 4 : 0);
}

}  // namespace

brls::View* RaSettingsActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(30, 40, 30, 40);

    auto info = new brls::Label();
    info->setFontSize(16);
    info->setMarginBottom(16);
    info->setText(trf(
        "Conta do RetroAchievements usada para mostrar na galeria as conquistas que você já desbloqueou nos "
        "emuladores (RetroArch com o login do RetroAchievements). A chave da Web API fica em "
        "retroachievements.org > Settings > Keys. Os dados ficam em %s, que também pode ser "
        "editado no PC (ra_user= e ra_key=).",
        PT_SECRETS_PATH));
    root->addView(info);

    m_user = new brls::DetailCell();
    m_user->setText(tr("Usuário"));
    m_user->registerClickAction([this](brls::View*) {
        PtConfig cfg;
        pt_config_load(&cfg);
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                brls::sync([this, text] {
                    std::string user = trim(text);
                    if (!valid_user(user)) {
                        brls::Application::notify(tr("Usuário inválido: 2 a 32 letras, números, _ - ou ."));
                        return;
                    }
                    PtConfig c;
                    pt_config_load(&c);
                    snprintf(c.ra_user, sizeof(c.ra_user), "%s", user.c_str());
                    brls::Application::notify(pt_secrets_save(&c) == 0 ? tr("Usuário salvo")
                                                                      : tr("Não foi possível gravar o secrets.ini."));
                    refresh();
                });
            },
            tr("Usuário do RetroAchievements"), tr("Como aparece no site"), 32, cfg.ra_user);
        return true;
    });
    root->addView(m_user);

    m_key = new brls::DetailCell();
    m_key->setText(tr("Chave da Web API"));
    m_key->registerClickAction([this](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                brls::sync([this, text] {
                    std::string key = trim(text);
                    if (!valid_key(key)) {
                        brls::Application::notify(tr("Chave inválida: só letras e números (a do site tem 32)."));
                        return;
                    }
                    PtConfig c;
                    pt_config_load(&c);
                    snprintf(c.ra_key, sizeof(c.ra_key), "%s", key.c_str());
                    brls::Application::notify(pt_secrets_save(&c) == 0 ? tr("Chave salva")
                                                                      : tr("Não foi possível gravar o secrets.ini."));
                    refresh();
                });
            },
            tr("Chave da Web API do RetroAchievements"), "retroachievements.org > Settings > Keys", 63, "");
        return true;
    });
    root->addView(m_key);

    auto clear = new brls::DetailCell();
    clear->setText(tr("Remover usuário e chave"));
    clear->registerClickAction([this](brls::View*) {
        ui::confirm(tr("Remover o usuário e a chave do RetroAchievements deste console?"), tr("Remover"), [this] {
            PtConfig c;
            pt_config_load(&c);
            c.ra_user[0] = '\0';
            c.ra_key[0] = '\0';
            brls::Application::notify(pt_secrets_save(&c) == 0 ? tr("Removidos") : tr("Não foi possível gravar o secrets.ini."));
            refresh();
        });
        return true;
    });
    root->addView(clear);

    refresh();
    auto frame = new tico::Frame(root, "RetroAchievements", tico::Backdrop::SETTINGS);
    return frame;
}

void RaSettingsActivity::refresh() {
    PtConfig cfg;
    pt_config_load(&cfg);
    m_user->setDetailText(cfg.ra_user[0] ? cfg.ra_user : tr("não configurado"));
    m_key->setDetailText(masked(cfg.ra_key));
}
