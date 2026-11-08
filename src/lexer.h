

#pragma once

namespace faloya {


#define TOKENS_MISC \
    F(T_EOF,      "end of file") \
    F(T_IDENT,    "identifier") \
    F(T_INTLIT,   "integer literal") \
    F(T_FLTLIT,   "float literal") \
    F(T_STRLIT,   "string literal")

#define TOKENS_PUNCT \
    F(T_LPAREN,   "(")   F(T_RPAREN,   ")") \
    F(T_LBRACKET, "[")   F(T_RBRACKET, "]") \
    F(T_LCURLY,   "{")   F(T_RCURLY,   "}") \
    F(T_COMMA,    ",")   F(T_SEMI,     ";")  F(T_COLON, ":")  F(T_QUESTION, "?") \
    F(T_COLONCOLON, "::") \
    F(T_DOT,      ".")   F(T_DOTDOT,   "..") F(T_DOTASSIGN, ".=") \
    F(T_DOTEQ,    ".==") F(T_DOTNEQ,   ".!=") \
    F(T_PLUS,     "+")   F(T_MINUS,    "-")  F(T_MUL,   "*")  F(T_DIV, "/")  F(T_MOD, "%") \
    F(T_PLUSEQ,   "+=")  F(T_MINUSEQ,  "-=") F(T_MULEQ, "*=") F(T_DIVEQ, "/=") F(T_MODEQ, "%=") \
    F(T_ANDEQ,    "&=")  F(T_OREQ,     "|=") F(T_XOREQ, "^=") \
    F(T_INC,      "++")  F(T_DEC,      "--") \
    F(T_ASSIGN,   "=")   F(T_EQ,       "==") F(T_NEQ,   "!=") \
    F(T_LT,       "<")   F(T_GT,       ">")  F(T_LTEQ,  "<=") F(T_GTEQ, ">=") \
    F(T_SHL,      "<<")  F(T_SHR,      ">>") F(T_SHLEQ, "<<=") F(T_SHREQ, ">>=") \
    F(T_NOT,      "!")   F(T_BITNOT,   "~") \
    F(T_BITAND,   "&")   F(T_BITOR,    "|")  F(T_XOR,   "^") \
    F(T_ANDAND,   "&&")  F(T_OROR,     "||") \
    F(T_ARROW,    "->")  F(T_FATARROW, "=>")

#define TOKENS_KEYWORDS \
    F(T_IMPORT,   "import")    F(T_NAMESPACE, "namespace") \
    F(T_STRUCT,   "struct")    F(T_ENUM,     "enum") \
    F(T_TYPE,     "type")      F(T_FN,       "fn")      F(T_THREADFN, "thread_fn") \
    F(T_EXPORT,   "export")    F(T_SIMD,     "simd") \
    F(T_RECURSIVE, "recursive") F(T_LET,     "let")     F(T_VAR,    "var") \
    F(T_EXTERN,   "extern")    F(T_CONST,    "const") \
    F(T_PAD,      "pad")       F(T_REUSABLE, "reusable") \
    F(T_IF,       "if")        F(T_ELSE,     "else")    F(T_WHILE,  "while") \
    F(T_FOR,      "for")       F(T_IN,       "in")      F(T_LOOP,   "loop") \
    F(T_BLOCK,    "block")     F(T_GUARD,    "guard")   F(T_MATCH,  "match") \
    F(T_DEFER,    "defer") \
    F(T_RETURN,   "return")    F(T_BREAK,  "break") \
    F(T_CONTINUE, "continue")  F(T_AS,       "as") \
    F(T_TRUE,     "true")      F(T_FALSE,    "false")   F(T_NULLLIT, "null") \
    F(T_SELF,     "self") \
    F(T_TBOOL,    "bool")      F(T_TVARINT,  "varint") \
    F(T_TI8,      "i8")  F(T_TI16, "i16") F(T_TI32, "i32") F(T_TI64, "i64") \
    F(T_TU8,      "u8")  F(T_TU16, "u16") F(T_TU32, "u32") F(T_TU64, "u64") \
    F(T_TF32,     "f32") F(T_TF64, "f64")

enum TType {
    #define F(t, s) t,
    TOKENS_MISC TOKENS_PUNCT TOKENS_KEYWORDS
    #undef F
};

inline const char *TName(TType t) {
    static const char *names[] = {
        #define F(t, s) s,
        TOKENS_MISC TOKENS_PUNCT TOKENS_KEYWORDS
        #undef F
    };
    return names[t];
}

inline bool IsIdentStart(char c) { return isalpha((uint8_t)c) || c == '_'; }
inline bool IsIdentCont(char c)  { return isalnum((uint8_t)c) || c == '_'; }




inline string_view Utf8CharAt(const char *p) {
    auto c = (uint8_t)*p;
    size_t len = c < 0x80 ? 1 : c >= 0xC2 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF5 ? 4 : 0;
    for (size_t i = 1; i < len; i++)
        if (((uint8_t)p[i] & 0xC0) != 0x80) return {};
    return string_view(p, len);
}



inline string QuotedCharAt(const char *p) {
    auto seq = Utf8CharAt(p);
    if (seq.empty()) return cat("byte ", (int)(uint8_t)*p);
    auto s = cat("'", seq, "'");
    if (seq.size() == 1) return cat(s, " (", (int)(uint8_t)*p, ")");
    uint32_t cp = (uint8_t)seq[0] & (0xFFu >> (seq.size() + 1));
    for (size_t i = 1; i < seq.size(); i++) cp = (cp << 6) | ((uint8_t)seq[i] & 0x3F);
    char buf[16];
    snprintf(buf, sizeof buf, " (U+%04X)", (unsigned)cp);
    return s + buf;
}




struct Lexer {
    const char *p = nullptr;         
    const char *linestart = nullptr;
    int line = 1;
    string_view filename;
    const char *source = nullptr;    
    int fileidx = 0;                 

    
    
