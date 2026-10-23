// Ruby Achievements - app NRO (Borealis)
// Tela inicial no estilo do Tico (capas paginadas, tempo por perfil), edição de
// nome/capa, unificação manual de jogos, reassociação de perfil, conquistas e
// revisão de sessões fantasmas.
#include <borealis.hpp>

#include <cstring>
#include <string>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "prefs.hpp"
#include "pt_config.h"
#include "pt_util.h"
#include "ui.hpp"

// Forwarder fantasma: "--ghost <TID>" (ou "--ghost=<TID>") abre a tela do jogo desinstalado.
static std::string ghost_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--ghost=", 8)) return argv[i] + 8;
        if (!strcmp(argv[i], "--ghost") && i + 1 < argc) return argv[i + 1];
    }
    return "";
}

// No Switch o Borealis usa a fonte chinesa do sistema como principal. Ela tem
// á é í ó ú (do pinyin) com largura de ideograma ("vis í vel"), enquanto ç, ã e õ
// vinham da Standard. Aqui a Standard passa a ser a principal (mesmo nome, que é
// o que Label usa) e as demais viram reserva. Precisa rodar antes da 1ª Label.
static void use_standard_font_first() {
#ifdef __SWITCH__
    PlFontData font;
    int old_main = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    if (old_main == brls::FONT_INVALID || R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) return;
    if (!brls::Application::loadFontFromMemory(brls::FONT_CHINESE_SIMPLIFIED, font.address, font.size, false)) return;
    NVGcontext* vg = brls::Application::getNVGContext();
    int main_font = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
    nvgAddFallbackFontId(vg, main_font, old_main);
    for (const std::string& name : {brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL,
                                    brls::FONT_KOREAN_REGULAR, brls::FONT_SWITCH_ICONS, brls::FONT_MATERIAL_ICONS,
                                    brls::FONT_EMOJI}) {
        int id = brls::Application::getFont(name);
        if (id != brls::FONT_INVALID) nvgAddFallbackFontId(vg, main_font, id);
    }
#endif
}

int main(int argc, char* argv[]) {
    pt_ensure_dirs();
    pt_config_migrate_secrets();  // chaves de API saem do config.ini antigo (pt_config.h)
    std::string ghost = ghost_arg(argc, argv);

    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    if (!brls::Application::init()) return EXIT_FAILURE;

    brls::Application::createWindow("Ruby Achievements");
    use_standard_font_first();
    prefs::load();        // tema, fundo, idioma e formato de data (aba Interface)
    tico::apply_theme();  // cores do tema e destaque prateado em todas as telas
    // Variante clara do Borealis sempre (o tema escuro do app troca os valores
    // dela em apply_theme), qualquer que seja o tema do console. O + abre o menu
    // da tela inicial (não sai do app); sair é pelo HOME.
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
    brls::Application::setGlobalQuit(false);
    brls::Application::pushActivity(new HomeActivity());
    if (!ghost.empty()) brls::Application::pushActivity(new GhostActivity(ghost));  // voltar leva à biblioteca
    else brls::sync(ui::first_run);  // assistente da 1ª abertura, depois do primeiro quadro da biblioteca

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
