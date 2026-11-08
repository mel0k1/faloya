


#pragma once

namespace faloya {




inline void CodeGen::GenBlockInner(Block *b, Dst d, size_t first, int exitscope) {
    auto region = MarkBlockBegin();
    for (auto i = first; i < b->stmts.size(); ++i) GenStmt(b->stmts[i]);
    if (b->tail && !IsVoidT(b->tail->exprtype) && d.k != DK_DISCARD) {
        
        
        if (exitscope >= 0) GenExitValue(b->tail, exitscope);
        else GenAny(b->tail, d);
    }
    else if (b->tail) GenAny(b->tail, Dst {});
    MarkLoopEnd(region);
}






inline size_t CodeGen::GenInlineArgs(Block *b) {
    size_t n = 0;
    while (n < b->stmts.size()) {
        auto arg = Is<VarDecl>(b->stmts[n]);
        if (!arg || !arg->inline_arg) break;
        GenStmt2(arg);
        n++;
    }
    return n;
}

inline void CodeGen::GenStmt(Node *n) {
    PushSc(SC_STMT);
    termjump = false;
    GenStmt2(n);
    if (termjump) {   
        cscopes.back().saves.clear();
        cscopes.back().defers.clear();
    }
    PopSc();
    termjump = false;
}













inline CodeGen::OpenedLoop CodeGen::OpenLoop(Block *bodyb, Node *cond,
                                             const vector<const VarDef *> &binders,
                                             const Dst &d) {
    OpenedLoop lp;
    lp.divisors = HoistDivisors(bodyb, cond, binders);
    PushSc(SC_LOOP);
    lp.si = (int)cscopes.size() - 1;
    cscopes[lp.si].brklbl = Lbl();
    cscopes[lp.si].cntlbl = Lbl();
    EnterDst(lp.si, d);
    lp.loopid = MarkLoopBegin();
    return lp;
}



inline void CodeGen::CloseLoop(const OpenedLoop &lp) {
    assert(lp.si == (int)cscopes.size() - 1);
    auto usedbrk = cscopes[lp.si].usedbrk;
    auto brklbl = cscopes[lp.si].brklbl;
    cscopes[lp.si].saves.clear();   
    cscopes[lp.si].defers.clear();
    PopSc();
    if (usedbrk) L(brklbl, ":;");
    MarkLoopEnd(lp.loopid);
    for (auto &k : lp.divisors) divmagic.erase(k);
    termjump = false;
}

inline void CodeGen::GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d,
                                 const string &forhead, Node *cond, size_t first,
                                 vector<const VarDef *> binders) {
    auto lp = OpenLoop(bodyb, cond, binders, d);
    L(forhead.empty() ? "for (;;) {" : forhead);
    ind++;
    
    
    if (condexit) condexit();
    cscopes[lp.si].straight = StraightCode(bodyb) && (!cond || StraightCode(cond));
    GenBodyCopy(bodyb, lp.si, first);
    ind--;
    L("}");
    CloseLoop(lp);
}




inline bool CodeGen::StraightCode(Node *n) {
    if (!n) return true;
    if (Is<While>(n) || Is<ForLoop>(n) || Is<LoopExpr>(n) || Is<Call>(n) || Is<Break>(n) ||
        Is<Continue>(n) || Is<Return>(n))
        return false;
    auto ok = true;
    n->Children([&](Node *ch) { ok = ok && StraightCode(ch); });
    return ok;
}




inline bool CodeGen::CallsRecursive(Node *n) {
    if (!n) return false;
    if (auto c = Is<Call>(n)) {
        if (c->spec && c->spec->sf && c->spec->sf->isrec) return true;
        for (auto s : c->dispatch)
            if (s && s->sf && s->sf->isrec) return true;
    }
    auto found = false;
    RunChildren(n, [&](Node *ch) { found = found || CallsRecursive(ch); });
    return found;
}



inline bool CodeGen::InStraightLoop() {
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (cscopes[i].kind == SC_LOOP) return cscopes[i].straight;
        if (cscopes[i].kind == SC_FN) return false;
    }
    return false;
}




inline bool CodeGen::AsDivision(Node *n, Division &dv) {
    if (auto b = Is<Binary>(n); b && (b->op == T_DIV || b->op == T_MOD)) {
        dv = { b->op, b->left->exprtype, b->right, b->nonneg };
        return true;
    }
    if (auto a = Is<Assign>(n); a && (a->op == T_DIVEQ || a->op == T_MODEQ)) {
        auto t = a->lval->exprtype;
        if (a->pointee && t && t->kind == TY_REF) t = t->ref->sub;
        dv = { a->op == T_DIVEQ ? T_DIV : T_MOD, t, a->rhs, false };
        return true;
    }
    return false;
}




inline bool CodeGen::LoopDivisible(const Division &dv) {
    auto t = dv.t;
    if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT) return false;
    if (!IsUnsigned(t->intstorage) && !dv.nonneg) return false;
    auto rt = dv.divisor->exprtype;
    return rt && rt->kind == TY_INT && !DivisorKey(dv.divisor).empty();
}




inline bool CodeGen::NarrowDivision(const Division &dv) {
    return IntBits(dv.t->intstorage) <= 32;
}




inline string CodeGen::DivisionKey(const Division &dv, const VarDef **root, bool *path) {
    auto k = DivisorKey(dv.divisor, root, path);
    return NarrowDivision(dv) ? cat(k, " u32") : k;
}





