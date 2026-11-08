
#pragma once

namespace faloya {


inline void CatOne(string &s, string_view v)   { s += v; }
inline void CatOne(string &s, const char *v)   { s += v; }
inline void CatOne(string &s, const string &v) { s += v; }
inline void CatOne(string &s, char v)          { s += v; }
inline void CatOne(string &s, bool v)          { s += v ? "true" : "false"; }



template<typename T> requires (is_integral_v<T> && !is_same_v<T, char> && !is_same_v<T, bool>)
void CatOne(string &s, T v)                    { s += to_string(v); }
inline void CatOne(string &s, double v) {
    
    if (v != v) { s += "nan"; return; }
    if (std::isinf(v)) { s += v > 0 ? "inf" : "-inf"; return; }
    
    
    
    
    
    char buf[32];
    *to_chars(buf, buf + sizeof buf - 1, v, chars_format::scientific).ptr = 0;
    auto e = strchr(buf, 'e');
    string dig;
    for (auto p = buf; p != e; p++)
        if (*p != '-' && *p != '.') dig += *p;
    int n = (int)dig.size(), x = atoi(e + 1);
    if (x < -4 || x >= max(n, 15)) { s += buf; return; }
    if (buf[0] == '-') s += '-';
    if (x < 0) {
        s += "0.";
        s.append(-x - 1, '0');
        s += dig;
        return;
    }
    s += dig.substr(0, x + 1);
    if (n < x + 1) s.append(x + 1 - n, '0');
    s += '.';
    s += n > x + 1 ? dig.substr(x + 1) : "0";
}

template<typename... Ts> void Append(string &s, const Ts &...args) {
    (CatOne(s, args), ...);
}

template<typename... Ts> string cat(const Ts &...args) {
    string s;
    Append(s, args...);
    return s;
}




struct DiagLoc {
    int fileidx = -1;
    int line = 0, col = 0;
    int endline = 0, endcol = 0;
};



struct Diagnostic {
    bool warning = false;
    DiagLoc where;
    string message;
    
    
    
    struct Related { DiagLoc where; string message; };
    vector<Related> related;
    string text;                
};







inline string RenderDiagnostic(const Diagnostic &d, string_view filename, string_view source) {
    string s;
    auto &w = d.where;
    if (!filename.empty()) Append(s, filename, ":", w.line, ": ");
    Append(s, d.warning ? "warning: " : "error: ", d.message);
    if (!filename.empty() && w.line > 0 && source.data()) {
        auto p = source.data(), e = p + source.size();
        for (auto ln = 1; p < e && ln < w.line; p++) if (*p == '\n') ln++;
        auto end = p;
        while (end < e && *end != '\n' && *end != '\r') end++;
        Append(s, "\n", string_view(p, (size_t)(end - p)));
        if (w.col > 0 && w.col - 1 <= end - p) {
            s += '\n';
            for (auto q = p; q < p + w.col - 1; q++) s += *q == '\t' ? '\t' : ' ';
            s += '^';
            if (w.endline == w.line && w.endcol > w.col + 1) {
                auto rend = p + std::min<ptrdiff_t>(w.endcol - 1, end - p);
                for (auto q = p + w.col; q < rend; q++)
                    if ((*q & 0xC0) != 0x80) s += '~';
            }
        }
    }
    for (auto &r : d.related) Append(s, "\n  ", r.message);
    return s;
}


inline bool stderrcolor = false;




inline string ColorDiagnostics(string_view text) {
    const char *bold = "\x1b[1m", *red = "\x1b[1;31m", *magenta = "\x1b[1;35m",
               *green = "\x1b[1;32m", *reset = "\x1b[0m";
    string s;
    for (size_t pos = 0; pos <= text.size();) {
        auto e = text.find('\n', pos);
        if (e == string_view::npos) e = text.size();
        auto line = text.substr(pos, e - pos);
        auto kind = string_view();
        size_t at = string_view::npos;
        for (auto k : { string_view("error: "), string_view("warning: ") }) {
            auto p = line.find(k);
            
            if (p != string_view::npos && (p == 0 || line.substr(p - 2, 2) == ": ") && p < at) {
                at = p;
                kind = k;
            }
        }
        auto caret = line.find_first_not_of(" \t");
        if (at != string_view::npos) {
            if (at) Append(s, bold, line.substr(0, at), reset);
            Append(s, kind[0] == 'e' ? red : magenta, kind, reset, bold, line.substr(at + kind.size()), reset);
        } else if (caret != string_view::npos && line[caret] == '^' &&
                   line.find_first_not_of("^~", caret) == string_view::npos) {
            Append(s, line.substr(0, caret), green, line.substr(caret), reset);
        } else {
            s += line;
        }
        if (e < text.size()) s += '\n';
        pos = e + 1;
    }
    return s;
}



inline void PrintDiagnosticText(string_view text) {
    auto s = stderrcolor ? ColorDiagnostics(text) : string(text);
    fprintf(stderr, "%s\n", s.c_str());
}




struct Cancelled {};






struct Abandoned {};







struct CompileError {
    string msg;
    vector<Diagnostic> diags;
};







inline thread_local uintptr_t stackfloor = 0;

inline uintptr_t StackPointer() {
    
    
    #ifdef _MSC_VER
        return (uintptr_t)_AddressOfReturnAddress();
    #else
        return (uintptr_t)__builtin_frame_address(0);
    #endif
}

inline bool StackLow() { return StackPointer() < stackfloor; }




inline bool LoadFile(const string &path, string &dest) {
    auto f = fopen(path.c_str(), "rb");
    if (!f) return false;
    dest.clear();
    for (size_t got = 0;;) {
        dest.resize(std::max<size_t>(got * 2, 1 << 16));
        got += fread(dest.data() + got, 1, dest.size() - got, f);
        if (got < dest.size()) {
            dest.resize(got);
            break;
        }
    }
    auto ok = !ferror(f);
    fclose(f);
    return ok;
}


inline string PathStem(const string &path) {
    auto base = path.substr(path.find_last_of("/\\") + 1);
    auto dot = base.find_last_of('.');
    return dot == string::npos ? base : base.substr(0, dot);
}



inline void StripBom(string &s) {
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
}
inline bool LoadSource(const string &path, string &dest) {
    if (!LoadFile(path, dest)) return false;
    StripBom(dest);
    return true;
}




using SourceProvider = function<const string *(const string &path)>;

}  
