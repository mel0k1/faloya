

#include "includes.h"
#include "utils.h"
#include "small_vector.h"
#include "flat_map.h"
#include "lexer.h"
#include "ast.h"
#include "dump.h"
#include "clone.h"
#include "parser.h"
#include "resolve.h"
#include "builtins.h"
#include "gfx.h"
#include "typecheck.h"
#include "typecheck_types.h"
#include "typecheck_exprs.h"
#include "typecheck_expansion.h"
#include "typecheck_flow.h"
#include "typecheck_calls.h"
#include "typecheck_builtins.h"
#include "typecheck_nodes.h"
#include "optimize.h"
#include "optimize_basecase.h"
#include "optimize_tre.h"
#include "optimize_loops.h"
#include "optimize_reassoc.h"
#include "bce.h"
#include "codegen.h"
#include "codegen_types.h"
#include "codegen_frames.h"
#include "codegen_values.h"
#include "codegen_construct.h"
#include "codegen_stmts.h"
#include "codegen_calls.h"
#include "codegen_builtins.h"
#include "codegen_render.h"
#include "codegen_emit.h"
#include "codegen_stacks.h"
#include "codegen_nodes.h"
#include "runtime_inline.h"
#include "process.h"
#include "jit.h"
#include "progress.h"
#include "buildfile.h"
#include "json.h"
#include "toolchain.h"
#include "builtin.h"
#include "build.h"
#include "packages.h"
#include "native.h"
#include "format.h"
#include "lsp_rpc.h"
#include "lsp_analysis.h"
#include "lsp_workspace.h"
#include "lsp_features.h"
#include "lsp.h"

namespace faloya {


const char *CompilerVersion();

string DirOf(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? "" : path.substr(0, pos + 1);
}

string ExePath(const char *argv0);

static bool FileExists(const string &path) {
    auto f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

static string SourcePath(const string &stem) {
    auto p = cat(stem, ".fal");
    if (FileExists(p)) return p;
    auto g = cat(stem, ".goose");
    return FileExists(g) ? g : p;
}





static vector<string> PathParts(const string &path) {
    vector<string> parts;
    string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        auto c = i < path.size() ? path[i] : '/';
        if (c != '/' && c != '\\') { cur += c; continue; }
        if (i == 0 && i < path.size()) parts.push_back(cur);
        else if (cur == "." || cur.empty()) {}
        else if (cur == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(cur);
        cur.clear();
    }
    return parts;
}






string RelativeTo(const string &path, const string &dir) {
    auto p = PathParts(path), d = PathParts(dir);
    if (p.empty()) return path;
    auto updir = [](const vector<string> &parts) {
        for (auto &part : parts) if (part == "..") return true;
        return false;
    };
    if (updir(p) || updir(d)) return path;
    
    auto rooted = [](const vector<string> &parts) {
        return !parts.empty() && (parts[0].empty() || parts[0].back() == ':');
    };
    if (rooted(p) != rooted(d)) return path;
    if (rooted(p) && p[0] != d[0]) return path;
    
    size_t i = 0;
    while (i + 1 < p.size() && i < d.size() && p[i] == d[i]) i++;
    string rel;
    for (size_t up = i; up < d.size(); up++) rel += "../";
    for (; i < p.size(); i++) {
        rel += p[i];
        if (i + 1 < p.size()) rel += '/';
    }
    return rel;
}







vector<string> StdlibDirs(const string &stdlibdir, const string &exe) {
    vector<string> cands;
    if (!stdlibdir.empty()) cands.push_back(cat(stdlibdir, "/"));
    if (auto env = getenv("FALOYA_STDLIB")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    cands.push_back(cat(exedir, "stdlib/"));
    cands.push_back(cat(exedir, "../stdlib/"));
    cands.push_back(cat(exedir, "../../stdlib/"));
    cands.push_back(cat(exedir, "../../../stdlib/"));
    #ifdef FALOYA_STDLIB_PATH
        if (*FALOYA_STDLIB_PATH) cands.push_back(cat(FALOYA_STDLIB_PATH, "/"));
    #endif
    cands.push_back("stdlib/");
    vector<string> dirs;
    set<string> seen;
    for (auto &dir : cands) {
        std::error_code ec;
        auto canon = filesystem::weakly_canonical(filesystem::path(dir), ec);
        
        if (seen.insert(ec ? dir : canon.string()).second)
            dirs.push_back(filesystem::path(dir).lexically_normal().generic_string());
    }
    return dirs;
}



struct DeclaredPackage {
    PackageSource source;
    int pkg = 0;             
    int line = 0;            
};


static Diagnostic ImportDiagnostic(const string &file, const Import &imp, const string &msg) {
    Diagnostic d;
    d.where = LocOf(imp.line);
    if (imp.end.line > 0) {
        d.where.endline = imp.end.line;
        d.where.endcol = imp.end.col;
    }
    d.message = msg;
    d.text = RenderDiagnostic(d, file, {});
    return d;
}

[[noreturn]] static void ImportError(const string &file, const Import &imp, const string &msg) {
    auto d = ImportDiagnostic(file, imp, msg);
    throw CompileError { d.text, { d } };
}















void ParseProgram(Ast &ast, const string &rootpath, const vector<string> &stdlibdirs,
                  const string &exedir, const SourceProvider &sources = {}) {
    
    
    auto rootdir = DirOf(rootpath);
    ast.packages.push_back(Package { PathStem(rootpath), rootdir, false });
    LoadSidecar(ast.packages[0], true);
    auto stdlibpkg = [&](const string &dir) {
        for (size_t i = 0; i < ast.packages.size(); i++)
            if (ast.packages[i].stdlib && ast.packages[i].root == dir) return (int)i;
        ast.packages.push_back(Package { "stdlib", dir, true });
        return (int)ast.packages.size() - 1;
    };
    map<string, DeclaredPackage> declared;
    
    
    
    auto checkroot = [&](const string &name, const string &root) {
        for (size_t i = 1; i < ast.packages.size(); i++)
            if (!ast.packages[i].stdlib && ast.packages[i].root == root)
                throw CompileError { cat("the packages ", ast.packages[i].name, " and ", name,
                                         " have the same root, ", root,
                                         ": each package needs a directory of its own") };
    };
    
    
    
    
    
    auto loadblessed = [&](const BlessedEntry &be) -> int {
        auto it = declared.find(be.package);
        if (it != declared.end()) return it->second.pkg;
        auto builtin = BuiltinLookup(be.package);
        auto builtindir = builtin ? BuiltinSrcDir(*builtin, exedir) : string();
        PackageSource src;
        src.text = be.source;
        string root;
        if (builtin && PathExists(SourcePath(cat(builtindir, be.package)))) {
            src.ispath = true;
            src.dir = root = builtindir;
        } else if (IsPathSource(be.source)) {
            auto rel = be.source.rfind("./", 0) == 0 ? be.source.substr(2) : be.source;
            for (auto &dir : stdlibdirs) {
                auto cand = cat(dir, "../", rel);
                if (PathExists(SourcePath(cat(cand, "/", be.package)))) {
                    src.ispath = true;
                    src.dir = PackageDir(cand);
                    break;
                }
            }
            if (src.dir.empty())
                throw CompileError { cat("cannot find the blessed package ", be.package,
                                         " at ", be.source, " of the source tree") };
            root = PackageRoot(src, be.package);
        } else {
            src = ParsePackageSource(cat(be.source, "@", be.ref), "");
            root = PackageRoot(src, be.package);
        }
        checkroot(be.package, root);
        ast.packages.push_back(Package { be.package, root, false });
        auto &p = ast.packages.back();
        if (!src.ispath) p.cachekey = GitCacheKey(src);
        if (builtin && root == builtindir) p.builtin = BuiltinIndex(builtin);
        LoadSidecar(p, false);
        declared.emplace(be.package, DeclaredPackage { src, (int)ast.packages.size() - 1, 0 });
        return (int)ast.packages.size() - 1;
    };
    vector<pair<string, int>> queue = { { rootpath, 0 } };   
    set<string> loaded = { rootpath };
    map<string, int> fileof;                 
    vector<vector<string>> importsof;        
    
    map<string, pair<string, Import>> importedat;
    
    
    set<string> failedpkgs;
    while (!queue.empty()) {
        auto [path, pkg] = queue.back();
        queue.pop_back();
        auto contents = make_unique<string>();
        if (auto text = sources ? sources(path) : nullptr) {
            *contents = *text;
            StripBom(*contents);
        } else if (!LoadSource(path, *contents)) {
            auto it = importedat.find(path);
            if (it == importedat.end()) throw CompileError { cat("cannot open file: ", path) };
            ast.Report(ImportDiagnostic(it->second.first, it->second.second,
                                        cat("cannot open file: ", path)));
            continue;
        }
        auto fileidx = (int)ast.sources.size();
        ast.sources.emplace_back(path, std::move(contents));
        ast.filepkg.push_back(pkg);
        fileof[path] = fileidx;
        importsof.emplace_back();
        auto &[filename, source] = ast.sources.back();
        Parser parser(ast, filename, source->c_str(), fileidx);
        parser.ParseTop();
        for (auto &imp : parser.imports) {
            
            string pkgname;
            try {
                auto imppkg = pkg;
                string imppath;
                if (!imp.source.empty()) {
                    if (failedpkgs.count(imp.path)) continue;
                    pkgname = imp.path;
                    PackageSource src;
                    try {
                        src = ParsePackageSource(imp.source, DirOf(path));
                    } catch (CompileError &e) {
                        ImportError(filename, imp, e.msg);
                    }
                    auto it = declared.find(imp.path);
                    if (fileidx == 0) {
                        
                        if (it != declared.end())
                            ImportError(filename, imp, cat("the package ", imp.path,
                                                                " is declared twice"));
                        string root;
                        try {
                            root = PackageRoot(src, imp.path);
                            checkroot(imp.path, root);
                        } catch (CompileError &e) {
                            ImportError(filename, imp, e.msg);
                        }
                        ast.packages.push_back(Package { imp.path, root, false });
                        if (!src.ispath) ast.packages.back().cachekey = GitCacheKey(src);
                        LoadSidecar(ast.packages.back(), false);
                        auto dp = DeclaredPackage { src, (int)ast.packages.size() - 1,
                                                    imp.line.line };
                        it = declared.emplace(imp.path, dp).first;
                    } else {
                        
                        
                        if (it == declared.end())
                            if (auto be = BlessedLookup(imp.path); be && be->package == imp.path) {
                                loadblessed(*be);
                                it = declared.find(imp.path);
                            }
                    }
                    if (it == declared.end()) {
                        
                        
                        ImportError(filename, imp,
                                    cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                        ": add\n    import ", imp.path, " from \"",
                                        SourceForRoot(src, rootdir), "\";\nto ", rootpath,
                                        " (packages are declared by the root file only)"));
                    } else if (fileidx != 0 && !SameSource(it->second.source, src)) {
                        ImportError(filename, imp,
                                    cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                        " from ", src.text, ", which ", rootpath, ":",
                                        it->second.line, " declared from a different source, ",
                                        it->second.source.text));
                    } else if (fileidx != 0 && it->second.source.ref != src.ref) {
                        ast.Warn(LocOf(imp.line),
                                 cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                     " at ", src.ref, ", which ", rootpath, ":", it->second.line,
                                     " declares at ", it->second.source.ref));
                    }
                    imppkg = it->second.pkg;
                    
                    if (fileidx != 0 && pkg != imppkg) {
                        auto &req = ast.packages[pkg].required;
                        if (std::find(req.begin(), req.end(), imppkg) == req.end())
                            req.push_back(imppkg);
                    }
                    imppath = SourcePath(cat(ast.packages[imppkg].root, imp.path));
                } else if (imp.relative) {
                    imppath = SourcePath(cat(DirOf(path), imp.path));
                } else {
                    
                    
                    
                    
                    auto local = SourcePath(cat(ast.packages[pkg].root, imp.path));
                    auto first = imp.path.substr(0, imp.path.find('/'));
                    auto rest = imp.path.size() > first.size() ? imp.path.substr(first.size() + 1)
                                                               : first;
                    auto it = declared.find(first);
                    
                    if (it != declared.end() && it->second.pkg == pkg) it = declared.end();
                    if (it == declared.end() && failedpkgs.count(first)) continue;
                    auto blessed = it == declared.end() && !FileExists(local) ? BlessedLookup(first)
                                                                               : nullptr;
                    if (blessed) pkgname = blessed->package;
                    if (it != declared.end()) {
                        if (FileExists(local))
                            ImportError(filename, imp,
                                        cat(imp.path, " is both a file of this package (", local,
                                            ") and the declared package ", first, "'s module"));
                        imppkg = it->second.pkg;
                        imppath = SourcePath(cat(ast.packages[imppkg].root, rest));
                    } else if (blessed) {
                        try {
                            imppkg = loadblessed(*blessed);
                        } catch (CompileError &e) {
                            ImportError(filename, imp, e.msg);
                        }
                        imppath = SourcePath(cat(ast.packages[imppkg].root, rest));
                    } else {
                        imppath = local;
                        if (!FileExists(local)) {
                            for (auto &dir : stdlibdirs) {
                                auto cand = SourcePath(cat(dir, imp.path));
                                if (FileExists(cand)) {
                                    imppath = cand;
                                    imppkg = stdlibpkg(dir);
                                    break;
                                }
                            }
                            
                            
                            if (imppath == local) {
                                auto name = imp.path;
                                for (auto &c : name) if (c == '/') c = '.';
                                auto msg = cat("cannot find the module ", name,
                                               ", looked for\n    ",
                                               ForwardSlashes(AbsolutePath(local)));
                                auto havestd = false;
                                for (auto &dir : stdlibdirs)
                                    if (FileExists(cat(dir, "std.fal"))) {
                                        havestd = true;
                                        auto cand = SourcePath(cat(dir, imp.path));
                                        Append(msg, "\n    ", ForwardSlashes(AbsolutePath(cand)));
                                    }
                                if (!havestd) {
                                    Append(msg, "\nand found no standard library, looking in");
                                    for (auto &dir : stdlibdirs)
                                        Append(msg, "\n    ", ForwardSlashes(AbsolutePath(dir)));
                                    Append(msg, "\nset --stdlib or FALOYA_STDLIB to its directory");
                                }
                                ImportError(filename, imp, msg);
                            }
                        }
                    }
                }
                importsof[fileidx].push_back(imppath);
                ast.imports.push_back({ imp.line, imp.end, imppath });
                if (loaded.insert(imppath).second) {
                    queue.push_back({ imppath, imppkg });
                    importedat.emplace(imppath, make_pair(filename, imp));
                }
            } catch (CompileError &e) {
                if (e.diags.empty()) ast.Report(ImportDiagnostic(filename, imp, e.msg));
                for (auto &d : e.diags) ast.Report(d);
                if (!pkgname.empty() && !declared.count(pkgname)) failedpkgs.insert(pkgname);
            }
        }
    }
    
    
    
    
    
    
    
