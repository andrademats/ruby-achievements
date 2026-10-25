#pragma once
#include <switch.h>
#include <stdint.h>
#include "pt_db.h"

// Operações de banco usadas pelo sysmodule. Todas exigem uma conexão aberta
// com pt_db_open (dentro do lock).

// Resolve o usuário: conta do Switch (cria/atualiza apelido) ou, sem conta,
// o usuário da última sessão; por fim "Compartilhado". `created` indica
// se um usuário novo foi inserido.
int64_t store_resolve_user(sqlite3* db, const AccountUid* uid, int64_t now, bool* created);

// Jogo nativo/forwarder pelo Title ID (lê NACP e ícone quando é novo).
int64_t store_game_for_title(sqlite3* db, u64 tid, int64_t now);

// Jogo de emulador pelo caminho da ROM (unificação automática só por rom_path idêntico).
int64_t store_game_for_rom(sqlite3* db, const char* rom_path, const char* label, int64_t now);

// Forwarder em execução carregou `rom_path`. Se o forwarder ainda não tem
// rom_path, adota este e unifica um jogo EMULATOR_ROM de mesmo caminho.
// Retorna false se o forwarder já pertence a outra ROM.
bool store_forwarder_learn_rom(sqlite3* db, int64_t game_id, const char* rom_path, int64_t now);

int64_t store_insert_session(sqlite3* db, int64_t game_id, int64_t user_id, int64_t start, int64_t now);
// end_time = 0 mantém a sessão aberta (NULL).
int store_update_session(sqlite3* db, int64_t session_id, int64_t user_id, int64_t duration, int64_t end_time);

int store_delete_session(sqlite3* db, int64_t session_id);
// Passa a sessão para outro jogo (a ROM identificada depois). 0 = ok.
int store_move_session(sqlite3* db, int64_t session_id, int64_t game_id);

// game_id atual da sessão (o app pode ter unificado o jogo). 0 se não existe.
int64_t store_session_game(sqlite3* db, int64_t session_id);

// Soma das sessões do jogo para o perfil (0 = todos), excluindo uma sessão.
int64_t store_profile_total(sqlite3* db, int64_t game_id, int64_t profile, int64_t exclude_session);

int64_t store_last_played_game(sqlite3* db);
// Jogo do Title ID já registrado (segue unificações), sem criar. 0 = nenhum.
int64_t store_find_title(sqlite3* db, u64 tid);
void store_game_name(sqlite3* db, int64_t game_id, char* out, size_t cap);

// Grava o build id do título (tabela title_builds) se mudou. 0 = ok.
int store_title_build(sqlite3* db, u64 tid, const char* build_id, int64_t now);

// Exporta "id|apelido" de todos os usuários para o overlay.
int store_export_users(sqlite3* db);
