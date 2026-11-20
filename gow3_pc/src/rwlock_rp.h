/* From HayatoG/tlou_pc (GPL-2.0-or-later), 6e668ea. */
/* Reader-preferring read/write lock (the semantics of glibc's default pthread_rwlock_t): a
 * reader waits only while a writer holds the lock, never for a writer that is still waiting.
 * Readers may then nest, on one thread or across threads that wait on each other.
 * winpthreads' pthread_rwlock_wrlock keeps its internal mutex while it waits for the readers to
 * leave (rwlock.c: rwlock_gain_both_locks, then pthread_cond_wait on ccomplete), so a reader that
 * reads again behind a queued writer blocks forever, and the writer with it. runtime_memory.c
 * nests readers: code under its table lock writes guest memory, a GPU-tracked page faults, and
 * the fault handler looks the table up again (or waits on the GPU thread, which does).
 * Writers can starve while readers keep overlapping, as with glibc's default. */
#ifndef GOW3_RWLOCK_RP_H
#define GOW3_RWLOCK_RP_H
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

/* gow3 15 Nov 2026: the lock state is one atomic word, so a reader or writer that does not wait
 * takes no mutex (before, every region lookup of the GPU and copy threads went through one
 * winpthreads mutex: the cost SpartanPort 6525a86 measured, 7 -> 27 FPS in The Order: 1886,
 * whose SRW lock cannot be used here because readers nest across threads). The mutex and the
 * condition are only for waiting: a waiter counts itself in `waiters` before it re-checks the
 * state under the mutex, and whoever changes the state checks `waiters` afterwards (both
 * sequentially consistent), so a wake-up is never lost. Same semantics as before: readers wait
 * only while a writer holds the lock; writers may starve. */
typedef struct {
    unsigned state; /* RP_WRITER while a writer holds it, otherwise the number of readers */
    unsigned waiters;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
} RpLock;
#define RP_WRITER 0x80000000u
#define RP_LOCK_INITIALIZER {0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER}

/* GOW3_FAST_REGION_LOCK=0 (launcher debug menu): every lock and unlock also takes the mutex, the
 * cost of the lock before 15 Nov 2026 (A/B). */
static inline int rp_slow(void) {
    static int slow = -1;
    if (slow < 0) {
        const char *env = getenv("GOW3_FAST_REGION_LOCK");
        slow = env && env[0] == '0';
    }
    return slow;
}
static inline int rp_try_read_lock_fast(RpLock *l) {
    unsigned s = __atomic_load_n(&l->state, __ATOMIC_RELAXED);
    while (!(s & RP_WRITER)) {
        if (__atomic_compare_exchange_n(&l->state, &s, s + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
    }
    return EBUSY;
}
static inline int rp_try_write_lock_fast(RpLock *l) {
    unsigned s = 0;
    return __atomic_compare_exchange_n(&l->state, &s, RP_WRITER, 0, __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED) ? 0 : EBUSY;
}
static inline int rp_try_read_lock(RpLock *l) {
    if (!rp_slow()) return rp_try_read_lock_fast(l);
    pthread_mutex_lock(&l->mutex);
    const int r = rp_try_read_lock_fast(l);
    pthread_mutex_unlock(&l->mutex);
    return r;
}
static inline int rp_try_write_lock(RpLock *l) {
    if (!rp_slow()) return rp_try_write_lock_fast(l);
    pthread_mutex_lock(&l->mutex);
    const int r = rp_try_write_lock_fast(l);
    pthread_mutex_unlock(&l->mutex);
    return r;
}
static inline void rp_wake(RpLock *l) {
    if (rp_slow() || __atomic_load_n(&l->waiters, __ATOMIC_SEQ_CST)) {
        pthread_mutex_lock(&l->mutex);
        pthread_cond_broadcast(&l->changed);
        pthread_mutex_unlock(&l->mutex);
    }
}
static inline void rp_read_lock(RpLock *l) {
    while (rp_try_read_lock(l)) {
        pthread_mutex_lock(&l->mutex);
        __atomic_add_fetch(&l->waiters, 1, __ATOMIC_SEQ_CST);
        while (__atomic_load_n(&l->state, __ATOMIC_SEQ_CST) & RP_WRITER)
            pthread_cond_wait(&l->changed, &l->mutex);
        __atomic_sub_fetch(&l->waiters, 1, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&l->mutex);
    }
}
static inline void rp_read_unlock(RpLock *l) {
    if (__atomic_sub_fetch(&l->state, 1, __ATOMIC_SEQ_CST) == 0) rp_wake(l);
}
static inline void rp_write_lock(RpLock *l) {
    while (rp_try_write_lock(l)) {
        pthread_mutex_lock(&l->mutex);
        __atomic_add_fetch(&l->waiters, 1, __ATOMIC_SEQ_CST);
        while (__atomic_load_n(&l->state, __ATOMIC_SEQ_CST))
            pthread_cond_wait(&l->changed, &l->mutex);
        __atomic_sub_fetch(&l->waiters, 1, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&l->mutex);
    }
}
static inline void rp_write_unlock(RpLock *l) {
    __atomic_store_n(&l->state, 0, __ATOMIC_SEQ_CST);
    rp_wake(l);
}
#endif