    vector<int> rank(ast.sources.size(), 0);
    vector<char> state(ast.sources.size(), 0);   
    auto next = 0;
    std::function<void(int)> visit = [&](int f) {
        if (state[f]) return;
        state[f] = 1;
        for (auto &p : importsof[f]) {
            auto it = fileof.find(p);
            if (it != fileof.end()) visit(it->second);
        }
        state[f] = 2;
        rank[f] = next++;
    };
    for (size_t i = 0; i < ast.sources.size(); i++) visit((int)i);
    std::stable_sort(ast.globals.begin(), ast.globals.end(),
                     [&](VarDecl *a, VarDecl *b) {
                         return rank[a->line.fileidx] < rank[b->line.fileidx];
                     });
    
    
    
    
    
    vector<set<string>> scanned(ast.packages.size());
    vector<bool> isscanned(ast.packages.size());
    auto scannedlibs = [&](size_t p) -> const set<string> & {
        if (!isscanned[p]) {
            PackageExternNames(ast, (int)p, &scanned[p]);
            isscanned[p] = true;
        }
        return scanned[p];
    };
    for (size_t p = 0; p < ast.packages.size(); p++) {
        auto &pkg = ast.packages[p];
        for (auto &d : pkg.dylibs) {
            auto declared = false;
            for (auto sf : ast.functions)
                if (sf->dylib == d.name && (p == 0 || ast.filepkg[sf->line.fileidx] == (int)p))
                    declared = true;
            for (size_t q = 1; q < ast.packages.size() && !declared; q++)
                if ((p == 0 || q == p) && !ast.packages[q].stdlib && ast.packages[q].builtin < 0 &&
                    !ast.packages[q].root.empty() && scannedlibs(q).count(d.name))
                    declared = true;
            if (!declared)
                throw CompileError { cat(pkg.sidecar, ": dylibs maps ", d.name, ", which no "
                                         "extern block ", p == 0 ? "in the program" : "of the package",
                                         " names") };
        }
    }
}




void CheckRoundtrip(const string &dumped, const string &path,
                    const vector<string> &stdlibdirs, const string &exedir) {
    Ast again;
    try {
        ParseProgram(again, path, stdlibdirs, exedir,
                     [&](const string &p) -> const string * { return p == path ? &dumped : nullptr; });
    } catch (CompileError &e) {
        throw CompileError { cat("the dump does not parse again:\n", e.msg) };
    }
    string redumped;
    again.Dump(redumped);
    if (redumped == dumped) return;
    
    size_t pos = 0, line = 1;
    while (pos < dumped.size() && pos < redumped.size() && dumped[pos] == redumped[pos]) {
        if (dumped[pos] == '\n') line++;
        pos++;
    }
    auto lineat = [&](const string &s) {
        auto start = pos ? s.rfind('\n', pos - 1) : string::npos;
        start = start == string::npos ? 0 : start + 1;
        auto end = s.find('\n', pos);
        return s.substr(start, end == string::npos ? string::npos : end - start);
    };
    throw CompileError { cat("the dump does not roundtrip: its line ", line, " reads\n    ",
                             lineat(dumped), "\nand dumps again as\n    ", lineat(redumped)) };
}

