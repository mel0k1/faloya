






static const char *gs_errmsgs[] = {
    "limited array capacity exceeded",
    "slice bounds out of range",
    "relative reference offset overflow",
    "assert failed",
    "invalid capacity",
    "resize growth requires a fill value",
    "resize to a negative length",
    "pop on empty array",
    "thread_wait on an unknown thread id",
    "thread_wait on the current thread",
    "corrupt ADT tag",
    "serialization needs a little-endian host (not supported yet)",
    "invalid slice length",
    "slice not from this pool",
    "non-null relative reference encodes as null",
    "pop_n count negative or beyond the array's length",
    "queue limit of zero or less",
    "invalid array length",
    "slice not within this array",
    "slice pool span tree is corrupt",
};








static GS_NORETURN void gs_end(int code);

#include <setjmp.h>
#include <stdarg.h>


#ifdef _WIN32
#define gs_setjmp(b)  setjmp(b)
#define gs_longjmp    longjmp
#else
#define gs_setjmp(b)  _setjmp(b)
#define gs_longjmp    _longjmp
#endif

GS_API GS_NORETURN void gs_panic(const char *msg) {
    fprintf(stderr, "faloya runtime error: %s\n", msg);
    gs_end(1);
}



typedef struct {
    jmp_buf jump;
    uint8_t *msg;
    int64_t len;
} gs_contained;
static GS_TLS gs_contained *gs_contain;







static GS_NORETURN void gs_fail(const void *msg, int64_t len, const char *file, int line) {
    static const char lead[] = "faloya runtime error: ";
    int64_t pre = sizeof lead - 1;
    int64_t loc = file ? snprintf(NULL, 0, " (%s:%d)\n", file, line) : 1;
    uint8_t *text = (uint8_t *)malloc((size_t)(pre + len + loc + 1));
    if (!text) {
        if (gs_contain) gs_panic("out of memory recording an aborted thread");
        fputs(lead, stderr);
        fwrite(msg, 1, (size_t)len, stderr);
        if (file) fprintf(stderr, " (%s:%d)\n", file, line);
        else fputc('\n', stderr);
        gs_end(1);
    }
    memcpy(text, lead, (size_t)pre);
    if (len) memcpy(text + pre, msg, (size_t)len);
    if (file) snprintf((char *)text + pre + len, (size_t)loc + 1, " (%s:%d)\n", file, line);
    else memcpy(text + pre + len, "\n", 2);
    if (gs_contain) {
        memmove(text, text + pre, (size_t)(len + loc - 1));
        gs_contain->msg = text;
        gs_contain->len = len + loc - 1;
        gs_longjmp(gs_contain->jump, 1);
    }
    fwrite(text, 1, (size_t)(pre + len + loc), stderr);
    free(text);
    gs_end(1);
}


static GS_NORETURN void gs_failf(const char *file, int line, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    gs_fail(buf, n < 0 ? 0 : n < (int)sizeof buf ? n : (int)sizeof buf - 1, file, line);
}

GS_API GS_NORETURN void gs_abort(int err, const char *file, int line) {
    gs_fail(gs_errmsgs[err], (int64_t)strlen(gs_errmsgs[err]), file, line);
}

GS_API GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line) {
    gs_fail(msg, len, file, line);
}

GS_API GS_NORETURN void gs_exit(int64_t code) {
    gs_end((int)code);
}

GS_API GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line) {
    gs_failf(file, line, "index %lld out of bounds (length %lld)", (long long)i, (long long)n);
}

GS_API GS_NORETURN void gs_divfail(const char *file, int line) {
    gs_failf(file, line, "division by zero");
}

GS_API GS_NORETURN void gs_divovf(const char *file, int line) {
    gs_failf(file, line, "integer overflow in division");
}

GS_API GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line) {
    gs_failf(file, line, "integer overflow (debug): %lld %s %lld at %s", (long long)a, op,
             (long long)b, type);
}

GS_API GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line) {
    gs_failf(file, line, "integer overflow (debug): -(%lld) at %s", (long long)a, type);
}


static GS_NORETURN void gs_asfail(const char *why, const char *num, const char *type,
                                  const char *file, int line) {
    gs_failf(file, line, "as conversion %s (debug): %s as %s", why, num, type);
}

GS_API GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%lld", (long long)v);
    gs_asfail(why, num, type, file, line);
}

GS_API GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%llu", (unsigned long long)v);
    gs_asfail(why, num, type, file, line);
}

GS_API GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line) {
    uint8_t num[32];
    num[f32 ? gs_fmt_f32(num, (float)d) : gs_fmt_f64(num, d)] = 0;
    gs_asfail(why, (const char *)num, type, file, line);
}











static int gs_argc;
static char **gs_argv;





static size_t gs_region_usable, gs_region_gap, gs_region_reserve, gs_region_page;

static GS_TLS int64_t gs_current_thread_id = -1;




static uint64_t gs_stack_budget;
static int64_t gs_mainregions, gs_workerregions;
static int64_t gs_thread_cap = INT64_MAX;



typedef struct {
    uint8_t *base;
    size_t usable, size;    
    gs_stack *stack;       
    



    uint8_t *committed;
} gs_region;
static GS_TLS gs_region *gs_regions;
static GS_TLS volatile long gs_nregions;


static GS_TLS long gs_regions_cap;




static char *gs_dec(char *p, uint64_t v) {
    char d[24];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) *p++ = d[--n];
    return p;
}
static char *gs_size(char *p, uint64_t bytes) {
    static const char *const units[] = { " bytes", " KB", " MB", " GB", " TB" };
    int u = 0;
    while (u < 4 && bytes && bytes % 1024 == 0) {
        bytes /= 1024;
        u++;
    }
    p = gs_dec(p, bytes);
    for (const char *s = units[u]; *s; s++) *p++ = *s;
    return p;
}
static char *gs_text(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}




#define GS_OVERFLOW_TEXT 320
static size_t gs_overflow_text(char *buf, const gs_region *r) {
    char *p = gs_text(buf, "faloya runtime error: data stack overflow: a value on ");
    if (gs_current_thread_id < 0) {
        p = gs_text(p, "main's");
    } else {
        p = gs_text(p, "worker ");
        p = gs_dec(p, (uint64_t)gs_current_thread_id);
        p = gs_text(p, "'s");
    }
    p = gs_text(p, " data stacks grew past the ");
    p = gs_size(p, r->usable);
    p = gs_text(p, " reserved for it");
    if (r->usable < gs_region_reserve) {
        p = gs_text(p, " (halved from ");
        p = gs_size(p, gs_region_reserve);
        p = gs_text(p, " when the platform ran out of address space)");
    }
    p = gs_text(p, "; --stack-reserve or -DGS_STACK_RESERVE raises the reservation, "
                   "up to 256 TB\n");
    return (size_t)(p - buf);
}

