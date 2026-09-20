/*
 * The pthread objects whose bionic layout the host does not share. Two of
 * them: pthread_attr_t, below, and pthread_mutex_t, further down.
 *
 * pthread_attr_t, because bionic's is 24 bytes and glibc's is 36.
 *
 * bionic (ILP32) declares the attribute object inline:
 *
 *     typedef struct {
 *       uint32_t flags;  void *stack_base;   size_t stack_size;
 *       size_t guard_size;  int32_t sched_policy;  int32_t sched_priority;
 *     } pthread_attr_t;                                  // 24 bytes
 *
 * glibc declares it as `union { char __size[36]; long __align; }`. The
 * generated bionic table binds pthread_attr_init straight to the host's, and
 * the host's zeroes all 36 bytes - into the 24 the game reserved.
 *
 * native_app_glue puts that object on the stack:
 *
 *     android_app_create():
 *         pthread_attr_t attr;                  // 24 bytes of frame
 *         pthread_attr_init(&attr);             // host writes 36
 *         pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 *         pthread_create(&app->thread, &attr, android_app_entry, app);
 *
 * so the twelve extra bytes land on whatever the compiler put after it -
 * here, the stack canary - and the process dies in __stack_chk_fail during
 * ANativeActivity_onCreate, before the game thread has done anything. Exactly
 * the same failure shape as clock_gettime in symtab_time.cpp, and just as
 * silent: nothing in the log mentions pthread.
 *
 * So the attribute object is kept in the game's layout on the game's stack,
 * and translated into a real host pthread_attr_t only at pthread_create.
 *
 * These entries come before the generated libc table in so_dynamic_libraries
 * so they win the lookup.
 */
#include <errno.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unordered_map>

#include <time.h>
#include "time_scale.h"
#include "trace.h"
#include "so_util.h"
#include "thunk_gen.h"
#include "thunk_pthread.h"

/* Defined in src/main.cpp, with C++ linkage - so declared outside the
 * extern "C" block below or the two would not be the same symbol. */
struct so_module;
extern so_module *katamari_module(void);

#if defined(_WIN32)
namespace {

struct GuestThreadRecord {
    pthread_t host_thread;
    bool detached;
};

struct GuestThreadStart {
    uint32_t guest_thread;
    void *(*entry)(void *);
    void *argument;
    bool detached;
    std::mutex registration_lock;
    std::condition_variable registered_condition;
    bool registered = false;
};

std::mutex g_guest_thread_lock;
std::unordered_map<uint32_t, GuestThreadRecord> g_guest_threads;
std::atomic<uint32_t> g_next_guest_thread{1};
thread_local uint32_t g_current_guest_thread = 0;

std::mutex g_once_lock;
struct GuestOnceState {
    bool running = false;
    bool complete = false;
    std::thread::id owner;
    std::condition_variable changed;
};
std::unordered_map<uint32_t *, std::shared_ptr<GuestOnceState>> g_once_states;

uint32_t allocate_guest_thread_id()
{
    for (;;) {
        const uint32_t id = g_next_guest_thread.fetch_add(
            1, std::memory_order_relaxed);
        if (id != 0)
            return id;
    }
}

void remember_guest_thread(uint32_t id, pthread_t host_thread, bool detached)
{
    std::lock_guard<std::mutex> lock(g_guest_thread_lock);
    g_guest_threads[id] = {host_thread, detached};
}

bool find_guest_thread(uint32_t id, pthread_t *host_thread,
                       bool *detached = nullptr)
{
    std::lock_guard<std::mutex> lock(g_guest_thread_lock);
    const auto found = g_guest_threads.find(id);
    if (found == g_guest_threads.end())
        return false;
    if (host_thread)
        *host_thread = found->second.host_thread;
    if (detached)
        *detached = found->second.detached;
    return true;
}

void forget_guest_thread(uint32_t id)
{
    std::lock_guard<std::mutex> lock(g_guest_thread_lock);
    g_guest_threads.erase(id);
}

uint32_t guest_thread_id_for_current()
{
    if (g_current_guest_thread)
        return g_current_guest_thread;

    const pthread_t host_thread = pthread_self();
    std::lock_guard<std::mutex> lock(g_guest_thread_lock);
    for (const auto &entry : g_guest_threads) {
        if (pthread_equal(entry.second.host_thread, host_thread)) {
            g_current_guest_thread = entry.first;
            return entry.first;
        }
    }

    const uint32_t id = allocate_guest_thread_id();
    g_guest_threads.emplace(id, GuestThreadRecord{host_thread, false});
    g_current_guest_thread = id;
    return id;
}

void *guest_thread_start(void *opaque)
{
    std::unique_ptr<GuestThreadStart> start(
        static_cast<GuestThreadStart *>(opaque));
    g_current_guest_thread = start->guest_thread;
    {
        std::unique_lock<std::mutex> lock(start->registration_lock);
        start->registered_condition.wait(lock, [&start] {
            return start->registered;
        });
    }
    void *(*entry)(void *) = start->entry;
    void *argument = start->argument;
    const bool detached = start->detached;
    start.reset();

    void *result = entry(argument);
    if (detached)
        forget_guest_thread(g_current_guest_thread);
    return result;
}

} // namespace
#endif

