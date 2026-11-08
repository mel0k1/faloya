





























#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#error "Faloya requires Windows Vista or later (_WIN32_WINNT >= 0x0600)"
#endif
#endif



#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif





#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#ifndef GS_EMBEDDED_JIT
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#endif






#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define GS_ALIGN1 __attribute__((aligned(1)))
#else
#define GS_ALIGN1
#endif
typedef int16_t gs_i16 GS_ALIGN1;
typedef int32_t gs_i32 GS_ALIGN1;
typedef int64_t gs_i64 GS_ALIGN1;
typedef uint16_t gs_u16 GS_ALIGN1;
typedef uint32_t gs_u32 GS_ALIGN1;
typedef uint64_t gs_u64 GS_ALIGN1;
typedef float gs_f32 GS_ALIGN1;
typedef double gs_f64 GS_ALIGN1;






#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__) && !defined(__TINYC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif





#if defined(__clang__)
#define GS_DL_CALL __attribute__((no_sanitize("function")))
#else
#define GS_DL_CALL
#endif

#ifndef GS_NEED_THREADS
#define GS_NEED_THREADS 0
#endif
#ifndef GS_DEBUG
#define GS_DEBUG 0          
#endif





#ifndef GS_MAX_STACKS
#define GS_MAX_STACKS 1024
#endif
#ifndef GS_STACK_RESERVE
#define GS_STACK_RESERVE (2048ull << 20)  
#endif
#ifndef GS_STACK_GAP
#define GS_STACK_GAP (1ull << 20)   
#endif





#ifndef GS_STACK_BUDGET
#define GS_STACK_BUDGET (32ull << 40)
#endif
#ifndef GS_STACK_STATS
#define GS_STACK_STATS 0    
#endif



#if GS_STACK_RESERVE > (1ull << 48)
#error "GS_STACK_RESERVE exceeds the 2^48 limit (faloya_spec.md 10.4)"
#endif

#ifdef _MSC_VER
#define GS_NORETURN __declspec(noreturn)
#define GS_NOINLINE __declspec(noinline)
#else
#define GS_NORETURN __attribute__((noreturn))
#define GS_NOINLINE __attribute__((noinline))
#endif


#if defined(__GNUC__) || defined(__clang__)
#define GS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define GS_INLINE __forceinline
#else
#define GS_INLINE inline
#endif

#if GS_NEED_THREADS
#ifdef _MSC_VER
#define GS_TLS __declspec(thread)
#else
#define GS_TLS __thread
#endif
#else
#define GS_TLS
#endif


#ifdef GS_SEPARATE_RUNTIME
#define GS_API extern
#else
#define GS_API static
#endif

#define GS_CAT_(a, b) a##b
#define GS_CAT(a, b) GS_CAT_(a, b)
#ifdef GS_RUNTIME_VERSION
#define gs_rt_start GS_CAT(gs_rt_start_, GS_RUNTIME_VERSION)
#endif








enum {
    GS_E_CAPACITY,     
    GS_E_SLICE,        
    GS_E_RELOFF,       
    GS_E_ASSERT,       
    GS_E_CAPRANGE,     
    GS_E_RESIZEFILL,   
    GS_E_RESIZENEG,    
    GS_E_POP,          
    GS_E_THREADID,     
    GS_E_THREADSELF,   
    GS_E_TAG,          
    GS_E_ENDIAN,       
    GS_E_SLICELEN,     
    GS_E_POOLSLICE,    
    GS_E_RELNULL,      
    GS_E_POPN,         
    GS_E_QBOUND,       
    GS_E_ARRAYLEN,     
    GS_E_ARRAYSLICE,   
    GS_E_SPANTREE,     
};



typedef struct {
    const char *file;
    int line;
} gs_loc;

GS_API GS_NORETURN void gs_panic(const char *msg);
GS_API GS_NORETURN void gs_abort(int err, const char *file, int line);


GS_API GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line);
GS_API GS_NORETURN void gs_exit(int64_t code);


GS_API GS_NORETURN void gs_stack_overflow(const void *at);
GS_API GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line);
GS_API GS_NORETURN void gs_divfail(const char *file, int line);
GS_API GS_NORETURN void gs_divovf(const char *file, int line);

GS_API GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line);
GS_API GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line);
GS_API GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line);
GS_API GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line);

GS_API GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line);






#define GS_IDX(i, n, f, l) \
    ((uint64_t)(i) < (uint64_t)(n) ? (i) \
                                   : (gs_idxfail((int64_t)(i), (n), (f), (l)), (int64_t)0))



#if GS_DEBUG
#define GS_UNREACHABLE(f, l) gs_abort(GS_E_TAG, (f), (l))
#elif defined(_MSC_VER)
#define GS_UNREACHABLE(f, l) __assume(0)
#elif defined(__GNUC__)
#define GS_UNREACHABLE(f, l) __builtin_unreachable()
#else
#define GS_UNREACHABLE(f, l) ((void)0)
#endif



























