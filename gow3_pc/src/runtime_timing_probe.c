/* GOW3_TIMING_LOG=1: read-only probe that samples the guest's timing globals every
 * GOW3_TIMING_PERIOD seconds (default 5, host CLOCK_MONOTONIC_RAW) and prints the
 * per-window deltas on stderr. Diagnostics for the animation-speed bug: it measures,
 * against host real time, (a) the guest frame-spawn counter, (b) the accumulated
 * game time in dword_1556100, (c) the TSC/divisor clock the guest itself uses,
 * (d) the per-frame dt the physics step receives (frame state +0x78) and
 * (e) g_havok's m_dt/step counters. It only reads guest memory and a host rdtsc;
 * it never writes guest memory.
 *
 * Offsets (eboot 1.11, verified in IDA, see analysis/tlou/AGENT-STATE.md):
 *   0x153D958  qword  TSC divisor global (3,000,000 in the image = 3.0e6 ticks/s as on
 *                     PS4, but sub_C86A70 at init overwrites it with
 *                     sceKernelGetTscFrequency(), so at runtime it is the HLE
 *                     frequency: 1e9 on Linux without GOW3_NATIVE_TSC, ~4.01e9
 *                     with the native TSC on the FX-8350)
 *   0x1B76778  qword  TSC origin the loop subtracts (raw rdtsc reference)
 *   0x1556100  float  printed as game_ms, but in menu/Prologue runs it stays near 0 in
 *                     both the buggy and the fixed builds: real semantics unverified, do NOT
 *                     use it as a game-time integrator. The reliable simulation-speed metric
 *                     is the probe's own guest_ms field = (rdtsc - origin) / divisor.
 *   0x15560F8  float  last per-frame time (SECONDS); 0.0333 = 30 FPS at real speed,
 *                     0.1338 = 4.0134x fast (Linux 1 GHz fallback on a 4.013 GHz host)
 *   0x156A480  qword  pointer to the engine ClockManager (verified: the "Draw Real Clock"
 *                     debug button references [ptr+0x120] = kRealClock slot). +0x28 = a
 *                     64-bit counter that advances once per guest frame (30/s at the limit).
 *                     The probe diffs the first 0x1000 bytes of the manager each window and
 *                     reports every changed field (offset, float value, delta) so the clocks
 *                     that advance faster than real time are visible without prior layout.
 *   0x1524A70  qword  in-image frame/time manager; +0x18 -> current frame state (heap),
 *                     +0xA8 (0x1524B18) = float scale the animation channels multiply
 *                     their per-frame step with (var_2C in sub_6F34E0)
 *   0x1524D10  qword  +0x104 = float the animation channels use when a flag is set
 *                     (var_30 in sub_6F34E0); 0x1524E18 = flag dword (low byte = enabled)
 *   frame state +0x1C = dt in seconds HavokStep uses, +0x60 = frame delta it clamps
 *   0x1524A70  qword  frame-state manager; +0x18 -> current frame state,
 *                     +0x78 = per-frame dt (float seconds) that HavokStep clamps
 *   0x1700C30  frame ring, stride 0xA20; +0x78 = per-frame dt, +0x64 = start tick
 *   0x16982C0  g_havok; +0x5A0 = m_dt, +0x59C = m_invDt, +0xAC8/+0xACC = step counters
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef _WIN32
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#endif

typedef struct {
    unsigned char *image;
    uint64_t size;
    double period;
    double delay;
#ifndef _WIN32
    double t0;
#endif
} TimingProbe;

static TimingProbe probe;

#ifndef _WIN32
static double raw_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
/* A guest pointer is safe to read when it is a canonical user address and every page
 * it touches is resident (mincore). This covers both the boot image and committed
 * pool pages; uncommitted PROT_NONE pages report not-resident and are skipped. */