inline string CodeGen::FixedDivX(const Division &dv, const string &l,
                                 const function<string()> &plain, Line line) {
    auto key = LoopDivisible(dv) ? DivisionKey(dv) : string();
    auto dm = key.empty() ? divmagic.end() : divmagic.find(key);
    if (dm == divmagic.end()) return plain();
    auto &mg = dm->second;
    auto ct = CT(dv.t);
    auto lc = cat("(", ct, ")(", l, ")");
    auto w = mg.narrow ? "uint32_t" : "uint64_t";
    auto q = cat(mg.narrow ? "gs_divu32_q(" : "gs_divu_q(", "(", w, ")(", lc, "), ", mg.magic, ", ",
                 mg.more, ")");
    auto fast = dv.op == T_DIV ? q : cat("(", w, ")(", lc, ") - ", q, " * ", mg.val);
    
    
    
    
    
    auto slow = mg.zeronone ? cat("(gs_divfail(", LocArgs(line), "), 0)") : plain();
    return cat("(", ct, ")(", mg.more, " != GS_DIVU_NONE ? ", fast, " : ", slow, ")");
}





inline string CodeGen::DivisorKey(Node *n, const VarDef **root, bool *path) {
    auto isint = [](TypeExpr *t) { return t && t->kind == TY_INT && t->intstorage != IS_VARINT; };
    if (!n || fillvalues.count(n)) return "";
    if (auto id = Is<Ident>(n)) {
        auto vd = id->vdef;
        if (!vd || !vd->type) return "";
        if (!isint(vd->type) && (vd->type->kind != TY_STRUCT || !IsFix(vd->type))) return "";
        if (root) *root = vd;
        if (path) *path = false;
        return cat("v", (uintptr_t)vd);
    }
    if (auto d = Is<Dot>(n)) {
        auto ot = d->obj->exprtype;
        if (!d->IsField() || !ot || ot->kind != TY_STRUCT || !IsFix(ot)) return "";
        auto k = DivisorKey(d->obj, root, path);
        if (k.empty()) return "";
        if (path) *path = true;
        return cat(k, ".", d->fieldidx);
    }
    if (auto c = Is<AsCast>(n)) {
        
        
        if (!isint(c->totype) || !isint(c->child->exprtype) || c->totype->ix) return "";
        auto k = DivisorKey(c->child, root, path);
        if (k.empty()) return "";
        return cat("(", k, c->unchecked ? " as! " : " as ", IntStorageName(c->totype->intstorage),
                   ")");
    }
    return "";
}




inline string CodeGen::DivisorValue(Node *n, string &ok) {
    auto c = Is<AsCast>(n);
    if (!c || c->totype->ix) return GenX(n);
    auto x = DivisorValue(c->child, ok);
    auto st = c->child->exprtype;
    auto is = c->totype->intstorage;
    string cond;
    if (!c->unchecked && !TEq(st, c->totype)) {
        
        auto [lo, hi] = IntRange(is);
        if (st->intstorage == IS_U64) {
            if (is != IS_U64) cond = cat("(uint64_t)(", x, ") <= ", (uint64_t)hi, "ULL");
        } else {
            auto [slo, shi] = IntRange(st->intstorage);
            if (slo < lo) cond = cat("(int64_t)(", x, ") >= ", IntStr(lo));
            if (shi > hi)
                cond = cat(cond, cond.empty() ? "" : " && ", "(int64_t)(", x, ") <= ", IntStr(hi));
        }
    }
    if (!cond.empty()) ok = ok == "1" ? cond : cat(ok, " && ", cond);
    return cat("(", IntCT(is), ")(", x, ")");
}








inline const set<const VarDef *> &CodeGen::RefdLocals() {
    assert(curspec);
    if (refdspec == curspec) return refdlocals;
    refdspec = curspec;
    refdlocals.clear();
    function<void(Node *)> all = [&](Node *n) {
        if (!n) return;
        if (auto id = Is<Ident>(n); id && id->vdef) refdlocals.insert(id->vdef);
        RunChildren(n, all);
    };
    auto mark = [&](Node *n) {
        while (n) {
            if (auto id = Is<Ident>(n)) {
                if (id->vdef) refdlocals.insert(id->vdef);
                return;
            }
            if (auto d = Is<Dot>(n)) n = d->obj;
            else if (auto ix = Is<Index>(n)) n = ix->obj;
            else if (auto sl = Is<SliceExpr>(n)) n = sl->obj;
            else if (auto u = Is<Unary>(n)) n = u->child;
            else return all(n);
        }
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) mark(u->child);
        if (auto vd = Is<VarDecl>(n); vd && vd->byref) for (auto i : vd->inits) mark(i);
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN) mark(a->rhs);
        if (auto fl = Is<ForLoop>(n);
            fl && (fl->iterkind == IK_ARRAY || fl->iterkind == IK_SLICE) &&
            !(fl->vdef && fl->vdef->copybind))
            mark(fl->iter);
        if (auto me = Is<MatchExpr>(n))
            for (auto &arm : me->arms) if (arm.pat.byref) mark(me->scrutinee);
        if (auto c = Is<Call>(n)) {
            if (!c->fmtspecs.empty()) for (auto a : c->args) all(a);
            for (size_t i = 0; i < c->fvparams.size() && i < c->args.size(); i++) {
                auto pt = c->fvparams[i]->type;
                if (pt && (pt->kind == TY_REF || pt->kind == TY_SLICE)) mark(c->args[i]);
            }
        }
        RunChildren(n, walk);
    };
    walk(curspec->body);
    return refdlocals;
}



















