// Fila de pop-ups do overlay e proteção contra flood (popup_queue.hpp).
// Simula o laço do PopupBanner::poll: lê a fila, aplica o flood, mostra a primeira.
#include <cstdio>
#include <string>
#include <vector>

#include "popup_queue.hpp"

static int fails = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "ok" : "FALHOU");
    if (!ok) fails++;
}

// Fila do sysmodule: ids [first, first+n), no máximo PT_POPUP_QUEUE_MAX linhas.
struct Sim {
    int64_t next_id = 1, last_id = 0, seen = 0;
    popup::Flood flood;
    int shown = 0, dropped = 0;

    void unlock(int n) { last_id += n; }
    std::string file() const {
        std::string s;
        int lines = 0;
        for (int64_t id = seen + 1; id <= last_id && lines < PT_POPUP_QUEUE_MAX; id++, lines++)
            s += std::to_string(id) + "|bronze||Conquista " + std::to_string(id) + "\n";
        return s;
    }
    std::vector<popup::Popup> pending() const {
        std::string f = file();
        std::vector<char> buf(f.begin(), f.end());
        buf.push_back('\0');
        return popup::parse_queue(buf.data(), seen);
    }
    // Um poll do overlay (sem pop-up na tela).
    void poll(bool enabled) {
        auto q = pending();
        if (q.empty()) {
            flood.drop(false, 0);
            return;
        }
        if (flood.drop(enabled, (int)q.size())) {
            dropped += (int)q.size();
            seen = q.back().id;
            return;
        }
        seen = q.front().id;
        flood.shown();
        shown++;
    }
    // Opção ligada no menu.
    void toggle_on() {
        auto q = pending();
        if (flood.drop(true, (int)q.size())) {
            dropped += (int)q.size();
            seen = q.back().id;
        }
    }
    void drain(bool enabled) {
        for (int i = 0; i < 100; i++) poll(enabled);
    }
};

int main() {
    // --- leitura da fila
    char buf[] = "3|gold|sdmc:/i.png|Ouro\n4|ruby||Platina\n2|bronze||Velha\n5|Antigo|descricao\nlixo\n";
    auto q = popup::parse_queue(buf, 2);
    check("fila: só ids > visto, na ordem do arquivo", q.size() == 3 && q[0].id == 3 && q[1].id == 4 && q[2].id == 5);
    check("fila: raridade, ícone e título",
          q[0].rarity == popup::Rarity::Gold && q[0].icon == "sdmc:/i.png" && q[0].title == "Ouro" &&
              q[1].rarity == popup::Rarity::Ruby);
    check("fila: formato antigo (id|título|descrição)", q[2].title == "Antigo" && q[2].icon.empty());

    // --- desligada (padrão): mostra todas, uma por uma
    Sim off;
    off.unlock(28);
    off.drain(false);
    check("desligada: 28 de uma vez = 28 pop-ups", off.shown == 28 && off.dropped == 0 && off.seen == 28);

    // --- ligada: 10 ou mais de uma vez não viram pop-up
    Sim on;
    on.unlock(28);
    on.poll(true);
    check("ligada: 28 de uma vez = nenhum pop-up", on.shown == 0 && on.seen == 16);
    on.drain(true);
    check("ligada: o resto da leva (além das 16 do arquivo) também some", on.shown == 0 && on.seen == 28);
    check("ligada: todas marcadas como vistas (o sysmodule apaga a fila)", on.dropped == 28);

    Sim ten;
    ten.unlock(10);
    ten.drain(true);
    check("ligada: exatamente 10 = nenhum pop-up", ten.shown == 0 && ten.dropped == 10);

    Sim nine;
    nine.unlock(9);
    nine.drain(true);
    check("ligada: 9 de uma vez = 9 pop-ups", nine.shown == 9 && nine.dropped == 0);

    // Depois de uma leva descartada, a próxima conquista normal aparece.
    on.unlock(1);
    on.drain(true);
    check("ligada: conquista nova depois da leva = pop-up normal", on.shown == 1 && on.seen == 29);

    // Leva que cresce enquanto os pop-ups aparecem: 5, e mais 5 antes de esvaziar.
    Sim grow;
    grow.unlock(5);
    grow.poll(true);
    grow.poll(true);
    grow.unlock(5);
    grow.drain(true);
    check("ligada: leva que chega a 10 aos poucos = resto descartado", grow.shown == 2 && grow.dropped == 8);

    // Duas levas de 5 separadas (a fila esvazia entre elas): todas aparecem.
    Sim two;
    two.unlock(5);
    two.drain(true);
    two.unlock(5);
    two.drain(true);
    check("ligada: duas levas de 5 separadas = 10 pop-ups", two.shown == 10 && two.dropped == 0);

    // --- ligar no meio de uma fila longa: limpa na hora
    Sim mid;
    mid.unlock(28);
    for (int i = 0; i < 5; i++) mid.poll(false);
    mid.toggle_on();
    check("ligar no 5º de 28: o resto some na hora", mid.shown == 5 && mid.seen == 21);  // 16 linhas no arquivo
    mid.drain(true);
    check("ligar no 5º de 28: nenhum pop-up depois", mid.shown == 5 && mid.seen == 28);

    Sim tail;
    tail.unlock(28);
    for (int i = 0; i < 25; i++) tail.poll(false);
    tail.toggle_on();
    check("ligar no 25º de 28: os 3 que faltam somem (a leva é longa)", tail.shown == 25 && tail.seen == 28);

    Sim shortq;
    shortq.unlock(4);
    shortq.poll(false);
    shortq.toggle_on();
    shortq.drain(true);
    check("ligar no meio de uma fila curta (4): nada some", shortq.shown == 4 && shortq.dropped == 0);

    printf("%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails;
}
