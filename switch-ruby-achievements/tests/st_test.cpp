#include "pt_status.h"
#include <stdio.h>
#include <string.h>
s64 g_fake_now = 1000;
int main() {
    PtStatus s; memset(&s, 0, sizeof(s));
    strcpy(s.game_name, "Jogo A"); s.is_qlaunch = 1; s.focused_tid = 0x0100152000022000ULL;
    s.focused_game_id = 9; strcpy(s.focused_name, "Mario Kart 8 Deluxe"); s.focused_playtime = 4720;
    int w = pt_status_write(&s);
    PtStatus r; int rd = pt_status_read(&r);
    printf("w=%d r=%d home=%d tid=%016llX gid=%lld name=%s play=%lld\n", w, rd, r.is_qlaunch,
           (unsigned long long)r.focused_tid, (long long)r.focused_game_id, r.focused_name, (long long)r.focused_playtime);
}
