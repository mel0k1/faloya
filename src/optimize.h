




















































#pragma once

namespace faloya {

struct BaseCaseInliner;   
struct Reassociator;      

struct Optimizer {
    Ast &ast;
    int nc = 0, ncu = 0;         
    bool caninline = false;
    FnSpec *curspec = nullptr;   
    int inlined = 0, folded = 0, propagated = 0, tailloops = 0;

    
    
    
    struct VarFacts { int writes = 0; int addrof = 0; };
    unordered_map<VarDef *, VarFacts> facts;

    
    
    
    unordered_map<VarDef *, Node *> consts;

    vector<FnSpec *> postorder;  

    
    
    
    
    struct InlineInfo {
        int nodecount = 0;
        int nest = 0;
        bool noinline = false;
        
        
        
        bool c_inlinable = false;
        bool nestedcalls = false;
    };
    unordered_map<FnSpec *, InlineInfo> inlineinfo;

    
    int depth = 0;
    
    
    
    
    
    
    int loopdepth = 0;
    static constexpr int LOOPNCU = 4;
    
    
    
    
    
    static constexpr int ONCEMAX = 200;
    
    
    
    
    int colddepth = 0;
    
    
    int branchdepth = 0;
    
    
    
    bool deferonce = false;
    vector<FnSpec *> deferred;

    
    
    static bool Jumps(Node *st) {
        if (Is<Return>(st) || Is<Break>(st) || Is<Continue>(st)) return true;
        auto c = Is<Call>(st);
        return c && (c->builtin == B_ABORT || c->builtin == B_EXIT);
    }
    
    
    static bool Leaves(Node *b) {
        auto blk = Is<Block>(b);
        if (!blk || blk->tail || blk->stmts.empty()) return false;
        auto last = blk->stmts.back();
        return Jumps(last) && !Is<Continue>(last);
    }
    
    
    
    static constexpr int MAXNEST = 64;

    
    

    static bool ScalarType(TypeExpr *t) {
        return t && (t->kind == TY_BOOL || t->kind == TY_FLT ||
                     (t->kind == TY_INT && t->intstorage != IS_VARINT));
    }

    
    
    static bool Unwritten(unordered_map<VarDef *, VarFacts> &facts, VarDef *d) {
        if (!ScalarType(d->type)) return false;
        auto &f = facts[d];
        return f.writes == 0 && f.addrof == 0;
    }

    static Node *AsLiteral(Node *n) {
        return Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) ? n : nullptr;
    }

    
    
    static bool SameType(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (!a || !b || a->kind != b->kind) return false;
        switch (a->kind) {
            case TY_INT:  return a->intstorage == b->intstorage;
            case TY_FLT:  return a->fltstorage == b->fltstorage;
            case TY_BOOL: case TY_VOID: return true;
            default: return false;
        }
    }

    
    
    static TypeExpr *IntTypeOf(Node *n) {
        auto t = n->exprtype;
        return t && t->kind == TY_INT && t->intstorage != IS_VARINT ? t : nullptr;
    }

    Node *NewInt(Node *at, int64_t v) {
        auto t = at->exprtype;
        auto r = ast.New<IntLit>(at->line, v, string_view {},
                                 v < 0 && t && t->kind == TY_INT && t->intstorage == IS_U64);
        r->exprtype = t;
        folded++;
        return r;
    }
    Node *NewFlt(Node *at, double v) {
        auto r = ast.New<FltLit>(at->line, v);
        r->exprtype = at->exprtype;
        folded++;
        return r;
    }
    Node *NewBool(Node *at, bool v) {
        auto r = ast.New<BoolLit>(at->line, v);
        r->exprtype = at->exprtype;
        folded++;
        return r;
    }
    Block *EmptyBlock(Node *at) {
        auto b = ast.New<Block>(at->line);
        b->exprtype = ast.voidtype;
        return b;
    }

    Node *CloneLit(Node *lit, TypeExpr *usetype) {
        Node *r;
        
        
        auto it = usetype && usetype->kind == TY_INT ? usetype->intstorage : IS_I64;
        if (auto i = Is<IntLit>(lit); i && it != IS_VARINT && !i->uns) {
            auto v = WrapStorage(i->val, it);
            r = ast.New<IntLit>(lit->line, v, v == i->val ? i->text : string_view {},
                                it == IS_U64 && v < 0);
        } else if (i) r = ast.New<IntLit>(lit->line, i->val, i->text, i->uns);
        else if (auto f = Is<FltLit>(lit)) {
            
            auto v = IsF32(lit->exprtype) ? (double)(float)f->val : f->val;
            r = ast.New<FltLit>(lit->line, v, f->text);
        } else r = ast.New<BoolLit>(lit->line, ((BoolLit *)lit)->val);
        r->exprtype = usetype ? usetype : lit->exprtype;
        return r;
    }

    
    
    

    void Reach(FnSpec *sp) {
        if (!sp || sp->live) return;
        sp->live = true;
        if (!sp->body) return;   
        ReachTree(sp->body);
        postorder.push_back(sp);
    }

    
    
    void ReachRoots() {
        auto mainsf = ast.MainFunction();
        if (mainsf && !mainsf->specs.empty()) Reach(mainsf->specs[0]);
        for (auto sf : ast.functions)
            if ((sf->isthread || sf->isexport) && !sf->specs.empty()) Reach(sf->specs[0]);
        ast.ForEachRootTree([&](Node *n) { ReachTree(n); });
    }