#if !defined(GS_VEC) && (defined(__clang__) || defined(__GNUC__)) && !defined(__TINYC__)
#define GS_VEC 1
#endif
#if GS_VEC
typedef double gs_vf64_2 __attribute__((vector_size(16), aligned(1), may_alias));
typedef double gs_vf64_4 __attribute__((vector_size(32), aligned(1), may_alias));
typedef double gs_vf64_8 __attribute__((vector_size(64), aligned(1), may_alias));
typedef float gs_vf32_4 __attribute__((vector_size(16), aligned(1), may_alias));
typedef float gs_vf32_8 __attribute__((vector_size(32), aligned(1), may_alias));
typedef float gs_vf32_16 __attribute__((vector_size(64), aligned(1), may_alias));
#define GS_VLD(V, p) (*(const V *)(const void *)(p))
#define GS_VST(V, p, x) ((void)(*(V *)(void *)(p) = (x)))
#else
#undef GS_VEC
#define GS_VEC 0
#endif

#if defined(__clang__) && (defined(__x86_64__) || defined(_M_X64)) && !defined(__TINYC__)
#ifndef GS_SIMD
#define GS_SIMD 2
#endif
#else
#undef GS_SIMD
#define GS_SIMD 0
#endif
#if GS_SIMD >= 1
#define GS_SIMD_TARGET1 __attribute__((target("avx2,fma,bmi,bmi2,lzcnt,popcnt")))
#define GS_SIMD_TARGET2 \
    __attribute__((target("avx512f,avx512bw,avx512cd,avx512dq,avx512vl,avx2,fma,bmi,bmi2,lzcnt,popcnt")))

static inline void gs_cpuid(uint32_t leaf, uint32_t r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}



static inline int gs_simd_detect(void) {
    uint32_t r[4], b7, xcr0, xhi;
    gs_cpuid(0, r);
    if (r[0] < 7) return 0;
    gs_cpuid(1, r);
    
    if ((r[2] & (1u << 27 | 1u << 28 | 1u << 23 | 1u << 12)) !=
        (1u << 27 | 1u << 28 | 1u << 23 | 1u << 12))
        return 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xhi) : "c"(0));
    (void)xhi;
    if ((xcr0 & 0x6) != 0x6) return 0;                  
    gs_cpuid(7, r);
    b7 = r[1];
    if ((b7 & (1u << 5 | 1u << 3 | 1u << 8)) != (1u << 5 | 1u << 3 | 1u << 8))
        return 0;                                       
    gs_cpuid(0x80000000u, r);
    if (r[0] < 0x80000001u) return 0;
    gs_cpuid(0x80000001u, r);
    if (!(r[2] & (1u << 5))) return 0;                  
    if (GS_SIMD < 2 || (xcr0 & 0xe0) != 0xe0) return 1; 
    
    if ((b7 & (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31)) !=
        (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31))
        return 1;
    return 2;
}



static inline int gs_simd_level(void) {
    static int level = -1;
    int l = __atomic_load_n(&level, __ATOMIC_RELAXED);
    if (l < 0) {
        l = gs_simd_detect();
        __atomic_store_n(&level, l, __ATOMIC_RELAXED);
    }
    return l;
}
#endif












#if !defined(__TINYC__) && !(defined(_MSC_VER) && !defined(__clang__)) && \
    (defined(__FMA__) || defined(__aarch64__) || defined(_M_ARM64))
#define GS_MUL_ADD_BASE fused
#else
#define GS_MUL_ADD_BASE separate
#endif
#define GS_MUL_ADD_MODE GS_MUL_ADD_BASE











#ifdef __TINYC__
#define GS_KSZ(n, lo, hi) (__builtin_constant_p(n) && (n) >= (lo) && (n) <= (hi))
#define GS_AT(T, p, k) (*(T *)((uint8_t *)(p) + (k)))
#define GS_CP(T, d, s, k) (GS_AT(T, d, k) = GS_AT(const T, s, k))
#define GS_MEMCPY(d, s, n) \
    (GS_KSZ(n, 8, 8) ? (void)GS_CP(uint64_t, d, s, 0) \
     : GS_KSZ(n, 4, 4) ? (void)GS_CP(uint32_t, d, s, 0) \
     : GS_KSZ(n, 1, 1) ? (void)GS_CP(uint8_t, d, s, 0) \
     : GS_KSZ(n, 2, 2) ? (void)GS_CP(uint16_t, d, s, 0) \
     : GS_KSZ(n, 3, 3) ? (void)(GS_CP(uint16_t, d, s, 0), GS_CP(uint8_t, d, s, 2)) \
     : GS_KSZ(n, 5, 7) ? (void)(GS_CP(uint32_t, d, s, 0), GS_CP(uint32_t, d, s, (n) - 4)) \
     : GS_KSZ(n, 9, 16) ? (void)(GS_CP(uint64_t, d, s, 0), GS_CP(uint64_t, d, s, (n) - 8)) \
     : GS_KSZ(n, 17, 32) ? (void)(GS_CP(uint64_t, d, s, 0), GS_CP(uint64_t, d, s, 8), \
                                  GS_CP(uint64_t, d, s, (n) - 16), GS_CP(uint64_t, d, s, (n) - 8)) \
     : (void)memcpy((d), (s), (n)))
