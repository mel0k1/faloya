


#pragma once

namespace faloya {






inline void CodeGen::EmitValStore(const string &stk, TypeExpr *t, const string &x) {
    auto sz = FixedSize(t);
    if (sz == 0) { L("(void)(", x, ");"); return; }
    StoreWhole(Top(stk), t, x);
    Bump(stk, cat(sz));
}








inline void CodeGen::StoreWhole(const string &p, TypeExpr *t, const string &x) {
    auto sz = FixedSize(t);
    if (!WholeStore(t)) {
        L("*(", CT(t), " *)", p, " = ", x, ";");
        return;
    }
    auto v = x;
    if (!IsCName(v)) {
        v = T();
        FixedLocal(t, v, x);
    }
    auto w = T();
    L(sz == 8 ? "uint64_t " : sz == 4 ? "uint32_t " : "uint16_t ", w, ";");
    L("GS_MEMCPY(&", w, ", &", v, ", ", sz, ");");
    L("GS_MEMCPY(", p, ", &", w, ", ", sz, ");");
}


inline bool CodeGen::WholeStore(TypeExpr *t) {
    auto sz = FixedSize(t);
    auto agg = t->kind == TY_STRUCT || t->kind == TY_VARIANT || t->kind == TY_ARRAY ||
               (t->kind == TY_ENUM && !t->enu->varmode);
    return agg && (sz == 2 || sz == 4 || sz == 8);
}

inline void CodeGen::EmitLenCheck(IntStorage ls, const string &n) {
    if (ls == IS_VARINT || IntSize(ls) == 8) return;
    auto max = (1ull << (IntSize(ls) * 8)) - 1;
    L("if ((uint64_t)(", n, ") > ", max,
      "ull) gs_panic(\"array length exceeds storage range\");");
}

inline void CodeGen::EmitLenStore(const string &stk, IntStorage ls, const string &n) {
    EmitLenCheck(ls, n);
    if (ls == IS_VARINT) {
        Bump(stk, cat("gs_uleb_write(", Top(stk), ", (uint64_t)(", n, "))"));
    } else {
        L("*(", IntCT(ls), " *)", Top(stk), " = (", IntCT(ls), ")(", n, ");");
        Bump(stk, cat(IntSize(ls)));
    }
}







inline void CodeGen::EmitArrayCount(const string &stk, TypeExpr *at, const string &lenlv,
                                    const string &n, bool check) {
    switch (at->arr->akind) {
        case A_VAR:
            if (!lenlv.empty()) L(lenlv, " = ", n, ";");
            else EmitLenStore(stk, LenStore(at->arr), n);
            break;
        case A_LIMITED: {   
            if (check) EmitLenCheck(IS_U32, n);
            auto x = check ? cat("(uint32_t)", n) : n;
            L("*(gs_u32 *)", Top(stk), " = ", x, ";");
            L("*(gs_u32 *)(", Top(stk), " + 4) = ", x, ";");
            Bump(stk, "8");
            break;
        }
        default:
            assert(!lenlv.empty());
            L(lenlv, " = ", n, ";");
            break;
    }
}


inline void CodeGen::EmitVarintStore(const string &stk, const string &x) {
    Bump(stk, cat("gs_zig_write(", Top(stk), ", ", x, ")"));
}













inline void CodeGen::EmitRelRangeCheck(TypeExpr *rt, const string &off, Line ln, bool inroot) {
    auto w = (IntStorage)rt->ref->lenstorage;
    auto bits = IntSize(w) * 8;
    if (bits >= 64) return;
    if (rt->ref->pool) {
        L("#if GS_STACK_RESERVE >= (1ull << ", bits, ")");
        L("if ((uint64_t)", off, " > ", (1ull << bits) - 1, "ull) gs_abort(GS_E_RELOFF, ",
          LocArgs(ln), ");");
        L("#endif");
        return;
    }
    auto guarded = inroot && relrootmax <= (1ll << (bits - 1));
    if (guarded) L("#if GS_STACK_RESERVE > (1ull << ", bits - 1, ")");
    L("if (", off, " < -(1LL << ", bits - 1, ") || ", off, " >= (1LL << ",
      bits - 1, ")) gs_abort(GS_E_RELOFF, ",
      LocArgs(ln), ");");
    if (guarded) L("#endif");
}





static bool NonNullRefValue(Node *v) {
    auto nonnull = [](TypeExpr *t) { return t && t->kind == TY_REF && !t->ref->optional; };
    if (!v) return false;
    if (nonnull(v->exprtype)) return true;
    if (auto c = Is<Call>(v)) return !c->rettypes.empty() && nonnull(c->rettypes[0]);
    if (auto b = Is<Block>(v)) return NonNullRefValue(b->tail);
    if (auto i = Is<IfExpr>(v))
        return i->elseb && NonNullRefValue(i->thenb) && NonNullRefValue(i->elseb);
    return false;
}

inline RelFacts CodeGen::RelValue(Node *v) {
    RelFacts f;
    f.nonnull = NonNullRefValue(v);
    return f;
}









inline bool CodeGen::RelSlotApart(TypeExpr *rt, TypeExpr *holder, int64_t off) {
    return off > 0 && holder && holder->kind == TY_STRUCT && IsFix(holder) &&
           rt->ref->sub->kind != TY_REF;
}






inline string CodeGen::RelOffset(TypeExpr *rt, const string &org, const string &rv, Line ln,
                                 RelFacts f) {
    auto addr = cat("(uint8_t *)(", rv, ")");
    auto off = T();
    if (rt->ref->optional && !f.nonnull)
        L("int64_t ", off, " = ", addr, " ? (int64_t)(", addr, " - (", org, ")) : 0;");
    else
        L("int64_t ", off, " = (int64_t)(", addr, " - (", org, "));");
    if (rt->ref->optional && !rt->ref->pool && !f.apart)
        L("if (", f.nonnull ? string() : cat(addr, " && "), "!", off, ") gs_abort(GS_E_RELNULL, ",
          LocArgs(ln), ");");
    return off;
}




inline void CodeGen::EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln,
                                    bool inroot, RelFacts f) {
    assert(rt->ref->lenstorage != IS_VARINT);
    assert(!IsResz(rt->ref->sub));
    auto off = RelOffset(rt, RelOrigin(rt, fa), rv, ln, f);
    EmitRelRangeCheck(rt, off, ln, inroot);
    L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", off, ";");
}