void DumpTokens(const string &path) {
    string contents;
    if (!LoadSource(path, contents)) throw CompileError { cat("cannot open file: ", path) };
    Lexer lex(path, contents.c_str());
    string s;
    while (lex.tok != T_EOF) {
        Append(s, TName(lex.tok));
        switch (lex.tok) {
            case T_IDENT:  Append(s, " ", lex.attr); break;
            case T_INTLIT: Append(s, " ", lex.ival, " (", lex.attr, ")"); break;
            case T_FLTLIT: Append(s, " ", lex.fval); break;
            case T_STRLIT: s += " "; EscapeString(s, lex.sval, '"'); break;
            default: break;
        }
        s += "\n";
        lex.Next();
    }
    fputs(s.c_str(), stdout);
}




static const char *runtimefiles[] = { "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                      "runtime_ext.h", "runtime_os.h", "faloya.h", "jit_c.h" };

static string RuntimeSections(std::initializer_list<const char *> names) {
    string s;
    for (auto name : names)
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == name)
                Append(s, "/* ==== ", rf.name, " ==== */\n", rf.text, "\n");
    return s;
}



string RuntimeVersion() {
    uint64_t h = 14695981039346656037ull;
    for (auto &rf : runtime_files)
        for (auto c : string_view(rf.text)) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[17];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(h >> 16));
    return buf;
}



string RuntimeObjectSource() {
    return cat("/* The Faloya runtime, written by `faloya --emit-runtime`. Do not edit.\n"
               "   Compile it once, with the C compiler and flags of the programs it is\n"
               "   for, and link the object with every program `faloya -o` writes (all\n"
               "   but --standalone ones). It needs no -D: a program hands the runtime\n"
               "   its configuration as it starts. A program from another version of\n"
               "   the compiler fails to link against it, missing gs_rt_start_<version>. */\n\n"
               "#define GS_SEPARATE_RUNTIME 1\n#define GS_RUNTIME_OBJECT 1\n"
               "#define GS_NEED_THREADS 1\n#define GS_RUNTIME_VERSION ", RuntimeVersion(),
               "\n\n", RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                          "runtime_ext.h", "runtime_os.h" }));
}




string FindRuntimeDir(const char *exe) {
    vector<string> cands;
    if (auto env = getenv("FALOYA_RUNTIME")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    cands.push_back(cat(exedir, "runtime/"));
    cands.push_back(cat(exedir, "../../src/runtime/"));
    cands.push_back(cat(exedir, "../src/runtime/"));
    cands.push_back("src/runtime/");
    for (auto &dir : cands) {
        string probe;
        if (LoadFile(cat(dir, runtimefiles[0]), probe)) return dir;
    }
    throw CompileError { "cannot locate the runtime sources (src/runtime); "
                         "set FALOYA_RUNTIME" };
}



void GenRuntimeHeader(const char *exe) {
    auto dir = FindRuntimeDir(exe);
    string out = "// Generated by `faloya --gen-runtime-header` from src/runtime/. "
                 "Do not edit.\n#pragma once\n\nnamespace faloya {\n\n"
                 "struct RuntimeFile {\n    const char *name;\n    string_view text;\n"
                 "};\n\ninline const RuntimeFile runtime_files[] = {\n";
    for (auto rf : runtimefiles) {
        string src;
        if (!LoadFile(cat(dir, rf), src))
            throw CompileError { cat("cannot read runtime file: ", dir, rf) };
        if (src.find(")GSRT\"") != string::npos)
            throw CompileError { cat(rf, " contains the raw-string delimiter )GSRT\"") };
        Append(out, "    { \"", rf, "\", string_view(\n");
        
        
        size_t pos = 0;
        while (pos < src.size()) {
            auto end = std::min(pos + 8000, src.size());
            for (auto line = pos; line < end;) {
                auto nl = src.find('\n', line);
                if (nl == string::npos || nl >= end) break;
                if (src.find_first_not_of(" \t\r", line) >= nl) {
                    end = nl + 1;
                    break;
                }
                line = nl + 1;
            }
            if (end < src.size() && src[end - 1] != '\n') {
                auto nl = src.rfind('\n', end - 1);
                if (nl != string::npos && nl >= pos) end = nl + 1;
            }
            Append(out, "R\"GSRT(", string_view(src).substr(pos, end - pos), ")GSRT\"\n");
            pos = end;
        }
        if (src.empty()) out += "\"\"\n";
        out += "    ) },\n";
    }
    out += "};\n\n}  // namespace faloya\n";
    auto path = cat(dir, "../runtime_inline.h");
    auto f = fopen(path.c_str(), "wb");
    if (!f) throw CompileError { cat("cannot write ", path) };
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("wrote %s (%d bytes)\n", path.c_str(), (int)out.size());
}


int RunOnCompilerStack(const function<int()> &fn);




constexpr const char *MULTIMARK = "==== faloya --multi-test: exit";


constexpr const char *COMPILEOPTIONS =
R"(  --stats               Print what the compile did: the declarations parsed,
                        the specializations, the optimizations, the bounds
                        checks removed and the files written.
  --color when          Color errors and warnings: auto (on a terminal, unless
                        NO_COLOR is set), always or never.
  --diagnostics json    Print the errors and warnings as JSON on stdout, and
                        the --stats lines on stderr.
  --offline             Fetch nothing: a package that is not fetched is an
                        error at its import, which faloya fetch resolves.
  -D NAME[=VALUE]       Define a C macro in the generated program (or -DNAME).
  --stdlib dir          The standard library's directory (or FALOYA_STDLIB);
                        found from where faloya is by default.
  --update              Fetch packages at a tag or branch again (a commit is
                        never fetched again).
  --stack-reserve size  Address space reserved per data stack, from 64K, with a
                        K, M, G or T suffix (default 2G).
  --stacks              Report the data stacks the program and each worker can
                        take, and the cap that puts on hardware_threads().
  --no-bce              Keep every bounds check.
  --bce-lines           Report the bounds checks elided and kept on each line.
  --specs               Print the specializations of each function.
)";

constexpr const char *DEBUGOPTION =
R"(  --debug               Overflow, range and tag checks that name the source
                        line they fail on.
)";




string HelpText(const string &topic) {
    if (topic.empty() || topic == "help") return
R"(usage: faloya <command> file.fal [options]
       faloya file.fal [options] [-- program args...]

Commands:
  run        Compile a program and run it in this process, which faloya
             file.fal does too.
  build      Build an executable with a C compiler on this machine.
  check      Parse and typecheck a program, and run nothing.
  fetch      Fetch the packages a program declares.
  format     Rewrite Faloya files in the standard layout.
  toolchain  Report the C compiler, cmake and git found, and FALOYA_HOME.
  version    Print the compiler's version (or --version).
  lsp        Serve an editor over the Language Server Protocol.
  completion Print a shell's completion script for faloya.
  help       Print this (or --help, -h); with a command, that command's help.

Common options:
  -O0, -O1, -O2   Inlining level (default -O2).
  --debug         Overflow, range and tag checks that name the source line
                  they fail on.
  --stats         Print what the compile did (declarations, bounds checks
                  removed, files written).
  --color when    Color errors and warnings: auto (on a terminal, unless
                  NO_COLOR is set), always or never.
  -- args...      What follows is the program's arguments.

Environment:
  FALOYA_HOME      Fetched packages, build intermediates and settings
                  (default ~/.faloya).
  FALOYA_CC        The C compiler, where --cc is not given.
  FALOYA_STDLIB    The standard library, where --stdlib is not given.
  FALOYA_PROGRESS  plain or live: how progress shows on stderr.

faloya <command> --help, or faloya help <command>, lists a command's options.
faloya help c covers writing the C yourself (faloya -o out.c), and faloya help
internals the options for looking into the compiler and testing it. faloya
help --man prints all of the help as a man page.
)";
    if (topic == "run") return cat(
R"(usage: faloya run file.fal [options] [-- program args...]
       faloya file.fal [options] [-- program args...]

Compiles the program and runs it in this process with TinyCC, and exits with
its exit code. What follows -- is the program's arguments. A program that
starts threads needs faloya build.

Options:
  -O0, -O1, -O2         Inlining level (default -O2).
)", DEBUGOPTION, COMPILEOPTIONS,
R"(  --cc name|path        The C compiler for packages' native parts: msvc,
                        clang-cl, clang, gcc, cc or a path (or FALOYA_CC).
  -v                    Print each command that builds a native part, and
                        what it prints.
)");
    if (topic == "build") return cat(
R"(usage: faloya build file.fal [options]

Builds an executable beside the source with a C compiler on this machine
(faloya toolchain says which), linking the runtime and the native parts of
the packages the program uses. Intermediates go under FALOYA_HOME.

Options:
  -o exe                Where the executable goes.
  -O0, -O1, -O2         Inlining level (default -O2). The C compiler
                        optimizes at every level, unless --debug.
  --debug               Overflow, range and tag checks that name the source
                        line they fail on, and the C built for a debugger.
)", COMPILEOPTIONS,
R"(  --cc name|path        The C compiler: msvc, clang-cl, clang, gcc, cc or a
                        path (or FALOYA_CC; by default the first one found).
  --standalone          Compile the runtime into the program rather than link
                        the runtime object built once per C compiler.
  -v                    Print each command the build runs and what it prints.
  --dry-run             Print the commands the build would run, and run none.
)");
    if (topic == "check") return cat(
R"(usage: faloya check file.fal [options]

Parses and typechecks the program and what it imports, and writes and runs
nothing.

Options:
)", COMPILEOPTIONS);
    if (topic == "fetch") return