#define GS_MEMZERO(d, n) \
    (GS_KSZ(n, 8, 8) ? (void)(GS_AT(uint64_t, d, 0) = 0) \
     : GS_KSZ(n, 4, 4) ? (void)(GS_AT(uint32_t, d, 0) = 0) \
     : GS_KSZ(n, 1, 1) ? (void)(GS_AT(uint8_t, d, 0) = 0) \
     : GS_KSZ(n, 2, 2) ? (void)(GS_AT(uint16_t, d, 0) = 0) \
     : GS_KSZ(n, 3, 3) ? (void)(GS_AT(uint16_t, d, 0) = 0, GS_AT(uint8_t, d, 2) = 0) \
     : GS_KSZ(n, 5, 7) ? (void)(GS_AT(uint32_t, d, 0) = 0, GS_AT(uint32_t, d, (n) - 4) = 0) \
     : GS_KSZ(n, 9, 16) ? (void)(GS_AT(uint64_t, d, 0) = 0, GS_AT(uint64_t, d, (n) - 8) = 0) \
     : GS_KSZ(n, 17, 32) ? (void)(GS_AT(uint64_t, d, 0) = 0, GS_AT(uint64_t, d, 8) = 0, \
                                  GS_AT(uint64_t, d, (n) - 16) = 0, GS_AT(uint64_t, d, (n) - 8) = 0) \
     : (void)memset((d), 0, (n)))
#else
#define GS_MEMCPY(d, s, n) memcpy((d), (s), (n))
#define GS_MEMZERO(d, n) memset((d), 0, (n))
#endif


#ifdef __TINYC__
#define gs_ld32(p) GS_AT(const uint32_t, p, 0)
#define gs_ld64(p) GS_AT(const uint64_t, p, 0)
#else
static uint32_t gs_ld32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t gs_ld64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }
#endif



static uint64_t gs_bytemask(int64_t k) {
    return k >= 8 ? ~(uint64_t)0 : k <= 0 ? 0 : ((uint64_t)1 << (k * 8)) - 1;
}









static void gs_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (n <= 16) {
        if (n >= 8) {
            uint64_t a = gs_ld64(s), b = gs_ld64(s + n - 8);
            GS_MEMCPY(d, &a, 8);
            GS_MEMCPY(d + n - 8, &b, 8);
        } else if (n >= 4) {
            uint32_t a = gs_ld32(s), b = gs_ld32(s + n - 4);
            GS_MEMCPY(d, &a, 4);
            GS_MEMCPY(d + n - 4, &b, 4);
        } else if (n) {
            uint8_t a = s[0], b = s[n >> 1], c = s[n - 1];
            d[0] = a;
            d[n >> 1] = b;
            d[n - 1] = c;
        }
        return;
    }
    if (n <= 256) {
        for (size_t i = 0; i + 16 < n; i += 16) GS_MEMCPY(d + i, s + i, 16);
        GS_MEMCPY(d + n - 16, s + n - 16, 16);
        return;
    }
    memcpy(dst, src, n);
}

static void gs_memmove(void *dst, const void *src, size_t n) {
    if (n) memmove(dst, src, n);
}







static GS_NOINLINE int gs_memeq_long(const uint8_t *p, const uint8_t *q, size_t n) {
    if (n > 256) return memcmp(p, q, n) == 0;
    for (size_t i = 0; i + 16 < n; i += 16)
        if ((gs_ld64(p + i) ^ gs_ld64(q + i)) | (gs_ld64(p + i + 8) ^ gs_ld64(q + i + 8)))
            return 0;
    return ((gs_ld64(p + n - 16) ^ gs_ld64(q + n - 16)) |
            (gs_ld64(p + n - 8) ^ gs_ld64(q + n - 8))) == 0;
}

static int gs_memeq(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    if (n >= 8 && n <= 16)
        return ((gs_ld64(p) ^ gs_ld64(q)) | (gs_ld64(p + n - 8) ^ gs_ld64(q + n - 8))) == 0;
    if (n >= 4 && n < 8)
        return ((gs_ld32(p) ^ gs_ld32(q)) | (gs_ld32(p + n - 4) ^ gs_ld32(q + n - 4))) == 0;
    if (n < 4)
        return n == 0 || ((p[0] ^ q[0]) | (p[n >> 1] ^ q[n >> 1]) | (p[n - 1] ^ q[n - 1])) == 0;
    return gs_memeq_long(p, q, n);
}




