inline void CodeGen::EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln,
                                  RelFacts f) {
    auto w = (IntStorage)rt->ref->lenstorage;
    auto fa = T();
    L("uint8_t *", fa, " = ", Top(stk), ";");
    if (w == IS_VARINT) {
        assert(!IsResz(rt->ref->sub));
        auto off = RelOffset(rt, RelOrigin(rt, fa), rv, ln, f);
        Bump(stk, rt->ref->pool ? cat("gs_uleb_write(", fa, ", (uint64_t)", off, ")")
                                : cat("gs_zig_write(", fa, ", ", off, ")"));
    } else {
        EmitRelStoreAt(fa, rt, rv, ln, true, f);
        Bump(stk, cat(IntSize(w)));
    }
}








inline void CodeGen::EmitRelSelfAt(const string &fa, TypeExpr *rt, int64_t fieldoff, Line ln,
                                   bool inroot) {
    auto w = (IntStorage)rt->ref->lenstorage;
    assert(w != IS_VARINT);
    auto bits = IntSize(w) * 8;
    if (rt->ref->pool) {
        if (!inroot)
            Fail(ln, cat("self in a ", IntStorageName(w), " in ", rt->ref->pool->name,
                         " field needs the value's final address, which this literal is "
                         "not being built at"));
        auto off = T();
        L("int64_t ", off, " = (int64_t)((", fa, ") - ", fieldoff, " - (",
          RelOrigin(rt, fa), "));");
        EmitRelRangeCheck(rt, off, ln, true);
        L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", off, ";");
        return;
    }
    if (bits < 64 && fieldoff > (1LL << (bits - 1)))
        Fail(ln, cat("self at byte offset ", fieldoff, " does not fit a ",
                     IntStorageName(w), "-width relative reference"));
    L("*(", RelCT(rt), " *)(", fa, ") = (", RelCT(rt), ")", -fieldoff, ";");
}

inline void CodeGen::EmitRelSelfStore(const string &stk, TypeExpr *rt, int64_t fieldoff, Line ln) {
    EmitRelSelfAt(Top(stk), rt, fieldoff, ln);
    Bump(stk, cat(IntSize((IntStorage)rt->ref->lenstorage)));
}








inline bool CodeGen::HasUninitSlots(TypeExpr *t) {
    switch (t->kind) {
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
            if (t->kind == TY_ENUM && t->enu->varmode) return false;
            if (AnyField(t, [&](TypeExpr *ft) { return HasUninitSlots(ft); })) return true;
            
            
            
            if (t->kind == TY_ENUM) {
                auto ei = EIOf(t);
                auto room = FixedSize(t) - TagSize(ei->en);
                for (size_t vi = 0; vi < ei->en->variants.size(); vi++)
                    if (FixedSize(VariantType(t, (int)vi)) < room) return true;
            }
            return false;
        }
        case TY_ARRAY:
            if (t->arr->akind == A_LIMITED) return true;
            return t->arr->akind == A_FIXED &&
                   (ArrSize(t->arr) == 0 || HasUninitSlots(t->arr->sub));
        default: return false;
    }
}



inline bool CodeGen::HasRelRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            if (t->kind == TY_ENUM && t->enu->varmode) return false;
            return AnyField(t, [&](TypeExpr *ft) { return HasRelRef(ft); });
        case TY_ARRAY:
            return (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
                   HasRelRef(t->arr->sub);
        default: return false;
    }
}



inline bool CodeGen::IsFlatFixed(TypeExpr *t) {
    if (!IsFix(t)) return false;
    switch (t->kind) {
        case TY_REF: case TY_SLICE: return false;
        case TY_STRUCT: return SI(t)->flat;
        case TY_ENUM: return EIOf(t)->flat;
        case TY_VARIANT: return !AnyField(t, [&](TypeExpr *ft) { return !IsFlatFixed(ft); });
        case TY_ARRAY: return IsFlatFixed(t->arr->sub);
        default: return true;
    }
}



inline bool CodeGen::HasSelfRelRef(TypeExpr *t) {
    return HasRelRefOf(t, false, [&](TypeExpr *x) { return FieldRuns(x); });
}





inline void CodeGen::NoSelfRelCopy(Node *val) {
    auto id = Is<Ident>(val);
    if (id && id->vdef && id->vdef->type && HasSelfRelRef(id->vdef->type))
        Fail(val->line, cat("internal error: ", id->vdef->name, " holds self-relative "
                            "references and is not built where it is returned"));
}

inline void CodeGen::ComputeRelRootMax() {
    for (auto vd : ast.vardefs) {
        if (!vd->type || !IsFix(vd->type) || !HasRelRef(vd->type)) continue;
        relrootmax = std::max(relrootmax, FixedSize(vd->type));
    }
}



inline void CodeGen::EmitCopyElems(const string &stk, TypeExpr *elem, const string &src,
                                   const string &n) {
    if (IsFix(elem)) {
        auto esz = FixedSize(elem);
        L(CopyFn(), "(", Top(stk), ", ", src, ", (size_t)((", n, ") * ", esz, "));");
        Bump(stk, cat("(", n, ") * ", esz));
    } else {
        auto p = T(), iv = T();
        L("const uint8_t *", p, " = (const uint8_t *)(", src, ");");
        L("for (int64_t ", iv, " = 0; ", iv, " < (", n, "); ", iv, "++) {");
        ind++;
        auto sz = T();
        L("int64_t ", sz, " = ", SizeX(elem, p), ";");
        L("GS_MEMCPY(", Top(stk), ", ", p, ", (size_t)", sz, ");");
        Bump(stk, sz);
        L(p, " += ", sz, ";");
        ind--;
        L("}");
    }
}