inline vector<string> CodeGen::HoistDivisors(Block *loopbody, Node *cond,
                                             const vector<const VarDef *> &binders) {
    vector<string> added;
    set<const VarDef *> inner(binders.begin(), binders.end());
    vector<Division> divs;
    function<void(Node *)> all = [&](Node *n) {
        if (!n) return;
        if (auto id = Is<Ident>(n); id && id->vdef) inner.insert(id->vdef);
        RunChildren(n, all);
    };
    
    
    auto written = [&](Node *n) {
        while (n) {
            if (auto id = Is<Ident>(n)) {
                if (id->vdef) inner.insert(id->vdef);
                return;
            }
            if (auto d = Is<Dot>(n)) n = d->obj;
            else if (auto ix = Is<Index>(n)) n = ix->obj;
            else if (auto sl = Is<SliceExpr>(n)) n = sl->obj;
            else if (auto u = Is<Unary>(n)) n = u->child;
            else return all(n);
        }
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || Is<FnDecl>(n) || Is<FunVal>(n)) return;
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) inner.insert(d);
        if (auto a = Is<Assign>(n)) written(a->lval);
        if (auto a = Is<IncDec>(n)) written(a->lval);
        if (auto fl = Is<ForLoop>(n)) { inner.insert(fl->vdef); inner.insert(fl->idxdef); }
        if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) inner.insert(arm.binder);
        if (auto c = Is<Call>(n)) {
            for (auto p : c->fvparams) inner.insert(p);
            if (c->builtin >= 0 && (BuiltinByKind(c->builtin).flags & BF_WRITE))
                written(c->FirstArg());
        }
        if (Division dv; AsDivision(n, dv) && LoopDivisible(dv)) divs.push_back(dv);
        RunChildren(n, walk);
    };
    walk(loopbody);
    walk(cond);
    for (auto &dv : divs) {
        const VarDef *root = nullptr;
        auto path = false;
        auto key = DivisionKey(dv, &root, &path);
        if (divmagic.count(key) || inner.count(root) || root->captured || root->refwrite ||
            nrvo.count(root))
            continue;
        if (root->isglobal || !curspec) {
            if (path || root->isvar || !(root->isglobal ? gnames : vnames).count(root)) continue;
        } else if (!vnames.count(root) || RefdLocals().count(root)) {
            continue;
        }
        auto ok = string("1");
        auto x = DivisorValue(dv.divisor, ok);
        auto v = T(), m = T(), more = T();
        auto narrow = NarrowDivision(dv);
        auto w = narrow ? "uint32_t" : "uint64_t";
        auto gen = narrow ? "gs_divu32_gen(" : "gs_divu_gen(";
        L("uint8_t ", more, ";");
        L(w, " ", v, " = (", w, ")(", x, ");");
        if (ok == "1") L(w, " ", m, " = ", gen, v, ", &", more, ");");
        else L(w, " ", m, " = (", ok, ") ? ", gen, v, ", &", more, ") : (", more,
               " = GS_DIVU_NONE, 0);");
        divmagic[key] = { v, m, more, narrow, narrow && ok == "1" };
        added.push_back(key);
    }
    return added;
}

inline void CodeGen::GenBreakPath(Node *val) {
    auto si = -1;
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (cscopes[i].kind == SC_LOOP || cscopes[i].kind == SC_BLOCK) { si = i; break; }
        if (cscopes[i].kind == SC_FN) break;
    }
    assert(si >= 0);
    if (val) GenExitValue(val, si);
    EmitExitRestores(si);
    cscopes[si].usedbrk = true;
    L("goto ", cscopes[si].brklbl, ";");
    termjump = true;
}







inline bool CodeGen::DupLocal(VarDef *v) {
    return !v || (v->type && IsFix(v->type) && !IsLargeFixed(v->type) && !v->captured &&
                  !v->reusable && !nrvo.count(v));
}




inline bool CodeGen::Dupable(Node *n) {
    if (!n) return true;
    if (Is<FunVal>(n) || Is<FnDecl>(n)) return false;
    auto path = Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n);
    auto t = n->exprtype;
    if (!path && t && t->kind != TY_VOID &&
        (t->kind == TY_FN || !IsFix(t) || IsLargeFixed(t)))
        return false;
    auto ok = true;
    if (auto vd = Is<VarDecl>(n)) for (auto v : vd->defs) ok = ok && DupLocal(v);
    if (auto fl = Is<ForLoop>(n)) ok = DupLocal(fl->vdef) && DupLocal(fl->idxdef);
    if (auto me = Is<MatchExpr>(n)) for (auto &arm : me->arms) ok = ok && DupLocal(arm.binder);
    if (auto c = Is<Call>(n)) for (auto p : c->fvparams) ok = ok && DupLocal(p);
    RunChildren(n, [&](Node *ch) { ok = ok && Dupable(ch); });
    return ok;
}




inline int CodeGen::BlockSize(ForLoop *f) {
    if (!f->stripk && !f->sumred) return 0;
    if (!DupLocal(f->vdef) || !DupLocal(f->idxdef) || !Dupable(f->body)) return 0;
    return f->stripk ? f->stripk : SUMBLOCK;
}





