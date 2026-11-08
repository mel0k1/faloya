


























#pragma once

namespace faloya {



struct LoopShaper {
    unordered_map<VarDef *, Optimizer::VarFacts> &facts;

    
    
    static constexpr int64_t MAXSTRIP = 16;
    static constexpr int MAXBODY = 300;

    explicit LoopShaper(unordered_map<VarDef *, Optimizer::VarFacts> &f) : facts(f) {}

    void Run(Ast &ast) {
        for (auto sp : ast.fnspecs)
            if (sp->live && sp->body) Walk(sp->body);
    }

    void Walk(Node *n) {
        if (!n) return;
        if (auto f = Is<ForLoop>(n)) ShapeFor(f);
        else if (auto w = Is<While>(n)) ShapeWhile(w);
        RunChildren(n, [&](Node *ch) { Walk(ch); });
    }

    
    
    
    
    bool PrivateScalar(VarDef *v) {
        if (!v || v->isglobal || v->captured || !v->type) return false;
        auto it = facts.find(v);
        return it == facts.end() || it->second.addrof == 0;
    }

    static bool IsVarRef(Node *n, VarDef *v) {
        auto id = Is<Ident>(n);
        return id && id->vdef == v;
    }

    
    static bool Writes(Node *n, VarDef *v) {
        if (!n) return false;
        if (auto a = Is<Assign>(n); a && IsVarRef(a->lval, v)) return true;
        if (auto x = Is<IncDec>(n); x && IsVarRef(x->lval, v)) return true;
        auto found = false;
        RunChildren(n, [&](Node *ch) { found = found || Writes(ch, v); });
        return found;
    }

    static bool IsZero(Node *n) {
        auto lit = Is<IntLit>(n);
        return lit && !lit->uns && lit->val == 0;
    }

    void ShapeWhile(While *w) {
        auto c = Is<Binary>(w->cond);
        if (!c) return;
        Node *var = nullptr;
        if (c->op == T_GT && IsZero(c->right)) var = c->left;
        else if (c->op == T_LT && IsZero(c->left)) var = c->right;
        auto id = Is<Ident>(var);
        auto v = id ? id->vdef : nullptr;
        if (!v || !v->isvar || !v->type || v->type->kind != TY_INT ||
            v->type->intstorage == IS_VARINT || !PrivateScalar(v))
            return;
        auto &st = w->body->stmts;
        if (st.empty()) return;
        auto one = [](Node *n) {
            auto lit = Is<IntLit>(n);
            return lit && !lit->uns && lit->val == 1;
        };
        auto dec = Is<IncDec>(st[0]);
        auto sub = Is<Assign>(st[0]);
        if (!(dec && dec->op == T_DEC && IsVarRef(dec->lval, v)) &&
            !(sub && sub->op == T_MINUSEQ && IsVarRef(sub->lval, v) && one(sub->rhs)))
            return;
        for (size_t i = 1; i < st.size(); i++)
            if (Writes(st[i], v)) return;
        if (Writes(w->body->tail, v)) return;
        w->countdown = true;
    }

    void ShapeFor(ForLoop *f) {
        if (CountNodes(f->body) > MAXBODY) return;
        ShapeStrip(f);
        if (!f->stripk) ShapeSum(f);
    }

    
    
    
    static VarDef *ZeroBasedCounter(ForLoop *f) {
        if (f->iterkind == IK_ARRAY || f->iterkind == IK_SLICE) {
            auto ix = f->idxdef;
            return ix && ix->type && ix->type->intstorage == IS_I64 ? ix : nullptr;
        }
        auto r = Is<RangeExpr>(f->iter);
        if (r && !IsZero(r->lo)) return nullptr;
        auto ct = r ? r->exprtype : f->iter->exprtype;
        auto v = f->vdef;
        if (!v || !v->type || !ct || ct->kind != TY_INT || v->type->kind != TY_INT ||
            v->type->intstorage != ct->intstorage)
            return nullptr;
        return v;
    }