inline CodeGen::SrcElems CodeGen::GenSrcElems(Node *n) {
    SrcElems r;
    if (auto s = Is<StrLit>(n)) {
        r.elems = StrRaw(s->val);
        r.n = cat(s->val.size());
        return r;
    }
    auto t = n->exprtype;
    if (t->kind == TY_SLICE) {
        auto x = GenPure(n);
        r.elems = cat(x, ".data");
        r.n = cat(x, ".len");
        return r;
    }
    
    
    assert(t->kind == TY_ARRAY || IsPlainRef(t));
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    if (lv.t->kind == TY_SLICE) {
        r.elems = cat(lv.s, ".data");
        r.n = cat(lv.s, ".len");
        return r;
    }
    auto v = ArrayView(lv);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    r.elems = v.elems;
    r.n = nn;
    return r;
}




inline void CodeGen::GenConstruct(Node *n, const string &stk, TypeExpr *want, const string &lenlv) {
    if (auto it = fillvalues.find(n); it != fillvalues.end()) {
        auto target = want ? want : it->second.t;
        if (IsRelRef(target))
            EmitRelStore(stk, target, GenX(n), n->line, RelValue(n));
        else if (IsVarintT(target)) EmitVarintStore(stk, GenXD(n, ast.inttypes[IS_I64]));
        else if (IsBytesT(target) || IsResz(target))
            ConstructFromLoc(it->second, target, stk, lenlv, n->line);
        else EmitValStore(stk, target, GenXD(n, target));
        return;
    }
    
    
    
    
    if (auto nd = NrvoBoundAt(n, stk, lenlv)) {
        EmitNrvoFinish(*nd);
        return;
    }
    auto et = n->exprtype;
    
    
    
    
    if (IsVarintT(want ? want : et)) {
        EmitVarintStore(stk, GenXD(n, ast.inttypes[IS_I64]));
        return;
    }
    
    
    
    
    
    if (want && et && want->kind == TY_ARRAY && IsBytesT(want) && et->kind == TY_ARRAY &&
        IsBytesT(et) && !TEq(want, et) && TEq(want->arr->sub, et->arr->sub))
        et = want;
    
    
    if (want && et && IsStaticLimited(want) && (et->kind == TY_ARRAY || et->kind == TY_SLICE) &&
        !TEq(et, want))
        et = want;
    if (want && NeedsDeref(n->exprtype, want)) {
        
        
        if (IsBytesT(want)) ConstructFromLoc(GenLoc(n), want, stk, lenlv, n->line);
        else EmitValStore(stk, want, GenXD(n, want));
        return;
    }
    if (IsCtl(n)) { GenAny(n, Dst { DK_STACK, stk, want, lenlv }); return; }
    
    
    OpenAt open(*this, stk);
    if (auto c = Is<Call>(n); c && c->builtin == B_COPY) {
        
        auto target = want ? want : et;
        auto lv = GenLoc(c->FirstArg());
        if (IsBytesT(target)) ConstructFromLoc(lv, target, stk, lenlv, n->line);
        else EmitValStore(stk, target, LoadLoc(lv, target, n->line));
        return;
    }
    
    
    
    
    if (want && et && IsFix(et) && (et->kind == TY_ARRAY || et->kind == TY_SLICE) &&
        want->kind == TY_ARRAY && IsBytesT(want)) {
        Loc lv;
        lv.t = et;
        lv.val = true;
        lv.s = Snapshot(et, GenXD(n, et));
        ConstructFromLoc(lv, want, stk, lenlv, n->line);
        return;
    }
    if (auto c = Is<Call>(n)) {
        if (auto from = AdtFrom(c)) {
            GenAdtAdapted(from, et, Dst { DK_STACK, stk, et, lenlv }, n->line,
                          [&](const Dst &d) { GenCallAs(c, from, d); });
            return;
        }
        ConstructCall(c, et, stk, want, lenlv);
        return;
    }
    
    
    
    
    
    if (want && IsRelRef(want) && et->kind == TY_REF) {
        EmitRelStore(stk, want, GenX(n), n->line, RelValue(n));
        return;
    }
    if (!IsBytesT(et)) {
        
        
        if ((Is<StructLit>(n) || Is<ArrayLit>(n)) && HasRelRef(et)) {
            ConsRegion cons(*this, stk);
            FixedLitAtStk(n, stk);
            return;
        }
        EmitValStore(stk, et, GenXD(n, et));
        return;
    }
    if (Is<NullLit>(n)) {
        
        assert(et->kind == TY_REF);
        if (et->ref->lenstorage == IS_VARINT) {
            L("*", Top(stk), " = 0;");
            Bump(stk, "1");
        } else {
            L("GS_MEMZERO(", Top(stk), ", ", FixedSize(et), ");");
            Bump(stk, cat(FixedSize(et)));
        }
        return;
    }
    if (auto s = Is<StrLit>(n)) {
        
        assert(et->kind == TY_ARRAY);
        EmitArrayCount(stk, et, lenlv, cat(s->val.size()), false);
        if (!s->val.empty()) {
            L("GS_MEMCPY(", Top(stk), ", ", StrRaw(s->val), ", ", s->val.size(), ");");
            Bump(stk, cat(s->val.size()));
        }
        return;
    }
    if (auto al = Is<ArrayLit>(n)) {
        
        
        if (!al->capexpr || TEq(et, al->exprtype)) {
            ConsRegion cons(*this, stk);
            GenArrayLit(al, stk, lenlv, et);
            return;
        }
    }
    if (auto sl = Is<StructLit>(n)) {
        ConsRegion cons(*this, stk);
        GenStructLit(sl, stk, lenlv);
        return;
    }
    if (auto d = Is<Dot>(n); d && d->variantconst) {
        
        assert(et->kind == TY_ENUM && et->enu->varmode);
        auto ei = EIOf(et);
        EmitValStoreTag(stk, TagStore(ei->en),
                        TagConst(ei, ei->en->VariantIndex(d->variantconst)));
        if (!lenlv.empty()) L(lenlv, " = 0;");
        return;
    }
    if (auto se = Is<SliceExpr>(n); se && et->kind == TY_ARRAY) {
        
        Loc slv;
        slv.val = true;
        slv.s = GenSlice(se);
        slv.t = ast.SliceOf(et->arr->sub, n->line);
        GenArrayFromLoc(slv, et, stk, n->line, lenlv);
        return;
    }
    
    
    
    ConstructFromLoc(GenLoc(n), et, stk, lenlv, n->line);
}