GS_API GS_NORETURN void gs_stack_overflow(const void *at) {
    for (long i = 0; i < gs_nregions; i++) {
        const gs_region *r = &gs_regions[i];
        if ((uintptr_t)at - (uintptr_t)r->base < r->size) {
            char msg[GS_OVERFLOW_TEXT];
            fwrite(msg, 1, gs_overflow_text(msg, r), stderr);
            gs_end(1);
        }
    }
    gs_panic("data stack overflow");
}


static void gs_regions_begin(int64_t capacity) {
    gs_regions_cap = (long)capacity;
    gs_regions = (gs_region *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(gs_region));
    if (!gs_regions) gs_panic("out of memory allocating the data stack registry");
    gs_nregions = 0;
}

static void gs_release_region(const gs_region *r);
static void gs_native_stack_free(void);


GS_API void gs_release_regions(void) {
    while (gs_nregions) {
        long i = gs_nregions - 1;
        gs_region r = gs_regions[i];
        gs_nregions = i;
        gs_regions[i].base = NULL;
        gs_release_region(&r);
    }
    free(gs_regions);
    gs_regions = NULL;
    gs_native_stack_free();
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static size_t gs_page_size = 0;

static void *gs_fault_handle;
#define GS_COMMIT_CHUNK (1u << 20)


#define GS_STACK_GUARANTEE (64u << 10)

static LONG WINAPI gs_fault_filter(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) {
        

        static const char msg[] = "faloya runtime error: native call stack overflow\n";
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, NULL);
        ExitProcess(1);
    }
    if (code != STATUS_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *hit = (uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    for (long i = 0; i < gs_nregions; i++) {
        gs_region *r = &gs_regions[i];
        if ((uintptr_t)hit - (uintptr_t)r->base < r->size) {
            


            if (hit < r->base + r->usable) {
                uint8_t *page = (uint8_t *)((size_t)hit & ~(gs_page_size - 1));
                size_t n = GS_COMMIT_CHUNK;
                if (page + n > r->base + r->usable)
                    n = (size_t)(r->base + r->usable - page);
                if (VirtualAlloc(page, n, MEM_COMMIT, PAGE_READWRITE)) {
                    if (page + n > r->committed) r->committed = page + n;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                static const char nomem[] =
                    "faloya runtime error: out of memory committing a data stack\n";
                DWORD written;
                WriteFile(GetStdHandle(STD_ERROR_HANDLE), nomem, sizeof(nomem) - 1, &written,
                          NULL);
                ExitProcess(1);
            }
            char msg[GS_OVERFLOW_TEXT];
            DWORD written;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)gs_overflow_text(msg, r),
                      &written, NULL);
            ExitProcess(1);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}


static size_t gs_os_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}


static void gs_regions_init(void) {
    gs_page_size = gs_os_page_size();
    gs_fault_handle = AddVectoredExceptionHandler(1, gs_fault_filter);
    if (!gs_fault_handle)
        gs_panic("cannot install data stack fault handler");
}



static void gs_native_stack_init(void) {
    ULONG room = GS_STACK_GUARANTEE;
    #ifdef __TINYC__
        BOOL (WINAPI *guarantee)(PULONG) = (BOOL (WINAPI *)(PULONG))GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "SetThreadStackGuarantee");
        if (guarantee) guarantee(&room);
    #else
        SetThreadStackGuarantee(&room);
    #endif
}

static void gs_native_stack_free(void) {}



static uint8_t *gs_os_reserve(size_t usable, size_t size) {
    (void)usable;
    return (uint8_t *)VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
}

static void gs_release_region(const gs_region *r) {
    if (!VirtualFree(r->base, 0, MEM_RELEASE))
        gs_panic("cannot release data stack address space");
}




static void gs_fault_uninstall(void) {
    if (gs_fault_handle) {
        #ifdef __TINYC__
            ULONG (WINAPI *remove)(PVOID) = (ULONG (WINAPI *)(PVOID))GetProcAddress(
                GetModuleHandleA("kernel32.dll"), "RemoveVectoredExceptionHandler");
            if (remove) remove(gs_fault_handle);
        #else
            RemoveVectoredExceptionHandler(gs_fault_handle);
        #endif
    }
    gs_fault_handle = NULL;
}


static int gs_end_first(void) {
    static volatile LONG ended;
    return !InterlockedExchange(&ended, 1);
}
static GS_NORETURN void gs_park(void) {
    for (;;) Sleep(INFINITE);
}

#else  

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>






#define GS_NATIVE_SLOP (64u << 10)
static GS_TLS uintptr_t gs_native_lo, gs_native_hi;

static GS_TLS void *gs_sigstack;
#define GS_SIGSTACK_MIN (64u << 10)


static struct sigaction gs_prev_segv, gs_prev_bus;
static int gs_fault_installed;


static size_t gs_page_size = 0;

static void gs_fault_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    uint8_t *hit = (uint8_t *)info->si_addr;
    for (long i = 0; i < gs_nregions; i++) {
        gs_region *r = &gs_regions[i];
        if ((uintptr_t)hit - (uintptr_t)r->base < r->size) {
            char msg[GS_OVERFLOW_TEXT];
            ssize_t w = write(2, msg, gs_overflow_text(msg, r));
            (void)w;
            _exit(1);
        }
    }
    if ((uintptr_t)hit - gs_native_lo < gs_native_hi - gs_native_lo) {
        static const char msg[] = "faloya runtime error: native call stack overflow\n";
        ssize_t w = write(2, msg, sizeof(msg) - 1);
        (void)w;
        _exit(1);
    }
    

    sigaction(sig, sig == SIGSEGV ? &gs_prev_segv : &gs_prev_bus, NULL);
}


static size_t gs_os_page_size(void) {
    long n = sysconf(_SC_PAGESIZE);
    return n > 0 ? (size_t)n : 4096;
}

static void gs_regions_init(void) {
    gs_page_size = gs_os_page_size();
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = gs_fault_handler;
    
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &gs_prev_segv))
        gs_panic("cannot install data stack fault handler");
    gs_fault_installed = 1;
    #ifdef SIGBUS
        if (sigaction(SIGBUS, &sa, &gs_prev_bus))
            gs_panic("cannot install data stack fault handler");
        gs_fault_installed |= 2;
    #endif
}

#ifdef __linux__


int pthread_getattr_np(pthread_t, pthread_attr_t *);
#endif

static size_t gs_sigstack_size(void) {
    return SIGSTKSZ > GS_SIGSTACK_MIN ? (size_t)SIGSTKSZ : (size_t)GS_SIGSTACK_MIN;
}




