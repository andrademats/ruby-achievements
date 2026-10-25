// Tela que consulta, baixa e instala a atualização (update.hpp); abre pela aba App.
#include "i18n.hpp"
#include "pt_log.h"
#include "pt_paths.h"
#include "pt_update.h"
#include "tico.hpp"
#include "ui.hpp"
#include "update.hpp"

#include <borealis.hpp>

#include <exception>

struct UpdateState {
    upd::Check check;
};

namespace {

// Roda uma etapa da atualização na thread de trabalho: exceção (falta de memória, JSON) vira
// erro na tela em vez de fechar o app.
bool guarded(const char* step, const std::function<bool(std::string&)>& fn, std::string& error) {
    try {
        return fn(error);
    } catch (const std::exception& e) {
        error = trf("Erro inesperado (%s): %s", step, e.what());
    } catch (...) {
        error = trf("Erro inesperado (%s).", step);
    }
    return false;
}

}  // namespace

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
    // Recebe o foco enquanto não há botões: apagar o botão focado sem mover o foco antes
    // deixava o Borealis com um ponteiro para a view apagada (o app fechava ao confirmar a
    // instalação, 2026-10-25).
    m_result->setFocusable(true);
    m_result->setHideHighlight(true);
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
        bool ok = guarded(tr("verificação"), [&](std::string& e) { return upd::check(state->check, e); }, error);
        if (ok)
            pt_log_file(PT_UPDATE_LOG_PATH, "app: verificacao ok: release %s, versao %s (instalada %s)",
                        state->check.release.tag.c_str(), upd::to_string(state->check.manifest.version).c_str(),
                        upd::to_string(upd::current()).c_str());
        else
            pt_log_file(PT_UPDATE_LOG_PATH, "app: verificacao falhou: %s", error.c_str());
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
                                  // O foco sai do botão antes de ele ser apagado.
                                  brls::Application::giveFocus(m_result);
                                  m_actions->clearViews();
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
        pt_log_file(PT_UPDATE_LOG_PATH, "app: instalacao da versao %s iniciada",
                    upd::to_string(state->check.manifest.version).c_str());
        bool ok = guarded(
            tr("instalação"),
            [&](std::string& e) {
                return upd::install(
                    state->check,
                    [this, alive](const std::string& step) {
                        pt_log_file(PT_UPDATE_LOG_PATH, "app: %s", step.c_str());
                        brls::sync([this, alive, step] {
                            if (*alive) m_step->setText(step);
                        });
                    },
                    e);
            },
            error);
        pt_log_file(PT_UPDATE_LOG_PATH, "app: instalacao %s", ok ? "concluida" : ("falhou: " + error).c_str());
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
