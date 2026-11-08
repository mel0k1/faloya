










#ifdef GS_RUNTIME_OBJECT
#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
typedef struct { uint8_t *data; int64_t len; } sl_u8;
#pragma pack(pop)
#endif




static void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}





static uint8_t gs_ffi_cstr(uint64_t p, gs_rref out) {
    const char *s = (const char *)(uintptr_t)p;
    if (!s) return 0;
    gs_bld_append(out, s, (int64_t)strlen(s));
    return 1;
}
static uint8_t gs_ffi_bytes(uint64_t p, int64_t n, gs_rref out) {
    if (!p) return 0;
    gs_bld_append(out, (const void *)(uintptr_t)p, n);
    return 1;
}







#if defined(__has_builtin)
#if __has_builtin(__builtin_elementwise_sqrt)
#define GS_SQRT_NO_ERRNO 1
#endif
#endif
#ifdef GS_SQRT_NO_ERRNO
static double gs_sqrt(double x) { return __builtin_elementwise_sqrt(x); }
static float gs_sqrtf(float x) { return __builtin_elementwise_sqrt(x); }
#else
static double gs_sqrt(double x) { return sqrt(x); }
static float gs_sqrtf(float x) { return sqrtf(x); }
#endif



static float gs_f32_from_bits(uint32_t bits) {
    float x;
    memcpy(&x, &bits, sizeof x);
    return x;
}
static double gs_f64_from_bits(uint64_t bits) {
    double x;
    memcpy(&x, &bits, sizeof x);
    return x;
}
static uint32_t gs_f32_to_bits(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}
static uint64_t gs_f64_to_bits(double x) {
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}










static inline double gs_mul_add_separate(double x, double y, double z) { return x * y + z; }
static inline float gs_mul_addf_separate(float x, float y, float z) { return x * y + z; }
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
static inline __attribute__((always_inline)) double gs_mul_add_fused(double x, double y, double z) {
    return __builtin_fma(x, y, z);
}
static inline __attribute__((always_inline)) float gs_mul_addf_fused(float x, float y, float z) {
    return __builtin_fmaf(x, y, z);
}
#else
static inline double gs_mul_add_fused(double x, double y, double z) { return fma(x, y, z); }
static inline float gs_mul_addf_fused(float x, float y, float z) { return fmaf(x, y, z); }
#endif
#define GS_MUL_ADD_CAT2(a, b) a##b
#define GS_MUL_ADD_CAT(a, b) GS_MUL_ADD_CAT2(a, b)
static double gs_mul_add(double x, double y, double z) {
    return GS_MUL_ADD_CAT(gs_mul_add_, GS_MUL_ADD_BASE)(x, y, z);
}
static float gs_mul_addf(float x, float y, float z) {
    return GS_MUL_ADD_CAT(gs_mul_addf_, GS_MUL_ADD_BASE)(x, y, z);
}
#define gs_mul_add(x, y, z) GS_MUL_ADD_CAT(gs_mul_add_, GS_MUL_ADD_MODE)(x, y, z)
#define gs_mul_addf(x, y, z) GS_MUL_ADD_CAT(gs_mul_addf_, GS_MUL_ADD_MODE)(x, y, z)















#if defined(__clang__) && !defined(__APPLE__) && !defined(__TINYC__)
#define GS_LIBM_SYM2(p, f) #p #f
#define GS_LIBM_SYM(p, f) GS_LIBM_SYM2(p, f)
#define GS_LIBM
#define GS_LIBM1(f) __asm__(GS_LIBM_SYM(__USER_LABEL_PREFIX__, f)) __attribute__((const));
#define GS_LIBM2(f) GS_LIBM1(f)
#elif defined(__GNUC__) && !defined(__TINYC__)
__attribute__((const)) double sin(double), cos(double), tan(double), asin(double), acos(double),
    atan(double), atan2(double, double), exp(double), log(double), log2(double), log10(double),
    pow(double, double), hypot(double, double), cbrt(double), exp2(double), expm1(double),
    log1p(double), sinh(double), cosh(double), tanh(double);