inline void CodeGen::ConstructFromLoc(Loc lv, TypeExpr *et, const string &stk,
                                      const string &lenlv, Line ln) {
    if (lv.t->kind == TY_REF && et->kind != TY_REF) DerefLoc(lv);
    if (IsResz(et)) {
        if (lv.t->kind == TY_SLICE && et->kind == TY_ARRAY) {
            GenArrayFromLoc(lv, et, stk, ln, lenlv);
            return;
        }
        if (lv.t->kind == TY_ARRAY || TEq(lv.t, et)) {
            if (TEq(lv.t, et) && et->kind != TY_ARRAY) {
                EmitRzCopy(lv, et, stk, lenlv, ln);
            } else {
                GenArrayFromLoc(lv, et, stk, ln, lenlv);
            }
            return;
        }
        
        if (et->kind == TY_ENUM && !IsResz(lv.t)) {
            GenVarEnumFromLoc(lv, et, stk);
            if (!lenlv.empty()) L(lenlv, " = 0;");
            return;
        }
        Fail(ln, cat("unsupported resizable construction from ", Mangle(lv.t)));
    }
    if (!lenlv.empty() && et->kind == TY_ARRAY) {
        
        GenArrayFromLoc(lv, et, stk, ln, lenlv);
        return;
    }
    if (TEq(lv.t, et)) {
        assert(!lv.val);
        auto sz = T();
        L("int64_t ", sz, " = ", SizeX(et, lv.s), ";");
        L("GS_MEMCPY(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
        Bump(stk, sz);
        return;
    }
    if (et->kind == TY_ARRAY) { GenArrayFromLoc(lv, et, stk, ln, lenlv); return; }
    if (et->kind == TY_ENUM && et->enu->varmode) {
        GenVarEnumFromLoc(lv, et, stk);
        return;
    }
    Fail(ln, cat("unsupported construction adaptation to ", Mangle(et)));
}











inline bool CodeGen::CallBuildsAt(Call *c, TypeExpr *rt, TypeExpr *et, const string &lenlv) {
    if (rt->kind != TY_ARRAY || et->kind != TY_ARRAY || TEq(rt, et)) return true;
    auto var = [](TypeExpr *t) { return t->arr->akind == A_VAR; };
    if (!lenlv.empty()) return IsResz(rt) || var(rt);
    if (IsResz(rt)) return c->builtin < 0 || var(et);
    return var(rt) && var(et);
}


inline void CodeGen::ConstructCall(Call *c, TypeExpr *et, const string &stk, TypeExpr *want,
                                   const string &lenlv) {
    auto rt0 = c->rettypes.empty() ? nullptr : c->rettypes[0];
    auto delivered = c->fvbody ? c->fvbody->exprtype : rt0;
    if (c->builtin < 0 && delivered && NeedsResultLocation(delivered) && TEq(delivered, et)) {
        auto p = T();
        L(CT(et), " *", p, " = (", CT(et), " *)", Top(stk), ";");
        Bump(stk, cat(FixedSize(et)));
        EmitCall(c, Dst { DK_LVALUE, cat("(*", p, ")"), et });
        return;
    }
    
    
    
    
    
    auto own = rt0 && IsBytesT(rt0) && (!IsBytesT(et) || !CallBuildsAt(c, rt0, et, lenlv));
    auto rets = EmitCall(c, own ? Dst {} : Dst { DK_STACK, stk, want, lenlv });
    if (rets.empty() || IsVoidT(et)) return;
    
    
    auto slot = want ? want : et;
    if (IsRelRef(slot)) {
        EmitRelStore(stk, slot, CallVal0(c, rets[0], slot), c->line);
        return;
    }
    
    
    if (rt0 && IsResz(rt0) && rt0->kind == TY_ARRAY && lenlv.empty() &&
        et->kind == TY_ARRAY && !rets[0].empty()) {
        Loc lv;
        lv.t = rt0;
        lv.s = cat(rets[0], ".base");
        lv.lenlv = cat(rets[0], ".len");
        GenArrayFromLoc(lv, et, stk, c->line, lenlv);
        return;
    }
    
    
    
    
    
    auto fixarr = rt0 && IsFix(rt0) && (rt0->kind == TY_ARRAY || rt0->kind == TY_SLICE);
    if (rt0 && IsBytesT(et) && (IsPlainRef(rt0) || fixarr || own)) {
        ConstructFromLoc(CallResLoc(c, rets[0]), et, stk, lenlv, c->line);
        return;
    }
    
    
    if (!IsBytesT(et)) EmitValStore(stk, et, CallVal0(c, rets[0], et));
}





inline TypeExpr *CodeGen::AdtFrom(Node *n) {
    auto et = n->exprtype;
    if (!et || et->kind != TY_ENUM) return nullptr;
    TypeExpr *from = nullptr;
    if (auto c = Is<Call>(n)) {
        if (c->fvbody) from = c->fvbody->exprtype;
        else if (c->builtin != B_COPY && !c->rettypes.empty()) from = c->rettypes[0];
    } else if (auto ib = Is<InlineBlock>(n)) {
        if (ib->spec && !ib->spec->rets.empty()) from = ib->spec->rets[0];
    }
    if (from && IsPlainRef(from)) from = from->ref->sub;
    if (!from || (from->kind != TY_VARIANT && from->kind != TY_ENUM) || TEq(from, et))
        return nullptr;
    return from;
}






inline void CodeGen::GenAdtAdapted(TypeExpr *from, TypeExpr *to, const Dst &d, Line ln,
                                   const function<void(const Dst &)> &gen) {
    if (d.k == DK_DISCARD) { gen(d); return; }
    if (to->enu->varmode) {
        assert(d.k == DK_STACK);
        if (from->kind == TY_VARIANT) {
            gen(VariantBehindTag(from, to, d));
            return;
        }
        
        Loc lv;
        lv.t = from;
        lv.val = true;
        lv.s = T();
        FixedLocal(from, lv.s);
        gen(Dst { DK_LVALUE, lv.s, from });
        GenVarEnumFromLoc(lv, to, d.s);
        if (!d.lenlv.empty()) L(d.lenlv, " = 0;");
        return;
    }
    
    
    if (from->kind == TY_VARIANT && NeedsResultLocation(from)) {
        auto base = d.s;
        if (d.k == DK_STACK) {
            auto p = T();
            L(CT(to), " *", p, " = (", CT(to), " *)", Top(d.s), ";");
            Bump(d.s, cat(FixedSize(to)));
            base = cat("(*", p, ")");
        }
        auto ei = EIOf(to);
        auto vi = ei->en->VariantIndex(from->var->variant);
        L(base, ".tag = ", TagConst(ei, vi), ";");
        gen(Dst { DK_LVALUE, cat(base, ".u.v_", Sanitize(from->var->variant->name)), from });
        return;
    }
    Loc lv;
    lv.t = from;
    if (IsBytesT(from)) {
        string stk;
        lv.s = BytesTemp(stk);
        lv.stk = stk;
        gen(Dst { DK_STACK, stk, from });
    } else {
        lv.val = true;
        lv.s = T();
        FixedLocal(from, lv.s);
        gen(Dst { DK_LVALUE, lv.s, from });
    }
    auto x = AdaptToFixed(lv, to, ln);
    if (d.k == DK_LVALUE) L(d.s, " = ", x, ";");
    else EmitValStore(d.s, to, x);
}





inline Dst CodeGen::VariantBehindTag(TypeExpr *from, TypeExpr *to, const Dst &d) {
    assert(d.k == DK_STACK);
    auto ei = EIOf(to);
    EmitValStoreTag(d.s, TagStore(ei->en), TagConst(ei, ei->en->VariantIndex(from->var->variant)));
    auto rz = IsResz(from);
    if (!d.lenlv.empty() && !rz) L(d.lenlv, " = 0;");
    return Dst { DK_STACK, d.s, from, rz ? d.lenlv : "" };
}


inline void CodeGen::GenCallAs(Call *c, TypeExpr *t, const Dst &d) {
    if (d.k == DK_STACK) {
        ConstructCall(c, t, d.s, d.t, d.lenlv);
        return;
    }
    auto rets = EmitCall(c, d);
    if (d.k != DK_LVALUE || rets.empty()) return;
    auto r0 = CallVal0(c, rets[0], t);
    if (r0 != d.s) L(d.s, " = ", r0, ";");
}

inline void CodeGen::GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                                     const string &lenlv) {
    if (IsStaticLimited(et)) {
        
        
        assert(lenlv.empty());
        EmitValStore(stk, et, AdaptToFixed(lv, et, ln));
        return;
    }
    auto v = ArrayView(lv);
    auto nn = T();
    L("int64_t ", nn, " = ", v.len, ";");
    EmitArrayCount(stk, et, lenlv, nn, true);
    EmitCopyElems(stk, et->arr->sub, v.elems, nn);
}