    const char *prevend = nullptr;
    const char *prevendlinestart = nullptr;
    int prevendline = 1;

    
    TType tok = T_EOF;
    string_view attr;                
    int64_t ival = 0;                
    bool iuns = false;               
    double fval = 0;                 
    string sval;                     
    bool smultiline = false;         
    bool sinterp = false;            
    bool interpop = false;           
    vector<int> interp;              
    int tokline = 1;
    const char *toklinestart = nullptr;
    
    
    bool scanning = false;
    
    
    
    
    string_view doc;
    
    
    int curlies = 0;

    Lexer(string_view _filename, const char *_source, int _fileidx = 0)
        : p(_source), filename(_filename), source(_source), fileidx(_fileidx) {
        linestart = p;
        Next();
    }

    [[noreturn]] void Error(const string &msg) { ErrorAt(msg, tokline, toklinestart); }

    
    
    int DepthBefore() const { return curlies - (tok == T_LCURLY) + (tok == T_RCURLY); }

    
    
    TType PeekTok() const {
        auto copy = *this;
        try {
            copy.Next();
        } catch (CompileError &) {
            return T_EOF;
        }
        return copy.tok;
    }

    
    
    
    
    [[noreturn]] void ErrorAt(const string &msg, int errline, const char *errlinestart,
                              const char *caret = nullptr) {
        auto d = Diagnose(msg, errline, errlinestart, caret);
        throw CompileError { d.text, { d } };
    }

    
    
    Diagnostic Diagnose(const string &msg, int errline, const char *errlinestart,
                        const char *caret = nullptr) {
        Diagnostic d;
        d.where = { fileidx, errline, 0, 0, 0 };
        d.message = msg;
        if (errlinestart) {
            auto end = errlinestart;
            while (*end && *end != '\n' && *end != '\r') end++;
            auto caretpos = caret ? caret : errlinestart == toklinestart ? attr.data() : nullptr;
            if (caretpos && caretpos >= errlinestart && caretpos <= end) {
                d.where.col = (int)(caretpos - errlinestart) + 1;
                auto len = caretpos == attr.data() && attr.find('\n') == string_view::npos
                               ? attr.size() : (size_t)0;
                d.where.endline = errline;
                d.where.endcol = d.where.col + (int)max<size_t>(len, 1);
            }
        }
        d.text = RenderDiagnostic(d, filename, errlinestart ? string_view(source) : string_view());
        return d;
    }

    
    
    
    
    
    void SkipBrokenToken() {
        if (!scanning) return;
        auto start = attr.data();
        if (start[0] == '"' && start[1] == '"' && start[2] == '"') {
            auto close = strstr(start + 3, "\"\"\"");
            if (close) {
                
                
                
                for (auto q = linestart; q < close; q++)
                    if (*q == '\n') { line++; linestart = q + 1; }
                p = close + 3;
                Next();
                return;
            }
        }
        while (*p && *p != '\n') p++;
        Next();
    }

