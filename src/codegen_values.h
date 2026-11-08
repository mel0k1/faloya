



#pragma once

namespace faloya {









inline bool CodeGen::PrefVar(const VarDef *vd) {
    return vd->type && CarriesPool(vd->type) && vd->ref.reusable;
}




inline bool CodeGen::NeedsDeref(TypeExpr *have, TypeExpr *want) {
    return have && want && IsPlainRef(have) && want->kind != TY_REF && want->kind != TY_VOID;
}


inline CodeGen::Loc CodeGen::FatRefLoc(const string &x, TypeExpr *sub) {
    Loc lv;
    lv.t = sub;
    lv.stk = cat(x, ".stk");
    if (IsFrameObj(sub)) {
        lv.val = true;
        lv.s = cat("(*(", CT(sub), " *)", x, ".hdr)");
        lv.hdr = lv.s;
    } else {
        lv.s = cat(x, ".hdr->base");
        lv.lenlv = cat(x, ".hdr->len");
        lv.hdr = cat("(*", x, ".hdr)");
    }
    return lv;
}

inline CodeGen::Loc CodeGen::BytesLoc(const string &ptr, TypeExpr *t, const Loc &from) {
    Loc l;
    l.t = t;
    l.stk = from.stk;
    l.lenlv = from.lenlv;
    l.fl = from.fl;
    l.flstk = from.flstk;
    l.viaref = from.viaref || t->kind == TY_REF;
    if (IsFix(t)) {
        l.val = true;
        l.s = cat("(*(", CT(t), " *)(", ptr, "))");
    } else {
        l.val = false;
        l.s = ptr;
    }
    return l;
}





inline void CodeGen::RelParts(const Loc &lv, string &faddr, string &off) {
    auto &r = *lv.t->ref;
    if (r.lenstorage == IS_VARINT) {
        assert(!lv.val);
        faddr = lv.s;
        off = cat(r.pool ? "gs_uleb_read(" : "gs_zig_read(", lv.s, ")");
    } else if (lv.val) {
        faddr = cat("((uint8_t *)&", lv.s, ")");
        off = cat("(int64_t)(", RelCT(lv.t), ")", lv.s);
    } else {
        faddr = lv.s;
        off = cat("(int64_t)*(", RelCT(lv.t), " *)(", lv.s, ")");
    }
}




inline string CodeGen::RelOrigin(TypeExpr *rt, const string &faddr) {
    if (!rt->ref->pool) return faddr;
    return cat("(", PoolBase(rt->ref->pool), " - 1)");
}





inline string CodeGen::PointeeLv(const string &p, TypeExpr *t) {
    CT(t);
    return cat("(*", p, ")");
}



inline void CodeGen::DerefLoc(Loc &lv) {
    assert(lv.t->kind == TY_REF);
    if (lv.aliasof || lv.aliaspath) {
        auto nl = lv.aliaspath ? GenLoc(lv.aliaspath) : VarLoc(const_cast<VarDef *>(lv.aliasof));
        
        
        if (!lv.hbase.empty() && !IsFrameObj(nl.t)) nl.s = lv.hbase;
        if (!lv.hlen.empty()) nl.hlen = lv.hlen;
        lv = nl;
        return;
    }
    auto &r = *lv.t->ref;
    if (r.lenstorage >= 0) {
        string faddr, off;
        RelParts(lv, faddr, off);
        auto p = T();
        L("uint8_t *", p, " = ", RelOrigin(lv.t, faddr), " + ", off, ";");
        auto nl = BytesLoc(p, r.sub, lv);
        lv = nl;
        return;
    }
    auto rv = lv.s;   
    if (IsResz(r.sub) && IsFrameObj(r.sub)) {
        auto nl = FatRefLoc(rv, r.sub);
        nl.viaref = lv.viaref;
        lv = nl;
        return;
    }
    if (IsResz(r.sub)) {
        Loc nl;
        nl.t = r.sub;
        nl.val = false;
        nl.hdr = cat("(*", rv, ".hdr)");
        nl.s = lv.hbase.empty() ? cat(rv, ".hdr->base") : lv.hbase;
        
        nl.lenlv = cat(rv, ".hdr->len");
        nl.lenroot = lv.lenroot;
        nl.hlen = lv.hlen;
        nl.viaref = lv.viaref;
        nl.stk = cat(rv, ".stk");
        
        
        nl.fl = lv.ispref ? cat("(*", rv, ".fl)") : lv.fl;
        nl.flstk = lv.ispref ? cat(rv, ".flstk") : lv.flstk;
        lv = nl;
        return;
    }
    if (IsBytesT(r.sub)) {
        Loc nl;
        nl.t = r.sub;
        nl.val = false;
        nl.s = rv;
        nl.hlen = lv.hlen;
        nl.viaref = lv.viaref;
        lv = nl;
        return;
    }
    Loc nl;
    nl.t = r.sub;
    nl.val = true;
    nl.s = PointeeLv(rv, r.sub);
    if (IsRoot(r.sub)) {
        if (auto desc = NamedRootDesc(rv); !desc.empty()) nl.s = desc;
    }
    nl.hlen = lv.hlen;
    nl.viaref = lv.viaref;
    lv = nl;
}






inline void CodeGen::PinLoc(Loc &lv) {
    if (lv.val) {
        auto p = T();
        L(CT(lv.t), " *", p, " = &(", lv.s, ");");
        lv.s = cat("(*", p, ")");
    } else if (!IsResz(lv.t)) {
        auto p = T();
        L("uint8_t *", p, " = ", lv.s, ";");
        lv.s = p;
    } else {
        assert(!lv.hdr.empty());
        auto h = T();
        L("gs_rhdr *", h, " = &(", lv.hdr, ");");
        lv.hdr = cat("(*", h, ")");
        lv.s = cat(h, "->base");
        lv.lenlv = cat(h, "->len");
    }
    if (!lv.stk.empty() && !CacheableStk(lv.stk)) {
        auto s = T();
        L("gs_stack *", s, " = ", lv.stk, ";");
        lv.stk = s;
    }
    lv.viaref = false;
}

inline CodeGen::Loc CodeGen::VarLoc(VarDef *vd) {
    Loc l;
    l.t = vd->type;
    if (IsRoot(l.t) && !fvptr.count(vd)) l.rootowner = vd;
    if (auto ait = refalias.find(vd); ait != refalias.end()) {
        auto tg = const_cast<VarDef *>(ait->second);
        if (auto pit = aliaspath.find(vd); pit != aliaspath.end()) {
            auto tl = GenLoc(pit->second);
            l.val = true;
            l.s = cat("((gs_rref){ (gs_rhdr *)&", tl.hdr, ", ", tl.stk, " })");
            l.aliaspath = pit->second;
        } else if (tg->type->kind == TY_REF) {
            l = VarLoc(tg);
        } else if (IsRoot(tg->type)) {
            l.val = true;
            l.s = cat("(&", VarLoc(tg).s, ")");
            l.aliasof = tg;
        } else {
            
            
            l.val = true;
            l.s = cat("((gs_rref){ (gs_rhdr *)&", HdrLv(tg), ", ", VStkOf(tg), " })");
            l.aliasof = tg;
        }
        
        
        l.t = vd->type;
        l.hbase.clear();
        l.hlen.clear();
        if (auto hit = views.find(vd); hit != views.end()) {
            l.hbase = hit->second.first;
            l.hlen = hit->second.second;
        }
        return l;
    }
    auto name = VName(vd);
    if (auto hit = views.find(vd); hit != views.end()) {
        l.hbase = hit->second.first;
        l.hlen = hit->second.second;
    }
    if (vd->reusable) {
        
        
        auto pit = vpool.find(vd);
        auto git = gpools.find(vd);
        if (pit != vpool.end() || git != gpools.end()) {
            auto &p = pit != vpool.end() ? pit->second : git->second;
            auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
            l.val = false;
            l.hdr = hdr;
            l.s = PoolBaseOr(vd, cat(hdr, ".base"));
            l.lenlv = cat(hdr, ".len");
            l.stk = VStkOf(vd);
            l.fl = p.first;
            l.flstk = p.second;
        } else {
            l.val = false;
            l.hdr = cat("(*", name, ".hdr)");
            l.s = cat(name, ".hdr->base");
            l.lenlv = cat(name, ".hdr->len");
            l.stk = cat(name, ".stk");
            l.fl = cat("(*", name, ".fl)");
            l.flstk = cat(name, ".flstk");
        }
        return l;
    }
    if (IsResz(vd->type)) {
        
        
        auto hdr = fvptr.count(vd) ? cat("(*", name, ")") : name;
        l.hdr = hdr;
        l.stk = VStkOf(vd);
        if (IsFrameObj(vd->type)) {
            l.val = true;
            l.s = hdr;
            return l;
        }
        l.val = false;
        l.s = PoolBaseOr(vd, cat(hdr, ".base"));
        l.lenlv = cat(hdr, ".len");
        return l;
    }
    if (IsBytesT(vd->type)) {
        l.val = false;
        l.s = name;
        l.stk = VStkOf(vd);
        return l;
    }
    l.val = true;
    
    l.s = splitroots.count(vd) ? cat("(&", name, ")") : fvptr.count(vd) ? cat("(*", name, ")") : name;
    l.viaref = vd->isvar && vd->type->kind == TY_REF;
    if (PrefVar(vd)) {
        l.ispref = true;
        l.fl = cat("(*", l.s, ".fl)");
        l.flstk = cat(l.s, ".flstk");
    }
    return l;
}


inline string CodeGen::HdrLv(VarDef *vd) {
    auto name = VName(vd);
    return fvptr.count(vd) ? cat("(*", name, ")") : name;
}


inline string CodeGen::VarCT(const VarDef *vd) {
    if (PrefVar(vd)) { EmitCoreTypes(); return "gs_pref"; }
    return CT(vd->type);
}


inline string CodeGen::VStkOf(const VarDef *vd) {
    auto it = vstk.find(vd);
    if (it != vstk.end()) return it->second;
    auto git = gstks.find(vd);
    if (git != gstks.end()) return git->second;
    return "";
}





inline string CodeGen::FieldPtr(const string &base, const vector<Field> &fields,
                                const vector<TypeExpr *> &ftypes, int fieldidx) {
    int64_t off = 0;
    string cur;   
    for (auto i = 0; i < fieldidx; i++) {
        auto &f = fields[i];
        if (f.ispad) { off += f.padsize > 0 ? f.padsize : 0; continue; }
        if (IsFix(ftypes[i])) { off += FixedSize(ftypes[i]); continue; }
        if (cur.empty()) {
            cur = T();
            L("uint8_t *", cur, " = ", base, " + ", off, ";");
        } else if (off) {
            L(cur, " += ", off, ";");
        }
        off = 0;
        L(cur, " += ", SizeX(ftypes[i], cur), ";");
    }
    if (cur.empty()) return off ? cat("(", base, " + ", off, ")") : base;
    return off ? cat("(", cur, " + ", off, ")") : cur;
}



inline CodeGen::ArrView CodeGen::ArrayView(const Loc &lv) {
    auto v = RawArrayView(lv);
    if (!lv.hlen.empty()) v.len = lv.hlen;
    return v;
}

inline CodeGen::ArrView CodeGen::RawArrayView(const Loc &lv) {
    auto t = lv.t;
    ArrView v;
    if (t->kind == TY_SLICE) {
        v.elem = t->sub;
        v.elems = cat(lv.s, ".data");
        v.len = cat(lv.s, ".len");
        v.typedelems = !IsBytesT(t->sub);
        return v;
    }
    assert(t->kind == TY_ARRAY);
    auto &a = *t->arr;
    v.elem = a.sub;
    switch (a.akind) {
        case A_FIXED:
            assert(lv.val);
            v.elems = cat(lv.s, ".e");
            v.len = cat(ArrSize(t->arr));
            v.typedelems = true;
            return v;
        case A_LIMITED:
            if (lv.val) {
                v.elems = cat(lv.s, ".e");
                v.len = cat("(int64_t)", lv.s, ".len");
                v.lenlv = cat(lv.s, ".len");
                v.typedelems = true;
            } else {
                v.elems = cat("((", lv.s, ") + 8)");
                v.len = cat("(int64_t)*(gs_u32 *)((", lv.s, ") + 4)");
                v.lenlv = cat("(*(gs_u32 *)((", lv.s, ") + 4))");
            }
            return v;
        case A_VAR: {
            assert(!lv.val);
            auto ls = LenStore(t->arr);
            if (ls == IS_VARINT) {
                
                
                auto p = T();
                L("uint8_t *", p, " = (", lv.s, ") + GS_ULEB_SIZE(", lv.s, ");");
                v.elems = p;
                v.len = cat("GS_ULEB_READ(", lv.s, ")");
            } else {
                v.elems = cat("((", lv.s, ") + ", IntSize(ls), ")");
                v.len = cat("(int64_t)*(", IntCT(ls), " *)(", lv.s, ")");
            }
            return v;
        }
        default:   
            assert(!lv.val && !lv.lenlv.empty());
            v.elems = cat("(", lv.s, ")");
            v.len = cat("(", lv.lenlv, ")");
            v.lenlv = lv.lenlv;
            return v;
    }
}

inline bool CodeGen::AddView(VarDef *vd) {
    auto t = vd->type;
    if (views.count(vd) || !t || t->kind != TY_REF) return false;
    if (t->ref->optional || t->ref->lenstorage >= 0) return false;
    auto s = t->ref->sub;
    if (s->kind != TY_ARRAY || s->arr->akind == A_FIXED) return false;
    
    if (s->arr->akind == A_VAR && LenStore(s->arr) == IS_VARINT) return false;
    
    
    
    if (!vnames.count(vd) && !gnames.count(vd) && !aliasbound.count(vd)) return false;
    auto lv = VarLoc(vd);
    DerefLoc(lv);
    auto v = ArrayView(lv);
    
    
    string nb;
    if (IsResz(s)) {
        nb = T();
        L("uint8_t *", nb, " = ", v.elems, ";");
    }
    auto nl = T();
    L("int64_t ", nl, " = ", v.len, ";");
    views[vd] = { nb, nl };
    return true;
}



inline const VarDef *CodeGen::FieldRoot(const VarDef *vd) {
    for (auto guard = 0; guard < 16; guard++) {
        auto it = refalias.find(vd);
        if (it == refalias.end() || aliaspath.count(vd)) break;
        vd = it->second;
    }
    return vd;
}



inline bool CodeGen::AddFieldView(const FieldPath &fp) {
    auto vd = const_cast<VarDef *>(FieldRoot(fp.first));
    auto key = pair<const VarDef *, vector<int>>(vd, fp.second);
    auto t = vd->type;
    if (fieldviews.count(key) || !t || fp.second.empty()) return false;
    if (t->kind == TY_REF && (t->ref->optional || t->ref->lenstorage >= 0)) return false;
    
    if (!vnames.count(vd) && !gnames.count(vd) && !aliasbound.count(vd)) return false;
    auto lv = VarLoc(vd);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    for (auto f : fp.second) {
        if (lv.t->kind != TY_STRUCT || !lv.val) return false;
        lv = FieldLocAt(lv, f);
    }
    if (lv.t->kind != TY_ARRAY || !IsResz(lv.t) || IsFrameObj(lv.t) || lv.lenlv.empty())
        return false;
    auto v = RawArrayView(lv);
    auto nb = T(), nl = T();
    L("uint8_t *", nb, " = ", v.elems, ";");
    L("int64_t ", nl, " = ", v.len, ";");
    fieldviews[key] = { nb, nl };
    return true;
}



inline void CodeGen::UseFieldView(Dot *d, Loc &lv) {
    if (lv.lenlv.empty() || lv.t->kind != TY_ARRAY || !IsResz(lv.t) || IsFrameObj(lv.t)) return;
    vector<int> path;
    Node *cur = d;
    while (auto dd = Is<Dot>(cur)) {
        if (!dd->IsField()) return;
        path.push_back(dd->fieldidx);
        cur = dd->obj;
    }
    auto id = Is<Ident>(cur);
    if (!id || !id->vdef) return;
    std::reverse(path.begin(), path.end());
    auto hit = fieldviews.find({ FieldRoot(id->vdef), path });
    if (hit == fieldviews.end()) return;
    lv.s = hit->second.first;
    lv.hlen = hit->second.second;
}






inline void CodeGen::FixedLocal(TypeExpr *t, string &name, const string &init, bool forlocal) {
    if (!LargeFixedOnStack(t)) {
        L(CT(t), " ", name, init.empty() ? ";" : cat(" = ", init, ";"));
        return;
    }
    auto stk = AllocStk(forlocal);
    L(CT(t), " *", name, " = (", CT(t), " *)", Top(stk), ";");
    SaveBase(forlocal, stk, cat("(uint8_t *)", name));
    Bump(stk, cat("sizeof(", CT(t), ")"));
    name = cat("(*", name, ")");
    if (init == "{0}") L("GS_MEMZERO(&", name, ", sizeof(", name, "));");
    else if (!init.empty()) L(name, " = ", init, ";");
}



inline string CodeGen::Snapshot(TypeExpr *vt, const string &x) {
    auto t = T();
    FixedLocal(vt, t, x);
    return t;
}

inline string CodeGen::GenPure(Node *n) {
    if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n)) return GenX(n);
    return Snapshot(n->exprtype, GenX(n));
}