extern "C" {

/* bionic's armeabi timespec: two 32-bit words. The host's is two 64-bit ones.
 * Declared here rather than shared with symtab_sem.cpp so each file stays
 * readable on its own; the layout is fixed by the game's ABI, not by us. */
static pthread_mutex_t g_mutex_bootstrap = PTHREAD_MUTEX_INITIALIZER;

struct bionic_timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};

struct bionic_pthread_attr {
    uint32_t flags;
    void    *stack_base;
    uint32_t stack_size;
    uint32_t guard_size;
    int32_t  sched_policy;
    int32_t  sched_priority;
};

/* bionic/pthread_internal.h */
enum {
    BIONIC_PTHREAD_ATTR_FLAG_DETACHED = 0x00000001,
    BIONIC_PTHREAD_ATTR_FLAG_INHERIT  = 0x00000004,
};

/* bionic's own defaults for a 32-bit process. */
static const uint32_t kBionicDefaultStack = 1 * 1024 * 1024;
static const uint32_t kBionicDefaultGuard = 4096;

int bionic_pthread_attr_init(struct bionic_pthread_attr *attr)
{
    if (!attr)
        return EINVAL;

    memset(attr, 0, sizeof(*attr));
    attr->stack_size    = kBionicDefaultStack;
    attr->guard_size    = kBionicDefaultGuard;
    attr->sched_policy  = SCHED_OTHER;
    attr->sched_priority = 0;
    return 0;
}