static void gs_native_stack_init(void) {
    stack_t ss;
    uintptr_t lo = 0, hi = 0;
    if (!sigaltstack(NULL, &ss) && (ss.ss_flags & SS_DISABLE)) {
        ss.ss_size = gs_sigstack_size();
        ss.ss_sp = malloc(ss.ss_size);
        ss.ss_flags = 0;
        if (ss.ss_sp && !sigaltstack(&ss, NULL)) gs_sigstack = ss.ss_sp;
        else free(ss.ss_sp);
    }
    #if defined(__APPLE__)
        hi = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        lo = hi - pthread_get_stacksize_np(pthread_self());
    #elif defined(__linux__)
        pthread_attr_t attr;
        void *base;
        size_t size;
        if (!pthread_getattr_np(pthread_self(), &attr)) {
            if (!pthread_attr_getstack(&attr, &base, &size)) {
                lo = (uintptr_t)base;
                hi = lo + size;
            }
            pthread_attr_destroy(&attr);
        }
    #endif
    if (lo > GS_NATIVE_SLOP) {
        gs_native_lo = lo - GS_NATIVE_SLOP;
        gs_native_hi = hi;
    }
}



static void gs_native_stack_free(void) {
    stack_t ss;
    if (!gs_sigstack) return;
    memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    ss.ss_size = gs_sigstack_size();
    sigaltstack(&ss, NULL);
    free(gs_sigstack);
    gs_sigstack = NULL;
}





static uint8_t *gs_os_reserve(size_t usable, size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS
                   #ifdef MAP_NORESERVE
                       | MAP_NORESERVE
                   #endif
                   , -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (mprotect((uint8_t *)p + usable, size - usable, PROT_NONE)) {
        munmap(p, size);
        gs_panic("cannot protect data stack guard gap");
    }
    

    #ifdef MADV_NOHUGEPAGE
        madvise(p, usable < (2u << 20) ? usable : (2u << 20), MADV_NOHUGEPAGE);
    #endif
    return (uint8_t *)p;
}

static void gs_release_region(const gs_region *r) {
    if (munmap(r->base, r->size)) gs_panic("cannot release data stack address space");
}


static void gs_fault_uninstall(void) {
    if (gs_fault_installed & 1) sigaction(SIGSEGV, &gs_prev_segv, NULL);
    #ifdef SIGBUS
        if (gs_fault_installed & 2) sigaction(SIGBUS, &gs_prev_bus, NULL);
    #endif
    gs_fault_installed = 0;
}



#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST 5
#endif
static int gs_end_first(void) {
    static int ended;
    return !__atomic_fetch_add(&ended, 1, __ATOMIC_SEQ_CST);
}
static GS_NORETURN void gs_park(void) {
    for (;;) pause();
}

#endif

static GS_NORETURN void gs_end(int code) {
#ifdef GS_HOSTED_JIT
    gs_jit_exit(code);
#else
    if (!gs_end_first()) gs_park();
    fflush(NULL);
    #ifdef __TINYC__
        if (gs_current_thread_id < 0) exit(code);
    #endif
    _Exit(code);
#endif
}


#define GS_REGION_MIN (1u << 20)









GS_API uint8_t *gs_reserve_region(gs_stack *stack) {
    if (gs_nregions >= gs_regions_cap)
        gs_panic("too many data stack regions");
    size_t usable = gs_region_usable;
    uint8_t *p;
    for (;;) {
        p = gs_os_reserve(usable, usable + gs_region_gap);
        if (p) break;
        if (usable <= GS_REGION_MIN) gs_panic("cannot reserve data stack address space");
        usable /= 2;
        usable -= usable % gs_region_page;
        gs_region_usable = usable;
    }
    long i = gs_nregions;
    gs_regions[i].base = p;
    gs_regions[i].usable = usable;
    gs_regions[i].size = usable + gs_region_gap;
    gs_regions[i].stack = stack;
    gs_regions[i].committed = p;
    gs_nregions = i + 1;  
    return p;
}