inline string CodeGen::DivX(bool div, IntStorage s, const string &l, const string &r,
                            Line line) {
    auto pure = [&](const string &x) {
        auto core = x;
        while (core.size() > 2 && core.front() == '(' && core.back() == ')' &&
               core.find_first_of("()", 1) == core.size() - 1)
            core = core.substr(1, core.size() - 2);
        auto name = !core.empty() && !isdigit((uint8_t)core[0]);
        auto num = !core.empty() && isdigit((uint8_t)core[0]);
        for (auto ch : core) {
            if (!isalnum((uint8_t)ch) && ch != '_') name = false;
            if (!isdigit((uint8_t)ch) && !strchr("uUlL", ch)) num = false;
        }
        if (name || num) return core;
        auto t = T();
        L(IntCT(s), " ", t, " = ", x, ";");
        return t;
    };
    auto a = pure(l), b = pure(r);
    if (div) return cat("gs_div_", IntSfx(s), "(", a, ", ", b, ", ", LocArgs(line), ")");
    if (IsUnsigned(s)) return cat("gs_mod_", IntSfx(s), "(", a, ", ", b, ", ", LocArgs(line), ")");
    auto t = T();
    L("int64_t ", t, ";");
    return cat("gs_mod_", IntSfx(s), "(", t, ", ", a, ", ", b, ", ", LocArgs(line), ")");
}