    void ReachTree(Node *n) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (c->spec) {
                if (c->builtin < 0) c->spec->uses++;  
                Reach(c->spec);
            }
            for (auto &fs : c->fmtspecs) {   
                fs.second->uses++;
                Reach(fs.second);
            }
            for (auto d : c->dispatch) { d->uses++; Reach(d); }
        }
        RunChildren(n, [&](Node *ch) { ReachTree(ch); });
    }

    
    
    
    

    void Analyze(Node *n) {
        if (!n) return;
        if (auto a = Is<Assign>(n)) {
            if (auto id = Is<Ident>(a->lval)) if (id->vdef) facts[id->vdef].writes++;
        } else if (auto x = Is<IncDec>(n)) {
            if (auto id = Is<Ident>(x->lval)) if (id->vdef) facts[id->vdef].writes++;
        } else if (auto u = Is<Unary>(n)) {
            if (u->op == T_BITAND)
                if (auto id = Is<Ident>(u->child)) if (id->vdef) facts[id->vdef].addrof++;
        } else if (auto c = Is<Call>(n)) {
            
            
            auto an = c->ArgNodes();
            for (size_t k = 0; k < an.size(); k++)
                if (auto id = Is<Ident>(an[k]); id && id->vdef && HookedByRef(c, k))
                    facts[id->vdef].addrof++;
        }
        RunChildren(n, [&](Node *ch) { Analyze(ch); });
    }

    
    

    Node *TryInline(Call *c);   

    
    
    
    
    
    
    
    
    
    
    static int Around(Node *n, Node *ch) {
        if (auto fi = Is<IfExpr>(n)) {
            if (ch == fi->thenb) return fi->flat ? -1 : 0;
            return ch == fi->elseb && !Is<Block>(ch) ? 1 : 0;
        }
        if (auto m = Is<MatchExpr>(n)) return ch != m->scrutinee ? 2 : 0;
        if (auto b = Is<Binary>(n))
            return ch == b->right && (b->op == T_ANDAND || b->op == T_OROR) ? 1 : 0;
        if (auto w = Is<While>(n)) return ch == w->cond ? 1 : 0;
        if (auto c = Is<Call>(n)) return c->fvbody && ch != c->callee && ch != c->fvbody ? 1 : 0;
        if (auto al = Is<ArrayLit>(n)) return ch == al->fillval ? 1 : 0;
        return 0;
    }

    
    
    
    unique_ptr<BaseCaseInliner> basecase;
    void SetupBaseCase(FnSpec *sp);   
    Node *TryBaseCase(Call *c);       
    int basecases = 0;

    
    
    
    
    unique_ptr<Reassociator> reassoc;
    Node *TryReassoc(Call *c);
    vector<Node *> loopnodes;
    int reassociated = 0;

    
    
    

    Node *Opt(Node *n) {
        assert(n);
        return n->Opt(*this);
    }

    
    Node *OptIn(Node *n, Node *ch) {
        auto k = Around(n, ch);
        depth += k;
        ch = Opt(ch);
        depth -= k;
        return ch;
    }

    
    
    Node *viewed = nullptr;

    
    
    
    bool Viewed(Node *n) {
        return viewed == n || (n->exprtype && n->exprtype->kind == TY_SLICE);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    Node *OptViewed(Node *n, bool anytype = false) {
        auto outer = viewed;
        viewed = n;
        auto r = Opt(n);
        viewed = outer;
        if (r == n || !(anytype ? IsPath(r) : NamesStorage(r))) return r;
        auto b = ast.New<Block>(r->line);
        b->tail = r;
        b->exprtype = r->exprtype;
        return b;
    }

    
    
    
    
    
    
    
    Node *OptRendered(Node *n) { return OptViewed(n, true); }

    
    
    static bool Hooked(Call *c, size_t k) {
        size_t first = c->builtin == B_FORMAT;   
        return k >= first && k - first < c->fmtcontexts.size() &&
               !c->fmtcontexts[k - first]->fmtspecs.empty();
    }

    
    static bool HookedByRef(Call *c, size_t k) {
        if (!Hooked(c, k)) return false;
        for (auto &fs : c->fmtcontexts[k - (c->builtin == B_FORMAT)]->fmtspecs)
            if (fs.second->argtypes[1]->kind == TY_REF) return true;
        return false;
    }

    
    
    
    static bool IsPath(Node *n) {
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND && u->child->exprtype &&
                                   u->child->exprtype->kind != TY_REF)
            n = u->child;
        auto d = Is<Dot>(n);
        return Is<Ident>(n) || (d && d->IsField()) || Is<Index>(n);
    }

    
    static bool NamesStorage(Node *n) {
        if (!IsPath(n)) return false;
        auto t = n->exprtype;
        return t && (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_ENUM ||
                     t->kind == TY_VARIANT);
    }

    
    
    static bool CodeFree(Node *n) {
        if (AsLiteral(n) || Is<Ident>(n)) return true;
        if (auto u = Is<Unary>(n)) return CodeFree(u->child);
        if (auto b = Is<Binary>(n)) return CodeFree(b->left) && CodeFree(b->right);
        if (auto c = Is<AsCast>(n)) return CodeFree(c->child);
        if (auto d = Is<Dot>(n)) return CodeFree(d->obj);
        if (auto ix = Is<Index>(n)) return CodeFree(ix->obj) && CodeFree(ix->idx);
        return false;
    }

    void OptBlock(Block *b) {
        depth++;
        vector<Node *> out;
        auto dead = false;
        for (auto st : b->stmts) {
            if (dead) break;  
            auto r = OptStmt(st);
            if (!r) continue;
            out.push_back(r);
            
            
            if (Jumps(r)) dead = true;
        }
        b->stmts = std::move(out);
        if (b->tail) {
            if (dead) {
                
                
                b->tail = nullptr;
                b->exprtype = ast.voidtype;
            } else {
                b->tail = Opt(b->tail);
            }
        }
        depth--;
    }

    Node *OptStmt(Node *n) {
        if (auto vd = Is<VarDecl>(n)) {
            for (auto &i : vd->inits) i = Opt(i);
            if (vd->names.size() == 1 && vd->inits.size() == 1 && vd->defs.size() == 1) {
                auto d = vd->defs[0];
                auto lit = AsLiteral(vd->inits[0]);
                if (lit && Unwritten(facts, d)) {
                    consts[d] = lit;
                    
                    
                    if (!d->captured) return nullptr;
                }
            }
            return n;
        }
        auto r = Opt(n);
        if (auto b = Is<Block>(r); b && b->stmts.empty() && !b->tail) return nullptr;
        return r;
    }

    
    
    

    void Scan(FnSpec *sp) {
        auto &info = inlineinfo[sp];
        info.nodecount = 0;
        info.nest = 0;
        info.nestedcalls = false;
        
        auto noin = sp->sf->isrec || sp->incycle || sp->sf->isthread || sp->sf->isexport ||
                    sp->sf->issimd || sp->relnamedresult;
        auto multiret = sp->rets.size() > 1;
        function<void(Node *, int)> rec = [&](Node *n, int d) {
            if (!n) return;
            info.nodecount++;
            if (Is<Block>(n)) info.nest = max(info.nest, ++d);   
            if (auto c = Is<Call>(n)) {
                auto callee = [&](FnSpec *k) {
                    if (!k) return;
                    
                    
                    
                    
                    if (!k->fnvals.empty()) noin = true;
                    if (k->needs.count(sp)) noin = true;
                    if (k->lexparent) info.nestedcalls = true;
                };
                if (c->builtin < 0) callee(c->spec);
                for (auto k : c->dispatch) callee(k);
            }
            RunChildren(n, [&](Node *ch) { rec(ch, d + Around(n, ch)); });
        };
        rec(sp->body, 0);
        info.noinline = noin || multiret;
        info.c_inlinable = multiret && !noin;
    }

    
    
    
    
    
    
    
    
    
    
    
    void OptBody(FnSpec *sp) {
        deferonce = sp->uses > 1;
        deferred.clear();
        OptBlock(sp->body);
        deferonce = false;
        if (deferred.empty()) return;
        Scan(sp);
        auto &info = inlineinfo[sp];
        auto maxsize = max(nc - 1, (ncu * LOOPNCU - 1) / sp->uses);
        auto inlinable = !info.noinline && info.nodecount <= maxsize;
        auto size = info.nodecount;
        auto any = false;
        for (auto K : deferred) {
            auto ksize = inlineinfo[K].nodecount;
            if (!inlinable || size + ksize <= maxsize) {
                size += ksize;
                any = true;
            } else {
                K->uses = sp->uses;
            }
        }
        
        
        if (any) OptBlock(sp->body);
    }

    
    void TailRecurse(FnSpec *sp);

    
    
    void ShapeLoops();

    
    
    

    void OptGlobal(VarDecl *g) {
        depth = 1;   
        for (auto &i : g->inits) i = Opt(i);
        depth = 0;
        if (g->names.size() == 1 && g->inits.size() == 1 && !g->defs.empty()) {
            auto d = g->defs[0];
            auto lit = AsLiteral(g->inits[0]);
            if (lit && Unwritten(facts, d))
                consts[d] = lit;  
        }
    }

    
    

    Optimizer(Ast &_ast, int level) : ast(_ast) {
        switch (level) {
            case 0:  nc = 0;  ncu = 0;  break;
            case 1:  nc = 8;  ncu = 48; break;
            default: nc = 16; ncu = 96; break;
        }
        
        ReachRoots();
        
        for (auto sp : postorder) if (sp->body) Analyze(sp->body);
        ast.ForEachRootTree([&](Node *n) { Analyze(n); });
        
        
        caninline = false;
        for (auto g : ast.globals) OptGlobal(g);
        
        caninline = nc > 0;
        for (auto sp : postorder) {
            curspec = sp;
            SetupBaseCase(sp);
            OptBody(sp);
            TailRecurse(sp);
            Scan(sp);
        }
        curspec = nullptr;
        
        for (auto g : ast.globals) OptGlobal(g);
        
        
        for (auto sp : ast.fnspecs) { sp->live = false; sp->uses = 0; }
        postorder.clear();
        ReachRoots();
        ShapeLoops();
    }

    
    void DumpSpecs(string &s) {
        for (auto sp : ast.fnspecs) {
            if (!sp->live) continue;
            auto &info = inlineinfo[sp];
            Append(s, "// spec ", sp->id, ": uses ", sp->uses, ", nodes ", info.nodecount,
                   ", nest ", info.nest, info.noinline ? ", noinline" : "", "\n");
            Append(s, "fn ", sp->sf->qname, "(");
            for (size_t i = 0; i < sp->params.size(); i++) {
                if (i) s += ", ";
                Append(s, sp->params[i]->name, ": ");
                sp->params[i]->type->Dump(s);
            }
            s += ") ";
            if (sp->body) sp->body->Dump(s, 0);
            else s += ";";
            s += "\n\n";
        }
    }
};