static int gs_discard_pages(uintptr_t start, size_t len) {
#ifdef _WIN32
    return VirtualFree((void *)start, len, MEM_DECOMMIT) != 0;
#elif defined(__APPLE__)
    return mmap((void *)start, len, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
#else
    

    return madvise((void *)start, len, MADV_DONTNEED) == 0;
#endif
}





















GS_API uint8_t gs_trim_memory(void) {
    uintptr_t page = (uintptr_t)gs_page_size;
    int ok = 1;
    for (long i = 0; i < gs_nregions; i++) {
        gs_region *r = &gs_regions[i];
        uintptr_t base = (uintptr_t)r->base, top = (uintptr_t)r->stack->top;
        

        if (top < base || top - base > r->usable)
            gs_panic("data stack top outside its region in trim_memory");
        uintptr_t start = (top + page - 1) / page * page;
        uintptr_t end = (base + r->usable) / page * page;
#ifdef _WIN32
        

        if ((uintptr_t)r->committed < end) end = (uintptr_t)r->committed;
#endif
        if (start >= end) continue;
        if (!gs_discard_pages(start, (size_t)(end - start))) {
            ok = 0;
            continue;
        }
#ifdef _WIN32
        
        r->committed = (uint8_t *)start;
#endif
    }
    return (uint8_t)ok;
}

















#define GS_SPAN_LEAF ((GS_SPAN_NODE - 16) / 16)
#define GS_SPAN_FAN ((GS_SPAN_NODE - 16) / 24)



#define GS_SPAN_DEPTH 24


typedef struct { int64_t n, link, idx[GS_SPAN_LEAF], cnt[GS_SPAN_LEAF]; } gs_spleaf;
typedef struct { int64_t n, link, key[GS_SPAN_FAN], max[GS_SPAN_FAN], child[GS_SPAN_FAN]; } gs_spinner;
typedef struct { int64_t root, height, chain; } gs_sphead;



typedef struct {
    uint8_t *base;
    int64_t *n;
    uint8_t **top;
    int64_t h, node[GS_SPAN_DEPTH], pos[GS_SPAN_DEPTH];
} gs_sppath;

#define GS_SPHEAD(p) ((gs_sphead *)(p)->base)
#define GS_SPLEAF(p, i) ((gs_spleaf *)((p)->base + (i) * GS_SPAN_NODE))
#define GS_SPINNER(p, i) ((gs_spinner *)((p)->base + (i) * GS_SPAN_NODE))

static int64_t gs_sp_newnode(gs_sppath *p) {
    int64_t i = GS_SPHEAD(p)->chain;
    if (i) {
        GS_SPHEAD(p)->chain = GS_SPLEAF(p, i)->link;
        return i;
    }
    *p->top += GS_SPAN_NODE;
    return (*p->n)++;
}

static void gs_sp_dropnode(gs_sppath *p, int64_t i) {
    GS_SPLEAF(p, i)->link = GS_SPHEAD(p)->chain;
    GS_SPHEAD(p)->chain = i;
}





static void gs_sp_open(gs_spleaf *lf, int64_t s) {
    int64_t j;
    for (j = lf->n; j > s; j--) {
        lf->idx[j] = lf->idx[j - 1];
        lf->cnt[j] = lf->cnt[j - 1];
    }
    lf->n++;
}

static void gs_sp_close(gs_spleaf *lf, int64_t s) {
    int64_t j;
    lf->n--;
    for (j = s; j < lf->n; j++) {
        lf->idx[j] = lf->idx[j + 1];
        lf->cnt[j] = lf->cnt[j + 1];
    }
}

static void gs_sp_openin(gs_spinner *in, int64_t s) {
    int64_t j;
    for (j = in->n; j > s; j--) {
        in->key[j] = in->key[j - 1];
        in->max[j] = in->max[j - 1];
        in->child[j] = in->child[j - 1];
    }
    in->n++;
}

static void gs_sp_closein(gs_spinner *in, int64_t s) {
    int64_t j;
    in->n--;
    for (j = s; j < in->n; j++) {
        in->key[j] = in->key[j + 1];
        in->max[j] = in->max[j + 1];
        in->child[j] = in->child[j + 1];
    }
}


static int64_t gs_sp_most(const int64_t *v, int64_t w, int64_t stop) {
    int64_t m = 0, k;
    for (k = 0; k < w && m < stop; k++)
        if (v[k] > m) m = v[k];
    return m;
}



static int64_t gs_sp_max(gs_sppath *p, int64_t i, int leaf, int64_t stop) {
    return leaf ? gs_sp_most(GS_SPLEAF(p, i)->cnt, GS_SPLEAF(p, i)->n, stop)
                : gs_sp_most(GS_SPINNER(p, i)->max, GS_SPINNER(p, i)->n, stop);
}

static int64_t gs_sp_key(gs_sppath *p, int64_t i, int leaf) {
    return leaf ? GS_SPLEAF(p, i)->idx[0] : GS_SPINNER(p, i)->key[0];
}








static void gs_sp_fix(gs_sppath *p, int64_t l, int64_t was, int64_t now) {
    for (; l > 0; l--) {
        gs_spinner *in = GS_SPINNER(p, p->node[l - 1]);
        int64_t k = p->pos[l - 1], c = p->node[l], m = in->max[k];
        int leaf = l == p->h;
        int64_t key = gs_sp_key(p, c, leaf);
        int64_t nm = now >= m ? now : was < m ? m : gs_sp_max(p, c, leaf, was);
        if (in->key[k] == key && nm == m) return;
        in->key[k] = key;
        in->max[k] = nm;
        was = m;
        now = nm;
    }
}



static int64_t gs_sp_find(gs_sppath *p, int64_t idx) {
    int64_t l, i = GS_SPHEAD(p)->root, k;
    gs_spleaf *lf;
    p->h = GS_SPHEAD(p)->height;
    for (l = 0; l < p->h; l++) {
        gs_spinner *in = GS_SPINNER(p, i);
        for (k = 1; k < in->n && in->key[k] <= idx; k++) {}
        p->node[l] = i;
        p->pos[l] = k - 1;
        i = in->child[k - 1];
    }
    p->node[p->h] = i;
    lf = GS_SPLEAF(p, i);
    for (k = 0; k < lf->n && lf->idx[k] <= idx; k++) {}
    p->pos[p->h] = k - 1;
    return k - 1;
}


static void gs_sp_newroot(gs_sppath *p, int64_t a, int64_t b, int leaf) {
    int64_t r = gs_sp_newnode(p);
    gs_spinner *rn = GS_SPINNER(p, r);
    rn->n = 2;
    rn->key[0] = gs_sp_key(p, a, leaf);
    rn->max[0] = gs_sp_max(p, a, leaf, INT64_MAX);
    rn->child[0] = a;
    rn->key[1] = gs_sp_key(p, b, leaf);
    rn->max[1] = gs_sp_max(p, b, leaf, INT64_MAX);
    rn->child[1] = b;
    GS_SPHEAD(p)->root = r;
    GS_SPHEAD(p)->height++;
}



static void gs_sp_addchild(gs_sppath *p, int64_t l, int64_t k, int64_t child, int leaf) {
    for (;;) {
        gs_spinner *in = GS_SPINNER(p, p->node[l]), *o = in;
        int64_t key = gs_sp_key(p, child, leaf), max = gs_sp_max(p, child, leaf, INT64_MAX);
        int64_t s = k, nn = 0;
        if (in->n == GS_SPAN_FAN) {
            int64_t half = GS_SPAN_FAN / 2;
            gs_spinner *r;
            nn = gs_sp_newnode(p);
            r = GS_SPINNER(p, nn);
            r->n = in->n - half;
            memcpy(r->key, in->key + half, (size_t)r->n * 8);
            memcpy(r->max, in->max + half, (size_t)r->n * 8);
            memcpy(r->child, in->child + half, (size_t)r->n * 8);
            in->n = half;
            if (k > half) {
                o = r;
                s = k - half;
            }
        }
        gs_sp_openin(o, s);
        o->key[s] = key;
        o->max[s] = max;
        o->child[s] = child;
        if (!nn) {
            gs_sp_fix(p, l, 0, max);
            return;
        }
        if (l == 0) {
            gs_sp_newroot(p, p->node[0], nn, 0);
            return;
        }
        

        gs_sp_fix(p, l, INT64_MAX, 0);
        child = nn;
        leaf = 0;
        k = p->pos[l - 1] + 1;
        l--;
    }
}


static void gs_sp_insert(gs_sppath *p, int64_t k, int64_t idx, int64_t cnt) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]), *o = lf;
    int64_t s = k, nn = 0;
    if (lf->n == GS_SPAN_LEAF) {
        int64_t half = GS_SPAN_LEAF / 2;
        gs_spleaf *r;
        nn = gs_sp_newnode(p);
        r = GS_SPLEAF(p, nn);
        r->n = lf->n - half;
        memcpy(r->idx, lf->idx + half, (size_t)r->n * 8);
        memcpy(r->cnt, lf->cnt + half, (size_t)r->n * 8);
        lf->n = half;
        if (k > half) {
            o = r;
            s = k - half;
        }
    }
    gs_sp_open(o, s);
    o->idx[s] = idx;
    o->cnt[s] = cnt;
    if (!nn) {
        gs_sp_fix(p, p->h, 0, cnt);
    } else if (p->h == 0) {
        gs_sp_newroot(p, p->node[0], nn, 1);
    } else {
        gs_sp_fix(p, p->h, INT64_MAX, 0);
        gs_sp_addchild(p, p->h - 1, p->pos[p->h - 1] + 1, nn, 1);
    }
}



static void gs_sp_delete(gs_sppath *p, int64_t k) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k], l;
    gs_sp_close(lf, k);
    if (lf->n > 0 || p->h == 0) {
        gs_sp_fix(p, p->h, c, 0);
        return;
    }
    gs_sp_dropnode(p, p->node[p->h]);
    for (l = p->h - 1;; l--) {
        gs_spinner *in = GS_SPINNER(p, p->node[l]);
        int64_t e = p->pos[l], m = in->max[e];
        gs_sp_closein(in, e);
        if (in->n > 0) {
            gs_sp_fix(p, l, m, 0);
            break;
        }
        

        gs_sp_dropnode(p, p->node[l]);
    }
    while (GS_SPHEAD(p)->height > 0 && GS_SPINNER(p, GS_SPHEAD(p)->root)->n == 1) {
        int64_t r = GS_SPHEAD(p)->root;
        GS_SPHEAD(p)->root = GS_SPINNER(p, r)->child[0];
        GS_SPHEAD(p)->height--;
        gs_sp_dropnode(p, r);
    }
}