int bionic_pthread_attr_destroy(struct bionic_pthread_attr *attr)
{
    if (!attr)
        return EINVAL;
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int bionic_pthread_attr_setdetachstate(struct bionic_pthread_attr *attr, int state)
{
    if (!attr)
        return EINVAL;

    /* PTHREAD_CREATE_JOINABLE == 0 and PTHREAD_CREATE_DETACHED == 1 on both
     * sides, so the constant crosses unchanged; only the storage differs. */
    if (state == PTHREAD_CREATE_DETACHED)
        attr->flags |= BIONIC_PTHREAD_ATTR_FLAG_DETACHED;
    else if (state == PTHREAD_CREATE_JOINABLE)
        attr->flags &= ~(uint32_t)BIONIC_PTHREAD_ATTR_FLAG_DETACHED;
    else
        return EINVAL;

    return 0;
}

int bionic_pthread_attr_getdetachstate(const struct bionic_pthread_attr *attr, int *state)
{
    if (!attr || !state)
        return EINVAL;
    *state = (attr->flags & BIONIC_PTHREAD_ATTR_FLAG_DETACHED)
                 ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    return 0;
}

int bionic_pthread_attr_setstacksize(struct bionic_pthread_attr *attr, uint32_t size)
{
    if (!attr || size < 16384)
        return EINVAL;
    attr->stack_size = size;
    return 0;
}

int bionic_pthread_attr_getstacksize(const struct bionic_pthread_attr *attr, uint32_t *size)
{
    if (!attr || !size)
        return EINVAL;
    *size = attr->stack_size;
    return 0;
}

int bionic_pthread_attr_setguardsize(struct bionic_pthread_attr *attr, uint32_t size)
{
    if (!attr)
        return EINVAL;
    attr->guard_size = size;
    return 0;
}

/*
 * The counterpart: read the game's attribute object and build a host one.
 *
 * The generated table's pthread_create thunk ignores the attribute argument
 * entirely - safe, since it cannot parse it, but it means a thread the game
 * detached stays joinable and is never reaped. native_app_glue detaches its
 * thread and never joins it, so honouring the flag is what keeps a long
 * session from accumulating dead thread descriptors.
 *
 * The stack size is deliberately *not* honoured downwards. bionic's 1 MB
 * default is sized for a thread that only ever runs the game; here the same
 * thread also runs host GL, SDL and libc code that Android's linker never had
 * to fit in that budget, so the host default is used unless the game asked
 * for more.
 */
/*
 */
static void report_thread(int rc, pthread_t *thread)
{
    if (rc != 0 || !thread)
        return;

#if defined(_WIN32)
    /* PThreads4W exposes the native thread handle instead of GNU's
     * pthread_getattr_np/pthread_attr_getstack extensions. */
    trace("  ^ pthread_t=%p win_thread=%p", thread->p,
          pthread_getw32threadhandle_np(*thread));
#else
    void  *stack = NULL;
    size_t size  = 0;
    pthread_attr_t a;
    if (pthread_getattr_np(*thread, &a) == 0) {
        pthread_attr_getstack(&a, &stack, &size);
        pthread_attr_destroy(&a);
    }

    trace("  ^ pthread_t=%p stack=%p..%p",
          (void *)*thread, stack, (char *)stack + size);
#endif
}


#if defined(_WIN32)
int bionic_pthread_create(uint32_t *guest_thread,
                          const struct bionic_pthread_attr *attr,
                          void *(*entry)(void *), void *arg)
{
    {
        so_module *mod = katamari_module();
        if (mod && (uintptr_t)entry >= mod->text_base &&
            (uintptr_t)entry <  mod->text_base + mod->text_size)
            trace("pthread_create: entry at +0x%08x, arg=%p",
                  (unsigned)((uintptr_t)entry - mod->text_base), arg);
        else
            trace("pthread_create: entry at %p (outside the module)", (void *)entry);
    }
    if (attr)
        trace("pthread_create: guest attrs stack=%p size=%u guard=%u flags=0x%08x",
              attr->stack_base, attr->stack_size, attr->guard_size, attr->flags);

    if (!guest_thread || !entry)
        return EINVAL;

    const bool detached = attr &&
        (attr->flags & BIONIC_PTHREAD_ATTR_FLAG_DETACHED) != 0;
    GuestThreadStart *start = new (std::nothrow) GuestThreadStart{};
    if (!start)
        return ENOMEM;
    start->guest_thread = allocate_guest_thread_id();
    start->entry = entry;
    start->argument = arg;
    start->detached = detached;

    pthread_attr_t host;
    int rc = pthread_attr_init(&host);
    if (rc != 0) {
        delete start;
        return rc;
    }

    if (detached) {
        rc = pthread_attr_setdetachstate(&host, PTHREAD_CREATE_DETACHED);
        if (rc != 0) {
            pthread_attr_destroy(&host);
            delete start;
            return rc;
        }
    }

    /* The Bionic stack address belongs to its caller and may be only reserved,
     * not committed as a Windows stack. PThreads4W cannot supply Windows' stack
     * guard growth for that region, so use an OS-managed stack and carry across
     * only the requested size. Bionic's 1 MiB default is also the minimum here
     * because UE3's initialization path exceeds PThreads4W's small default. */
    size_t requested_stack = attr && attr->stack_size >= 16384
        ? attr->stack_size : kBionicDefaultStack;
    if (requested_stack < kBionicDefaultStack)
        requested_stack = kBionicDefaultStack;
    size_t host_default = 0;
    if (pthread_attr_getstacksize(&host, &host_default) != 0)
        host_default = 0;
    if (requested_stack > host_default)
        rc = pthread_attr_setstacksize(&host, requested_stack);
    if (rc != 0) {
        pthread_attr_destroy(&host);
        delete start;
        return rc;
    }
    trace("pthread_create: host stack=%u guest request=%u custom=%p",
          (unsigned)(requested_stack > host_default ? requested_stack : host_default),
          attr ? attr->stack_size : 0, attr ? attr->stack_base : nullptr);

    pthread_t host_thread{};
    rc = pthread_create(&host_thread, &host, guest_thread_start, start);
    pthread_attr_destroy(&host);
    if (rc != 0) {
        delete start;
        return rc;
    }

    remember_guest_thread(start->guest_thread, host_thread, detached);
    *guest_thread = start->guest_thread;
    {
        std::lock_guard<std::mutex> lock(start->registration_lock);
        start->registered = true;
    }
    start->registered_condition.notify_one();
    report_thread(rc, &host_thread);
    return rc;
}
#else
int bionic_pthread_create(pthread_t *thread, const struct bionic_pthread_attr *attr,
                          void *(*entry)(void *), void *arg)
{
    {
        so_module *mod = katamari_module();
        if (mod && (uintptr_t)entry >= mod->text_base &&
            (uintptr_t)entry <  mod->text_base + mod->text_size)
            trace("pthread_create: entry at +0x%08x, arg=%p",
                  (unsigned)((uintptr_t)entry - mod->text_base), arg);
        else
            trace("pthread_create: entry at %p (outside the module)", (void *)entry);
    }

    if (!attr) {
        int rc0 = pthread_create(thread, NULL, entry, arg);
        report_thread(rc0, thread);
        return rc0;
    }

    pthread_attr_t host;
    if (pthread_attr_init(&host) != 0)
        return pthread_create(thread, NULL, entry, arg);

    if (attr->flags & BIONIC_PTHREAD_ATTR_FLAG_DETACHED)
        pthread_attr_setdetachstate(&host, PTHREAD_CREATE_DETACHED);

    size_t host_default = 0;
    pthread_attr_getstacksize(&host, &host_default);
    if (attr->stack_size > host_default)
        pthread_attr_setstacksize(&host, attr->stack_size);

    int rc = pthread_create(thread, &host, entry, arg);
    pthread_attr_destroy(&host);
    report_thread(rc, thread);
    return rc;
}
#endif

/*
 * Mutexes, because bionic's pthread_mutex_t is one 32-bit word and glibc's is
 * twenty-four bytes.
 *
 * The game cannot own a real host mutex in the space it reserved, so
 * gmloader-next's bridge (thunks/libc/pthread.cpp) keeps the host mutex on the
 * heap and stores the pointer in that word. Sound - except for how it decides
 * whether a mutex has been created yet: it tests the word against zero, and
 * zero is only one of bionic's three static initialisers.
 *
 *     PTHREAD_MUTEX_INITIALIZER             { 0      }
 *     PTHREAD_RECURSIVE_MUTEX_INITIALIZER   { 0x4000 }
 *     PTHREAD_ERRORCHECK_MUTEX_INITIALIZER  { 0x8000 }
 *
 * A statically initialised *recursive* mutex therefore reaches the bridge
 * holding 0x4000, the bridge reads that as an already-allocated pointer, and
 * glibc dereferences address 0x4000.
 *
 * the C++ runtime it links statically guards its function-local statics with
 *
 *     static pthread_mutex_t guard = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
 *
 *
 * A sibling port never tripped this: its build of the same engine has no
 * statically initialised recursive mutex, so every word the bridge ever saw
 * was 0.
 *
 * The word is therefore decoded here instead, and this table comes before the
 * generated libc one in so_dynamic_libraries, so these win the lookup.
 *
 * The claim that used to close this comment - that only four entry points are
 * imported and "pthread_mutex_trylock and friends are left to the bridge
 * because the game never calls them" - was wrong, and wrong in the expensive
 * direction. The game imports trylock and calls it three times, twice inside
 * EAThread's Mutex on the same object it locks two instructions earlier.
 * Bound straight to glibc, it reads our heap pointer as __lock, __kind from a
 * live field of the game's object, and on success writes __owner and __nusers
 * over twenty bytes the game owns.
 *
 * The lesson is not about trylock. "The game never calls it" is a claim about
 * the binary and belongs in a readelf output, not in a comment - and the same
 * sentence was covering pthread_mutexattr_setpshared, pthread_attr_setstack
 * and pthread_attr_setschedparam, all three imported, all three called from
 * the thread-creation path that every worker in the engine goes through.
 */

/* bionic/pthread.h: the mutex type lives in bits 14-15 of the value word. */
enum {
    BIONIC_MUTEX_TYPE_MASK       = 0xc000,
    BIONIC_MUTEX_TYPE_RECURSIVE  = 0x4000,
    BIONIC_MUTEX_TYPE_ERRORCHECK = 0x8000,
};

/*
 * How a static initialiser is told apart from a pointer we stored.
 *
 * Every value bionic can put in that word statically fits in sixteen bits,
 * and nothing we hand back can be that low: Linux refuses to map below
 * vm.mmap_min_addr, which is 64 KiB on every kernel these handhelds run.
 */
static const uintptr_t kBionicInitWordMax = 0xffff;

/*
 * Serialises the lazy creation below. A statically initialised mutex has no
 * init call to hang the allocation off, so the first lock is where it has to
 * happen - and two threads can arrive there at once.
 */

static pthread_mutex_t *host_mutex(BIONIC_pthread_mutex_t *m)
{
    if (!m)
        return NULL;

    /*
     * Acquire, not a plain read.
     *
     * This is double-checked locking, and the fast path below never takes
     * g_mutex_bootstrap - so without an acquire here there is no happens-before
     * edge between another thread initialising the mutex body and this thread
     * seeing the pointer. On ARM, which is weakly ordered, a thread can observe
     * the published pointer and still read a pthread_mutex_t whose fields have
     * not arrived. glibc then locks on uninitialised __kind and __lock and
     * waits on a futex nobody will wake.
     *
     * That is not a theoretical path in this game. EAThread emulates 64-bit
     * atomics with a table of 32 static mutexes in .bss, indexed by hashing the
     * address (`lsr r5,r4,#1 ; and r5,r5,#0x7c` at +0x24bdec) and locked on
     * every 64-bit load and store from any thread. Being in .bss they all start
     * at zero, so every one of them goes through this lazy creation, from
     * several threads, starting on the first frame.
     *
     * The release store below is the other half. real_mtx is also read through
     * __atomic_load_n rather than as a plain field so the compiler cannot
     * reload it between the check and the return.
     */
    pthread_mutex_t *fast = std::atomic_ref<pthread_mutex_t *>(m->real_mtx)
        .load(std::memory_order_acquire);
    if ((uintptr_t)fast > kBionicInitWordMax)
        return fast;

    pthread_mutex_lock(&g_mutex_bootstrap);

    /* Re-read under the lock: another thread may have created it meanwhile. */
    uintptr_t word = (uintptr_t)m->real_mtx;
    if (word <= kBionicInitWordMax) {
        pthread_mutex_t *host = (pthread_mutex_t *)calloc(1, sizeof(*host));
        if (host) {
            /* The type is the whole point: a recursive guard mutex locked as
             * a normal one self-deadlocks the first time the engine re-enters
             * it, which is what __cxa_guard_acquire does by design. */
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            switch (word & BIONIC_MUTEX_TYPE_MASK) {
            case BIONIC_MUTEX_TYPE_RECURSIVE:
                pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
                break;
            case BIONIC_MUTEX_TYPE_ERRORCHECK:
                pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
                break;
            default:
                pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
                break;
            }
            pthread_mutex_init(host, &attr);
            pthread_mutexattr_destroy(&attr);
            /* Release: everything written into *host above must be visible to
             * any thread that sees this pointer on the fast path. */
            std::atomic_ref<pthread_mutex_t *>(m->real_mtx)
                .store(host, std::memory_order_release);
        }
    }

    pthread_mutex_unlock(&g_mutex_bootstrap);
    return std::atomic_ref<pthread_mutex_t *>(m->real_mtx)
        .load(std::memory_order_acquire);
}

#if defined(_WIN32)
int bionic_pthread_mutex_init(BIONIC_pthread_mutex_t *m,
                              const pthread_mutexattr_t *attr)
#else
int bionic_pthread_mutex_init(BIONIC_pthread_mutex_t *m,
                              pthread_mutexattr_t **attr)
#endif
{
    if (!m)
        return EINVAL;

    /*
     * The incoming word is deliberately NOT looked at.
     *
     * An earlier version tried to be tidy here: if the word already held a
     * pointer above kBionicInitWordMax it destroyed and freed that host mutex
     * first, so re-initialising would not leak. That reasoning has a hole -
     * it assumes the word means something on entry, and on a freshly
     * malloc'd mutex it does not.
     *
     * is plain memalign - no zeroing:
     *
     *     xt::FileWatcher::FileWatcher+0x7c   mov  r0, #4
     *                                         bl   xt::MemoryManager::allocMemory
     *                                         mov  r1, #0
     *                                         bl   pthread_mutex_init@plt
     *
     * Those four bytes are whatever the previous owner of the chunk left
     * there - in practice a stale heap pointer, which sails past the
     * kBionicInitWordMax test. The shim then called free() on a pointer that
     * was never allocated as a mutex, and glibc aborted the process with
     * "free(): invalid pointer" a few allocations later.
     *
     * Bionic itself never reads the word in pthread_mutex_init: the mutex is
     * inline there, so initialising simply overwrites it. Matching that is
     * both correct and the only safe option. Re-initialising an already
     * initialised mutex is undefined behaviour in POSIX and neither the game
     * nor bionic does it, so the leak this used to prevent cannot happen -
     * and even if it did, a leaked 24-byte mutex beats a dead process.
     */
    pthread_mutex_t *host = (pthread_mutex_t *)calloc(1, sizeof(*host));
    if (!host)
        return ENOMEM;

    /* The double indirection is gmloader-next's convention, not ours: its
     * pthread_mutexattr_init bridge stores a host attribute object in the
*/
#if defined(_WIN32)
    int rc = pthread_mutex_init(host, attr);
#else
    int rc = pthread_mutex_init(host, attr ? *attr : NULL);
#endif
    if (rc != 0) {
        free(host);
        return rc;
    }

    m->real_mtx = host;
    return 0;
}

int bionic_pthread_mutex_destroy(BIONIC_pthread_mutex_t *m)
{
    if (!m)
        return EINVAL;

    /* A mutex that was only ever statically initialised and never locked has
     * nothing behind it, and destroying it is still legal. */
    if ((uintptr_t)m->real_mtx > kBionicInitWordMax) {
        pthread_mutex_destroy(m->real_mtx);
        free(m->real_mtx);
    }

    m->value = 0;
    return 0;
}

int bionic_pthread_mutex_lock(BIONIC_pthread_mutex_t *m)
{
    pthread_mutex_t *host = host_mutex(m);
    return host ? pthread_mutex_lock(host) : EINVAL;
}

int bionic_pthread_mutex_unlock(BIONIC_pthread_mutex_t *m)
{
    pthread_mutex_t *host = host_mutex(m);
    return host ? pthread_mutex_unlock(host) : EINVAL;
}

} /* extern "C" */

