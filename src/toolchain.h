




#pragma once

#include <filesystem>
#include <thread>

namespace faloya {




inline string FaloyaHome() {
    string home;
    if (auto env = getenv("FALOYA_HOME"); env && *env) home = env;
    else {
        #ifdef _WIN32
            auto profile = getenv("USERPROFILE");
        #else
            auto profile = getenv("HOME");
        #endif
        home = cat(profile && *profile ? profile : ".", "/.fal");
    }
    for (auto &c : home) if (c == '\\') c = '/';
    while (home.size() > 1 && home.back() == '/') home.pop_back();
    return home;
}

struct Toolchain {
    string name;      
    string style;     
    string cc;        
    string cxx;       
    string version;
    string desc;      
    string id;        
    string how;       
};

struct ToolInfo {
    string path;      
    string version;
};

struct Tools {
    bool havecc = false;
    Toolchain cc;
    string ccwhy;     
    ToolInfo cmake, git;
    string home;
};



inline string FirstLine(const string &text) {
    size_t i = 0;
    while (i < text.size()) {
        auto end = text.find('\n', i);
        if (end == string::npos) end = text.size();
        auto line = text.substr(i, end - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) return line;
        i = end + 1;
    }
    return "";
}



inline string VersionIn(const string &line) {
    string lower = line;
    for (auto &c : lower) c = (char)tolower((unsigned char)c);
    auto at = lower.find("version ");
    if (at != string::npos) {
        auto start = at + 8;
        auto end = line.find_first_of(" \t,", start);
        return line.substr(start, end == string::npos ? string::npos : end - start);
    }
    auto sp = line.find_last_of(' ');
    return sp == string::npos ? line : line.substr(sp + 1);
}

inline string Hex8(const string &s) {
    uint64_t h = 14695981039346656037ull;
    for (auto c : s) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[9];
    snprintf(buf, sizeof buf, "%08x", (unsigned)(h >> 32));
    return buf;
}

inline string BaseName(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? path : path.substr(pos + 1);
}

inline string LowerNoExe(string name) {
    for (auto &c : name) c = (char)tolower((unsigned char)c);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
        name.resize(name.size() - 4);
    return name;
}

inline bool PathExists(const string &path) {
    std::error_code ec;
    return filesystem::exists(filesystem::path(path), ec);
}





inline std::error_code RenamePath(const string &from, const string &to) {
    std::error_code ec;
    for (int wait = 1;; wait *= 2) {
        filesystem::rename(filesystem::path(from), filesystem::path(to), ec);
        #ifdef _WIN32
            
            auto held = ec.category() == std::system_category() &&
                        (ec.value() == 5 || ec.value() == 32);
            if (held && wait <= 1024) {
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                continue;
            }
        #endif
        return ec;
    }
}


inline bool WriteFileMakingDirs(const string &path, const string &text) {
    std::error_code ec;
    filesystem::create_directories(filesystem::path(path).parent_path(), ec);
    auto f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}







inline bool WriteFileIfChanged(const string &path, const string &text) {
    string old;
    if (LoadFile(path, old) && old == text) return true;
    auto tmp = cat(path, ".tmp");
    return WriteFileMakingDirs(tmp, text) && !RenamePath(tmp, path);
}




inline string ToolVersion(const string &path, const vector<string> &args) {
    vector<string> argv = { path };
    for (auto &a : args) argv.push_back(a);
    auto r = RunProcess(argv);
    return r.started ? VersionIn(FirstLine(r.output)) : "";
}

inline ToolInfo FindTool(const string &name, const vector<string> &extradirs = {}) {
    ToolInfo t;
    t.path = FindOnPath(name);
    for (auto &dir : extradirs) {
        if (!t.path.empty()) break;
        auto cand = cat(dir, "/", name);
        #ifdef _WIN32
            cand += ".exe";
        #endif
        if (PathExists(cand)) t.path = cand;
    }
    if (!t.path.empty()) t.version = ToolVersion(t.path, { "--version" });
    return t;
}





inline const char *InheritedPath() {
    static const string path = [] {
        auto p = getenv("PATH");
        return string(p ? p : "");
    }();
    return path.c_str();
}



#ifdef _WIN32


inline string VsInstallDir() {
    static string found;
    static bool probed = false;
    if (probed) return found;
    probed = true;
    auto pf = getenv("ProgramFiles(x86)");
    auto vswhere = cat(pf ? pf : "C:\\Program Files (x86)",
                       "\\Microsoft Visual Studio\\Installer\\vswhere.exe");
    if (!PathExists(vswhere)) return found;
    auto r = RunProcess({ vswhere, "-latest", "-prerelease", "-products", "*",
                          "-property", "installationPath" });
    if (r.started && r.code == 0) found = FirstLine(r.output);
    return found;
}






inline bool ImportVcvars(bool refresh) {
    auto vs = VsInstallDir();
    if (vs.empty()) return false;
    auto vcvars = cat(vs, "\\VC\\Auxiliary\\Build\\vcvars64.bat");
    if (!PathExists(vcvars)) return false;
    std::error_code ec;
    auto mtime = filesystem::last_write_time(filesystem::path(vcvars), ec).time_since_epoch().count();
    auto header = cat("# ", vcvars, " ", (long long)mtime, "\n");
    auto cachedir = cat(FaloyaHome(), "/toolchain");
    auto cachefile = cat(cachedir, "/vcvars64-", Hex8(vcvars), ".env");
    string env;
    if (!refresh && LoadFile(cachefile, env) && env.compare(0, header.size(), header) == 0) {
        env.erase(0, header.size());
    } else {
        
        
        auto bat = cat(cachedir, "/vcvars64-probe.bat");
        if (!WriteFileMakingDirs(bat, cat("@echo off\r\ncall \"", vcvars, "\" >nul 2>&1\r\nset\r\n")))
            return false;
        auto r = RunProcess({ "cmd.exe", "/c", bat });
        if (!r.started || r.code != 0) return false;
        env = r.output;
        WriteFileMakingDirs(cachefile, cat(header, env));
    }
    InheritedPath();   
    for (size_t i = 0; i < env.size();) {
        auto end = env.find('\n', i);
        if (end == string::npos) end = env.size();
        auto line = env.substr(i, end - i);
        i = end + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        
        if (eq == string::npos || eq == 0) continue;
        SetEnvVar(line.substr(0, eq), line.substr(eq + 1));
    }
    return true;
}

inline string VsLlvmTool(const string &name) {
    auto vs = VsInstallDir();
    if (vs.empty()) return "";
    auto path = cat(vs, "\\VC\\Tools\\Llvm\\x64\\bin\\", name, ".exe");
    return PathExists(path) ? path : "";
}

inline vector<string> VsCmakeDirs() {
    auto vs = VsInstallDir();
    if (vs.empty()) return {};
    return { cat(vs, "\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin") };
}

#endif


inline string CMakePath() {
    #ifdef _WIN32
        return FindTool("cmake", VsCmakeDirs()).path;
    #else
        return FindOnPath("cmake");
    #endif
}

inline string NinjaPath() {
    auto p = FindOnPath("ninja");
    #ifdef _WIN32
        if (p.empty()) {
            auto vs = VsInstallDir();
            if (!vs.empty()) {
                auto cand = cat(vs, "\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe");
                if (PathExists(cand)) p = cand;
            }
        }
    #endif
    return p;
}





inline Toolchain DescribeToolchain(const string &name, const string &style, const string &cc,
                                   const string &cxx, const string &how) {
    Toolchain t;
    t.name = name;
    t.style = style;
    t.cc = cc;
    t.cxx = cxx;
    t.how = how;
    if (name == "msvc") {
        t.version = ToolVersion(cc, {});
        auto toolset = getenv("VCToolsVersion");
        t.desc = cat("MSVC ", t.version, toolset ? cat(" (toolset ", toolset, ")") : string());
    } else {
        t.version = ToolVersion(cc, { "--version" });
        t.desc = cat(name, " ", t.version);
    }
    t.id = cat(name, "-", t.version, "-", Hex8(cat(cc, "|", style)));
    return t;
}



inline string DriverKind(const string &path) {
    auto r = RunProcess({ path, "--version" });
    if (!r.started) return "cc";
    string lower = r.output;
    for (auto &c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("clang") != string::npos) return "clang";
    if (lower.find("gcc") != string::npos || lower.find("free software foundation") != string::npos)
        return "gcc";
    return "cc";
}



inline string CxxBeside(const string &kind, const string &cc) {
    auto plus = kind == "clang" ? "clang++" : kind == "gcc" ? "g++" : "c++";
    auto dir = cc.substr(0, cc.find_last_of("/\\") + 1);
    for (auto ext : { "", ".exe" }) {
        auto cand = cat(dir, plus, ext);
        if (!dir.empty() && PathExists(cand)) return cand;
    }
    return FindOnPath(plus, InheritedPath());
}

inline Toolchain GccStyle(const string &cc, const string &how, const string &kind = "") {
    auto k = kind.empty() ? DriverKind(cc) : kind;
    return DescribeToolchain(k, "cc", cc, CxxBeside(k, cc), how);
}

inline Toolchain MsvcStyle(const string &name, const string &cc, const string &how) {
    return DescribeToolchain(name, "msvc", cc, cc, how);
}






inline bool FindToolchain(string request, Toolchain &out, string &why,
                          [[maybe_unused]] bool refresh = false) {
    
    ProgressStep step("finding the C compiler");
    string how = "--cc";
    if (request.empty()) {
        if (auto env = getenv("FALOYA_CC"); env && *env) {
            request = env;
            how = "FALOYA_CC";
        }
    }
    if (request.empty()) {
        bool found;
        auto cfg = LoadBuildFile(cat(FaloyaHome(), "/config"), found);
        if (found) {
            cfg.CheckKeys({ "cc" });
            request = cfg.Str("cc");
            how = "FALOYA_HOME/config";
        }
    }
    auto lowered = request;
    for (auto &c : lowered) c = (char)tolower((unsigned char)c);
    auto bypath = request.find_first_of("/\\") != string::npos ||
                  (lowered.size() > 4 && lowered.compare(lowered.size() - 4, 4, ".exe") == 0);
    if (!request.empty() && bypath) {
        if (!PathExists(request)) {
            why = cat("the C compiler ", request, " (", how, ") is not there");
            return false;
        }
        auto base = LowerNoExe(BaseName(request));
        if (base == "cl" || base == "clang-cl") {
            #ifdef _WIN32
                if (base == "cl" && FindOnPath("cl").empty()) ImportVcvars(refresh);
            #endif
            out = MsvcStyle(base == "cl" ? "msvc" : base, request, how);
        } else {
            out = GccStyle(request, how);
        }
        return true;
    }
    auto name = request;
    if (name == "cl") name = "msvc";
    auto wanted = [&](const char *n) { return name.empty() || name == n; };
    #ifdef _WIN32
        if (wanted("msvc")) {
            if (auto cl = FindOnPath("cl", InheritedPath()); !cl.empty()) {
                out = MsvcStyle("msvc", cl, "on PATH");
                return true;
            }
        }
        if (wanted("clang-cl")) {
            if (auto p = VsLlvmTool("clang-cl"); !p.empty()) {
                out = MsvcStyle("clang-cl", p, "in Visual Studio");
                return true;
            }
            if (auto p = FindOnPath("clang-cl", InheritedPath()); !p.empty()) {
                out = MsvcStyle("clang-cl", p, "on PATH");
                return true;
            }
        }
        if (wanted("clang")) {
            if (auto p = FindOnPath("clang", InheritedPath()); !p.empty()) {
                out = GccStyle(p, "on PATH", "clang");
                return true;
            }
            if (auto p = VsLlvmTool("clang"); !p.empty()) {
                out = GccStyle(p, "in Visual Studio", "clang");
                return true;
            }
            
            if (auto pf = getenv("ProgramFiles")) {
                auto p = cat(pf, "\\LLVM\\bin\\clang.exe");
                if (PathExists(p)) {
                    out = GccStyle(p, "in Program Files\\LLVM", "clang");
                    return true;
                }
            }
        }
        if (wanted("msvc")) {
            
            
            if (ImportVcvars(refresh)) {
                if (auto cl = FindOnPath("cl"); !cl.empty()) {
                    out = MsvcStyle("msvc", cl, "in Visual Studio (vcvars64)");
                    return true;
                }
            }
        }
        if (wanted("gcc")) {
            if (auto p = FindOnPath("gcc", InheritedPath()); !p.empty()) {
                out = GccStyle(p, "on PATH", "gcc");
                return true;
            }
        }
        if (wanted("cc")) {
            if (auto p = FindOnPath("cc", InheritedPath()); !p.empty()) {
                out = GccStyle(p, "on PATH");
                return true;
            }
        }
        if (!name.empty() && name != "msvc" && name != "clang-cl" && name != "clang" &&
            name != "gcc" && name != "cc") {
            why = cat("unknown C compiler name ", request, " (", how,
                      "): the names are msvc, clang-cl, clang, gcc and cc, or give a path");
            return false;
        }
        why = name.empty()
            ? "cannot find a C compiler (looked for cl, clang-cl and clang on PATH and in "
              "Visual Studio, for clang in Program Files\\LLVM, and for gcc and cc on PATH); "
              "install Visual Studio with C++ or LLVM, or set FALOYA_CC"
            : cat("cannot find the C compiler ", request, " (", how, ") on PATH or in Visual Studio");
        return false;
    #else
        for (auto n : { "cc", "gcc", "clang" }) {
            if (!wanted(n)) continue;
            if (auto p = FindOnPath(n); !p.empty()) {
                
                
                out = GccStyle(p, "on PATH", string(n) == "cc" ? "" : n);
                return true;
            }
        }
        if (!name.empty() && name != "clang" && name != "gcc" && name != "cc") {
            why = cat("unknown C compiler name ", request, " (", how,
                      "): the names are clang, gcc and cc, or give a path");
            return false;
        }
        why = name.empty()
            ? "cannot find a C compiler (looked for cc, gcc and clang on PATH); install one, "
              "or set FALOYA_CC"
            : cat("cannot find the C compiler ", request, " (", how, ") on PATH");
        return false;
    #endif
}


inline Tools FindTools(const string &ccrequest, bool refresh = false) {
    Tools t;
    t.home = FaloyaHome();
    t.havecc = FindToolchain(ccrequest, t.cc, t.ccwhy, refresh);
    #ifdef _WIN32
        t.cmake = FindTool("cmake", VsCmakeDirs());
    #else
        t.cmake = FindTool("cmake");
    #endif
    t.git = FindTool("git");
    return t;
}




inline string ToolsReport(const Tools &t, bool json, const vector<string> &builtin) {
    string s;
    if (json) {
        auto q = [](const string &v) {
            string out;
            JsonQuote(out, v);
            return out;
        };
        auto tool = [&](const ToolInfo &ti) {
            return ti.path.empty() ? string("null")
                 : cat("{\"path\": ", q(ti.path), ", \"version\": ", q(ti.version), "}");
        };
        s += "{\n";
        if (t.havecc) {
            auto &c = t.cc;
            Append(s, "  \"cc\": {\"name\": ", q(c.name), ", \"style\": ", q(c.style),
                   ", \"path\": ", q(c.cc), ", \"cxx\": ", q(c.cxx),
                   ", \"version\": ", q(c.version), ", \"desc\": ", q(c.desc),
                   ", \"id\": ", q(c.id), ", \"how\": ", q(c.how), "},\n");
        } else {
            Append(s, "  \"cc\": null,\n  \"cc_error\": ", q(t.ccwhy), ",\n");
        }
        Append(s, "  \"cmake\": ", tool(t.cmake), ",\n  \"git\": ", tool(t.git), ",\n");
        s += "  \"builtin\": [";
        for (size_t i = 0; i < builtin.size(); i++) Append(s, i ? ", " : "", q(builtin[i]));
        Append(s, "],\n  \"home\": ", q(t.home), "\n}\n");
        return s;
    }
    if (t.havecc) {
        Append(s, "C compiler:   ", t.cc.desc, ", found ", t.cc.how, "\n              ", t.cc.cc, "\n");
        Append(s, "C++ compiler: ", t.cc.cxx.empty() ? "none" : t.cc.cxx, "\n");
        Append(s, "style:        ", t.cc.style, "\nid:           ", t.cc.id, "\n");
    } else {
        Append(s, "C compiler:   ", t.ccwhy, "\n");
    }
    auto tool = [&](const char *name, const ToolInfo &ti) {
        Append(s, name, ti.path.empty() ? string("not found on PATH")
                                              : cat(ti.path, " (", ti.version, ")"), "\n");
    };
    tool("cmake:       ", t.cmake);
    tool("git:         ", t.git);
    string mods;
    for (auto &m : builtin) Append(mods, mods.empty() ? "" : ", ", m);
    Append(s, "built in:     ", mods.empty() ? "no packages" : mods, "\n");
    Append(s, "FALOYA_HOME:   ", t.home, "\n");
    return s;
}

}  