#define GS_DIVU(T, a, b, f, l) \
    ((T)(b) ? (T)((T)(a) / (T)(b)) : (gs_divfail((f), (l)), (T)0))
#define GS_MODU(T, a, b, f, l) \
    ((T)(b) ? (T)((T)(a) % (T)(b)) : (gs_divfail((f), (l)), (T)0))
#define GS_DIVS(T, a, b, f, l) \
    ((T)(b) ? (T)((int64_t)(T)(a) / (int64_t)(T)(b)) : (gs_divfail((f), (l)), (T)0))
#define GS_MODS(T, t, a, b, f, l) \
    ((T)(b) == 0 ? (gs_divfail((f), (l)), (T)0) \
     : (T)(((t) = (int64_t)(T)(a) % (int64_t)(T)(b)) < 0 \
               ? (t) + ((T)(b) < 0 ? -(int64_t)(T)(b) : (int64_t)(T)(b)) : (t)))

#define gs_div_u8(a, b, f, l)  GS_DIVU(uint8_t, a, b, f, l)
#define gs_div_u16(a, b, f, l) GS_DIVU(uint16_t, a, b, f, l)
#define gs_div_u32(a, b, f, l) GS_DIVU(uint32_t, a, b, f, l)
#define gs_div_u64(a, b, f, l) GS_DIVU(uint64_t, a, b, f, l)
#define gs_mod_u8(a, b, f, l)  GS_MODU(uint8_t, a, b, f, l)
#define gs_mod_u16(a, b, f, l) GS_MODU(uint16_t, a, b, f, l)
#define gs_mod_u32(a, b, f, l) GS_MODU(uint32_t, a, b, f, l)
#define gs_mod_u64(a, b, f, l) GS_MODU(uint64_t, a, b, f, l)
#define gs_mod_i8(t, a, b, f, l)  GS_MODS(int8_t, t, a, b, f, l)
#define gs_mod_i16(t, a, b, f, l) GS_MODS(int16_t, t, a, b, f, l)
#define gs_mod_i32(t, a, b, f, l) GS_MODS(int32_t, t, a, b, f, l)
#define gs_div_i64(a, b, f, l) \
    ((int64_t)(b) == 0 ? (gs_divfail((f), (l)), (int64_t)0) \
     : (int64_t)(a) == INT64_MIN && (int64_t)(b) == -1 ? (gs_divovf((f), (l)), (int64_t)0) \
     : (int64_t)(a) / (int64_t)(b))


#define gs_mod_i64(t, a, b, f, l) \
    ((int64_t)(b) == 0 ? (gs_divfail((f), (l)), (int64_t)0) \
     : (int64_t)(b) == -1 ? (int64_t)0 \
     : ((t) = (int64_t)(a) % (int64_t)(b)) < 0 \
         ? (int64_t)((uint64_t)(t) + ((int64_t)(b) < 0 ? 0u - (uint64_t)(int64_t)(b) \
                                                       : (uint64_t)(int64_t)(b))) \
         : (t))

#if GS_DEBUG
#define GS_DIVOPS_S(SFX, T, MIN, MAX) \
static T gs_div_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    int64_t r = (int64_t)a / (int64_t)b; \
    if (r < (MIN) || r > (MAX)) gs_ovf(a, "/", b, #SFX, file, line); \
    return (T)r; }
GS_DIVOPS_S(i8, int8_t, -128, 127)
GS_DIVOPS_S(i16, int16_t, -32768, 32767)
GS_DIVOPS_S(i32, int32_t, INT32_MIN, INT32_MAX)
#else
#define gs_div_i8(a, b, f, l)  GS_DIVS(int8_t, a, b, f, l)
#define gs_div_i16(a, b, f, l) GS_DIVS(int16_t, a, b, f, l)
#define gs_div_i32(a, b, f, l) GS_DIVS(int32_t, a, b, f, l)
#endif

#if GS_DEBUG



#define GS_INTOPS_S(SFX, T, MIN, MAX, BITS) \
static T gs_add_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a + (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "+", b, #SFX, file, line); \
    return (T)r; } \
static T gs_sub_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a - (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "-", b, #SFX, file, line); \
    return (T)r; } \
static T gs_mul_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a * (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "*", b, #SFX, file, line); \
    return (T)r; } \
static T gs_neg_##SFX(T a, const char *file, int line) { \
    int64_t r = -(int64_t)a; \
    if (r < MIN || r > MAX) gs_ovf_neg(a, #SFX, file, line); \
    return (T)r; } \