__attribute__((const)) float sinf(float), cosf(float), tanf(float), asinf(float), acosf(float),
    atanf(float), atan2f(float, float), expf(float), logf(float), log2f(float), log10f(float),
    powf(float, float), hypotf(float, float), cbrtf(float), exp2f(float), expm1f(float),
    log1pf(float), sinhf(float), coshf(float), tanhf(float);
#define GS_LIBM static inline
#define GS_LIBM1(f) { return f(x); }
#define GS_LIBM2(f) { return f(x, y); }
#else
#define GS_LIBM static
#define GS_LIBM1(f) { return f(x); }
#define GS_LIBM2(f) { return f(x, y); }
#endif
GS_LIBM double gs_sin(double x) GS_LIBM1(sin)
GS_LIBM double gs_cos(double x) GS_LIBM1(cos)
GS_LIBM double gs_tan(double x) GS_LIBM1(tan)
GS_LIBM double gs_asin(double x) GS_LIBM1(asin)
GS_LIBM double gs_acos(double x) GS_LIBM1(acos)
GS_LIBM double gs_atan(double x) GS_LIBM1(atan)
GS_LIBM double gs_atan2(double x, double y) GS_LIBM2(atan2)
GS_LIBM double gs_exp(double x) GS_LIBM1(exp)
GS_LIBM double gs_log(double x) GS_LIBM1(log)
GS_LIBM double gs_log2(double x) GS_LIBM1(log2)
GS_LIBM double gs_log10(double x) GS_LIBM1(log10)
GS_LIBM double gs_pow(double x, double y) GS_LIBM2(pow)
GS_LIBM double gs_hypot(double x, double y) GS_LIBM2(hypot)
GS_LIBM double gs_cbrt(double x) GS_LIBM1(cbrt)
GS_LIBM double gs_exp2(double x) GS_LIBM1(exp2)
GS_LIBM double gs_expm1(double x) GS_LIBM1(expm1)
GS_LIBM double gs_log1p(double x) GS_LIBM1(log1p)
GS_LIBM double gs_sinh(double x) GS_LIBM1(sinh)
GS_LIBM double gs_cosh(double x) GS_LIBM1(cosh)
GS_LIBM double gs_tanh(double x) GS_LIBM1(tanh)
GS_LIBM float gs_sinf(float x) GS_LIBM1(sinf)
GS_LIBM float gs_cosf(float x) GS_LIBM1(cosf)
GS_LIBM float gs_tanf(float x) GS_LIBM1(tanf)
GS_LIBM float gs_asinf(float x) GS_LIBM1(asinf)
GS_LIBM float gs_acosf(float x) GS_LIBM1(acosf)
GS_LIBM float gs_atanf(float x) GS_LIBM1(atanf)
GS_LIBM float gs_atan2f(float x, float y) GS_LIBM2(atan2f)
GS_LIBM float gs_expf(float x) GS_LIBM1(expf)
GS_LIBM float gs_logf(float x) GS_LIBM1(logf)
GS_LIBM float gs_log2f(float x) GS_LIBM1(log2f)
GS_LIBM float gs_log10f(float x) GS_LIBM1(log10f)
GS_LIBM float gs_powf(float x, float y) GS_LIBM2(powf)
GS_LIBM float gs_hypotf(float x, float y) GS_LIBM2(hypotf)
GS_LIBM float gs_cbrtf(float x) GS_LIBM1(cbrtf)
GS_LIBM float gs_exp2f(float x) GS_LIBM1(exp2f)
GS_LIBM float gs_expm1f(float x) GS_LIBM1(expm1f)
GS_LIBM float gs_log1pf(float x) GS_LIBM1(log1pf)
GS_LIBM float gs_sinhf(float x) GS_LIBM1(sinhf)
GS_LIBM float gs_coshf(float x) GS_LIBM1(coshf)
GS_LIBM float gs_tanhf(float x) GS_LIBM1(tanhf)



static int64_t gs_find_any(sl_u8 s, const void *set) { return gs_scan_any(s.data, s.len, set); }
static int64_t gs_find_pair(sl_u8 s, const void *a, int64_t d, const void *b) {
    return gs_scan_pair(s.data, s.len, a, d, b);
}