    void Next() {
        interpop = false;
        NextRaw();
        if (interp.empty()) return;
        if (tok == T_LCURLY) { interp.back()++; return; }
        if (tok == T_RCURLY) {
            if (interp.back() > 0) { interp.back()--; return; }
            interp.pop_back();
            interpop = true;
            LexStringBody(p);
        }
    }

    void NextRaw() {
        prevend = p;
        prevendline = line;
        prevendlinestart = linestart;
        
        
        
        const char *docstart = nullptr, *docend = nullptr;
        int docline = 0;
        again:
        for (;;) {
            auto c = *p;
            if (c == ' ' || c == '\t' || c == '\r') { p++; }
            else if (c == '\n') { p++; line++; linestart = p; }
            else break;
        }
        auto start = p;
        tokline = line;
        toklinestart = linestart;
        doc = docstart && docline + 1 == line ? string_view(docstart, (size_t)(docend - docstart))
                                              : string_view();
        
        
        attr = string_view(start, 0);
        scanning = true;
        auto Set = [&](TType t) {
            tok = t;
            attr = string_view(start, (size_t)(p - start));
            scanning = false;
        };
        auto c = *p++;
        switch (c) {
            case 0:   p--; Set(T_EOF); return;

            case '(': Set(T_LPAREN);   return;
            case ')': Set(T_RPAREN);   return;
            case '[': Set(T_LBRACKET); return;
            case ']': Set(T_RBRACKET); return;
            case '{': curlies++; Set(T_LCURLY); return;
            case '}': curlies--; Set(T_RCURLY); return;
            case ',': Set(T_COMMA);    return;
            case ';': Set(T_SEMI);     return;
            case ':':
                if (*p == ':') { p++; Set(T_COLONCOLON); } else Set(T_COLON);
                return;
            case '?': Set(T_QUESTION); return;
            case '~': Set(T_BITNOT);   return;

            case '.':
                if (*p == '.') { p++; Set(T_DOTDOT); }
                else if (*p == '=' && p[1] == '=') { p += 2; Set(T_DOTEQ); }
                else if (*p == '!' && p[1] == '=') { p += 2; Set(T_DOTNEQ); }
                else if (*p == '=') { p++; Set(T_DOTASSIGN); }
                else Set(T_DOT);
                return;
            case '+':
                if (*p == '+') { p++; Set(T_INC); }
                else if (*p == '=') { p++; Set(T_PLUSEQ); }
                else Set(T_PLUS);
                return;
            case '-':
                if (*p == '-') { p++; Set(T_DEC); }
                else if (*p == '=') { p++; Set(T_MINUSEQ); }
                else if (*p == '>') { p++; Set(T_ARROW); }
                else Set(T_MINUS);
                return;
            case '*':
                if (*p == '=') { p++; Set(T_MULEQ); } else Set(T_MUL);
                return;
            case '%':
                if (*p == '=') { p++; Set(T_MODEQ); } else Set(T_MOD);
                return;
            case '=':
                if (*p == '=') { p++; Set(T_EQ); }
                else if (*p == '>') { p++; Set(T_FATARROW); }
                else Set(T_ASSIGN);
                return;
            case '!':
                if (*p == '=') { p++; Set(T_NEQ); } else Set(T_NOT);
                return;
            case '<':
                if (*p == '=') { p++; Set(T_LTEQ); }
                else if (*p == '<' && p[1] == '=') { p += 2; Set(T_SHLEQ); }
                else if (*p == '<') { p++; Set(T_SHL); }
                else Set(T_LT);
                return;
            case '>':
                if (*p == '=') { p++; Set(T_GTEQ); }
                else if (*p == '>' && p[1] == '=') { p += 2; Set(T_SHREQ); }
                else if (*p == '>') { p++; Set(T_SHR); }
                else Set(T_GT);
                return;
            case '&':
                if (*p == '&') { p++; Set(T_ANDAND); }
                else if (*p == '=') { p++; Set(T_ANDEQ); }
                else Set(T_BITAND);
                return;
            case '|':
                if (*p == '|') { p++; Set(T_OROR); }
                else if (*p == '=') { p++; Set(T_OREQ); }
                else Set(T_BITOR);
                return;
            case '^':
                if (*p == '=') { p++; Set(T_XOREQ); } else Set(T_XOR);
                return;

            case '/':
                if (*p == '/') {
                    
                    
                    
                    auto own = true;
                    for (auto q = linestart; q < start; q++) own = own && (*q == ' ' || *q == '\t');
                    if (own && (!docstart || docline + 1 != line)) docstart = start;
                    while (*p && *p != '\n') p++;
                    if (own) {
                        docend = p > start && p[-1] == '\r' ? p - 1 : p;
                        docline = line;
                    }
                    goto again;
                }
                if (*p == '*') {
                    
                    p++;
                    for (auto nesting = 1; nesting;) {
                        switch (*p++) {
                            case 0: p--; Error("unterminated /* comment");
                            case '\n': line++; linestart = p; break;
                            case '*': if (*p == '/') { p++; nesting--; } break;
                            case '/': if (*p == '*') { p++; nesting++; } break;
                        }
                    }
                    goto again;
                }
                if (*p == '=') { p++; Set(T_DIVEQ); } else Set(T_DIV);
                return;

            case '\'': {
                ival = LexEscapedChar('\'');
                iuns = false;
                if (*p != '\'') Error("expected closing \' of character literal");
                p++;
                Set(T_INTLIT);
                return;
            }
            case '"': {
                if (p[0] == '"' && p[1] == '"') {
                    p += 2;
                    LexRawString();
                    Set(T_STRLIT);
                    return;
                }
                LexStringBody(start);
                return;
            }

            default: {
                if (isdigit((uint8_t)c)) {
                    if (c == '0' && (*p == 'x' || *p == 'X')) {
                        p++;
                        auto numstart = p;
                        while (isxdigit((uint8_t)*p)) p++;
                        if (p == numstart) Error("hex digits expected after 0x");
                        
                        auto ishexfloat = false;
                        if (*p == '.' && isxdigit((uint8_t)p[1])) {
                            ishexfloat = true;
                            p++;
                            while (isxdigit((uint8_t)*p)) p++;
                        }
                        if (*p == 'p' || *p == 'P') {
                            ishexfloat = true;
                            p++;
                            if (*p == '+' || *p == '-') p++;
                            if (!isdigit((uint8_t)*p)) Error("exponent digits expected in hex float");
                            while (isdigit((uint8_t)*p)) p++;
                        } else if (ishexfloat) {
                            Error("hex float requires a p exponent");
                        }
                        if (ishexfloat) {
                            fval = strtod(string(start, p).c_str(), nullptr);
                            Set(T_FLTLIT);
                            return;
                        }
                        errno = 0;
                        ival = (int64_t)strtoull(string(numstart, p).c_str(), nullptr, 16);
                        if (errno) Error("hex literal too large for 64 bits");
                        iuns = ival < 0;
                        Set(T_INTLIT);
                        return;
                    }
                    while (isdigit((uint8_t)*p)) p++;
                    
                    
                    auto isfloat = *p == '.' && isdigit((uint8_t)p[1]);
                    if (isfloat) {
                        p++;
                        while (isdigit((uint8_t)*p)) p++;
                    }
                    if ((*p == 'e' || *p == 'E') &&
                        (isdigit((uint8_t)p[1]) ||
                         ((p[1] == '+' || p[1] == '-') && isdigit((uint8_t)p[2])))) {
                        isfloat = true;
                        p += 2;
                        while (isdigit((uint8_t)*p)) p++;
                    }
                    auto text = string(start, p);
                    if (isfloat) {
                        fval = strtod(text.c_str(), nullptr);
                        Set(T_FLTLIT);
                    } else {
                        
                        
                        errno = 0;
                        ival = (int64_t)strtoull(text.c_str(), nullptr, 10);
                        if (errno) Error(cat("integer literal too large for 64 bits: ", text));
                        iuns = ival < 0;
                        Set(T_INTLIT);
                    }
                    return;
                }
                if (IsIdentStart(c)) {
                    while (IsIdentCont(*p)) p++;
                    Set(T_IDENT);
                    static const unordered_map<string_view, TType> keywords = {
                        #define F(t, s) { s, t },
                        TOKENS_KEYWORDS
                        #undef F
                    };
                    auto it = keywords.find(attr);
                    if (it != keywords.end()) tok = it->second;
                    return;
                }
                Error(cat("illegal character: ", QuotedCharAt(start)));
            }
        }
    }

    
    
    
    
    
    