inline string CodeGen::ArithX(TType op, TypeExpr *t, const string &l, const string &r, Line line,
                              bool nonneg, string lx, string rx) {
    assert(t->kind == TY_FLT || (t->kind == TY_INT && t->intstorage != IS_VARINT));
    switch (op) {
        case T_PLUSEQ:  op = T_PLUS;   break;
        case T_MINUSEQ: op = T_MINUS;  break;
        case T_MULEQ:   op = T_MUL;    break;
        case T_DIVEQ:   op = T_DIV;    break;
        case T_MODEQ:   op = T_MOD;    break;
        case T_ANDEQ:   op = T_BITAND; break;
        case T_OREQ:    op = T_BITOR;  break;
        case T_XOREQ:   op = T_XOR;    break;
        case T_SHLEQ:   op = T_SHL;    break;
        case T_SHREQ:   op = T_SHR;    break;
        default: break;
    }
    auto ct = CT(t);
    if (lx.empty()) lx = cat("(", ct, ")(", l, ")");
    if (rx.empty()) rx = cat("(", ct, ")(", r, ")");
    if (t->kind == TY_FLT) {
        switch (op) {
            case T_PLUS:  return cat("(", lx, " + ", rx, ")");
            case T_MINUS: return cat("(", lx, " - ", rx, ")");
            case T_MUL:   return cat("(", lx, " * ", rx, ")");
            case T_DIV:   return cat("(", lx, " / ", rx, ")");
            case T_MOD:
                return cat(t->fltstorage == FS_F32 ? "fmodf(" : "fmod(", lx, ", ", rx, ")");
            default: assert(false); return l;
        }
    }
    auto s = t->intstorage;
    auto sfx = IntSfx(s);
    switch (op) {
        case T_PLUS:  return cat("gs_add_", sfx, "(", l, ", ", r, OvfLocArgs(s, line), ")");
        case T_MINUS: return cat("gs_sub_", sfx, "(", l, ", ", r, OvfLocArgs(s, line), ")");
        case T_MUL:   return cat("gs_mul_", sfx, "(", l, ", ", r, OvfLocArgs(s, line), ")");
        case T_DIV: case T_MOD:
            if (!nonneg) return DivX(op == T_DIV, s, l, r, line);
            return cat("(", ct, ")((uint64_t)(", lx, op == T_DIV ? ") / " : ") % ", "(uint64_t)(",
                       rx, "))");
        case T_BITAND: return cat("(", ct, ")(", lx, " & ", rx, ")");
        case T_BITOR:  return cat("(", ct, ")(", lx, " | ", rx, ")");
        case T_XOR:    return cat("(", ct, ")(", lx, " ^ ", rx, ")");
        case T_SHL:    return cat("gs_shl_", sfx, "(", l, ", (int64_t)(", r, "))");
        case T_SHR:
            if (!nonneg) return cat("gs_shr_", sfx, "(", l, ", (int64_t)(", r, "))");
            return cat("(", ct, ")((uint64_t)(", lx, ") >> ((int64_t)(", r, ") & ", IntBits(s) - 1,
                       "))");
        default: assert(false); return l;
    }
}




inline string CodeGen::IntStr(int64_t v) {
    if (v == INT64_MIN) return "(-9223372036854775807LL - 1)";
    if (v == INT32_MIN) return "(-2147483647 - 1)";
    if (v > INT32_MAX || v < INT32_MIN) return cat(v, "LL");
    return cat(v);
}

inline string CodeGen::FltStr(double v, bool f32) {
    
    if (v != v) return "NAN";
    if (std::isinf(v)) return v > 0 ? "INFINITY" : "(-INFINITY)";
    
    
    if (f32 && std::fabs(v) >= std::ldexp(2.0 - std::ldexp(1.0, -24), 127))
        return v > 0 ? "INFINITY" : "(-INFINITY)";
    string s;
    CatOne(s, v);
    if (f32) s += "f";
    return s;
}




inline CodeGen::Loc CodeGen::IndexLoc(Loc lv, Node *idxnode, Line ln, bool nobc) {
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    auto v = ArrayView(lv);
    if (HasStmts(idxnode)) {
        auto elems = T(), len = T();
        L(v.typedelems ? CT(v.elem) : string("uint8_t"), " *", elems, " = ", v.elems, ";");
        L("int64_t ", len, " = ", v.len, ";");
        v.elems = elems;
        v.len = len;
    }
    string idx;
    if (locnow && Is<Ident>(idxnode)) {
        idx = GenX(idxnode);
        if (!IsCName(idx)) idx = Snapshot(idxnode->exprtype, idx);
    } else {
        idx = GenPure(idxnode);
    }
    if (lv.t->kind == TY_SLICE) {
        
        
        
        
        
        auto p = T();
        L(IsBytesT(v.elem) ? "uint8_t" : CT(v.elem), " *", p, " = ", v.elems, ";");
        v.elems = p;
    }
    string ix;
    auto il = Is<IntLit>(idxnode);
    auto statlen = lv.t->kind == TY_ARRAY && lv.t->arr->akind == A_FIXED
                       ? ArrSize(lv.t->arr) : -1;
    if (nobc || (il && statlen >= 0 && il->val >= 0 && il->val < statlen)) ix = idx;
    else ix = cat("GS_IDX((int64_t)(", idx, "), ", v.len, ", ", LocArgs(ln), ")");
    if (v.typedelems) {
        Loc r;
        r.t = v.elem;
        r.val = true;
        r.s = cat(v.elems, "[", ix, "]");
        r.stk = lv.stk;
        r.fl = lv.fl;
        r.flstk = lv.flstk;
        r.viaref = lv.viaref || v.elem->kind == TY_REF;
        return r;
    }
    assert(IsFix(v.elem));   
    
    
    
    
    auto esz = FixedSize(v.elem);
    if ((v.elem->kind == TY_INT || v.elem->kind == TY_FLT) && (esz == 2 || esz == 4 || esz == 8)) {
        auto r = BytesLoc(v.elems, v.elem, lv);
        r.s = cat("(((", CT(v.elem), " *)(", v.elems, "))[", ix, "])");
        return r;
    }
    return BytesLoc(cat("(", v.elems, " + ", ix, " * ", esz, ")"), v.elem, lv);
}



static bool CheckFreePath(Node *n) {
    if (auto id = Is<Ident>(n)) return id->vdef != nullptr;
    if (auto d = Is<Dot>(n)) return d->IsField() && CheckFreePath(d->obj);
    return false;
}


inline bool CodeGen::SamePath(Node *a, Node *b) {
    if (auto ia = Is<Ident>(a)) {
        auto ib = Is<Ident>(b);
        return ib && ia->vdef && ia->vdef == ib->vdef;
    }
    if (auto la = Is<IntLit>(a)) {
        auto lb = Is<IntLit>(b);
        return lb && la->val == lb->val;
    }
    auto da = Is<Dot>(a), db = Is<Dot>(b);
    return da && db && da->IsField() && db->IsField() && da->fieldidx == db->fieldidx &&
           TEq(da->obj->exprtype, db->obj->exprtype) && SamePath(da->obj, db->obj);
}






inline Index *CodeGen::LeadingCheck(Node *arm) {
    auto b = Is<Block>(arm);
    if (!b || b->stmts.empty()) return nullptr;
    auto a = Is<Assign>(b->stmts[0]);
    if (!a) return nullptr;
    auto p = a->lval;
    while (auto d = Is<Dot>(p)) {
        if (!d->IsField()) return nullptr;
        p = d->obj;
    }
    auto ix = Is<Index>(p);
    if (!ix || ix->nobc || fillvalues.count(ix) || !CheckFreePath(ix->obj)) return nullptr;
    if (auto iv = Is<Ident>(ix->idx)) return iv->vdef ? ix : nullptr;
    auto il = Is<IntLit>(ix->idx);
    auto at = ix->obj->exprtype;
    if (at && at->kind == TY_REF) at = at->ref->sub;
    if (!il || !at || (at->kind == TY_ARRAY && at->arr->akind == A_FIXED)) return nullptr;
    return ix;
}


inline bool CodeGen::SameCheck(Index *a, Index *b) {
    return SamePath(a->obj, b->obj) && SamePath(a->idx, b->idx) &&
           TEq(a->obj->exprtype, b->obj->exprtype);
}




inline void CodeGen::EmitHoistedCheck(Index *ta, Index *ea, const string &c) {
    auto lv = GenLoc(ta->obj);
    while (lv.t->kind == TY_REF) DerefLoc(lv);
    auto v = ArrayView(lv);
    auto idx = GenPure(ta->idx);
    auto tf = LocFile(ta->line), ef = LocFile(ea->line);
    auto tl = LocLine(ta->line), el = LocLine(ea->line);
    auto file = tf == ef ? tf : cat("(", c, ") ? ", tf, " : ", ef);
    auto line = tl == el ? tl : cat("(", c, ") ? ", tl, " : ", el);
    L("(void)GS_IDX((int64_t)(", idx, "), ", v.len, ", ", file, ", ", line, ");");
}

