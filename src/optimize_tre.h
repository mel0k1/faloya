





















#pragma once

namespace faloya {




struct TailRecursion {
    Ast &ast;
    FnSpec *sp;                                          
    unordered_map<VarDef *, Optimizer::VarFacts> &facts;
    int &tailloops;                                      

    TypeExpr *rt = nullptr;     
    VarDef *acc = nullptr;      
    TType op = T_PLUS, comp = T_PLUSEQ;
    int64_t identity = 0;

    struct Tail {
        Call *call = nullptr;   
        Node *e = nullptr;      
        TType op = T_PLUS;      
    };

    unordered_map<Return *, Tail> tails;

    
    
    static bool AssocOp(TType op, TType &comp, int64_t &identity) {
        switch (op) {
            case T_PLUS:   comp = T_PLUSEQ; identity =  0; return true;
            case T_MUL:    comp = T_MULEQ;  identity =  1; return true;
            case T_BITAND: comp = T_ANDEQ;  identity = -1; return true;
            case T_BITOR:  comp = T_OREQ;   identity =  0; return true;
            case T_XOR:    comp = T_XOREQ;  identity =  0; return true;
            default: return false;
        }
    }

    static bool CompoundBase(TType comp, TType &op) {
        switch (comp) {
            case T_PLUSEQ: op = T_PLUS;   return true;
            case T_MULEQ:  op = T_MUL;    return true;
            case T_ANDEQ:  op = T_BITAND; return true;
            case T_OREQ:   op = T_BITOR;  return true;
            case T_XOREQ:  op = T_XOR;    return true;
            default: return false;
        }
    }

    
    
    Call *SelfCall(Node *n) {
        auto c = Is<Call>(n);
        return c && c->builtin < 0 && c->dispatch.empty() && c->spec == sp ? c : nullptr;
    }

    
    
    static bool ReszType(TypeExpr *t) {
        switch (t->kind) {
            case TY_ARRAY:   return t->arr->akind == A_GROW || t->arr->akind == A_GROWSHRINK;
            case TY_STRUCT:  return !t->struc->inst || t->struc->inst->sclass == SC_RESIZABLE;
            case TY_ENUM:    return t->enu->varmode &&
                                    (!t->enu->inst || t->enu->inst->varclass == SC_RESIZABLE);
            case TY_VARIANT: return !t->var->adt || ReszType(t->var->adt);
            default:         return false;
        }
    }

    
    
    
    static bool Rebindable(VarDef *p) {
        auto t = p->type;
        if (!t || p->captured) return false;
        if (Optimizer::ScalarType(t)) return true;
        return t->kind == TY_REF && t->ref->lenstorage < 0 && !p->reusable &&
               !p->ref.reusable && !ReszType(t->ref->sub);
    }

    
    
    bool Pure(Node *n) {
        if (Optimizer::AsLiteral(n)) return true;
        auto id = Is<Ident>(n);
        if (!id || !id->vdef || id->vdef->isglobal) return false;
        
        
        
        return Optimizer::Unwritten(facts, id->vdef);
    }

    
    
    
    unordered_map<VarDef *, Node *> bound;

    void CollectBound(Node *n) {
        if (!n) return;
        if (auto vd = Is<VarDecl>(n); vd && !vd->results && vd->defs.size() == 1 &&
                                      vd->inits.size() == 1)
            bound[vd->defs[0]] = vd->inits[0];
        if (auto m = Is<MatchExpr>(n))
            for (auto &arm : m->arms)
                if (arm.binder) bound[arm.binder] = m->scrutinee;
        RunChildren(n, [&](Node *ch) { CollectBound(ch); });
    }

    
    
    
    enum Loc { LOC_ANY, LOC_HOLDSOUT, LOC_OUT };

    
    
    
    
    
    
    
    
    
    
    
    
    
    Loc Locate(Node *n, bool viewed = false) {
        
        
        auto through = [&](TypeExpr *t, Loc l) {
            auto ref = t && (t->kind == TY_REF || (t->kind == TY_SLICE && viewed));
            return ref && l == LOC_HOLDSOUT ? LOC_OUT : l;
        };
        if (Is<NullLit>(n)) return LOC_OUT;
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) return Locate(u->child, viewed);
        if (auto d = Is<Dot>(n))
            return d->IsField() ? through(d->exprtype, Locate(d->obj)) : LOC_ANY;
        if (auto ix = Is<Index>(n)) return through(ix->exprtype, Locate(ix->obj, true));
        if (auto se = Is<SliceExpr>(n)) return Locate(se->obj, true);
        auto id = Is<Ident>(n);
        auto v = id ? id->vdef : nullptr;
        if (!v || !v->type) return LOC_ANY;
        if (v->isglobal || v->ownerspec != sp) return LOC_OUT;
        if (v->isparam) return through(v->type, LOC_HOLDSOUT);
        auto it = bound.find(v);
        if (v->isvar || it == bound.end()) return LOC_ANY;
        auto t = v->type;
        if (t->kind == TY_REF) return Locate(it->second);
        if (t->kind == TY_SLICE) return viewed ? Locate(it->second, true) : LOC_ANY;
        return Locate(it->second) == LOC_ANY ? LOC_ANY : LOC_HOLDSOUT;
    }

    
    
