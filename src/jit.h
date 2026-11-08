





#pragma once

#include "libtcc.h"
#include "jit_runtime.h"
#include <filesystem>

namespace faloya {




inline string JitLibPath(const string &exedir) {
    if (auto env = getenv("FALOYA_TCCLIB")) return env;
    #ifdef FALOYA_TCC_LIB_PATH
        auto staged = string(FALOYA_TCC_LIB_PATH);
    #else
        auto staged = string();
    #endif
    auto beside = cat(exedir, "tcclib");
    
    
    if (auto f = fopen(cat(beside, "/include/tccdefs.h").c_str(), "rb")) {
        fclose(f);
        return beside;
    }
    return staged;
}



inline void JitDiag(void *opaque, const char *msg) {
    Append(*(string *)opaque, msg, "\n");
}





inline TCCState *CompileJit(const string &csrc, const string &libpath,
                            const vector<pair<string, const void *>> &symbols) {
    string diags;
    auto s = tcc_new();
    if (!s) throw CompileError { "libtcc: out of memory" };
    tcc_set_error_func(s, &diags, JitDiag);
    
    
    tcc_set_lib_path(s, libpath.c_str());
    tcc_set_options(s, "-nostdinc -nostdlib -Wl,-nostdlib");
    tcc_add_sysinclude_path(s, cat(libpath, "/faloya/include").c_str());
    tcc_add_sysinclude_path(s, cat(libpath, "/include").c_str());
    auto fail = [&](const char *what) {
        auto msg = cat("libtcc: ", what, "\n", diags);
        tcc_delete(s);
        throw CompileError { msg };
    };
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) fail("cannot target memory");
    
    #ifdef _WIN32
        auto support = cat(libpath, "/lib/libtcc1.a");
    #else
        auto support = cat(libpath, "/libtcc1.a");
    #endif
    for (auto p = faloya_jit_symbols(); p->name; ++p)
        tcc_add_symbol(s, p->name, p->address);
    for (auto &[name, addr] : symbols) tcc_add_symbol(s, name.c_str(), addr);
    if (tcc_compile_string(s, csrc.c_str()) < 0) fail("compiling the generated C failed");
    if (tcc_add_file(s, support.c_str()) < 0) fail("loading compiler support failed");
    if (tcc_relocate(s) < 0) fail("relocating the generated C failed");
    tcc_set_error_func(s, nullptr, nullptr);   
    return s;
}




inline int RunJit(TCCState *s, const string &progname, const vector<string> &progargs) {
    auto fail = [&](const char *what) {
        tcc_delete(s);
        throw CompileError { cat("libtcc: ", what) };
    };
    auto entry = (int (*)(int, char **))tcc_get_symbol(s, "main");
    auto cleanup = (void (*)(void))tcc_get_symbol(s, "gs_jit_cleanup");
    if (!entry || !cleanup) fail("cannot find the program entry points");
    std::error_code patherror;
    auto root = filesystem::absolute(filesystem::path(progname), patherror).parent_path();
    if (patherror) fail("cannot locate the entry source directory");
    auto bytes = root.generic_u8string();
    string resource_dir((const char *)bytes.data(), bytes.size());
    if (resource_dir.empty() || resource_dir.back() != '/') resource_dir += '/';
    
    
    auto name = progname;
    auto args = progargs;
    vector<char *> argv;
    argv.push_back(name.data());
    for (auto &a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    auto code = faloya_jit_run(entry, cleanup, (int)argv.size() - 1, argv.data(),
                              resource_dir.c_str());
    tcc_delete(s);
    return code;
}

}  