inline CodeGen::Loc CodeGen::GenLoc(Node *n) {
    if (auto it = fillvalues.find(n); it != fillvalues.end()) return it->second;
    if (auto id = Is<Ident>(n)) {
        assert(id->vdef);
        return VarLoc(id->vdef);
    }
    
    
    if (auto d = Is<Dot>(n); d && d->IsField()) {
        auto lv = GenLoc(d->obj);
        if (lv.t->kind == TY_REF) DerefLoc(lv);
        auto ml = MemberLoc(lv, d);
        if (!fieldviews.empty()) UseFieldView(d, ml);
        return ml;
    }
    if (auto ix = Is<Index>(n)) {
        auto lv = GenLoc(ix->obj);
        if (lv.t->kind == TY_REF) DerefLoc(lv);
        return IndexLoc(lv, ix->idx, ix->line, ix->nobc);
    }
    
    
    
    
    if (auto u = Is<Unary>(n); u && u->op == T_BITAND &&
        (Is<Ident>(u->child) || Is<Dot>(u->child) || Is<Index>(u->child)) &&
        u->child->exprtype && u->child->exprtype->kind != TY_REF)
        return GenLoc(u->child);
    
    Loc l;
    l.t = n->exprtype;
    if (IsResz(n->exprtype)) return GenRzTmp(n);
    if (IsBytesT(n->exprtype)) {
        string stk;
        l.s = GenPtr(n, &stk);
        l.stk = stk;
        l.val = false;
    } else {
        auto t = T();
        FixedLocal(n->exprtype, t, GenX(n));
        l.s = t;
        l.val = true;
    }
    return l;
}





inline CodeGen::Loc CodeGen::GenLocNow(Node *n) {
    auto saved = locnow;
    locnow = !HasStmts(n);
    auto lv = GenLoc(n);
    locnow = saved;
    return lv;
}





inline bool CodeGen::QuietValue(Node *n) {
    if (auto id = Is<Ident>(n)) return id->vdef && n->exprtype && IsFix(n->exprtype);
    if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<NullLit>(n)) return true;
    int budget = 16;
    return Speculatable(n, false, false, budget);
}












inline bool CodeGen::StableDest(Node *n) {
    auto fixed = [&](TypeExpr *t) {
        if (t && t->kind == TY_REF) {
            if (t->ref->lenstorage >= 0) return (TypeExpr *)nullptr;
            t = t->ref->sub;
        }
        return t && IsFix(t) ? t : nullptr;
    };
    for (;;) {
        if (auto d = Is<Dot>(n)) {
            if (!d->IsField() || !fixed(d->obj->exprtype)) return false;
            n = d->obj;
        } else if (auto ix = Is<Index>(n)) {
            auto at = fixed(ix->obj->exprtype);
            if (!at || at->kind != TY_ARRAY || at->arr->akind != A_FIXED || fillvalues.count(ix))
                return false;
            auto il = Is<IntLit>(ix->idx);
            if (!ix->nobc && !(il && il->val >= 0 && il->val < ArrSize(at->arr))) return false;
            n = ix->obj;
        } else {
            return Is<Ident>(n) != nullptr;
        }
    }
}



inline string CodeGen::BytesTemp(string &stk) {
    stk = AllocStk(false);
    auto base = T();
    L("uint8_t *", base, " = ", Top(stk), ";");
    SaveBase(false, stk, base);
    return base;
}




inline string CodeGen::RzTemp(TypeExpr *t, string &stk) {
    EmitCoreTypes();
    stk = AllocStk(false);
    auto h = T();
    if (IsFrameObj(t)) {
        L(CT(t), " ", h, ";");
        SaveBase(false, stk, cat(FoTailHdr(t, h), ".base"));
    } else {
        L("gs_rhdr ", h, " = { ", Top(stk), ", 0 };");
        SaveBase(false, stk, cat(h, ".base"));
    }
    return h;
}



inline string CodeGen::RzLenLv(TypeExpr *t, const string &h) {
    return IsFrameObj(t) ? h : cat(h, ".len");
}


inline CodeGen::Loc CodeGen::RzTempLoc(TypeExpr *t, const string &h, const string &stk) {
    Loc l;
    l.t = t;
    l.stk = stk;
    l.hdr = h;
    if (IsFrameObj(t)) {
        l.val = true;
        l.s = h;
    } else {
        l.s = cat(h, ".base");
        l.lenlv = cat(h, ".len");
    }
    return l;
}



inline CodeGen::Loc CodeGen::GenRzTmp(Node *n) {
    auto t = n->exprtype;
    string stk;
    auto h = RzTemp(t, stk);
    GenAny(n, Dst { DK_STACK, stk, t, RzLenLv(t, h) });
    return RzTempLoc(t, h, stk);
}

inline CodeGen::Loc CodeGen::MemberLoc(Loc lv, Dot *d) {
    assert(d->fieldidx >= 0);
    return FieldLocAt(lv, d->fieldidx);
}

inline CodeGen::Loc CodeGen::FieldLocAt(Loc lv, int fieldidx) {
    if (lv.t->kind == TY_STRUCT) {
        auto si = SI(lv.t);
        auto ft = si->ftypes[fieldidx];
        if (si->st->isroot) {
            if (lv.rootowner) return VarLoc(lv.rootowner->components[fieldidx]);
            assert(lv.val);
            Loc r;
            r.t = ast.RefTo(ft, lv.t->line);
            r.val = true;
            r.s = cat(lv.s, ".", Sanitize(si->st->fields[fieldidx].name));
            if (ft->kind == TY_ARRAY && IsResz(ft)) r.lenroot = cat(r.s, ".hdr");
            r.viaref = lv.viaref || ft->kind == TY_REF;
            DerefLoc(r);
            return r;
        }
        if (lv.val) {
            Loc r = lv;
            r.t = ft;
            r.s = cat(lv.s, ".", Sanitize(si->st->fields[fieldidx].name));
            r.viaref = lv.viaref || ft->kind == TY_REF;
            r.hbase.clear();
            r.hlen.clear();
            r.lenlv.clear();
            r.lenroot.clear();
            if (IsResz(ft)) {
                
                
                r.hdr = r.s;
                if (!IsFrameObj(ft)) {
                    r.val = false;
                    r.lenlv = cat(r.s, ".len");
                    r.s = cat(r.s, ".base");
                }
            }
            return r;
        }
        return BytesLoc(FieldPtr(lv.s, si->st->fields, si->ftypes, fieldidx), ft, lv);
    }
    if (lv.t->kind != TY_VARIANT)
        Fail(Line {}, cat("internal: field of a non-struct location of type ", Mangle(lv.t)));
    auto ei = EIVar(lv.t);
    auto vi = ei->en->VariantIndex(lv.t->var->variant);
    auto ft = ei->vftypes[vi][fieldidx];
    if (lv.val) {
        Loc r = lv;
        r.t = ft;
        r.s = cat(lv.s, ".", Sanitize(ei->en->variants[vi].fields[fieldidx].name));
        r.viaref = lv.viaref || ft->kind == TY_REF;
        return r;
    }
    return BytesLoc(FieldPtr(lv.s, ei->en->variants[vi].fields, ei->vftypes[vi],
                             fieldidx), ft, lv);
}







inline bool CodeGen::IsCtl(Node *n) {
    return Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<Block>(n) || Is<EarlyBlock>(n) ||
           Is<LoopExpr>(n) || Is<InlineBlock>(n);
}




inline string CodeGen::LoadLoc(Loc lv, TypeExpr *et, Line ln) {
    if (lv.t->kind == TY_REF && et->kind != TY_REF) {
        DerefLoc(lv);
        return LoadLoc(lv, et, ln);
    }
    if (IsRelRef(lv.t)) {
        
        
        auto &r = *lv.t->ref;
        string faddr, off;
        RelParts(lv, faddr, off);
        auto org = RelOrigin(lv.t, faddr);
        auto ov = T(), p = T();
        L("int64_t ", ov, " = ", off, ";");
        
        
        if (r.optional) L("uint8_t *", p, " = ", ov, " ? ", org, " + ", ov, " : NULL;");
        else L("uint8_t *", p, " = ", org, " + ", ov, ";");
        
        
        assert(!IsResz(r.sub));
        if (IsBytesT(r.sub)) return p;
        return cat("((", CT(r.sub), " *)", p, ")");
    }
    if (et && et->kind == TY_SLICE && lv.t->kind == TY_ARRAY) {
        
        
        auto v = ArrayView(lv);
        auto t = T();
        auto dp = v.typedelems && IsBytesT(et->sub)
                      ? cat("(uint8_t *)(", v.elems, ")") : string(v.elems);
        if (!v.typedelems && !IsBytesT(et->sub))
            dp = cat("(", CT(et->sub), " *)(", v.elems, ")");
        L(CT(et), " ", t, " = { ", dp, ", ", v.len, " };");
        return t;
    }
    if (!lv.val) {
        assert(IsBytesT(lv.t));
        if (lv.t->kind == TY_INT)   
            return cat("gs_zig_read(", lv.s, ")");
        if (et && IsFix(et) && !TEq(lv.t, et)) return AdaptToFixed(lv, et, ln);
        return lv.s;   
    }
    
    
    if (et && lv.val && IsStaticLimited(et) &&
        (lv.t->kind == TY_SLICE || (lv.t->kind == TY_ARRAY && !TEq(lv.t, et))))
        return AdaptToFixed(lv, et, ln);
    
    if (et && et->kind == TY_ENUM && lv.t->kind == TY_VARIANT) return AdaptToFixed(lv, et, ln);
    if (lv.ispref && et && et->kind == TY_REF) {
        
        auto t = T();
        L("gs_rref ", t, " = { ", lv.s, ".hdr, ", lv.s, ".stk };");
        return t;
    }
    return lv.s;
}