inline void CodeGen::EmitRzCopy(Loc lv, TypeExpr *et, const string &stk, const string &lenlv,
                                Line ln) {
    assert(!lenlv.empty());
    if (IsFrameObj(et)) {
        
        
        if (!lv.val) lv = FoView(lv);
        auto src = T();
        L(CT(et), " ", src, " = ", lv.s, ";");
        L(lenlv, " = ", src, ";");
        auto dh = FoTailHdr(et, lenlv), sh = FoTailHdr(et, src);
        L(dh, ".base = ", Top(stk), ";");
        EmitCopyElems(stk, FoTailArr(et)->arr->sub, cat(sh, ".base"), cat(sh, ".len"));
        return;
    }
    int64_t prefix = 0;
    TypeExpr *elem = nullptr;
    if (!RzShape(et, prefix, elem))
        Fail(ln, "copying this resizable value is unsupported (variable-size prefix)");
    auto len = T();
    L("int64_t ", len, " = ", lv.lenlv, ";");
    if (prefix) {
        L("GS_MEMCPY(", Top(stk), ", ", lv.s, ", ", prefix, ");");
        Bump(stk, cat(prefix));
    }
    EmitCopyElems(stk, elem, cat("(", lv.s, ") + ", prefix), len);
    L(lenlv, " = ", len, ";");
}





inline void CodeGen::FoBytes(TypeExpr *t, const string &obj, const string &p, bool tobytes) {
    auto si = SI(t);
    auto &fields = si->st->fields;
    int64_t off = 0;
    for (size_t i = 0; i < fields.size(); i++) {
        auto at = off ? cat("(", p, " + ", off, ")") : p;
        if (fields[i].ispad) {
            if (fields[i].padsize <= 0) continue;
            if (tobytes) L("GS_MEMZERO(", at, ", ", fields[i].padsize, ");");
            off += fields[i].padsize;
            continue;
        }
        auto ft = si->ftypes[i];
        auto flv = cat(obj, ".", Sanitize(fields[i].name));
        if (IsResz(ft)) {
            if (IsFrameObj(ft)) FoBytes(ft, flv, at, tobytes);
            return;
        }
        if (tobytes) L("*(", CT(ft), " *)", at, " = ", flv, ";");
        else L(flv, " = *(", CT(ft), " *)", at, ";");
        off += FixedSize(ft);
    }
}




inline CodeGen::Loc CodeGen::FoView(const Loc &lv) {
    assert(!lv.val && !lv.lenlv.empty());
    Loc r;
    r.t = lv.t;
    r.val = true;
    r.s = T();
    L(CT(lv.t), " ", r.s, ";");
    FoBytes(lv.t, r.s, lv.s, false);
    auto th = FoTailHdr(lv.t, r.s);
    L(th, ".base = (", lv.s, ") + ", FoBytesPrefix(lv.t), ";");
    L(th, ".len = ", lv.lenlv, ";");
    return r;
}







inline void CodeGen::GenFoAsBytes(Node *n, const string &stk, TypeExpr *t, const string &lenlv) {
    OpenAt open(*this, stk);
    if (auto sl = Is<StructLit>(n); sl && TEq(sl->exprtype, t) && !fillvalues.count(n)) {
        auto si = SI(t);
        GenFieldInits(sl, si->st->fields, si->ftypes, stk, lenlv);
        return;
    }
    auto pre = FoBytesPrefix(t);
    string p;
    if (pre) {
        p = T();
        L("uint8_t *", p, " = ", Top(stk), ";");
        Bump(stk, cat(pre));
    }
    auto h = T();
    L(CT(t), " ", h, ";");
    GenConstruct(n, stk, t, h);
    if (pre) FoBytes(t, h, p, true);
    L(lenlv, " = ", FoTailHdr(t, h), ".len;");
}



