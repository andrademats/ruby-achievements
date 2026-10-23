#pragma once
// Ordem da grade da tela inicial (botão Y). Sem Borealis: testável no PC.
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "data.hpp"

namespace ui {

enum SortMode {
    SORT_RECENT = 0,        // jogados recentemente (last_played)
    SORT_AZ = 1,            // A-Z
    SORT_TIME = 2,          // mais tempo de jogo
    SORT_ACHIEVEMENTS = 3,  // mais % de conquistas desbloqueadas
    SORT_COUNT = 4,
};

// Modo gravado em config.ini ([app] app_sort); padrão SORT_RECENT.
int sort_mode();
void set_sort_mode(int mode);

// Ordena `games` pelo modo. Empate: A-Z. Jogo sem dado para o modo (nunca
// jogado, 0 s, sem conquistas cadastradas) vai depois de todos os que têm,
// em A-Z. `ach`: game_id -> (desbloqueadas, total).
void sort_games(std::vector<GameRow>& games, const std::map<int64_t, std::pair<int, int>>& ach, int mode);

// Chave de comparação A-Z: minúsculas e sem acento (á -> a, ç -> c).
std::string sort_key(const std::string& name);

}  // namespace ui
