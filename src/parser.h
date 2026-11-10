


#pragma once

namespace faloya {





struct Import {
    string path;                     
    bool relative = false;           
    string source;                   
    Line line;                       
    Line end;                        
};





enum TokenRole : uint8_t {
    R_ANGLE_OPEN = 1,     
    R_ANGLE_CLOSE = 2,    
    R_GLUE = 4,           
    R_TYPE_POSTFIX = 8,   
    R_UNARY = 16,         
    R_GLUE_LEFT = 32,     
    R_LIST = 64,          
    R_FUNVAL = 128,       
};

struct Parser {
    Ast &ast;
    Lexer lex;
    int fileidx;
    vector<Import> imports;          

    
    
    
    vector<pair<const char *, uint8_t>> *roles = nullptr;
    void Role(uint8_t r) { if (roles) roles->push_back({ lex.attr.data(), r }); }
    void RoleAt(const char *at, uint8_t r) { if (roles) roles->push_back({ at, r }); }
    size_t RoleMark() const { return roles ? roles->size() : 0; }
    void DropRoles(size_t mark) { if (roles) roles->resize(mark); }

    
    
    bool no_struct_lit = false;

    
    
    bool constcond = false;
    struct CondScope {
        Parser &p;
        bool was;
        CondScope(Parser &_p) : p(_p), was(_p.constcond) { p.constcond = true; }
        ~CondScope() { p.constcond = was; }
    };

    
    
    
    
    
    bool stmt_level = false;
    bool stmt_ended = false;

    SFunction *curfn = nullptr;      
    
    
    bool forbindtype = false;

    
    
    
    
    string_view filens;
    string_view curns;
    bool declseen = false;           

    
    
    
    
    
    
    int tentative = 0;
    struct Tentatively {
        Parser &p;
        Tentatively(Parser &_p) : p(_p) { p.tentative++; }
        ~Tentatively() { p.tentative--; }
    };
    
    
    
    Dot *pendingdot = nullptr;
    
    
    
    bool eoferror = false;

    Parser(Ast &_ast, string_view filename, const char *source, int _fileidx)
        : ast(_ast), lex(filename, source, _fileidx), fileidx(_fileidx) {}

    [[noreturn]] void Error(const string &msg) { lex.Error(msg); }

    
    
    
    void Report(const CompileError &e) {
        if (e.diags.empty()) {
            Diagnostic d;
            d.message = d.text = e.msg;
            ast.Report(d);
        }
        for (auto &d : e.diags) ast.Report(d);
    }

    
    
    
    void NextSkipping() {
        for (;;) {
            try {
                if (lex.scanning) lex.SkipBrokenToken();
                else lex.Next();
                return;
            } catch (CompileError &e) {
                Report(e);
            }
        }
    }

    
    
