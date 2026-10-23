#include "ui.hpp"

#include "pt_config.h"
#include "pt_paths.h"
#include "pt_status.h"
#include "pt_util.h"
#include "prefs.hpp"
#include "pt_wav.hpp"
#include "steamgriddb.hpp"
#include "icon_image.hpp"

namespace {
const char* import_details() {
    return tr("Cada sessão do histórico entra como sessão \"importada\", com data e duração, e o total de cada jogo e "
              "perfil fecha exatamente com o total do sistema (o que o histórico não detalha vira uma sessão \"tempo "
              "anterior / não detalhado\"). Sessões que coincidem com as do Ruby Achievements ficam de fora. Suas outras sessões "
              "não são alteradas; repetir a importação refaz só as importadas. Detalhes em /switch/ruby_achievements/import.log.");
}
}  // namespace

// --- Configurações globais (engrenagem do menu +) --------------------------------
brls::View* SettingsActivity::createContentView() {
    // Ordem pedida em 2026-10-20 (10ª etapa); kInterfaceTab é o índice da Interface.
    auto tabs = new tico::Tabs();
    tabs->addTab(tico::icon::kPalette, tr("Interface"), [] { return new InterfaceTab(); });
    tabs->addTab(tico::icon::kStar, tr("Conquistas"), [] { return new AchievementsTab(); });
    tabs->addTab(tico::icon::kDelete, tr("Desinstalados"), [] { return new UninstalledTab(); });
    tabs->addTab(tico::icon::kTimerOff, tr("Desativados"), [] { return new DisabledTab(); });
    tabs->addTab(tico::icon::kHistory, tr("Sessões fantasmas"), [] { return new GhostsTab(); });
    tabs->addTab(tico::icon::kKey, tr("Chaves"), [] { return new KeysTab(); });
    tabs->addTab(tico::icon::kUpdate, tr("Atualização"), [] { return new UpdateTab(); });
    tabs->addTab(tico::icon::kInfo, tr("Sobre"), [] { return new AboutTab(); });
    tabs->select(m_initial_tab);
    return tabs;
}

