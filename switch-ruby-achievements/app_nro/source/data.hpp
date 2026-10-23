#pragma once
// Camada de dados do app: cada operação abre o banco (dentro do lock
// compartilhado com o sysmodule), executa e fecha. Nada fica aberto entre
// telas, para o sysmodule continuar gravando sessões enquanto o app roda.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct UserRow {
    int64_t id = 0;
    std::string nickname;
    bool shared = false;
    std::string uid;  // conta do Switch em texto (pt_uid_to_string); "" no perfil compartilhado
};

struct GameRow {
    int64_t id = 0;
    std::string title_id;
    std::string name;
    std::string rom_path;
    std::string source;  // NATIVE / FORWARDER / EMULATOR_ROM
    std::string cover;
    int64_t total = 0;   // segundos (filtrado por perfil quando aplicável)
    int64_t last_played = 0;
    int64_t session_count = 0;
    bool auto_disabled = false;  // em disabled_games: desativado pelo filtro de utilitários
};

struct SessionRow {
    int64_t id = 0;
    int64_t game_id = 0;
    int64_t user_id = 0;
    std::string game_name;
    std::string user_name;
    int64_t start = 0;
    int64_t end = 0;  // 0 = aberta
    int64_t duration = 0;
    bool live = false;   // sessão em andamento no sysmodule
    bool ghost = false;  // aberta e não é a sessão viva (interrompida)
    bool residual = false;  // importada: diferença até o total oficial do sistema
};

// Conquistas de um jogo, somadas para o perfil filtrado (0 = todos os perfis:
// conta como desbloqueada se algum perfil desbloqueou).
struct AchievementGameRow {
    int64_t game_id = 0;
    std::string name;
    std::string cover;
    int total = 0, unlocked = 0;
    int64_t last_unlock = 0;
};

struct AchievementRow {
    std::string id;
    std::string title;
    std::string description;
    std::string icon;  // caminho local; "" = sem ícone
    bool secret = false;
    int64_t unlocked_at = 0;  // 0 = bloqueada
    std::string rarity = "bronze";  // bronze, silver, gold ou ruby (RetroAchievements: bronze)
};

// Pacote de conquistas de um jogo no SD (ver common/ach/pt_pack.h).
struct AchievementPack {
    std::string file;   // nome em PT_ACH_DIR
    std::string label;  // "pack_name" do JSON, a variante do nome do arquivo ou "principal"
    int count = 0;      // conquistas no arquivo
    bool active = false;
};

// Conquista lida do RetroAchievements (espelho do progresso da conta).
struct RaAchievement {
    int64_t id = 0;
    std::string title, description;
    std::string icon_url, icon_path;  // icon_path = "" se o ícone não foi baixado
    int order = 0;
    int64_t unlocked_at = 0;  // 0 = bloqueada na conta
};