R"(usage: faloya fetch [--update] file.fal

Fetches the packages the program declares from git into FALOYA_HOME, and
compiles nothing. A run or a build fetches what is missing as well.

Options:
  --update              Fetch packages at a tag or branch again (a commit is
                        never fetched again).
)";
    if (topic == "format") return
R"(usage: faloya format [--check] file.fal|dir...
       faloya format [-]

Rewrites each file in the standard layout, in place: indentation, the
spaces between tokens, blank lines and trailing commas, with the types
and =s of a run of declarations lined up in columns. Line breaks stay
where they are. A directory stands for the .fal files under it, less
those in directories whose names start with a dot and in build trees. With
no files, or -, it formats standard input to standard output. A file that
does not parse is left as it is, with its errors.

Options:
  --check               Write nothing: list the files that are not formatted.

Exit status:
  0                     Every file is formatted (or now is).
  1                     --check found a file that is not.
  2                     A file does not parse or cannot be read or written,
                        or an option is wrong.
)";
    if (topic == "lsp") return
R"(usage: faloya lsp [--stdio] [--stdlib dir]
       faloya lsp --probe

Serves an editor over the Language Server Protocol, on stdin and stdout:
diagnostics as files change, hover, go to definition, references and
rename, document and workspace symbols, semantic highlighting, signature
help, completion, inlay hints, folding and selection ranges, code
lenses, and formatting as faloya format does. It analyzes the programs whose entry files the editor configures
(the initializationOptions' entryFiles, with the standard library's
directory as stdlib) and every open file none of them imports; the
initializationOptions' commands are the client's own commands a code lens
may name (faloya.run, faloya.showInstantiations). It fetches nothing: a
package not fetched is an error at its import, which faloya fetch resolves.

Options:
  --probe               Print the protocol version and exit.
  --stdio               Serve on stdin and stdout, as it does anyway.
  --stdlib dir          The standard library's directory (or FALOYA_STDLIB).
)";
    if (topic == "toolchain") return
R"(usage: faloya toolchain [--cc name|path] [--json] [--refresh]

Reports the C compiler a build would use, cmake, git, FALOYA_HOME and the
modules compiled into faloya. Exits with 1 when there is no C compiler.

Options:
  --cc name|path        Look for this C compiler: msvc, clang-cl, clang, gcc,
                        cc or a path (or FALOYA_CC).
  --json                Report as JSON.
  --refresh             Set up Visual Studio's environment again rather than
                        take the one kept from last time.
)";
    if (topic == "version") return
R"(usage: faloya version
       faloya --version

Prints the compiler's version and the runtime's: a hash of the runtime's
text, which the runtime that faloya --emit-runtime writes has to match.
)";
    if (topic == "completion") return
R"(usage: faloya completion bash|zsh|fish|powershell

Prints the script that completes faloya's commands, options and .fal
files in that shell. To have it in every shell:

  bash        faloya completion bash > ~/.local/share/bash-completion/completions/faloya
  zsh         faloya completion zsh > "${fpath[1]}/_faloya"
  fish        faloya completion fish > ~/.config/fish/completions/faloya.fish
  powershell  faloya completion powershell >> $PROFILE

or, for the shell at hand, source <(faloya completion bash) (or zsh).
)";
    if (topic == "c") return cat(
R"(usage: faloya -o out.c file.fal [options]
       faloya --emit-runtime runtime.c
       faloya --<package>-link msvc|cc

Writes the program's C, for a build of your own or a C host. It links with
the runtime that --emit-runtime writes, compiled once with the C compiler
and flags the programs use. faloya build --dry-run prints the commands for
the program at hand.

Options:
  -o out.c              Write the program's C rather than run it.
  --standalone          Write C that holds the whole runtime and builds alone.
  --header out.h        Also write a header for a C host, which supplies main,
                        starts the program through faloya_init and may end it
                        through faloya_exit.
  --include header.h    Have the C include this header (may be repeated).
  --jit                 Also run the program in this process.
  --emit-runtime out.c  Write the runtime's C.
  --<package>-link msvc|cc
                        Print the response file that links the native part of
                        a package compiled into faloya (--gfx-link cc).
  -O0, -O1, -O2         Inlining level (default -O2).
)", DEBUGOPTION, COMPILEOPTIONS);
    if (topic == "internals") return cat(
R"(usage: faloya [options] file.fal

Options for looking into what the compiler does, and for its test suite.

Options:
  --tokens              Print the file's tokens.
  --parse               Stop after parsing.
  --check               Stop after typechecking, as faloya check does.
  --dump                Print the parsed program back as source.
  --dump-docs           Print the documentation comments of the program's
                        declarations, fields and variants.
  --dump-file out       Write that dump to a file, and go on.
  --roundtrip           Check that the dump parses and dumps back to itself.
  --multi-test file...  Compile each file as a run of its own would, ending what
                        each printed with a line on stdout and on stderr:
                        `)", MULTIMARK, R"( <code> <file>`.
                        A % in -o stands for each file's name.
  --bce-test            Verify the `// bce:elide` and `// bce:keep` annotations.
  --unsafe-no-rf-check  Leave out the checks after calls that a `return ...
                        from` can pass through: unsound, for pricing them only.
  --gen-runtime-header  Regenerate src/runtime_inline.h from src/runtime/.

faloya format --verify file.fal|dir... formats each file in memory and
writes nothing: it checks the result as a format does, and that formatting
it again changes nothing, and skips a file that does not parse.
)");
    return "";
}







string DiagnosticsJson(const vector<Diagnostic> &diags, const Ast *ast) {
    string s = "[";
    auto where = [&](const DiagLoc &w) {
        if (!ast || w.fileidx < 0 || w.fileidx >= (int)ast->sources.size()) return;
        s += ", \"file\": ";
        JsonQuote(s, ast->sources[w.fileidx].first);
        Append(s, ", \"line\": ", w.line);
        if (w.col > 0) Append(s, ", \"col\": ", w.col);
        if (w.endline > 0) Append(s, ", \"endLine\": ", w.endline, ", \"endCol\": ", w.endcol);
    };
    for (size_t i = 0; i < diags.size(); i++) {
        auto &d = diags[i];
        Append(s, i ? ",\n  {" : "\n  {", "\"severity\": \"", d.warning ? "warning" : "error",
               "\", \"message\": ");
        JsonQuote(s, d.message);
        where(d.where);
        if (!d.related.empty()) {
            s += ", \"related\": [";
            for (size_t j = 0; j < d.related.size(); j++) {
                Append(s, j ? ", {" : "{", "\"message\": ");
                JsonQuote(s, d.related[j].message);
                where(d.related[j].where);
                s += "}";
            }
            s += "]";
        }
        s += ", \"text\": ";
        JsonQuote(s, d.text);
        s += "}";
    }
    s += diags.empty() ? "]\n" : "\n]\n";
    return s;
}


bool ParseSize(string s, uint64_t &out) {
    if (!s.empty() && toupper((unsigned char)s.back()) == 'B') s.pop_back();
    if (s.empty()) return false;
    uint64_t mult = 1;
    switch (toupper((unsigned char)s.back())) {
        case 'K': mult = 1ull << 10; s.pop_back(); break;
        case 'M': mult = 1ull << 20; s.pop_back(); break;
        case 'G': mult = 1ull << 30; s.pop_back(); break;
        case 'T': mult = 1ull << 40; s.pop_back(); break;
    }
    if (s.empty() || s.size() > 18 || s.find_first_not_of("0123456789") != string::npos)
        return false;
    auto n = strtoull(s.c_str(), nullptr, 10);
    if (n > UINT64_MAX / mult) return false;
    out = n * mult;
    return true;
}



bool ParseConfigValue(string s, uint64_t &out) {
    auto strip = [](string &t) {
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        size_t i = 0;
        while (i < t.size() && isspace((unsigned char)t[i])) i++;
        t.erase(0, i);
    };
    strip(s);
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = s.substr(1, s.size() - 2);
        strip(s);
    }
    auto num = [&](string t, uint64_t &v) {
        strip(t);
        while (!t.empty() && (tolower((unsigned char)t.back()) == 'u' ||
                              tolower((unsigned char)t.back()) == 'l'))
            t.pop_back();
        if (t.empty() || t.size() > 19 || t.find_first_not_of("0123456789") != string::npos)
            return false;
        v = strtoull(t.c_str(), nullptr, 10);
        return true;
    };
    if (auto sh = s.find("<<"); sh != string::npos) {
        uint64_t a, b;
        if (!num(s.substr(0, sh), a) || !num(s.substr(sh + 2), b) || b > 63) return false;
        out = a << b;
        return true;
    }
    return num(s, out);
}