struct Inliner {
    Optimizer &o;
    Ast &ast;
    FnSpec *src;             
    FnSpec *dst;             
    unordered_map<VarDef *, VarDef *> vmap;
    unordered_map<VarDef *, Node *> subst;   
    
    
    
    bool keepcaptured = false;
    
    
    unordered_map<VarDef *, VarDef *> outer;
    
    
    vector<pair<const Call *, Call *>> nestedcopies;
    int cpdepth = 0;
    
    
    
    
    
    
    bool atsite = false;
    Line site;

    void AtCallSite(const Call *c) {
        atsite = ast.StdlibFile(src->sf->line.fileidx) && !ast.StdlibFile(c->line.fileidx) &&
                 !(dst && dst->sf->checkfree);
        site = c->line;
    }

    VarDef *Remap(VarDef *v) {
        if (!v) return v;
        if (v->ownerspec != src) {
            auto it = outer.find(v);
            return it != outer.end() ? it->second : v;
        }
        auto it = vmap.find(v);
        if (it != vmap.end()) return it->second;
        auto nv = ast.NewVarDef();
        *nv = *v;
        nv->ownerspec = dst;
        nv->isparam = false;
        
        nv->captured = keepcaptured && v->captured;
        vmap[v] = nv;
        nv->componentowner = Remap(v->componentowner);
        for (auto &c : nv->components) c = Remap(c);
        auto fit = o.facts.find(v);
        if (fit != o.facts.end()) o.facts[nv] = fit->second;
        return nv;
    }

    
    
    
    void RemapFreeVars(const Call *from, Call *to) {
        auto add = [&](VarDef *v, VarDef *nv) {
            if (v == nv) return;
            for (auto &p : to->fvremap) if (p.first == v) return;
            to->fvremap.push_back({ v, nv });
        };
        for (auto &p : from->fvremap) add(p.first, Remap(p.second));
        for (auto &kv : vmap) if (kv.first->captured) add(kv.first, kv.second);
        for (auto &kv : outer) add(kv.first, kv.second);
    }

    
    
    
    VarDecl *BindArg(VarDef *pv, Node *arg, Line ln) {
        if (Optimizer::AsLiteral(arg) && Optimizer::Unwritten(o.facts, pv) &&
            !(keepcaptured && pv->captured)) {
            subst[pv] = arg;
            return nullptr;
        }
        auto vd = ast.New<VarDecl>(ln, pv->isvar);
        vd->names.push_back(pv->name);
        vd->defs.push_back(Remap(pv));
        vd->inits.push_back(arg);
        vd->exprtype = ast.voidtype;
        return vd;
    }