/* ------------------------------------------------------------------ *
 * The four calls the comment above wrongly assumed nobody made.
 *
 * Each one operates on an object this loader keeps in a layout the host does
 * not share, and each was bound straight to glibc. None of them crash where
 * they are called - they corrupt an object that faults later, somewhere else,
 * which is why they survived this long. The Vita port wraps all four
 * (vita-ref/loader/reimpl/pthr.c) against this same library.
 * ------------------------------------------------------------------ */

/*
 * trylock has to follow the same indirection lock/unlock do: the game's word
 * holds a pointer to the host mutex, and host_mutex() decodes bionic's static
 * initialisers on the way. Three call sites, two of them on the same object
 * that pthread_mutex_lock takes twelve instructions later.
 */
int bionic_pthread_mutex_trylock(BIONIC_pthread_mutex_t *m)
{
    pthread_mutex_t *host = host_mutex(m);
    if (!host)
        return EINVAL;
    return pthread_mutex_trylock(host);
}

/*
 * setpshared, likewise. glibc implements it as a read-modify-write that sets
 * bit 31 of the word it is given, and under this bridge that word is a heap
 * pointer - so a PROCESS_SHARED request turns 0x000b1020 into 0x800b1020 and
 * the next pthread_mutex_init dereferences it. The engine asks for
 * PROCESS_SHARED on one of its two branches.
 */