#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
static int64_t gs_popcount(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER) && defined(_M_X64) && defined(__AVX__)
    return (int64_t)__popcnt64(x);
#else
    int64_t n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
#endif
}
static int64_t gs_clz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_clzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanReverse64(&i, x) ? 63 - (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x >> 63)) {
        x <<= 1;
        n++;
    }
    return n;
#endif
}
static int64_t gs_ctz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_ctzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanForward64(&i, x) ? (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
#endif
}






static uint32_t gs_rotl32(uint32_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotl(x, (int)(n & 31));
#else
    uint32_t s = (uint32_t)n & 31;
    return (x << s) | (x >> (-s & 31));
#endif
}
static uint64_t gs_rotl64(uint64_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotl64(x, (int)(n & 63));
#else
    uint32_t s = (uint32_t)n & 63;
    return (x << s) | (x >> (-s & 63));
#endif
}
static uint32_t gs_rotr32(uint32_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotr(x, (int)(n & 31));
#else
    uint32_t s = (uint32_t)n & 31;
    return (x >> s) | (x << (-s & 31));
#endif
}
static uint64_t gs_rotr64(uint64_t x, int64_t n) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _rotr64(x, (int)(n & 63));
#else
    uint32_t s = (uint32_t)n & 63;
    return (x >> s) | (x << (-s & 63));
#endif
}
static uint16_t gs_byte_swap16(uint16_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)
    return _byteswap_ushort(x);
#else
    return (uint16_t)((x << 8) | (x >> 8));
#endif
}
static uint32_t gs_byte_swap32(uint32_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return (x << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | (x >> 24);
#endif
}
static uint64_t gs_byte_swap64(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((uint64_t)gs_byte_swap32((uint32_t)x) << 32) | gs_byte_swap32((uint32_t)(x >> 32));
#endif
}

GS_API uint8_t gs_os_read_file(sl_u8 path, gs_rref out);
GS_API uint8_t gs_os_write_file(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_write_file_atomic(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_append_file(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_file_exists(sl_u8 path);
GS_API uint8_t gs_os_remove_file(sl_u8 path);
GS_API uint8_t gs_os_rename_file(sl_u8 from, sl_u8 to);
GS_API uint8_t gs_os_make_dir(sl_u8 path);
GS_API uint8_t gs_os_is_dir(sl_u8 path);
GS_API uint8_t gs_os_remove_dir(sl_u8 path);
GS_API uint8_t gs_os_list_dir(sl_u8 path, gs_rref out);
GS_API void gs_os_write_stdout(sl_u8 s);
GS_API void gs_os_write_stderr(sl_u8 s);
GS_API void gs_os_flush_stdout(void);
GS_API uint8_t gs_os_read_line(gs_rref out);
GS_API void gs_os_read_stdin(gs_rref out);
GS_API uint8_t gs_os_read_stdin_max(gs_rref out, int64_t max);
GS_API int64_t gs_os_arg_count(void);
GS_API void gs_os_arg(int64_t i, gs_rref out);
GS_API uint8_t gs_os_getenv(sl_u8 name, gs_rref out);
GS_API uint8_t gs_os_resource_dir(gs_rref out);
GS_API int64_t gs_os_time_ns(void);
GS_API int64_t gs_os_clock_ns(void);
GS_API void gs_os_sleep_ms(int64_t ms);
GS_API uint64_t gs_os_random_u64(void);






GS_API uint8_t gs_trim_memory(void);





GS_API int64_t gs_f64_digits(double x, sl_u8 dig, int64_t *point);





GS_API int64_t gs_slice_pool_place(gs_rref nodes, void *tree, int64_t len, int64_t cnt,
                                   const gs_loc *at);
GS_API void gs_slice_pool_free(gs_rref nodes, void *tree, int64_t idx, int64_t cnt,
                               const gs_loc *at);
GS_API int64_t gs_slice_pool_regrow(gs_rref nodes, void *tree, int64_t len, int64_t idx,
                                    int64_t ol, int64_t cnt, const gs_loc *at);


GS_API uint8_t gs_slice_pool_trim(gs_rref nodes, void *tree, sl_u8 items, int64_t elem,
                                  const gs_loc *at);
