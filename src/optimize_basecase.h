



































#pragma once

namespace faloya {




struct BaseCaseInliner {
    Optimizer &o;
    Ast &ast;

    FnSpec *basespec = nullptr;   
    Node *basecond = nullptr;     
    Node *baseexpr = nullptr;     
    bool basenot = false;         
    
    
    bool guarded = false;
    vector<Node *> basestmts;

    BaseCaseInliner(Optimizer &_o) : o(_o), ast(_o.ast) {}

    
    
    
    
    static bool FixedType(TypeExpr *t) {
        switch (t->kind) {
            case TY_INT:  return t->intstorage != IS_VARINT;
            case TY_REF:  return t->ref->lenstorage != IS_VARINT;
            case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
            case TY_STRUCT: return t->struc->inst && t->struc->inst->sclass == SC_FIXED;
            case TY_ENUM: return !t->enu->varmode;
            case TY_ARRAY: return t->arr->akind == A_FIXED;
            default: return false;
        }
    }

    
    
    static int SelfCalls(Node *n, FnSpec *sp) {
        if (!n) return 0;
        auto k = 0;
        if (auto c = Is<Call>(n))
            if (c->spec == sp && c->builtin < 0 && c->dispatch.empty() &&
                (Is<Ident>(c->callee) || Is<Dot>(c->callee))) k++;
        
        RunChildren(n, [&](Node *ch) { k += SelfCalls(ch, sp); });
        return k;
    }

    
    
    
    
    
    bool BaseOK(Node *n, set<VarDef *> &bound, bool pure) {
        if (!n) return true;
        if (auto id = Is<Ident>(n)) {
            auto v = id->vdef;   
            if (!v || v->isglobal || v->ownerspec != o.curspec) return true;
            return v->isparam || bound.count(v) > 0;
        }
        if (pure && !(Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<StrLit>(n) ||
                      Is<NullLit>(n) || Is<Unary>(n) || Is<Binary>(n) || Is<Dot>(n) ||
                      Is<AsCast>(n) || Is<Index>(n) || Is<SliceExpr>(n)))
            return false;
        
        
        
        
        
        assert(!Is<InlineBlock>(n));
        if (Is<Break>(n) || Is<Continue>(n) || Is<Return>(n) || Is<FnDecl>(n) || Is<FunVal>(n) ||
            Is<Defer>(n))
            return false;
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) bound.insert(d);
        if (auto fl = Is<ForLoop>(n)) {
            bound.insert(fl->vdef);
            if (fl->idxdef) bound.insert(fl->idxdef);
        }
        if (auto m = Is<MatchExpr>(n)) for (auto &a : m->arms) if (a.binder) bound.insert(a.binder);
        if (auto c = Is<Call>(n)) {
            
            
            auto bad = [&](FnSpec *k) {
                
                
                
                return k && (k->incycle || k->sf->isrec || k->needs.count(o.curspec) ||
                             !k->envreads.empty());
            };
            if (bad(c->spec)) return false;
            for (auto d : c->dispatch) if (bad(d)) return false;
            for (auto p : c->fvparams) bound.insert(p);
        }
        auto ok = true;
        RunChildren(n, [&](Node *ch) { ok = ok && BaseOK(ch, bound, pure); });
        return ok;
    }

    
    
    
    void Setup(FnSpec *sp) {
        basespec = nullptr;
        basecond = baseexpr = nullptr;
        basenot = guarded = false;
        basestmts.clear();
        if (!o.caninline || !sp->sf->isrec || !sp->body) return;
        
        
        
        
        for (auto pv : sp->params) if (!FixedType(pv->type)) return;
        if (!SetupLeading(sp)) SetupGuarded(sp);
    }

    
    