// --- Interface ------------------------------------------------------------------------
namespace {

const char* theme_name(int v) { return v == prefs::THEME_DARK ? tr("Escuro") : tr("Claro"); }
const char* background_name(int v) { return v == prefs::BG_CLASSIC ? tr("Clássico") : tr("Sólido"); }
// Nomes dos idiomas sempre no próprio idioma.
const char* language_name(int v) { return v == i18n::LANG_PT ? "Português (Brasil)" : "English"; }
const char* date_name(int v) {
    switch (v) {
        case i18n::DATE_DMY: return "DD/MM/YYYY";
        case i18n::DATE_YMD: return "YYYY/MM/DD";
        default: return "MM/DD/YYYY";
    }
}

brls::DetailCell* option_cell(const char* title, const std::string& value, std::function<void()> on_click) {
    auto cell = tico::cell(title, value);
    cell->registerClickAction([on_click](brls::View*) {
        if (on_click) on_click();
        return true;
    });
    return cell;
}

void saved(bool ok) {
    if (!ok) brls::Application::notify(tr("Não foi possível gravar o config.ini."));
}

// --- Som do pop-up de conquista (regras em pt_wav.hpp) ---
// "4,2 s" (vírgula em português).
std::string seconds(uint32_t ms) {
    char b[16];
    snprintf(b, sizeof(b), prefs::language() == i18n::LANG_PT ? "%u,%u s" : "%u.%u s", ms / 1000, ms % 1000 / 100);
    return b;
}

std::string sfx_problem(const pt::SfxFile& f) {
    switch (f.check) {
        case pt::SfxCheck::OK: return "";
        case pt::SfxCheck::NOT_WAV: return tr("não é um arquivo WAV válido");
        case pt::SfxCheck::NOT_PCM16: return tr("não é PCM de 16 bits");
        case pt::SfxCheck::RATE: return trf("é de %d Hz (precisa ser 48000 Hz)", f.wav.rate);
        case pt::SfxCheck::CHANNELS: return trf("tem %d canais (precisa ser mono ou estéreo)", f.wav.channels);
        case pt::SfxCheck::TOO_SHORT: return trf("dura %s (mínimo de 3 s)", seconds(f.wav.ms()).c_str());
        case pt::SfxCheck::TOO_LONG: return trf("dura %s (máximo de 10 s)", seconds(f.wav.ms()).c_str());
    }
    return "";
}

const char* sfx_rules() {
    return tr("Formato: arquivo .wav, PCM de 16 bits, 48 kHz (mono ou estéreo).\nDuração: de 3 a 10 segundos (o pop-up "
              "dura o tempo do som).\nPasta: /switch/ruby_achievements/sfx/ (copie o arquivo para lá e escolha aqui).");
}

// Detalhe da célula: o som em uso. Um arquivo que sumiu ou saiu das regras não toca (o
// overlay usa o padrão).
std::string sfx_current() {
    std::string name = prefs::popup_sfx();
    if (name.empty()) return tr("Padrão");
    for (auto& f : pt::list_sfx())
        if (f.name == name && f.check == pt::SfxCheck::OK) return name;
    return trf("%s (inválido: tocando o padrão)", name.c_str());
}

void choose_sfx(brls::DetailCell* cell) {
    std::vector<pt::SfxFile> files = pt::list_sfx();
    std::vector<std::string> options = {trf("Padrão (%s)", pt::kSfxDefault)};
    int selected = 0;
    std::string current = prefs::popup_sfx();
    for (size_t i = 0; i < files.size(); i++) {
        const pt::SfxFile& f = files[i];
        options.push_back(f.name + "  ·  " + (f.check == pt::SfxCheck::OK ? seconds(f.wav.ms()) : sfx_problem(f)));
        if (f.name == current) selected = (int)i + 1;
    }
    ui::pick(tr("Som de desbloqueio"), options, [cell, files](int i) {
        if (i > 0 && files[i - 1].check != pt::SfxCheck::OK) {
            tico::message(trf("%s não pode ser usado: %s.", files[i - 1].name.c_str(), sfx_problem(files[i - 1]).c_str()) +
                          "\n\n" + sfx_rules());
            return;
        }
        saved(prefs::set_popup_sfx(i == 0 ? "" : files[i - 1].name));
        cell->setDetailText(sfx_current());
    }, selected);
}

}  // namespace

InterfaceTab::InterfaceTab() : tico::Page(true) {
    auto look = add_card(tr("Aparência"));
    look->addView(option_cell(tr("Tema visual"), theme_name(prefs::theme()), [] {
        ui::pick(tr("Tema visual"), {theme_name(prefs::THEME_LIGHT), theme_name(prefs::THEME_DARK)}, [](int i) {
            if (i == prefs::theme()) return;
            saved(prefs::set_theme(i));
            ui::reload_interface(SettingsActivity::kInterfaceTab);
        }, prefs::theme());
    }));
    auto bg = option_cell(tr("Estilo de fundo"), background_name(prefs::background()), {});
    bg->registerClickAction([bg](brls::View*) {
        ui::pick(tr("Estilo de fundo"), {background_name(prefs::BG_SOLID), background_name(prefs::BG_CLASSIC)},
                 [bg](int i) {
                     saved(prefs::set_background(i));
                     bg->setDetailText(background_name(prefs::background()));  // o fundo é lido a cada quadro
                 },
                 prefs::background());
        return true;
    });
    look->addView(bg);

    auto region = add_card(tr("Idioma e região"));
    region->addView(option_cell(tr("Idioma do sistema"), language_name(prefs::language()), [] {
        ui::pick(tr("Idioma do sistema"), {language_name(i18n::LANG_EN), language_name(i18n::LANG_PT)}, [](int i) {
            if (i == prefs::language()) return;
            saved(prefs::set_language(i));
            ui::reload_interface(SettingsActivity::kInterfaceTab);
        }, prefs::language());
    }));
    auto date = option_cell(tr("Formato de data"), date_name(prefs::date_format()), {});
    date->registerClickAction([date](brls::View*) {
        ui::pick(tr("Formato de data"),
                 {date_name(i18n::DATE_MDY), date_name(i18n::DATE_DMY), date_name(i18n::DATE_YMD)},
                 [date](int i) {
                     saved(prefs::set_date_format(i));
                     date->setDetailText(date_name(prefs::date_format()));
                 },
                 prefs::date_format());
        return true;
    });
    region->addView(date);
    auto info = tico::card_text(tr("O formato de data vale para o histórico de sessões, as conquistas e os dados "
                                   "dos jogos. Trocar o tema ou o idioma recria as telas abertas."));
    region->addView(info);

    auto sound = add_card(tr("Som da conquista"));
    auto sfx = option_cell(tr("Som de desbloqueio"), sfx_current(), {});
    sfx->registerClickAction([sfx](brls::View*) {
        choose_sfx(sfx);
        return true;
    });
    sound->addView(sfx);
    sound->addView(tico::card_text(sfx_rules()));
}