static void gs_sp_take(gs_sppath *p, int64_t k, int64_t cnt) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k];
    if (c <= cnt) {
        gs_sp_delete(p, k);
        return;
    }
    lf->idx[k] += cnt;
    lf->cnt[k] = c - cnt;
    gs_sp_fix(p, p->h, c, c - cnt);
}






GS_API int64_t gs_spans_alloc(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                              int64_t cnt) {
    gs_sppath p;
    int64_t l, i, k = 0, j, m, c, now, idx, before[GS_SPAN_DEPTH];
    gs_spleaf *lf = NULL;
    if (cnt == 0 || *n == 0) return len;
    p.base = base;
    p.n = n;
    p.top = top;
    p.h = GS_SPHEAD(&p)->height;
    i = GS_SPHEAD(&p)->root;
    



    for (l = 0; l < p.h; l++) {
        gs_spinner *in = GS_SPINNER(&p, i);
        for (k = 0, m = 0; k < in->n && in->max[k] < cnt; k++)
            if (in->max[k] > m) m = in->max[k];
        if (k == in->n) break;
        p.node[l] = i;
        p.pos[l] = k;
        before[l] = m;
        i = in->child[k];
    }
    if (l == p.h) {
        lf = GS_SPLEAF(&p, i);
        for (k = 0, m = 0; k < lf->n && lf->cnt[k] < cnt; k++)
            if (lf->cnt[k] > m) m = lf->cnt[k];
        before[l] = m;
    }
    if (l < p.h || k == lf->n) {
        for (l = 0, i = GS_SPHEAD(&p)->root; l < p.h; l++) {
            gs_spinner *in = GS_SPINNER(&p, i);
            p.node[l] = i;
            p.pos[l] = in->n - 1;
            i = in->child[in->n - 1];
        }
        p.node[p.h] = i;
        lf = GS_SPLEAF(&p, i);
        k = lf->n - 1;
        if (k < 0 || lf->idx[k] + lf->cnt[k] != len) return len;
        idx = lf->idx[k];
        gs_sp_take(&p, k, cnt);
        return idx;
    }
    p.node[p.h] = i;
    p.pos[p.h] = k;
    idx = lf->idx[k];
    c = lf->cnt[k];
    if (c > cnt) {
        lf->idx[k] = idx + cnt;
        lf->cnt[k] = now = c - cnt;
        j = k + 1;
    } else if (lf->n > 1 || p.h == 0) {
        gs_sp_close(lf, k);
        now = 0;
        j = k;
    } else {
        gs_sp_delete(&p, k);
        return idx;
    }
    



    for (l = p.h; l > 0; l--) {
        gs_spinner *in = GS_SPINNER(&p, p.node[l - 1]);
        int64_t e = p.pos[l - 1], mx = in->max[e], nm = mx;
        int64_t key = gs_sp_key(&p, p.node[l], l == p.h);
        if (c == mx) {
            const int64_t *v = l == p.h ? lf->cnt : GS_SPINNER(&p, p.node[l])->max;
            int64_t w = l == p.h ? lf->n : GS_SPINNER(&p, p.node[l])->n;
            nm = before[l] > now ? before[l] : now;
            for (; j < w && nm < c; j++)
                if (v[j] > nm) nm = v[j];
        }
        if (in->key[e] == key && nm == mx) break;
        in->key[e] = key;
        in->max[e] = nm;
        c = mx;
        now = nm;
        j = e + 1;
    }
    return idx;
}







typedef struct {
    gs_spleaf *lf, *nl;
    int64_t k, nk;
    gs_sppath *np;
} gs_spnear;

static void gs_sp_near(gs_sppath *p, gs_sppath *q, int64_t idx, int64_t end, gs_spnear *s) {
    int64_t l;
    s->k = gs_sp_find(p, idx);
    s->lf = GS_SPLEAF(p, p->node[p->h]);
    s->nl = NULL;
    s->nk = -1;
    s->np = p;
    if (s->k + 1 < s->lf->n) {
        s->nl = s->lf;
        s->nk = s->k + 1;
        return;
    }
    for (l = p->h - 1; l >= 0 && p->pos[l] + 1 >= GS_SPINNER(p, p->node[l])->n; l--) {}
    if (l < 0 || GS_SPINNER(p, p->node[l])->key[p->pos[l] + 1] != end) return;
    q->base = p->base;
    q->n = p->n;
    q->top = p->top;
    q->h = p->h;
    memcpy(q->node, p->node, (size_t)(l + 1) * 8);
    memcpy(q->pos, p->pos, (size_t)(l + 1) * 8);
    for (q->pos[l]++; l < q->h; l++) {
        q->node[l + 1] = GS_SPINNER(q, q->node[l])->child[q->pos[l]];
        q->pos[l + 1] = 0;
    }
    s->np = q;
    s->nl = GS_SPLEAF(q, q->node[q->h]);
    s->nk = 0;
}



static void gs_sp_release(gs_sppath *p, gs_spnear *s, int64_t idx, int64_t cnt) {
    gs_spleaf *lf = s->lf, *nl = s->nl;
    int64_t k = s->k, nk = s->nk;
    int prev = k >= 0 && lf->idx[k] + lf->cnt[k] == idx;
    int next = nl && idx + cnt == nl->idx[nk];
    if (prev) {
        int64_t c = lf->cnt[k], m = c + cnt + (next ? nl->cnt[nk] : 0);
        lf->cnt[k] = m;
        

        if (next && nl == lf) gs_sp_close(lf, nk);
        gs_sp_fix(p, p->h, c, m);
        if (next && nl != lf) gs_sp_delete(s->np, nk);
    } else if (next) {
        int64_t c = nl->cnt[nk];
        nl->idx[nk] = idx;
        nl->cnt[nk] = c + cnt;
        gs_sp_fix(s->np, s->np->h, c, c + cnt);
    } else {
        gs_sp_insert(p, k + 1, idx, cnt);
    }
}


GS_API void gs_spans_free(uint8_t *base, int64_t *n, uint8_t **top, int64_t idx, int64_t cnt) {
    gs_sppath p, q;
    gs_spnear s;
    if (cnt <= 0) return;
    p.base = base;
    p.n = n;
    p.top = top;
    if (*n == 0) {
        gs_sphead *hd = (gs_sphead *)base;
        *n = 1;
        *top = base + GS_SPAN_NODE;
        hd->chain = 0;
        hd->height = 0;
        hd->root = gs_sp_newnode(&p);
        GS_SPLEAF(&p, hd->root)->n = 0;
    }
    gs_sp_near(&p, &q, idx, idx + cnt, &s);
    gs_sp_release(&p, &s, idx, cnt);
}







