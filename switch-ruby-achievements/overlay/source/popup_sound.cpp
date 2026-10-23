#include "popup_sound.hpp"

#include <stdio.h>
#include <string.h>

#include <atomic>
#include <string>

#include "pt_wav.hpp"
#include "pt_log.h"
#include "pt_paths.h"

#define SLOG(...) pt_log_file(PT_OVERLAY_LOG_PATH, __VA_ARGS__)

namespace sound {
namespace {

constexpr int kBuffers = 3;
constexpr size_t kBufBytes = 0x4000;  // 4096 quadros estéreo de 16 bits (~85 ms)
alignas(0x1000) u8 g_mem[kBuffers][kBufBytes];
AudioOutBuffer g_out[kBuffers];

bool g_ready = false;
Thread g_thread;
Mutex g_mutex;
CondVar g_cond;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_stop{false};
bool g_request = false;  // protegido por g_mutex
std::string g_path;
u32 g_max_ms = 0;

// Enche um buffer com até kBufBytes de áudio estéreo; devolve os bytes escritos.
size_t fill(FILE* f, const pt::Wav& w, u32& frames_left, u8* out) {
    const u32 max_frames = (u32)(kBufBytes / 4);
    u32 frames = frames_left < max_frames ? frames_left : max_frames;
    if (!frames) return 0;
    size_t got;
    if (w.channels == 2) {
        got = fread(out, 4, frames, f);
    } else {  // mono: lê no fim do buffer e duplica para os dois canais
        s16* mono = (s16*)(out + kBufBytes) - frames;
        got = fread(mono, 2, frames, f);
        s16* st = (s16*)out;
        for (size_t i = 0; i < got; i++) st[2 * i] = st[2 * i + 1] = mono[i];
    }
    frames_left -= (u32)got;
    if (got < frames) frames_left = 0;  // fim do arquivo
    return got * 4;
}

void play_file(const std::string& path, u32 max_ms) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    pt::Wav w;
    if (!pt::read_wav(f, w) || !w.playable()) {
        fclose(f);
        return;
    }
    fseek(f, w.data_offset, SEEK_SET);
    u32 frames_left = w.data_size / (w.channels * 2);
    u32 limit = max_ms * 48;  // o som acaba junto com o pop-up
    if (frames_left > limit) frames_left = limit;

    if (R_FAILED(audoutStartAudioOut())) {
        fclose(f);
        return;
    }
    int queued = 0;
    for (int i = 0; i < kBuffers; i++) {
        g_out[i].next = nullptr;
        g_out[i].buffer = g_mem[i];
        g_out[i].buffer_size = kBufBytes;
        g_out[i].data_offset = 0;
        g_out[i].data_size = fill(f, w, frames_left, g_mem[i]);
        if (!g_out[i].data_size) break;
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&g_out[i]))) queued++;
    }
    while (queued > 0) {
        AudioOutBuffer* released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 50'000'000ULL)) || !released || !count) {
            if (g_stop || g_quit) break;
            continue;
        }
        queued--;
        if (g_stop || g_quit) continue;
        released->data_size = fill(f, w, frames_left, (u8*)released->buffer);
        if (released->data_size && R_SUCCEEDED(audoutAppendAudioOutBuffer(released))) queued++;
    }
    audoutStopAudioOut();
    bool flushed = false;
    audoutFlushAudioOutBuffers(&flushed);
    fclose(f);
}

void worker(void*) {
    while (true) {
        mutexLock(&g_mutex);
        while (!g_request && !g_quit) condvarWait(&g_cond, &g_mutex);
        if (g_quit) {
            mutexUnlock(&g_mutex);
            return;
        }
        std::string path = g_path;
        u32 max_ms = g_max_ms;
        g_request = false;
        g_stop = false;
        mutexUnlock(&g_mutex);
        play_file(path, max_ms);
    }
}

}  // namespace

void init() {
    mutexInit(&g_mutex);
    condvarInit(&g_cond);
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        SLOG("som do pop-up desativado: audout rc=0x%X", rc);
        return;
    }
    rc = threadCreate(&g_thread, worker, nullptr, nullptr, 0x4000, 0x2C, -2);
    if (R_SUCCEEDED(rc)) rc = threadStart(&g_thread);
    if (R_FAILED(rc)) {
        SLOG("som do pop-up desativado: thread rc=0x%X", rc);
        audoutExit();
        return;
    }
    g_ready = true;
    SLOG("som do pop-up: audout pronto (%u Hz, %u canais)", audoutGetSampleRate(), audoutGetChannelCount());
}

void exit() {
    if (!g_ready) return;
    g_stop = true;
    mutexLock(&g_mutex);
    g_quit = true;
    condvarWakeAll(&g_cond);
    mutexUnlock(&g_mutex);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    audoutExit();
    g_ready = false;
}

u32 duration_ms(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    pt::Wav w;
    bool ok = pt::read_wav(f, w);
    fclose(f);
    if (!ok || !w.playable()) {
        static bool warned = false;
        if (!warned) SLOG("som do pop-up: %s não é WAV PCM 16 bits 48 kHz (pop-up mudo)", path);
        warned = true;
        return 0;
    }
    return w.ms();
}

void play(const char* path, u32 max_ms) {
    if (!g_ready) return;
    g_stop = true;  // interrompe o anterior, se houver
    mutexLock(&g_mutex);
    g_path = path;
    g_max_ms = max_ms;
    g_request = true;
    condvarWakeOne(&g_cond);
    mutexUnlock(&g_mutex);
}

void stop() {
    if (!g_ready) return;
    mutexLock(&g_mutex);
    g_request = false;
    mutexUnlock(&g_mutex);
    g_stop = true;
}

}  // namespace sound