// --- Apps e jogos desativados -------------------------------------------------------
DisabledTab::DisabledTab() {
    auto card = add_card(tr("Apps e jogos desativados"), true);
    auto info = tico::card_text(
        tr("O tempo destes jogos e apps não é contado e eles não aparecem na tela inicial. As sessões gravadas "
           "antes continuam guardadas. Sphaira e o próprio Ruby Achievements são desativados automaticamente. "
           "Aperte A para reativar o tracking."));
    card->addView(info);
    m_summary = tico::cell(tr("Desativados"));
    card->addView(m_summary);
    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_list = new brls::Box(brls::Axis::COLUMN);
    tico::set_scroll_content(scroll, m_list);
    card->addView(scroll);
    rebuild();
}

void DisabledTab::rebuild() {
    ui::rescue_focus(m_list, m_summary);
    m_list->clearViews();
    std::vector<GameRow> games;
    if (!data::disabled_games(games)) {
        m_summary->setDetailText(data::last_error());
        return;
    }
    m_summary->setDetailText(games.empty() ? std::string(tr("nenhum"))
                                           : games.size() == 1 ? std::string(tr("1 item"))
                                                               : trf("%d itens", (int)games.size()));
    for (auto& g : games) {
        auto cell = tico::cell(g.name, g.auto_disabled ? tr("automático · reativar") : tr("reativar"));
        GameRow copy = g;
        cell->registerClickAction([this, copy](brls::View*) {
            ui::confirm(trf("Reativar o tracking de tempo de %s?\n\nO jogo volta para a tela inicial e "
                            "as próximas sessões passam a contar.",
                            copy.name.c_str()),
                        tr("Reativar"), [this, copy] {
                            if (!ui::report(data::set_tracking(copy.id, true), trf("%s reativado", copy.name.c_str())))
                                return;
                            HomeActivity::request_refresh();
                            rebuild();
                        });
            return true;
        });
        m_list->addView(cell);
    }
}

// --- Fantasmas --------------------------------------------------------------------
GhostsTab::GhostsTab() {
    auto card = add_card(tr("Sessões não encerradas"), true);
    auto info = tico::card_text(
        tr("Sessões que não foram encerradas (desligamento, falta de bateria, travamento). "
           "A duração até o último checkpoint já conta no total. Finalize para confirmar "
           "ou exclua para descartar."));
    card->addView(info);
    card->addView(new SessionList(0, [] { HomeActivity::request_refresh(); }));
}

// --- Desinstalados -----------------------------------------------------------------
UninstalledTab::UninstalledTab() {
    auto card = add_card(tr("Jogos desinstalados"), true);
    auto info = tico::card_text(
        tr("Jogos com tempo registrado que não estão mais instalados. Um forwarder fantasma "
           "coloca o jogo de volta na Home: ao abrir, ele mostra no Ruby Achievements que o jogo está "
           "desinstalado e o tempo total jogado."));
    card->addView(info);

    m_summary = tico::cell(tr("Desinstalados"));
    card->addView(m_summary);

    auto scroll = tico::scroller();
    scroll->setGrow(1);
    m_list = new brls::Box(brls::Axis::COLUMN);
    tico::set_scroll_content(scroll, m_list);
    card->addView(scroll);
    rebuild();
}