namespace data {

const std::string& last_error();

bool users(std::vector<UserRow>& out);
// user_filter = 0: todos os perfis; senão só jogos com sessões do perfil.
bool games(int64_t user_filter, std::vector<GameRow>& out);
// user_filter = 0: todos os perfis; senão total e sessões só desse perfil.
bool game(int64_t id, int64_t user_filter, GameRow& out);
bool game_user_totals(int64_t game_id, std::vector<std::pair<std::string, int64_t>>& out);
// Tempo total registrado (todas as sessões, de todos os jogos e apps) do perfil;
// user_filter = 0: de todos os perfis.
bool profile_total(int64_t user_filter, int64_t& seconds);
// Uma página de sessões, das mais recentes para as mais antigas.
// game_id = 0 => todas as sessões fantasmas. user_filter = 0: todos os perfis;
// senão só as sessões desse perfil (users.id). after = última linha da página
// anterior (nullptr = primeira página); a ordem (início, id) não repete nem pula
// linhas se o sysmodule gravar uma sessão nova entre as páginas.
bool sessions(int64_t game_id, int64_t user_filter, const SessionRow* after, int limit,
              std::vector<SessionRow>& out);
// Quantidade e soma das durações de todas as sessões da mesma consulta.
struct SessionSummary {
    int64_t count = 0;
    int64_t total = 0;
};
bool session_summary(int64_t game_id, int64_t user_filter, SessionSummary& out);

bool rename_game(int64_t id, const std::string& name);
bool set_cover(int64_t id, const std::string& path);  // "" remove a capa
bool merge_games(int64_t from, int64_t into);
bool reassign_game_user(int64_t game_id, int64_t user_id);
bool reassign_session_user(int64_t session_id, int64_t user_id);
bool move_session(int64_t session_id, int64_t game_id);
bool delete_session(int64_t session_id);
bool finalize_ghost(int64_t session_id);

// Tracking de tempo por jogo (tabela tracking_disabled). Desativado: o
// sysmodule não grava sessões e o jogo sai da tela inicial (games()).
bool set_tracking(int64_t game_id, bool enabled);
// Jogos com o tracking desativado, mais recentes primeiro.
bool disabled_games(std::vector<GameRow>& out);

// Histórico que o próprio Switch registrou (serviço pdm, a fonte do NX Activity
// Log) para os jogos nativos: uma sessão importada por sessão do histórico, com
// totais ajustados ao total oficial por jogo e perfil. Sessões que coincidem com
// sessões do Ruby Achievements ficam de fora; uma nova importação refaz só as importadas.
struct ImportReport {
    int titles = 0;     // títulos nativos consultados
    int users = 0;      // perfis com tempo registrado
    int sessions = 0;   // sessões importadas gravadas
    int skipped = 0;    // sessões do histórico que já estavam no Ruby Achievements
    int new_games = 0;  // jogos novos na biblioteca
    int uninstalled = 0;  // títulos do histórico que não estão instalados
    int64_t seconds = 0;
};
bool import_system_playtime(ImportReport& out);

// Jogos do Switch com tempo registrado que não estão mais instalados.
bool uninstalled_games(std::vector<GameRow>& out);
// Jogo pelo Title ID (segue unificações). Usado pela tela do forwarder fantasma.
bool game_by_title(const std::string& title_id, GameRow& out);
// Title ID do forwarder fantasma (fórmula do Sphaira, faixa 0x05...).
std::string ghost_title_id(const std::string& title_id);
// Argumentos do forwarder fantasma: "--ghost <TID>".
std::string ghost_args(const std::string& title_id);
// true se o forwarder fantasma deste jogo já foi instalado pelo Ruby Achievements.
bool ghost_installed(const std::string& title_id);
// Monta e instala o forwarder fantasma no console (código do Sphaira, ver
// source/forwarder). `icon` = JPEG 256x256. Chamado fora do thread da UI.
// need_keys = true quando falta a chave de cabeçalho (pedir o prod.keys).
using GhostProgress = std::function<void(int step, int total, const char* what)>;
bool install_ghost_forwarder(const GameRow& g, const std::string& name, const std::string& icon,
                             const std::string& keys_path, const GhostProgress& progress, std::string& message,
                             bool& need_keys);
// Grava em PT_GHOSTS_DIR/<TID>/ o pacote do forwarder fantasma: icon.jpg,
// forwarder.txt (nome, Title ID, NRO e argumentos) e LEIA-ME.txt.
bool create_ghost_package(const GameRow& g, const std::string& name, const std::string& icon, std::string& dir_out);

// Jogos com conquistas, os desbloqueados mais recentemente primeiro.
bool achievement_games(int64_t user_filter, std::vector<AchievementGameRow>& out);
// Conquistas de um jogo: desbloqueadas primeiro (mais recentes), depois as
// bloqueadas na ordem da definição.
bool achievements(int64_t game_id, int64_t user_filter, std::vector<AchievementRow>& out);

// Pacotes do SD para os jogos nativos: liga cada <TID>.json (ou variante) ao jogo do
// Title ID e grava os metadados do pacote ativo, sem precisar abrir o jogo. Jogo que
// ainda não está no banco entra se estiver instalado (nome e ícone do NACP).
bool sync_achievement_packs();
// Pacotes no SD do jogo (só jogos nativos, pela chave do Title ID).
bool achievement_packs(int64_t game_id, std::vector<AchievementPack>& out);
// Ativa outro pacote: apaga todo o progresso de conquistas do jogo (todos os perfis)
// e carrega as conquistas do pacote novo, do zero. O antigo continua no SD.
bool set_active_pack(int64_t game_id, const std::string& file);

// Cache de MD5 das ROMs (tabela rom_hash_cache), válido para o mesmo tamanho e data.
bool rom_hash_lookup(const std::string& path, int64_t size, int64_t mtime, std::string& md5);
bool rom_hash_store(const std::string& path, int64_t size, int64_t mtime, const std::string& md5);
// Perfil com mais tempo no jogo (o RetroAchievements não conhece os perfis do
// Switch); sem sessões, o perfil "Compartilhado".
int64_t main_user_for_game(int64_t game_id);
// Grava as conquistas do RetroAchievements do jogo e os desbloqueios da conta
// para `user_id`. new_unlocks = desbloqueios que ainda não estavam no banco.
bool store_ra_achievements(int64_t game_id, int64_t user_id, const std::vector<RaAchievement>& list,
                           int& new_unlocks);

// Imagens .jpg/.png em PT_COVERS_DIR (capas manuais).
std::vector<std::string> cover_files();
// Ícone extraído do NACP, se existir.
std::string system_icon(const std::string& title_id);

std::string format_duration(int64_t seconds);
std::string format_date(int64_t epoch);
std::string format_clock(int64_t epoch);  // "14:05" na hora local do console
// Avatar da conta do Switch (JPEG 256x256) pelo uid de UserRow. false = sem conta/imagem.
bool user_avatar(const std::string& uid, std::vector<unsigned char>& jpeg);
std::string source_label(const std::string& source);

}  // namespace data
