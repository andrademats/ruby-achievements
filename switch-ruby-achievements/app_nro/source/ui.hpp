#pragma once
#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "community.hpp"
#include "data.hpp"
#include "i18n.hpp"
#include "sort.hpp"
#include "tico.hpp"

// --- Utilitários de UI -------------------------------------------------------
// Toda mudança de dados reconstrói listas no próximo frame (brls::sync), nunca
// dentro do callback: o Borealis devolve o foco à view que abriu o
// Dropdown/Dialog, e ela não pode ter sido destruída.
namespace ui {
void pick(const std::string& title, const std::vector<std::string>& options, std::function<void(int)> cb,
          int selected = 0);
void confirm(const std::string& text, const std::string& action, std::function<void()> cb);
// Mostra o erro de data:: ou a mensagem de sucesso.
bool report(bool ok, const std::string& success);
// Escolhe um perfil (lista de users) e devolve o id.
void pick_user(const std::string& title, std::function<void(int64_t)> cb);
// Escolhe outro jogo (exceto `exclude`) e devolve o id e o nome.
void pick_game(const std::string& title, int64_t exclude, std::function<void(int64_t, std::string)> cb);
// Perfil escolhido na Biblioteca (users.id; 0 = todos os perfis). Vale para o
// app inteiro: biblioteca, tela do jogo e listas de sessões.
int64_t profile_filter();
void set_profile_filter(int64_t user_id);
std::string profile_name();  // apelido do perfil filtrado; "" = todos
// Se o foco está dentro de `container`, move-o para `fallback`.
void rescue_focus(brls::View* container, brls::View* fallback);
// Importa o NX Activity Log (data::import_system_playtime) e mostra o resultado.
void run_import();
// Primeira abertura do app (config.ini import_asked=0): assistente com o idioma, o
// formato de data e, por último, a pergunta de importar o histórico do sistema.
void first_run();

// Ações de um jogo (tela do jogo e menus + / - da tela inicial). `changed` roda
// depois de uma alteração; em merge_game, quando o jogo deixou de existir.
void edit_game_name(const GameRow& g, std::function<void()> changed);
void change_game_cover(const GameRow& g, std::function<void()> changed);  // arquivo local ou SteamGridDB
void merge_game(const GameRow& g, std::function<void()> gone);
void reassign_game(const GameRow& g, std::function<void()> changed);
void open_sessions(const GameRow& g);
// Desativa o tracking de tempo do jogo (com confirmação); `changed` roda depois.
void disable_tracking(const GameRow& g, std::function<void()> changed);
// Ativa outro pacote de conquistas do jogo, depois de confirmar que o progresso do jogo
// (todos os perfis) será apagado (data::set_active_pack); `done` roda depois da troca.
void activate_pack(int64_t game_id, const AchievementPack& pack, std::function<void()> done);
// Célula que abre a CommunityActivity de um jogo do Switch ("Buscar pacotes da comunidade"
// sem pacote no SD; "Gerenciar / baixar outros pacotes" com pacote). nullptr = jogo sem Title ID.
brls::DetailCell* community_cell(int64_t game_id, const std::string& name);
// Troca de tema ou idioma: reaplica as cores, fecha as telas abertas, recria a
// tela inicial no lugar e reabre as Configurações globais na aba `settings_tab`
// (-1 = não reabre). As views copiam cores e textos ao serem criadas.
void reload_interface(int settings_tab);
}  // namespace ui

// --- Telas ---------------------------------------------------------------------
// Tela inicial no estilo do launcher Tico (home.cpp): grade 6x3 paginada de
// capas, avatar do perfil (A/toque troca; fica no config.ini), jogo selecionado
// com o tempo total, hora, bateria e o menu modal do +.
class HomeActivity : public brls::Activity {
public:
    brls::View* createContentView() override;
    void onResume() override;
    static void request_refresh();  // refaz a grade no próximo quadro (se a tela existir)
    // Recria todas as views da tela inicial no lugar (tema/idioma novos). A
    // própria tela segura o foco durante a troca: o Borealis não pode ficar com
    // foco numa view apagada.
    static void reload();
};

// Configurações globais (engrenagem do menu +), nesta ordem: Interface, Conquistas,
// Gerenciamento, Chaves e App. Todas as abas são tico::Page (molde da aba App).
// Desinstalados, Desativados e Sessões fantasmas abrem pela aba Gerenciamento, em tela cheia.
class SettingsActivity : public brls::Activity {
public:
    static constexpr int kInterfaceTab = 0;  // índice da aba Interface
    explicit SettingsActivity(int initial_tab = 0) : m_initial_tab(initial_tab) {}
    brls::View* createContentView() override;

private:
    int m_initial_tab;
};