CodeGen::StackConfig StackConfigOf(const vector<string> &cdefines) {
    string_view runtime;
    for (auto &rf : runtime_files)
        if (string_view(rf.name) == "runtime.h") runtime = rf.text;
    auto value = [&](const char *name, uint64_t &out) {
        auto key = cat("#define ", name, " ");
        if (auto at = runtime.find(key); at != string_view::npos) {
            auto line = runtime.substr(at + key.size());
            line = line.substr(0, line.find('\n'));
            if (auto c = line.find("/*"); c != string_view::npos) line = line.substr(0, c);
            ParseConfigValue(string(line), out);
        }
        for (auto &d : cdefines) {
            auto eq = d.find('=');
            if (d.substr(0, eq) == name)
                ParseConfigValue(eq == string::npos ? string("1") : d.substr(eq + 1), out);
        }
    };
    CodeGen::StackConfig cfg { 0, 0, 0, 0 };
    uint64_t maxstacks = 0;
    value("GS_STACK_RESERVE", cfg.reserve);
    value("GS_STACK_GAP", cfg.gap);
    value("GS_STACK_BUDGET", cfg.budget);
    value("GS_MAX_STACKS", maxstacks);
    cfg.maxstacks = (int64_t)maxstacks;
    return cfg;
}


static const char *const VERBS[] = { "run", "build", "check", "fetch", "format", "toolchain",
                                     "version", "lsp", "completion", "help" };


static size_t EditDistance(string_view a, string_view b) {
    vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) row[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        auto diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            auto up = row[j];
            row[j] = min({ row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1]) });
            diag = up;
        }
    }
    return row[b.size()];
}



static vector<string> HelpOptions(const string &topic) {
    vector<string> opts;
    auto text = HelpText(topic);
    for (size_t pos = 0; pos < text.size();) {
        auto e = text.find('\n', pos);
        if (e == string::npos) e = text.size();
        auto line = string_view(text).substr(pos, e - pos);
        pos = e + 1;
        if (line.substr(0, 3) != "  -") continue;
        auto head = line.substr(2, line.find("  ", 2) - 2);
        for (size_t p = 0; p < head.size();) {
            auto comma = head.find(", ", p);
            auto spelling = head.substr(p, comma == string_view::npos ? string_view::npos : comma - p);
            auto word = string(spelling.substr(0, spelling.find(' ')));
            if (word != "--" && word.size() > 1 && find(opts.begin(), opts.end(), word) == opts.end())
                opts.push_back(word);
            if (comma == string_view::npos) break;
            p = comma + 2;
        }
    }
    return opts;
}




static string CompletionScript(const string &shell) {
    vector<string> verbs(begin(VERBS), end(VERBS));
    vector<string> topics = verbs;
    topics.push_back("c");
    topics.push_back("internals");
    const vector<string> shells = { "bash", "zsh", "fish", "powershell" };
    
    map<string, string> summary;
    auto overview = HelpText("");
    for (auto &v : verbs) {
        auto at = overview.find(cat("\n  ", v, " "));
        if (at == string::npos) continue;
        auto start = overview.find_first_not_of(' ', at + 3 + v.size());
        auto end = overview.find('\n', start);
        summary[v] = overview.substr(start, end - start);
        
        while (overview.compare(end, 6, "\n     ") == 0) {
            auto next = overview.find_first_not_of(' ', end + 1);
            end = overview.find('\n', next);
            Append(summary[v], " ", overview.substr(next, end - next));
        }
    }
    
    vector<pair<string, vector<string>>> optsof;
    for (auto &v : verbs)
        if (v != "help" && v != "completion" && v != "version") optsof.push_back({ v, HelpOptions(v) });
    auto words = [](const vector<string> &ws) {
        string s;
        for (auto &w : ws) Append(s, s.empty() ? "" : " ", w);
        return s;
    };
    auto runopts = words(HelpOptions("run"));
    string s;
    if (shell == "bash") {
        Append(s, "# faloya completion bash: source this, or install it as\n"
                  "# ~/.local/share/bash-completion/completions/faloya\n"
                  "_faloya() {\n"
                  "    local cur=${COMP_WORDS[COMP_CWORD]} verb= i opts\n"
                  "    for ((i = 1; i < COMP_CWORD; i++)); do\n"
                  "        case ${COMP_WORDS[i]} in -*) ;; *) verb=${COMP_WORDS[i]}; break ;; esac\n"
                  "    done\n"
                  "    case $verb in\n");
        for (auto &[v, opts] : optsof) Append(s, "        ", v, ") opts=\"", words(opts), "\" ;;\n");
        Append(s, "        help) COMPREPLY=($(compgen -W \"", words(topics), "\" -- \"$cur\")); return ;;\n"
                  "        completion) COMPREPLY=($(compgen -W \"", words(shells), "\" -- \"$cur\")); return ;;\n"
                  "        version) return ;;\n"
                  "        *) opts=\"", runopts, " --help --version\" ;;\n"
                  "    esac\n"
                  "    if [[ $cur == -* ]]; then COMPREPLY=($(compgen -W \"$opts\" -- \"$cur\")); return; fi\n"
                  "    COMPREPLY=()\n"
                  "    [[ -z $verb ]] && COMPREPLY=($(compgen -W \"", words(verbs), "\" -- \"$cur\"))\n"
                  "    COMPREPLY+=($(compgen -f -X '!*.fal' -- \"$cur\") $(compgen -d -- \"$cur\"))\n"
                  "}\n"
                  "complete -o filenames -F _faloya faloya\n");
    } else if (shell == "zsh") {
        Append(s, "#compdef faloya\n"
                  "# faloya completion zsh: source this, or install it as _faloya in a\n"
                  "# directory on $fpath\n"
                  "_faloya() {\n"
                  "    local verb word\n"
                  "    for word in ${words[2,CURRENT-1]}; do\n"
                  "        [[ $word == -* ]] || { verb=$word; break }\n"
                  "    done\n"
                  "    local -a opts\n"
                  "    case $verb in\n");
        for (auto &[v, opts] : optsof) Append(s, "        ", v, ") opts=(", words(opts), ") ;;\n");
        Append(s, "        help) compadd -- ", words(topics), "; return ;;\n"
                  "        completion) compadd -- ", words(shells), "; return ;;\n"
                  "        version) return ;;\n"
                  "        *) opts=(", runopts, " --help --version) ;;\n"
                  "    esac\n"
                  "    if [[ $PREFIX == -* ]]; then compadd -- $opts; return; fi\n"
                  "    if [[ -z $verb ]]; then\n"
                  "        local -a commands\n"
                  "        commands=(\n");
        for (auto &v : verbs) {
            string d;
            for (auto c : summary[v]) { if (c == '\'') d += "'\\''"; else if (c == ':') d += "\\:"; else d += c; }
            Append(s, "            '", v, ":", d, "'\n");
        }
        Append(s, "        )\n"
                  "        _describe command commands\n"
                  "    fi\n"
                  "    _files -g '*.fal'\n"
                  "}\n"
                  "if [[ $zsh_eval_context[-1] == loadautofunc ]]; then _faloya \"$@\"; else compdef _faloya faloya; fi\n");
    } else if (shell == "fish") {
        auto quote = [](const string &t) {
            string q = "'";
            for (auto c : t) { if (c == '\'' || c == '\\') q += '\\'; q += c; }
            return q + "'";
        };
        Append(s, "# faloya completion fish: install it as ~/.config/fish/completions/faloya.fish\n"
                  "complete -c faloya -f\n");
        for (auto &v : verbs)
            Append(s, "complete -c faloya -n __fish_use_subcommand -a ", v, " -d ", quote(summary[v]), "\n");
        auto flag = [&](const string &cond, const string &o) {
            Append(s, "complete -c faloya -n ", quote(cond), " ");
            if (o.rfind("--", 0) == 0) Append(s, "-l ", o.substr(2));
            else if (o.size() == 2) Append(s, "-s ", o.substr(1));
            else Append(s, "-o ", o.substr(1));
            s += "\n";
        };
        for (auto &[v, opts] : optsof)
            for (auto &o : opts) flag(cat("__fish_seen_subcommand_from ", v), o);
        string verbset;
        for (auto &v : verbs) Append(verbset, " ", v);
        for (auto &o : HelpOptions("run")) flag(cat("not __fish_seen_subcommand_from", verbset), o);
        Append(s, "complete -c faloya -n '__fish_seen_subcommand_from help' -a ", quote(words(topics)), "\n"
                  "complete -c faloya -n '__fish_seen_subcommand_from completion' -a ", quote(words(shells)), "\n"
                  "complete -c faloya -n 'not __fish_seen_subcommand_from help completion version toolchain lsp' "
                  "-a '(__fish_complete_suffix .fal)'\n");
    } else if (shell == "powershell") {
        auto list = [](const vector<string> &ws) {
            string l = "@(";
            for (size_t i = 0; i < ws.size(); i++) Append(l, i ? ", " : "", "'", ws[i], "'");
            return l + ")";
        };
        Append(s, "# faloya completion powershell: add this to $PROFILE\n"
                  "Register-ArgumentCompleter -Native -CommandName faloya, faloya.exe -ScriptBlock {\n"
                  "    param($wordToComplete, $commandAst, $cursorPosition)\n"
                  "    $words = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })\n"
                  "    if ($wordToComplete -and $words.Count -gt 0) { $words = @($words | Select-Object -First ($words.Count - 1)) }\n"
                  "    $verb = $words | Where-Object { $_ -notlike '-*' } | Select-Object -First 1\n"
                  "    $opts = switch ($verb) {\n");
        for (auto &[v, opts] : optsof) Append(s, "        '", v, "' { ", list(opts), " }\n");
        auto runlist = HelpOptions("run");
        runlist.push_back("--help");
        runlist.push_back("--version");
        Append(s, "        default { ", list(runlist), " }\n"
                  "    }\n"
                  "    $candidates = if ($verb -eq 'help') { ", list(topics), " }\n"
                  "        elseif ($verb -eq 'completion') { ", list(shells), " }\n"
                  "        elseif ($verb -eq 'version') { @() }\n"
                  "        elseif ($wordToComplete -like '-*') { $opts }\n"
                  "        else {\n"
                  "            $dir = Split-Path $wordToComplete\n"
                  "            $files = @(Get-ChildItem -Path \"$wordToComplete*\" -ErrorAction SilentlyContinue |\n"
                  "                Where-Object { $_.PSIsContainer -or $_.Extension -eq '.fal' } |\n"
                  "                ForEach-Object { if ($dir) { Join-Path $dir $_.Name } else { $_.Name } })\n"
                  "            if ($verb) { $files } else { ", list(verbs), " + $files }\n"
                  "        }\n"
                  "    $candidates | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
                  "        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
                  "    }\n"
                  "}\n");
    }
    return s;
}