    Node *Cp(const Node *n) {
        cpdepth++;
        auto r = n->Cp1(*this);
        r->end = n->end;
        if (atsite && ast.StdlibFile(r->line.fileidx)) {
            r->line = site;
            r->end = Line {};
        }
        r->exprtype = n->exprtype;
        r->expansiontrace = n->expansiontrace;
        if (!--cpdepth) Finish();
        return r;
    }

    Block *CpBlock(const Block *b) { return (Block *)Cp(b); }

    
    
    
    
    
    void Finish() {
        for (auto [from, to] : nestedcopies) RemapFreeVars(from, to);
        nestedcopies.clear();
        RemapProvenance();
    }

    
    
    
    
    
    void RemapProvenance() {
        auto remap = [&](VarDef *&r) {
            auto it = r ? vmap.find(r) : vmap.end();
            if (it != vmap.end()) r = it->second;
        };
        for (auto &kv : vmap) {
            auto nv = kv.second;
            if (nv == kv.first) continue;
            for (auto &a : nv->ref.alts) {
                remap(a.root);
                remap(a.from);
            }
            for (auto &a : nv->contents.alts) {
                remap(a.root);
                remap(a.from);
            }
        }
    }
};

inline Node *Optimizer::TryInline(Call *c) {
    if (!caninline || !c->spec || c->builtin >= 0 || !c->dispatch.empty()) return nullptr;
    auto K = c->spec;
    auto &info = inlineinfo[K];
    if (!K->live || !K->body) return nullptr;
    auto ncuhere = loopdepth > 0 && !colddepth ? ncu * LOOPNCU : ncu;
    
    auto small = (K->sf->isinline && nc > 0) || info.nodecount < nc || info.nodecount * K->uses < ncuhere;
    if (info.noinline) {
        
        if (info.c_inlinable && small && !K->outofline) K->cinline = true;
        return nullptr;
    }
    
    
    if (curspec && (curspec->incycle || curspec->sf->isrec)) return nullptr;
    if (!small && K->uses == 1 && colddepth > 0) {
        K->outofline = true;
        return nullptr;
    }
    if (!((K->uses == 1 && info.nodecount <= ONCEMAX) || small)) return nullptr;
    
    
    
    if (depth + info.nest > MAXNEST) return nullptr;
    
    
    if (info.nestedcalls && !curspec) return nullptr;
    auto argnodes = c->ValueArgNodes();
    if (argnodes.size() != K->params.size()) return nullptr;
    if (!small && deferonce && branchdepth > 0 && loopdepth == 0) {
        deferred.push_back(K);
        return nullptr;
    }
    Inliner inl { *this, ast, K, curspec, {}, {} };
    inl.keepcaptured = info.nestedcalls;
    inl.AtCallSite(c);
    for (auto &p : c->fvremap) inl.outer[p.first] = p.second;
    vector<Node *> decls;
    for (size_t i = 0; i < K->params.size(); i++) {
        if (auto vd = inl.BindArg(K->params[i], argnodes[i], c->line)) {
            vd->inline_arg = true;
            decls.push_back(vd);
        }
    }
    auto body = inl.CpBlock(K->body);
    body->stmts.insert(body->stmts.begin(), decls.begin(), decls.end());
    auto ib = ast.New<InlineBlock>(c->line, K->sf, K, body);
    ib->exprtype = c->exprtype;
    inlined++;
    
    
    OptBlock(body);
    
    
    
    
    auto unwrapok = [&](Node *v) {
        if (SameType(v->exprtype, c->exprtype)) return true;
        auto a = v->exprtype, b = c->exprtype;
        return a && b && a->kind == b->kind && a->kind != TY_REF && a->kind != TY_ENUM;
    };
    if (body->stmts.empty() && body->tail && !ReturnsTo(body->tail, K->sf) &&
        unwrapok(body->tail))
        return body->tail;
    if (body->stmts.empty() && !body->tail) return EmptyBlock(c);
    if (body->stmts.size() == 1 && !body->tail) {
        if (auto r = Is<Return>(body->stmts[0]); r && r->target == K->sf) {
            if (r->vals.empty()) return EmptyBlock(c);
            if (r->vals.size() == 1 && !ReturnsTo(r->vals[0], K->sf) &&
                unwrapok(r->vals[0]))
                return r->vals[0];
        }
    }
    return ib;
}