inline string CodeGen::AtLeastLeft(TypeExpr *ct, const string &hi, const string &ctr, int k,
                                   bool fromzero) {
    if (IntBits(ct->intstorage) < 64) return cat("(int64_t)", hi, " - (int64_t)", ctr, " >= ", k);
    if (IsUnsigned(ct->intstorage)) return cat(ctr, " < ", hi, " && ", hi, " - ", ctr, " >= ", k);
    if (fromzero) return cat(hi, " - ", ctr, " >= ", k);
    return cat(ctr, " < ", hi, " && (uint64_t)", hi, " - (uint64_t)", ctr, " >= ", k);
}



inline void CodeGen::GenBodyCopy(Block *bodyb, int si, size_t first) {
    for (auto i = first; i < bodyb->stmts.size(); i++) GenStmt(bodyb->stmts[i]);
    if (bodyb->tail) GenStmt(bodyb->tail);
    EmitRestores(cscopes[si]);
    if (cscopes[si].usedcnt) L(cscopes[si].cntlbl, ":;");
}











inline void CodeGen::GenBlocked(ForLoop *f, int k, const string &more, const string &step,
                                const function<void()> &bind, const string &cond) {
    auto lp = OpenLoop(f->body, nullptr, { f->vdef, f->idxdef }, Dst {});
    auto si = lp.si;
    cscopes[si].straight = StraightCode(f->body);
    {
        NameScope ns(*this);
        L(f->stripk ? "/* loop shape: strip-mined by " : "/* loop shape: sum terms in blocks of ",
          k, " */");
        L("while (", more, ") {");
        ind++;
        auto l = T();
        auto inner = cat("for (int64_t ", l, " = 0; ", l, " < ", k, "; ", l, "++, ", step, ") {");
        if (f->stripk) {
            L(inner);
            ind++;
            bind();
            for (auto m : f->lanemods) substs[m] = cat("((", CT(m->exprtype), ")", l, ")");
            GenBodyCopy(f->body, si);
            for (auto m : f->lanemods) substs.erase(m);
            ind--;
            L("}");
        } else {
            auto a = Is<Assign>(f->body->stmts.empty() ? f->body->tail : f->body->stmts[0]);
            auto sum = GenLoc(a->lval);
            auto terms = T();
            L(CT(sum.t), " ", terms, "[", k, "];");
            L(inner);
            ind++;
            bind();
            PushSc(SC_STMT);
            termjump = false;
            auto r = GenX(a->rhs);
            L(terms, "[", l, "] = ", r, ";");
            PopSc();
            termjump = false;
            EmitRestores(cscopes[si]);
            ind--;
            L("}");
            auto l2 = T();
            L("for (int64_t ", l2, " = 0; ", l2, " < ", k, "; ", l2, "++) ", sum.s, " = ", sum.s,
              " + ", terms, "[", l2, "];");
        }
        ind--;
        L("}");
    }
    cscopes[si].saves.clear();
    cscopes[si].defers.clear();
    cscopes[si].usedcnt = false;
    cscopes[si].cntlbl = Lbl();
    L("for (; ", cond, "; ", step, ") {");
    ind++;
    bind();
    GenBodyCopy(f->body, si);
    ind--;
    L("}");
    CloseLoop(lp);
}














inline void CodeGen::EnterDst(int si, const Dst &d) {
    auto &s = cscopes[si];
    s.dst = d;
    if (d.k != DK_STACK) return;
    s.open0 = openat[d.s];
    s.topat = body.size();
    s.topind = ind;
}



inline void CodeGen::DeclareTop0(size_t at, int indent, const string &name, const string &stk) {
    string line((size_t)indent * 4, ' ');
    Append(line, "uint8_t *", name, " = ", Top(stk), ";\n");
    body.insert(at, line);
    for (auto &s : cscopes) if (s.topat > at) s.topat += line.size();
}

inline string CodeGen::ScopeTop0(int si) {
    auto &s = cscopes[si];
    if (s.top0.empty()) {
        s.top0 = T();
        DeclareTop0(s.topat, s.topind, s.top0, s.dst.s);
    }
    return s.top0;
}

inline string CodeGen::DstTop0(size_t i) {
    if (dsttop0.size() <= i) dsttop0.resize(i + 1);
    if (dsttop0[i].empty()) {
        dsttop0[i] = T();
        DeclareTop0(0, 1, dsttop0[i], cat("gs_dst", i));
    }
    return dsttop0[i];
}




inline const CodeGen::NrvoDest *CodeGen::NrvoBoundAt(Node *val, const string &stk,
                                                      const string &lenlv) {
    auto id = Is<Ident>(val);
    if (!id || !id->vdef) return nullptr;
    auto it = nrvo.find(id->vdef);
    if (it == nrvo.end() || !it->second.inlined || it->second.stk != stk ||
        it->second.lenlv != lenlv)
        return nullptr;
    return &it->second;
}




inline string CodeGen::ExitStart(Node *val, const string &stk, const string &lenlv, int open0) {
    if (openat[stk] <= open0 || NrvoBoundAt(val, stk, lenlv)) return "";
    auto start = T();
    L("uint8_t *", start, " = ", Top(stk), ";");
    return start;
}



inline void CodeGen::LandValue(const string &stk, const string &top0, const string &start,
                               TypeExpr *t, const string &lenlv) {
    L("if (", start, " != ", top0, ") {");
    ind++;
    auto n = T();
    L("int64_t ", n, " = (int64_t)(", Top(stk), " - ", start, ");");
    L("memmove(", top0, ", ", start, ", (size_t)", n, ");");
    L(TopW(stk), " = ", top0, " + ", n, ";");
    if (t && IsFrameObj(t)) {
        auto th = FoTailHdr(t, lenlv);
        L(th, ".base = ", top0, " + (", th, ".base - ", start, ");");
    }
    ind--;
    L("}");
}


