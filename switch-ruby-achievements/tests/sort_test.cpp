// Ordem da grade (botão Y): critérios, desempate A-Z e jogos sem dados no fim.
#include "sort.hpp"
#include "pt_config.h"
#include <stdio.h>
#include <string>
s64 g_fake_now = 1000;
static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-60s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}
static GameRow G(int64_t id, const char* name, int64_t total, int64_t last) {
    GameRow g;
    g.id = id;
    g.name = name;
    g.total = total;
    g.last_played = last;
    return g;
}
static std::string order(const std::vector<GameRow>& v) {
    std::string s;
    for (auto& g : v) s += std::to_string(g.id) + " ";
    return s;
}
int main() {
    std::vector<GameRow> base = {
        G(1, "Zelda", 5000, 300), G(2, "Ábaco", 5000, 0), G(3, "mario", 0, 0), G(4, "Balatro", 9000, 500),
        G(5, "Celeste", 5000, 300), G(6, "Óculos", 0, 0),
    };
    std::map<int64_t, std::pair<int, int>> ach = {{1, {1, 2}}, {5, {2, 4}}, {4, {9, 10}}, {2, {0, 5}}};

    auto v = base;
    ui::sort_games(v, ach, ui::SORT_AZ);
    check("A-Z sem acento e sem caixa", order(v) == "2 4 5 3 6 1 ");  // abaco balatro celeste mario oculos zelda

    v = base;
    ui::sort_games(v, ach, ui::SORT_RECENT);
    // 4 (500); 1 e 5 empatados em 300 -> Celeste, Zelda; sem data: Ábaco, mario, Óculos.
    check("recentes: empate A-Z e nunca jogados no fim", order(v) == "4 5 1 2 3 6 ");

    v = base;
    ui::sort_games(v, ach, ui::SORT_TIME);
    // 4 (9000); 2, 5, 1 empatados em 5000 -> Ábaco, Celeste, Zelda; 0 s: mario, Óculos.
    check("tempo: empate A-Z e 0 s no fim", order(v) == "4 2 5 1 3 6 ");

    v = base;
    ui::sort_games(v, ach, ui::SORT_ACHIEVEMENTS);
    // 4 (90%); 1 e 5 em 50% -> Celeste, Zelda; 2 (0%, tem conquistas); sem conquistas: mario, Óculos.
    check("conquistas: % , empate A-Z, 0% antes de sem dados", order(v) == "4 5 1 2 3 6 ");

    check("chave sem acento", ui::sort_key("Ação Çé") == "acao ce");

    // Modo gravado no config.ini.
    check("padrão recentes", ui::sort_mode() == ui::SORT_RECENT);
    ui::set_sort_mode(ui::SORT_TIME);
    PtConfig c;
    pt_config_load(&c);
    check("modo gravado no config.ini", c.app_sort == ui::SORT_TIME);
    ui::set_sort_mode(99);
    pt_config_load(&c);
    check("modo inválido volta a recentes", c.app_sort == ui::SORT_RECENT);
    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