inline Node *IntLit::Cp1(Inliner &inl) const { return Clone1(inl.ast); }
inline Node *FltLit::Cp1(Inliner &inl) const { return Clone1(inl.ast); }


inline Node *ConstFor::Cp1(Inliner &) const { assert(false); return nullptr; }
inline Node *ConstFor::Opt(Optimizer &) { assert(false); return nullptr; }
inline Node *ConstIf::Cp1(Inliner &) const { assert(false); return nullptr; }
inline Node *ConstIf::Opt(Optimizer &) { assert(false); return nullptr; }
inline Node *ConstQuery::Cp1(Inliner &) const { assert(false); return nullptr; }
inline Node *ConstQuery::Opt(Optimizer &) { assert(false); return nullptr; }
inline Node *BoolLit::Cp1(Inliner &inl) const { return inl.ast.New<BoolLit>(line, val); }
inline Node *StrLit::Cp1(Inliner &inl) const {
    return inl.ast.New<StrLit>(line, val, multiline);
}
inline Node *NullLit::Cp1(Inliner &inl) const { return inl.ast.New<NullLit>(line); }
inline Node *SelfRef::Cp1(Inliner &inl) const { return inl.ast.New<SelfRef>(line); }
inline Node *Continue::Cp1(Inliner &inl) const { return inl.ast.New<Continue>(line); }
inline Node *Defer::Cp1(Inliner &inl) const { return inl.ast.New<Defer>(line, inl.CpBlock(body)); }

inline Node *Ident::Cp1(Inliner &inl) const {
    if (vdef) {
        
        auto it = inl.subst.find(vdef);
        if (it != inl.subst.end()) {
            inl.o.propagated++;
            return inl.o.CloneLit(it->second, exprtype);
        }
    }
    auto c = inl.ast.New<Ident>(line, name, ns);
    c->vdef = inl.Remap(vdef);
    c->fnref = fnref;
    return c;
}

inline Node *ArrayLit::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<ArrayLit>(line);
    for (auto e : elems) c->elems.push_back(inl.Cp(e));
    if (fillval) c->fillval = inl.Cp(fillval);
    if (fillcount) c->fillcount = inl.Cp(fillcount);
    if (capexpr) c->capexpr = inl.Cp(capexpr);
    return c;
}

inline Node *StructLit::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<StructLit>(line, type);
    for (auto &fi : inits) c->inits.push_back({ fi.name, inl.Cp(fi.val), fi.fromdefault });
    c->defaultall = defaultall;
    c->sinst = sinst;
    c->einst = einst;
    c->variant = variant;
    c->fieldindices = fieldindices;
    c->sourcefieldindices = sourcefieldindices;
    return c;
}

inline Node *Unary::Cp1(Inliner &inl) const {
    return inl.ast.New<Unary>(line, op, inl.Cp(child));
}

inline Node *Binary::Cp1(Inliner &inl) const {
    return inl.ast.New<Binary>(line, op, inl.Cp(left), inl.Cp(right));
}

inline Node *Dot::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Dot>(line, inl.Cp(obj), name, ns);
    c->fieldidx = fieldidx;
    c->member = member;
    c->variantconst = variantconst;
    c->einst = einst;
    return c;
}

inline Node *Call::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Call>(line, inl.Cp(callee));
    c->tyargs = tyargs;
    for (auto a : args) c->args.push_back(inl.Cp(a));
    c->firstdefault = firstdefault;
    c->ndefaults = ndefaults;
    c->trailing = trailing;   
    c->spec = spec;
    c->dispatch = dispatch;
    c->dispatcharg = dispatcharg;
    c->builtin = builtin;
    c->poolcheck = poolcheck;
    c->qtype = qtype;
    c->shaderblob = shaderblob;
    c->rettypes = rettypes;
    c->fmtspecs = fmtspecs;
    c->fmtcontexts = fmtcontexts;
    for (auto p : fvparams) c->fvparams.push_back(inl.Remap(p));
    c->fvbody = fvbody ? inl.CpBlock(fvbody) : nullptr;
    
    
    
    auto nested = spec && spec->lexparent;
    for (auto d : dispatch) nested = nested || d->lexparent;
    if (nested) inl.nestedcopies.push_back({ this, c });
    c->defaultinit = defaultinit ? inl.Cp(defaultinit) : nullptr;
    return c;
}

inline Node *Index::Cp1(Inliner &inl) const {
    return inl.ast.New<Index>(line, inl.Cp(obj), inl.Cp(idx));
}

inline Node *SliceExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<SliceExpr>(line, inl.Cp(obj));
    if (lo) c->lo = inl.Cp(lo);
    if (hi) c->hi = inl.Cp(hi);
    c->lo_from_end = lo_from_end;
    c->hi_from_end = hi_from_end;
    c->cmpview = cmpview;
    return c;
}

inline Node *AsCast::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<AsCast>(line, inl.Cp(child), type, unchecked);
    c->implicit = implicit;
    c->totype = totype;
    return c;
}

inline Node *RangeExpr::Cp1(Inliner &inl) const {
    return inl.ast.New<RangeExpr>(line, inl.Cp(lo), inl.Cp(hi));
}

inline Node *Block::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Block>(line);
    for (auto st : stmts) c->stmts.push_back(inl.Cp(st));
    if (tail) c->tail = inl.Cp(tail);
    return c;
}

inline Node *IfExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<IfExpr>(line, inl.Cp(cond), inl.CpBlock(thenb),
                                 elseb ? inl.Cp(elseb) : nullptr);
    c->flat = flat;
    return c;
}