GS_API int64_t gs_spans_regrow(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                               int64_t idx, int64_t ol, int64_t cnt) {
    gs_sppath p, q;
    gs_spnear s;
    int64_t end = idx + ol;
    if (*n == 0) {
        gs_spans_free(base, n, top, idx, ol);
        return gs_spans_alloc(base, n, top, len, cnt);
    }
    p.base = base;
    p.n = n;
    p.top = top;
    gs_sp_near(&p, &q, idx, end, &s);
    if (s.nl && s.nl->idx[s.nk] == end &&
        (s.nl->cnt[s.nk] >= cnt - ol || end + s.nl->cnt[s.nk] == len)) {
        gs_sp_take(s.np, s.nk, cnt - ol);
        return idx;
    }
    gs_sp_release(&p, &s, idx, ol);
    return gs_spans_alloc(base, n, top, len, cnt);
}


























#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define GS_MAY_ALIAS __attribute__((may_alias))
#else
#define GS_MAY_ALIAS
#endif
#pragma pack(push, 1)
typedef struct {
    uint8_t members[256];
    uint8_t kind, invert, bytes[3], lo[16], hi[16];
} GS_MAY_ALIAS gs_byteset;
#pragma pack(pop)

static int64_t gs_scan_any_bytes(const uint8_t *p, int64_t n, const gs_byteset *s) {
    if (s->kind == 1 && !s->invert) {
        const uint8_t *q = (const uint8_t *)memchr(p, s->bytes[0], (size_t)n);
        return q ? (int64_t)(q - p) : -1;
    }
    for (int64_t i = 0; i < n; i++)
        if (s->members[p[i]]) return i;
    return -1;
}


static int64_t gs_scan_pair_bytes(const uint8_t *p, int64_t np, const gs_byteset *a, int64_t d,
                                  const gs_byteset *b) {
    for (int64_t i = 0; i < np; i++)
        if (a->members[p[i]] & b->members[p[i + d]]) return i;
    return -1;
}

#if !defined(__TINYC__) && (defined(__x86_64__) || defined(_M_X64))
#define GS_SCAN_SSE2 1
#include <emmintrin.h>
#include <tmmintrin.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__GNUC__) || defined(__clang__)

#define GS_SSSE3 __attribute__((target("ssse3")))
#define gs_ctz32(x) __builtin_ctz(x)
#else
#define GS_SSSE3
static int gs_ctz32(unsigned x) {
    unsigned long i;
    _BitScanForward(&i, x);
    return (int)i;
}
#endif

static int gs_cpu_ssse3;

static void gs_scan_init(void) {
#ifdef _WIN32
    int r[4];
    __cpuid(r, 1);
    gs_cpu_ssse3 = (r[2] >> 9) & 1;
#else
    unsigned a, b, c, d;
    gs_cpu_ssse3 = __get_cpuid(1, &a, &b, &c, &d) && ((c >> 9) & 1);
#endif
}



typedef struct { __m128i a, b, c, flip; } gs_bsm;

static GS_INLINE void gs_bsm_init(gs_bsm *m, const gs_byteset *s, int tables) {
    if (tables) {
        m->a = _mm_loadu_si128((const __m128i *)s->lo);
        m->b = _mm_loadu_si128((const __m128i *)s->hi);
        m->c = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
        m->flip = _mm_setzero_si128();
    } else {
        m->a = _mm_set1_epi8((char)s->bytes[0]);
        m->b = _mm_set1_epi8((char)s->bytes[1]);
        m->c = _mm_set1_epi8((char)s->bytes[2]);
        m->flip = s->invert ? _mm_set1_epi8(-1) : _mm_setzero_si128();
    }
}




static GS_INLINE __m128i gs_bsm_test(const gs_bsm *m, int k, __m128i x) {
    __m128i r, t;
    if (k == 1) {
        r = _mm_cmpeq_epi8(x, m->a);
    } else if (k == 2) {
        r = _mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b));
    } else if (k == 3) {
        r = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b)),
                         _mm_cmpeq_epi8(x, m->c));
    } else {
        t = _mm_sub_epi8(x, m->a);
        r = _mm_cmpeq_epi8(_mm_min_epu8(t, m->b), t);
    }
    return _mm_xor_si128(r, m->flip);
}




static GS_SSSE3 GS_INLINE __m128i gs_bsm_tables(const gs_bsm *m, __m128i x) {
    __m128i lo = _mm_shuffle_epi8(m->a, x);
    __m128i hi = _mm_shuffle_epi8(m->b, _mm_xor_si128(x, _mm_set1_epi8((char)0x80)));
    __m128i bit = _mm_shuffle_epi8(m->c, _mm_and_si128(_mm_srli_epi16(x, 4), _mm_set1_epi8(7)));
    return _mm_cmpeq_epi8(_mm_and_si128(_mm_or_si128(lo, hi), bit), bit);
}

#define GS_LOAD(q) _mm_loadu_si128((const __m128i *)(q))



#define GS_ANY_LOOP(TEST)                                                              \
    int64_t i = 0;                                                                     \
    unsigned k;                                                                        \
    for (; i + 16 <= n; i += 16) {                                                     \
        k = (unsigned)_mm_movemask_epi8(TEST(GS_LOAD(p + i)));                         \
        if (k) return i + gs_ctz32(k);                                                 \
    }                                                                                  \
    if (i == n) return -1;                                                             \
    k = (unsigned)_mm_movemask_epi8(TEST(GS_LOAD(p + n - 16))) >> (16 - (n - i));      \
    return k ? i + gs_ctz32(k) : -1;


#define GS_PAIR_LOOP(TESTA, TESTB)                                                     \
    int64_t i = 0;                                                                     \
    unsigned k;                                                                        \
    for (; i + 16 <= np; i += 16) {                                                    \
        k = (unsigned)_mm_movemask_epi8(                                               \
            _mm_and_si128(TESTA(GS_LOAD(p + i)), TESTB(GS_LOAD(p + i + d))));          \
        if (k) return i + gs_ctz32(k);                                                 \
    }                                                                                  \
    if (i == np) return -1;                                                            \
    k = (unsigned)_mm_movemask_epi8(_mm_and_si128(TESTA(GS_LOAD(p + np - 16)),         \
                                                  TESTB(GS_LOAD(p + np - 16 + d))))    \
        >> (16 - (np - i));                                                            \
    return k ? i + gs_ctz32(k) : -1;

#define GS_TEST_S(x) gs_bsm_test(&ms, ks, x)
#define GS_TEST_A(x) gs_bsm_test(&ma, ka, x)
#define GS_TEST_B(x) gs_bsm_test(&mb, kb, x)
#define GS_TABLES_S(x) gs_bsm_tables(&ms, x)
#define GS_TABLES_A(x) gs_bsm_tables(&ma, x)
#define GS_TABLES_B(x) gs_bsm_tables(&mb, x)

static GS_INLINE int64_t gs_scan_any_k(const uint8_t *p, int64_t n, const gs_byteset *s, int ks) {
    gs_bsm ms;
    gs_bsm_init(&ms, s, 0);
    GS_ANY_LOOP(GS_TEST_S)
}