inline string CodeGen::AdaptToFixed(Loc lv, TypeExpr *et, Line ln) {
    if (et->kind == TY_ENUM && lv.t->kind == TY_VARIANT) {
        
        assert(lv.val);
        auto ei = EIOf(et);
        auto vi = ei->en->VariantIndex(lv.t->var->variant);
        auto tv = T();
        FixedLocal(et, tv, HasUninitSlots(et) ? "{0}" : "");
        L(tv, ".tag = ", TagConst(ei, vi), ";");
        if (!EmptyLayout(ei->en->variants[vi].fields))
            L(tv, ".u.v_", Sanitize(ei->en->variants[vi].name), " = ", lv.s, ";");
        return tv;
    }
    if (et->kind == TY_ENUM) {
        auto ei = EIOf(et);
        auto ts = TagSize(ei->en);
        auto tv = T();
        FixedLocal(et, tv);
        L(tv, ".tag = *(", IntCT(TagStore(ei->en)), " *)(", lv.s, ")", ";");
        L("switch (", tv, ".tag) {");
        for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
            auto psz = VariantLayout(ei, (int)vi).size;
            L("case ", TagConst(ei, (int)vi), ":");
            ind++;
            if (psz)
                L("GS_MEMCPY(&", tv, ".u.v_", Sanitize(ei->en->variants[vi].name), ", ",
                  lv.s, " + ", ts, ", ", psz, ");");
            L("break;");
            ind--;
        }
        L("}");
        return tv;
    }
    assert(et->kind == TY_ARRAY && et->arr->akind == A_LIMITED);
    auto tv = T();
    FixedLocal(et, tv);
    CopyIntoLimited(lv, et, ln, tv, false);
    return tv;
}