// Tema (claro/escuro), estilo de fundo, idioma e formato de data (prefs.hpp).
class InterfaceTab : public tico::Page {
public:
    InterfaceTab();
};

// Chaves (antiga Preferências): importação do tempo do sistema, chave do
// SteamGridDB e RetroAchievements.
class KeysTab : public tico::Page {
public:
    KeysTab();
};

// Lista de sessões de um jogo (game_id > 0) ou de todas as fantasmas (0), só do
// perfil filtrado (ui::profile_filter), ou de todos com "Todos os perfis".
class SessionList : public brls::Box {
public:
    SessionList(int64_t game_id, std::function<void()> on_change);
    void rebuild();

private:
    static constexpr int kPageSize = 100;

    void open_actions(const SessionRow& s);
    // Acrescenta a próxima página antes de m_more; retorna a 1ª célula nova.
    brls::View* load_page();

    int64_t m_game_id;
    std::function<void()> m_on_change;
    brls::DetailCell* m_summary = nullptr;  // célula fixa: recebe o foco ao reconstruir
    brls::Box* m_list = nullptr;
    brls::DetailCell* m_more = nullptr;     // "Carregar mais", sempre a última de m_list
    SessionRow m_last;                      // última linha carregada (cursor da página)
    bool m_has_last = false;
};

class GhostsTab : public tico::Page {
public:
    GhostsTab();
};

// Jogos e apps com o tracking de tempo desativado; A reativa.
class DisabledTab : public tico::Page {
public:
    DisabledTab();

private:
    void rebuild();
    brls::DetailCell* m_summary = nullptr;
    brls::Box* m_list = nullptr;
};

// Conquistas por jogo (perfil filtrado em ui::profile_filter).
class AchievementsTab : public tico::Page {
public:
    AchievementsTab();
    ~AchievementsTab() override;
    static void request_refresh();

private:
    void rebuild();
    static AchievementsTab* s_current;
    brls::DetailCell* m_summary = nullptr;
    brls::DetailCell* m_sync = nullptr;
    brls::Box* m_list = nullptr;
};

// Sincroniza com o RetroAchievements mostrando o progresso. A rede roda fora do
// thread da UI; B fica bloqueado até terminar.
class RaSyncActivity : public brls::Activity {
public:
    RaSyncActivity();
    ~RaSyncActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::shared_ptr<bool> m_alive;
    bool m_running = false;
    brls::Label* m_step = nullptr;
    brls::Label* m_result = nullptr;
    brls::View* m_spinner = nullptr;
    brls::Box* m_actions = nullptr;
};

// Conquistas de um jogo: desbloqueadas (com data) e bloqueadas.
class AchievementListActivity : public brls::Activity {
public:
    AchievementListActivity(int64_t game_id, std::string name);
    brls::View* createContentView() override;

private:
    int64_t m_game_id;
    std::string m_name;
};

// Pacotes de conquistas de um jogo do Switch (community_ui.cpp): os do cartão SD (ativar,
// apagar) e os do catálogo da comunidade (community.hpp) com o selo de compatibilidade
// (baixar, atualizar). A rede roda fora do thread da UI; B fica bloqueado durante a tarefa.
class CommunityActivity : public brls::Activity {
public:
    CommunityActivity(int64_t game_id, std::string name);
    ~CommunityActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void load(bool fetch_index);
    void show();
    void local_actions(const AchievementPack& p);
    void entry_actions(size_t index);
    void download(const community::Entry& e);
    void changed();

    std::shared_ptr<bool> m_alive;
    bool m_running = false;
    int64_t m_game_id;
    std::string m_name;
    std::shared_ptr<struct CommunityState> m_state;
    brls::DetailCell* m_summary = nullptr;
    brls::View* m_spinner = nullptr;
    brls::Label* m_status = nullptr;
    brls::Box* m_list = nullptr;
};

// Usuário e chave da Web API do RetroAchievements (config.ini, seção [app]).
class RaSettingsActivity : public brls::Activity {
public:
    brls::View* createContentView() override;

private:
    void refresh();
    brls::DetailCell* m_user = nullptr;
    brls::DetailCell* m_key = nullptr;
};

// Atalhos para Jogos desinstalados, Jogos desativados e Sessões fantasmas.
class ManageTab : public tico::Page {
public:
    ManageTab();
};

// Sobre (versão, sysmodule, caminhos), versão instalada, aviso de reinício pendente e o
// botão que abre a UpdateActivity (update.hpp).
class AppTab : public tico::Page {
public:
    AppTab();
};