void UninstalledTab::rebuild() {
    ui::rescue_focus(m_list, m_summary);
    m_list->clearViews();
    std::vector<GameRow> games;
    if (!data::uninstalled_games(games)) {
        m_summary->setDetailText(data::last_error());
        return;
    }
    m_summary->setDetailText(games.size() == 1 ? std::string(tr("1 jogo")) : trf("%d jogos", (int)games.size()));
    for (auto& g : games) {
        auto cell = tico::cell(g.name + "  ·  " + data::format_duration(g.total));
        std::string ghost_dir = std::string(PT_GHOSTS_DIR) + "/" + g.title_id + "/forwarder.txt";
        FILE* f = fopen(ghost_dir.c_str(), "rb");
        if (data::ghost_installed(g.title_id)) cell->setDetailText(tr("forwarder fantasma instalado"));
        else cell->setDetailText(f ? std::string(tr("pacote do fantasma criado")) : g.title_id);
        if (f) fclose(f);
        GameRow copy = g;
        cell->registerClickAction([this, copy](brls::View*) {
            open_actions(copy);
            return true;
        });
        m_list->addView(cell);
    }
}

void UninstalledTab::open_actions(const GameRow& g) {
    ui::pick(g.name, {tr("Criar forwarder fantasma"), tr("Ver a tela do forwarder fantasma"), tr("Abrir o jogo na biblioteca")},
             [this, g](int i) {
                 if (i == 0) create_ghost(g);
                 else if (i == 1) brls::Application::pushActivity(new GhostActivity(g.title_id));
                 else if (i == 2) brls::Application::pushActivity(new GameActivity(g.id));
             });
}

namespace {
// Ícone convertido (JPEG 256x256) que o instalador e o pacote leem logo em seguida.
constexpr const char* kGhostIcon = PT_DIR "/ghost_icon.jpg";

std::string file_name(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}
}  // namespace

void UninstalledTab::create_ghost(const GameRow& g) {
    // 1) nome (padrão: o do jogo), 2) ícone, 3) instalar agora ou só o pacote.
    brls::Application::getImeManager()->openForText(
        [this, g](std::string name) {
            brls::sync([this, g, name] { pick_ghost_icon(g, name.empty() ? g.name : name); });
        },
        tr("Nome do forwarder fantasma"), tr("Como vai aparecer na Home"), 64, g.name);
}

void UninstalledTab::pick_ghost_icon(const GameRow& g, const std::string& name) {
    auto paths = std::make_shared<std::vector<std::string>>();
    std::vector<std::string> labels;
    std::string sys = data::system_icon(g.title_id);
    if (!sys.empty()) {
        paths->push_back(sys);
        labels.push_back(tr("Ícone original do jogo"));
    }
    if (!g.cover.empty() && g.cover != sys) {
        paths->push_back(g.cover);
        labels.push_back(tr("Capa atual do jogo"));
    }
    paths->push_back("");  // "" = ícone personalizado
    labels.push_back(tr("Ícone personalizado"));
    ui::pick(trf("Ícone de %s", name.c_str()), labels, [this, g, name, paths](int i) {
        const std::string& path = (*paths)[i];
        if (path.empty()) pick_custom_icon(g, name);
        else finish_ghost(g, name, path);
    });
}