    bool Abandons(Node *n, bool breaks = true) {
        if (!n) return false;
        if (auto r = Is<Return>(n); r && r->target == sp->sf) return true;
        if (breaks && Is<Break>(n)) return true;
        if (auto fl = Is<ForLoop>(n))
            return Abandons(fl->iter, breaks) || Abandons(fl->body, false);
        if (Is<EarlyBlock>(n) || Is<While>(n) || Is<LoopExpr>(n))
            breaks = false;
        auto found = false;
        RunChildren(n, [&](Node *ch) { found = found || Abandons(ch, breaks); });
        return found;
    }

    
    
    
    
    
    
    
    
    
    
    
    
    bool Rebinds(Call *c, vector<pair<VarDef *, Node *>> &out) {
        auto an = c->ValueArgNodes();
        if (an.size() != sp->params.size()) return false;
        if (Abandons(c)) return false;
        for (size_t i = 0; i < an.size(); i++) {
            auto p = sp->params[i];
            if (auto id = Is<Ident>(an[i]); id && id->vdef == p &&
                (Optimizer::Unwritten(facts, p) ||
                 (p->type->kind == TY_REF && !p->isvar))) continue;
            if (!Rebindable(p)) return false;
            if (p->type->kind == TY_REF && Locate(an[i]) != LOC_OUT) return false;
            out.push_back({ p, an[i] });
        }
        return true;
    }

    
    bool Classify(Node *v, Tail &t) {
        if (!Optimizer::SameType(v->exprtype, rt)) return false;
        if (auto c = SelfCall(v)) { t.call = c; t.e = nullptr; return true; }
        auto b = Is<Binary>(v);
        TType compop;
        int64_t ident;
        if (!b || !AssocOp(b->op, compop, ident)) return false;
        if (!Optimizer::SameType(b->left->exprtype, rt) ||
            !Optimizer::SameType(b->right->exprtype, rt)) return false;
        if (auto c = SelfCall(b->right)) {
            t.call = c; t.e = b->left; t.op = b->op;
            return true;
        }
        
        if (auto c = SelfCall(b->left); c && Pure(b->right)) {
            t.call = c; t.e = b->right; t.op = b->op;
            return true;
        }
        return false;
    }

    Node *AccRef(Line ln) {
        auto id = ast.New<Ident>(ln, acc->name);
        id->vdef = acc;
        id->exprtype = rt;
        return id;
    }

    Node *VarRef(VarDef *v, Line ln) {
        auto id = ast.New<Ident>(ln, v->name);
        id->vdef = v;
        id->exprtype = v->type;
        return id;
    }

    
    
    
    
