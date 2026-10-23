// Aba Atualização das Configurações e a tela que consulta, baixa e instala (update.hpp).
#include "i18n.hpp"
#include "pt_paths.h"
#include "pt_update.h"
#include "tico.hpp"
#include "ui.hpp"
#include "update.hpp"

#include <borealis.hpp>

struct UpdateState {
    upd::Check check;
};

// --- Aba ---------------------------------------------------------------------------------
UpdateTab::UpdateTab() : tico::Page(true) {
    auto version = add_card(tr("Versão"));
    version->addView(tico::cell(tr("Versão instalada"), upd::to_string(upd::current())));
    char pending[32];
    if (pt_update_pending(pending, sizeof(pending)))
        version->addView(tico::card_text(
            trf("A versão %s já foi instalada. Reinicie o console para usá-la: o app, o overlay e o sysmodule só "
                "trocam no reinício.",
                pending)));

    auto update = add_card(tr("Atualização"));
    auto button = tico::cell(tr("Verificar e atualizar aplicativo"), "GitHub");
    button->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new UpdateActivity());
        return true;
    });
    update->addView(button);
    update->addView(tico::card_text(
        tr("Consulta a última versão publicada no GitHub. A instalação confere a assinatura do pacote e o SHA-256 de "
           "cada arquivo antes de trocar o app, o overlay e o sysmodule. Banco de dados, histórico, capas, sons, "
           "pacotes de conquistas, config.ini e secrets.ini não são tocados, e o banco ganha uma cópia de "
           "segurança antes. Se a troca for interrompida, a versão anterior volta sozinha.")));
}

// --- Tela --------------------------------------------------------------------------------
UpdateActivity::UpdateActivity() : m_alive(std::make_shared<bool>(true)), m_state(std::make_shared<UpdateState>()) {}

UpdateActivity::~UpdateActivity() { *m_alive = false; }

brls::View* UpdateActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(40, 50, 40, 50);

    auto info = new brls::Label();
    info->setFontSize(16);
    info->setTextColor(tico::kMuted);
    info->setText(trf("Versão instalada: %s. Detalhes da instalação em %s.", upd::to_string(upd::current()).c_str(),
                      PT_UPDATE_LOG_PATH));
    root->addView(info);

    auto spinner = new brls::ProgressSpinner();
    spinner->setMarginTop(24);
    m_spinner = spinner;
    root->addView(spinner);

    m_step = new brls::Label();
    m_step->setFontSize(20);
    m_step->setMarginTop(12);
    m_step->setText(tr("Consultando o GitHub..."));
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
    frame->setTitle(tr("Atualização"));
    return frame;
}

void UpdateActivity::onContentAvailable() {
    // B só funciona fora da consulta e da instalação.
    registerAction(tr("Voltar"), brls::BUTTON_B, [this](brls::View*) {
        if (!m_running) brls::Application::popActivity();
        return true;
    });
    run_check();
}

void UpdateActivity::add_action(const std::string& label, std::function<void()> cb) {
    auto cell = new brls::DetailCell();
    cell->setText(label);
    cell->registerClickAction([cb](brls::View*) {
        cb();
        return true;
    });
    bool first = m_actions->getChildren().empty();
    m_actions->addView(cell);
    if (first) brls::Application::giveFocus(cell);
}

void UpdateActivity::finish(bool ok, const std::string& text) {
    m_running = false;
    m_spinner->setVisibility(brls::Visibility::GONE);
    m_result->setTextColor(ok ? nvgRGB(0, 190, 120) : nvgRGB(230, 90, 70));
    m_result->setText(text);
    add_action(tr("Voltar"), [] { brls::Application::popActivity(); });
}

void UpdateActivity::run_check() {
    m_running = true;
    auto alive = m_alive;
    auto state = m_state;
    brls::async([this, alive, state] {
        std::string error;
        bool ok = upd::check(state->check, error);
        brls::sync([this, alive, state, ok, error] {
            if (!*alive) return;
            const upd::Check& c = state->check;
            if (!ok) {
                m_step->setText(tr("Não foi possível verificar"));
                finish(false, error);
                return;
            }
            std::string installed = upd::to_string(upd::current()), latest = upd::to_string(c.manifest.version);
            if (!c.newer) {
                m_step->setText(tr("Nenhuma atualização"));
                finish(true, trf("Você já está na versão mais recente (%s).", installed.c_str()));
                return;
            }
            std::string blocked = upd::blocked_reason(c);
            if (!blocked.empty()) {
                m_step->setText(trf("Versão %s disponível", latest.c_str()));
                finish(false, blocked);
                return;
            }
            m_running = false;
            m_spinner->setVisibility(brls::Visibility::GONE);
            m_step->setText(trf("Versão %s disponível (instalada: %s)", latest.c_str(), installed.c_str()));
            std::string text = c.manifest.notes.empty() ? std::string(" ") : c.manifest.notes;
            m_result->setTextColor(tico::kText);
            m_result->setText(text);
            add_action(tr("Baixar e instalar"), [this, latest] {
                tico::confirm(trf("Instalar a versão %s? Depois será preciso reiniciar o console.", latest.c_str()),
                              tr("Instalar"), [this] {
                                  m_actions->clearViews();
                                  brls::Application::giveFocus(m_result);
                                  run_install();
                              });
            });
            add_action(tr("Agora não"), [] { brls::Application::popActivity(); });
        });
    });
}

void UpdateActivity::run_install() {
    m_running = true;
    m_spinner->setVisibility(brls::Visibility::VISIBLE);
    m_result->setText(" ");
    auto alive = m_alive;
    auto state = m_state;
    brls::async([this, alive, state] {
        std::string error;
        bool ok = upd::install(
            state->check,
            [this, alive](const std::string& step) {
                brls::sync([this, alive, step] {
                    if (*alive) m_step->setText(step);
                });
            },
            error);
        std::string latest = upd::to_string(state->check.manifest.version);
        brls::sync([this, alive, ok, error, latest] {
            if (!*alive) return;
            m_step->setText(ok ? tr("Concluído") : tr("Falhou"));
            finish(ok, ok ? trf("Versão %s instalada. Reinicie o console para usá-la: o app, o overlay e o sysmodule "
                                "só trocam no reinício.",
                                latest.c_str())
                          : error);
        });
    });
}