void UninstalledTab::pick_custom_icon(const GameRow& g, const std::string& name) {
    ui::pick(tr("Ícone personalizado"), {tr("Arquivo local (.png/.jpg)"), tr("Baixar do SteamGridDB")},
             [this, g, name](int i) {
        if (i == 1) {
            // Arquivo próprio (<TID>-icone): não troca a capa do jogo.
            brls::Application::pushActivity(new CoverSearchActivity(
                g, tr("Ícone do SteamGridDB"), g.title_id + "-icone",
                [this, g, name](const std::string& path) { finish_ghost(g, name, path); }));
            return;
        }
        auto files = std::make_shared<std::vector<std::string>>(data::cover_files());
        if (files->empty()) {
            brls::Application::notify(tr("Copie imagens .png/.jpg para /switch/ruby_achievements/covers para escolher um ícone."));
            return;
        }
        std::vector<std::string> labels;
        for (auto& f : *files) labels.push_back(file_name(f));
        ui::pick(tr("Ícone personalizado"), labels,
                 [this, g, name, files](int k) { finish_ghost(g, name, (*files)[k]); });
    });
}

void UninstalledTab::finish_ghost(const GameRow& g, const std::string& name, const std::string& source) {
    std::string error;
    if (!icon::make_jpeg(source, kGhostIcon, error)) {
        tico::message(error);
        return;
    }
    std::string icon_path = kGhostIcon;
    ui::pick(name, {tr("Instalar na Home agora"), tr("Só gerar o pacote para montar no PC")},
             [this, g, name, icon_path](int k) {
        if (k == 0) {
            brls::Application::pushActivity(new GhostInstallActivity(g, name, icon_path));
            return;
        }
        std::string dir;
        bool ok = data::create_ghost_package(g, name, icon_path, dir);
        if (!ok && dir.empty()) {
            ui::report(false, "");
            return;
        }
        tico::message(std::string(ok ? tr("Pacote do forwarder fantasma criado em:") : tr("Pacote criado com aviso:")) +
                      "\n" + (ok ? dir : data::last_error()) + "\n\nTitle ID: " + data::ghost_title_id(g.title_id) +
                      "\n" + trf("Alvo: %s %s", PT_APP_NRO, data::ghost_args(g.title_id).c_str()) + "\n\n" +
                      tr("Veja o LEIA-ME.txt da pasta."));
        rebuild();
    });
}

// --- Instalação do forwarder fantasma -------------------------------------------------
GhostInstallActivity::GhostInstallActivity(GameRow game, std::string name, std::string icon)
    : m_game(std::move(game)), m_name(std::move(name)), m_icon(std::move(icon)) {}

brls::View* GhostInstallActivity::createContentView() {
    auto root = new brls::Box(brls::Axis::ROW);
    root->setPadding(40, 50, 40, 50);

    auto img = new brls::Image();
    img->setSize(brls::Size(256, 256));
    img->setScalingType(brls::ImageScalingType::FIT);
    img->setMarginRight(36);
    img->setImageFromFile(m_icon);
    root->addView(img);

    auto col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1);
    auto title = new brls::Label();
    title->setFontSize(28);
    title->setText(m_name);
    col->addView(title);

    auto info = new brls::Label();
    info->setFontSize(16);
    info->setTextColor(tico::kMuted);
    info->setMarginTop(6);
    info->setText(trf("Forwarder fantasma de %s · Title ID %s\nAbre %s %s", m_game.title_id.c_str(),
                      data::ghost_title_id(m_game.title_id).c_str(), PT_APP_NRO,
                      data::ghost_args(m_game.title_id).c_str()));
    col->addView(info);

    auto spinner = new brls::ProgressSpinner();
    spinner->setMarginTop(24);
    m_spinner = spinner;
    col->addView(spinner);

    m_step = new brls::Label();
    m_step->setFontSize(20);
    m_step->setMarginTop(12);
    m_step->setText(tr("Preparando..."));
    col->addView(m_step);

    m_result = new brls::Label();
    m_result->setFontSize(18);
    m_result->setMarginTop(16);
    m_result->setText(" ");
    col->addView(m_result);

    m_actions = new brls::Box(brls::Axis::COLUMN);
    m_actions->setMarginTop(16);
    col->addView(m_actions);
    root->addView(col);

    auto frame = new tico::Frame(root);
    frame->setTitle(tr("Criando forwarder fantasma"));
    return frame;
}

void GhostInstallActivity::onContentAvailable() {
    // B só funciona depois que a instalação terminar.
    registerAction(tr("Voltar"), brls::BUTTON_B, [this](brls::View*) {
        if (!m_running) brls::Application::popActivity();
        return true;
    });
    start("");
}

