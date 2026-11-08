



















#ifndef GS_GFX_BLOB_H
#define GS_GFX_BLOB_H

#include <stdint.h>

#define GS_GFX_BLOB_MAGIC "GSHD"
#define GS_GFX_BLOB_VERSION 1

enum {
    GS_GFX_STAGE_VERTEX = 0,
    GS_GFX_STAGE_FRAGMENT = 1,
    GS_GFX_STAGE_COMPUTE = 2,
};


enum {
    GS_GFX_INPUT_INT = 1, GS_GFX_INPUT_UINT = 2, GS_GFX_INPUT_FLOAT = 3,
    GS_GFX_INPUT_INT2 = 4, GS_GFX_INPUT_UINT2 = 5, GS_GFX_INPUT_FLOAT2 = 6,
    GS_GFX_INPUT_INT3 = 7, GS_GFX_INPUT_UINT3 = 8, GS_GFX_INPUT_FLOAT3 = 9,
    GS_GFX_INPUT_INT4 = 10, GS_GFX_INPUT_UINT4 = 11, GS_GFX_INPUT_FLOAT4 = 12,
    GS_GFX_INPUT_MAT4 = 13,
};

#pragma pack(push, 1)
typedef struct {
    char magic[4];
    uint16_t version;
    uint8_t stage;
    uint8_t num_inputs;
    uint32_t local_size[3];         
    
    uint16_t num_samplers, num_storage_textures_ro, num_storage_textures_rw,
             num_storage_buffers_ro, num_storage_buffers_rw, num_uniform_buffers;
    uint32_t spirv_offset, spirv_size;
    uint32_t msl_offset, msl_size;
    uint32_t hlsl_offset, hlsl_size;
} gs_gfx_blob_header;

typedef struct {
    uint8_t location;
    uint8_t type;                   
} gs_gfx_blob_input;
#pragma pack(pop)

#endif