inline bool CodeGen::RzShape(TypeExpr *t, int64_t &prefix, TypeExpr *&elem) {
    if (t->kind == TY_ARRAY) {
        elem = t->arr->sub;
        return true;
    }
    if (t->kind == TY_STRUCT) {
        auto si = SI(t);
        auto last = LastRealField(si->st->fields);
        assert(last >= 0);
        vector<Field> pre(si->st->fields.begin(), si->st->fields.begin() + last);
        vector<TypeExpr *> pret(si->ftypes.begin(), si->ftypes.begin() + last);
        for (auto ft : pret) if (ft && !IsFix(ft)) return false;
        auto lo = LayoutFields(pre, pret);
        int64_t sub = 0;
        if (!RzShape(si->ftypes[last], sub, elem)) return false;
        prefix += lo.size + sub;
        return true;
    }
    return false;   
}

inline void CodeGen::GenVarEnumFromLoc(Loc lv, TypeExpr *et, const string &stk) {
    auto ei = EIOf(et);
    auto ts = TagStore(ei->en);
    if (lv.t->kind == TY_VARIANT) {
        auto vi = ei->en->VariantIndex(lv.t->var->variant);
        EmitValStoreTag(stk, ts, TagConst(ei, vi));
        if (!lv.val) {
            auto sz = T();
            L("int64_t ", sz, " = ", SizeX(lv.t, lv.s), ";");
            L("GS_MEMCPY(", Top(stk), ", ", lv.s, ", (size_t)", sz, ");");
            Bump(stk, sz);
        } else {
            EmitValStore(stk, lv.t, lv.s);
        }
        return;
    }
    assert(lv.t->kind == TY_ENUM && !lv.t->enu->varmode);
    auto sv = T();
    FixedLocal(lv.t, sv, lv.s);
    EmitValStoreTag(stk, ts, cat("(int64_t)", sv, ".tag"));
    L("switch (", sv, ".tag) {");
    for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
        auto psz = VariantLayout(ei, (int)vi).size;
        L("case ", TagConst(ei, (int)vi), ":");
        ind++;
        if (psz) {
            L("GS_MEMCPY(", Top(stk), ", &", sv, ".u.v_",
              Sanitize(ei->en->variants[vi].name), ", ", psz, ");");
            Bump(stk, cat(psz));
        }
        L("break;");
        ind--;
    }
    L("}");
}





inline void CodeGen::FreezeFill(Node *n, TypeExpr *t, vector<Node *> &added) {
    if (!n || Is<SelfRef>(n) || fillvalues.count(n)) return;
    if (HasRelRefAny(t)) {
        if (auto sl = Is<StructLit>(n)) {
            const vector<TypeExpr *> *fields;
            if (t->kind == TY_STRUCT) fields = &SI(t)->ftypes;
            else {
                auto ei = t->kind == TY_VARIANT ? EIVar(t) : EIOf(t);
                auto vi = ei->en->VariantIndex(t->kind == TY_VARIANT ? t->var->variant : sl->variant);
                fields = &ei->vftypes[vi];
            }
            for (size_t i = 0; i < sl->inits.size(); i++)
                FreezeFill(sl->inits[i].val, (*fields)[sl->fieldindices[i]], added);
            return;
        }
        if (auto al = Is<ArrayLit>(n); al && t->kind == TY_ARRAY) {
            if (al->capexpr) FreezeFill(al->capexpr, ast.inttypes[IS_I64], added);
            if (al->fillval) FreezeFill(al->fillval, t->arr->sub, added);
            else for (auto e : al->elems) FreezeFill(e, t->arr->sub, added);
            return;
        }
    }
    if (IsVarintT(t)) t = ast.inttypes[IS_I64];
    if (IsRelRef(t))
        t = ast.RefTo(t->ref->sub, n->line, t->ref->optional);
    Loc lv;
    lv.t = t;
    if (IsResz(t)) {
        auto h = RzTemp(t, lv.stk);
        GenAny(n, Dst { DK_STACK, lv.stk, t, RzLenLv(t, h) });
        lv = RzTempLoc(t, h, lv.stk);
    } else if (IsBytesT(t)) {
        lv.s = BytesTemp(lv.stk);
        GenConstruct(n, lv.stk, t);
    } else {
        lv.val = true;
        lv.s = Snapshot(t, GenXD(n, t));
    }
    fillvalues.emplace(n, lv);
    added.push_back(n);
}




inline void CodeGen::FixedLitAtStk(Node *n, const string &stk) {
    auto et = n->exprtype;
    auto Gap = [&](int64_t bytes) {
        if (bytes <= 0) return;
        L("GS_MEMZERO(", Top(stk), ", ", bytes, ");");
        Bump(stk, cat(bytes));
    };
    
    
    auto EmitF = [&](Node *init, TypeExpr *ft, int64_t fieldoff = 0) {
        if (!init) {   
            Gap(FixedSize(ft));
            return;
        }
        if (IsRelRef(ft)) {
            if (Is<SelfRef>(init)) {
                EmitRelSelfStore(stk, ft, fieldoff, n->line);
            } else {
                auto f = RelValue(init);
                f.apart = RelSlotApart(ft, et, fieldoff);
                EmitRelStore(stk, ft, GenX(init), n->line, f);
            }
            return;
        }
        if ((Is<StructLit>(init) || Is<ArrayLit>(init)) && HasRelRef(ft)) {
            FixedLitAtStk(init, stk);
            return;
        }
        
        
        
        if (HasRelRef(ft) && IsFix(ft)) {
            GenConstruct(init, stk, ft);
            return;
        }
        EmitValStore(stk, ft, GenX(init));
    };
    if (auto al = Is<ArrayLit>(n)) {
        auto elem = et->arr->sub;
        auto count = LitCount(al);
        if (et->arr->akind == A_LIMITED) EmitLenStore(stk, LenStore(et->arr), cat(count));
        if (al->fillval) {
            FillScope fill(*this, al->fillval, elem);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            PushSc(SC_PLAIN);
            EmitF(al->fillval, elem);
            PopSc();
            ind--;
            L("}");
        } else {
            for (auto e : al->elems) EmitF(e, elem);
        }
        if (et->arr->akind == A_LIMITED) Gap((ArrSize(et->arr) - count) * FixedSize(elem));
        return;
    }
    auto sl = Is<StructLit>(n);
    assert(sl);
    
    
    auto emitfields = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                          const Layout &lo, int64_t total, int64_t base) {
        int64_t cur = 0;
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            Gap(lo.offs[i] - cur);
            cur = lo.offs[i];
            EmitF(sl->InitFor((int)i), ftypes[i], base + lo.offs[i]);
            cur += FixedSize(ftypes[i]);
        }
        Gap(total - cur);
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        auto &lo = StructLayout(si);
        emitfields(si->st->fields, si->ftypes, lo, lo.size, 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        auto &lo = VariantLayout(ei, vi);
        emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], lo, lo.size, 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = ei->en->VariantIndex(sl->variant);
    EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
    auto &lo = VariantLayout(ei, vi);
    emitfields(ei->en->variants[vi].fields, ei->vftypes[vi], lo, lo.size, TagSize(ei->en));
    Gap(FixedSize(et) - TagSize(ei->en) - lo.size);
}






