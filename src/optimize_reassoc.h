


































#pragma once

namespace faloya {

struct Reassociator {
    Optimizer &o;
    Ast &ast;

    
    unordered_map<Node *, unordered_set<VarDef *>> carriedvars;

    explicit Reassociator(Optimizer &_o) : o(_o), ast(_o.ast) {}

    
    static VarDef *RootVar(Node *n) {
        for (;;) {
            if (auto id = Is<Ident>(n)) return id->vdef;
            if (auto ix = Is<Index>(n)) { n = ix->obj; continue; }
            if (auto d = Is<Dot>(n); d && d->IsField()) { n = d->obj; continue; }
            if (auto sl = Is<SliceExpr>(n)) { n = sl->obj; continue; }
            return nullptr;
        }
    }

    const unordered_set<VarDef *> &Carried(Node *loop) {
        auto it = carriedvars.find(loop);
        if (it != carriedvars.end()) return it->second;
        unordered_set<VarDef *> written, local;
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto vd = Is<VarDecl>(n)) {
                for (auto d : vd->defs) local.insert(d);
            } else if (auto f = Is<ForLoop>(n)) {
                local.insert(f->vdef);
                local.insert(f->idxdef);
            } else if (auto a = Is<Assign>(n)) {
                written.insert(RootVar(a->lval));
            } else if (auto x = Is<IncDec>(n)) {
                written.insert(RootVar(x->lval));
            } else if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
                written.insert(RootVar(u->child));
            }
            RunChildren(n, walk);
        };
        
        walk(loop);
        auto &out = carriedvars[loop];
        for (auto v : written)
            if (v && !local.count(v)) out.insert(v);
        return out;
    }

    static bool Reads(Node *n, const unordered_set<VarDef *> &vars) {
        if (!n) return false;
        if (auto id = Is<Ident>(n); id && id->vdef && vars.count(id->vdef)) return true;
        auto found = false;
        RunChildren(n, [&](Node *ch) { found = found || Reads(ch, vars); });
        return found;
    }

    
    static VarDef *Returned(Node *n) {
        if (auto r = Is<Return>(n); r && r->vals.size() == 1) n = r->vals[0];
        auto id = Is<Ident>(n);
        return id ? id->vdef : nullptr;
    }

    
    
    static bool MinMaxShaped(FnSpec *K) {
        if (!K->body || K->params.size() != 2 || K->rets.size() != 1) return false;
        auto t = K->params[0]->type;
        if (!t || t->kind != TY_INT || !Optimizer::SameType(t, K->params[1]->type) ||
            !Optimizer::SameType(t, K->rets[0]))
            return false;
        auto &st = K->body->stmts;
        Node *last = K->body->tail;
        if (st.size() == 2 && !last) last = st[1];
        else if (st.size() != 1 || !last) return false;
        auto ife = Is<IfExpr>(st[0]);
        if (!ife || ife->elseb || ife->thenb->stmts.size() != 1 || ife->thenb->tail) return false;
        if (!Is<Return>(ife->thenb->stmts[0])) return false;
        auto cmp = Is<Binary>(ife->cond);
        if (!cmp || cmp->op != T_LT) return false;
        auto x = Returned(cmp->left), y = Returned(cmp->right);
        auto p = K->params[0], q = K->params[1];
        if (!((x == p && y == q) || (x == q && y == p))) return false;
        auto taken = Returned(ife->thenb->stmts[0]), other = Returned(last);
        return (taken == x && other == y) || (taken == y && other == x);
    }

    
    static bool Link(Node *n, FnSpec *K) {
        auto c = Is<Call>(n);
        return c && c->spec == K && c->builtin < 0 && c->dispatch.empty() && !c->fvbody &&
               Is<Ident>(c->callee) && c->args.size() == 2;
    }

    Node *Try(Call *c) {
        if (!o.caninline || !o.curspec || o.loopnodes.empty()) return nullptr;
        auto K = c->spec;
        if (!K || !Link(c, K) || !MinMaxShaped(K)) return nullptr;
        
        
        vector<pair<Node *, int>> ops;
        vector<Call *> calls;
        function<void(Node *, int)> flatten = [&](Node *n, int depth) {
            if (!Link(n, K)) { ops.push_back({ n, depth }); return; }
            auto cc = Is<Call>(n);
            calls.push_back(cc);
            for (auto a : cc->args) flatten(a, depth + 1);
        };
        flatten(c, 0);
        if (ops.size() < 3) return nullptr;
        auto &carried = Carried(o.loopnodes.back());
        vector<size_t> ns, cs;
        auto deepest = 0;
        for (size_t i = 0; i < ops.size(); i++) {
            if (Reads(ops[i].first, carried)) {
                cs.push_back(i);
                deepest = max(deepest, ops[i].second);
            } else {
                ns.push_back(i);
            }
        }
        
        if (cs.empty() || ns.empty() || deepest <= (int)cs.size()) return nullptr;
        auto blk = ast.New<Block>(c->line);
        vector<Node *> refs;
        for (size_t i = 0; i < ops.size(); i++) {
            auto n = ops[i].first;
            if (Optimizer::AsLiteral(n)) { refs.push_back(n); continue; }
            auto nv = ast.NewVarDef();
            *nv = *K->params[0];
            nv->isparam = false;
            nv->isvar = false;
            nv->captured = false;
            nv->name = ast.Intern(cat("operand", i + 1));
            nv->ownerspec = o.curspec;
            o.facts[nv] = {};
            auto vd = ast.New<VarDecl>(n->line, false);
            vd->names.push_back(nv->name);
            vd->defs.push_back(nv);
            vd->inits.push_back(n);
            vd->exprtype = ast.voidtype;
            blk->stmts.push_back(vd);
            auto id = ast.New<Ident>(n->line, nv->name);
            id->vdef = nv;
            id->exprtype = nv->type;
            refs.push_back(id);
        }
        
        
        vector<size_t> order = ns;
        order.insert(order.end(), cs.begin(), cs.end());
        Node *acc = refs[order[0]];
        for (size_t k = 1; k < order.size(); k++) {
            auto cc = calls[k - 1];
            cc->args = { acc, refs[order[k]] };
            acc = cc;
        }
        blk->tail = acc;
        blk->exprtype = c->exprtype;
        o.reassociated++;
        return o.Opt(blk);
    }
};

inline Node *Optimizer::TryReassoc(Call *c) {
    if (!reassoc) reassoc = make_unique<Reassociator>(*this);
    return reassoc->Try(c);
}

}  