inline Node *MatchExpr::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<MatchExpr>(line, inl.Cp(scrutinee));
    for (auto &arm : arms) {
        MatchArm a;
        a.pat = arm.pat;
        for (auto &pi : a.pat.items) {
            pi.lo = inl.Cp(pi.lo);
            if (pi.hi) pi.hi = inl.Cp(pi.hi);
        }
        a.variants = arm.variants;
        a.binder = inl.Remap(arm.binder);
        a.ranges = arm.ranges;
        a.body = inl.Cp(arm.body);
        c->arms.push_back(a);
    }
    return c;
}

inline Node *EarlyBlock::Cp1(Inliner &inl) const {
    return inl.ast.New<EarlyBlock>(line, inl.CpBlock(body));
}

inline Node *While::Cp1(Inliner &inl) const {
    return inl.ast.New<While>(line, inl.Cp(cond), inl.CpBlock(body));
}

inline Node *LoopExpr::Cp1(Inliner &inl) const {
    return inl.ast.New<LoopExpr>(line, inl.CpBlock(body));
}

inline Node *ForLoop::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<ForLoop>(line, byref, var, idxvar, inl.Cp(iter), inl.CpBlock(body));
    c->vartype = vartype;
    c->idxtype = idxtype;
    c->vdef = inl.Remap(vdef);
    c->idxdef = inl.Remap(idxdef);
    c->iterkind = iterkind;
    return c;
}

inline Node *Return::Cp1(Inliner &inl) const {
    auto r = inl.ast.New<Return>(line);
    for (auto v : vals) r->vals.push_back(inl.Cp(v));
    r->from = from;
    r->ns = ns;
    r->target = target;
    r->targetspec = targetspec;
    return r;
}

inline Node *Break::Cp1(Inliner &inl) const {
    return inl.ast.New<Break>(line, val ? inl.Cp(val) : nullptr);
}

inline Node *VarDecl::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<VarDecl>(line, isvar);
    c->isconst = isconst;
    c->reusable = reusable;
    c->isglobal = isglobal;
    c->byref = byref;
    c->inline_arg = inline_arg;
    c->names = names;
    c->type = type;
    for (auto i : inits) c->inits.push_back(inl.Cp(i));
    for (auto d : defs) c->defs.push_back(inl.Remap(d));
    return c;
}

inline Node *Assign::Cp1(Inliner &inl) const {
    auto c = inl.ast.New<Assign>(line, op, inl.Cp(lval), inl.Cp(rhs));
    c->pointee = pointee;
    return c;
}

inline Node *IncDec::Cp1(Inliner &inl) const {
    return inl.ast.New<IncDec>(line, op, inl.Cp(lval));
}

inline Node *InlineBlock::Cp1(Inliner &inl) const {
    return inl.ast.New<InlineBlock>(line, sf, spec, inl.CpBlock(body));
}



inline Node *FnDecl::Cp1(Inliner &inl) const { return inl.ast.New<FnDecl>(line, sf); }



inline Node *FunVal::Cp1(Inliner &) const { assert(false); return const_cast<FunVal *>(this); }
inline Node *StructDecl::Cp1(Inliner &) const { assert(false); return const_cast<StructDecl *>(this); }
inline Node *EnumDecl::Cp1(Inliner &) const { assert(false); return const_cast<EnumDecl *>(this); }
inline Node *AliasDecl::Cp1(Inliner &) const { assert(false); return const_cast<AliasDecl *>(this); }






inline Node *IntLit::Opt(Optimizer &) { return this; }
inline Node *FltLit::Opt(Optimizer &) { return this; }
inline Node *BoolLit::Opt(Optimizer &) { return this; }
inline Node *StrLit::Opt(Optimizer &) { return this; }
inline Node *NullLit::Opt(Optimizer &) { return this; }
inline Node *SelfRef::Opt(Optimizer &) { return this; }
inline Node *Continue::Opt(Optimizer &) { return this; }



inline Node *Defer::Opt(Optimizer &o) {
    o.OptBlock(body);
    if (body->stmts.empty() && !body->tail) return body;
    return this;
}
inline Node *FnDecl::Opt(Optimizer &) { return this; }   

inline Node *Ident::Opt(Optimizer &o) {
    if (vdef) {
        auto it = o.consts.find(vdef);
        if (it != o.consts.end()) {
            o.propagated++;
            return o.CloneLit(it->second, exprtype);
        }
    }
    return this;
}

inline Node *Unary::Opt(Optimizer &o) {
    child = op == T_BITAND ? o.OptViewed(child) : o.Opt(child);
    
    
    switch (op) {
        case T_MINUS:
            if (auto i = Is<IntLit>(child)) {
                auto t = Optimizer::IntTypeOf(child);
                if (!t) break;
                
                
                
                
                if (i->uns) {
                    if (i->val != INT64_MIN) break;
                    return o.NewInt(this, INT64_MIN);
                }
                if (i->val == INT64_MIN || !FitsIntStorage(-i->val, false, t->intstorage))
                    break;
                return o.NewInt(this, -i->val);
            }
            if (auto fl = Is<FltLit>(child)) return o.NewFlt(this, -fl->val);
            break;
        case T_BITNOT:
            if (auto i = Is<IntLit>(child)) {
                auto t = Optimizer::IntTypeOf(child);
                if (!t) break;
                return o.NewInt(this, WrapStorage(~i->val, t->intstorage));
            }
            break;
        case T_NOT:
            if (auto b = Is<BoolLit>(child)) return o.NewBool(this, !b->val);
            break;
        default: break;  
    }
    return this;
}

