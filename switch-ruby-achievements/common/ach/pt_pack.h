#pragma once
// Pacotes de conquistas no SD (PT_ACH_DIR): quais arquivos existem para um jogo, qual
// está ativo e os metadados das conquistas dele no banco. Usado pelo sysmodule (que
// avalia o pacote ativo) e pelo app (galeria sem abrir o jogo e troca de pacote).
//
// Arquivos de uma chave (Title ID em hex maiúsculo, ou nome da ROM sem extensão):
//   <chave>.json                 o pacote principal
//   <chave>.<variante>.json      outros pacotes do mesmo jogo (só chaves de Title ID:
//                                nomes de ROM podem ter pontos)
// Só um é lido por vez: o escolhido no app (PT_ACH_ACTIVE_PATH, linhas
// "<chave>|<arquivo>") se ainda existir; senão o primeiro de pt_pack_files.
#include <stdint.h>

#include <string>
#include <vector>

#include "mjson.h"
#include "sqlite3.h"

constexpr size_t kPtPackMaxFile = 64 * 1024;  // o resto do arquivo é ignorado

// Chave de Title ID: 16 dígitos hexadecimais.
bool pt_pack_is_title_key(const std::string& key);

// Arquivos de pacote da chave (nomes dentro de PT_ACH_DIR): "<chave>.json" primeiro,
// depois as variantes em ordem alfabética.
std::vector<std::string> pt_pack_files(const std::string& key);

// Arquivo ativo da chave; "" = nenhum pacote no SD.
std::string pt_pack_active(const std::string& key);

// Variante do nome do arquivo ("" = o principal "<chave>.json").
std::string pt_pack_variant(const std::string& key, const std::string& file);

// Lê e interpreta um arquivo de PT_ACH_DIR (até kPtPackMaxFile). false = sem o
// arquivo ou JSON inválido/sem "achievements" (motivo em `err`).
bool pt_pack_load(const std::string& file, JVal& root, std::string* err = nullptr);

// Raridade normalizada de uma conquista: "bronze" (padrão e valores desconhecidos),
// "silver", "gold" ou "ruby".
const char* pt_pack_rarity(const JVal& a);

// Grava no banco os metadados (título, descrição, secreta, ícone, raridade, ordem)
// das conquistas válidas do pacote, com id "L:<chave>:<id>". Rubys depois da primeira
// ficam de fora. Retorna quantas gravou.
int pt_pack_sync_meta(sqlite3* db, const std::string& key, int64_t game_id, const JVal& root);

// O banco tem metadados de alguma conquista da chave? (falso depois de pt_pack_switch)
bool pt_pack_has_meta(sqlite3* db, const std::string& key);

// Troca o pacote ativo da chave para `file`: apaga numa transação própria todo o
// progresso (desbloqueios de todos os perfis, fila de pop-ups) e os metadados das
// conquistas "L:<chave>:" e depois grava a escolha. O pacote antigo fica no SD; ao
// voltar a ele, recomeça do zero. Chamar com o banco aberto (lock) e fora de transação.
bool pt_pack_switch(sqlite3* db, const std::string& key, const std::string& file, std::string* err = nullptr);

// Chaves de Title ID com algum pacote no SD, sem repetir, em ordem.
std::vector<std::string> pt_pack_title_keys();