static GS_SSSE3 int64_t gs_scan_any_tables(const uint8_t *p, int64_t n, const gs_byteset *s) {
    gs_bsm ms;
    gs_bsm_init(&ms, s, 1);
    GS_ANY_LOOP(GS_TABLES_S)
}

static GS_INLINE int64_t gs_scan_pair_k(const uint8_t *p, int64_t np, const gs_byteset *a,
                                        int64_t d, const gs_byteset *b, int ka, int kb) {
    gs_bsm ma, mb;
    gs_bsm_init(&ma, a, 0);
    gs_bsm_init(&mb, b, 0);
    GS_PAIR_LOOP(GS_TEST_A, GS_TEST_B)
}

static GS_SSSE3 int64_t gs_scan_pair_tables(const uint8_t *p, int64_t np, const gs_byteset *a,
                                            int64_t d, const gs_byteset *b) {
    gs_bsm ma, mb;
    gs_bsm_init(&ma, a, 1);
    gs_bsm_init(&mb, b, 1);
    GS_PAIR_LOOP(GS_TABLES_A, GS_TABLES_B)
}


static int64_t gs_scan_any_blocks(const uint8_t *p, int64_t n, const gs_byteset *s) {
    switch (s->kind) {
        case 1: return gs_scan_any_k(p, n, s, 1);
        case 2: return gs_scan_any_k(p, n, s, 2);
        case 3: return gs_scan_any_k(p, n, s, 3);
        default: return gs_scan_any_k(p, n, s, 4);
    }
}

#define GS_PAIR_CASE(ka, kb) \
    case (ka) * 4 + (kb): return gs_scan_pair_k(p, np, a, d, b, ka, kb);

static int64_t gs_scan_pair_blocks(const uint8_t *p, int64_t np, const gs_byteset *a, int64_t d,
                                   const gs_byteset *b) {
    switch (a->kind * 4 + b->kind) {
        GS_PAIR_CASE(1, 1) GS_PAIR_CASE(1, 2) GS_PAIR_CASE(1, 3) GS_PAIR_CASE(1, 4)
        GS_PAIR_CASE(2, 1) GS_PAIR_CASE(2, 2) GS_PAIR_CASE(2, 3) GS_PAIR_CASE(2, 4)
        GS_PAIR_CASE(3, 1) GS_PAIR_CASE(3, 2) GS_PAIR_CASE(3, 3) GS_PAIR_CASE(3, 4)
        GS_PAIR_CASE(4, 1) GS_PAIR_CASE(4, 2) GS_PAIR_CASE(4, 3)
        default: return gs_scan_pair_k(p, np, a, d, b, 4, 4);
    }
}

#else
static void gs_scan_init(void) {}
#endif

GS_API int64_t gs_scan_any(const uint8_t *p, int64_t n, const void *set) {
    const gs_byteset *s = (const gs_byteset *)set;
    if (n <= 0) return -1;
    if (s->kind == 0) return s->invert ? 0 : -1;
#ifdef GS_SCAN_SSE2
    if (n >= 16 && s->kind != 5) return gs_scan_any_blocks(p, n, s);
    if (n >= 16 && gs_cpu_ssse3) return gs_scan_any_tables(p, n, s);
#endif
    return gs_scan_any_bytes(p, n, s);
}

GS_API int64_t gs_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b) {
    const gs_byteset *sa = (const gs_byteset *)a, *sb = (const gs_byteset *)b;
    if (d < 0 || d >= n) return -1;
    int64_t np = n - d;     
    
    if (sa->kind == 0) return sa->invert ? gs_scan_any(p + d, np, sb) : -1;
    if (sb->kind == 0) return sb->invert ? gs_scan_any(p, np, sa) : -1;
#ifdef GS_SCAN_SSE2
    if (np >= 16 && sa->kind != 5 && sb->kind != 5) return gs_scan_pair_blocks(p, np, sa, d, sb);
    if (np >= 16 && gs_cpu_ssse3) return gs_scan_pair_tables(p, np, sa, d, sb);
#endif
    return gs_scan_pair_bytes(p, np, sa, d, sb);
}

GS_API void gs_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions) {
    


    if (gs_regions)
        gs_panic("a program is already started in this process: call its faloya_exit first");
    gs_argc = argc;
    gs_argv = argv;
    



    gs_region_page = gs_os_page_size();
    reserve -= reserve % gs_region_page;
    if (!reserve) gs_panic("the data stack reservation (GS_STACK_RESERVE) is less than a page");
    gs_region_usable = gs_region_reserve = (size_t)reserve;
    gs_region_gap = (size_t)gap;
    gs_stack_budget = budget;
    gs_mainregions = mainregions;
    gs_workerregions = workerregions;
    



    if (workerregions > 0) {
        uint64_t per = reserve + gap;
        uint64_t total = per ? budget / per : 0;
        int64_t spare = total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
        spare -= mainregions;
        gs_thread_cap = spare > workerregions ? spare / workerregions : 1;
    }
    
    
    
    setvbuf(stdout, NULL, _IONBF, 0);
    #ifdef _WIN32
        
        
        
        
        
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    #endif
    gs_regions_init();
    gs_regions_begin(mainregions);
    gs_native_stack_init();
    gs_scan_init();
}





static void gs_rt_release(void) {
    gs_release_regions();
    gs_fault_uninstall();
    gs_argc = 0;
    gs_argv = NULL;
}