inline Node *Binary::Opt(Optimizer &o) {
    if (op == T_ANDAND || op == T_OROR) {
        left = o.Opt(left);
        if (auto lb = Is<BoolLit>(left)) {
            
            
            o.folded++;
            if (op == T_ANDAND) return lb->val ? o.Opt(right) : left;
            return lb->val ? left : o.Opt(right);
        }
        o.branchdepth++;
        right = o.OptIn(this, right);
        o.branchdepth--;
        if (auto rb = Is<BoolLit>(right)) {
            
            if (op == T_ANDAND && rb->val) { o.folded++; return left; }
            if (op == T_OROR && !rb->val) { o.folded++; return left; }
        }
        return this;
    }
    left = o.Opt(left);
    right = o.Opt(right);
    
    
    auto compare = [&](auto a, auto b) -> Node * {
        switch (op) {
            case T_LT:   return o.NewBool(this, a < b);
            case T_GT:   return o.NewBool(this, a > b);
            case T_LTEQ: return o.NewBool(this, a <= b);
            case T_GTEQ: return o.NewBool(this, a >= b);
            case T_EQ:   return o.NewBool(this, a == b);
            case T_NEQ:  return o.NewBool(this, a != b);
            default:     return nullptr;
        }
    };
    auto li = Is<IntLit>(left), ri = Is<IntLit>(right);
    if (li && ri) {
        
        
        
        
        auto ot = Optimizer::IntTypeOf(left);
        if (!ot) return this;
        auto s = ot->intstorage;
        int64_t r;
        if (FoldIntOp(op, li->val, ri->val, s, r)) return o.NewInt(this, r);
        
        
        auto c = IsUnsigned(s) ? compare((uint64_t)li->val, (uint64_t)ri->val)
                               : compare(li->val, ri->val);
        return c ? c : this;
    }
    auto lf = Is<FltLit>(left), rf = Is<FltLit>(right);
    if (lf && rf) {
        
        
        auto f32 = IsF32(left->exprtype) && IsF32(right->exprtype);
        auto a = lf->val, b = rf->val;
        if (f32) { a = (float)a; b = (float)b; }
        switch (op) {
            case T_PLUS:  return o.NewFlt(this, f32 ? (double)(float)(a + b) : a + b);
            case T_MINUS: return o.NewFlt(this, f32 ? (double)(float)(a - b) : a - b);
            case T_MUL:   return o.NewFlt(this, f32 ? (double)(float)(a * b) : a * b);
            case T_DIV:   return o.NewFlt(this, f32 ? (double)(float)(a / b) : a / b);
            default: break;
        }
        auto c = compare(a, b);
        return c ? c : this;   
    }
    auto lb = Is<BoolLit>(left), rb = Is<BoolLit>(right);
    if (lb && rb && (op == T_EQ || op == T_NEQ))
        return o.NewBool(this, (lb->val == rb->val) == (op == T_EQ));
    return this;
}

inline Node *Dot::Opt(Optimizer &o) {
    obj = IsField() && o.Viewed(this) ? o.OptViewed(obj) : o.Opt(obj);
    if ((member == B_LEN || member == B_CAP) && Is<Ident>(obj)) {
        
        
        
        auto t = obj->exprtype;
        if (t && t->kind == TY_REF && !t->ref->optional) t = t->ref->sub;
        if (t && t->kind == TY_ARRAY) {
            auto &a = *t->arr;
            if (member == B_LEN && a.akind == A_FIXED && a.size >= 0)
                return o.NewInt(this, a.size);
            if (member == B_CAP && a.akind == A_LIMITED && a.sizeexpr && a.size >= 0)
                return o.NewInt(this, a.size);
        }
    }
    return this;
}

inline Node *Call::Opt(Optimizer &o) {
    
    if (auto r = o.TryReassoc(this)) return r;
    
    
    
    auto recv = builtin >= 0 && (builtindefs[builtin].flags & BF_MEMBER);
    auto d = Is<Dot>(callee);
    if (d) d->obj = Optimizer::Hooked(this, 0) ? o.OptRendered(d->obj)
                    : recv                     ? o.OptViewed(d->obj)
                                               : o.Opt(d->obj);
    for (size_t i = 0; i < args.size(); i++)
        args[i] = Optimizer::Hooked(this, i + (d ? 1 : 0)) ? o.OptRendered(args[i])
                  : recv && !d && !i                       ? o.OptViewed(args[i])
                                                           : o.OptIn(this, args[i]);
    if (fvbody) o.OptBlock(fvbody);
    if (defaultinit) defaultinit = o.Opt(defaultinit);
    if (builtin == B_ASSERT) {
        if (auto b = Is<BoolLit>(FirstArg()); b && b->val) {
            o.folded++;
            return o.EmptyBlock(this);
        }
    }
    if (auto r = o.TryInline(this)) return r;
    if (auto r = o.TryBaseCase(this)) return r;
    return this;
}

inline Node *Index::Opt(Optimizer &o) {
    
    obj = o.Viewed(this) || !Optimizer::CodeFree(idx) ? o.OptViewed(obj) : o.Opt(obj);
    idx = o.Opt(idx);
    return this;
}

inline Node *SliceExpr::Opt(Optimizer &o) {
    obj = o.OptViewed(obj);
    if (lo) lo = o.Opt(lo);
    if (hi) hi = o.Opt(hi);
    return this;
}

inline Node *AsCast::Opt(Optimizer &o) {
    child = o.Opt(child);
    auto tt = totype;
    
    
    if (tt->ix) return this;
    if (auto i = Is<IntLit>(child)) {
        
        auto st = Optimizer::IntTypeOf(child);
        auto suns = i->val < 0 && (i->uns || (st && st->intstorage == IS_U64));
        if (tt->kind == TY_INT) {
            if (unchecked) return o.NewInt(this, WrapStorage(i->val, tt->intstorage));
            
            if (FitsIntStorage(i->val, suns, tt->intstorage))
                return o.NewInt(this, i->val);
        } else if (tt->kind == TY_FLT) {
            if (suns) return this;   
            
            if (IsF32(tt)) return o.NewFlt(this, (double)(float)i->val);
            return o.NewFlt(this, (double)i->val);
        }
        return this;
    }
    if (auto fl = Is<FltLit>(child)) {
        
        auto d = IsF32(child->exprtype) ? (double)(float)fl->val : fl->val;
        if (tt->kind == TY_INT) {
            
            
            
            if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                auto t = (int64_t)d;
                if (unchecked) return o.NewInt(this, WrapStorage(t, tt->intstorage));
                if ((double)t == d && FitsIntStorage(t, false, tt->intstorage))
                    return o.NewInt(this, t);
            }
        } else if (tt->kind == TY_FLT) {
            return o.NewFlt(this, IsF32(tt) ? (double)(float)d : d);
        }
        return this;
    }
    return this;
}