static string ManPage() {
    auto roff = [](const string &text) {
        string s = ".nf\n";
        for (size_t pos = 0; pos < text.size();) {
            auto e = text.find('\n', pos);
            if (e == string::npos) e = text.size();
            auto line = string_view(text).substr(pos, e - pos);
            pos = e + 1;
            if (!line.empty() && (line[0] == '.' || line[0] == '\'')) s += "\\&";
            for (auto c : line) {
                if (c == '\\') s += "\\e";
                else if (c == '-') s += "\\-";
                else s += c;
            }
            s += '\n';
        }
        return s + ".fi\n";
    };
    auto overview = HelpText("");
    auto body = overview.substr(overview.find("\n\n") + 2);
    string s;
    Append(s, ".TH GOOSE 1 \"\" \"faloya ", CompilerVersion(), "\" \"Faloya Manual\"\n"
              ".SH NAME\n"
              "faloya \\- the Faloya compiler, its build tool and its language server\n"
              ".SH SYNOPSIS\n", roff(overview.substr(0, overview.find("\n\n"))),
              ".SH DESCRIPTION\n", roff(body), ".SH COMMANDS\n");
    for (auto v : VERBS) {
        if (string_view(v) == "help") continue;
        Append(s, ".SS ", v, "\n", roff(HelpText(v)));
    }
    Append(s, ".SH WRITING THE C\n", roff(HelpText("c")),
              ".SH INTERNALS\n", roff(HelpText("internals")));
    return s;
}

