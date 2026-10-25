#pragma once
// Fila de pop-ups exportada pelo sysmodule em PT_POPUP_PATH ("id|raridade|ícone|título"
// por linha, no máximo PT_POPUP_QUEUE_MAX) e a proteção contra flood do menu do overlay.
// Sem libtesla/libnx: testável no PC.
#include <stdlib.h>
#include <string.h>

#include <cstdint>
#include <string>
#include <vector>

#include "pt_paths.h"

namespace popup {

enum class Rarity { Bronze, Silver, Gold, Ruby };

struct Popup {
    int64_t id = 0;
    Rarity rarity = Rarity::Bronze;
    std::string icon, title;
};

inline bool parse_rarity(const char* s, Rarity* out) {
    static const struct {
        const char* name;
        Rarity r;
    } k[] = {{"bronze", Rarity::Bronze}, {"silver", Rarity::Silver}, {"gold", Rarity::Gold}, {"ruby", Rarity::Ruby}};
    for (auto& e : k)
        if (!strcmp(s, e.name)) {
            *out = e.r;
            return true;
        }
    return false;
}

// Conquistas do arquivo com id > `seen`, na ordem dele. Altera `buf`.
inline std::vector<Popup> parse_queue(char* buf, int64_t seen) {
    std::vector<Popup> out;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* f[4] = {line, NULL, NULL, NULL};
        for (int i = 1; i < 4; i++) {
            char* bar = strchr(f[i - 1], '|');
            if (!bar) break;
            *bar = '\0';
            f[i] = bar + 1;
        }
        Popup p;
        if (!f[3] || !parse_rarity(f[1], &p.rarity)) continue;  // linha fora do formato
        p.id = strtoll(f[0], NULL, 10);
        if (p.id <= seen) continue;
        p.icon = f[2];
        p.title = f[3];
        out.push_back(p);
    }
    return out;
}

// --- Proteção contra flood ----------------------------------------------------------------
// Com a opção ligada, uma leva de kFloodLimit ou mais conquistas (um save avançado
// carregado pela primeira vez, por exemplo) não vira pop-up: o overlay marca a fila
// inteira como vista e o sysmodule a apaga. As conquistas continuam gravadas no banco.
// Leva = pop-ups já mostrados desde a última vez que a fila esteve vazia + os que
// esperam. Ligar a opção no meio de uma leva longa descarta o resto dela na hora.
constexpr int kFloodLimit = 10;
static_assert(kFloodLimit <= PT_POPUP_QUEUE_MAX, "o overlay precisa ver a fila longa inteira");

class Flood {
public:
    // A cada leitura da fila: true = descartar os `pending` que esperam.
    bool drop(bool enabled, int pending) {
        if (pending <= 0) {
            m_wave = 0;  // fila vazia: a leva acabou
            return false;
        }
        if (!enabled || m_wave + pending < kFloodLimit) return false;
        m_wave += pending;  // chegadas da mesma leva também são descartadas
        return true;
    }
    void shown() { m_wave++; }  // um pop-up da leva começou
    int wave() const { return m_wave; }

private:
    int m_wave = 0;
};

}  // namespace popup