static const char gs_digit_pairs[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

static int gs_fmt_digits(uint64_t v) {
    int n = 1;
    for (;;) {
        if (v < 10) return n;
        if (v < 100) return n + 1;
        if (v < 1000) return n + 2;
        if (v < 10000) return n + 3;
        v /= 10000;
        n += 4;
    }
}

GS_API int64_t gs_fmt_u64(uint8_t *dst, uint64_t v) {
    int n = gs_fmt_digits(v);
    uint8_t *p = dst + n;
    while (v >= 100) {
        unsigned r = (unsigned)(v % 100);
        v /= 100;
        p -= 2;
        p[0] = (uint8_t)gs_digit_pairs[2 * r];
        p[1] = (uint8_t)gs_digit_pairs[2 * r + 1];
    }
    if (v >= 10) {
        p[-2] = (uint8_t)gs_digit_pairs[2 * v];
        p[-1] = (uint8_t)gs_digit_pairs[2 * v + 1];
    } else {
        p[-1] = (uint8_t)('0' + v);
    }
    return n;
}

GS_API int64_t gs_fmt_i64(uint8_t *dst, int64_t v) {
    if (v >= 0) return gs_fmt_u64(dst, (uint64_t)v);
    
    dst[0] = '-';
    return 1 + gs_fmt_u64(dst + 1, 0 - (uint64_t)v);
}













typedef struct { int n; uint32_t d[36]; } gs_big;


static void gs_big_set(gs_big *a, uint64_t v, int sh) {
    int w = sh >> 5, b = sh & 31, i;
    uint64_t lo = v << b, hi = b ? v >> (64 - b) : 0;
    for (i = 0; i < w; i++) a->d[i] = 0;
    a->d[w] = (uint32_t)lo;
    a->d[w + 1] = (uint32_t)(lo >> 32);
    a->d[w + 2] = (uint32_t)hi;
    a->n = w + 3;
    while (!a->d[a->n - 1]) a->n--;
}

static void gs_big_mul(gs_big *a, uint32_t m) {
    uint64_t c = 0;
    for (int i = 0; i < a->n; i++) {
        c += (uint64_t)a->d[i] * m;
        a->d[i] = (uint32_t)c;
        c >>= 32;
    }
    if (c) a->d[a->n++] = (uint32_t)c;
}

static void gs_big_mul_pow10(gs_big *a, int k) {
    for (; k >= 9; k -= 9) gs_big_mul(a, 1000000000u);
    for (; k > 0; k--) gs_big_mul(a, 10);
}

static int gs_big_cmp(const gs_big *a, const gs_big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}


static void gs_big_add(gs_big *t, const gs_big *a, const gs_big *b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t c = 0;
    for (int i = 0; i < n; i++) {
        c += (uint64_t)(i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        t->d[i] = (uint32_t)c;
        c >>= 32;
    }
    t->n = n;
    if (c) t->d[t->n++] = (uint32_t)c;
}


static void gs_big_sub(gs_big *a, const gs_big *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t x = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        a->d[i] = (uint32_t)x;
        borrow = x >> 63;
    }
    while (a->n && !a->d[a->n - 1]) a->n--;
}




static int gs_float_digits(uint64_t f, int e, int bits, int emin, char *dig, int *k) {
    gs_big r, s, mp, mlo, t;
    

    int even = !(f & 1);
    
    int asym = f == (uint64_t)1 << (bits - 1) && e > emin;
    int up = e > 0 ? e : 0, lg = e - 1, n = 0, c;
    

    gs_big *mm = asym ? &mlo : &mp;
    gs_big_set(&r, f, up + 1 + asym);
    gs_big_set(&s, 1, up - e + 1 + asym);
    gs_big_set(&mp, 1, up + asym);
    gs_big_set(&mlo, 1, up);
    


    for (uint64_t g = f; g; g >>= 1) lg++;
    *k = lg > 0 ? ((lg * 78913) >> 18) + 1 : -((-lg * 78913) >> 18);
    if (*k >= 0) gs_big_mul_pow10(&s, *k);
    else {
        gs_big_mul_pow10(&r, -*k);
        gs_big_mul_pow10(&mp, -*k);
        if (asym) gs_big_mul_pow10(&mlo, -*k);
    }
    gs_big_add(&t, &r, &mp);
    c = gs_big_cmp(&t, &s);
    if (even ? c >= 0 : c > 0) {
        gs_big_mul(&s, 10);
        ++*k;
    }
    

    for (;;) {
        int d = 0, low, high;
        gs_big_mul(&r, 10);
        gs_big_mul(&mp, 10);
        if (asym) gs_big_mul(&mlo, 10);
        while (gs_big_cmp(&r, &s) >= 0) {
            gs_big_sub(&r, &s);
            d++;
        }
        c = gs_big_cmp(&r, mm);
        low = even ? c <= 0 : c < 0;
        gs_big_add(&t, &r, &mp);
        c = gs_big_cmp(&t, &s);
        high = even ? c >= 0 : c > 0;
        if (!low && !high) {
            dig[n++] = (char)('0' + d);
            continue;
        }
        if (low && high) {
            

            gs_big_add(&t, &r, &r);
            c = gs_big_cmp(&t, &s);
            high = c > 0 || (c == 0 && (d & 1));
        }
        dig[n++] = (char)('0' + d + high);
        return n;
    }
}






static int64_t gs_fmt_float(uint8_t *dst, int neg, uint64_t frac, int bexp, int bits,
                            int emin) {
    char dig[20], *p = (char *)dst;
    int k, n, x, i;
    if (neg) *p++ = '-';
    if (!frac && !bexp) {
        memcpy(p, "0.0", 3);
        return p + 3 - (char *)dst;
    }
    n = gs_float_digits(bexp ? frac | (uint64_t)1 << (bits - 1) : frac,
                        (bexp ? bexp - 1 : 0) + emin, bits, emin, dig, &k);
    x = k - 1;
    if (x < -4 || x >= (n > 15 ? n : 15)) {
        *p++ = dig[0];
        if (n > 1) *p++ = '.';
        for (i = 1; i < n; i++) *p++ = dig[i];
        *p++ = 'e';
        *p++ = x < 0 ? '-' : '+';
        if (x < 0) x = -x;
        if (x >= 100) *p++ = (char)('0' + x / 100);
        *p++ = (char)('0' + x / 10 % 10);
        *p++ = (char)('0' + x % 10);
    } else if (x < 0) {
        *p++ = '0';
        *p++ = '.';
        for (i = -1; i > x; i--) *p++ = '0';
        for (i = 0; i < n; i++) *p++ = dig[i];
    } else {
        for (i = 0; i < n || i <= x; i++) {
            if (i == x + 1) *p++ = '.';
            *p++ = i < n ? dig[i] : '0';
        }
        
        if (n <= x + 1) {
            *p++ = '.';
            *p++ = '0';
        }
    }
    return p - (char *)dst;
}

GS_API int64_t gs_fmt_f64(uint8_t *dst, double v) {
    uint64_t b;
    


    if (v != v) { memcpy(dst, "nan", 3); return 3; }
    if (isinf(v)) {
        if (v > 0) { memcpy(dst, "inf", 3); return 3; }
        memcpy(dst, "-inf", 4);
        return 4;
    }
    memcpy(&b, &v, sizeof b);
    return gs_fmt_float(dst, (int)(b >> 63), b & (((uint64_t)1 << 52) - 1),
                        (int)(b >> 52) & 0x7FF, 53, -1074);
}



GS_API int64_t gs_fmt_f32(uint8_t *dst, float v) {
    double d = v;
    uint32_t b;
    if (d != d || isinf(d)) return gs_fmt_f64(dst, d);
    memcpy(&b, &v, sizeof b);
    return gs_fmt_float(dst, (int)(b >> 31), b & 0x7FFFFF, (int)(b >> 23) & 0xFF, 24, -149);
}

GS_API int64_t gs_fmt_bool(uint8_t *dst, int64_t v) {
    memcpy(dst, v ? "true" : "false", v ? 4 : 5);
    return v ? 4 : 5;
}

GS_API int64_t gs_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n) {
    uint8_t *d = dst;
    *d++ = '"';
    for (int64_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        switch (c) {
            case '"': *d++ = '\\'; *d++ = '"'; break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n'; break;
            case '\r': *d++ = '\\'; *d++ = 'r'; break;
            case '\t': *d++ = '\\'; *d++ = 't'; break;
            default: *d++ = c; break;
        }
    }
    *d++ = '"';
    return (int64_t)(d - dst);
}


GS_API void gs_out_bytes(const uint8_t *p, int64_t len) { fwrite(p, 1, (size_t)len, stdout); }
