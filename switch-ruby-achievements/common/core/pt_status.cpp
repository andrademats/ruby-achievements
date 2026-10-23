#include "pt_status.h"
#include "pt_paths.h"
#include "pt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_line_value(char* dst, size_t cap, const char* src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int pt_status_write(const PtStatus* s) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
                     "title_id=%s\ngame_id=%lld\ngame_name=%s\nsession_id=%lld\nuser_id=%lld\n"
                     "profile_id=%lld\nbase=%lld\nlive=%lld\ncounting=%d\nwritten_at=%lld\n"
                     "is_qlaunch=%d\nfocused_tid=%016llX\nfocused_game_id=%lld\nfocused_name=%s\n"
                     "focused_playtime=%lld\n",
                     s->title_id, (long long)s->game_id, s->game_name, (long long)s->session_id,
                     (long long)s->user_id, (long long)s->profile_id, (long long)s->base,
                     (long long)s->live, s->counting, (long long)s->written_at, s->is_qlaunch,
                     (unsigned long long)s->focused_tid, (long long)s->focused_game_id, s->focused_name,
                     (long long)s->focused_playtime);
    if (n < 0 || n >= (int)sizeof(buf)) return -1;
    return pt_write_file_atomic(PT_STATUS_PATH, buf, (size_t)n);
}

int pt_status_read(PtStatus* s) {
    char buf[1024];
    if (pt_read_file(PT_STATUS_PATH, buf, sizeof(buf)) <= 0) return -1;
    memset(s, 0, sizeof(*s));
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* k = line;
        const char* v = eq + 1;
        long long n = strtoll(v, NULL, 10);
        if (!strcmp(k, "title_id")) copy_line_value(s->title_id, sizeof(s->title_id), v);
        else if (!strcmp(k, "game_id")) s->game_id = n;
        else if (!strcmp(k, "game_name")) copy_line_value(s->game_name, sizeof(s->game_name), v);
        else if (!strcmp(k, "session_id")) s->session_id = n;
        else if (!strcmp(k, "user_id")) s->user_id = n;
        else if (!strcmp(k, "profile_id")) s->profile_id = n;
        else if (!strcmp(k, "base")) s->base = n;
        else if (!strcmp(k, "live")) s->live = n;
        else if (!strcmp(k, "counting")) s->counting = (int)n;
        else if (!strcmp(k, "written_at")) s->written_at = n;
        else if (!strcmp(k, "is_qlaunch")) s->is_qlaunch = (int)n;
        else if (!strcmp(k, "focused_tid")) s->focused_tid = strtoull(v, NULL, 16);
        else if (!strcmp(k, "focused_game_id")) s->focused_game_id = n;
        else if (!strcmp(k, "focused_name")) copy_line_value(s->focused_name, sizeof(s->focused_name), v);
        else if (!strcmp(k, "focused_playtime")) s->focused_playtime = n;
    }
    return 0;
}

int pt_users_read(PtUserEntry* out, int max) {
    char buf[4096];
    if (pt_read_file(PT_USERS_PATH, buf, sizeof(buf)) <= 0) return 0;
    int count = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line && count < max;
         line = strtok_r(NULL, "\n", &save)) {
        char* bar = strchr(line, '|');
        if (!bar) continue;
        *bar = '\0';
        out[count].id = strtoll(line, NULL, 10);
        copy_line_value(out[count].nickname, sizeof(out[count].nickname), bar + 1);
        if (out[count].id > 0) count++;
    }
    return count;
}