    bool StartsNext(bool declaration) {
        switch (lex.tok) {
            case T_LET: case T_VAR: case T_CONST: case T_REUSABLE: case T_RECURSIVE:
            case T_THREADFN: case T_EXTERN: case T_EXPORT: case T_SIMD:
                return true;
            case T_FN: {
                
                auto next = lex.PeekTok();
                return next == T_IDENT || next == T_COLONCOLON;
            }
            case T_IMPORT: case T_NAMESPACE: case T_STRUCT: case T_ENUM: case T_TYPE:
                return declaration;
            case T_IF: case T_MATCH: case T_LOOP: case T_WHILE: case T_FOR: case T_GUARD:
            case T_DEFER: case T_RETURN: case T_BREAK: case T_CONTINUE:
                return !declaration;
            default:
                return AtInline() || AtCheckfree();
        }
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    bool Resync(bool declaration, const char *startp, int stmtdepth = 0) {
        if (lex.scanning) NextSkipping();
        auto depth = 0;  
        for (auto consumed = lex.attr.data() != startp;; consumed = true) {
            auto atstmt = !declaration && lex.DepthBefore() <= stmtdepth;
            switch (lex.tok) {
                case T_EOF:
                    eoferror = true;
                    return true;
                case T_SEMI:
                    if (depth == 0) {
                        NextSkipping();
                        return false;
                    }
                    break;
                case T_LCURLY:
                    depth++;
                    break;
                case T_RCURLY:
                    if (atstmt) {
                        
                        NextSkipping();
                        return true;
                    }
                    
                    
                    
                    if (depth > 0 && --depth == 0 && declaration) {
                        NextSkipping();
                        return false;
                    }
                    break;
                default:
                    if ((declaration ? depth == 0 : atstmt) && consumed &&
                        StartsNext(declaration))
                        return false;
                    break;
            }
            NextSkipping();
        }
    }

    
    
    
    bool StartsDeclInBody() {
        switch (lex.tok) {
            case T_LET: case T_CONST: return false;
            default: return StartsNext(true);
        }
    }

    
    
    
    
    
    void SkipBody(int depth) {
        while (lex.tok != T_EOF) {
            auto inbody = lex.DepthBefore() == depth + 1;
            if (inbody && lex.tok == T_RCURLY) {
                NextSkipping();
                return;
            }
            if (inbody && StartsDeclInBody()) return;
            NextSkipping();
        }
    }

    
    
    
    
    
    
    
    
    
    template<typename F> void ParseMembers(bool &broken, int depth, const char *startp, F f) {
        auto kept = ast.Mark();
        try {
            f(kept);
        } catch (CompileError &e) {
            if (tentative) throw;
            
            if (!pendingdot) Report(e);
            pendingdot = nullptr;
            ast.DropSince(kept);
            broken = true;
            if (lex.scanning) NextSkipping();
            if (depth >= 0 && lex.DepthBefore() > depth) SkipBody(depth);
            else Resync(true, startp);
        }
    }

    
    
    
    void KeepMember(Ast::DeclMark *kept) {
        if (!kept) return;
        *kept = ast.Mark();
        pendingdot = nullptr;
    }

    
    Line CurLine() {
        return { lex.tokline, fileidx, (int)(lex.attr.data() - lex.toklinestart) + 1 };
    }
    
    
    Line PrevEnd() {
        return { lex.prevendline, fileidx, (int)(lex.prevend - lex.prevendlinestart) + 1 };
    }
    
    template<typename T> T *Finish(T *n, Line end) {
        n->end = end;
        return n;
    }
    template<typename T> T *Finish(T *n) { return Finish(n, PrevEnd()); }

    bool IsNext(TType t) {
        if (lex.tok != t) return false;
        lex.Next();
        return true;
    }

    
    
    bool AtInline() {
        if (lex.tok != T_IDENT || lex.attr != "inline") return false;
        auto next = lex.PeekTok();
        return next == T_FN || next == T_RECURSIVE || next == T_THREADFN;
    }

    
    
    bool AtCheckfree() {
        if (lex.tok != T_IDENT || lex.attr != "checkfree") return false;
        auto next = lex.PeekTok();
        return next == T_FN || next == T_RECURSIVE || next == T_THREADFN || next == T_SIMD ||
               next == T_EXTERN || next == T_IDENT;
    }

    void Expect(TType t, const char *context) {
        if (lex.tok != t)
            Error(cat("expected \'", TName(t), "\' in ", context, ", found \'", TokStr(), "\'"));
        lex.Next();
    }

    string TokStr() {
        switch (lex.tok) {
            case T_IDENT: case T_INTLIT: case T_FLTLIT: return string(lex.attr);
            case T_STRLIT: return "string literal";
            default: return TName(lex.tok);
        }
    }

    string_view ExpectIdent(const char *context) {
        if (lex.tok != T_IDENT)
            Error(cat("expected identifier in ", context, ", found \'", TokStr(), "\'"));
        auto name = lex.attr;
        lex.Next();
        return name;
    }

    
    void ExpectClosingAngle(const char *context) {
        if (lex.tok == T_SHR || lex.tok == T_GT) Role(R_ANGLE_CLOSE);
        if (lex.tok == T_SHR) { lex.tok = T_GT; return; }
        Expect(T_GT, context);
    }

    template<typename T, typename... Args> T *New(Args &&...args) {
        return ast.New<T>(std::forward<Args>(args)...);
    }

    TypeExpr *NewUnresolvedType(string_view name, Line line) {
        auto t = ast.NewType(TY_UNRESOLVED, line);
        t->named = ast.NewDetail<TypeName>();
        t->named->name = name;
        t->named->ns = curns;
        return t;
    }

    
    
    string_view ParseQualifiedName(const char *context) {
        if (IsNext(T_COLONCOLON)) return ast.Intern(cat("::", ExpectIdent(context)));
        auto name = ExpectIdent(context);
        if (!IsNext(T_COLONCOLON)) return name;
        return ast.Intern(cat(name, "::", ExpectIdent(context)));
    }

    string_view ParseStorageName(const char *context) {
        auto name = ParseQualifiedName(context);
        while (IsNext(T_DOT)) name = ast.Intern(cat(name, ".", ExpectIdent(context)));
        return name;
    }

    
    
    
    
    string_view ParseDeclName(const char *context) {
        auto name = lex.tok == T_COLONCOLON ? string_view {} : ExpectIdent(context);
        if (IsNext(T_COLONCOLON)) {
            curns = name;
            name = ExpectIdent(context);
        }
        return name;
    }

    string QualifiedStr(string_view ns, string_view name) {
        return ns.empty() ? string(name) : cat(ns, "::", name);
    }

    
    

    void ParseTop() {
        while (lex.tok != T_EOF) ParseTopDecl();
    }

    
    
    
    
    
    
    void ParseTopDecl() {
        ast.CheckCancel();
        auto mark = ast.Mark();
        auto startp = lex.attr.data();
        pendingdot = nullptr;
        try {
            ParseTopDeclInner();
        } catch (CompileError &e) {
            if (tentative) throw;
            
            
            if (!pendingdot) Report(e);
            pendingdot = nullptr;
            ast.DropSince(mark);
            Resync(true, startp);
        }
    }

    void ParseTopDeclInner() {
        auto line = CurLine();
        auto doc = lex.doc;
        curns = filens;
        switch (lex.tok) {
            case T_NAMESPACE: {
                
                
                lex.Next();
                if (!filens.empty()) Error("namespace declared twice in this file");
                if (declseen) Error("the namespace declaration must precede all declarations");
                filens = ExpectIdent("namespace declaration");
                Expect(T_SEMI, "namespace declaration");
                curns = filens;
                return;
            }
            case T_IMPORT: {
                Import imp;
                imp.line = line;
                lex.Next();
                imp.relative = IsNext(T_DOT);
                imp.path = ExpectIdent("import");
                while (IsNext(T_DOT)) Append(imp.path, "/", ExpectIdent("import"));
                
                if (lex.tok == T_IDENT && lex.attr == "from") {
                    if (imp.relative || imp.path.find('/') != string::npos)
                        Error("a sourced import names one package: import name from \"source\";");
                    lex.Next();
                    if (lex.tok != T_STRLIT) Error("expected the package's source, a string, after from");
                    imp.source = lex.sval;
                    lex.Next();
                }
                Expect(T_SEMI, "import");
                imp.end = PrevEnd();
                
                
                imports.push_back(imp);
                return;
            }
            default: break;
        }
        declseen = true;
        
        if (lex.tok == T_IDENT && lex.attr == "root" && lex.PeekTok() == T_STRUCT) {
            ParseStructDecl(true);
            return;
        }
        if (AtInline() || AtCheckfree()) goto fndecl;
        switch (lex.tok) {
            case T_STRUCT: ParseStructDecl(); return;
            case T_ENUM:   ParseEnumDecl();   return;
            case T_TYPE: {
                lex.Next();
                auto al = new SAlias();
                ast.aliases.push_back(al);
                al->name = ParseDeclName("type alias");
                al->ns = curns;
                al->qname = ast.QualifiedName(al->ns, al->name);
                al->line = line;
                al->doc = doc;
                CheckFreshTypeName(al->ns, al->name);
                Expect(T_ASSIGN, "type alias");
                al->type = ParseType();
                Expect(T_SEMI, "type alias");
                al->end = PrevEnd();
                ast.NS(al->ns).aliasmap[al->name] = al;
                ast.topdecls.push_back(Finish(New<AliasDecl>(line, al)));
                return;
            }
            case T_EXTERN: {
                
                
                
                
                ExternPrefix pre { CurLine(), lex.doc, "", "" };
                auto depth = lex.DepthBefore();
                auto startp = lex.attr.data();
                lex.Next();
                if (lex.tok == T_STRLIT) {
                    pre.cname = lex.sval;
                    lex.Next();
                }
                if (lex.tok == T_LCURLY) {
                    ParseExternBlock(pre.cname, depth, startp);
                    return;
                }
                ast.topdecls.push_back(ParseFnDecl(false, &pre));
                return;
            }
            case T_RECURSIVE: case T_FN: case T_THREADFN: case T_EXPORT:
            case T_SIMD: fndecl: {
                auto fd = ParseFnDecl(false);
                ast.topdecls.push_back(fd);
                return;
            }
            case T_REUSABLE: case T_LET: case T_VAR: case T_CONST: {
                auto vd = ParseVarDecl(true);
                Expect(T_SEMI, "global declaration");
                ast.topdecls.push_back(vd);
                return;
            }
            default:
                Error(cat("top-level declaration expected, found \'", TokStr(), "\'"));
        }
    }

    void CheckFreshTypeName(string_view ns, string_view name) {
        if (auto n = ast.FindNS(ns); n && n->TypeNameExists(name))
            Error(cat("type name already declared: ", QualifiedStr(ns, name)));
    }

    
    void ParseGenerics(vector<GenericParam> &generics, Ast::DeclMark *kept = nullptr) {
        if (lex.tok != T_LT) return;
        Role(R_ANGLE_OPEN);
        lex.Next();
        for (;;) {
            GenericParam g;
            g.line = CurLine();
            g.name = ExpectIdent("generic parameter list");
            for (auto &prev : generics)
                if (prev.name == g.name) Error(cat("duplicate type parameter name: ", g.name));
            if (IsNext(T_COLON)) g.bound = ParseType();
            generics.push_back(g);
            KeepMember(kept);
            if (!IsNext(T_COMMA)) break;
            if (lex.tok == T_GT || lex.tok == T_SHR) break;
        }
        ExpectClosingAngle("generic parameter list");
    }

    
    
    
    
    
    void BindGenericNames(size_t firsttype, const vector<GenericParam> &generics) {
        for (auto i = firsttype; i < ast.alltypes.size(); i++) {
            auto t = ast.alltypes[i];
            if (t->kind != TY_UNRESOLVED) continue;
            for (auto &g : generics)
                if (t->named->name == g.name) { t->kind = TY_GENERIC; break; }
        }
    }

    void ParseStructDecl(bool isroot = false) {
        auto firsttype = ast.alltypes.size();
        auto line = CurLine();
        auto doc = lex.doc;
        auto depth = lex.DepthBefore();
        auto startp = lex.attr.data();
        if (isroot) lex.Next();
        Expect(T_STRUCT, "struct declaration");
        auto st = new SStruct();
        st->isroot = isroot;
        ast.structs.push_back(st);
        st->name = ParseDeclName("struct declaration");
        st->ns = curns;
        st->qname = ast.QualifiedName(st->ns, st->name);
        st->line = line;
        st->doc = doc;
        CheckFreshTypeName(st->ns, st->name);
        ParseMembers(st->broken, depth, startp, [&](Ast::DeclMark &kept) {
            ParseGenerics(st->generics, &kept);
            if (lex.tok == T_LCURLY) Role(R_LIST);
            Expect(T_LCURLY, "struct declaration");
            ParseFieldList(st->fields, "struct body", &kept);
            Expect(T_RCURLY, "struct declaration");
        });
        st->end = PrevEnd();
        BindGenericNames(firsttype, st->generics);
        ast.NS(st->ns).structmap[st->name] = st;
        ast.topdecls.push_back(Finish(New<StructDecl>(line, st)));
    }

    
    void ParseFieldList(vector<Field> &fields, const char *context,
                        Ast::DeclMark *kept = nullptr) {
        while (lex.tok != T_RCURLY) {
            Field f;
            f.line = CurLine();
            f.doc = lex.doc;
            if (IsNext(T_PAD)) {
                f.ispad = true;
                if (lex.tok == T_INTLIT) {
                    f.padsize = lex.ival;
                    lex.Next();
                }
            } else {
                
                auto constq = IsNext(T_CONST);
                f.isconst = constq || IsNext(T_LET);
                f.line = CurLine();
                f.name = ExpectIdent(context);
                for (auto &prev : fields)
                    if (!prev.ispad && prev.name == f.name)
                        Error(cat("duplicate field name: ", f.name));
                Expect(T_COLON, context);
                f.type = ParseType();
                if (constq) f.type = ast.ConstOf(f.type);
                if (IsNext(T_ASSIGN)) f.defaultval = ParseExpr();
            }
            fields.push_back(f);
            KeepMember(kept);
            if (!IsNext(T_COMMA)) break;
        }
    }

    void ParseEnumDecl() {
        auto firsttype = ast.alltypes.size();
        auto line = CurLine();
        auto doc = lex.doc;
        auto depth = lex.DepthBefore();
        auto startp = lex.attr.data();
        lex.Next();
        auto en = new SEnum();
        ast.enums.push_back(en);
        en->name = ParseDeclName("enum declaration");
        en->ns = curns;
        en->qname = ast.QualifiedName(en->ns, en->name);
        en->line = line;
        en->doc = doc;
        CheckFreshTypeName(en->ns, en->name);
        ParseMembers(en->broken, depth, startp, [&](Ast::DeclMark &kept) {
            ParseGenerics(en->generics, &kept);
            if (lex.tok == T_LCURLY) Role(R_LIST);
            Expect(T_LCURLY, "enum declaration");
            while (lex.tok != T_RCURLY) {
                SVariant v;
                auto vstart = lex.attr.data();
                v.line = CurLine();
                v.doc = lex.doc;
                v.name = ExpectIdent("enum variant");
                for (auto &prev : en->variants)
                    if (prev.name == v.name) Error(cat("duplicate variant name: ", v.name));
                if (lex.tok == T_LCURLY) {
                    Role(R_LIST);
                    lex.Next();
                    v.has_payload = true;
                    ParseFieldList(v.fields, "variant body");
                    Expect(T_RCURLY, "enum variant");
                }
                v.src = string_view(vstart, lex.prevend - vstart);
                en->variants.push_back(v);
                KeepMember(&kept);
                if (!IsNext(T_COMMA)) break;
            }
            Expect(T_RCURLY, "enum declaration");
        });
        en->end = PrevEnd();
        BindGenericNames(firsttype, en->generics);
        ast.NS(en->ns).enummap[en->name] = en;
        ast.topdecls.push_back(Finish(New<EnumDecl>(line, en)));
    }

    
    
    
    bool InFetchedPackage() const {
        if (fileidx < 0 || fileidx >= (int)ast.filepkg.size()) return false;
        auto pkg = ast.filepkg[fileidx];
        if (pkg < 0 || pkg >= (int)ast.packages.size()) return false;
        
        
        auto &p = ast.packages[pkg];
        return !p.cachekey.empty() || p.builtin >= 0;
    }

    
    
    
    
    struct ExternPrefix {
        Line line;
        string_view doc;    
        string cname;       
        string dylib;       
    };

    
    
    
    
    
    
    void ParseExternBlock(const string &lib, int depth, const char *startp) {
        if (lib.empty()) Error("an extern block names the library it binds from: extern \"lib\" {");
        Expect(T_LCURLY, "extern block");
        auto broken = false;
        while (lex.DepthBefore() > depth && lex.tok != T_RCURLY && lex.tok != T_EOF) {
            ExternPrefix pre { CurLine(), lex.doc, "", lib };
            auto memberp = lex.attr.data();
            ParseMembers(broken, depth, startp, [&](Ast::DeclMark &kept) {
                ast.topdecls.push_back(ParseFnDecl(false, &pre));
                kept = ast.Mark();
            });
            
            
            
            if (lex.attr.data() == memberp) NextSkipping();
        }
        
        if (lex.DepthBefore() > depth) Expect(T_RCURLY, "extern block");
    }

    FnDecl *ParseFnDecl(bool nested, const ExternPrefix *pre = nullptr) {
        auto firsttype = ast.alltypes.size();
        auto line = pre ? pre->line : CurLine();
        auto startp = lex.attr.data();
        auto sf = new SFunction();
        ast.functions.push_back(sf);
        sf->line = line;
        sf->doc = pre ? pre->doc : lex.doc;
        sf->isnested = nested;
        sf->outer = curfn;
        auto inblock = pre && !pre->dylib.empty();
        if (pre) {
            sf->isextern = true;
            sf->cname = pre->cname;
            sf->dylib = pre->dylib;
            if (inblock && lex.tok == T_STRLIT)
                Error(cat("a member's symbol follows its signature: fn name(...) = \"", lex.sval,
                          "\"; (§7.10)"));
        }
        
        
        auto oldcname = pre && !inblock && !pre->cname.empty();
        auto oldline = line;
        auto checkfree = false;
        if (AtCheckfree()) {
            checkfree = true;
            lex.Next();
        }
        if (lex.tok == T_EXPORT) {
            if (sf->isextern) Error("export fn cannot be extern");
            sf->isexport = true;
            if (nested) Error("export fn must be declared at top level");
            lex.Next();
            if (lex.tok == T_STRLIT) {
                sf->cname = lex.sval;
                lex.Next();
            }
        }
        if (lex.tok == T_EXTERN) {
            if (sf->isexport) Error("export fn cannot be extern");
            if (inblock)
                Error("a declaration in an extern block is written without extern: the block's "
                      "extern covers it");
            if (sf->isextern) Error("extern repeated in a function declaration");
            sf->isextern = true;
            if (nested) Error("extern fn must be declared at top level");
            oldline = CurLine();
            lex.Next();
            if (lex.tok == T_STRLIT) {
                sf->cname = lex.sval;
                oldcname = true;
                lex.Next();
            }
        }
        if (!checkfree && AtCheckfree()) {
            checkfree = true;
            lex.Next();
        }
        if (checkfree && sf->isextern)
            Error("extern fn cannot be checkfree: it has no body to check");
        
        sf->checkfree = checkfree || (nested && curfn && curfn->checkfree);
        if (IsNext(T_SIMD)) {
            if (sf->isextern) Error("extern fn cannot be simd: it has no body to compile");
            sf->issimd = true;
        }
        if (AtInline()) {
            if (sf->isextern) Error("extern fn cannot be inline: it has no body to copy");
            if (sf->isexport) Error("export fn cannot be inline: its C wrapper calls it");
            if (sf->issimd) Error("simd fn cannot be inline: its callers would run the baseline only");
            sf->isinline = true;
            lex.Next();
        }
        sf->isrec = IsNext(T_RECURSIVE);
        if (sf->isinline && sf->isrec) Error("recursive fn cannot be inline");
        if (lex.tok == T_THREADFN) {
            if (sf->isinline) Error("thread_fn cannot be inline");
            sf->isthread = true;
            if (nested) Error("thread_fn must be declared at top level");
            lex.Next();
        } else {
            Expect(T_FN, "function declaration");
        }
        if (nested) {
            sf->name = ExpectIdent("function declaration");
            if (lex.tok == T_COLONCOLON)
                Error("a nested function cannot be declared into a namespace");
        } else {
            sf->name = ParseDeclName("function declaration");
        }
        sf->ns = curns;
        sf->qname = ast.QualifiedName(sf->ns, sf->name);
        
        
        
        auto header = [&](Ast::DeclMark *kept) {
            ParseGenerics(sf->generics, kept);
            if (lex.tok == T_LPAREN) Role(R_LIST);
            Expect(T_LPAREN, "function declaration");
            while (lex.tok != T_RPAREN) {
                Param p;
                p.isvar = IsNext(T_VAR);
                p.line = CurLine();
                p.name = ExpectIdent("parameter list");
                for (auto &prev : sf->params)
                    if (prev.name == p.name) Error(cat("duplicate parameter name: ", p.name));
                if (IsNext(T_COLON)) p.type = ParseType();
                if (IsNext(T_ASSIGN)) {
                    
                    
                    if (!p.type)
                        Error(cat("parameter ", p.name, " has a default but no type: an untyped "
                                  "parameter takes its type from its argument (§7.1)"));
                    if (sf->isthread)
                        Error(cat("thread_fn parameter ", p.name, " cannot have a default: "
                                  "thread_spawn passes every argument (§11.2)"));
                    p.defaultval = ParseExpr();
                } else if (!sf->params.empty() && sf->params.back().defaultval) {
                    Error(cat("parameter ", p.name, " needs a default: only trailing parameters "
                              "have defaults, and ", sf->params.back().name,
                              " before it has one (§7.1)"));
                }
                sf->params.push_back(p);
                KeepMember(kept);
                if (!IsNext(T_COMMA)) break;
            }
            Expect(T_RPAREN, "function declaration");
            if (IsNext(T_ARROW)) {
                
                sf->has_rets = true;
                for (;;) {
                    sf->rets.push_back(ParseType());
                    KeepMember(kept);
                    if (!IsNext(T_COMMA)) break;
                }
            }
            if (sf->isextern) {
                
                if (IsNext(T_ASSIGN)) {
                    if (lex.tok != T_STRLIT)
                        Error("an extern fn's symbol is a string: fn name(...) = \"symbol\"; (§7.10)");
                    if (oldcname)
                        Error(cat("extern fn ", sf->name, " names its symbol twice, as \"", sf->cname,
                                  "\" and \"", lex.sval, "\": write only = \"", lex.sval, "\""));
                    sf->cname = lex.sval;
                    lex.Next();
                } else if (oldcname && !InFetchedPackage()) {
                    ast.Warn(LocOf(oldline),
                             cat("extern \"", sf->cname, "\" fn is deprecated: write the symbol after "
                                 "the signature, extern fn ", sf->name, "(...) = \"", sf->cname,
                                 "\"; (§7.10)"));
                }
                if (sf->cname.empty()) sf->cname = string(sf->name);
                
                
                sf->readsalltops = sf->cname == "gs_trim_memory";
                sf->takesloc = sf->cname == "gs_slice_pool_place" ||
                               sf->cname == "gs_slice_pool_free" ||
                               sf->cname == "gs_slice_pool_regrow" ||
                               sf->cname == "gs_slice_pool_trim";
                Expect(T_SEMI, "extern declaration");
            } else if (lex.tok == T_ASSIGN) {
                Error("only an extern fn takes a symbol after its signature: = \"symbol\" (§7.10)");
            } else if (kept && lex.tok != T_LCURLY) {
                
                Expect(T_LCURLY, "function body");
            }
        };
        
        if (nested || inblock) header(nullptr);
        else ParseMembers(sf->brokensig, -1, startp, [&](Ast::DeclMark &kept) { header(&kept); });
        if (!sf->isextern && !sf->brokensig) {
            if (sf->isexport && sf->cname.empty()) sf->cname = string(sf->name);
            
            
            struct FnScope {
                Parser &p;
                SFunction *saved;
                FnScope(Parser &_p, SFunction *sf) : p(_p), saved(_p.curfn) { p.curfn = sf; }
                ~FnScope() { p.curfn = saved; }
            } fs(*this, sf);
            sf->body = ParseBlockExpr("function body");
        }
        sf->end = PrevEnd();
        BindGenericNames(firsttype, sf->generics);
        
        
        
        if (!nested && !(sf->name == "main" && fileidx != 0))
            ast.NS(sf->ns).functionmap[sf->name].push_back(sf);
        return Finish(New<FnDecl>(line, sf));
    }

    VarDecl *ParseVarDecl(bool isglobal) {
        auto line = CurLine();
        auto doc = lex.doc;
        auto reusable = 0;
        if (IsNext(T_REUSABLE)) {
            reusable = RU_SLOTS;
            if (IsNext(T_LBRACKET)) {
                Expect(T_RBRACKET, "reusable[]");
                reusable = RU_SLICES;
            }
        }
        bool isvar, isconst = false;
        if (IsNext(T_VAR)) isvar = true;
        else if (IsNext(T_LET)) isvar = false;
        else if (IsNext(T_CONST)) { isvar = false; isconst = true; }
        else { Error("let, var or const expected"); }
        auto vd = New<VarDecl>(line, isvar);
        vd->isconst = isconst;
        vd->reusable = reusable;
        vd->isglobal = isglobal;
        vd->doc = doc;
        for (;;) {
            vd->names.push_back(isglobal ? ParseDeclName("global declaration")
                                         : ExpectIdent("variable declaration"));
            if (!IsNext(T_COMMA)) break;
        }
        vd->ns = curns;
        if (IsNext(T_COLON)) vd->type = ParseType();
        
        
        if (IsNext(T_DOTASSIGN)) vd->byref = true;
        if (vd->byref || IsNext(T_ASSIGN)) {
            for (;;) {
                vd->inits.push_back(ParseExpr());
                if (!IsNext(T_COMMA)) break;
            }
        }
        if (isglobal) {
            
            
            if (vd->inits.empty() && !vd->isvar)
                Error("let and const global declarations require an initializer");
            if (vd->names.size() != 1) Error("global declarations declare a single name");
            auto name = vd->names[0];
            auto &ns = ast.NS(vd->ns);
            if (ns.globalmap.count(name))
                Error(cat("global name already declared: ", QualifiedStr(vd->ns, name)));
            ns.globalmap[name] = vd;
            ast.globals.push_back(vd);
        }
        return Finish(vd);
    }

    
    

    TypeExpr *ParseType() {
        auto typebegin = lex.attr.data();
        auto t = ParseTypeInner();
        if (t) { t->srcbegin = typebegin; t->srcend = lex.prevend; }
        return t;
    }

    TypeExpr *ParseTypeInner() {
        
        
        
        
        
        auto constq = IsNext(T_CONST);
        auto t = ParseTypePrimary();
        auto qualify = [&](TypeExpr *n) {
            n->cq = constq;
            constq = false;
        };
        
        for (;;) {
            auto line = CurLine();
            switch (lex.tok) {
                case T_LBRACKET: {
                    lex.Next();
                    if (IsNext(T_COLON)) {
                        Expect(T_RBRACKET, "slice type");
                        auto sl = ast.SliceOf(t, line);
                        qualify(sl);
                        t = sl;
                        continue;
                    }
                    auto a = ast.NewType(TY_ARRAY, line);
                    a->arr = ast.NewDetail<TypeArray>();
                    a->arr->sub = t;
                    if (IsNext(T_RBRACKET)) { a->arr->akind = A_VAR; t = a; continue; }
                    if (IsNext(T_DOTDOT)) {
                        a->arr->akind = A_LIMITED;
                        if (lex.tok != T_RBRACKET) a->arr->sizeexpr = ParseTypeSizeExpr();
                    } else if (lex.tok == T_GT) {
                        Role(R_GLUE);
                        lex.Next();
                        if (lex.tok == T_DOTDOT) Role(R_GLUE);
                        Expect(T_DOTDOT, "resizable array type");
                        if (lex.tok == T_LT) Role(R_GLUE);
                        a->arr->akind = IsNext(T_LT) ? A_GROWSHRINK : A_GROW;
                    } else if (IsPrimTypeToken(lex.tok)) {
                        a->arr->akind = A_VAR;
                        a->arr->lenstorage = ParseLengthStorage("array length type");
                    } else {
                        a->arr->akind = A_FIXED;
                        a->arr->sizeexpr = ParseTypeSizeExpr();
                    }
                    Expect(T_RBRACKET, "array type");
                    t = a;
                    continue;
                }
                case T_BITAND: {
                    Role(R_TYPE_POSTFIX);
                    lex.Next();
                    auto r = ast.RefTo(t, line);
                    qualify(r);
                    if (lex.tok == T_LT) Role(R_ANGLE_OPEN);
                    if (IsNext(T_LT)) {
                        r->ref->lenstorage = ParseLengthStorage("relative reference width");
                        
                        
                        if (IsNext(T_IN)) {
                            r->ref->poolname = ParseStorageName("relative reference pool");
                            r->ref->poolns = curns;
                        }
                        ExpectClosingAngle("relative reference width");
                    }
                    t = r;
                    continue;
                }
                case T_QUESTION: {
                    Role(R_TYPE_POSTFIX);
                    lex.Next();
                    if (t->kind == TY_REF) {
                        
                        if (t->ref->optional) Error("type is already optional");
                        t->ref->optional = true;
                    } else {
                        auto o = ast.RefTo(t, line, true);
                        qualify(o);
                        t = o;
                    }
                    continue;
                }
                case T_DOTDOT: {
                    Role(R_TYPE_POSTFIX);
                    lex.Next();
                    
                    if (t->kind != TY_UNRESOLVED)
                        Error("variable mode (..) applies only to ADT type names");
                    if (t->named->varmode) Error("type is already variable-mode");
                    t->named->varmode = true;
                    continue;
                }
                case T_DOT: {
                    lex.Next();
                    auto name = ExpectIdent("variant type");
                    t = ast.VariantOf(t, name, nullptr, line);
                    continue;
                }
                default:
                    if (constq) t = ast.ConstOf(t);
                    return t;
            }
        }
    }

    
    
    int ParseLengthStorage(const char *context) {
        int s;
        switch (lex.tok) {
            case T_TU8:     s = IS_U8;     break;
            case T_TU16:    s = IS_U16;    break;
            case T_TU32:    s = IS_U32;    break;
            case T_TU64:    s = IS_U64;    break;
            case T_TVARINT: s = IS_VARINT; break;
            default:
                Error(cat("unsigned integer storage type or varint expected in ", context,
                          ", found \'", TokStr(), "\'"));
        }
        lex.Next();
        return s;
    }

    
    Node *ParseTypeSizeExpr() {
        auto sub = Sub();
        return ParseExpr();
    }

    TypeExpr *ParseTypePrimary() {
        auto line = CurLine();
        if (IsPrimTypeToken(lex.tok)) {
            auto t = ast.PrimTypeForToken(lex.tok);
            lex.Next();
            auto upto = lex.tok == T_IDENT && lex.attr == "upto";
            if (t->kind == TY_INT && !forbindtype && (lex.tok == T_IN || upto)) {
                
                if (!IsUnsigned(t->intstorage))
                    Error(cat("an index type is unsigned (u8, u16, u32 or u64), not ",
                              IntStorageName(t->intstorage)));
                lex.Next();
                auto it = ast.NewType(TY_INT, line);
                it->intstorage = t->intstorage;
                it->ix = ast.NewDetail<TypeIndex>();
                it->ix->upto = upto;
                it->ix->name = ParseStorageName(upto ? "bound type" : "index type");
                it->ix->ns = curns;
                
                
                if (lex.tok == T_LBRACKET || lex.tok == T_BITAND || lex.tok == T_QUESTION ||
                    lex.tok == T_DOTDOT || lex.tok == T_DOT)
                    Error(cat("an index type takes no postfix: parenthesize it, as in (",
                              IntStorageName(it->intstorage), upto ? " upto " : " in ",
                              it->ix->name, ")", TokStr()));
                return it;
            }
            return t;
        }
        switch (lex.tok) {
            case T_IDENT: case T_COLONCOLON: {
                auto t = NewUnresolvedType(ParseQualifiedName("type"), line);
                if (lex.tok == T_LT) Role(R_ANGLE_OPEN);
                if (IsNext(T_LT)) {
                    for (;;) {
                        t->named->args.push_back(ParseType());
                        if (!IsNext(T_COMMA)) break;
                        if (lex.tok == T_GT || lex.tok == T_SHR) break;
                    }
                    ExpectClosingAngle("type argument list");
                }
                return t;
            }
            case T_FN: {
                lex.Next();
                auto t = ast.NewType(TY_FN, line);
                t->fn = ast.NewDetail<TypeFn>();
                if (lex.tok == T_LPAREN) Role(R_LIST);
                if (IsNext(T_LPAREN)) {
                    t->fn->has_sig = true;
                    while (lex.tok != T_RPAREN) {
                        t->fn->args.push_back(ParseType());
                        if (!IsNext(T_COMMA)) break;
                    }
                    Expect(T_RPAREN, "function type");
                    if (IsNext(T_ARROW)) {
                        
                        if (lex.tok == T_LPAREN) Role(R_LIST);
                        if (IsNext(T_LPAREN)) {
                            while (lex.tok != T_RPAREN) {
                                t->fn->rets.push_back(ParseType());
                                if (!IsNext(T_COMMA)) break;
                            }
                            Expect(T_RPAREN, "function type return list");
                        } else {
                            t->fn->rets.push_back(ParseType());
                        }
                    }
                }
                return t;
            }
            case T_LPAREN: {
                
                lex.Next();
                auto forbind = forbindtype;
                forbindtype = false;
                auto t = ParseType();
                forbindtype = forbind;
                Expect(T_RPAREN, "parenthesized type");
                return t;
            }
            default:
                Error(cat("type expected, found \'", TokStr(), "\'"));
        }
    }

    
    

    
    
    
    
    
    
    
    struct Context {
        Parser &p;
        bool nsl, sl, se;
        Context(Parser &_p, bool nostructlit, bool onspine)
            : p(_p), nsl(_p.no_struct_lit), sl(_p.stmt_level), se(_p.stmt_ended) {
            p.no_struct_lit = nostructlit;
            p.stmt_level = onspine;
            p.stmt_ended = false;
        }
        ~Context() {
            p.no_struct_lit = nsl;
            p.stmt_level = sl;
            p.stmt_ended = se;
        }
    };
    
    
    Context Sub(bool nostructlit = false) { return Context(*this, nostructlit, false); }

    Node *ParseExpr() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_IF: {
                auto e = ParseIf();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_MATCH: {
                auto e = ParseMatch();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_BLOCK: {
                lex.Next();
                auto e = Finish(New<EarlyBlock>(line, ParseBlockExpr("block expression")));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_LCURLY: {
                
                
                
                auto e = ParseBlockExpr("block");
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_WHILE: {
                lex.Next();
                auto cond = ParseScrutinee();
                auto e = Finish(New<While>(line, cond, ParseBlockExpr("while body")));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_LOOP: {
                lex.Next();
                auto e = Finish(New<LoopExpr>(line, ParseBlockExpr("loop body")));
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_FOR: {
                auto e = ParseFor();
                if (stmt_level) stmt_ended = true;
                return e;
            }
            case T_GUARD:
                Error("guard is a statement: it guards the rest of the block it is in");
            case T_DEFER:
                Error("defer is a statement: it runs its body when the block it is in ends");
            case T_RETURN: {
                lex.Next();
                auto sub = Sub(no_struct_lit);
                auto r = New<Return>(line);
                if (lex.tok != T_SEMI && lex.tok != T_RCURLY && !AtReturnFrom()) {
                    for (;;) {
                        r->vals.push_back(ParseExpr());
                        if (!IsNext(T_COMMA)) break;
                    }
                }
                if (AtReturnFrom()) {
                    lex.Next();
                    r->from = ParseQualifiedName("return from");
                    r->ns = curns;
                }
                return Finish(r);
            }
            case T_BREAK: {
                lex.Next();
                auto sub = Sub(no_struct_lit);
                auto val = lex.tok != T_SEMI && lex.tok != T_RCURLY ? ParseExpr() : nullptr;
                return Finish(New<Break>(line, val));
            }
            case T_CONTINUE:
                lex.Next();
                return Finish(New<Continue>(line));
            default:
                return ParseBinary(1);
        }
    }

    
    
    Node *ParseScrutinee() {
        auto sub = Sub(true);
        return ParseExpr();
    }

    Node *ParseIf() {
        auto line = CurLine();
        lex.Next();
        auto cond = ParseScrutinee();
        auto thenb = ParseBlockExpr("if body");
        Node *elseb = nullptr;
        if (IsNext(T_ELSE))
            elseb = lex.tok == T_IF ? ParseIf() : ParseBlockExpr("else body");
        return Finish(New<IfExpr>(line, cond, thenb, elseb));
    }

    Node *ParseFor() {
        auto line = CurLine();
        lex.Next();
        if (lex.tok == T_BITAND) Role(R_UNARY);
        auto byref = IsNext(T_BITAND);
        auto var = ExpectIdent("for binding");
        TypeExpr *vartype = nullptr, *idxtype = nullptr;
        string_view idxvar;
        {
            
            struct Scope {
                bool &f, saved;
                Scope(bool &_f) : f(_f), saved(_f) { f = true; }
                ~Scope() { f = saved; }
            } fs(forbindtype);
            if (IsNext(T_COLON)) vartype = ParseType();
            if (IsNext(T_COMMA)) {
                idxvar = ExpectIdent("for index binding");
                if (IsNext(T_COLON)) idxtype = ParseType();
            }
        }
        Expect(T_IN, "for statement");
        Node *iter;
        {
            auto sub = Sub(true);
            iter = ParseExpr();
            if (IsNext(T_DOTDOT)) iter = Finish(New<RangeExpr>(line, iter, ParseExpr()));
        }
        auto f = New<ForLoop>(line, byref, var, idxvar, iter, ParseBlockExpr("for body"));
        f->vartype = vartype;
        f->idxtype = idxtype;
        return Finish(f);
    }

    Node *ParseMatch() {
        auto line = CurLine();
        lex.Next();
        auto m = New<MatchExpr>(line, ParseScrutinee());
        auto open = lex.attr.data();
        Expect(T_LCURLY, "match expression");
        ParseArms(m->arms);
        if (ArmsTakeComma(m->arms)) RoleAt(open, R_LIST);
        Expect(T_RCURLY, "match expression");
        if (m->arms.empty()) Error("match must have at least one arm");
        return Finish(m);
    }

    
    
    
    void ParseArms(vector<MatchArm> &arms) {
        auto sub = Sub(no_struct_lit);
        while (lex.tok != T_RCURLY) {
            MatchArm arm;
            if (lex.tok == T_CONST && lex.PeekTok() == T_FOR) {
                arm.expansion = ParseConstFor(false, true);
            } else if (lex.tok == T_CONST && lex.PeekTok() == T_IF) {
                arm.expansion = ParseConstIf(false, true);
            } else {
                arm.pat = ParsePattern();
                Expect(T_FATARROW, "match arm");
                arm.body = ParseExpr();
            }
            arms.push_back(arm);
            if (!IsNext(T_COMMA)) break;
        }
    }

    
    
    
    
    bool ArmsTakeComma(const vector<MatchArm> &arms) {
        if (arms.empty() || !arms.back().body) return true;
        for (auto n = arms.back().body;;) {
            if (auto b = Is<Break>(n)) {
                if (!b->val) return false;
                n = b->val;
                continue;
            }
            if (auto r = Is<Return>(n)) return !r->from.empty();
            return true;
        }
    }

    Pattern ParsePattern() {
        Pattern p;
        auto wildcard = [&]() { return lex.tok == T_IDENT && lex.attr == "_"; };
        if (wildcard()) {
            lex.Next();
            if (lex.tok == T_COMMA) Error("_ cannot be listed with other patterns");
            return p;
        }
        p.kind = P_LIST;
        for (;;) {
            p.items.push_back(ParsePatternItem());
            
            
            
            auto &item = p.items.back();
            auto id = Is<Ident>(item.lo);
            if (id && !item.hi && id->name.find("::") == string_view::npos &&
                (lex.tok == T_BITAND || lex.tok == T_IDENT)) {
                auto inlist = "a list of patterns binds no payload: give the variant an arm "
                              "of its own";
                if (p.items.size() > 1) Error(inlist);
                
                if (lex.tok == T_BITAND) Role(R_UNARY);
                p.byref = IsNext(T_BITAND);
                p.binder = ExpectIdent("match binding");
                if (lex.tok == T_COMMA) Error(inlist);
                return p;
            }
            if (!IsNext(T_COMMA)) return p;
            if (wildcard()) Error("_ cannot be listed with other patterns");
        }
    }

    PatItem ParsePatternItem() {
        PatItem r;
        r.lo = ParsePatternBound();
        if (IsNext(T_DOTDOT)) r.hi = ParsePatternBound();
        return r;
    }

    
    
    Node *ParsePatternBound() {
        auto line = CurLine();
        if (lex.tok == T_MINUS) Role(R_UNARY);
        auto neg = IsNext(T_MINUS);
        auto operandline = CurLine();
        if (lex.tok == T_IDENT || lex.tok == T_COLONCOLON) {
            Node *n = Finish(New<Ident>(operandline, ParseQualifiedName("match pattern"), curns));
            return neg ? Finish(New<Unary>(line, T_MINUS, n)) : n;
        }
        if (lex.tok != T_INTLIT)
            Error(cat("integer constant expected in match pattern, found \'", TokStr(), "\'"));
        if (neg && lex.iuns && lex.ival != INT64_MIN)
            Error("negated literal too large for i64");
        
        auto val = neg ? (int64_t)(0u - (uint64_t)lex.ival) : lex.ival;
        auto n = New<IntLit>(line, val, neg ? string_view {} : lex.attr, !neg && lex.iuns);
        lex.Next();
        return Finish(n);
    }

    
    
    int BinPrec(TType t) {
        switch (t) {
            case T_MUL: case T_DIV: case T_MOD:                return 10;
            case T_PLUS: case T_MINUS:                         return 9;
            case T_SHL: case T_SHR:                            return 8;
            case T_BITAND:                                     return 7;
            case T_XOR:                                        return 6;
            case T_BITOR:                                      return 5;
            case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:    return 4;
            case T_EQ: case T_NEQ: case T_DOTEQ: case T_DOTNEQ: return 3;
            case T_ANDAND:                                     return 2;
            case T_OROR:                                       return 1;
            default:                                           return 0;
        }
    }

    
    
    
    bool AtReturnFrom() {
        if (lex.tok != T_IDENT || lex.attr != "from") return false;
        Lexer save = lex;
        lex.Next();
        auto isfrom = lex.tok == T_IDENT || lex.tok == T_COLONCOLON;
        lex = save;
        return isfrom;
    }

    Node *ParseBinary(int minprec) {
        auto l = ParseUnary();
        for (;;) {
            if (stmt_ended) return l;  
            if (minprec <= 1 && lex.tok == T_PIPE) {
                auto line = CurLine();
                lex.Next();
                Node *r;
                {
                    auto sub = Sub(no_struct_lit);
                    r = ParseUnary();
                }
                if (auto c = Is<Call>(r)) {
                    c->args.insert(c->args.begin(), l);
                    l = r;
                } else {
                    auto call = New<Call>(line, r);
                    call->args.push_back(l);
                    l = Finish(call);
                }
                continue;
            }
            auto prec = BinPrec(lex.tok);
            if (prec < minprec || !prec) return l;
            auto op = lex.tok;
            auto line = CurLine();
            lex.Next();
            Node *r;
            {
                auto sub = Sub(no_struct_lit);
                r = ParseBinary(prec + 1);  
            }
            l = Finish(New<Binary>(line, op, l, r));
        }
    }

    Node *ParseUnary() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_MINUS: case T_NOT: case T_BITNOT: case T_BITAND: {
                auto op = lex.tok;
                Role(R_UNARY);
                lex.Next();
                auto sub = Sub(no_struct_lit);
                return Finish(New<Unary>(line, op, ParseUnary()));
            }
            default:
                return ParsePostfix();
        }
    }

    Node *ParsePostfix() {
        auto e = ParsePrimary();
        for (;;) {
            if (stmt_ended) return e;  
            auto line = CurLine();
            switch (lex.tok) {
                case T_LPAREN:
                    
                    Role(R_LIST);
                    lex.Next();
                    e = ParseCallRest(e, {}, e->line);
                    continue;
                case T_LBRACKET:
                    e = ParseIndexOrSlice(e, line);
                    continue;
                case T_DOT: {
                    lex.Next();
                    if (lex.tok != T_IDENT && !tentative) {
                        
                        
                        
                        
                        
                        
                        ast.Report(lex.Diagnose(cat("expected identifier in field access, found \'",
                                                    TokStr(), "\'"),
                                                lex.tokline, lex.toklinestart));
                        auto d = Finish(New<Dot>(line, e, string_view(), curns));
                        d->incomplete = true;
                        pendingdot = d;
                        if (curfn) curfn->broken = true;
                        return d;
                    }
                    auto name = ExpectIdent("field access");
                    if (lex.tok == T_LT) {
                        
                        
                        auto nameend = PrevEnd();
                        auto save = lex;
                        auto mark = ast.Mark();
                        auto rolemark = RoleMark();
                        vector<TypeExpr *> tyargs;
                        if (TryParseTyArgs(tyargs)) {
                            if (lex.tok == T_LPAREN) {
                                Role(R_LIST);
                                lex.Next();
                                auto d = Finish(New<Dot>(line, e, name, curns), nameend);
                                e = ParseCallRest(d, std::move(tyargs), d->line);
                                continue;
                            }
                            lex = save;
                            ast.DropSince(mark);
                            DropRoles(rolemark);
                        }
                    }
                    
                    
                    if (lex.tok == T_LCURLY && !no_struct_lit && Is<Ident>(e)) {
                        auto base = NewUnresolvedType(Is<Ident>(e)->name, e->line);
                        e = ParseStructLitBody(ast.VariantOf(base, name, nullptr, line), line);
                    } else {
                        e = Finish(New<Dot>(line, e, name, curns));
                    }
                    continue;
                }
                case T_AS: {
                    lex.Next();
                    if (lex.tok == T_NOT) Role(R_GLUE_LEFT);
                    auto unchecked = IsNext(T_NOT);
                    e = Finish(New<AsCast>(line, e, ParseType(), unchecked));
                    continue;
                }
                default:
                    return e;
            }
        }
    }

    
    Node *ParseCallRest(Node *callee, vector<TypeExpr *> tyargs, Line line) {
        auto c = New<Call>(line, callee);
        c->tyargs = std::move(tyargs);
        {
            auto sub = Sub();
            while (lex.tok != T_RPAREN) {
                c->args.push_back(ParseExpr());
                if (!IsNext(T_COMMA)) break;
            }
            Expect(T_RPAREN, "call arguments");
        }
        if (lex.tok == T_LCURLY && !no_struct_lit) {
            c->trailing = ParseFunVal();
            
            if (stmt_level) stmt_ended = true;
        }
        return Finish(c);
    }

    Node *ParseIndexOrSlice(Node *obj, Line line) {
        lex.Next();
        auto sub = Sub();
        Node *lo = nullptr;
        auto lo_from_end = false;
        auto isslice = lex.tok == T_DOTDOT;
        if (!isslice) {
            if (lex.tok == T_XOR) Role(R_UNARY);
            lo_from_end = IsNext(T_XOR);
            lo = ParseExpr();
            isslice = lex.tok == T_DOTDOT;
        }
        Node *result;
        if (isslice) {
            lex.Next();
            auto sl = New<SliceExpr>(line, obj);
            sl->lo = lo;
            sl->lo_from_end = lo_from_end;
            if (lex.tok != T_RBRACKET) {
                if (lex.tok == T_XOR) Role(R_UNARY);
                sl->hi_from_end = IsNext(T_XOR);
                sl->hi = ParseExpr();
            }
            result = sl;
        } else {
            if (lo_from_end) Error("\'^\' bounds are only valid in slices");
            result = New<Index>(line, obj, lo);
        }
        Expect(T_RBRACKET, "indexing");
        return Finish(result);
    }

    Node *ParseStrInterp(Line line) {
        auto c = New<Call>(line, New<Ident>(line, "str", curns));
        for (;;) {
            if (!lex.sval.empty()) c->args.push_back(Finish(New<StrLit>(line, lex.sval)));
            if (!lex.sinterp) {
                lex.Next();
                break;
            }
            lex.Next();
            if (lex.tok == T_STRLIT && !lex.sinterp && lex.interpop)
                Error("empty interpolation in \"${ }\"");
            c->args.push_back(ParseExpr());
            if (lex.tok != T_STRLIT)
                Error("expected the rest of the string after \"${\"");
        }
        return Finish(c);
    }

    Node *ParsePrimary() {
        auto line = CurLine();
        switch (lex.tok) {
            case T_INTLIT: {
                auto n = New<IntLit>(line, lex.ival, lex.attr, lex.iuns);
                lex.Next();
                return Finish(n);
            }
            case T_FLTLIT: {
                auto n = New<FltLit>(line, lex.fval, lex.attr);
                lex.Next();
                return Finish(n);
            }
            case T_STRLIT: {
                if (lex.sinterp) return ParseStrInterp(line);
                auto n = New<StrLit>(line, lex.sval, lex.smultiline);
                lex.Next();
                return Finish(n);
            }
            case T_TRUE:  lex.Next(); return Finish(New<BoolLit>(line, true));
            case T_FALSE: lex.Next(); return Finish(New<BoolLit>(line, false));
            case T_NULLLIT: lex.Next(); return Finish(New<NullLit>(line));
            case T_SELF:  lex.Next(); return Finish(New<SelfRef>(line));
            case T_LPAREN: {
                lex.Next();
                auto sub = Sub();
                auto e = ParseExpr();
                Expect(T_RPAREN, "parenthesized expression");
                return e;
            }
            case T_LBRACKET: return ParseArrayLit(line);
            case T_IDENT: case T_COLONCOLON: {
                if (constcond && lex.tok == T_IDENT && lex.PeekTok() == T_LT &&
                    (lex.attr == "has_default" || lex.attr == "same"))
                    return ParseConstQuery(line);
                auto name = ParseQualifiedName("expression");
                auto nameend = PrevEnd();
                if (lex.tok == T_LT) {
                    
                    
                    
                    vector<TypeExpr *> tyargs;
                    if (TryParseTyArgs(tyargs)) {
                        if (lex.tok == T_LPAREN) Role(R_LIST);
                        if (IsNext(T_LPAREN))
                            return ParseCallRest(Finish(New<Ident>(line, name, curns), nameend),
                                                 std::move(tyargs), line);
                        auto t = NewUnresolvedType(name, line);
                        t->named->args = std::move(tyargs);
                        if (lex.tok == T_DOT) {
                            auto dotline = CurLine();
                            lex.Next();
                            auto vname = ExpectIdent("variant literal");
                            if (lex.tok == T_LCURLY && !no_struct_lit)
                                return ParseStructLitBody(ast.VariantOf(t, vname, nullptr, line), line);
                            auto d = Finish(New<Dot>(dotline, Finish(New<Ident>(line, name, curns), nameend),
                                                     vname, curns));
                            d->enumtype = t;
                            return d;
                        }
                        return ParseStructLitBody(t, line);
                    }
                }
                if (lex.tok == T_LCURLY && !no_struct_lit) {
                    return ParseStructLitBody(NewUnresolvedType(name, line), line);
                }
                return Finish(New<Ident>(line, name, curns), nameend);
            }
            default:
                Error(cat("expression expected, found \'", TokStr(), "\'"));
        }
    }

    
    
    
    
    
    
    bool TryParseTyArgs(vector<TypeExpr *> &tyargs) {
        auto save = lex;
        auto mark = ast.Mark();
        auto rolemark = RoleMark();
        auto backtrack = [&]() {
            lex = save;
            tyargs.clear();
            ast.DropSince(mark);
            DropRoles(rolemark);
            return false;
        };
        Tentatively t(*this);
        Role(R_ANGLE_OPEN);
        lex.Next();  
        try {
            for (;;) {
                tyargs.push_back(ParseType());
                if (!IsNext(T_COMMA)) break;
                if (lex.tok == T_GT || lex.tok == T_SHR) break;
            }
            if (lex.tok == T_SHR) {
                Role(R_ANGLE_CLOSE);
                lex.tok = T_GT;
            } else if (lex.tok == T_GT) {
                Role(R_ANGLE_CLOSE);
                lex.Next();
            } else {
                return backtrack();
            }
            if (lex.tok == T_LPAREN || (lex.tok == T_LCURLY && !no_struct_lit)) return true;
            if (lex.tok == T_DOT) {
                
                auto save2 = lex;
                lex.Next();
                auto ident = lex.tok == T_IDENT;
                lex = save2;  
                if (ident) return true;
            }
        } catch (CompileError &) {}
        return backtrack();
    }

    Node *ParseArrayLit(Line line) {
        auto open = lex.attr.data();
        lex.Next();
        auto a = New<ArrayLit>(line);
        auto sub = Sub();
        if (lex.tok == T_DOTDOT) {
            
            
            lex.Next();
            a->capexpr = ParseExpr();
        } else if (lex.tok != T_RBRACKET) {
            auto first = ParseExpr();
            if (IsNext(T_SEMI)) {
                a->fillval = first;
                a->fillcount = ParseExpr();
            } else {
                RoleAt(open, R_LIST);
                a->elems.push_back(first);
                while (IsNext(T_COMMA) && lex.tok != T_RBRACKET)
                    a->elems.push_back(ParseExpr());
            }
        }
        Expect(T_RBRACKET, "array literal");
        return Finish(a);
    }

    ConstFor *ParseConstFor(bool initializers, bool arms = false) {
        auto cf = New<ConstFor>(CurLine());
        Expect(T_CONST, "const for");
        Expect(T_FOR, "const for");
        if (lex.tok == T_LT) Role(R_ANGLE_OPEN);
        Expect(T_LT, "const for bindings");
        do {
            auto name = ExpectIdent("const for binding");
            if (find(cf->names.begin(), cf->names.end(), name) != cf->names.end())
                Error(cat("duplicate const for binding: ", name));
            cf->names.push_back(name);
            if (!IsNext(T_COMMA)) break;
        } while (lex.tok != T_GT);
        ExpectClosingAngle("const for bindings");
        Expect(T_IN, "const for");
        if (lex.tok == T_LBRACKET) Role(R_LIST);
        if (IsNext(T_LBRACKET)) {
            cf->source = ES_CONSTANTS;
            auto sub = Sub();
            while (lex.tok != T_RBRACKET) {
                cf->constants.push_back(ParseExpr());
                if (!IsNext(T_COMMA)) break;
            }
            Expect(T_RBRACKET, "const for constant list");
        } else {
            auto query = ExpectIdent("const for source");
            if (query != "fields" && query != "types" && query != "variants")
                Error("const for source must be fields<T>, variants<E>, types<...>, or [constants]");
            cf->source = query == "fields" ? ES_FIELDS : query == "variants" ? ES_VARIANTS : ES_TYPES;
            if (lex.tok == T_LT) Role(R_ANGLE_OPEN);
            Expect(T_LT, "const for source");
            while (lex.tok != T_GT && lex.tok != T_SHR) {
                cf->types.push_back(ParseType());
                if (!IsNext(T_COMMA)) break;
            }
            ExpectClosingAngle("const for source");
            if (cf->source == ES_FIELDS && cf->types.size() != 1)
                Error("fields requires exactly one type");
            if (cf->source == ES_VARIANTS && cf->types.size() != 1)
                Error("variants requires exactly one type");
        }
        auto arity = cf->source == ES_FIELDS || cf->source == ES_VARIANTS ? 2u : 1u;
        if (cf->names.size() != arity)
            Error(cat("const for source requires exactly ", arity, " binding(s)"));
        if (initializers) {
            bool defaults = false;
            ParseInitList(cf->inits, defaults, true);
        } else if (arms) {
            cf->armfragments = true;
            auto open = lex.attr.data();
            Expect(T_LCURLY, "const for arms");
            ParseArms(cf->arms);
            if (ArmsTakeComma(cf->arms)) RoleAt(open, R_LIST);
            Expect(T_RCURLY, "const for arms");
        } else {
            cf->body = ParseBlockExpr("const for body");
        }
        return Finish(cf);
    }

    
    
    
    Node *ParseConstQuery(Line line) {
        auto kind = lex.attr == "same" ? Q_SAME : Q_HAS_DEFAULT;
        lex.Next();
        auto q = New<ConstQuery>(line, kind, string_view {});
        Role(R_ANGLE_OPEN);
        Expect(T_LT, "const if query");
        if (kind == Q_HAS_DEFAULT) {
            q->name = ExpectIdent("has_default");
        } else {
            q->types.push_back(ParseType());
            Expect(T_COMMA, "same, which takes two types");
            q->types.push_back(ParseType());
            if (lex.tok == T_COMMA) Error("same takes exactly two types");
        }
        ExpectClosingAngle("const if query");
        return Finish(q);
    }

    
    
    ConstIf *ParseConstIf(bool initializers, bool arms = false) {
        auto ci = New<ConstIf>(CurLine());
        ci->armfragments = arms;
        Expect(T_CONST, "const if");
        auto conditional = true;
        for (;;) {
            ConstIfBranch br;
            if (conditional) {
                Expect(T_IF, "const if");
                CondScope cs(*this);
                br.cond = ParseScrutinee();
            }
            if (initializers) {
                bool defaults = false;
                ParseInitList(br.inits, defaults, true);
            } else if (arms) {
                auto open = lex.attr.data();
                Expect(T_LCURLY, "const if arms");
                ParseArms(br.arms);
                if (ArmsTakeComma(br.arms)) RoleAt(open, R_LIST);
                Expect(T_RCURLY, "const if arms");
            } else {
                br.body = ParseBlockExpr("const if body");
            }
            ci->branches.push_back(br);
            if (!conditional || !IsNext(T_ELSE)) break;
            
            
            if (lex.tok == T_IF)
                Error("else if after const if: write else const if for a compile-time "
                      "condition, or else { if ... } for a runtime one");
            conditional = IsNext(T_CONST);
        }
        return Finish(ci);
    }

    void ParseInitList(vector<FieldInit> &inits, bool &defaultall, bool fragment = false) {
        if (lex.tok == T_LCURLY) Role(R_LIST);
        Expect(T_LCURLY, "constructor initializers");
        auto sub = Sub();
        while (lex.tok != T_RCURLY) {
            
            if (IsNext(T_DOTDOT)) {
                if (fragment) Error("trailing .. belongs to the whole constructor, not a const for fragment");
                defaultall = true;
                break;
            }
            FieldInit fi;
            if (lex.tok == T_CONST && (lex.PeekTok() == T_FOR || lex.PeekTok() == T_IF)) {
                fi.val = lex.PeekTok() == T_FOR ? (Node *)ParseConstFor(true) : ParseConstIf(true);
                inits.push_back(fi);
                if (!IsNext(T_COMMA)) break;
                continue;
            }
            if (lex.tok == T_IDENT) {
                
                auto save2 = lex;
                auto name = lex.attr;
                lex.Next();
                if (IsNext(T_COLON)) fi.name = name; else lex = save2;
            }
            fi.val = ParseExpr();
            inits.push_back(fi);
            if (!IsNext(T_COMMA)) break;
        }
        Expect(T_RCURLY, "struct literal");
    }

    StructLit *ParseStructLitBody(TypeExpr *type, Line line) {
        auto sl = New<StructLit>(line, type);
        ParseInitList(sl->inits, sl->defaultall);
        Finish(sl);
        auto named = 0;
        for (auto &fi : sl->inits) {
            
            if (Is<ConstFor>(fi.val) || Is<ConstIf>(fi.val)) return sl;
            named += !fi.name.empty();
        }
        if (named && named != (int)sl->inits.size())
            Error("struct literal mixes named and positional initializers");
        return sl;
    }

    FunVal *ParseFunVal() {
        auto line = CurLine();
        if (lex.tok == T_LCURLY) Role(R_FUNVAL);
        Expect(T_LCURLY, "function value");
        auto b = New<Block>(line);
        auto fv = New<FunVal>(line, b);
        if (lex.tok == T_IDENT || lex.tok == T_VAR) {
            
            
            auto save = lex;
            auto rolemark = RoleMark();
            vector<Param> params;
            auto ok = true;
            
            
            
            optional<Lexer> dup;
            try {
                Tentatively t(*this);
                for (;;) {
                    Param p;
                    p.isvar = IsNext(T_VAR);
                    if (lex.tok != T_IDENT) { ok = false; break; }
                    p.line = CurLine();
                    p.name = lex.attr;
                    for (auto &prev : params)
                        if (prev.name == p.name && !dup) dup = lex;
                    lex.Next();
                    if (IsNext(T_COLON)) p.type = ParseType();
                    params.push_back(p);
                    if (!IsNext(T_COMMA)) break;
                }
                if (!ok || !IsNext(T_FATARROW)) ok = false;
            } catch (CompileError &) { ok = false; }
            if (ok) {
                if (dup) dup->Error(cat("duplicate parameter name: ", dup->attr));
                fv->params = std::move(params);
                fv->explicit_params = true;
            } else {
                lex = save;
                DropRoles(rolemark);
            }
        }
        ParseBlockBody(b);
        Finish(b);
        return Finish(fv);
    }

    
    

    Block *ParseBlockExpr(const char *context) {
        auto line = CurLine();
        Expect(T_LCURLY, context);
        auto b = New<Block>(line);
        ParseBlockBody(b);
        return Finish(b);
    }

    
    
    
    
    
    void ParseBlockBody(Block *b) {
        auto ctx = Sub();
        for (;;) {
            stmt_level = false;
            stmt_ended = false;
            if (IsNext(T_RCURLY)) break;
            if (lex.tok == T_EOF && eoferror) return;
            auto nfunctions = ast.functions.size();
            auto startp = lex.attr.data();
            auto stmtdepth = lex.DepthBefore();
            pendingdot = nullptr;
            try {
                if (!ParseStatement(b)) return;
            } catch (CompileError &e) {
                if (tentative) throw;
                
                
                
                if (pendingdot) {
                    b->stmts.push_back(pendingdot);
                } else {
                    Report(e);
                    for (auto i = nfunctions; i < ast.functions.size(); i++) delete ast.functions[i];
                    ast.functions.resize(nfunctions);
                }
                pendingdot = nullptr;
                if (b->brokenat < 0) b->brokenat = (int)b->stmts.size();
                if (curfn) curfn->broken = true;
                if (Resync(false, startp, stmtdepth)) return;
            }
        }
    }

    
    
    bool ParseStatement(Block *b) {
        if (AtInline() || AtCheckfree()) goto nestedfndecl;
        switch (lex.tok) {
            case T_LET: case T_VAR: case T_CONST: case T_REUSABLE: {
                if (lex.tok == T_CONST && lex.PeekTok() == T_FOR) {
                    b->stmts.push_back(ParseConstFor(false));
                    return true;
                }
                if (lex.tok == T_CONST && lex.PeekTok() == T_IF) {
                    b->stmts.push_back(ParseConstIf(false));
                    return true;
                }
                auto vd = ParseVarDecl(false);
                Expect(T_SEMI, "variable declaration");
                b->stmts.push_back(vd);
                return true;
            }
            case T_RECURSIVE: case T_FN: case T_THREADFN: case T_EXTERN: case T_EXPORT:
            case T_SIMD: nestedfndecl:
                b->stmts.push_back(ParseFnDecl(true));
                return true;
            case T_GUARD:
                ParseGuard(b);
                return false;
            case T_DEFER:
                b->stmts.push_back(ParseDefer());
                return true;
            default: break;
        }
        return ParseExprStatement(b, true);
    }

    
    
    
    
    
    bool ParseExprStatement(Block *b, bool tailok) {
        stmt_level = true;
        auto e = ParseExpr();
        stmt_level = false;
        auto line = CurLine();
        if (!stmt_ended) {
            switch (lex.tok) {
                case T_ASSIGN: case T_DOTASSIGN: case T_PLUSEQ: case T_MINUSEQ:
                case T_MULEQ: case T_DIVEQ: case T_MODEQ: case T_ANDEQ: case T_OREQ:
                case T_XOREQ: case T_SHLEQ: case T_SHREQ: {
                    auto op = lex.tok;
                    lex.Next();
                    auto rhs = ParseExpr();
                    Expect(T_SEMI, "assignment statement");
                    b->stmts.push_back(Finish(New<Assign>(line, op, e, rhs)));
                    return true;
                }
                case T_INC: case T_DEC: {
                    auto op = lex.tok;
                    lex.Next();
                    Expect(T_SEMI, "increment statement");
                    b->stmts.push_back(Finish(New<IncDec>(line, op, e)));
                    return true;
                }
                case T_COMMA:
                    b->stmts.push_back(ParseMultiAssign(e, line));
                    return true;
                default: break;
            }
        }
        if (IsNext(T_SEMI)) {  
            b->stmts.push_back(e);
            return true;
        }
        if (tailok && IsNext(T_RCURLY)) {
            b->tail = e;  
            return false;
        }
        
        if (stmt_ended) {
            b->stmts.push_back(e);
            return true;
        }
        Error(cat("\';\' expected after expression, found \'", TokStr(), "\'"));
    }

    
    
    
    
    Node *ParseDefer() {
        auto line = CurLine();
        lex.Next();
        Block *body;
        if (lex.tok == T_LCURLY) {
            body = ParseBlockExpr("defer body");
            IsNext(T_SEMI);  
        } else {
            switch (lex.tok) {
                case T_LET: case T_VAR: case T_CONST: case T_REUSABLE: case T_GUARD: case T_DEFER:
                case T_FN: case T_RECURSIVE: case T_THREADFN: case T_EXTERN: case T_EXPORT:
                case T_SIMD:
                    Error(cat("defer takes a statement or a block, not `", TokStr(), "`"));
                default: break;
            }
            body = New<Block>(line);
            stmt_ended = false;
            ParseExprStatement(body, false);
            Finish(body);
        }
        return Finish(New<Defer>(line, body));
    }

    
    
    
    
    
    
    Node *ParseMultiAssign(Node *first, Line line) {
        vector<Node *> targets = { first };
        while (IsNext(T_COMMA)) targets.push_back(ParseExpr());
        switch (lex.tok) {
            case T_DOTASSIGN: case T_PLUSEQ: case T_MINUSEQ: case T_MULEQ: case T_DIVEQ:
            case T_MODEQ: case T_ANDEQ: case T_OREQ: case T_XOREQ: case T_SHLEQ: case T_SHREQ:
                Error(cat("several targets take a call's results with `=` alone, not `",
                          TokStr(), "` (§7.1)"));
            default: break;
        }
        if (lex.tok != T_ASSIGN)
            Error("several values exist only in `return a, b;` and in a call's results,"
                  " so a block or branch cannot end in `a, b`: return from each branch"
                  " instead (§7.1)");
        lex.Next();
        auto rhs = ParseExpr();
        Expect(T_SEMI, "assignment statement");
        auto b = Finish(New<Block>(line));
        auto vd = Finish(New<VarDecl>(line, false));
        vd->results = true;
        for (size_t i = 0; i < targets.size(); i++)
            vd->names.push_back(ast.Intern(cat("result#", i + 1)));
        vd->inits.push_back(rhs);
        b->stmts.push_back(vd);
        for (size_t i = 0; i < targets.size(); i++)
            b->stmts.push_back(Finish(New<Assign>(line, T_ASSIGN, targets[i],
                                                  Finish(New<Ident>(line, vd->names[i])))));
        return b;
    }

    
    
    
    
    
    void ParseGuard(Block *b) {
        auto line = CurLine();
        lex.Next();
        auto cond = ParseScrutinee();
        Block *elseb = nullptr;
        if (IsNext(T_ELSE)) {
            elseb = ParseBlockExpr("guard else");
            IsNext(T_SEMI);  
        } else {
            Expect(T_SEMI, "guard");
        }
        auto rest = New<Block>(line);
        ParseBlockBody(rest);  
        Finish(rest);
        auto ife = Finish(New<IfExpr>(line, cond, rest, elseb));
        if (rest->tail) b->tail = ife; else b->stmts.push_back(ife);
    }
};

}  