static T gs_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T gs_shr_##SFX(T a, int64_t n) { \
    return (T)((int64_t)a >> (n & (BITS - 1))); }



#define GS_INTOPS_U(SFX, T, MAX, BITS) \
static T gs_add_##SFX(T a, T b) { return (T)(a + b); } \
static T gs_sub_##SFX(T a, T b) { return (T)(a - b); } \
static T gs_mul_##SFX(T a, T b) { return (T)((uint64_t)a * (uint64_t)b); } \
static T gs_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T gs_shr_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a >> (n & (BITS - 1))); }

GS_INTOPS_S(i8,  int8_t,  -128, 127, 8)
GS_INTOPS_S(i16, int16_t, -32768, 32767, 16)
GS_INTOPS_S(i32, int32_t, INT32_MIN, INT32_MAX, 32)
GS_INTOPS_U(u8,  uint8_t,  255u, 8)
GS_INTOPS_U(u16, uint16_t, 65535u, 16)
GS_INTOPS_U(u32, uint32_t, 4294967295u, 32)


static int64_t gs_add_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) gs_ovf(a, "+", b, "i64", file, line);
    return r;
}
static int64_t gs_sub_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (((a ^ b) & (a ^ r)) < 0) gs_ovf(a, "-", b, "i64", file, line);
    return r;
}
static int64_t gs_mul_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a && b &&
        ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN) || r / b != a))
        gs_ovf(a, "*", b, "i64", file, line);
    return r;
}
static int64_t gs_neg_i64(int64_t a, const char *file, int line) {
    if (a == INT64_MIN) gs_ovf_neg(a, "i64", file, line);
    return (int64_t)(0u - (uint64_t)a);
}
static int64_t gs_shl_i64(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a << (n & 63));
}
static int64_t gs_shr_i64(int64_t a, int64_t n) { return a >> (n & 63); }
static uint64_t gs_add_u64(uint64_t a, uint64_t b) { return a + b; }
static uint64_t gs_sub_u64(uint64_t a, uint64_t b) { return a - b; }
static uint64_t gs_mul_u64(uint64_t a, uint64_t b) { return a * b; }
static uint64_t gs_shl_u64(uint64_t a, int64_t n) { return a << (n & 63); }
static uint64_t gs_shr_u64(uint64_t a, int64_t n) { return a >> (n & 63); }

#else  

#define gs_add_i8(a, b, f, l)  ((int8_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i8(a, b, f, l)  ((int8_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i8(a, b, f, l)  ((int8_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i8(a, f, l)     ((int8_t)(-(int64_t)(a)))
#define gs_shl_i8(a, n)  ((int8_t)((uint64_t)(a) << ((n) & 7)))
#define gs_shr_i8(a, n)  ((int8_t)((int64_t)(a) >> ((n) & 7)))

#define gs_add_i16(a, b, f, l) ((int16_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i16(a, b, f, l) ((int16_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i16(a, b, f, l) ((int16_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i16(a, f, l)    ((int16_t)(-(int64_t)(a)))
#define gs_shl_i16(a, n) ((int16_t)((uint64_t)(a) << ((n) & 15)))
#define gs_shr_i16(a, n) ((int16_t)((int64_t)(a) >> ((n) & 15)))

#define gs_add_i32(a, b, f, l) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i32(a, b, f, l) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i32(a, b, f, l) ((int32_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i32(a, f, l)    ((int32_t)(-(int64_t)(a)))
#define gs_shl_i32(a, n) ((int32_t)((uint64_t)(a) << ((n) & 31)))
#define gs_shr_i32(a, n) ((int32_t)((int64_t)(a) >> ((n) & 31)))

#define gs_add_u8(a, b)  ((uint8_t)((a) + (b)))
#define gs_sub_u8(a, b)  ((uint8_t)((a) - (b)))
#define gs_mul_u8(a, b)  ((uint8_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u8(a, n)  ((uint8_t)((uint64_t)(a) << ((n) & 7)))
#define gs_shr_u8(a, n)  ((uint8_t)((uint64_t)(a) >> ((n) & 7)))

#define gs_add_u16(a, b) ((uint16_t)((a) + (b)))
#define gs_sub_u16(a, b) ((uint16_t)((a) - (b)))
#define gs_mul_u16(a, b) ((uint16_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u16(a, n) ((uint16_t)((uint64_t)(a) << ((n) & 15)))
#define gs_shr_u16(a, n) ((uint16_t)((uint64_t)(a) >> ((n) & 15)))

#define gs_add_u32(a, b) ((uint32_t)((a) + (b)))
#define gs_sub_u32(a, b) ((uint32_t)((a) - (b)))
#define gs_mul_u32(a, b) ((uint32_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u32(a, n) ((uint32_t)((uint64_t)(a) << ((n) & 31)))
#define gs_shr_u32(a, n) ((uint32_t)((uint64_t)(a) >> ((n) & 31)))