inline void CodeGen::GenExitValue(Node *val, int si) {
    auto d = cscopes[si].dst;
    if (d.k == DK_BRANCH) return GenBranch(val, d.when, d.s, d.exitscope);
    if (d.k != DK_STACK || !NrvoBoundAt(val, d.s, d.lenlv)) NoSelfRelCopy(val);
    auto start = d.k == DK_STACK ? ExitStart(val, d.s, d.lenlv, cscopes[si].open0) : "";
    GenAny(val, d);
    if (!start.empty())
        LandValue(d.s, ScopeTop0(si), start, d.t ? d.t : val->exprtype, d.lenlv);
}




inline void CodeGen::BindLocal(VarDef *d, Node *init, bool forlocal) {
    
    
    if (refalias.count(d)) {
        aliasbound.insert(d);
        return;
    }
    auto name = LocalName(d);
    auto t = d->type;
    if (IsRoot(t)) {
        auto sl = Is<StructLit>(init);
        assert(sl && sl->sinst->st->isroot);
        L(CT(t), " ", name, " = { 0 };");
        for (size_t i = 0; i < d->components.size(); i++) {
            auto c = d->components[i];
            if (!c) continue;
            BindLocal(c, sl->InitFor((int)i), forlocal);
            Ident id(c->line, c->name);
            id.vdef = c;
            id.exprtype = c->type;
            auto ref = c->type->kind == TY_REF ? cat("&", VarLoc(c).s)
                                              : GenRefVal(&id, init->line);
            L(name, ".", Sanitize(sl->sinst->st->fields[i].name), " = ", ref, ";");
        }
        return;
    }
    if (IsResz(t) || IsBytesT(t)) {
        assert(init);
        auto dst = DeclStackLocal(d, forlocal);
        GenConstruct(init, dst.s, dst.t, dst.lenlv);
        return;
    }
    if (LargeFixedOnStack(t)) {
        FixedLocal(t, name, "", forlocal);
        vnames[d] = name;
        if (init) GenAny(init, Dst { DK_LVALUE, name, t });
        return;
    }
    if (!init) {
        L(VarCT(d), " ", name, ";");
        return;
    }
    if (PrefVar(d)) {
        L("gs_pref ", name, " = ", GenPrefVal(init), ";");
        return;
    }
    
    
    if (IsRelRef(t)) {
        L(CT(t), " ", name, ";");
        EmitRelStoreAt(cat("(uint8_t *)&", name), t, GenX(init), init->line, true,
                       RelValue(init));
        return;
    }
    
    
    if (IsCtl(init) || Is<Call>(init) || (Is<SliceExpr>(init) && t->kind == TY_SLICE) ||
        ((Is<StructLit>(init) || Is<ArrayLit>(init)) && HasRelRef(t))) {
        L(CT(t), " ", name, ";");
        GenAny(init, Dst { DK_LVALUE, name, t });
        return;
    }
    if (IsStaticLimited(t)) {
        L(CT(t), " ", name, ";");
        if (GenIntoLimited(init, t, name, false)) return;
        L(name, " = ", GenXD(init, t), ";");
        return;
    }
    auto ct = CT(t), x = GenXD(init, t);
    L(ct, " ", name, " = ", x, ";");
}





inline Dst CodeGen::DeclStackLocal(VarDef *d, bool forlocal) {
    auto name = LocalName(d);
    auto t = d->type;
    auto resz = IsResz(t);
    if (resz) EmitCoreTypes();
    string stk;
    auto nit = nrvo.find(d);
    if (nit != nrvo.end()) {
        auto &nd = nit->second;
        stk = nd.stk;
        if (resz) {
            
            
            
            
            if (nd.prefix) {
                nd.pref = T();
                L("uint8_t *", nd.pref, " = ", Top(stk), ";");
                Bump(stk, cat(PrefixBytes(nd.ls)));
            }
            nd.hdr = name;
        }
        
        if (!nd.inlined) openat[stk]++;
    } else {
        stk = AllocStk(forlocal);
    }
    vstk[d] = stk;
    
    
    auto own = nit == nrvo.end();
    if (!resz) {
        L("uint8_t *", name, " = ", Top(stk), ";");
        if (own) SaveBase(forlocal, stk, name);
        return Dst { DK_STACK, stk, t };
    }
    if (IsFrameObj(t)) {
        L(CT(t), " ", name, ";");
        if (own) SaveBase(forlocal, stk, cat(FoTailHdr(t, name), ".base"));
        return Dst { DK_STACK, stk, t, name };
    }
    L("gs_rhdr ", name, " = { ", Top(stk), ", 0 };");
    if (own) SaveBase(forlocal, stk, cat(name, ".base"));
    if (d->reusable) {
        
        auto flstk = AllocStk(forlocal);
        auto fln = Unique2(cat(name, "_fl"));
        L("gs_rhdr ", fln, " = { ", Top(flstk), ", 0 };");
        SaveBase(forlocal, flstk, cat(fln, ".base"));
        vpool[d] = { fln, flstk };
    }
    return Dst { DK_STACK, stk, t, cat(name, ".len") };
}