    void AccVarForm() {
        auto body = sp->body;
        if (body->stmts.empty() || !body->tail) return;
        auto tid = Is<Ident>(body->tail);
        if (!tid || !tid->vdef || tid->vdef->isglobal) return;
        auto v = tid->vdef;
        auto &vf = facts[v];
        if (!Optimizer::SameType(v->type, rt) ||
            !Optimizer::SameType(body->tail->exprtype, rt)) return;
        if (vf.addrof || v->captured) return;
        Node **slot = &body->stmts.back();
        auto wholestmt = true;
        if (auto fi = Is<IfExpr>(*slot);
            fi && !fi->elseb && fi->thenb->stmts.size() == 1 && !fi->thenb->tail) {
            slot = &fi->thenb->stmts[0];
            wholestmt = false;
        }
        auto a = Is<Assign>(*slot);
        if (!a || a->pointee) return;
        TType binop;
        if (!CompoundBase(a->op, binop)) return;
        auto lid = Is<Ident>(a->lval);
        if (!lid || lid->vdef != v) return;
        auto c = SelfCall(a->rhs);
        if (!c || !Optimizer::SameType(a->rhs->exprtype, rt)) return;
        vector<pair<VarDef *, Node *>> rb;
        if (!Rebinds(c, rb)) return;   
        auto bin = ast.New<Binary>(a->line, binop, VarRef(v, a->line), a->rhs);
        bin->exprtype = rt;
        auto r = ast.New<Return>(a->line);
        r->target = sp->sf;
        r->vals.push_back(bin);
        r->exprtype = ast.voidtype;
        *slot = r;
        if (wholestmt) {
            
            body->tail = nullptr;
            body->exprtype = ast.voidtype;
        }
    }

    
    Block *Step(Return *r, const Tail &tl) {
        auto ln = r->line;
        auto b = ast.New<Block>(ln);
        b->exprtype = ast.voidtype;
        if (tl.e) {
            auto a = ast.New<Assign>(ln, comp, AccRef(ln), tl.e);
            a->exprtype = ast.voidtype;
            b->stmts.push_back(a);
        }
        vector<pair<VarDef *, Node *>> rb;
        Rebinds(tl.call, rb);
        vector<VarDef *> tmps;
        if (rb.size() > 1) {
            
            
            for (auto &[p, val] : rb) {
                auto nv = ast.NewVarDef();
                *nv = *p;
                nv->isparam = false;
                nv->isvar = false;
                nv->captured = false;
                nv->ownerspec = sp;
                auto vd = ast.New<VarDecl>(ln, false);
                vd->names.push_back(nv->name);
                vd->defs.push_back(nv);
                vd->inits.push_back(val);
                vd->exprtype = ast.voidtype;
                b->stmts.push_back(vd);
                tmps.push_back(nv);
            }
        }
        for (size_t i = 0; i < rb.size(); i++) {
            auto p = rb[i].first;
            auto val = tmps.empty() ? rb[i].second : VarRef(tmps[i], ln);
            auto a = ast.New<Assign>(ln, p->type->kind == TY_REF ? T_DOTASSIGN : T_ASSIGN,
                                     VarRef(p, ln), val);
            a->exprtype = ast.voidtype;
            b->stmts.push_back(a);
            facts[p].writes++;
        }
        auto cont = ast.New<Continue>(ln);
        cont->exprtype = ast.voidtype;
        b->stmts.push_back(cont);
        return b;
    }

    void RwSlot(Node *&slot) {
        if (auto r = Is<Return>(slot)) {
            auto it = tails.find(r);
            if (it != tails.end()) slot = Step(r, it->second);
        }
        RwWalk(slot);
    }

    
    
    void RwWalk(Node *n) {
        if (!n) return;
        if (auto b = Is<Block>(n)) {
            for (auto &st : b->stmts) RwSlot(st);
            if (b->tail) RwSlot(b->tail);
            return;
        }
        if (auto fi = Is<IfExpr>(n)) {
            RwWalk(fi->thenb);
            if (fi->elseb) RwSlot(fi->elseb);
            return;
        }
        if (auto me = Is<MatchExpr>(n)) {
            RwWalk(me->scrutinee);
            for (auto &arm : me->arms) RwSlot(arm.body);
            return;
        }
        RunChildren(n, [&](Node *ch) { RwWalk(ch); });
    }

    
    
    
    void FoldReturns(Node *n) {
        if (!n) return;
        if (auto r = Is<Return>(n); r && r->target == sp->sf && r->vals.size() == 1) {
            auto val = r->vals[0];
            FoldReturns(val);
            auto lit = Is<IntLit>(val);
            if (lit && lit->val == identity) {
                r->vals[0] = AccRef(r->line);   
                return;
            }
            auto bin = ast.New<Binary>(r->line, op, AccRef(r->line), val);
            bin->exprtype = rt;
            r->vals[0] = bin;
            return;
        }
        RunChildren(n, [&](Node *ch) { FoldReturns(ch); });
    }

    
    static bool Diverges(Node *n) {
        if (!n) return false;
        if (Optimizer::Jumps(n)) return true;
        auto b = Is<Block>(n);
        return b && !b->tail && !b->stmts.empty() && Diverges(b->stmts.back());
    }

    
    
    bool Applicable(Node *n, bool &selfcalls) {
        if (!n) return true;
        auto ok = true;
        if (auto r = Is<Return>(n)) {
            if (r->target == sp->sf && r->vals.size() != 1) return false;
        } else if (auto c = Is<Call>(n)) {
            if (SelfCall(c)) selfcalls = true;
            
            
            if (c->builtin < 0 && c->spec && c->spec->needs.count(sp)) return false;
            for (auto d : c->dispatch) if (d->needs.count(sp)) return false;
        }
        RunChildren(n, [&](Node *ch) { ok = ok && Applicable(ch, selfcalls); });
        return ok;
    }

    
    
    
    
    
    void Collect(Node *n, bool tailok, vector<pair<Return *, Tail>> &found) {
        if (!n) return;
        if (tailok) {
            if (auto r = Is<Return>(n); r && r->target == sp->sf && r->vals.size() == 1) {
                Tail tl;
                if (Classify(r->vals[0], tl)) found.push_back({ r, tl });
            }
        }
        if (auto c = Is<Call>(n)) {
            Collect(c->callee, tailok, found);
            for (auto a : c->args) Collect(a, tailok, found);
            Collect(c->fvbody, false, found);
            return;
        }
        auto inner = tailok && !Is<While>(n) && !Is<LoopExpr>(n) && !Is<ForLoop>(n);
        n->Children([&](Node *ch) { Collect(ch, inner, found); });
    }

    
    
    
    static bool HasDefer(Node *n) {
        if (!n || Is<FnDecl>(n)) return false;
        if (Is<Defer>(n)) return true;
        auto found = false;
        RunChildren(n, [&](Node *ch) { found = found || HasDefer(ch); });
        return found;
    }

