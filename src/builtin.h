




#pragma once

namespace faloya {



struct faloya_symbol {
    const char *name;
    const void *addr;
};

struct BuiltinPackage {
    const char *name;
    const faloya_symbol *(*symbols)();   
    const char *srcdir;                 
    const char *rspdir;                 
};

}  

#include "builtin_packages.h"

namespace faloya {

struct BlessedEntry {
    string import;      
    string package;     
    string source;      
    string ref;         
};

inline const vector<BlessedEntry> &BlessedTable() {
    static const vector<BlessedEntry> table = [] {
        vector<BlessedEntry> t;
        string text = blessed_table;
        for (size_t i = 0; i < text.size();) {
            auto end = text.find('\n', i);
            if (end == string::npos) end = text.size();
            auto line = text.substr(i, end - i);
            i = end + 1;
            if (auto hash = line.find('#'); hash != string::npos) line.resize(hash);
            vector<string> fields;
            string cur;
            for (auto c : line) {
                if (c == ' ' || c == '\t' || c == '\r') {
                    if (!cur.empty()) fields.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) fields.push_back(cur);
            if (fields.size() == 4) t.push_back({ fields[0], fields[1], fields[2], fields[3] });
        }
        return t;
    }();
    return table;
}

inline const BlessedEntry *BlessedLookup(const string &import) {
    for (auto &e : BlessedTable()) if (e.import == import) return &e;
    return nullptr;
}

inline const BuiltinPackage *BuiltinLookup(const string &package) {
    for (auto b = builtin_packages; b->name; b++) if (package == b->name) return b;
    return nullptr;
}

inline int BuiltinIndex(const BuiltinPackage *b) {
    return b ? (int)(b - builtin_packages) : -1;
}




inline string BuiltinSrcDir(const BuiltinPackage &b, const string &exedir) {
    if (PathExists(cat(b.srcdir, b.name, ".fal"))) return b.srcdir;
    auto beside = cat(exedir, "pkg/", b.name, "/");
    for (auto &c : beside) if (c == '\\') c = '/';
    return beside;
}





inline string AbsoluteLinkFile(const string &rsp, const string &pkgname, const string &style) {
    std::error_code ec;
    auto dir = filesystem::absolute(filesystem::path(rsp), ec).parent_path();
    string text;
    if (ec || !LoadFile(rsp, text)) throw CompileError { cat("cannot read ", rsp) };
    string out;
    for (size_t i = 0; i < text.size();) {
        auto end = text.find('\n', i);
        if (end == string::npos) end = text.size();
        auto item = text.substr(i, end - i);
        i = end + 1;
        if (!item.empty() && item.back() == '\r') item.pop_back();
        if (item.empty()) continue;
        if (item.size() > 1 && item.front() == '"' && item.back() == '"')
            item = item.substr(1, item.size() - 2);
        auto path = filesystem::path(item);
        if (item[0] != '-' && path.is_relative() && filesystem::exists(dir / path, ec))
            item = (dir / path).lexically_normal().generic_string();
        Append(out, item.find(' ') != string::npos ? cat("\"", item, "\"") : item, "\n");
    }
    auto dest = cat(FaloyaHome(), "/build/builtin/", pkgname, "-", Hex8(dir.generic_string()),
                    "/link-", style, ".rsp");
    string old;
    if (LoadFile(dest, old) && old == out) return dest;
    filesystem::create_directories(filesystem::path(dest).parent_path(), ec);
    
    FileLock lock(cat(dest, ".lock"),
                  cat("waiting for another faloya writing the link file of ", pkgname));
    if (!WriteFileIfChanged(dest, out)) throw CompileError { cat("cannot write ", dest) };
    return dest;
}




inline string BuiltinPackageLinkFile(const BuiltinPackage &b, const string &style,
                                     const string &exedir) {
    if (style != "msvc" && style != "cc")
        throw CompileError { cat("the link style is msvc or cc, not ", style) };
    if (!*b.rspdir)
        throw CompileError { cat("the package ", b.name, " has no native part to link") };
    auto name = cat("link-", style, ".rsp");
    auto recorded = cat(b.rspdir, "/", name);
    if (PathExists(recorded)) return recorded;
    auto beside = cat(exedir, "pkg/", b.name);
    if (auto shipped = cat(beside, "/", name); PathExists(shipped))
        return AbsoluteLinkFile(shipped, b.name, style);
    throw CompileError { cat("cannot find ", name, " of the builtin package ", b.name,
                             " (looked in ", b.rspdir, " and ", beside, ")") };
}



inline string BuiltinLinkFile(const string &import, const string &style, const string &exedir) {
    auto be = BlessedLookup(import);
    if (!be) throw CompileError { cat("--", import, "-link: no blessed package is imported as ", import) };
    auto b = BuiltinLookup(be->package);
    if (!b)
        throw CompileError { cat("this compiler was built without the package ", be->package,
                                 " (configure with -DFALOYA_BUILTIN_LIBS=all); faloya build and "
                                 "faloya run fetch and build it instead") };
    return BuiltinPackageLinkFile(*b, style, exedir);
}

}  