#if defined(_WIN32)
int bionic_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (!attr)
        return EINVAL;
    *attr = nullptr;
    return pthread_mutexattr_init(attr);
}

int bionic_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (!attr || !*attr)
        return EINVAL;

    int host_type;
    switch (type) {
    case 0: host_type = PTHREAD_MUTEX_NORMAL; break;
    case 1: host_type = PTHREAD_MUTEX_RECURSIVE; break;
    case 2: host_type = PTHREAD_MUTEX_ERRORCHECK; break;
    default: return EINVAL;
    }
    return pthread_mutexattr_settype(attr, host_type);
}

int bionic_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (!attr || !*attr)
        return EINVAL;
    return pthread_mutexattr_destroy(attr);
}

int bionic_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
    if (!attr || !*attr ||
        (pshared != PTHREAD_PROCESS_PRIVATE &&
         pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;

    /* This port runs the game in one process; map Bionic's shared request to
     * a process-local Windows mutex rather than failing an otherwise usable
     * mutex initialization on hosts without process-shared pthread support. */
    return pthread_mutexattr_setpshared(attr, PTHREAD_PROCESS_PRIVATE);
}
#else
int bionic_pthread_mutexattr_setpshared(pthread_mutexattr_t **attr_ptr, int pshared)
{
    if (!attr_ptr || !*attr_ptr)
        return EINVAL;
    return pthread_mutexattr_setpshared(*attr_ptr, pshared);
}
#endif

#if defined(_WIN32)
uint32_t bionic_pthread_self()
{
    return guest_thread_id_for_current();
}

int bionic_pthread_join(uint32_t guest_thread, void **result)
{
    pthread_t host_thread{};
    bool detached = false;
    if (!guest_thread ||
        !find_guest_thread(guest_thread, &host_thread, &detached))
        return ESRCH;
    if (detached)
        return EINVAL;

    const int rc = pthread_join(host_thread, result);
    if (rc == 0)
        forget_guest_thread(guest_thread);
    return rc;
}

int bionic_pthread_detach(uint32_t guest_thread)
{
    pthread_t host_thread{};
    bool detached = false;
    if (!guest_thread ||
        !find_guest_thread(guest_thread, &host_thread, &detached))
        return ESRCH;
    if (detached)
        return EINVAL;

    const int rc = pthread_detach(host_thread);
    if (rc == 0)
        forget_guest_thread(guest_thread);
    return rc;
}

[[noreturn]] void bionic_pthread_exit(void *result)
{
    const uint32_t guest_thread = g_current_guest_thread;
    if (guest_thread) {
        bool detached = false;
        if (find_guest_thread(guest_thread, nullptr, &detached) && detached)
            forget_guest_thread(guest_thread);
    }
    pthread_exit(result);
    abort();
}

int bionic_pthread_once(volatile uint32_t *once_control,
                        void (*init_routine)(void))
{
    if (!once_control || !init_routine)
        return EINVAL;

    uint32_t *key = const_cast<uint32_t *>(once_control);
    std::shared_ptr<GuestOnceState> state;
    {
        std::lock_guard<std::mutex> lock(g_once_lock);
        auto &stored = g_once_states[key];
        if (!stored)
            stored = std::make_shared<GuestOnceState>();
        state = stored;
    }

    std::unique_lock<std::mutex> lock(g_once_lock);
    while (state->running) {
        if (state->owner == std::this_thread::get_id())
            return EDEADLK;
        state->changed.wait(lock);
    }
    if (state->complete)
        return 0;

    state->running = true;
    state->owner = std::this_thread::get_id();
    *once_control = 1;
    lock.unlock();

    init_routine();

    lock.lock();
    state->complete = true;
    state->running = false;
    state->owner = std::thread::id{};
    *once_control = 2;
    lock.unlock();
    state->changed.notify_all();
    return 0;
}
#endif

/*
 * setstack and setschedparam write through glibc's pthread_attr layout into an
 * object this file deliberately keeps in bionic's. The offsets overlap without
 * overflowing, so nothing faults - the fields simply land on the wrong ones:
 *
 *   setschedparam writes the priority at offset 0, which is bionic's `flags`,
 *   erasing the DETACHED bit that pthread_attr_setdetachstate set two calls
 *   earlier. Every thread the engine starts then leaks its descriptor and its
 *   stack, because nothing ever joins them.
 *
 *   setstack writes stackaddr and stacksize at 16 and 20, which are bionic's
 *   sched_policy and sched_priority, while the size the engine asked for never
 *   reaches `stack_size` at offset 8 - so bionic_pthread_create below compares
 *   against the host default, loses, and the thread runs on a stack the engine
 *   did not choose while the buffer it allocated is simply abandoned.
 *
 * Both are in EA::Thread::Thread::Begin, between setdetachstate and
 * pthread_create. Every worker goes through them.
 */
int bionic_pthread_attr_setstack(struct bionic_pthread_attr *attr,
                                 void *stack_base, uint32_t stack_size)
{
    if (!attr || !stack_base || stack_size < 16384)
        return EINVAL;
    attr->stack_base = stack_base;
    attr->stack_size = stack_size;
    return 0;
}

int bionic_pthread_attr_setschedparam(struct bionic_pthread_attr *attr,
                                      const int *param)
{
    if (!attr || !param)
        return EINVAL;
    /* bionic's sched_param is a single int, and so is the host's on ARM; only
     * where it lands differs. */
    attr->sched_priority = *param;
    return 0;
}



/*
 * pthread_cond_timedwait, because the game's struct timespec is eight bytes and
 * this host's is sixteen.
 *
 * Debian trixie's armhf is built with _TIME_BITS=64, so the host's time_t is
 * 64-bit and its timespec is {int64 tv_sec; int64 tv_nsec}. bionic's armeabi is
 * {int32; int32}. This is the same disagreement src/symtab_time.cpp fixed for
 * clock_gettime and nanosleep and src/symtab_sem.cpp fixed for sem_timedwait -
 * and this one call was left behind, forwarding the pointer untouched.
 *
 * The disassembly shows the caller reading only two words:
 *
 *     24bf18  ldr r5, [r2, #4]    ; tv_nsec
 *     24bf1c  ldr lr, [ip, #3740] ; the "no timeout" sentinel
 *     24bf40  bl  pthread_cond_timedwait@plt
 *
 * so glibc composes tv_sec from the game's {tv_sec, tv_nsec} pair and reads
 * tv_nsec from eight bytes of the caller's frame that were never written. The
 * deadline lands either far in the future - the waiting thread never wakes - or
 * in the past, or glibc rejects it with EINVAL and the caller, which only
 * distinguishes ETIMEDOUT from "error", spins. All three are a hung or spinning
 * subsystem with nothing in the log, which is the failure mode this port has
 * been chasing on a worker thread.
 *
 * EA::Thread::Condition::Wait is the only call site.
 */
#if defined(_WIN32)
static bool guest_condition_initialized(pthread_cond_t *condition)
{
    const pthread_cond_t handle =
        std::atomic_ref<pthread_cond_t>(*condition)
            .load(std::memory_order_acquire);
    const uintptr_t value = (uintptr_t)handle;
    return value > kBionicInitWordMax &&
           value != (uintptr_t)PTHREAD_COND_INITIALIZER;
}

static int ensure_guest_condition(pthread_cond_t *condition)
{
    if (!condition)
        return EINVAL;
    if (guest_condition_initialized(condition))
        return 0;

    const int lock_rc = pthread_mutex_lock(&g_mutex_bootstrap);
    if (lock_rc != 0)
        return lock_rc;

    int rc = 0;
    if (!guest_condition_initialized(condition)) {
        std::atomic_ref<pthread_cond_t>(*condition)
            .store(nullptr, std::memory_order_release);
        rc = pthread_cond_init(condition, nullptr);
    }
    pthread_mutex_unlock(&g_mutex_bootstrap);
    return rc;
}

int bionic_pthread_cond_init(pthread_cond_t *condition,
                             const pthread_condattr_t * /* attributes */)
{
    if (!condition)
        return EINVAL;
    const int lock_rc = pthread_mutex_lock(&g_mutex_bootstrap);
    if (lock_rc != 0)
        return lock_rc;
    std::atomic_ref<pthread_cond_t>(*condition)
        .store(nullptr, std::memory_order_release);
    const int rc = pthread_cond_init(condition, nullptr);
    pthread_mutex_unlock(&g_mutex_bootstrap);
    return rc;
}

int bionic_pthread_cond_destroy(pthread_cond_t *condition)
{
    if (!condition)
        return EINVAL;
    if (!guest_condition_initialized(condition))
        return 0;
    const int rc = pthread_cond_destroy(condition);
    if (rc == 0)
        std::atomic_ref<pthread_cond_t>(*condition)
            .store(nullptr, std::memory_order_release);
    return rc;
}

int bionic_pthread_cond_signal(pthread_cond_t *condition)
{
    const int rc = ensure_guest_condition(condition);
    return rc == 0 ? pthread_cond_signal(condition) : rc;
}

int bionic_pthread_cond_broadcast(pthread_cond_t *condition)
{
    const int rc = ensure_guest_condition(condition);
    return rc == 0 ? pthread_cond_broadcast(condition) : rc;
}

int bionic_pthread_cond_wait(pthread_cond_t *condition,
                             BIONIC_pthread_mutex_t *mutex)
{
    const int rc = ensure_guest_condition(condition);
    if (rc != 0)
        return rc;
    pthread_mutex_t *host_mutex_handle = host_mutex(mutex);
    return host_mutex_handle
        ? pthread_cond_wait(condition, host_mutex_handle) : EINVAL;
}

int bionic_pthread_cond_timedwait(pthread_cond_t *condition,
                                  BIONIC_pthread_mutex_t *mutex,
                                  const struct bionic_timespec *absolute)
{
    const int rc = ensure_guest_condition(condition);
    if (rc != 0)
        return rc;
    pthread_mutex_t *host_mutex_handle = host_mutex(mutex);
    if (!host_mutex_handle || !absolute)
        return EINVAL;

    struct timespec deadline;
    deadline.tv_sec = (time_t)absolute->tv_sec;
    deadline.tv_nsec = (long)absolute->tv_nsec;
    port_time_scale_reverse(&deadline);
    return pthread_cond_timedwait(condition, host_mutex_handle, &deadline);
}
#else
int bionic_pthread_cond_timedwait(pthread_cond_t **cnd, BIONIC_pthread_mutex_t *m,
                                  const struct bionic_timespec *abs)
{
    pthread_mutex_t *host = host_mutex(m);
    if (!cnd || !host || !abs)
        return EINVAL;

    /*
     * Lazily created, and serialised - which the bridge's version is not.
     *
     * The engine never calls pthread_cond_init: readelf lists it as UND with no
     * relocation at all, and EA::Thread::Condition's constructor simply zeroes
     * two words. So every condvar in this game is created on first use, and a
     * condvar is by definition touched from more than one thread - the waiter
     * and the signaller are never the same. Two of them arriving at a zero word
     * together each allocate one, and one of the two is then referenced by
     * nobody: a lost wakeup, a thread parked forever, no crash and nothing in
     * the log.
     *
     * Same reasoning and same lock as host_mutex() below, which took
     * g_mutex_bootstrap for exactly this case and whose comment does not
     * mention that condvars have it worse.
     */
    if (!*cnd) {
        pthread_mutex_lock(&g_mutex_bootstrap);
        if (!*cnd) {                       /* re-read under the lock */
            pthread_cond_t *c = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
            if (!c) {
                pthread_mutex_unlock(&g_mutex_bootstrap);
                return ENOMEM;
            }
            pthread_cond_init(c, NULL);
            std::atomic_ref<pthread_cond_t *>(*cnd)
                .store(c, std::memory_order_release);
        }
        pthread_mutex_unlock(&g_mutex_bootstrap);
    }

    struct timespec ts;
    ts.tv_sec  = (time_t)abs->tv_sec;
    ts.tv_nsec = (long)abs->tv_nsec;
    /* The guest built this deadline from a clock the time-scale shim may be
     * running fast, so it sits in the host's future by the whole accumulated
     * offset. Without the inverse the wait gets longer the longer the process
     * has been up, and a timed wait that never times out is indistinguishable
     * from a deadlock. A no-op unless the scale is set; see time_scale.h. */
    port_time_scale_reverse(&ts);
    return pthread_cond_timedwait(*cnd, host, &ts);
}
#endif

DynLibFunction symtable_pthread[] = {
    THUNK_SPECIFIC("pthread_attr_init",           bionic_pthread_attr_init),
    THUNK_SPECIFIC("pthread_attr_destroy",        bionic_pthread_attr_destroy),
    THUNK_SPECIFIC("pthread_attr_setdetachstate", bionic_pthread_attr_setdetachstate),
    THUNK_SPECIFIC("pthread_attr_getdetachstate", bionic_pthread_attr_getdetachstate),
    THUNK_SPECIFIC("pthread_attr_setstacksize",   bionic_pthread_attr_setstacksize),
    THUNK_SPECIFIC("pthread_attr_getstacksize",   bionic_pthread_attr_getstacksize),
    THUNK_SPECIFIC("pthread_attr_setguardsize",   bionic_pthread_attr_setguardsize),
#if defined(_WIN32)
    THUNK_SPECIFIC("pthread_self",                bionic_pthread_self),
    THUNK_SPECIFIC("pthread_create",              bionic_pthread_create),
    THUNK_SPECIFIC("pthread_join",                bionic_pthread_join),
    THUNK_SPECIFIC("pthread_detach",              bionic_pthread_detach),
    THUNK_SPECIFIC("pthread_exit",                bionic_pthread_exit),
    THUNK_SPECIFIC("pthread_once",                bionic_pthread_once),
    THUNK_DIRECT(pthread_key_create),
    THUNK_DIRECT(pthread_key_delete),
    THUNK_DIRECT(pthread_getspecific),
    THUNK_DIRECT(pthread_setspecific),
#else
    THUNK_SPECIFIC("pthread_create",              bionic_pthread_create),
#endif

    THUNK_SPECIFIC("pthread_mutex_init",          bionic_pthread_mutex_init),
    THUNK_SPECIFIC("pthread_mutex_destroy",       bionic_pthread_mutex_destroy),
    THUNK_SPECIFIC("pthread_mutex_lock",          bionic_pthread_mutex_lock),
    THUNK_SPECIFIC("pthread_mutex_unlock",        bionic_pthread_mutex_unlock),
    THUNK_SPECIFIC("pthread_mutex_trylock",       bionic_pthread_mutex_trylock),
#if defined(_WIN32)
    THUNK_SPECIFIC("pthread_cond_init",           bionic_pthread_cond_init),
    THUNK_SPECIFIC("pthread_cond_destroy",        bionic_pthread_cond_destroy),
    THUNK_SPECIFIC("pthread_cond_wait",           bionic_pthread_cond_wait),
    THUNK_SPECIFIC("pthread_cond_signal",          bionic_pthread_cond_signal),
    THUNK_SPECIFIC("pthread_cond_broadcast",       bionic_pthread_cond_broadcast),
#endif
    THUNK_SPECIFIC("pthread_cond_timedwait",      bionic_pthread_cond_timedwait),
#if defined(_WIN32)
    THUNK_SPECIFIC("pthread_mutexattr_init",       bionic_pthread_mutexattr_init),
    THUNK_SPECIFIC("pthread_mutexattr_destroy",    bionic_pthread_mutexattr_destroy),
    THUNK_SPECIFIC("pthread_mutexattr_settype",    bionic_pthread_mutexattr_settype),
#endif
    THUNK_SPECIFIC("pthread_mutexattr_setpshared", bionic_pthread_mutexattr_setpshared),
    THUNK_SPECIFIC("pthread_attr_setstack",       bionic_pthread_attr_setstack),
    THUNK_SPECIFIC("pthread_attr_setschedparam",  bionic_pthread_attr_setschedparam),

    { NULL, 0 },
};