int Main(int argc, char **argv) {
    auto exe = ExePath(argv[0]);
    
    
    
    string verb;
    auto firstarg = 1;
    if (argc > 1) {
        string v = argv[1];
        for (auto known : VERBS)
            if (v == known) {
                verb = v;
                firstarg = 2;
            }
    }
    
    stderrcolor = StderrColor(false);
    
    
    for (int i = firstarg; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--") break;
        if (arg == "--help" || arg == "-h") {
            fputs(HelpText(verb).c_str(), stdout);
            return 0;
        }
        if (arg == "--version" && (verb.empty() || verb == "version")) verb = "version";
    }
    
    
    if (verb.empty() && argc > 1 && argv[1][0] != '-') {
        string v = argv[1];
        if (v.find_first_of("/\\.") == string::npos && !PathExists(v)) {
            string near;
            for (auto known : VERBS)
                if (EditDistance(v, known) <= 2 && near.empty()) near = known;
            fprintf(stderr, "faloya: unknown command %s%s (faloya help lists the commands)\n", v.c_str(),
                    near.empty() ? "" : cat(": did you mean ", near, "?").c_str());
            return 1;
        }
    }
    if (verb == "completion") {
        auto shell = argc > 2 ? string(argv[2]) : string();
        auto script = CompletionScript(shell);
        if (script.empty() || argc > 3) {
            fprintf(stderr, "faloya completion takes bash, zsh, fish or powershell%s\n",
                    shell.empty() ? "" : cat(", not ", shell).c_str());
            return 1;
        }
        fputs(script.c_str(), stdout);
        return 0;
    }
    if (verb == "help") {
        auto topic = argc > 2 ? string(argv[2]) : string();
        if (topic == "--man") {
            fputs(ManPage().c_str(), stdout);
            return 0;
        }
        auto help = HelpText(topic);
        if (help.empty()) {
            fprintf(stderr, "no help on %s: faloya help lists the commands\n", topic.c_str());
            return 1;
        }
        fputs(help.c_str(), stdout);
        return 0;
    }
    if (verb == "version") {
        printf("faloya %s (runtime %s)\n", CompilerVersion(), RuntimeVersion().c_str());
        return 0;
    }
    if (verb == "lsp") return RunLsp(exe, vector<string>(argv + firstarg, argv + argc));
    if (verb == "format") return RunFormat(vector<string>(argv + firstarg, argv + argc));
    string outname, headername, stdlibdir, shaderfile, shadersource, dumpfile;
    auto dump = false, dumpdocs = false, tokens = false, parseonly = false, specs = false;
    auto nocgen = false;
    auto roundtrip = false, multitest = false;
    auto nobce = false, bcetest = false, bcelines = false, norfcheck = false;
    auto stacks = false;
    auto stats = false;
    auto forcejit = false, standalone = false;
    auto json = false, refresh = false, debug = false, verbose = false, dryrun = false;
    auto jsondiag = false;
    string ccrequest;
    
    
    auto optlevel = 2;
    vector<string> cdefines, progargs, includenames, files;
    for (int i = firstarg; i < argc; i++) {
        string arg = argv[i];
        
        if (arg == "--") {
            for (int j = i + 1; j < argc; j++) progargs.push_back(argv[j]);
            break;
        }
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (arg == "--roundtrip") roundtrip = true;
        else if (arg == "--dump-file" && i + 1 < argc) dumpfile = argv[++i];
        else if (arg == "--specs") specs = true;
        else if (arg == "--stacks") stacks = true;
        else if (arg == "--stack-reserve" && i + 1 < argc) {
            uint64_t size;
            
            
            if (!ParseSize(argv[++i], size) || size < (64ull << 10) || size > (1ull << 48)) {
                fprintf(stderr, "--stack-reserve takes a size from 64 KB to 256 TB (2^48, "
                                "faloya_spec.md 10.4), with an optional K, M, G or T suffix: %s\n",
                        argv[i]);
                return 1;
            }
            cdefines.push_back(cat("GS_STACK_RESERVE=", size, "ull"));
        }
        else if (arg == "--check") nocgen = true;
        else if (arg == "--diagnostics" && i + 1 < argc) {
            string how = argv[++i];
            if (how == "json") jsondiag = true;
            else if (how != "text") {
                fprintf(stderr, "--diagnostics takes json or text, not %s\n", how.c_str());
                return 1;
            }
        }
        else if (arg == "--no-bce") nobce = true;
        else if (arg == "--bce-test") bcetest = true;
        else if (arg == "--bce-lines") bcelines = true;
        
        else if (arg == "--unsafe-no-rf-check") norfcheck = true;
        else if (arg == "--jit") forcejit = true;
        else if (arg == "--standalone") standalone = true;
        else if (arg == "--multi-test") multitest = true;
        else if (arg == "--gen-runtime-header") {
            try {
                GenRuntimeHeader(exe.c_str());
            } catch (CompileError &e) {
                PrintDiagnosticText(e.msg);
                return 1;
            }
            return 0;
        }
        else if (arg == "--emit-runtime" && i + 1 < argc) {
            auto path = argv[++i];
            auto src = RuntimeObjectSource();
            auto f = fopen(path, "wb");
            if (!f) {
                fprintf(stderr, "cannot write output file: %s\n", path);
                return 1;
            }
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
            printf("wrote %s (%d bytes)\n", path, (int)src.size());
            return 0;
        }
        
        else if (arg == "--compile-shader" && i + 1 < argc) shaderfile = argv[++i];
        else if (arg == "--shader-source" && i + 1 < argc) shadersource = argv[++i];
        
        
        else if (arg.size() > 7 && arg.rfind("--", 0) == 0 &&
                 arg.compare(arg.size() - 5, 5, "-link") == 0 && i + 1 < argc) {
            try {
                auto style = argv[++i];
                auto path = BuiltinLinkFile(arg.substr(2, arg.size() - 7), style, DirOf(exe));
                printf("%s\n", path.c_str());
            } catch (CompileError &e) {
                PrintDiagnosticText(e.msg);
                return 1;
            }
            return 0;
        }
        else if (arg == "-O0") optlevel = 0;
        else if (arg == "-O1") optlevel = 1;
        else if (arg == "-O2") optlevel = 2;
        else if (arg == "-o" && i + 1 < argc) outname = argv[++i];
        else if (arg == "--header" && i + 1 < argc) headername = argv[++i];
        else if (arg == "--include" && i + 1 < argc) includenames.push_back(argv[++i]);
        else if (arg == "--stdlib" && i + 1 < argc) stdlibdir = argv[++i];
        else if (arg == "--cc" && i + 1 < argc) ccrequest = argv[++i];
        else if (arg == "--json") json = true;
        else if (arg == "--refresh") refresh = true;
        else if (arg == "--debug") {
            debug = true;
            cdefines.push_back("GS_DEBUG=1");
        }
        else if (arg == "-v") verbose = true;
        else if (arg == "--stats") stats = true;
        else if (arg == "--color" && i + 1 < argc) {
            string when = argv[++i];
            if (when != "auto" && when != "always" && when != "never") {
                fprintf(stderr, "--color takes auto, always or never, not %s\n", when.c_str());
                return 1;
            }
            stderrcolor = when == "always" ? StderrColor(true) : when == "auto" && StderrColor(false);
        }
        else if (arg == "--dry-run") dryrun = true;
        else if (arg == "--update") package_update = true;
        else if (arg == "--offline") package_nofetch = true;
        else if (arg == "--dump-docs") dumpdocs = true;
        
        
        else if (arg == "-D" && i + 1 < argc) cdefines.push_back(argv[++i]);
        else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) cdefines.push_back(arg.substr(2));
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s (faloya --help lists them)\n", arg.c_str());
            return 1;
        }
        else files.push_back(arg);
    }
    if (verbose || dryrun) ProgressPlain();
    if (verb == "toolchain") {
        try {
            auto tools = FindTools(ccrequest, refresh);
            
            
            vector<string> builtin;
            for (auto &e : BlessedTable())
                if (BuiltinLookup(e.package)) builtin.push_back(e.import);
            fputs(ToolsReport(tools, json, builtin).c_str(), stdout);
            return tools.havecc ? 0 : 1;
        } catch (CompileError &e) {
            PrintDiagnosticText(e.msg);
            return 1;
        }
    }
    
    
    BuildOptions buildopts;
    string builddir;
    if (verb == "run") {
        if (!outname.empty()) {
            fprintf(stderr, "faloya run takes no -o: it compiles and runs the program in this "
                            "process\n");
            return 1;
        }
        forcejit = true;
    } else if (verb == "check") {
        nocgen = true;
    } else if (verb == "fetch") {
        
        
        parseonly = true;
    } else if (verb == "build") {
        if (multitest || !headername.empty()) {
            fprintf(stderr, "faloya build takes one program and no --header; the flag form "
                            "(-o out.c) writes the C for a C host\n");
            return 1;
        }
        buildopts = BuildOptions { debug, verbose, dryrun, standalone, outname };
        outname.clear();
    }
    if (files.size() > 1 && !multitest) {
        fprintf(stderr, "multiple input files given\n");
        return 1;
    }
    if (!shaderfile.empty()) {
        try {
            DumpShader(shaderfile, shadersource);
        } catch (CompileError &e) {
            PrintDiagnosticText(e.msg);
            return 1;
        }
        return 0;
    }
    if (files.empty()) {
        auto help = HelpText(verb);
        fprintf(stderr, "%s\n%s\n", help.substr(0, help.find("\n\n") + 1).c_str(),
                verb.empty() ? "faloya --help lists the commands and common options."
                             : cat("faloya ", verb, " --help lists its options.").c_str());
        return 1;
    }
    
    
    
    auto jit = verb != "build" && (forcejit || outname.empty());
    if (!headername.empty() && (jit || multitest)) {
        fprintf(stderr, "--header requires C output for a single input file\n");
        return 1;
    }
    
    
    auto library = !headername.empty();
    if (multitest && (tokens || !dumpfile.empty() || !progargs.empty() ||
                      (!outname.empty() && outname.find('%') == string::npos) ||
                      (jit && !nocgen && !parseonly && !dump))) {
        fprintf(stderr, "--multi-test runs no programs and writes no dump files: it takes "
                        "--check, --parse, --dump or an -o with a %%, and no --tokens, "
                        "--dump-file, --jit or program arguments\n");
        return 1;
    }
    
    
    auto msgs = jit || jsondiag ? stderr : stdout;
    
    
    string filename, outfile;
    vector<string> includes;
    
    string program;
    
    
    unique_ptr<Ast> astp;
    vector<int> nativepkgs;
    
    
    vector<Diagnostic> found;
    auto compile = [&]() -> int {
        if (tokens) {
            DumpTokens(filename);
            return 0;
        }
        astp = make_unique<Ast>();
        auto &ast = *astp;
        if (jsondiag) ast.sink = [&](const Diagnostic &d) { found.push_back(d); };
        auto stdlibdirs = StdlibDirs(stdlibdir, exe);
        ParseProgram(ast, filename, stdlibdirs, DirOf(exe));
        
        
        if (!ast.errors.empty()) return 1;
        if (dumpdocs) {
            string s;
            DumpDocs(ast, s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        if (dump) {
            
            
            
            string s;
            ast.Dump(s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        
        string dumped;
        if (roundtrip || !dumpfile.empty()) ast.Dump(dumped);
        if (!dumpfile.empty()) {
            auto f = fopen(dumpfile.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write dump file: ", dumpfile) };
            fwrite(dumped.data(), 1, dumped.size(), f);
            fclose(f);
        }
        ResolveTypeNames(ast);
        
        if (!ast.errors.empty()) return 1;
        
        
        
        if (stats) fprintf(msgs, "parsed ok: %d top-level declarations, %d file(s)\n",
                (int)ast.topdecls.size(), (int)ast.sources.size());
        
        
        if (roundtrip) {
            CheckRoundtrip(dumped, dumpfile.empty() ? cat(filename, ".dump") : dumpfile,
                           stdlibdirs, DirOf(exe));
            if (stats) fprintf(msgs, "roundtrip ok: %d bytes of dump\n", (int)dumped.size());
        }
        if (parseonly) return 0;
        TypeCheckProgram(ast, library, nocgen);
        
        if (!ast.errors.empty()) return 1;
        Optimizer opt(ast, optlevel);
        if (specs) {
            string s;
            opt.DumpSpecs(s);
            fputs(s.c_str(), stdout);
        }
        if (stats) fprintf(msgs, "typechecked ok: %d specialization(s), %d struct/%d enum instance(s); "
                "optimized -O%d: %d inlined, %d base case(s), %d folded, %d propagated, "
                "%d tail loop(s), %d reassociated\n",
                (int)ast.fnspecs.size(), (int)ast.structinsts.size(),
                (int)ast.enuminsts.size(), optlevel, opt.inlined, opt.basecases,
                opt.folded, opt.propagated, opt.tailloops, opt.reassociated);
        BCE bce(ast);
        if (!nobce) {
            bce.RunAll();
            if (stats) fprintf(msgs, "bce: elided %d/%d index and %d/%d slice checks\n",
                    bce.idxelided, bce.idxtotal, bce.slelided, bce.sltotal);
            if (!bce.checkfreefails.empty()) throw bce.CheckfreeError();
        } else {
            for (auto sf : ast.functions)
                
                
                if (sf->checkfree && !sf->isnested && !ast.StdlibFile(sf->line.fileidx)) {
                    ast.Warn(DiagLoc {}, "checkfree is not verified under --no-bce");
                    break;
                }
        }
        
        if (bcelines)
            for (auto &[where, counts] : bce.lineout)
                fprintf(msgs, "bce-line: %s:%d: %d elided, %d kept\n",
                        ast.sources[where.first].first.c_str(), where.second,
                        counts.first, counts.second);
        if (bcetest) {
            auto fails = bce.VerifyAnnotations();
            if (fails) {
                fprintf(stderr, "bce-test: %d annotation failure(s)\n", fails);
                return 1;
            }
            fprintf(msgs, "bce-test: all annotations verified\n");
        }
        
        
        if (nocgen && !stacks) return 0;
        
        
        
        for (auto &pkg : ast.packages)
            includes.insert(includes.end(), pkg.cheaders.begin(), pkg.cheaders.end());
        
        
        
        
        
        
        
        if (!outfile.empty())
            for (auto &inc : includes)
                if (FileExists(inc)) inc = RelativeTo(inc, DirOf(outfile));
        
        
        string_view runtime_ext_text;
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == "runtime_ext.h") runtime_ext_text = rf.text;
        auto stackcfg = StackConfigOf(cdefines);
        CodeGen cg(ast, runtime_ext_text, includes, norfcheck, library, stackcfg.maxstacks);
        if (stacks) cg.StackReport(msgs, stackcfg);
        
        
        
        
        
        nativepkgs = NativePackagesUsed(ast, cg.usedexterns);
        
        
        
        
        auto assemble = [&](bool separate, bool embedded = false) {
            string out = cat("/* Generated by the Faloya compiler from ", filename,
                             ". Do not edit.\n"
                             "   Names from the program carry a _g suffix, which keeps them clear of\n"
                             "   the C keywords, of the runtime's gs_ names and of whatever this\n"
                             "   platform's headers declare; a namespaced name ns::x is ns_x_g followed\n"
                             "   by the namespace's length. An --include header names them that way.",
                             separate ? "\n   Link it with the runtime that `faloya --emit-runtime` "
                                        "writes. */\n\n#define GS_SEPARATE_RUNTIME 1\n"
                                      + (embedded ? string() :
                                         "#define GS_RUNTIME_VERSION " + RuntimeVersion() + "\n")
                                      : " */\n\n",
                             cg.predefs);
            
            
            for (auto &d : cdefines) {
                auto eq = d.find('=');
                Append(out, "#define ", eq == string::npos ? d : d.substr(0, eq), " ",
                       eq == string::npos ? string("1") : d.substr(eq + 1), "\n");
            }
            if (embedded)
                Append(out, "#define GS_EMBEDDED_JIT 1\n",
                       RuntimeSections({ "jit_c.h" }));
            if (separate)
                Append(out, RuntimeSections({ "runtime.h" }), cg.head,
                       RuntimeSections({ "runtime_ext.h" }), cg.result);
            else
                Append(out, RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h" }),
                       cg.head, RuntimeSections({ "runtime_ext.h", "runtime_os.h" }), cg.result);
            if (embedded)
                Append(out, "\nvoid gs_jit_cleanup(void) { free(gs_stks); gs_stks = NULL; "
                            "gs_nstks = 0; }\n");
            return out;
        };
        if (!outfile.empty() && !nocgen) {
            auto out = assemble(!standalone);
            if (!WriteFileMakingDirs(outfile, out))
                throw CompileError { cat("cannot write output file: ", outfile) };
            if (stats) fprintf(msgs, "wrote %s (%d bytes)\n", outfile.c_str(), (int)out.size());
        }
        if (!headername.empty()) {
            auto header = cg.ExportHeader();
            if (!WriteFileMakingDirs(headername, header))
                throw CompileError { cat("cannot write export header: ", headername) };
            if (stats) fprintf(msgs, "wrote %s (%d bytes)\n", headername.c_str(), (int)header.size());
        }
        if (jit && !nocgen) {
            
            
            if (cg.usesthreads)
                throw CompileError { "JIT mode does not support threads yet (TinyCC cannot "
                                     "place thread-local storage in an in-memory run); "
                                     "compile with -o and a C compiler instead" };
            program = assemble(true, true);
        }
        return 0;
    };
    
    
    
    Toolchain tc;
    string tcwhy;
    bool tclooked = false, havetc = false;
    auto findtc = [&] {
        if (!tclooked) havetc = FindToolchain(ccrequest, tc, tcwhy);
        tclooked = true;
        if (!havetc) fprintf(stderr, "%s\n", tcwhy.c_str());
        return havetc;
    };
    auto run = [&](const string &file) -> int {
        filename = file;
        outfile = outname;
        if (multitest && !outfile.empty()) {
            auto base = file.substr(file.find_last_of("/\\") + 1);
            auto stem = base.substr(0, base.find_last_of('.'));
            for (auto pos = outfile.find('%'); pos != string::npos;
                 pos = outfile.find('%', pos + stem.size()))
                outfile.replace(pos, 1, stem);
        }
        includes = includenames;
        if (verb == "build") {
            builddir = ProgramBuildDir(file);
            std::error_code ec;
            filesystem::create_directories(filesystem::path(builddir), ec);
            if (ec) {
                fprintf(stderr, "cannot create the build directory %s\n", builddir.c_str());
                return 1;
            }
            outfile = cat(builddir, "/", PathStem(file), ".c");
            
            
            
            for (auto &inc : includes)
                if (FileExists(inc)) inc = AbsolutePath(inc);
        }
        program.clear();
        astp.reset();
        nativepkgs.clear();
        found.clear();
        auto attempt = [&]() -> int {
            
            
            
            
            
            auto code = RunOnCompilerStack(compile);
            if (code) return code;
            if (verb == "build") {
                if (nocgen) return 0;
                
                
                
                if (!findtc()) return 1;
                Builder b { tc, buildopts, msgs };
                vector<string> rsps;
                for (auto pkg : nativepkgs) {
                    auto &p = astp->packages[pkg];
                    if (p.builtin >= 0) {
                        rsps.push_back(BuiltinPackageLinkFile(builtin_packages[p.builtin],
                                                              tc.style, DirOf(exe)));
                        continue;
                    }
                    NativeBuild nb;
                    if (!BuildNativePart(*astp, pkg, filename, b, false, nb)) return 1;
                    rsps.push_back(nb.rsp);
                }
                return BuildExecutable(filename, outfile, builddir, tc, buildopts, msgs, rsps,
                                       astp->packages[0].output);
            }
            if (program.empty()) return code;
            
            
            
            vector<pair<string, const void *>> symbols;
            BuildOptions jitopts;
            jitopts.verbose = verbose;
            for (auto pkg : nativepkgs) {
                auto &p = astp->packages[pkg];
                if (p.builtin >= 0) {
                    if (auto table = builtin_packages[p.builtin].symbols)
                        for (auto e = table(); e->name; e++) symbols.emplace_back(e->name, e->addr);
                    continue;
                }
                if (!findtc()) return 1;
                Builder b { tc, jitopts, msgs };
                NativeBuild nb;
                if (!BuildNativePart(*astp, pkg, filename, b, true, nb)) return 1;
                string err;
                if (!LoadSharedSymbols(nb.jitlib, symbols, err)) {
                    fprintf(stderr, "cannot load the package %s's library: %s\n", p.name.c_str(),
                            err.c_str());
                    return 1;
                }
            }
            
            
            
            TCCState *jit = nullptr;
            RunOnCompilerStack([&]() -> int {
                jit = CompileJit(program, JitLibPath(DirOf(exe)), symbols);
                return 0;
            });
            
            
            fflush(msgs);
            return RunJit(jit, filename, progargs);
        };
        auto code = 0;
        try {
            code = attempt();
        } catch (CompileError &e) {
            if (!jsondiag) PrintDiagnosticText(e.msg);
            
            
            if (e.diags.empty()) {
                Diagnostic d;
                d.message = d.text = e.msg;
                found.push_back(std::move(d));
            } else {
                found.insert(found.end(), e.diags.begin(), e.diags.end());
            }
            code = 1;
        }
        if (jsondiag) fputs(DiagnosticsJson(found, astp.get()).c_str(), stdout);
        return code;
    };
    if (!multitest) return run(files[0]);
    
    
    
    
    
    for (auto &file : files) {
        auto code = run(file);
        for (auto f : { stdout, stderr }) {
            fflush(f);
            fprintf(f, "\n%s %d %s\n", MULTIMARK, code, file.c_str());
            fflush(f);
        }
    }
    return 0;
}

}  



#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <pthread.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace faloya {




void SetBinaryStdio() {
    #ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    #endif
}





string ExePath(const char *argv0) {
    #ifdef _WIN32
        char path[4096];
        auto n = GetModuleFileNameA(nullptr, path, sizeof path);
        if (n && n < sizeof path) return string(path, n);
    #elif defined(__linux__)
        char path[4096];
        auto n = readlink("/proc/self/exe", path, sizeof path);
        if (n > 0 && (size_t)n < sizeof path) return string(path, (size_t)n);
    #elif defined(__APPLE__)
        char path[4096];
        uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size) == 0) {
            if (auto resolved = realpath(path, nullptr)) {
                string s = resolved;
                free(resolved);
                return s;
            }
        }
    #endif
    return argv0;
}