#define gs_add_i64(a, b, f, l) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define gs_sub_i64(a, b, f, l) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define gs_mul_i64(a, b, f, l) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_neg_i64(a, f, l)    ((int64_t)(0u - (uint64_t)(a)))
#define gs_shl_i64(a, n) ((int64_t)((uint64_t)(a) << ((n) & 63)))
#define gs_shr_i64(a, n) ((int64_t)((a) >> ((n) & 63)))

#define gs_add_u64(a, b) ((uint64_t)((a) + (b)))
#define gs_sub_u64(a, b) ((uint64_t)((a) - (b)))
#define gs_mul_u64(a, b) ((uint64_t)((a) * (b)))
#define gs_shl_u64(a, n) ((uint64_t)((a) << ((n) & 63)))
#define gs_shr_u64(a, n) ((uint64_t)((a) >> ((n) & 63)))

#endif  







#define GS_DIVU_NONE 255
#define GS_DIVU_ADD 64
#define GS_DIVU_SHIFT 63
#if defined(__TINYC__)
#define GS_HAVE_U128 0
#elif defined(__SIZEOF_INT128__)
#define GS_HAVE_U128 1
static uint64_t gs_mulhi_u64(uint64_t a, uint64_t b) {
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}



static uint64_t gs_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
#if defined(__x86_64__)
    uint64_t q, r;
    __asm__("divq %[d]" : "=a"(q), "=d"(r) : [d] "r"(d), "a"((uint64_t)0), "d"(hi));
    *rem = r;
    return q;
#else
    unsigned __int128 n = (unsigned __int128)hi << 64;
    *rem = (uint64_t)(n % d);
    return (uint64_t)(n / d);
#endif
}
#elif defined(_MSC_VER) && _MSC_VER >= 1920 && defined(_M_X64)
#include <intrin.h>
#define GS_HAVE_U128 1
static uint64_t gs_mulhi_u64(uint64_t a, uint64_t b) { return __umulh(a, b); }
static uint64_t gs_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
    return _udiv128(hi, 0, d, rem);
}
#else
#define GS_HAVE_U128 0
#endif

static uint64_t gs_divu_gen(uint64_t d, uint8_t *more) {
#if GS_HAVE_U128
    int k = 63;
    uint64_t rem, m;
    if (d == 0) { *more = GS_DIVU_NONE; return 0; }
    while (!(d >> k)) k--;
    if (!(d & (d - 1))) { *more = (uint8_t)k; return 0; }
    

    m = gs_div128_u64((uint64_t)1 << k, d, &rem);
    if (d - rem < ((uint64_t)1 << k)) {
        *more = (uint8_t)k;
    } else {
        uint64_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem) m++;
        *more = (uint8_t)(k | GS_DIVU_ADD);
    }
    return m + 1;
#else
    (void)d;
    *more = GS_DIVU_NONE;
    return 0;
#endif
}

static uint64_t gs_divu_q(uint64_t x, uint64_t magic, uint8_t more) {
#if GS_HAVE_U128
    uint64_t q;
    if (!magic) return x >> more;
    q = gs_mulhi_u64(magic, x);
    if (more & GS_DIVU_ADD) return (((x - q) >> 1) + q) >> (more & GS_DIVU_SHIFT);
    return q >> more;
#else
    (void)magic;
    return x >> more;   
#endif
}





static uint32_t gs_divu32_gen(uint32_t d, uint8_t *more) {
    int k = 31;
    uint64_t n;
    uint32_t m, rem;
    if (d == 0) { *more = GS_DIVU_NONE; return 0; }
    while (!(d >> k)) k--;
    if (!(d & (d - 1))) { *more = (uint8_t)k; return 0; }
    n = (uint64_t)1 << (32 + k);
    m = (uint32_t)(n / d);
    rem = (uint32_t)(n % d);
    if (d - rem < ((uint32_t)1 << k)) {
        *more = (uint8_t)k;
    } else {
        uint32_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem) m++;
        *more = (uint8_t)(k | GS_DIVU_ADD);
    }
    return m + 1;
}

static uint32_t gs_divu32_q(uint32_t x, uint32_t magic, uint8_t more) {
    uint32_t q;
    if (!magic) return x >> more;
    q = (uint32_t)(((uint64_t)magic * x) >> 32);
    if (more & GS_DIVU_ADD) return (((x - q) >> 1) + q) >> (more & GS_DIVU_SHIFT);
    return q >> more;
}







static GS_NOINLINE int64_t gs_f2iwrap_slow(double d) {
    if (d != d) return 0;
    d = fmod(trunc(d), 18446744073709551616.0);
    if (d < 0) d += 18446744073709551616.0;
    return (int64_t)(uint64_t)d;
}
static int64_t gs_f2iwrap(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return (int64_t)d;
    return gs_f2iwrap_slow(d);
}