static int readable(const volatile void *p, size_t n) {
    const unsigned long long a = (unsigned long long)(uintptr_t)p;
    if (a < 0x1000 || a >= (1ULL << 47)) return 0;
    unsigned long long base = a & ~0xFFFULL;
    size_t pages = ((a - base) + n + 0xFFF) >> 12;
    if (pages > 8) return 0;
    unsigned char vec[8] = {0};
    if (mincore((void *)(uintptr_t)base, pages * 4096, vec)) return 0;
    for (size_t i = 0; i < pages; i++)
        if (!vec[i]) return 0;
    return 1;
}
static uint64_t rd64(uint64_t a) { return readable((const void *)a, 8) ? *(const volatile uint64_t *)a : 0; }
static float rd32f(uint64_t a) { return readable((const void *)a, 4) ? *(const volatile float *)a : -1.0f; }

#define MGR_SPAN 0x1000u
static float mgr_prev[MGR_SPAN / 4];
static int mgr_prev_valid = 0;
static void mgr_diff(uint64_t base, double dt) {
    if (!readable((const void *)base, MGR_SPAN)) { mgr_prev_valid = 0; return; }
    const volatile float *p = (const volatile float *)base;
    if (mgr_prev_valid) {
        int shown = 0;
        for (unsigned i = 0; i < MGR_SPAN / 4 && shown < 12; i++) {
            const float now = p[i];
            const float prev = mgr_prev[i];
            const float d = now - prev;
            if (d != 0.0f && (now == now) && (prev == prev) && (d == d)) {
                /* ignore tiny float noise (< 1e-4 of the larger magnitude or < 1e-6 absolute) */
                const float m = (now > -now && now < -prev ? now : (prev > -prev ? prev : -prev));
                if (d < 0 ? -d > 1e-4f * m + 1e-6f : d > 1e-4f * m + 1e-6f) {
                    fprintf(stderr, "  MGR +%03x: %+9.4f -> %+9.4f  (d %+9.4f, rate %+9.2f/s)\n",
                            i * 4, (double)prev, (double)now, (double)d, (double)d / dt);
                    ++shown;
                }
            }
        }
    }
    for (unsigned i = 0; i < MGR_SPAN / 4; i++) mgr_prev[i] = p[i];
    mgr_prev_valid = 1;
}
static void *probe_thread(void *arg) {
    (void)arg;
    nanosleep(&(struct timespec){(time_t)probe.delay, (long)((probe.delay - (double)(time_t)probe.delay) * 1e9)}, NULL);
    fprintf(stderr, "TIMING probe sampling every %.1f s (CLOCK_MONOTONIC_RAW)\n", probe.period);
    double t_prev = raw_now();
    uint64_t frames_prev = 0, havok_prev_a = 0, havok_prev_b = 0;
    double guest_ms_prev = -1.0, game_ms_prev = -1.0;
    int have_prev = 0;
    for (;;) {
        nanosleep(&(struct timespec){0, 250 * 1000000L}, NULL);
        const double t = raw_now();
        if (t - t_prev < probe.period) continue;
        const uint64_t base = (uint64_t)probe.image;
        const uint64_t div = rd64(base + 0x153D958);
        const uint64_t origin = rd64(base + 0x1B76778);
        const uint64_t tsc = __rdtsc();
        const double guest_ms = div ? (double)((int64_t)(tsc - origin)) / (double)div : -1.0;
        uint64_t ps = rd64(base + 0x156A480);
        uint64_t frames = ps ? rd64(ps + 40) : 0;
        uint64_t fidx = ps ? rd64(ps + 0x28) : 0;
        uint64_t mgr = rd64(base + 0x1524A70);
        uint64_t fs = mgr ? rd64(mgr + 0x18) : 0;
        const float frame_dt = fs ? rd32f(fs + 0x78) : -1.0f;
        const float ring_dt = (fidx < 512) ? rd32f(base + 0x1700C30 + fidx * 0xA20 + 0x78) : -1.0f;
        const float game_ms = rd32f(base + 0x1556100);
        const float last_ms = rd32f(base + 0x15560F8);
        const float havok_dt = rd32f(base + 0x16982C0 + 0x5A0);
        const uint64_t hc = base + 0x16982C0 + 0xAC8;
        const uint32_t havok_a = readable((const void *)hc, 8) ? *(const volatile uint32_t *)hc : 0;
        const uint32_t havok_b = readable((const void *)hc, 8) ? *(const volatile uint32_t *)(hc + 4) : 0;
        /* Frame state (Havok dt source) and the animation scale globals. */
        const float fs_dt_s = fs ? rd32f(fs + 0x1C) : -1.0f;
        const float fs_delta = fs ? rd32f(fs + 0x60) : -1.0f;
        const float fs_78 = fs ? rd32f(fs + 0x78) : -1.0f;
        const float anim_scale = rd32f(base + 0x1524B18);
        const float anim_scale2 = rd32f(base + 0x1524D10 + 0x104);
        const uint32_t anim_flag = rd64(base + 0x1524E18);
        if (have_prev) {
            const double dt = t - t_prev;
            fprintf(stderr,
                    "TIMING t=%6.1fs frames=%9llu (%6.1f/s) havok=%8u/%8u game_ms=%+11.1f (%6.4fx) last_ms=%8.4f guest_ms=%+12.1f (%6.4fx) fdt=%.5f rdt=%.5f hdt=%.4f fidx=%3llu div=%llu\n",
                    t - probe.t0,
                    (unsigned long long)(frames - frames_prev),
                    (double)(frames - frames_prev) / dt,
                    (unsigned)(havok_a - havok_prev_a), (unsigned)(havok_b - havok_prev_b),
                    (double)(game_ms - game_ms_prev), (game_ms - game_ms_prev) / (dt * 1000.0),
                    (double)last_ms,
                    (guest_ms - guest_ms_prev), (guest_ms - guest_ms_prev) / (dt * 1000.0),
                    (double)frame_dt, (double)ring_dt, (double)havok_dt,
                    (unsigned long long)fidx, (unsigned long long)div);
            fprintf(stderr,
                    "  FS fs=%llx dt_s=%9.5f delta=%9.4f f78=%9.4f | ASCL=%.4f ASCL2=%.4f flag=%x\n",
                    (unsigned long long)fs, (double)fs_dt_s, (double)fs_delta, (double)fs_78,
                    (double)anim_scale, (double)anim_scale2, (unsigned)anim_flag);
            mgr_diff(ps, dt);
        } else {
            fprintf(stderr,
                    "TIMING baseline t=%6.1fs ps=%llx fs=%llx mgr=%llx frames=%llu fidx=%llu origin=%llx game_ms=%.1f last_ms=%.4f guest_ms=%.1f div=%llu\n",
                    t - probe.t0,
                    (unsigned long long)ps, (unsigned long long)fs, (unsigned long long)mgr,
                    (unsigned long long)frames, (unsigned long long)fidx,
                    (unsigned long long)origin, (double)game_ms, (double)last_ms, guest_ms,
                    (unsigned long long)div);
        }
        t_prev = t;
        frames_prev = frames;
        guest_ms_prev = guest_ms;
        game_ms_prev = game_ms;
        havok_prev_a = havok_a;
        havok_prev_b = havok_b;
        have_prev = 1;
    }
    return NULL;
}
#endif

void runtime_timing_probe_start(unsigned char *image, uint64_t size) {
    const char *env = getenv("GOW3_TIMING_LOG");
    if (!env || !env[0] || !strcmp(env, "0")) return;
    probe.image = image;
    probe.size = size;
    const char *per = getenv("GOW3_TIMING_PERIOD");
    probe.period = per && per[0] ? atof(per) : 5.0;
    if (probe.period < 0.5) probe.period = 0.5;
    const char *del = getenv("GOW3_TIMING_DELAY");
    probe.delay = del && del[0] ? atof(del) : 10.0;
#ifdef _WIN32
    fputs("TIMING probe: available on POSIX only\n", stderr);
#else
    probe.t0 = raw_now();
    pthread_t th;
    if (pthread_create(&th, NULL, probe_thread, NULL)) {
        fputs("TIMING probe: cannot create thread\n", stderr);
        return;
    }
    pthread_detach(th);
#endif
}