void GhostInstallActivity::start(const std::string& keys_path) {
    m_running = true;
    m_spinner->setVisibility(brls::Visibility::VISIBLE);
    m_result->setText(" ");
    GameRow game = m_game;
    std::string name = m_name, icon = m_icon;
    brls::async([this, game, name, icon, keys_path] {
        std::string message;
        bool need_keys = false;
        bool ok = data::install_ghost_forwarder(
            game, name, icon, keys_path,
            [this](int step, int total, const char* what) {
                std::string text = std::to_string(step) + "/" + std::to_string(total) + "  " + tr(what);
                brls::sync([this, text] { m_step->setText(text); });
            },
            message, need_keys);
        brls::sync([this, ok, message, need_keys] { finish(ok, message, need_keys); });
    });
}

void GhostInstallActivity::finish(bool ok, const std::string& message, bool need_keys) {
    m_running = false;
    m_spinner->setVisibility(brls::Visibility::GONE);
    m_step->setText(ok ? tr("Concluído") : tr("Falhou"));
    m_result->setTextColor(ok ? nvgRGB(0, 190, 120) : nvgRGB(230, 90, 70));
    m_result->setText(ok ? message + "\n" + tr("Volte à Home para ver o ícone.") : message);

    auto cell = new brls::DetailCell();
    if (!ok && need_keys) {
        cell->setText(tr("Informar o caminho do prod.keys"));
        cell->setDetailText(tr("ex.: /switch/prod.keys"));
        cell->registerClickAction([this](brls::View*) {
            brls::Application::getImeManager()->openForText(
                [this](std::string path) {
                    brls::sync([this, path] {
                        std::string full = path.rfind("sdmc:", 0) == 0 ? path : "sdmc:" + path;
                        brls::Application::giveFocus(m_result);
                        m_actions->clearViews();
                        start(full);
                    });
                },
                tr("Caminho do prod.keys"), tr("Arquivo com header_key"), 256, "/switch/prod.keys");
            return true;
        });
    } else {
        cell->setText(tr("Voltar"));
        cell->registerClickAction([](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }
    m_actions->addView(cell);
    brls::Application::giveFocus(cell);
}

// --- Sobre ---------------------------------------------------------------------------
AboutTab::AboutTab() : tico::Page(true) {
    auto panel = add_card("Ruby Achievements");

    PtStatus st;
    std::string sys = tr("Sysmodule: sem dados (não está rodando?)");
    if (pt_status_read(&st) == 0) {
        sys = trf("Sysmodule: última atualização em %s", data::format_date(st.written_at).c_str());
        if (st.title_id[0]) sys += " · " + trf("rodando %s", st.title_id);
    }

    const std::string lines[] = {
        trf("Ruby Achievements %s — usa a internet só quando você pede: capas do SteamGridDB, "
            "RetroAchievements e a aba Atualização.", PT_VERSION),
        "",
        sys,
        "",
        tr("Capas manuais: copie imagens .jpg/.png para"),
        PT_COVERS_DIR,
        tr("e escolha em - (Editar) > Alterar capa, com o jogo selecionado na tela inicial."),
        "",
        trf("Banco de dados: %s", PT_DB_PATH),
        tr("Tempo do jogo aberto: no overlay (aba do Ruby Achievements no Ultrahand/Tesla)."),
    };
    std::string text;  // um texto só: o respiro do card_text não se repete entre as linhas
    for (const std::string& l : lines) text += (text.empty() ? "" : "\n") + l;
    panel->addView(tico::card_text(text));
}

// --- Chaves (antiga Preferências): importação, SteamGridDB e RetroAchievements ------
KeysTab::KeysTab() : tico::Page(true) {
    auto history = add_card(tr("Histórico do sistema"));
    auto import = tico::cell(tr("Importar tempo do sistema (NX Activity Log)"), tr("Jogos nativos"));
    import->registerClickAction([](brls::View*) {
        ui::confirm(std::string(tr("Importar o histórico que o Switch registrou para os jogos nativos (a mesma fonte do NX "
                                   "Activity Log)?")) +
                        "\n\n" + import_details(),
                    tr("Importar"), ui::run_import);
        return true;
    });
    history->addView(import);

    auto accounts = add_card(tr("Contas"));
    auto key = tico::cell(tr("Chave da API do SteamGridDB"),
                          sgdb::api_key().empty() ? tr("não configurada") : tr("configurada"));
    key->registerClickAction([key](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [key](std::string text) {
                brls::sync([key, text] {
                    bool ok = sgdb::save_api_key(text);
                    brls::Application::notify(ok ? tr("Chave salva") : tr("Chave inválida: use só letras e números."));
                    if (ok) key->setDetailText(tr("configurada"));
                });
            },
            tr("Chave da API do SteamGridDB"), tr("Gerada em steamgriddb.com > Preferences > API"), 63, "");
        return true;
    });
    accounts->addView(key);

    auto ra = tico::cell("RetroAchievements", tr("Usuário e chave da Web API"));
    ra->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new RaSettingsActivity());
        return true;
    });
    accounts->addView(ra);
}