inline void CodeGen::FixedLitAt(Node *n, const string &dst) {
    auto p = T();
    L(CT(n->exprtype), " *", p, " = &", dst, ";");
    FixedLitAtLv(n, cat("(*", p, ")"), true);
}


inline void CodeGen::FixedLitAtLv(Node *n, const string &base, bool inroot) {
    if (auto al = Is<ArrayLit>(n)) { FixedArrayLitAt(al, base, inroot); return; }
    auto sl = Is<StructLit>(n);
    assert(sl);
    StructLitAt(sl, base, inroot);
}

inline void CodeGen::FixedArrayLitAt(ArrayLit *al, const string &base, bool inroot) {
    auto et = al->exprtype;
    assert(et->kind == TY_ARRAY);
    auto elem = et->arr->sub;
    auto rel = IsRelRef(elem);
    auto emitelem = [&](Node *e, const string &path) {
        if (rel) EmitRelStoreAt(cat("(uint8_t *)&", path), elem, GenX(e), al->line, inroot,
                                RelValue(e));
        else GenAny(e, Dst { DK_LVALUE, path, elem });
    };
    auto count = LitCount(al);
    if (et->arr->akind == A_LIMITED) L(base, ".len = ", count, ";");
    if (al->fillval) {
        int byte;
        if (!HasRelRef(elem) && UniformFillByte(al->fillval, elem, byte)) {
            L("memset(", base, ".e, ", byte, ", ", count, " * sizeof(", base, ".e[0]));");
            return;
        }
        FillScope fill(*this, al->fillval, elem);
        
        auto perelem = HasRelRef(elem);
        auto fv = perelem ? string() : GenXD(al->fillval, elem);
        auto iv = T();
        L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
        ind++;
        auto path = cat(base, ".e[", iv, "]");
        PushSc(SC_PLAIN);
        if (perelem) emitelem(al->fillval, path);
        else L(path, " = ", fv, ";");
        PopSc();
        ind--;
        L("}");
        return;
    }
    for (size_t i = 0; i < al->elems.size(); i++)
        emitelem(al->elems[i], cat(base, ".e[", i, "]"));
}



inline void CodeGen::StructLitAt(StructLit *sl, const string &base, bool inroot) {
    auto et = sl->exprtype;
    if (ZeroDefaultValue(sl, et)) {
        EmitDefaultInto(base, et);
        return;
    }
    
    
    auto fieldset = [&](const string &b, const vector<Field> &fields,
                        const vector<TypeExpr *> &ftypes, const Layout &lo, int64_t baseoff) {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].ispad) continue;
            auto init = sl->InitFor((int)i);
            auto ft = ftypes[i];
            auto path = cat(b, ".", Sanitize(fields[i].name));
            if (!init) {   
                assert(ft->kind == TY_REF);
                L("GS_MEMZERO(&", path, ", sizeof(", path, "));");
                continue;
            }
            if (IsRelRef(ft)) {
                if (Is<SelfRef>(init))
                    EmitRelSelfAt(cat("(uint8_t *)&", path), ft, baseoff + lo.offs[i],
                                  sl->line, inroot);
                else {
                    auto f = RelValue(init);
                    f.apart = RelSlotApart(ft, et, baseoff + lo.offs[i]);
                    EmitRelStoreAt(cat("(uint8_t *)&", path), ft, GenX(init), sl->line, inroot, f);
                }
                continue;
            }
            GenAny(init, Dst { DK_LVALUE, path, ft });
        }
    };
    if (et->kind == TY_STRUCT) {
        auto si = SI(et);
        
        
        if (LastRealField(si->st->fields) < 0) L("GS_MEMZERO(&", base, ", sizeof(", base, "));");
        fieldset(base, si->st->fields, si->ftypes, StructLayout(si), 0);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        if (LastRealField(ei->en->variants[vi].fields) < 0)
            L("GS_MEMZERO(&", base, ", sizeof(", base, "));");
        fieldset(base, ei->en->variants[vi].fields, ei->vftypes[vi], VariantLayout(ei, vi), 0);
        return;
    }
    assert(et->kind == TY_ENUM && !et->enu->varmode && sl->variant);
    auto ei = EIOf(et);
    auto vi = ei->en->VariantIndex(sl->variant);
    L(base, ".tag = ", TagConst(ei, vi), ";");
    if (!EmptyLayout(ei->en->variants[vi].fields))
        fieldset(cat(base, ".u.v_", Sanitize(ei->en->variants[vi].name)),
                 ei->en->variants[vi].fields, ei->vftypes[vi], VariantLayout(ei, vi),
                 TagSize(ei->en));
}

inline void CodeGen::EmitValStoreTag(const string &stk, IntStorage ts, const string &x) {
    L("*(", IntCT(ts), " *)", Top(stk), " = (", IntCT(ts), ")(", x, ");");
    Bump(stk, cat(IntSize(ts)));
}