inline string CodeGen::GenPrefVal(Node *n) {
    if (IsCtl(n)) {
        auto t = T();
        L("gs_pref ", t, ";");
        GenAny(n, Dst { DK_LVALUE, t, n->exprtype, "", true });
        return t;
    }
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND) {
        if (auto id = Is<Ident>(u->child); id && id->vdef && id->vdef->reusable) {
            auto vd = id->vdef;
            auto pit = vpool.find(vd);
            auto git = gpools.find(vd);
            
            if (pit == vpool.end() && git == gpools.end()) return VName(vd);
            auto &pl = pit != vpool.end() ? pit->second : git->second;
            auto t = T();
            L("gs_pref ", t, " = { &", HdrLv(vd), ", ", VStkOf(vd), ", &", pl.first,
              ", ", pl.second, " };");
            return t;
        }
        auto lv = GenLoc(u->child);
        if (lv.t->kind != TY_REF || !lv.ispref)
            Fail(n->line, "cannot form a reusable pool reference here");
        return lv.s;
    }
    if (auto id = Is<Ident>(n); id && id->vdef && PrefVar(id->vdef)) {
        return fvptr.count(id->vdef) ? cat("(*", VName(id->vdef), ")")
                                     : VName(id->vdef);
    }
    Fail(n->line, "cannot form a reusable pool reference from this expression");
}

inline void CodeGen::GenRelAssign(Loc lv, Node *lval, Node *rhs, Line ln) {
    auto rv = GenX(rhs);
    
    assert(lv.t->ref->lenstorage != IS_VARINT);
    auto f = RelValue(rhs);
    if (auto d = Is<Dot>(lval); d && d->IsField()) {
        auto ht = d->obj->exprtype;
        if (ht && ht->kind == TY_REF) ht = ht->ref->sub;
        if (ht && ht->kind == TY_STRUCT && IsFix(ht))
            f.apart = RelSlotApart(lv.t, ht, StructLayout(SI(ht)).offs[(size_t)d->fieldidx]);
    }
    EmitRelStoreAt(BytesAddrOf(lv), lv.t, rv, ln, true, f);
}

inline void CodeGen::GenRebind(Assign *a, Loc lv) {
    if (IsRelRef(lv.t)) {
        GenRelAssign(lv, a->lval, a->rhs, a->line);
        return;
    }
    assert(lv.val);
    if (Is<NullLit>(a->rhs)) {
        if (IsResz(lv.t->ref->sub)) L("GS_MEMZERO(&", lv.s, ", sizeof(", lv.s, "));");
        else L(lv.s, " = NULL;");
        return;
    }
    L(lv.s, " = ", lv.ispref ? GenPrefVal(a->rhs) : GenX(a->rhs), ";");
}















inline vector<string> CodeGen::GenForward(Call *c, const vector<TypeExpr *> &rets,
                                          const vector<Dst> &chans) {
    vector<Dst> dsts;
    vector<Loc> arrived(rets.size());
    
    
    deque<OpenAt> open;
    for (size_t i = 0; i < rets.size(); i++) {
        auto ct = c->rettypes[i];
        if (TEq(ct, rets[i])) {
            auto d = chans[i];
            if (d.k == DK_DISCARD) {
                d = Dst { DK_LVALUE, T() };
                FixedLocal(rets[i], d.s);
            }
            dsts.push_back(d);
            continue;
        }
        if (ct->kind == TY_VARIANT && IsBytesT(ct)) {
            open.emplace_back(*this, chans[i].s);
            dsts.push_back(VariantBehindTag(ct, rets[i], chans[i]));
            continue;
        }
        auto &lv = arrived[i];
        string stk;
        if (IsResz(ct)) {
            auto h = RzTemp(ct, stk);
            lv = RzTempLoc(ct, h, stk);
            dsts.push_back(Dst { DK_STACK, stk, ct, RzLenLv(ct, h) });
        } else if (IsBytesT(ct)) {
            lv.t = ct;
            lv.s = BytesTemp(stk);
            lv.stk = stk;
            dsts.push_back(Dst { DK_STACK, stk, ct });
        } else {
            lv.t = ct;
            lv.val = true;
            lv.s = T();
            FixedLocal(ct, lv.s);
            dsts.push_back(Dst { DK_LVALUE, lv.s, ct });
        }
    }
    auto res = EmitCall(c, dsts[0], &dsts);
    open.clear();
    vector<string> vals(rets.size());
    for (size_t i = 0; i < rets.size(); i++) {
        auto rt = rets[i];
        auto r = i < res.size() ? res[i] : string();
        if (auto lv = arrived[i]; lv.t) {
            
            if (!IsResz(lv.t) && !r.empty()) lv.s = r;
            if (IsBytesT(rt)) ConstructFromLoc(lv, rt, chans[i].s, chans[i].lenlv, c->line);
            else vals[i] = LoadLoc(lv, rt, c->line);
        } else if (!IsBytesT(rt)) {
            vals[i] = !r.empty() ? r : dsts[i].s;
        }
    }
    return vals;
}