    void ShapeStrip(ForLoop *f) {
        auto ctr = ZeroBasedCounter(f);
        if (!ctr || ctr->captured) return;
        int64_t k = 0;
        vector<Binary *> mods;
        function<void(Node *)> find = [&](Node *n) {
            if (!n) return;
            if (auto b = Is<Binary>(n); b && b->op == T_MOD && IsVarRef(b->left, ctr)) {
                auto lit = Is<IntLit>(b->right);
                if (lit && !lit->uns && lit->val >= 2 && lit->val <= MAXSTRIP &&
                    (!k || lit->val == k)) {
                    k = lit->val;
                    mods.push_back(b);
                }
            }
            RunChildren(n, find);
        };
        find(f->body);
        if (!k) return;
        f->stripk = (int)k;
        f->lanemods = std::move(mods);
    }

    
    
    
    bool PureTerm(Node *e, VarDef *s, vector<SFunction *> &ibs) {
        if (!e) return true;
        if (Is<IntLit>(e) || Is<FltLit>(e) || Is<BoolLit>(e)) return true;
        if (auto id = Is<Ident>(e)) return id->vdef && id->vdef != s;
        if (auto b = Is<Binary>(e))
            return PureTerm(b->left, s, ibs) && PureTerm(b->right, s, ibs);
        if (auto u = Is<Unary>(e))
            return u->op != T_BITAND && PureTerm(u->child, s, ibs);
        if (auto c = Is<AsCast>(e)) return PureTerm(c->child, s, ibs);
        if (auto ix = Is<Index>(e)) return PureTerm(ix->obj, s, ibs) && PureTerm(ix->idx, s, ibs);
        if (auto d = Is<Dot>(e)) {
            if (d->variantconst) return true;
            return (d->IsField() || d->member == B_LEN || d->member == B_CAP) &&
                   PureTerm(d->obj, s, ibs);
        }
        if (auto fi = Is<IfExpr>(e))
            return PureTerm(fi->cond, s, ibs) && PureTerm(fi->thenb, s, ibs) &&
                   PureTerm(fi->elseb, s, ibs);
        if (auto ib = Is<InlineBlock>(e)) {
            ibs.push_back(ib->sf);
            auto ok = PureTerm(ib->body, s, ibs);
            ibs.pop_back();
            return ok;
        }
        if (auto r = Is<Return>(e)) {
            if (std::find(ibs.begin(), ibs.end(), r->target) == ibs.end()) return false;
            for (auto v : r->vals) if (!PureTerm(v, s, ibs)) return false;
            return true;
        }
        if (auto vd = Is<VarDecl>(e)) {
            if (vd->defs.size() != 1 || vd->inits.size() != 1 || vd->byref) return false;
            auto d = vd->defs[0];
            auto t = d ? d->type : nullptr;
            if (!t || d->captured ||
                (t->kind != TY_INT && t->kind != TY_FLT && t->kind != TY_BOOL))
                return false;
            return PureTerm(vd->inits[0], s, ibs);
        }
        if (auto bl = Is<Block>(e)) {
            for (auto st : bl->stmts) if (!PureTerm(st, s, ibs)) return false;
            return PureTerm(bl->tail, s, ibs);
        }
        return false;
    }

    void ShapeSum(ForLoop *f) {
        auto body = f->body;
        Node *st = nullptr;
        if (body->stmts.size() == 1 && !body->tail) st = body->stmts[0];
        else if (body->stmts.empty()) st = body->tail;
        auto a = Is<Assign>(st);
        if (!a || a->op != T_PLUSEQ || a->pointee) return;
        auto id = Is<Ident>(a->lval);
        auto s = id ? id->vdef : nullptr;
        if (!s || !s->type || s->type->kind != TY_FLT || !PrivateScalar(s)) return;
        vector<SFunction *> ibs;
        if (!PureTerm(a->rhs, s, ibs)) return;
        f->sumred = true;
    }
};

inline void Optimizer::ShapeLoops() { LoopShaper(facts).Run(ast); }

}  