    void Run() {
        if (sp->rets.size() != 1) return;
        rt = sp->rets[0];
        if (rt->kind != TY_INT || rt->intstorage == IS_VARINT) return;
        if (HasDefer(sp->body)) return;
        auto selfcalls = false;
        if (!Applicable(sp->body, selfcalls) || !selfcalls) return;
        CollectBound(sp->body);
        AccVarForm();
        
        
        
        
        
        auto tail = sp->body->tail;
        auto tailisvalue = tail && tail->exprtype && tail->exprtype->kind != TY_VOID;
        if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) tailisvalue = false;
        vector<pair<Return *, Tail>> found, cands;
        Collect(sp->body, true, found);
        for (auto &[r, tl] : found) {
            vector<pair<VarDef *, Node *>> rb;
            if (Rebinds(tl.call, rb)) cands.push_back({ r, tl });
        }
        Tail tailtl;
        auto tailistail = false;
        if (tailisvalue && Classify(tail, tailtl)) {
            vector<pair<VarDef *, Node *>> rb;
            tailistail = Rebinds(tailtl.call, rb);
        }
        auto haveop = false;
        for (auto &[r, tl] : cands)
            if (tl.e) { op = tl.op; haveop = true; break; }
        if (!haveop && tailistail && tailtl.e) { op = tailtl.op; haveop = true; }
        if (haveop) AssocOp(op, comp, identity);
        auto usable = [&](const Tail &tl) { return !tl.e || tl.op == op; };
        auto ntails = tailistail && usable(tailtl) ? 1 : 0;
        for (auto &[r, tl] : cands) if (usable(tl)) ntails++;
        if (!ntails) return;
        
        if (tailisvalue) {
            auto r = ast.New<Return>(tail->line);
            r->target = sp->sf;
            r->vals.push_back(tail);
            r->exprtype = ast.voidtype;
            sp->body->stmts.push_back(r);
            sp->body->tail = nullptr;
            sp->body->exprtype = ast.voidtype;
            if (tailistail && usable(tailtl)) tails[r] = tailtl;
        }
        for (auto &[r, tl] : cands)
            if (usable(tl)) tails[r] = tl;
        if (haveop) {
            acc = ast.NewVarDef();
            acc->name = "acc";
            acc->type = rt;
            acc->line = sp->sf->line;
            acc->isvar = true;
            acc->assigned = true;
            acc->ownerspec = sp;
            facts[acc].writes = 1;   
        }
        RwWalk(sp->body);
        if (acc) FoldReturns(sp->body);
        
        auto ln = sp->body->line;
        auto inner = ast.New<Block>(ln);
        inner->stmts = std::move(sp->body->stmts);
        inner->exprtype = ast.voidtype;
        if (sp->body->tail) inner->stmts.push_back(sp->body->tail);
        if (inner->stmts.empty() || !Diverges(inner->stmts.back())) {
            
            
            auto br = ast.New<Break>(ln, nullptr);
            br->exprtype = ast.voidtype;
            inner->stmts.push_back(br);
        }
        auto loop = ast.New<LoopExpr>(ln, inner);
        loop->exprtype = ast.voidtype;
        sp->body->stmts.clear();
        sp->body->tail = nullptr;
        sp->body->exprtype = ast.voidtype;
        if (acc) {
            auto v = WrapStorage(identity, rt->intstorage);
            auto init = ast.New<IntLit>(ln, v, string_view {}, rt->intstorage == IS_U64 && v < 0);
            init->exprtype = rt;
            auto vd = ast.New<VarDecl>(ln, true);
            vd->names.push_back(acc->name);
            vd->defs.push_back(acc);
            vd->inits.push_back(init);
            vd->exprtype = ast.voidtype;
            sp->body->stmts.push_back(vd);
        }
        sp->body->stmts.push_back(loop);
        tailloops++;
    }
};

inline void Optimizer::TailRecurse(FnSpec *sp) {
    if (!nc) return;   
    TailRecursion tre { ast, sp, facts, tailloops };
    tre.Run();
}

}  