inline Node *RangeExpr::Opt(Optimizer &o) {
    lo = o.Opt(lo);
    hi = o.Opt(hi);
    return this;
}

inline Node *ArrayLit::Opt(Optimizer &o) {
    for (auto &e : elems) e = o.Opt(e);
    if (fillval) fillval = o.OptIn(this, fillval);
    if (fillcount) fillcount = o.Opt(fillcount);
    if (capexpr) capexpr = o.Opt(capexpr);
    return this;
}

inline Node *StructLit::Opt(Optimizer &o) {
    for (auto &fi : inits) fi.val = o.Opt(fi.val);
    return this;
}

inline Node *Block::Opt(Optimizer &o) {
    o.OptBlock(this);
    
    
    
    if (stmts.empty() && tail) return tail;
    return this;
}

inline Node *IfExpr::Opt(Optimizer &o) {
    cond = o.Opt(cond);
    if (auto b = Is<BoolLit>(cond)) {
        Node *taken = b->val ? (Node *)thenb : elseb;
        if (!taken) { o.folded++; return o.EmptyBlock(this); }
        
        
        if (exprtype->kind == TY_VOID || Optimizer::SameType(taken->exprtype, exprtype)) {
            o.folded++;
            return o.Opt(taken);
        }
    }
    
    
    auto thenleaves = Optimizer::Leaves(thenb), elseleaves = elseb && Optimizer::Leaves(elseb);
    auto thencold = thenleaves && !elseleaves && !flat;
    auto elsecold = elseleaves && (flat || !thenleaves);
    auto k = Optimizer::Around(this, thenb);
    o.branchdepth++;
    o.depth += k;
    o.colddepth += thencold;
    o.OptBlock(thenb);
    o.colddepth -= thencold;
    o.depth -= k;
    o.colddepth += elsecold;
    if (elseb) elseb = o.OptIn(this, elseb);
    o.colddepth -= elsecold;
    o.branchdepth--;
    return this;
}

inline Node *MatchExpr::Opt(Optimizer &o) {
    scrutinee = o.Opt(scrutinee);
    if (auto iv = Is<IntLit>(scrutinee)) {
        
        
        
        auto st = Optimizer::IntTypeOf(scrutinee);
        auto uns = st && IsUnsigned(st->intstorage);
        auto inrange = [&](const MatchArm &arm) {
            for (auto &r : arm.ranges)
                if (uns ? (uint64_t)iv->val >= (uint64_t)r.lo && (uint64_t)iv->val <= (uint64_t)r.hi
                        : iv->val >= r.lo && iv->val <= r.hi)
                    return true;
            return false;
        };
        MatchArm *sel = nullptr;
        for (auto &arm : arms) {
            if (arm.pat.kind == P_WILDCARD || inrange(arm)) { sel = &arm; break; }
        }
        if (sel && (exprtype->kind == TY_VOID ||
                    Optimizer::SameType(sel->body->exprtype, exprtype))) {
            o.folded++;
            return o.Opt(sel->body);
        }
    }
    o.branchdepth++;
    for (auto &arm : arms) arm.body = o.OptIn(this, arm.body);
    o.branchdepth--;
    return this;
}

inline Node *EarlyBlock::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}

inline Node *While::Opt(Optimizer &o) {
    o.loopdepth++;
    o.loopnodes.push_back(this);
    cond = o.OptIn(this, cond);
    if (auto b = Is<BoolLit>(cond); b && !b->val) {
        o.loopnodes.pop_back();
        o.loopdepth--;
        o.folded++;
        return o.EmptyBlock(this);
    }
    o.OptBlock(body);
    o.loopnodes.pop_back();
    o.loopdepth--;
    return this;
}

inline Node *LoopExpr::Opt(Optimizer &o) {
    o.loopdepth++;
    o.loopnodes.push_back(this);
    o.OptBlock(body);
    o.loopnodes.pop_back();
    o.loopdepth--;
    return this;
}

inline Node *ForLoop::Opt(Optimizer &o) {
    iter = o.OptViewed(iter);
    o.loopdepth++;
    o.loopnodes.push_back(this);
    o.OptBlock(body);
    o.loopnodes.pop_back();
    o.loopdepth--;
    return this;
}

inline Node *Return::Opt(Optimizer &o) {
    for (auto &v : vals) v = o.Opt(v);
    return this;
}

inline Node *Break::Opt(Optimizer &o) {
    if (val) val = o.Opt(val);
    return this;
}

inline Node *Assign::Opt(Optimizer &o) {
    
    
    lval = o.Opt(lval);
    rhs = o.Opt(rhs);
    return this;
}

inline Node *IncDec::Opt(Optimizer &o) {
    lval = o.Opt(lval);
    return this;
}

inline Node *InlineBlock::Opt(Optimizer &o) {
    o.OptBlock(body);
    return this;
}



inline Node *VarDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *FunVal::Opt(Optimizer &) { assert(false); return this; }
inline Node *StructDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *EnumDecl::Opt(Optimizer &) { assert(false); return this; }
inline Node *AliasDecl::Opt(Optimizer &) { assert(false); return this; }

}  
