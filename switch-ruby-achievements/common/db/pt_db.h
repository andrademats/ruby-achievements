#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sqlite3.h"

// Abre o banco dentro do lock entre processos e garante o schema (na 1ª abertura
// do processo também roda quick_check). Banco malformado é movido para
// ruby_achievements.db.corrupt e recriado vazio (até 3 vezes por processo).
// Retorna NULL se o lock não vier dentro de timeout_ms ou o banco falhar.
// Toda conexão deve ser curta e fechada com pt_db_close (libera o lock).
sqlite3* pt_db_open(int timeout_ms);
void pt_db_close(sqlite3* db);

// Motivo da última falha de pt_db_open (etapa, mensagem, código SQLite e
// resultado do libnx); "" após sucesso.
const char* pt_db_last_error(void);

// Destino do log de diagnóstico (abertura passo a passo, I/O da VFS, mensagens
// internas do SQLite e recuperação). Chamar antes de qualquer uso do SQLite.
void pt_db_set_log(void (*fn)(const char* line));

// Gera um identificador aleatório de 32 caracteres hex (coluna sync_id).
void pt_make_sync_id(char out[33]);

// Executa SQL com até dois parâmetros inteiros e retorna a 1ª coluna da 1ª linha.
int64_t pt_db_scalar(sqlite3* db, const char* sql, int64_t a, int64_t b, int64_t def);

// Garante o usuário "Compartilhado" (is_shared = 1) e retorna o id.
int64_t pt_db_shared_user(sqlite3* db, int64_t now);

// Segue a cadeia status = 'MERGED:<id>' até o jogo final.
int64_t pt_db_resolve_game(sqlite3* db, int64_t game_id);

// Recalcula o cache total_playtime/first_played/last_played a partir das sessões.
int pt_db_recalc_game(sqlite3* db, int64_t game_id, int64_t now);

// Fecha sessões abertas (end_time NULL) mantendo a duração do último checkpoint:
// end_time = start_time + duration. session_id > 0 fecha só essa; 0 fecha todas
// menos `keep` (a sessão viva). Duração e last_played não mudam, então não há
// recálculo. Retorna quantas fechou ou -1.
int pt_db_close_open_sessions(sqlite3* db, int64_t session_id, int64_t keep);

// Unifica `from` em `into`: move as sessões, marca `from` como MERGED:<into>
// e recalcula os dois. Retorna 0 em sucesso.
int pt_db_merge_games(sqlite3* db, int64_t from, int64_t into, int64_t now);

// Usuário de uma conta do Switch (account_uid em hex): acha, atualiza o apelido
// ou cria. `changed` = inserido ou renomeado (a lista do overlay deve ser refeita).
int64_t pt_db_account_user(sqlite3* db, const char* uid_str, const char* nickname, int64_t now, bool* changed);

// Leitor de NACP: nome e caminho do ícone salvo. Retorna false se não leu.
typedef bool (*PtTitleReader)(uint64_t tid, char* name, size_t name_cap, char* icon_path, size_t icon_cap);

// Nome de jogo ilegível (UTF-8 inválido, controle, bytes soltos lidos como
// texto) ou igual ao Title ID `tid_str` (pode ser NULL).
bool pt_db_name_suspect(const char* name, const char* tid_str);

// Jogo nativo/forwarder de um Title ID: acha (seguindo MERGED) ou cria com o
// nome/ícone do NACP (`read`; nome = Title ID se falhar). Um jogo existente com
// nome suspeito (pt_db_name_suspect) tem o nome relido do NACP. 0 = erro.
int64_t pt_db_title_game(sqlite3* db, uint64_t tid, int64_t now, PtTitleReader read);

// --- Tracking por jogo (tabela tracking_disabled) ---
// Homebrew utilitário que nunca conta tempo, pelo nome do NACP: Sphaira e o
// próprio Ruby Achievements.
bool pt_db_is_utility_name(const char* name);
// 1 = o tempo deste jogo (já resolvido por MERGED) não é contado.
bool pt_db_tracking_disabled(sqlite3* db, int64_t game_id);
// Desativa (reason 'user' ou 'auto') ou reativa o tracking. 0 = ok.
int pt_db_set_tracking(sqlite3* db, int64_t game_id, bool enabled, const char* reason, int64_t now);
// Filtro automático: utilitários ainda sem linha em tracking_disabled são
// desativados ('auto') e as sessões deles apagadas. Retorna quantos ou -1.
int pt_db_apply_utility_filter(sqlite3* db, int64_t now);