    void LexStringBody(const char *start) {
        sval.clear();
        smultiline = false;
        while (*p != '"') {
            if (*p == '$' && p[1] == '{') {
                p += 2;
                interp.push_back(0);
                sinterp = true;
                tok = T_STRLIT;
                attr = string_view(start, (size_t)(p - start));
                scanning = false;
                return;
            }
            sval += (char)LexEscapedChar('"');
        }
        p++;
        sinterp = false;
        tok = T_STRLIT;
        attr = string_view(start, (size_t)(p - start));
        scanning = false;
    }

    void LexRawString() {
        sval.clear();
        auto q = p;
        while (*q == ' ' || *q == '\t' || *q == '\r') q++;
        smultiline = *q == '\n';
        if (!smultiline) {
            for (; !(p[0] == '"' && p[1] == '"' && p[2] == '"'); p++) {
                if (!*p) Error("unterminated \"\"\" string");
                if (*p == '\n' || (*p == '\r' && p[1] == '\n'))
                    Error("a multi-line string's text starts on the line after its opening \"\"\"");
                sval += *p;
            }
            p += 3;
            return;
        }
        auto first = q + 1;
        auto close = first;
        for (; !(close[0] == '"' && close[1] == '"' && close[2] == '"'); close++)
            if (!*close) Error("unterminated \"\"\" string");
        auto closestart = close;
        while (closestart > first && closestart[-1] != '\n') closestart--;
        auto margin = string_view(closestart, (size_t)(close - closestart));
        if (margin.find_first_not_of(" \t") != string_view::npos)
            ErrorAt("only indentation may come before the closing \"\"\" of a multi-line string",
                    line + 1 + (int)count(first, closestart, '\n'), closestart, close);
        line++;
        for (auto l = first; l < closestart; line++) {
            linestart = l;
            auto e = l;
            while (*e != '\n') e++;
            auto text = string_view(l, (size_t)(e - l));
            
            if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
            if (l != first) sval += '\n';
            if (text.substr(0, margin.size()) == margin) {
                sval += text.substr(margin.size());
            } else if (text.find_first_not_of(" \t") != string_view::npos) {
                auto bad = l;
                while ((size_t)(bad - l) < margin.size() && *bad == margin[bad - l]) bad++;
                ErrorAt("a line of a multi-line string must start with the indentation of its "
                        "closing \"\"\"", line, l, bad);
            }
            l = e + 1;
        }
        linestart = closestart;
        p = close + 3;
    }

    
    int LexEscapedChar(char quote) {
        auto c = *p++;
        if (c == '\\') {
            auto e = *p++;
            switch (e) {
                case 'n':  return '\n';
                case 't':  return '\t';
                case 'r':  return '\r';
                case '0':  return 0;
                case '\\': return '\\';
                case '"':  return '"';
                case '\'': return '\'';
                case '$':  return '$';
                case 'x': {
                    if (!isxdigit((uint8_t)p[0]) || !isxdigit((uint8_t)p[1]))
                        Error("\\x escape requires two hex digits");
                    auto hex = [](char h) { return h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10; };
                    auto v = hex(*p) * 16 + hex(p[1]);
                    p += 2;
                    return v;
                }
                default: {
                    p -= 2;
                    auto seq = Utf8CharAt(p + 1);
                    Error(cat("unknown escape sequence: \\", seq.empty() ? QuotedCharAt(p + 1) : string(seq)));
                }
            }
        }
        if (!c || c == '\n') { p--; Error(cat("unterminated ", quote == '"' ? "string" : "character", " literal")); }
        return (uint8_t)c;
    }
};

}  