constexpr size_t COMPILERSTACK = (size_t)64 << 20;
constexpr size_t STACKHEADROOM = (size_t)4 << 20;

struct CompilerThread {
    const function<int()> &fn;
    int code = 0;
    exception_ptr failure;

    void Run() {
        stackfloor = StackPointer() - (COMPILERSTACK - STACKHEADROOM);
        try {
            code = fn();
        } catch (...) {
            failure = current_exception();
        }
    }
};

#ifdef _WIN32
static unsigned __stdcall CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return 0;
}
#else
static void *CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return nullptr;
}
#endif




int RunOnCompilerStack(const function<int()> &fn) {
    CompilerThread t { fn };
    #ifdef _WIN32
        
        auto h = (HANDLE)_beginthreadex(nullptr, (unsigned)COMPILERSTACK, CompilerThreadMain,
                                        &t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!h) return fn();
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    #else
        pthread_attr_t attr;
        pthread_t th;
        if (pthread_attr_init(&attr)) return fn();
        auto failed = pthread_attr_setstacksize(&attr, COMPILERSTACK) ||
                      pthread_create(&th, &attr, CompilerThreadMain, &t);
        pthread_attr_destroy(&attr);
        if (failed) return fn();
        pthread_join(th, nullptr);
    #endif
    if (t.failure) rethrow_exception(t.failure);
    return t.code;
}

}  

int main(int argc, char **argv) {
    #ifdef _MSC_VER
        
        
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    #endif
    return faloya::Main(argc, argv);
}