void ui::run_import() {
    data::ImportReport r;
    if (!data::import_system_playtime(r)) {
        ui::report(false, "");
        return;
    }
    HomeActivity::request_refresh();
    tico::message(trf("Importação concluída.\n\n%d sessões importadas, %s no total, de %d perfil(is).\n"
                      "%d sessões já estavam no Ruby Achievements; %d jogo(s) novo(s) na biblioteca; %d não instalado(s) "
                      "(aba Desinstalados).\n(%d jogos consultados. Detalhes: /switch/ruby_achievements/import.log)",
                      (int)r.sessions, data::format_duration(r.seconds).c_str(), (int)r.users, (int)r.skipped,
                      (int)r.new_games, (int)r.uninstalled, (int)r.titles));
}

// --- Primeira abertura: idioma (1/3), formato de data (2/3) e importação (3/3) ---------
// A importação vem por último para já aparecer no idioma e no formato escolhidos. B ou
// toque fora num dos dois primeiros mantém o padrão e segue.
namespace {

void onboarding_import() {
    PtConfig cfg;
    pt_config_load(&cfg);
    // Marca só no último passo (fechar o app antes repete o assistente na próxima
    // abertura) e antes de mostrar: a pergunta aparece uma vez só, qualquer que seja a resposta.
    cfg.import_asked = 1;
    pt_config_save_app(&cfg);
    tico::confirm(std::string(tr("Bem-vindo ao Ruby Achievements!\n\nQuer importar o tempo que o Switch já registrou dos "
                                 "seus jogos (a mesma fonte do NX Activity Log)?")) +
                      "\n\n" + import_details() + "\n\n" +
                      tr("Dá para importar depois na engrenagem do menu + > Chaves."),
                  tr("Importar"), ui::run_import);
}

void onboarding_date() {
    tico::pick(std::string(tr("Formato de data")) + "  (2/3)",
               {trf("%s (padrão)", date_name(i18n::DATE_MDY)), date_name(i18n::DATE_DMY), date_name(i18n::DATE_YMD)},
               [](int i) {
                   saved(prefs::set_date_format(i));
                   onboarding_import();
               },
               prefs::date_format(), onboarding_import);
}

void onboarding_language() {
    tico::pick(std::string(tr("Idioma do sistema")) + "  (1/3)",
               {trf("%s (padrão)", language_name(i18n::LANG_EN)), language_name(i18n::LANG_PT)},
               [](int i) {
                   if (i != prefs::language()) {
                       saved(prefs::set_language(i));
                       ui::reload_interface(-1);  // a tela inicial já no idioma novo
                   }
                   brls::sync(onboarding_date);
               },
               prefs::language(), onboarding_date);
}

}  // namespace

void ui::first_run() {
    PtConfig cfg;
    pt_config_load(&cfg);
    if (!cfg.import_asked) onboarding_language();
}
