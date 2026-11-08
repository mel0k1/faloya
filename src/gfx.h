





#pragma once

#include "shaderc.h"
#include "gfx_blob.h"

namespace faloya {



inline int ShaderStageNamed(string_view name) {
    if (name == "vert") return GS_GFX_STAGE_VERTEX;
    if (name == "frag") return GS_GFX_STAGE_FRAGMENT;
    if (name == "comp") return GS_GFX_STAGE_COMPUTE;
    return -1;
}


inline int ShaderStageOf(const string &path) {
    auto dot = path.find_last_of('.');
    return dot == string::npos ? -1 : ShaderStageNamed(string_view(path).substr(dot + 1));
}



inline string EmbeddedShaderPath(const string &from, string_view lit) {
    auto absolute = (!lit.empty() && (lit[0] == '/' || lit[0] == '\\')) ||
                    (lit.size() > 1 && lit[1] == ':');
    if (absolute) return string(lit);
    auto pos = from.find_last_of("/\\");
    return cat(pos == string::npos ? string() : from.substr(0, pos + 1), lit);
}







inline string CompileShader(const string &source, const string &path, int stage) {
    
    
    
    static map<tuple<int, string, string>, string> compiled;
    auto key = make_tuple(stage, path, source);
    if (auto it = compiled.find(key); it != compiled.end()) return it->second;
    auto r = gs_shaderc_compile(source.c_str(), path.c_str(), stage);
    if (!r.blob) {
        auto msg = string(r.error ? r.error : "shader compilation failed");
        gs_shaderc_free(&r);
        throw CompileError { msg };
    }
    string blob((const char *)r.blob, r.size);
    gs_shaderc_free(&r);
    return compiled.emplace(std::move(key), std::move(blob)).first->second;
}


inline string CompileShaderFile(const string &path) {
    auto stage = ShaderStageOf(path);
    if (stage < 0)
        throw CompileError { cat("cannot tell the shader stage of ", path,
                                 ": the extension must be .vert, .frag or .comp") };
    string src;
    if (!LoadFile(path, src)) throw CompileError { cat("cannot open shader file: ", path) };
    return CompileShader(src, path, stage);
}




inline bool ShaderMessageAt(const string &err, const string &path, int &line, string &msg) {
    if (err.compare(0, path.size(), path) || err.size() < path.size() + 2 ||
        err[path.size()] != ':')
        return false;
    auto rest = string_view(err).substr(path.size() + 1);
    line = 0;
    while (!rest.empty() && isdigit((uint8_t)rest[0])) {
        line = line * 10 + (rest[0] - '0');
        rest.remove_prefix(1);
    }
    auto sep = string_view(line ? ": error: " : " ");
    if (rest.substr(0, sep.size()) != sep) return false;
    msg = string(rest.substr(sep.size()));
    return true;
}



inline void DumpShader(const string &path, const string &source) {
    auto blob = CompileShaderFile(path);
    gs_gfx_blob_header h;
    memcpy(&h, blob.data(), sizeof h);
    static const char *stages[] = { "vertex", "fragment", "compute" };
    string s = cat(stages[h.stage], " shader, ", blob.size(), " bytes: SPIR-V ", h.spirv_size,
                   ", MSL ", h.msl_size, ", HLSL ", h.hlsl_size, "\n");
    Append(s, "samplers ", h.num_samplers, ", storage textures ", h.num_storage_textures_ro,
           " ro / ", h.num_storage_textures_rw, " rw, storage buffers ", h.num_storage_buffers_ro,
           " ro / ", h.num_storage_buffers_rw, " rw, uniform blocks ", h.num_uniform_buffers);
    auto p = blob.data() + sizeof h;
    for (int i = 0; i < h.num_uniform_buffers; i++, p += 4) {
        uint32_t size;
        memcpy(&size, p, 4);
        Append(s, i ? ", " : " (", size, " bytes", i + 1 == h.num_uniform_buffers ? ")" : "");
    }
    s += "\n";
    if (h.stage == GS_GFX_STAGE_COMPUTE)
        Append(s, "local size ", h.local_size[0], " x ", h.local_size[1], " x ",
               h.local_size[2], "\n");
    for (int i = 0; i < h.num_inputs; i++, p += sizeof(gs_gfx_blob_input)) {
        gs_gfx_blob_input in;
        memcpy(&in, p, sizeof in);
        Append(s, "input location ", (int)in.location, " type ", (int)in.type, "\n");
    }
    if (source == "msl") Append(s, string_view(blob).substr(h.msl_offset, h.msl_size));
    else if (source == "hlsl") Append(s, string_view(blob).substr(h.hlsl_offset, h.hlsl_size));
    else if (!source.empty())
        throw CompileError { cat("--shader-source takes msl or hlsl, not ", source) };
    fputs(s.c_str(), stdout);
}

}  