// Consulta a última versão no GitHub e, se o usuário confirmar, baixa, confere e instala
// (update.hpp). A rede roda fora do thread da UI; B fica bloqueado durante a tarefa.
class UpdateActivity : public brls::Activity {
public:
    UpdateActivity();
    ~UpdateActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void run_check();
    void run_install();
    void finish(bool ok, const std::string& text);
    void add_action(const std::string& label, std::function<void()> cb);

    std::shared_ptr<bool> m_alive;
    bool m_running = false;
    brls::View* m_spinner = nullptr;
    brls::Label* m_step = nullptr;
    brls::Label* m_result = nullptr;
    brls::Box* m_actions = nullptr;
    std::shared_ptr<struct UpdateState> m_state;
};

// Jogos com tempo registrado que foram desinstalados, com a opção de criar
// um forwarder fantasma.
class UninstalledTab : public tico::Page {
public:
    UninstalledTab();

private:
    void rebuild();
    void open_actions(const GameRow& g);
    void create_ghost(const GameRow& g);
    // Etapas do forwarder fantasma: ícone (original, capa, personalizado do SD ou do
    // SteamGridDB), conversão para JPEG 256x256 e instalar agora ou só o pacote.
    void pick_ghost_icon(const GameRow& g, const std::string& name);
    void pick_custom_icon(const GameRow& g, const std::string& name);
    void finish_ghost(const GameRow& g, const std::string& name, const std::string& source);

    brls::DetailCell* m_summary = nullptr;
    brls::Box* m_list = nullptr;
};

// Monta e instala o forwarder fantasma, mostrando cada etapa. A instalação
// roda fora do thread da UI; B fica bloqueado até ela terminar.
class GhostInstallActivity : public brls::Activity {
public:
    GhostInstallActivity(GameRow game, std::string name, std::string icon);
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void start(const std::string& keys_path);
    void finish(bool ok, const std::string& message, bool need_keys);

    GameRow m_game;
    std::string m_name, m_icon;
    bool m_running = false;
    brls::Label* m_step = nullptr;
    brls::Label* m_result = nullptr;
    brls::Box* m_actions = nullptr;
    brls::View* m_spinner = nullptr;
};

// Tela aberta pelo forwarder fantasma (argumento --ghost <TID>): o jogo está
// desinstalado e o tempo total jogado.
class GhostActivity : public brls::Activity {
public:
    explicit GhostActivity(std::string title_id);
    brls::View* createContentView() override;

private:
    std::string m_title_id;
};

class GameActivity : public brls::Activity {
public:
    explicit GameActivity(int64_t game_id);
    brls::View* createContentView() override;
    void onResume() override;

private:
    void refresh();

    int64_t m_id;
    GameRow m_game;
    tico::Frame* m_frame = nullptr;
    brls::Image* m_cover = nullptr;
    brls::Label* m_total = nullptr;
    brls::Box* m_per_user = nullptr;
};

// Imagens quadradas do SteamGridDB: busca o jogo pelo nome (sugestão = nome atual,
// ou texto digitado no teclado do Switch), mostra os grids quadrados e grava o
// escolhido em PT_COVERS_DIR/<file_base>.png (ou .jpg). A rede roda fora do thread
// da UI. Depois de fechar a tela, `on_saved` recebe o caminho do arquivo (a capa do
// jogo ou o ícone do forwarder fantasma: quem chama decide).
class CoverSearchActivity : public brls::Activity {
public:
    CoverSearchActivity(GameRow game, std::string title, std::string file_base,
                        std::function<void(const std::string& path)> on_saved);
    ~CoverSearchActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    struct Shared;  // estado visto pelas tarefas em segundo plano
    void ask_key(bool then_search);
    void ask_term();
    void search(const std::string& term);
    void choose_game();
    void load_grids(size_t game_index);
    void show_grids();
    void apply(size_t grid_index);
    void set_status(const std::string& text, bool error = false);

    GameRow m_game;
    std::string m_title, m_file_base;
    std::function<void(const std::string&)> m_on_saved;
    std::shared_ptr<Shared> m_shared;
    std::string m_term;
    brls::DetailCell* m_search = nullptr;
    brls::DetailCell* m_match = nullptr;
    brls::Label* m_status = nullptr;
    brls::Box* m_grid = nullptr;
    std::vector<brls::Image*> m_thumbs;
};

// Esquece a textura em cache de `path` (a imagem do arquivo mudou).
namespace ui {
void forget_texture(const std::string& path);
}

class SessionsActivity : public brls::Activity {
public:
    SessionsActivity(int64_t game_id, std::string title);
    brls::View* createContentView() override;

private:
    int64_t m_game_id;
    std::string m_title;
};