inline void CodeGen::GenNormalReturn(const vector<Node *> &vals) {
    auto sp = curspec;
    assert(sp);
    auto &si = *curinfo;
    string retv;
    
    if (vals.size() == 1 && sp->rets.size() > 1) {
        if (auto c = Is<Call>(vals[0]); c && c->rettypes.size() == sp->rets.size()) {
            vector<Dst> chans(sp->rets.size());
            vector<string> starts(sp->rets.size());
            for (size_t i = 0; i < sp->rets.size(); i++) {
                auto rt = sp->rets[i];
                auto dst = cat("gs_dst", i);
                if (IsResz(rt)) chans[i] = Dst { DK_STACK, dst, rt, cat("(*gs_rl", i, ")") };
                else if (IsBytesT(rt)) chans[i] = Dst { DK_STACK, dst };
                else if (NeedsResultLocation(rt))
                    chans[i] = Dst { DK_LVALUE, cat("(*gs_r", i, ")"), rt };
                else continue;
                if (IsBytesT(rt)) starts[i] = ExitStart(c, dst, "", 0);
            }
            auto fixed = GenForward(c, sp->rets, chans);
            for (size_t i = 0; i < sp->rets.size(); i++) {
                if (IsBytesT(sp->rets[i])) continue;
                if ((int)i == si.cret) retv = fixed[i];
                else if (!NeedsResultLocation(sp->rets[i])) L("*gs_r", i, " = ", fixed[i], ";");
            }
            for (size_t i = 0; i < sp->rets.size(); i++)
                if (!starts[i].empty())
                    LandValue(cat("gs_dst", i), DstTop0(i), starts[i], sp->rets[i],
                              cat("(*gs_rl", i, ")"));
            Epilogue(retv);
            return;
        }
    }
    assert(vals.size() == sp->rets.size());
    for (size_t i = 0; i < vals.size(); i++) {
        auto rt = sp->rets[i];
        auto id = Is<Ident>(vals[i]);
        auto named = id && id->vdef ? nrvo.find(id->vdef) : nrvo.end();
        auto dst = cat("gs_dst", i);
        
        
        auto direct = named != nrvo.end() && !named->second.inlined && named->second.stk == dst;
        if (IsResz(rt) || (emiter && i == 0)) {
            if (direct) {
                
                
                if (IsFrameObj(rt)) L("*gs_rl", i, " = ", HdrLv(id->vdef), ";");
                else L("*gs_rl", i, " = ", HdrLv(id->vdef), ".len;");
                continue;
            }
            NoSelfRelCopy(vals[i]);
            auto lenlv = cat("(*gs_rl", i, ")");
            auto start = ExitStart(vals[i], dst, lenlv, 0);
            GenConstruct(vals[i], dst, rt, lenlv);
            if (!start.empty()) LandValue(dst, DstTop0(i), start, rt, lenlv);
        } else if (IsBytesT(rt)) {
            if (direct) {
                
                
                
                if (IsResz(id->vdef->type)) EmitNrvoFinish(named->second);
                continue;
            }
            NoSelfRelCopy(vals[i]);
            auto start = ExitStart(vals[i], dst, "", 0);
            GenConstruct(vals[i], dst, rt);
            if (!start.empty()) LandValue(dst, DstTop0(i), start, rt, "");
        } else if (NeedsResultLocation(rt)) {
            GenAny(vals[i], Dst { DK_LVALUE, cat("(*gs_r", i, ")"), rt });
        } else if ((int)i == si.cret) {
            retv = T();
            L(CT(rt), " ", retv, ";");
            GenAny(vals[i], Dst { DK_LVALUE, retv, rt });
        } else {
            auto tv = T();
            FixedLocal(rt, tv);
            GenAny(vals[i], Dst { DK_LVALUE, tv, rt });
            L("*gs_r", i, " = ", tv, ";");
        }
    }
    Epilogue(retv);
}





inline void CodeGen::EmitPrefixPatch(const string &pref, IntStorage ls, const string &stk,
                                     const string &count, const string &elems) {
    if (ls != IS_VARINT) {
        EmitLenCheck(ls, count);
        L("*(", IntCT(ls), " *)", pref, " = (", IntCT(ls), ")(", count, ");");
        return;
    }
    L("if ((", count, ") < 128) {");
    ind++;
    L("*", pref, " = (uint8_t)(", count, ");");
    ind--;
    L("} else {");
    ind++;
    auto tmp = T(), ms = T();
    L("uint8_t ", tmp, "[10];");
    L("int64_t ", ms, " = gs_uleb_write(", tmp, ", (uint64_t)(", count, "));");
    L("memmove((", elems, ") + ", ms, " - 1, ", elems, ", (size_t)(", Top(stk), " - (",
      elems, ")));");
    L("GS_MEMCPY(", pref, ", ", tmp, ", (size_t)", ms, ");");
    L(TopW(stk), " += ", ms, " - 1;");
    ind--;
    L("}");
}




inline void CodeGen::EmitNrvoFinish(const NrvoDest &nd) {
    if (nd.hdr.empty()) return;   
    if (nd.fo) { L(nd.lenlv, " = ", nd.hdr, ";"); return; }
    if (!nd.prefix) { L(nd.lenlv, " = ", nd.hdr, ".len;"); return; }
    EmitPrefixPatch(nd.pref, nd.ls, nd.stk, cat(nd.hdr, ".len"), cat(nd.hdr, ".base"));
}

inline void CodeGen::Epilogue(const string &retv) {
    EmitExitRestores(0);
    MarkFlush();   
    for (auto &s : fdstsaves) L(s);
    L(retv.empty() ? "return;" : cat("return ", retv, ";"));
    termjump = true;
}




inline void CodeGen::PropagateReturn(const string &rfval) {
    EmitExitRestores(0);
    MarkFlush();
    for (auto &s : fdstsaves) L(s);
    if (!rfval.empty()) L("gs_rf = ", rfval, ";");
    if (curinfo->cret >= 0) {
        auto d = T();
        L(CT(curspec->rets[curinfo->cret]), " ", d, " = {0};");
        L("return ", d, ";");
    } else {
        L("return;");
    }
    termjump = true;
}



