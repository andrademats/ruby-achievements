#pragma once
// Espelho do RetroAchievements: para cada ROM da biblioteca, acha o jogo pelo
// hash da ROM e copia para a galeria as conquistas e o que a conta já
// desbloqueou (nos emuladores com login do RetroAchievements). Só leitura: nada
// é enviado ao RetroAchievements. Bloqueante: rodar com brls::async.
#include <functional>
#include <string>

namespace ra {

struct SyncReport {
    int roms = 0;           // ROMs na biblioteca
    int matched = 0;        // reconhecidas pelo RetroAchievements
    int unknown = 0;        // hash sem jogo no RetroAchievements
    int unsupported = 0;    // formato sem hash suportado (disco, .zip, ...)
    int missing = 0;        // arquivo não encontrado no SD
    int achievements = 0;   // conquistas gravadas
    int new_unlocks = 0;    // desbloqueios novos na galeria
    std::string error;      // falha que interrompeu a sincronização
};

using Progress = std::function<void(int done, int total, const std::string& what)>;

// false = credenciais ausentes/inválidas ou sem conexão (report.error).
bool sync(const Progress& progress, SyncReport& report);

// Hash do RetroAchievements para cartuchos (MD5 do conteúdo, sem cabeçalhos de
// cópia; N64 normalizado para big-endian). false = formato não suportado.
bool rom_hash(const std::string& path, std::string& md5, std::string& why);

}  // namespace ra