inline void CodeGen::CopyIntoLimited(Loc lv, TypeExpr *et, Line ln, const string &dst,
                                     bool overlap) {
    auto v = ArrayView(lv);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    L("if (", nn, " > ", ArrSize(et->arr),
      ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
    L(overlap ? "gs_memmove" : CopyFn(), "(", dst, ".e, ", v.elems, ", (size_t)(", nn, " * ",
      FixedSize(et->arr->sub), "));");
    L(dst, ".len = (", IntCT(LenStore(et->arr)), ")", nn, ";");
}



inline bool CodeGen::AdaptsToLimited(Node *n, TypeExpr *want) {
    auto nt = n->exprtype;
    if (!want || !nt || !IsStaticLimited(want) || fillvalues.count(n)) return false;
    auto st = IsPlainRef(nt) ? nt->ref->sub : nt;
    return (st->kind == TY_ARRAY || st->kind == TY_SLICE) && !TEq(st, want);
}







inline bool CodeGen::GenIntoLimited(Node *n, TypeExpr *want, const string &dst, bool overlap) {
    if (!want || !IsStaticLimited(want) || !n->exprtype || fillvalues.count(n)) return false;
    Loc lv;
    auto dot = Is<Dot>(n);
    if (AdaptsToLimited(n, want)) {
        lv = GenLoc(n);
    } else if (!TEq(n->exprtype, want)) {
        return false;
    } else if (auto se = Is<SliceExpr>(n)) {
        lv.val = true;
        lv.s = GenSlice(se);
        lv.t = ast.SliceOf(want->arr->sub, n->line);
    } else if (Is<Ident>(n) || Is<Index>(n) || (dot && !dot->variantconst && dot->member < 0)) {
        
        lv = GenLoc(n);
        while (lv.t->kind == TY_REF) DerefLoc(lv);
        if (lv.t->kind != TY_SLICE && (lv.t->kind != TY_ARRAY || TEq(lv.t, want))) {
            L(dst, " = ", LoadLoc(lv, want, n->line), ";");
            return true;
        }
    } else {
        return false;
    }
    while (lv.t->kind == TY_REF) DerefLoc(lv);
    CopyIntoLimited(lv, want, n->line, dst, overlap);
    return true;
}

inline string CodeGen::BytesAddrOf(const Loc &lv) {
    return lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
}



inline string CodeGen::GenRefVal(Node *child, Line ln) {
    
    
    if (auto id = Is<Ident>(child); id && id->vdef && IsResz(id->vdef->type)) {
        auto stk = VStkOf(id->vdef);
        assert(!stk.empty());
        auto t = T();
        L("gs_rref ", t, " = { (gs_rhdr *)&", HdrLv(id->vdef), ", ", stk, " };");
        return t;
    }
    auto lv = GenLoc(child);
    if (lv.t->kind == TY_REF) {
        if (lv.t->ref->lenstorage >= 0) return LoadLoc(lv, lv.t, ln);
        return lv.s;
    }
    if (IsResz(lv.t)) {
        
        
        if (lv.hdr.empty() || lv.stk.empty())
            Fail(ln, "a reference to a resizable value nested in a variable-size "
                     "prefix or an ADT payload is unsupported; reference the owning "
                     "variable");
        auto t = T();
        L("gs_rref ", t, " = { (gs_rhdr *)&", lv.hdr, ", ", lv.stk, " };");
        return t;
    }
    if (!lv.val) return lv.s;
    return cat("&", lv.s);
}



inline string CodeGen::GenXD(Node *n, TypeExpr *want) {
    if (auto it = fillvalues.find(n); it != fillvalues.end())
        return LoadLoc(it->second, want, n->line);
    if (AdaptsToLimited(n, want)) {
        
        
        
        
        auto lv = GenLoc(n);
        if (lv.t->kind == TY_REF) DerefLoc(lv);
        return AdaptToFixed(lv, want, n->line);
    }
    if (NeedsDeref(n->exprtype, want)) {
        auto sub = n->exprtype->ref->sub;
        if (want->kind == TY_SLICE && sub->kind == TY_ARRAY) {
            
            auto lv = GenLoc(n);
            if (lv.t->kind == TY_REF) DerefLoc(lv);
            return LoadLoc(lv, want, n->line);
        }
        auto x = GenX(n);
        if (IsVarintT(sub)) return cat("gs_zig_read(", x, ")");
        if (IsResz(sub) || IsBytesT(sub)) return x;   
        return PointeeLv(x, sub);
    }
    return GenX(n);
}

inline string CodeGen::GenTruth(Node *n) {
    auto x = GenX(n);
    auto t = n->exprtype;
    if (t->kind == TY_REF && IsResz(t->ref->sub)) return cat("(", x, ".hdr != 0)");
    return x;
}




inline bool CodeGen::SpeculativeForm(Binary *b) {
    auto budget = 24;
    return InStraightLoop() && Speculatable(b->right, b->specidx > 0, true, budget);
}


inline Binary *CodeGen::ChainOp(Node *n) {
    auto b = Is<Binary>(n);
    return b && (b->op == T_ANDAND || b->op == T_OROR) && !SpeculativeForm(b) ? b : nullptr;
}



inline InlineBlock *CodeGen::BranchCall(Node *n) {
    auto ib = Is<InlineBlock>(n);
    return ib && ib->exprtype && ib->exprtype->kind == TY_BOOL && !AdtFrom(ib) ? ib : nullptr;
}


inline bool CodeGen::BranchChain(Node *n) {
    for (auto u = Is<Unary>(n); u && u->op == T_NOT; u = Is<Unary>(n)) n = u->child;
    return ChainOp(n) || BranchCall(n);
}






inline bool CodeGen::RangeTest(Binary *b, Node *&x, Node *&y) {
    auto ors = b->op == T_OROR;
    auto cmp = [&](Node *n, TType op, TType flipped, Node *&l, Node *&r) {
        auto c = Is<Binary>(n);
        if (!c || (c->op != op && c->op != flipped)) return false;
        auto t = c->left->exprtype ? OperandT(c->left->exprtype) : nullptr;
        if (!t || t->kind != TY_INT || IntRange(t->intstorage).first >= 0 ||
            t->intstorage == IS_VARINT)
            return false;
        l = c->op == op ? c->left : c->right;
        r = c->op == op ? c->right : c->left;
        return true;
    };
    Node *x0, *z, *x1, *n;
    if (!cmp(b->left, ors ? T_LT : T_GTEQ, ors ? T_GT : T_LTEQ, x0, z) ||
        !cmp(b->right, ors ? T_GTEQ : T_LT, ors ? T_LTEQ : T_GT, x1, n))
        return false;
    auto zero = Is<IntLit>(z);
    auto xi = Is<Ident>(x0);
    if (!zero || zero->val != 0 || !xi || !xi->vdef || !SamePath(x0, x1)) return false;
    if (auto k = Is<IntLit>(n); k && !k->uns && k->val >= 0) {
        x = x0;
        y = n;
        return true;
    }
    auto d = Is<Dot>(n);
    if (!d || (d->member != B_LEN && d->member != B_CAP) || !CheckFreePath(d->obj)) return false;
    x = x0;
    y = n;
    return true;
}













inline void CodeGen::GenBranch(Node *n, bool when, string lbl, int exitscope) {
    if (auto u = Is<Unary>(n); u && u->op == T_NOT)
        return GenBranch(u->child, !when, lbl, exitscope);
    auto b = ChainOp(n);
    Node *rx = nullptr, *ry = nullptr;
    if (b && !RangeTest(b, rx, ry)) {
        
        if ((b->op == T_ANDAND) != when) {
            GenBranch(b->left, when, lbl, exitscope);
            GenBranch(b->right, when, lbl, exitscope);
            return;
        }
        auto skip = Lbl();
        GenBranch(b->left, !when, skip);
        GenBranch(b->right, when, lbl, exitscope);
        L(skip, ":;");
        return;
    }
    if (auto ib = BranchCall(n)) {
        
        PushSc(SC_PLAIN);
        Dst d;
        d.k = DK_BRANCH;
        d.s = lbl;
        d.t = ib->exprtype;
        d.when = when;
        d.exitscope = exitscope >= 0 ? exitscope : (int)cscopes.size() - 1;
        ib->EmitBody(*this, d);
        PopSc();
        return;
    }
    auto jump = [&](const string &c) {
        auto restores = false;
        if (exitscope >= 0)
            for (auto i = exitscope; i < (int)cscopes.size(); i++)
                restores = restores || !cscopes[i].saves.empty();
        if (!restores) {
            L(c.empty() ? "" : cat("if (", c, ") "), "goto ", lbl, ";");
            return;
        }
        if (!c.empty()) {
            L("if (", c, ") {");
            ind++;
        }
        EmitExitRestores(exitscope);
        L("goto ", lbl, ";");
        if (!c.empty()) {
            ind--;
            L("}");
        }
    };
    if (auto bl = Is<BoolLit>(n)) {
        if (bl->val == when) jump("");
        return;
    }
    if (rx) {
        
        
        auto xl = GenX(rx), xr = GenX(ry);
        auto x = cat("(uint64_t)(int64_t)(", xl, ") ", b->op == T_OROR ? ">=" : "<",
                     " (uint64_t)(int64_t)(", xr, ")");
        jump(when ? x : cat("!(", x, ")"));
        return;
    }
    PushSc(SC_PLAIN);
    auto x = GenTruth(n);
    if (!cscopes.back().saves.empty()) {
        auto t = T();
        L("uint8_t ", t, " = (uint8_t)(", x, " != 0);");
        x = t;
    }
    PopSc();
    jump(when ? x : cat("!(", x, ")"));
}












inline bool CodeGen::Speculatable(Node *n, bool idxok, bool truth, int &budget) {
    if (--budget < 0) return false;
    auto t = n->exprtype;
    auto scalar = [](TypeExpr *x) {
        return x && (IsIntT(x) || x->kind == TY_FLT || x->kind == TY_BOOL);
    };
    auto wraps = [](TypeExpr *x) {   
        return x && IsIntT(x) && IntRange(x->intstorage).first == 0;
    };
    if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n)) return true;
    if (auto id = Is<Ident>(n)) {
        auto vt = id->vdef ? id->vdef->type : nullptr;
        if (!vt) return false;
        if (truth && IsOptional(vt) && t && IsOptional(t)) return true;
        return scalar(t ? OperandT(t) : nullptr) &&
               (scalar(vt) || (IsPlainRef(vt) && scalar(vt->ref->sub)));
    }
    if (auto u = Is<Unary>(n)) {
        switch (u->op) {
            case T_NOT: return Speculatable(u->child, idxok, true, budget);
            case T_BITNOT: return IsIntT(t) && Speculatable(u->child, idxok, false, budget);
            case T_MINUS:
                return t && t->kind == TY_FLT && Speculatable(u->child, idxok, false, budget);
            default: return false;
        }
    }
    if (auto b = Is<Binary>(n)) {
        auto both = [&](bool tr) {
            return Speculatable(b->left, idxok, tr, budget) &&
                   Speculatable(b->right, idxok, tr, budget);
        };
        auto lt = b->left->exprtype ? OperandT(b->left->exprtype) : nullptr;
        auto rt = b->right->exprtype ? OperandT(b->right->exprtype) : nullptr;
        switch (b->op) {
            case T_ANDAND: case T_OROR: return both(true);
            case T_EQ: case T_NEQ:
                if (Is<NullLit>(b->left) || Is<NullLit>(b->right)) {
                    auto other = Is<NullLit>(b->left) ? b->right : b->left;
                    if (Is<NullLit>(other)) return true;
                    auto id = Is<Ident>(other);
                    return id && id->vdef && id->vdef->type && IsOptional(id->vdef->type);
                }
                return scalar(lt) && scalar(rt) && both(false);
            case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:
                return scalar(lt) && scalar(rt) && both(false);
            
            case T_BITAND: case T_BITOR: case T_XOR: case T_SHL: case T_SHR:
                return lt && IsIntT(lt) && IsIntT(t) && both(false);
            case T_PLUS: case T_MINUS: case T_MUL:
                return (wraps(lt) || (lt && lt->kind == TY_FLT)) && scalar(t) && both(false);
            case T_DIV: return lt && lt->kind == TY_FLT && scalar(t) && both(false);
            default: return false;
        }
    }
    if (auto d = Is<Dot>(n)) {
        if (d->member == B_LEN || d->member == B_CAP)
            return SpeculatablePlace(d->obj, idxok, budget);
        if (!d->IsField() || !scalar(t ? OperandT(t) : nullptr)) return false;
        auto ot = d->obj->exprtype ? OperandT(d->obj->exprtype) : nullptr;
        if (!ot || ot->kind != TY_STRUCT || !IsFix(ot) || !scalar(SI(ot)->ftypes[d->fieldidx]))
            return false;
        return SpeculatablePlace(d->obj, idxok, budget);
    }
    if (auto ix = Is<Index>(n)) {
        if (!scalar(t ? OperandT(t) : nullptr) || !(IndexInRangeByType(ix) || (idxok && ix->nobc)))
            return false;
        return SpeculatablePlace(ix->obj, idxok, budget) &&
               Speculatable(ix->idx, idxok, false, budget);
    }
    if (auto c = Is<AsCast>(n)) {
        auto st = c->child->exprtype, tt = c->totype;
        if (!st || !tt || !(IsIntT(st) || st->kind == TY_FLT)) return false;
        if (tt->ix && !c->nobc) return false;   
        if (tt->kind == TY_INT) {
            if (!IsIntT(st) || !IsIntT(tt)) return false;
            auto [slo, shi] = IntRange(st->intstorage);
            auto [lo, hi] = IntRange(tt->intstorage);
            auto exact = st->intstorage == IS_U64 ? tt->intstorage == IS_U64
                                                  : slo >= lo && shi <= hi;
            if (!c->unchecked && !TEq(st, tt) && !exact) return false;
        } else if (tt->kind != TY_FLT) {
            return false;
        }
        return Speculatable(c->child, idxok, false, budget);
    }
    
    Block *block = nullptr;
    if (auto ib = Is<InlineBlock>(n)) block = ib->body;
    else block = Is<Block>(n);
    if (!block || !block->tail) return false;
    for (auto st : block->stmts) {
        auto vd = Is<VarDecl>(st);
        if (!vd || vd->isglobal || vd->byref || vd->defs.size() != 1 || vd->inits.size() != 1 ||
            !vd->defs[0] || !scalar(vd->defs[0]->type) ||
            !Speculatable(vd->inits[0], idxok, false, budget))
            return false;
    }
    return Speculatable(block->tail, idxok, truth, budget);
}





inline bool CodeGen::SpeculatablePlace(Node *n, bool idxok, int &budget) {
    if (--budget < 0) return false;
    if (auto id = Is<Ident>(n)) {
        auto vt = id->vdef ? id->vdef->type : nullptr;
        return vt && (vt->kind != TY_REF || IsPlainRef(vt));
    }
    if (auto d = Is<Dot>(n)) {
        if (!d->IsField()) return false;
        auto ot = d->obj->exprtype ? OperandT(d->obj->exprtype) : nullptr;
        if (!ot || ot->kind != TY_STRUCT || !IsFix(ot) ||
            SI(ot)->ftypes[d->fieldidx]->kind == TY_REF)
            return false;
        return SpeculatablePlace(d->obj, idxok, budget);
    }
    if (auto ix = Is<Index>(n)) {
        if (!(IndexInRangeByType(ix) || (idxok && ix->nobc))) return false;
        auto et = ix->exprtype;
        if (!et || et->kind == TY_REF) return false;
        return SpeculatablePlace(ix->obj, idxok, budget) &&
               Speculatable(ix->idx, idxok, false, budget);
    }
    return false;
}







inline string CodeGen::CtlValX(Node *n) {
    auto t = T();
    auto vt = n->exprtype;
    if (vt->kind == TY_INT && vt->intstorage == IS_VARINT) vt = ast.inttypes[IS_I64];
    if (IsRelRef(vt))
        vt = ast.RefTo(vt->ref->sub, n->line, vt->ref->optional);
    FixedLocal(vt, t);
    
    
    GenAny(n, Dst { DK_LVALUE, t, vt });
    return t;
}