#if GS_DEBUG

static int64_t gs_rangechk(int64_t v, int64_t lo, int64_t hi, const char *type,
                           const char *file, int line) {
    if (v < lo || v > hi) gs_asfail_i("out of range", v, type, file, line);
    return v;
}
static uint64_t gs_rangechk_u(uint64_t v, uint64_t hi, const char *type, const char *file,
                              int line) {
    if (v > hi) gs_asfail_u("out of range", v, type, file, line);
    return v;
}

static int64_t gs_f2ichk(double d, int f32, int64_t lo, int64_t hi, const char *type,
                         const char *file, int line) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        gs_asfail_f("out of range", d, f32, type, file, line);
    int64_t v = (int64_t)d;
    if (v < lo || v > hi) gs_asfail_f("out of range", d, f32, type, file, line);
    if ((double)v != d) gs_asfail_f("changes the value", d, f32, type, file, line);
    return v;
}
static uint64_t gs_f2uchk(double d, int f32, const char *file, int line) {
    if (!(d >= 0 && d < 18446744073709551616.0))
        gs_asfail_f("out of range", d, f32, "u64", file, line);
    uint64_t v = (uint64_t)d;
    if ((double)v != d) gs_asfail_f("changes the value", d, f32, "u64", file, line);
    return v;
}
#define GS_RANGE(v, lo, hi, t, f, l) gs_rangechk((v), (lo), (hi), (t), (f), (l))
#define GS_RANGE_U(v, hi, t, f, l)   gs_rangechk_u((v), (hi), (t), (f), (l))
#define GS_F2I(d, s, lo, hi, t, f, l) gs_f2ichk((d), (s), (lo), (hi), (t), (f), (l))
#define GS_F2U(d, s, f, l)            gs_f2uchk((d), (s), (f), (l))

#else

#define GS_RANGE(v, lo, hi, t, f, l) (v)
#define GS_RANGE_U(v, hi, t, f, l)   (v)

#define GS_F2I(d, s, lo, hi, t, f, l) gs_f2iwrap(d)
#define GS_F2U(d, s, f, l)            ((uint64_t)gs_f2iwrap(d))

#endif









typedef struct {
    uint8_t *top;
} gs_stack;




#define FALOYA_H 1






GS_API void gs_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions);

GS_API uint8_t *gs_reserve_region(gs_stack *stack);


GS_API void gs_stack_stats(int64_t stacks);


GS_API void gs_release_regions(void);




GS_API void gs_rt_stop(void);









typedef struct gs_dllib {
    const char *name, *onwindows, *onmacos, *onlinux;
} gs_dllib;


typedef struct gs_dlbind {
    int32_t lib;
    const char *sym;
    void **slot;
} gs_dlbind;



GS_API void gs_dl_bind(const gs_dllib *libs, int64_t nlibs, const gs_dlbind *binds,
                       int64_t nbinds);




GS_API int64_t gs_hardware_threads(void);



GS_API int64_t gs_thread_start(void (*run)(void (*)(uint8_t *), uint8_t *),
                               void (*release)(void), void (*entry)(uint8_t *),
                               const void *args, int64_t argsize, int contained);




GS_API uint8_t gs_thread_wait(int64_t id, uint8_t **msg, int64_t *len,
                               const char *file, int line);
GS_API void gs_free_msg(uint8_t *msg);







typedef struct gs_qnode {
    struct gs_qnode *next;
    int64_t size;
    
} gs_qnode;

typedef struct gs_qstate *gs_queue;
#define GS_QUEUE_INIT NULL

GS_API void gs_qinit(gs_queue *q);
GS_API uint8_t gs_qput(gs_queue *q, const void *data, int64_t size);
GS_API gs_qnode *gs_qget(gs_queue *q);
GS_API gs_qnode *gs_qwait(gs_queue *q, int64_t ms);
GS_API gs_qnode *gs_qpoll(gs_queue *q);
GS_API void gs_qlimit(gs_queue *q, int64_t n, const char *file, int line);


GS_API void gs_qfree(gs_queue *q);

#ifndef GS_RUNTIME_OBJECT




static GS_TLS gs_stack *gs_stks;
static GS_TLS int64_t gs_nstks;






static GS_TLS void *gs_gl;

#define GS(i) (&gs_stks[i])

static void gs_stack_init(gs_stack *s) {
    s->top = gs_reserve_region(s);
}



static void gs_stack_block(int64_t n) {
    gs_stks = (gs_stack *)calloc((size_t)(n > 0 ? n : 1), sizeof(gs_stack));
    if (!gs_stks) gs_panic("out of memory allocating stack block");
    for (int64_t i = 0; i < n; i++) gs_stack_init(&gs_stks[i]);
    gs_nstks = n;
}