inline void CodeGen::GenFromReturn(Return *r) {
    auto t = r->targetspec;
    assert(t);
    auto tid = fromids[t];
    EnsureFromChannels(t);
    auto &rets = t->rets;
    assert(r->vals.size() == rets.size() ||
           (r->vals.size() == 1 && Is<Call>(r->vals[0])));
    
    
    
    for (size_t i = 0; i < rets.size(); i++)
        if (IsBytesT(rets[i]))
            L("gs_fval_", tid, "_", i, " = ", Top(cat("gs_fdst_", tid, "_", i)), ";");
    if (r->vals.size() == rets.size()) {
        for (size_t i = 0; i < rets.size(); i++) {
            if (IsResz(rets[i]))
                GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i],
                             cat("gs_lret_", tid, "_", i));
            else if (IsBytesT(rets[i]))
                GenConstruct(r->vals[i], cat("gs_fdst_", tid, "_", i), rets[i]);
            else {
                auto dst = cat("gs_lret_", tid, "_", i);
                if (NeedsResultLocation(rets[i])) dst = cat("(*", dst, ")");
                GenAny(r->vals[i], Dst { DK_LVALUE, dst, rets[i] });
            }
        }
    } else {
        
        vector<Dst> chans;
        for (size_t i = 0; i < rets.size(); i++) {
            auto stk = cat("gs_fdst_", tid, "_", i), lret = cat("gs_lret_", tid, "_", i);
            if (NeedsResultLocation(rets[i])) lret = cat("(*", lret, ")");
            if (IsResz(rets[i])) chans.push_back(Dst { DK_STACK, stk, rets[i], lret });
            else if (IsBytesT(rets[i])) chans.push_back(Dst { DK_STACK, stk });
            else chans.push_back(Dst { DK_LVALUE, lret, rets[i] });
        }
        auto fixed = GenForward(Is<Call>(r->vals[0]), rets, chans);
        for (size_t i = 0; i < rets.size(); i++)
            if (!fixed[i].empty() && fixed[i] != chans[i].s) L(chans[i].s, " = ", fixed[i], ";");
    }
    assert(curinfo && curinfo->hasrf);
    PropagateReturn(cat(tid));
}

inline void CodeGen::EnsureFromChannels(FnSpec *t) {
    if (fromemitted.count(t)) return;
    fromemitted.insert(t);
    auto tid = fromids[t];
    auto &rets = t->rets;
    for (size_t i = 0; i < rets.size(); i++) {
        if (IsFix(rets[i])) {
            Append(data, "static GS_TLS ", CT(rets[i]), NeedsResultLocation(rets[i]) ? " *" : " ",
                   "gs_lret_", tid, "_", i, ";\n");
            continue;
        }
        Append(data, "static GS_TLS gs_stack *gs_fdst_", tid, "_", i, ";\n");
        Append(data, "static GS_TLS uint8_t *gs_fval_", tid, "_", i, ";\n");
        if (IsResz(rets[i]))
            Append(data, "static GS_TLS ", IsFrameObj(rets[i]) ? CT(rets[i]) : string("int64_t"),
                   " gs_lret_", tid, "_", i, ";\n");
    }
}










inline string CodeGen::CallVal0(Call *c, const string &r0, TypeExpr *want) {
    auto rt = c->rettypes.empty() ? nullptr : c->rettypes[0];
    auto et = want ? want : c->exprtype;
    
    if (c->builtin == B_COPY) return r0;
    if (rt && et && IsStaticLimited(et)) {
        auto st = IsPlainRef(rt) ? rt->ref->sub : rt;
        if ((st->kind == TY_ARRAY || st->kind == TY_SLICE) && !TEq(st, et))
            return AdaptToFixed(CallResLoc(c, r0), et, c->line);
    }
    auto st = rt && IsPlainRef(rt) ? rt->ref->sub : rt;
    if (st && st->kind == TY_ARRAY && et && et->kind == TY_SLICE) {
        
        
        
        
        
        auto lv = CallResLoc(c, r0);
        if (lv.val && st == rt) {
            auto tv = T();
            FixedLocal(rt, tv, r0);
            lv.s = tv;
        }
        return LoadLoc(lv, et, c->line);
    }
    if (rt && rt->kind == TY_REF && rt->ref->lenstorage < 0 && et &&
        et->kind != TY_REF && et->kind != TY_VOID) {
        if (IsVarintT(rt->ref->sub)) return cat("gs_zig_read(", r0, ")");
        if (IsResz(rt->ref->sub) || IsBytesT(rt->ref->sub)) return r0;
        return PointeeLv(r0, rt->ref->sub);
    }
    return r0;
}




inline CodeGen::Loc CodeGen::CallResLoc(Call *c, const string &r0) {
    auto rt = c->rettypes[0];
    Loc lv;
    if (IsPlainRef(rt)) {
        auto sub = rt->ref->sub;
        if (IsResz(sub)) return FatRefLoc(r0, sub);
        if (IsBytesT(sub)) return BytesLoc(r0, sub, Loc {});
        lv.t = sub;
        lv.val = true;
        lv.s = PointeeLv(r0, sub);
        return lv;
    }
    if (IsResz(rt)) return RzTempLoc(rt, r0, "");
    if (IsBytesT(rt)) return BytesLoc(r0, rt, Loc {});
    lv.t = rt;
    lv.val = true;
    lv.s = r0;
    return lv;
}

}  
