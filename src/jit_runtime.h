#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const void *address;
} FaloyaJitSymbol;

const FaloyaJitSymbol *faloya_jit_symbols(void);
int faloya_jit_run(int (*entry)(int, char **), void (*cleanup)(void),
                  int argc, char **argv, const char *resource_dir);

#ifdef __cplusplus
}
#endif