static void gs_rt_init(int argc, char **argv, int64_t mainstacks, int64_t mainregions,
                       int64_t workerregions) {
    gs_rt_start(argc, argv, GS_STACK_RESERVE, GS_STACK_GAP, GS_STACK_BUDGET, mainregions,
                workerregions);
    gs_stack_block(mainstacks);
}


static void gs_rt_stats(void) {
#if GS_STACK_STATS
    gs_stack_stats(gs_nstks);
#endif
}

#if GS_NEED_THREADS


static void gs_thread_run(void (*entry)(uint8_t *), uint8_t *args) {
    entry(args);
    gs_rt_stats();
}



static void gs_thread_release(void) {
    free(gs_gl);
    gs_gl = NULL;
    free(gs_stks);
    gs_stks = NULL;
    gs_nstks = 0;
}

static int64_t gs_thread_spawn(void (*entry)(uint8_t *), const void *args, int64_t argsize,
                               int contained) {
    return gs_thread_start(gs_thread_run, gs_thread_release, entry, args, argsize, contained);
}
#endif

#endif  









#define GS_SPAN_NODE 512



GS_API int64_t gs_spans_alloc(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                              int64_t cnt);



GS_API int64_t gs_spans_regrow(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                               int64_t idx, int64_t ol, int64_t cnt);


GS_API void gs_spans_free(uint8_t *base, int64_t *n, uint8_t **top, int64_t idx, int64_t cnt);




static int64_t gs_uleb_read(const uint8_t *p) {
    uint64_t v = 0;
    int shift = 0;
    for (;;) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return (int64_t)v;
        shift += 7;
    }
}

static int64_t gs_uleb_size(const uint8_t *p) {
    const uint8_t *q = p;
    while (*q & 0x80) q++;
    return (int64_t)(q - p) + 1;
}











static GS_NOINLINE int64_t gs_uleb_read_slow(const uint8_t *p) {
    return gs_uleb_read(p);
}

static GS_NOINLINE int64_t gs_uleb_size_slow(const uint8_t *p) {
    return gs_uleb_size(p);
}

#define GS_ULEB_READ(p) ((*(p) & 0x80) ? gs_uleb_read_slow(p) : (int64_t)*(p))
#define GS_ULEB_SIZE(p) ((*(p) & 0x80) ? gs_uleb_size_slow(p) : (int64_t)1)

static int64_t gs_uleb_write(uint8_t *p, uint64_t v) {
    uint8_t *q = p;
    for (;;) {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) *q++ = b | 0x80; else { *q++ = b; break; }
    }
    return (int64_t)(q - p);
}

static int64_t gs_zig_read(const uint8_t *p) {
    uint64_t u = (uint64_t)gs_uleb_read(p);
    return (int64_t)((u >> 1) ^ (0u - (u & 1)));
}

static int64_t gs_zig_write(uint8_t *p, int64_t v) {
    return gs_uleb_write(p, ((uint64_t)v << 1) ^ (uint64_t)(v >> 63));
}









static int64_t gs_uleb_check(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    const uint8_t *q = p;
    for (;;) {
        uint8_t b;
        if (q >= end) return 0;
        b = *q++;
        if (shift > 63 || (shift == 63 && (b & 0x7e))) return 0;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            if (shift && !b) return 0;  
            break;
        }
        shift += 7;
    }
    *out = v;
    return (int64_t)(q - p);
}



static int64_t gs_zig_check(const uint8_t *p, const uint8_t *end, int64_t *out) {
    uint64_t u = 0;
    int64_t k = gs_uleb_check(p, end, &u);
    if (k) *out = (int64_t)((u >> 1) ^ (0u - (u & 1)));
    return k;
}







#define GS_BM_SET(bm, i) ((bm)[(uint64_t)(i) >> 3] |= (uint8_t)(1u << ((i) & 7)))
#define GS_BM_GET(bm, i) (((bm)[(uint64_t)(i) >> 3] >> ((i) & 7)) & 1)
#define GS_BM_MAX ((int64_t)(GS_STACK_RESERVE))





static int gs_is_le(void) {
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}






#define GS_FMT_MAX 32

GS_API int64_t gs_fmt_i64(uint8_t *dst, int64_t v);
GS_API int64_t gs_fmt_u64(uint8_t *dst, uint64_t v);
GS_API int64_t gs_fmt_f64(uint8_t *dst, double v);
GS_API int64_t gs_fmt_f32(uint8_t *dst, float v);
GS_API int64_t gs_fmt_bool(uint8_t *dst, int64_t v);

GS_API int64_t gs_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n);


GS_API void gs_out_bytes(const uint8_t *p, int64_t len);




GS_API int64_t gs_scan_any(const uint8_t *p, int64_t n, const void *set);
GS_API int64_t gs_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b);
