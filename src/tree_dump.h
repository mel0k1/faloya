

#pragma once

#include "dump.h"

namespace faloya {

inline void TIndent(string &s, int ind) { s.append((size_t)ind * 2, ' '); }
inline void TNl(string &s, int ind) { s += '\n'; TIndent(s, ind); }




inline void TreeCompact(string &s, string_view v) {
    for (size_t i = 0; i < v.size();) {
        auto c = v[i];
        if (c == '/' && i + 1 < v.size() && v[i + 1] == '/') {
            while (i < v.size() && v[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < v.size() && v[i + 1] == '*') {
            i += 2;
            while (i + 1 < v.size() && !(v[i] == '*' && v[i + 1] == '/')) i++;
            i += 2;
            continue;
        }
        if ((uint8_t)c <= ' ') { i++; continue; }
        s += c;
        i++;
    }
}

inline void TreeTypeCompact(string &s, const TypeExpr *t) {
    if (t && t->srcbegin && t->srcend && t->srcend >= t->srcbegin) {
        TreeCompact(s, string_view(t->srcbegin, (size_t)(t->srcend - t->srcbegin)));
        return;
    }
    t->Dump(s, (size_t)-1);
}

inline void TreeTypeRaw(string &s, int ind, const TypeExpr *t) {
    TNl(s, ind);
    s += "raw ";
    TreeTypeCompact(s, t);
}

inline void TreeRawLine(string &s, int ind, const string &text) {
    TNl(s, ind);
    s += "raw ";
    TreeCompact(s, text);
}

inline string TreeTypeText(const TypeExpr *t) {
    string out;
    TreeTypeCompact(out, t);
    return out;
}


inline void TreeGenerics(string &s, const vector<GenericParam> &gs, int ind) {
    if (gs.empty()) return;
    string t = "<";
    for (size_t i = 0; i < gs.size(); i++) {
        if (i) t += ", ";
        t += gs[i].name;
        if (gs[i].bound) { t += ": "; TreeTypeCompact(t, gs[i].bound); }
    }
    t += ">";
    TreeRawLine(s, ind, t);
}

inline void TreeStrLine(string &s, int ind, string_view val) {
    TNl(s, ind);
    s += "str ";
    EscapeString(s, val, '"');
}

inline void TreeLine(string &s, int ind, string_view text) {
    TNl(s, ind);
    s += text;
}

inline void TreeExpr(string &s, const Node *n, int ind);
inline void TreeStmt(string &s, const Node *n, int ind);
inline void TreeBlockBody(string &s, const Block *b, int ind);

inline void TreeNameLine(string &s, int ind, string_view text, bool byref = false) {
    TNl(s, ind);
    s += "name ";
    s += text;
    if (byref) s += " ref";
}


inline void TreeLitType(string &s, const TypeExpr *t, int ind) {
    string_view base;
    const vector<TypeExpr *> *args = nullptr;
    auto adt = t;
    if (adt->kind == TY_VARIANT && adt->var) adt = adt->var->adt;
    if ((adt->kind == TY_UNRESOLVED || adt->kind == TY_GENERIC) && adt->named) {
        base = adt->named->name;
        args = &adt->named->args;
        if (t->kind == TY_VARIANT && t->var) {
            TNl(s, ind);
            s += "name ";
            s += base;
            s += ".";
            s += t->var->name;
            base = {};
        }
    }
    if (!base.empty()) TreeNameLine(s, ind, base);
    if (args && !args->empty()) {
        string t2 = "<";
        for (size_t i = 0; i < args->size(); i++) {
            if (i) t2 += ", ";
            TreeTypeCompact(t2, (*args)[i]);
        }
        t2 += ">";
        TreeRawLine(s, ind, t2);
    }
}

inline void TreeBlock(string &s, const Block *b, int ind) {
    TreeLine(s, ind, "block");
    TreeBlockBody(s, b, ind);
}

inline void TreeStmtInto(string &s, const Node *st, int ind) {
    auto b = Is<Block>(st);
    auto ma = b ? MultiAssignOf(b) : nullptr;
    if (ma) {
        TNl(s, ind);
        s += "assign = multi";
        for (size_t i = 1; i < b->stmts.size(); i++)
            TreeExpr(s, Is<Assign>(b->stmts[i])->lval, ind + 1);
        TreeExpr(s, ma->inits[0], ind + 1);
        return;
    }
    TreeStmt(s, st, ind);
}

inline void TreeBlockBody(string &s, const Block *b, int ind) {
    for (auto st : b->stmts) TreeStmtInto(s, st, ind + 1);
    if (b->tail) {
        TNl(s, ind + 1);
        s += "expr tail";
        TreeExpr(s, b->tail, ind + 2);
    }
}


inline void TreeFieldType(string &s, const Field &f) {
    TreeTypeCompact(s, f.type);
}

inline void TreeField(string &s, int ind, const Field &f) {
    string t;
    if (f.ispad) {
        t = "pad";
        if (f.padsize >= 0) Append(t, " ", f.padsize);
    } else {
        auto sugar = f.isconst && f.type->cq;
        if (f.isconst) t += sugar ? "const " : "let ";
        t += f.name;
        t += ": ";
        if (sugar) {
            TypeExpr plain = *f.type;
            plain.cq = false;
            plain.Dump(t, (size_t)-1);
        } else {
            TreeFieldType(t, f);
        }
    }
    TNl(s, ind);
    s += "raw ";
    TreeCompact(s, t);
    if (f.defaultval) TreeExpr(s, f.defaultval, ind + 1);
}

inline void TreeEnumFields(int ind, const vector<Field> &fs, string &t) {
    for (size_t i = 0; i < fs.size(); i++) {
        if (i) t += ", ";
        auto &f = fs[i];
        if (f.ispad) {
            t += "pad";
            if (f.padsize >= 0) Append(t, " ", f.padsize);
            continue;
        }
        auto sugar = f.isconst && f.type->cq;
        if (f.isconst) t += sugar ? "const " : "let ";
        t += f.name;
        t += ": ";
        if (sugar) {
            TypeExpr plain = *f.type;
            plain.cq = false;
            plain.Dump(t, (size_t)-1);
        } else {
            string tmp;
            TreeFieldType(tmp, f);
            t += tmp;
        }
    }
}


inline void TreeParams(string &s, const vector<Param> &ps, int ind) {
    for (auto &p : ps) {
        TNl(s, ind);
        Append(s, "param ", p.name);
        if (p.isvar) s += " var";
        if (p.type) TreeTypeRaw(s, ind + 1, p.type);
        if (p.defaultval) TreeExpr(s, p.defaultval, ind + 1);
    }
}

inline void TreeFn(string &s, const FnDecl *fd, int ind) {
    auto sf = fd->sf;
    TNl(s, ind);
    Append(s, "fn ", sf->name);
    if (sf->isexport) s += " export";
    if (sf->isextern) s += " extern";
    if (sf->isinline) s += " inline";
    if (sf->checkfree) s += " checkfree";
    if (sf->issimd) s += " simd";
    if (sf->isrec) s += " recursive";
    if (sf->isthread) s += " thread_fn";
    TreeGenerics(s, sf->generics, ind + 1);
    TreeParams(s, sf->params, ind + 1);
    if (sf->has_rets) {
        string t;
        for (size_t i = 0; i < sf->rets.size(); i++) {
            if (i) t += ", ";
            TreeTypeCompact(t, sf->rets[i]);
        }
        TreeRawLine(s, ind + 1, t);
    }
    if (!sf->cname.empty() && sf->cname != sf->name) TreeStrLine(s, ind + 1, sf->cname);
    if (sf->body) TreeBlock(s, sf->body, ind + 1);
}

inline void TreeStruct(string &s, const StructDecl *sd, int ind) {
    TNl(s, ind);
    Append(s, "struct ", sd->st->name);
    if (sd->st->isroot) s += " root";
    TreeGenerics(s, sd->st->generics, ind + 1);
    for (auto &f : sd->st->fields) TreeField(s, ind + 1, f);
}

inline void TreeEnum(string &s, const EnumDecl *ed, int ind) {
    TNl(s, ind);
    Append(s, "enum ", ed->en->name);
    TreeGenerics(s, ed->en->generics, ind + 1);
    for (auto &v : ed->en->variants) {
        string t = string(v.name);
        if (v.has_payload) {
            t += " { ";
            TreeEnumFields(ind, v.fields, t);
            t += " }";
        }
        TreeRawLine(s, ind + 1, t);
    }
}

inline void TreeVarDecl(string &s, const VarDecl *vd, int ind) {
    TNl(s, ind);
    s += "let";
    if (vd->isvar) s += " var";
    if (vd->byref) s += " bind";
    if (vd->isconst) s += " const";
    if (vd->reusable == RU_SLOTS) s += " reusable";
    if (vd->reusable == RU_SLICES) s += " reusable[]";
    if (!vd->names.empty()) Append(s, " ", vd->names[0]);
    for (auto &nm : vd->names) TreeNameLine(s, ind + 1, nm);
    if (vd->type) TreeTypeRaw(s, ind + 1, vd->type);
    for (auto init : vd->inits) TreeExpr(s, init, ind + 1);
}

inline void TreeTrailing(string &s, const FunVal *fv, int ind) {
    if (fv->explicit_params) {
        TreeLine(s, ind, "closure");
        TreeParams(s, fv->params, ind + 1);
        TreeBlock(s, fv->body, ind + 1);
    } else {
        TreeBlock(s, fv->body, ind);
    }
}

inline void TreeCall(string &s, const Call *c, int ind) {
    TreeLine(s, ind, "call");
    TreeExpr(s, c->callee, ind + 1);
    if (!c->tyargs.empty()) {
        string t = "<";
        for (size_t i = 0; i < c->tyargs.size(); i++) {
            if (i) t += ", ";
            TreeTypeCompact(t, c->tyargs[i]);
        }
        t += ">";
        TreeRawLine(s, ind + 1, t);
    }
    for (auto a : c->args) TreeExpr(s, a, ind + 1);
    if (c->trailing) TreeTrailing(s, c->trailing, ind + 1);
}

inline void TreeArm(string &s, const MatchArm &arm, int ind) {
    TNl(s, ind);
    s += "arm";
    if (arm.pat.kind == P_WILDCARD) s += " wild";
    if (!arm.pat.binder.empty()) s += " bind";
    if (arm.pat.byref) s += " byref";
    if (arm.pat.kind == P_LIST) {
        for (auto &pi : arm.pat.items) {
            if (pi.hi) {
                TreeLine(s, ind + 1, "range ..");
                TreeExpr(s, pi.lo, ind + 2);
                TreeExpr(s, pi.hi, ind + 2);
            } else {
                TreeExpr(s, pi.lo, ind + 1);
            }
        }
        if (!arm.pat.binder.empty()) TreeNameLine(s, ind + 1, arm.pat.binder);
    }
    if (arm.body) TreeExpr(s, arm.body, ind + 1);
}

inline void TreeExpr(string &s, const Node *n, int ind) {
    switch (n->nkind) {
        case NK_IntLit: {
            TNl(s, ind);
            Append(s, "int ", (int64_t)Is<IntLit>(n)->val);
            return;
        }
        case NK_FltLit: {
            TNl(s, ind);
            s += "float ";
            CatOne(s, Is<FltLit>(n)->val);
            return;
        }
        case NK_BoolLit: {
            TreeNameLine(s, ind, Is<BoolLit>(n)->val ? "true" : "false");
            return;
        }
        case NK_StrLit: {
            TreeStrLine(s, ind, Is<StrLit>(n)->val);
            return;
        }
        case NK_Ident: {
            TreeNameLine(s, ind, Is<Ident>(n)->name);
            return;
        }
        case NK_ArrayLit: {
            auto a = Is<ArrayLit>(n);
            TNl(s, ind);
            s += "arr";
            if (a->capexpr) s += " cap";
            if (a->fillval) s += " fill";
            if (a->capexpr) TreeExpr(s, a->capexpr, ind + 1);
            if (a->fillval) { TreeExpr(s, a->fillval, ind + 1); TreeExpr(s, a->fillcount, ind + 1); }
            for (auto e : a->elems) TreeExpr(s, e, ind + 1);
            return;
        }
        case NK_StructLit: {
            auto sl = Is<StructLit>(n);
            TNl(s, ind);
            s += "lit";
            if (sl->defaultall) s += " ..";
            TreeLitType(s, sl->type, ind + 1);
            for (auto &fi : sl->inits) {
                TNl(s, ind + 1);
                if (fi.name.empty()) s += "fld";
                else Append(s, "fld ", fi.name);
                TreeExpr(s, fi.val, ind + 2);
            }
            return;
        }
        case NK_Unary: {
            TNl(s, ind);
            Append(s, "un ", TName(Is<Unary>(n)->op));
            TreeExpr(s, Is<Unary>(n)->child, ind + 1);
            return;
        }
        case NK_Binary: {
            auto b = Is<Binary>(n);
            TNl(s, ind);
            Append(s, "bin ", TName(b->op));
            TreeExpr(s, b->left, ind + 1);
            TreeExpr(s, b->right, ind + 1);
            return;
        }
        case NK_Dot: {
            auto d = Is<Dot>(n);
            TNl(s, ind);
            s += "field ";
            s += d->name;
            TreeExpr(s, d->obj, ind + 1);
            return;
        }
        case NK_Call: {
            TreeCall(s, Is<Call>(n), ind);
            return;
        }
        case NK_Index: {
            auto ix = Is<Index>(n);
            TreeLine(s, ind, "index");
            TreeExpr(s, ix->obj, ind + 1);
            TreeExpr(s, ix->idx, ind + 1);
            return;
        }
        case NK_SliceExpr: {
            auto sl = Is<SliceExpr>(n);
            TNl(s, ind);
            s += "index";
            if (sl->lo_from_end) s += " loref";
            if (sl->hi_from_end) s += " hiref";
            TreeExpr(s, sl->obj, ind + 1);
            TreeLine(s, ind + 1, "range ..");
            if (sl->lo) TreeExpr(s, sl->lo, ind + 2);
            if (sl->hi) TreeExpr(s, sl->hi, ind + 2);
            return;
        }
        case NK_AsCast: {
            auto ac = Is<AsCast>(n);
            TNl(s, ind);
            s += ac->unchecked ? "as!" : "as";
            TreeExpr(s, ac->child, ind + 1);
            TreeTypeRaw(s, ind + 1, ac->type);
            return;
        }
        case NK_NullLit:  TreeNameLine(s, ind, "null"); return;
        case NK_SelfRef:  TreeNameLine(s, ind, "self"); return;
        case NK_RangeExpr: {
            auto r = Is<RangeExpr>(n);
            TreeLine(s, ind, "range ..");
            TreeExpr(s, r->lo, ind + 1);
            TreeExpr(s, r->hi, ind + 1);
            return;
        }
        case NK_Block:            TreeBlock(s, Is<Block>(n), ind); return;
        case NK_EarlyBlock: {
            TNl(s, ind);
            s += "block early";
            TreeBlockBody(s, Is<EarlyBlock>(n)->body, ind);
            return;
        }
        case NK_IfExpr: {
            auto e = Is<IfExpr>(n);
            TreeLine(s, ind, "if");
            TreeExpr(s, e->cond, ind + 1);
            TreeBlock(s, e->thenb, ind + 1);
            if (e->elseb) TreeExpr(s, e->elseb, ind + 1);
            return;
        }
        case NK_MatchExpr: {
            auto m = Is<MatchExpr>(n);
            TreeLine(s, ind, "match");
            TreeExpr(s, m->scrutinee, ind + 1);
            for (auto &arm : m->arms) TreeArm(s, arm, ind + 1);
            return;
        }
        case NK_While: {
            auto w = Is<While>(n);
            TreeLine(s, ind, "while");
            TreeExpr(s, w->cond, ind + 1);
            TreeBlock(s, w->body, ind + 1);
            return;
        }
        case NK_LoopExpr: {
            TreeLine(s, ind, "loop");
            TreeBlock(s, Is<LoopExpr>(n)->body, ind + 1);
            return;
        }
        case NK_ForLoop: {
            auto f = Is<ForLoop>(n);
            TreeLine(s, ind, "for");
            TreeNameLine(s, ind + 1, f->var, f->byref);
            if (f->vartype) TreeTypeRaw(s, ind + 2, f->vartype);
            if (!f->idxvar.empty()) {
                TreeNameLine(s, ind + 1, f->idxvar);
                if (f->idxtype) TreeTypeRaw(s, ind + 2, f->idxtype);
            }
            TreeExpr(s, f->iter, ind + 1);
            TreeBlock(s, f->body, ind + 1);
            return;
        }
        case NK_Return: {
            auto r = Is<Return>(n);
            TreeLine(s, ind, "return");
            for (auto v : r->vals) TreeExpr(s, v, ind + 1);
            if (!r->from.empty()) TreeNameLine(s, ind + 1, r->from);
            return;
        }
        case NK_Break: {
            auto br = Is<Break>(n);
            TreeLine(s, ind, "break");
            if (br->val) TreeExpr(s, br->val, ind + 1);
            return;
        }
        case NK_Continue: TreeLine(s, ind, "continue"); return;
        case NK_Defer: {
            auto d = Is<Defer>(n);
            auto body = d->body;
            TreeLine(s, ind, "defer");
            if (body->stmts.size() == 1 && !body->tail && !Is<Block>(body->stmts[0])) {
                TreeStmtInto(s, body->stmts[0], ind + 1);
            } else {
                TreeBlock(s, body, ind + 1);
            }
            return;
        }
        case NK_FunVal:  TreeTrailing(s, Is<FunVal>(n), ind); return;
        case NK_FnDecl:  TreeFn(s, Is<FnDecl>(n), ind); return;
        case NK_VarDecl: TreeVarDecl(s, Is<VarDecl>(n), ind); return;
        case NK_Assign: {
            auto a = Is<Assign>(n);
            TNl(s, ind);
            Append(s, "assign ", TName(a->op));
            TreeExpr(s, a->lval, ind + 1);
            TreeExpr(s, a->rhs, ind + 1);
            return;
        }
        case NK_IncDec: {
            auto d = Is<IncDec>(n);
            TNl(s, ind);
            Append(s, "assign ", TName(d->op), " inc");
            TreeExpr(s, d->lval, ind + 1);
            return;
        }
        default: {
            TNl(s, ind);
            Append(s, "!! unsupported-expr ", (int)n->nkind);
            return;
        }
    }
}


inline void TreeStmt(string &s, const Node *n, int ind) {
    switch (n->nkind) {
        case NK_VarDecl:  TreeVarDecl(s, Is<VarDecl>(n), ind); return;
        case NK_FnDecl:   TreeFn(s, Is<FnDecl>(n), ind); return;
        case NK_StructDecl: TreeStruct(s, Is<StructDecl>(n), ind); return;
        case NK_EnumDecl: TreeEnum(s, Is<EnumDecl>(n), ind); return;
        case NK_AliasDecl: {
            auto al = Is<AliasDecl>(n);
            TNl(s, ind);
            Append(s, "alias ", al->al->name);
            TreeTypeRaw(s, ind + 1, al->al->type);
            return;
        }
        case NK_Assign:   TreeExpr(s, n, ind); return;
        case NK_IncDec:   TreeExpr(s, n, ind); return;
        case NK_Defer:    TreeExpr(s, n, ind); return;
        case NK_ConstFor: case NK_ConstIf: case NK_ConstQuery: case NK_InlineBlock: {
            TNl(s, ind);
            Append(s, "!! unsupported-stmt ", (int)n->nkind);
            return;
        }
        default: {
            TNl(s, ind);
            s += "expr";
            TreeExpr(s, n, ind + 1);
            return;
        }
    }
}

inline void TreeDump(Ast &ast, string &s) {
    TreeLine(s, 0, "file");
    for (auto d : ast.topdecls) TreeStmt(s, d, 1);
}

}