inline void CodeGen::LeafAny(Node *n, const Dst &d) {
    if (d.k == DK_STACK) { GenConstruct(n, d.s, d.t, d.lenlv); return; }
    if (d.k == DK_LVALUE) {
        if (d.pool) { L(d.s, " = ", GenPrefVal(n), ";"); return; }
        if (GenIntoLimited(n, d.t, d.s, true)) return;   
        
        
        if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(n->exprtype)) {
            FixedLitAt(n, d.s);
        } else {
            L(d.s, " = ", GenXD(n, d.t), ";");
        }
        return;
    }
    if (IsVoidT(n->exprtype)) { Fail(n->line, "internal: valueless leaf"); }
    
    
    
    
    if (IsResz(n->exprtype)) {
        L("(void)(", GenLoc(n).s, ");");
        return;
    }
    L("(void)(", GenVal(n), ");");
}

inline string CodeGen::GenFixedArrayLit(ArrayLit *al) {
    auto et = al->exprtype;
    if (et->kind == TY_SLICE) {
        
        
        auto count = LitCount(al);
        auto at = ast.ArrayOf(et->sub, A_FIXED, al->line, count);
        auto save = al->exprtype;
        al->exprtype = at;
        auto av = GenFixedArrayLit(al);
        al->exprtype = save;
        auto t = T();
        L(CT(et), " ", t, " = { ", av, ".e, ", count, " };");
        return t;
    }
    assert(et->kind == TY_ARRAY);
    auto tv = T();
    FixedLocal(et, tv, HasUninitSlots(et) ? "{0}" : "");
    FixedArrayLitAt(al, tv, false);
    return tv;
}




inline string CodeGen::GenPtr(Node *n, string *stkout) {
    assert(!IsResz(n->exprtype));
    if (stkout) stkout->clear();
    if (auto it = fillvalues.find(n); it != fillvalues.end()) {
        if (stkout) *stkout = it->second.stk;
        return it->second.s;
    }
    
    
    
    Node *path = nullptr;
    if (Is<Ident>(n) || Is<Index>(n)) path = n;
    else if (auto dot = Is<Dot>(n); dot && dot->IsField()) path = n;
    else if (auto u = Is<Unary>(n); u && u->op == T_BITAND) path = u->child;
    if (path) {
        auto lv = GenLoc(path);
        if (lv.t->kind == TY_REF) DerefLoc(lv);
        auto et = n->exprtype;
        if (et->kind != TY_ARRAY || TEq(lv.t, et)) {
            if (stkout) *stkout = lv.stk;
            assert(!lv.val);
            return lv.s;
        }
        
        
        string stk;
        auto base = BytesTemp(stk);
        ConstructFromLoc(lv, et, stk, "", n->line);
        if (stkout) *stkout = stk;
        return base;
    }
    
    
    if (auto s = Is<StrLit>(n); s && n->exprtype->arr->akind == A_VAR) return GenStrBytes(s);
    
    string stk;
    auto base = BytesTemp(stk);
    GenAny(n, Dst { DK_STACK, stk, n->exprtype });
    if (stkout) *stkout = stk;
    return base;
}

inline string CodeGen::StrRaw(const string &v) {
    auto it = strdata.find(v);
    if (it != strdata.end()) return it->second;
    auto name = Unique(cat("gs_str", strdata.size()));
    
    
    
    Append(pdata, "static const uint8_t ", name, "[", v.size() ? v.size() : 1, "] = ", CStr(v),
           ";\n");
    auto expr = cat("((uint8_t *)", name, ")");
    strdata[v] = expr;
    return expr;
}




inline string CodeGen::BlobRaw(const string &v) {
    auto it = blobdata.find(v);
    if (it != blobdata.end()) return it->second;
    auto name = Unique(cat("gs_blob", blobdata.size()));
    Append(pdata, "static const uint8_t ", name, "[", v.size() ? v.size() : 1, "] = {");
    for (size_t i = 0; i < v.size(); i++) {
        if (i % 24 == 0) pdata += "\n   ";
        Append(pdata, " ", (int)(uint8_t)v[i], ",");
    }
    pdata += "\n};\n";
    auto expr = cat("((uint8_t *)", name, ")");
    blobdata[v] = expr;
    return expr;
}

inline string CodeGen::GenStrBytes(StrLit *s) {
    auto et = s->exprtype;
    assert(et->kind == TY_ARRAY && et->arr->akind == A_VAR);
    auto key = cat(Mangle(et), "|", s->val);
    auto it = strval.find(key);
    if (it != strval.end()) return it->second;
    auto ls = LenStore(et->arr);
    auto name = Unique(cat("gs_strv", strval.size()));
    if (ls == IS_VARINT) {
        uint8_t lenbuf[10];
        uint64_t v = s->val.size();
        string enc;
        auto nn = 0;
        do {
            lenbuf[nn] = v & 0x7f;
            v >>= 7;
            if (v) lenbuf[nn] |= 0x80;
            nn++;
        } while (v);
        enc.assign((char *)lenbuf, nn);
        enc += s->val;
        Append(pdata, "static const uint8_t ", name, "[", enc.size(), "] = ", CStr(enc),
               ";\n");
    } else {
        Append(pdata, "static const struct { ", IntCT(ls), " len; uint8_t s[",
               s->val.size() ? s->val.size() : 1, "]; } ", name, " = { ",
               s->val.size(), ", ", CStr(s->val), " };\n");
    }
    auto expr = cat("((uint8_t *)&", name, ")");
    strval[key] = expr;
    return expr;
}






inline TypeExpr *CodeGen::OperandT(TypeExpr *t) {
    return t && IsPlainRef(t) ? t->ref->sub : t;
}



inline string CodeGen::GenVal(Node *n) {
    auto et = OperandT(n->exprtype);
    if (et != n->exprtype) return GenXD(n, et);   
    return IsBytesT(et) ? GenPtr(n) : GenX(n);
}

inline string CodeGen::GenPureVal(Node *n) {
    auto et = OperandT(n->exprtype);
    if (IsBytesT(et)) {
        auto p = T();
        L("uint8_t *", p, " = (uint8_t *)(", GenPtr(n), ");");
        return p;
    }
    return Snapshot(et, GenVal(n));
}









inline string CodeGen::GenOperandVal(Node *n, bool later) {
    auto et = OperandT(n->exprtype);
    if (later || IsBytesT(et)) return GenPureVal(n);
    auto x = GenVal(n);
    if (HasStmts(n) && !IsCName(x)) return Snapshot(et, x);
    if (et->kind == TY_INT || et->kind == TY_FLT) return cat("(", CT(et), ")(", x, ")");
    return x;
}

inline bool CodeGen::IsCName(const string &x) {
    return !x.empty() && !isdigit((unsigned char)x[0]) &&
           all_of(x.begin(), x.end(), [](char c) { return isalnum((unsigned char)c) || c == '_'; });
}



inline bool CodeGen::HasStmts(Node *n) {
    return n && (NestsDeep(n) || EmitsStmts(n));
}

inline bool CodeGen::EmitsStmts(Node *n) {
    if (Is<Call>(n) || IsCtl(n) || Is<StrLit>(n)) return true;
    auto found = false;
    n->Children([&](Node *c) { found = found || EmitsStmts(c); });
    return found;
}



inline bool CodeGen::NestsDeep(Node *n, int levels) {
    if (levels <= 1) return true;
    if (IsCtl(n)) return false;
    auto deep = false;
    n->Children([&](Node *c) { deep = deep || NestsDeep(c, levels - 1); });
    return deep;
}

inline bool CodeGen::Spills(Node *n) {
    auto t = n->exprtype;
    return t && (t->kind == TY_FLT || t->kind == TY_BOOL || (t->kind == TY_INT && !IsVarintT(t))) &&
           NestsDeep(n);
}



inline string CodeGen::GenEquality(TypeExpr *lt, const string &l, const string &r) {
    if (lt->kind == TY_SLICE) return GenSliceEq(lt, l, r);
    return EqX(lt, l, r);
}

inline string CodeGen::GenSliceEq(TypeExpr *st, const string &l, const string &r) {
    return GenRangeEq(st->sub, cat(l, ".data"), cat(l, ".len"), cat(r, ".data"),
                      cat(r, ".len"));
}




inline string CodeGen::GenRangeEq(TypeExpr *elem, const string &ae, const string &an,
                                  const string &be, const string &bn) {
    auto t = T();
    L("uint8_t ", t, " = ", an, " == ", bn, ";");
    L("if (", t, ") {");
    ind++;
    if (ScalarEq(elem) && BitwiseEq(elem)) {
        L(t, " = gs_memeq(", ae, ", ", be, ", (size_t)((", an, ") * ", FixedSize(elem), "));");
    } else if (IsFix(elem)) {
        auto pa = T(), pb = T(), iv = T();
        L("const ", CT(elem), " *", pa, " = (const ", CT(elem), " *)(", ae, ");");
        L("const ", CT(elem), " *", pb, " = (const ", CT(elem), " *)(", be, ");");
        L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++)");
        L("    ", t, " = ", EqX(elem, cat(pa, "[", iv, "]"), cat(pb, "[", iv, "]")),
          ";");
    } else {
        
        auto pa = T(), pb = T(), iv = T();
        L("const uint8_t *", pa, " = (const uint8_t *)(", ae, "), *", pb,
          " = (const uint8_t *)(", be, ");");
        L("for (int64_t ", iv, " = 0; ", t, " && ", iv, " < (", an, "); ", iv, "++) {");
        ind++;
        L(t, " = ", EqFn(elem), "(", pa, ", ", pb, ");");
        auto sa = SizeX(elem, pa), sb = SizeX(elem, pb);
        L(pa, " += ", sa, "; ", pb, " += ", sb, ";");
        ind--;
        L("}");
    }
    ind--;
    L("}");
    return t;
}









