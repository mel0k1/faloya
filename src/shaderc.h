




#ifndef GS_SHADERC_H
#define GS_SHADERC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *blob;   
    size_t size;
    char *error;           
} gs_shaderc_result;



gs_shaderc_result gs_shaderc_compile(const char *source, const char *path, int stage);
void gs_shaderc_free(gs_shaderc_result *r);

#ifdef __cplusplus
}
#endif

#endif
