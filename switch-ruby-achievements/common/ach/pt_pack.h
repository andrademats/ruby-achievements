#pragma once
// Pacotes de conquistas no SD (PT_ACH_DIR): quais arquivos existem para um jogo, qual
// está ativo e os metadados das conquistas dele no banco. Usado pelo sysmodule (que
// avalia o pacote ativo) e pelo app (galeria sem abrir o jogo e troca de pacote).
//
// Formato .rubydb (SQLite, esquema em rubydb_schema.sql): um arquivo por pacote com as
// conquistas, os ícones e o som do pop-up em BLOB. Gravado por tools/json_to_rubydb.py e
// pelo AchieveSet Studio. Arquivos de uma chave (Title ID em hex maiúsculo, ou nome da ROM
// sem extensão):
//   <chave>.rubydb               o pacote principal
//   <chave>.<variante>.rubydb    outros pacotes do mesmo jogo (só chaves de Title ID:
//                                nomes de ROM podem ter pontos)
// Só um é lido por vez: o escolhido no app (PT_ACH_ACTIVE_PATH, linhas
// "<chave>|<arquivo>") se ainda existir; senão o primeiro de pt_pack_files.
#include <stdint.h>

#include <string>
#include <vector>

#include "mjson.h"
#include "sqlite3.h"

// Texto das conquistas (título, descrição e condição) de um pacote: acima
// disso o pacote é recusado (a árvore da condição ocupa o heap de 1,5 MB do sysmodule).
constexpr size_t kPtPackMaxText = 64 * 1024;
constexpr size_t kPtPackMaxIcon = 256 * 1024;  // PNG de um ícone
constexpr const char* kPtPackExt = ".rubydb";

// Chave de Title ID: 16 dígitos hexadecimais.
bool pt_pack_is_title_key(const std::string& key);

// Arquivos de pacote da chave (nomes dentro de PT_ACH_DIR): "<chave>.rubydb" primeiro,
// depois as variantes em ordem alfabética.
std::vector<std::string> pt_pack_files(const std::string& key);

// Arquivo ativo da chave; "" = nenhum pacote no SD.
std::string pt_pack_active(const std::string& key);

// Variante do nome do arquivo ("" = o principal "<chave>.rubydb").
std::string pt_pack_variant(const std::string& key, const std::string& file);

// Lê um pacote de PT_ACH_DIR e monta a árvore que o sysmodule avalia:
//   {"pack_name": título, "title_id": ..., "build_id": "<id>,<id>", "achievements": [
//     {"id", "title", "description", "rarity", "secret", "icon": referência
//      (pt_pack_icon_ref) se houver ícone, ...campos da condição (type e parâmetros)}]}
// em sort_order. false = sem o arquivo, banco inválido, formato desconhecido ou texto
// acima de kPtPackMaxText (motivo em `err`).
bool pt_pack_load(const std::string& file, JVal& root, std::string* err = nullptr);

// Raridade normalizada de uma conquista: "bronze" (padrão e valores desconhecidos),
// "silver", "gold" ou "ruby".
const char* pt_pack_rarity(const JVal& a);

// Ícone de uma conquista dentro do pacote, guardado em achievements.icon_local_path:
// "rubydb:<arquivo>#<id>". pt_pack_parse_icon_ref devolve false para caminhos comuns
// (ícones do RetroAchievements, arquivos no SD).
std::string pt_pack_icon_ref(const std::string& file, const std::string& aid);
bool pt_pack_parse_icon_ref(const std::string& ref, std::string& file, std::string& aid);

// PNG do ícone (`locked` = icon_locked) em `png`. false = sem esse ícone.
bool pt_pack_read_icon(const std::string& file, const std::string& aid, bool locked, std::string& png);
// Copia o ícone desbloqueado para `out_path` em pedaços, sem carregar o BLOB inteiro
// (sysmodule). false = sem o ícone ou erro de E/S (nada fica em `out_path`).
bool pt_pack_extract_icon(const std::string& file, const std::string& aid, const std::string& out_path);

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