    bool SizeOK(FnSpec *sp, int sz) {
        auto sites = SelfCalls(sp->body, sp);
        return sites && (sz < o.nc || sz * sites < o.ncu);
    }

    
    bool SetupLeading(FnSpec *sp) {
        auto &stmts = sp->body->stmts;
        auto ife = Is<IfExpr>(stmts.empty() ? sp->body->tail : stmts[0]);
        if (!ife) return false;
        auto cond = ife->cond;
        auto neg = ife->elseb != nullptr;
        auto arm = neg ? Is<Block>(ife->elseb) : ife->thenb;
        if (!arm || arm->tail || arm->stmts.size() != 1) return false;
        auto r = Is<Return>(arm->stmts[0]);
        if (!r || r->target != sp->sf || !r->from.empty()) return false;
        if (r->vals.size() > 1 || r->vals.size() != sp->rets.size()) return false;
        auto expr = r->vals.empty() ? nullptr : r->vals[0];
        set<VarDef *> bound;
        if (!BaseOK(cond, bound, true)) return false;
        if (!BaseOK(expr, bound, false)) return false;
        if (!SizeOK(sp, CountNodes(cond) + CountNodes(expr))) return false;
        Inliner inl { o, ast, sp, sp, {}, {} };
        for (auto pv : sp->params) inl.vmap[pv] = pv;   
        basecond = inl.Cp(cond);
        if (expr) baseexpr = inl.Cp(expr);
        basenot = neg;
        basespec = sp;
        return true;
    }

    
    
    
    bool GuardOK(Node *n, FnSpec *sp) {
        if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<NullLit>(n)) return true;
        if (auto id = Is<Ident>(n)) {
            auto v = id->vdef;
            if (!v || !v->isparam || v->ownerspec != sp) return false;
            
            
            return Optimizer::Unwritten(o.facts, v);
        }
        if (auto u = Is<Unary>(n)) return u->op == T_NOT && GuardOK(u->child, sp);
        if (auto b = Is<Binary>(n)) {
            switch (b->op) {
                case T_EQ: case T_NEQ: case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:
                case T_ANDAND: case T_OROR:
                    return GuardOK(b->left, sp) && GuardOK(b->right, sp);
                default: return false;
            }
        }
        return false;
    }

    
    
    void SetupGuarded(FnSpec *sp) {
        auto body = sp->body;
        auto sites = SelfCalls(body, sp);
        if (!sites) return;
        vector<Node *> all = body->stmts;
        if (body->tail) all.push_back(body->tail);
        size_t k = 0;
        IfExpr *ife = nullptr;
        for (; k < all.size(); k++) {
            ife = Is<IfExpr>(all[k]);
            if (ife && SelfCalls(ife->thenb, sp) == sites) break;
            if (SelfCalls(all[k], sp)) return;
        }
        if (k == all.size() || ife->elseb || !GuardOK(ife->cond, sp)) return;
        
        
        vector<Node *> pre(all.begin(), all.begin() + k), post(all.begin() + k + 1, all.end());
        Node *expr = nullptr;
        if (!post.empty() && post.back() == body->tail) {
            if (sp->rets.size() != 1) return;
            expr = post.back();
            post.pop_back();
        } else if (auto r = post.empty() ? nullptr : Is<Return>(post.back())) {
            if (r->target != sp->sf || !r->from.empty() || r->vals.size() > 1 ||
                r->vals.size() != sp->rets.size())
                return;
            expr = r->vals.empty() ? nullptr : r->vals[0];
            post.pop_back();
        } else if (!sp->rets.empty()) {
            return;
        }
        set<VarDef *> bound;
        auto sz = CountNodes(ife->cond) + CountNodes(expr);
        for (auto part : { &pre, &post })
            for (auto st : *part) {
                if (!BaseOK(st, bound, false)) return;
                sz += CountNodes(st);
            }
        if (!BaseOK(expr, bound, false) || !SizeOK(sp, sz)) return;
        Inliner inl { o, ast, sp, sp, {}, {} };
        for (auto pv : sp->params) inl.vmap[pv] = pv;   
        basecond = inl.Cp(ife->cond);
        for (auto part : { &pre, &post })
            for (auto st : *part) basestmts.push_back(inl.Cp(st));
        if (expr) baseexpr = inl.Cp(expr);
        guarded = true;
        basespec = sp;
    }

    
    Node *Try(Call *c) {
        
        
        if (!basespec || basespec != o.curspec) return nullptr;
        if (c->spec != basespec || c->builtin >= 0 || !c->dispatch.empty()) return nullptr;
        if (!Is<Ident>(c->callee) && !Is<Dot>(c->callee)) return nullptr;
        auto K = basespec;
        
        
        
        auto argnodes = c->ValueArgNodes();
        if (argnodes.size() != K->params.size()) return nullptr;
        
        
        
        if (c->exprtype && c->exprtype->kind == TY_SLICE && K->rets.size() == 1 &&
            K->rets[0]->kind == TY_ARRAY)
            return nullptr;
        Inliner inl { o, ast, K, o.curspec, {}, {} };
        inl.AtCallSite(c);
        vector<Node *> decls;
        for (size_t i = 0; i < K->params.size(); i++) {
            auto pv = K->params[i];
            auto vd = inl.BindArg(pv, argnodes[i], c->line);
            if (!vd) continue;
            vd->inline_arg = true;
            decls.push_back(vd);
            
            
            auto id = ast.New<Ident>(c->line, pv->name);
            id->vdef = vd->defs[0];
            id->exprtype = pv->type;
            c->SetArgNode(i, id);
        }
        auto vt = c->exprtype ? c->exprtype : ast.voidtype;
        auto cond = o.Opt(inl.Cp(basecond));
        auto arm = [&](Node *v) {
            auto b = ast.New<Block>(c->line);
            if (v) b->tail = v;
            b->exprtype = vt;
            return b;
        };
        IfExpr *ife;
        if (guarded) {
            
            
            auto leaf = arm(nullptr);
            for (auto st : basestmts) leaf->stmts.push_back(inl.Cp(st));
            if (baseexpr) leaf->tail = inl.Cp(baseexpr);
            auto around = decls.empty() ? 0 : 1;
            o.depth += around;
            o.OptBlock(leaf);
            o.depth -= around;
            ife = ast.New<IfExpr>(c->line, cond, arm(c), leaf);
        } else {
            
            
            auto around = decls.empty() ? 1 : 2;
            o.depth += around;
            auto val = baseexpr ? o.Opt(inl.Cp(baseexpr)) : nullptr;
            o.depth -= around;
            
            
            ife = ast.New<IfExpr>(c->line, cond, arm(basenot ? (Node *)c : val),
                                  arm(basenot ? val : (Node *)c));
        }
        ife->exprtype = vt;
        o.basecases++;
        if (decls.empty()) return ife;
        auto blk = ast.New<Block>(c->line);
        blk->stmts = std::move(decls);
        blk->tail = ife;
        blk->exprtype = vt;
        return blk;
    }
};



inline void Optimizer::SetupBaseCase(FnSpec *sp) {
    if (!basecase) basecase = make_unique<BaseCaseInliner>(*this);
    basecase->Setup(sp);
}

inline Node *Optimizer::TryBaseCase(Call *c) {
    return basecase ? basecase->Try(c) : nullptr;
}

}  