inline void CodeGen::GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv,
                                TypeExpr *as) {
    auto et = as ? as : al->exprtype;
    assert(et->kind == TY_ARRAY);
    auto elem = et->arr->sub;
    if (al->capexpr) {
        
        
        assert(et->arr->akind == A_LIMITED);
        auto cv = GenX(al->capexpr);
        auto t = T();
        L("int64_t ", t, " = ", cv, ";");
        L("if (", t, " < 0 || ", t, " > (int64_t)UINT32_MAX) "
          "gs_abort(GS_E_CAPRANGE, ", LocArgs(al->line), ");");
        L("*(gs_u32 *)", Top(stk), " = (uint32_t)", t, ";");
        L("*(gs_u32 *)(", Top(stk), " + 4) = 0;");
        Bump(stk, cat("8 + ", t, " * ", FixedSize(elem)));
        return;
    }
    auto count = LitCount(al);
    EmitArrayCount(stk, et, lenlv, cat(count), false);
    if (al->fillval) {
        FillScope fill(*this, al->fillval, elem);
        if (IsVarintT(elem)) {
            
            auto i64 = ast.inttypes[IS_I64];
            auto fv = Snapshot(i64, GenXD(al->fillval, i64));
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitVarintStore(stk, fv);
            ind--;
            L("}");
        } else if (IsBytesT(elem) || HasRelRef(elem)) {
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            PushSc(SC_PLAIN);
            GenConstruct(al->fillval, stk, elem);
            PopSc();
            ind--;
            L("}");
        } else {
            auto fv = GenXD(al->fillval, elem);
            auto iv = T();
            L("for (int64_t ", iv, " = 0; ", iv, " < ", count, "; ", iv, "++) {");
            ind++;
            EmitValStore(stk, elem, fv);
            ind--;
            L("}");
        }
        return;
    }
    for (auto e : al->elems) GenConstruct(e, stk, elem);
}




inline bool CodeGen::HasSelfInit(StructLit *sl) {
    for (auto &fi : sl->inits) if (Is<SelfRef>(fi.val)) return true;
    return false;
}





inline void CodeGen::GenStructLit(StructLit *sl, const string &stk, const string &lenlv) {
    auto et = sl->exprtype;
    string selfbase;
    if (HasSelfInit(sl)) {
        selfbase = T();
        L("uint8_t *", selfbase, " = ", Top(stk), ";");
    }
    if (et->kind == TY_ENUM) {
        assert(et->enu->varmode && sl->variant);
        auto ei = EIOf(et);
        auto vi = ei->en->VariantIndex(sl->variant);
        EmitValStoreTag(stk, TagStore(ei->en), TagConst(ei, vi));
        
        
        if (!lenlv.empty() && IsFix(VariantType(et, vi))) L(lenlv, " = 0;");
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi], stk, lenlv, selfbase);
        return;
    }
    if (et->kind == TY_VARIANT) {
        auto ei = EIVar(et);
        auto vi = ei->en->VariantIndex(et->var->variant);
        GenFieldInits(sl, ei->en->variants[vi].fields, ei->vftypes[vi], stk, lenlv, selfbase);
        return;
    }
    assert(et->kind == TY_STRUCT);
    auto si = SI(et);
    if (IsFrameObj(et)) {
        if (!selfbase.empty()) Fail(sl->line, "self references in a frame object are unsupported");
        GenFrameObjLit(sl, si, stk, lenlv);
        return;
    }
    GenFieldInits(sl, si->st->fields, si->ftypes, stk, lenlv, selfbase);
}




inline void CodeGen::GenFrameObjLit(StructLit *sl, StructInst *si, const string &stk,
                                    const string &obj) {
    assert(!obj.empty());
    auto &fields = si->st->fields;
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].ispad) continue;
        auto init = sl->InitFor((int)i);
        auto ft = si->ftypes[i];
        auto flv = cat(obj, ".", Sanitize(fields[i].name));
        if (IsResz(ft)) {
            assert(init);
            if (IsFrameObj(ft)) {
                GenConstruct(init, stk, ft, flv);
            } else {
                L(flv, ".base = ", Top(stk), ";");
                L(flv, ".len = 0;");
                GenConstruct(init, stk, ft, cat(flv, ".len"));
            }
            continue;
        }
        if (!init || Is<NullLit>(init)) {
            assert(ft->kind == TY_REF && ft->ref->optional);
            L("GS_MEMZERO(&", flv, ", sizeof(", flv, "));");
            continue;
        }
        
        
        if (IsRelRef(ft)) {
            EmitRelStoreAt(cat("(uint8_t *)&", flv), ft, GenX(init), sl->line, true,
                           RelValue(init));
            continue;
        }
        GenAny(init, Dst { DK_LVALUE, flv, ft });
    }
}

inline void CodeGen::GenFieldInits(StructLit *sl, const vector<Field> &fields,
                                   const vector<TypeExpr *> &ftypes, const string &stk,
                                   const string &lenlv, const string &selfbase) {
    for (size_t i = 0; i < fields.size(); i++) {
        if (fields[i].ispad) {
            auto n = fields[i].padsize > 0 ? fields[i].padsize : 0;
            if (n) { L("GS_MEMZERO(", Top(stk), ", ", n, ");"); Bump(stk, cat(n)); }
            continue;
        }
        auto init = sl->InitFor((int)i);
        auto ft = ftypes[i];
        if (IsRelRef(ft)) {
            
            if (!init) {
                if (ft->ref->lenstorage == IS_VARINT) { L("*", Top(stk), " = 0;"); Bump(stk, "1"); }
                else { L("GS_MEMZERO(", Top(stk), ", ", IntSize((IntStorage)ft->ref->lenstorage), ");");
                       Bump(stk, cat(IntSize((IntStorage)ft->ref->lenstorage))); }
                continue;
            }
            
            
            auto rv = Is<SelfRef>(init) ? selfbase : GenX(init);
            EmitRelStore(stk, ft, rv, sl->line, RelValue(init));
            continue;
        }
        if (!init) {
            
            assert(ft->kind == TY_REF && ft->ref->optional);
            L("GS_MEMZERO(", Top(stk), ", ", FixedSize(ft), ");");
            Bump(stk, cat(FixedSize(ft)));
            continue;
        }
        
        
        
        if (IsResz(ft) && IsFrameObj(ft)) GenFoAsBytes(init, stk, ft, lenlv);
        else GenConstruct(init, stk, ft, IsResz(ft) ? lenlv : "");
    }
}

}  