inline void CodeGen::GenElemwiseInto(TypeExpr *t, TType op, Line line, const string &l,
                                     const string &r, const string &dst,
                                     bool lscalar, bool rscalar) {
    FltStorage fs = FS_F64;
    int64_t n = 0;
    const char *o = op == T_PLUS || op == T_PLUSEQ   ? " + "
                    : op == T_MINUS || op == T_MINUSEQ ? " - "
                    : op == T_MUL || op == T_MULEQ   ? " * "
                    : op == T_DIV || op == T_DIVEQ   ? " / "
                                                     : nullptr;
    bool vec = o && VecShape(t, fs, n);
    
    
    if (vec) {
        
        
        
        L("#if GS_VEC");
        auto a = VecOperand(t, fs, l, lscalar);
        auto b = VecOperand(t, fs, r, rscalar);
        auto vt = VecType(fs, n);
        L("GS_VST(", vt, ", &(", dst, "), ", VecLoad(vt, n, a, lscalar), o,
          VecLoad(vt, n, b, rscalar), ");");
        L("#else");
    }
    GenElemwiseLeaves(t, dst, [&](TypeExpr *tt, const string &path) {
        auto a = cat("(", l, ")", lscalar ? string() : path);
        auto c = cat("(", r, ")", rscalar ? string() : path);
        return ArithX(op, tt, a, c, line, false, a, c);
    });
    if (vec) L("#endif");
}



inline void CodeGen::GenElemwiseNegInto(TypeExpr *t, Line line, const string &x,
                                        const string &dst) {
    FltStorage fs = FS_F64;
    int64_t n = 0;
    bool vec = VecShape(t, fs, n);
    if (vec) {
        L("#if GS_VEC");
        auto a = VecOperand(t, fs, x, false);
        auto vt = VecType(fs, n);
        L("GS_VST(", vt, ", &(", dst, "), -", VecLoad(vt, n, a, false), ");");
        L("#else");
    }
    GenElemwiseLeaves(t, dst, [&](TypeExpr *tt, const string &path) {
        auto a = cat("(", x, ")", path);
        if (tt->kind == TY_FLT) return cat("(-", a, ")");
        return cat("gs_neg_", IntSfx(tt->intstorage), "(", a,
                   OvfLocArgs(tt->intstorage, line), ")");
    });
    if (vec) L("#endif");
}



inline bool CodeGen::VecShape(TypeExpr *t, FltStorage &fs, int64_t &n) {
    n = 0;
    
    
    if (FixedSize(t) > 64) return false;
    int sto = -1;
    bool ok = true;
    function<void(TypeExpr *)> rec = [&](TypeExpr *tt) {
        if (!ok) return;
        switch (tt->kind) {
            case TY_FLT:
                if (sto < 0) sto = tt->fltstorage;
                else if (sto != tt->fltstorage) ok = false;
                if (++n > 16) ok = false;
                return;
            case TY_STRUCT: {
                auto si = SI(tt);
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad) rec(si->ftypes[i]);
                return;
            }
            case TY_ARRAY:
                if (tt->arr->akind != A_FIXED || ArrSize(tt->arr) > 16) { ok = false; return; }
                for (int64_t i = 0; i < ArrSize(tt->arr); i++) rec(tt->arr->sub);
                return;
            default: ok = false; return;
        }
    };
    rec(t);
    if (!ok || sto < 0 || (n != 2 && n != 4 && n != 8 && n != 16)) return false;
    fs = (FltStorage)sto;
    auto bytes = n * (fs == FS_F32 ? 4 : 8);
    return (bytes == 16 || bytes == 32 || bytes == 64) && FixedSize(t) == bytes;
}

inline string CodeGen::VecType(FltStorage fs, int64_t n) {
    return cat("gs_v", FltStorageName(fs), "_", n);
}



inline string CodeGen::VecOperand(TypeExpr *t, FltStorage fs, const string &x, bool scalar) {
    auto v = T();
    if (scalar) {
        auto ect = fs == FS_F32 ? "float" : "double";
        L(ect, " ", v, " = (", ect, ")(", x, ");");
    } else {
        L(CT(t), " ", v, " = ", x, ";");
    }
    return v;
}



inline string CodeGen::VecLoad(const string &vt, int64_t n, const string &v, bool scalar) {
    if (!scalar) return cat("GS_VLD(", vt, ", &", v, ")");
    string lanes;
    for (int64_t i = 0; i < n; i++) lanes += cat(i ? ", " : "", v);
    return cat("((", vt, "){ ", lanes, " })");
}



inline void CodeGen::GenElemwiseLeaves(TypeExpr *t, const string &dst,
                                       const function<string(TypeExpr *, const string &)> &leaf) {
    function<void(TypeExpr *, const string &)> rec = [&](TypeExpr *tt, const string &path) {
        switch (tt->kind) {
            case TY_INT: case TY_FLT:
                L(dst, path, " = ", leaf(tt, path), ";");
                return;
            case TY_STRUCT: {
                auto si = SI(tt);
                for (size_t i = 0; i < si->st->fields.size(); i++)
                    if (!si->st->fields[i].ispad)
                        rec(si->ftypes[i],
                            cat(path, ".", Sanitize(si->st->fields[i].name)));
                return;
            }
            case TY_ARRAY: {
                auto iv = T();
                L("for (int64_t ", iv, " = 0; ", iv, " < ", ArrSize(tt->arr), "; ", iv,
                  "++) {");
                ind++;
                rec(tt->arr->sub, cat(path, ".e[", iv, "]"));
                ind--;
                L("}");
                return;
            }
            default: assert(false); return;
        }
    };
    
    
    rec(t, "");
}



inline void CodeGen::ElemwiseOperands(Binary *b, string &l, string &r) {
    auto scalar = [&](Node *n) {
        auto t = OperandT(n->exprtype);
        return t->kind == TY_INT || t->kind == TY_FLT;
    };
    
    
    if (scalar(b->left)) l = GenPureVal(b->left);
    else if (HasStmts(b->right)) {
        l = GenVal(b->left);
        auto lt = T();
        FixedLocal(OperandT(b->left->exprtype), lt, l);
        l = lt;
    } else l = GenVal(b->left);
    r = scalar(b->right) ? GenPureVal(b->right) : GenVal(b->right);
}

inline string CodeGen::GenElemwise(Binary *b, const string &l, const string &r) {
    auto tv = T();
    FixedLocal(b->exprtype, tv);
    GenElemwiseInto(b->exprtype, b->op, b->line, l, r, tv,
                    OperandT(b->left->exprtype)->kind != b->exprtype->kind,
                    OperandT(b->right->exprtype)->kind != b->exprtype->kind);
    return tv;
}





inline string CodeGen::GenSlice(SliceExpr *se, const string &into) {
    auto lv = GenLoc(se->obj);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    auto v = ArrayView(lv);
    auto elems = T();
    L(v.typedelems ? CT(v.elem) : string("uint8_t"), " *", elems, " = ", v.elems, ";");
    v.elems = elems;
    auto lenv = T();
    L("int64_t ", lenv, " = ", v.len, ";");
    auto bound = [&](Node *b, bool fromend, const char *dflt) -> string {
        if (!b) return dflt;
        auto x = GenX(b);
        if (fromend) return cat("(", lenv, " - (", x, "))");
        return x;
    };
    auto lo = bound(se->lo, se->lo_from_end, "0");
    auto lov = T(), hiv = T();
    L("int64_t ", lov, " = ", lo, ";");
    auto hi = bound(se->hi, se->hi_from_end, lenv.c_str());
    L("int64_t ", hiv, " = ", hi, ";");
    if (!se->nobc)
        L("if (", lov, " < 0 || ", hiv, " < ", lov, " || ", hiv, " > ", lenv,
          ") gs_abort(GS_E_SLICE, ", LocArgs(se->line), ");");
    
    
    
    auto et = se->exprtype->kind == TY_SLICE ? se->exprtype : ast.SliceOf(v.elem, se->line);
    string data, len;
    if (IsFix(v.elem)) {
        auto dp = v.typedelems ? v.elems
                               : cat("(", CT(v.elem), " *)(", v.elems, ")");
        data = cat(dp, " + ", lov);
        len = cat(hiv, " - ", lov);
    } else {
        
        data = v.elems;
        len = lenv;
    }
    if (!into.empty()) {
        L(into, ".data = ", data, "; ", into, ".len = ", len, ";");
        return into;
    }
    auto t = T();
    L(CT(et), " ", t, " = { ", data, ", ", len, " };");
    return t;
}

}  
