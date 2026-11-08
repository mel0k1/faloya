


#pragma once

namespace faloya {








inline TypeExpr *CodeGen::GrowU8() {
    if (growu8) return growu8;
    growu8 = ast.ArrayOf(ast.inttypes[IS_U8], A_GROW, Line {});
    return growu8;
}


inline CodeGen::Loc CodeGen::TempBuilder() {
    string stk;
    auto h = RzTemp(GrowU8(), stk);
    return RzTempLoc(GrowU8(), h, stk);
}

inline Call *CodeGen::FmtContext(Call *c, Node *arg) {
    if (!c || c->fmtcontexts.empty()) return c;
    auto args = c->ArgNodes();
    size_t start = c->builtin == B_FORMAT ? 1 : 0;
    for (size_t i = start; i < args.size(); i++)
        if (args[i] == arg) return c->fmtcontexts.at(i - start);
    return c;
}

inline FnSpec *CodeGen::FmtSpecFor(Call *c, TypeExpr *t) {
    if (!c) return nullptr;
    for (auto &fs : c->fmtspecs) if (TEq(fs.first, t)) return fs.second;
    return nullptr;
}


inline bool CodeGen::SimpleText(Call *c, TypeExpr *t) {
    if (FmtSpecFor(c, t)) return false;
    if (t->kind == TY_INT || t->kind == TY_FLT || t->kind == TY_BOOL) return true;
    if (t->kind == TY_ARRAY) return IsU8(t->arr->sub);
    if (t->kind == TY_SLICE) return IsU8(t->sub);
    return false;
}

inline void CodeGen::RenderLit(Loc &out, const string &text) {
    L("GS_MEMCPY(", Top(out.stk), ", ", StrRaw(text), ", ", text.size(), ");");
    Bump(out.stk, cat(text.size()));
    L(out.lenlv, " += ", text.size(), ";");
}


inline void CodeGen::RenderN(Loc &out, const string &nexpr) {
    auto n = T();
    L("int64_t ", n, " = ", nexpr, ";");
    Bump(out.stk, n);
    L(out.lenlv, " += ", n, ";");
}

inline void CodeGen::RenderLoc(Loc &out, Loc lv, TypeExpr *t, bool nested, Call *c, Line ln) {
    
    
    if (lv.t->kind == TY_REF && t->kind != TY_REF) DerefLoc(lv);
    if (auto sp = FmtSpecFor(c, t)) { EmitUserFormat(out, lv, sp, ln); return; }
    
    
    
    
    
    
    auto nominal = t->kind == TY_STRUCT || t->kind == TY_VARIANT || t->kind == TY_ENUM;
    if (nominal && (!IsResz(t) || (!lv.hdr.empty() && !lv.stk.empty()))) {
        for (auto r : rendering) {
            if (!TEq(r, t)) continue;
            EmitRenderCall(out, lv, t, ln);
            return;
        }
    }
    struct Inside {
        vector<TypeExpr *> &types;
        bool in;
        ~Inside() { if (in) types.pop_back(); }
    } inside { rendering, nominal };
    if (nominal) rendering.push_back(t);
    
    
    
    if (lv.viaref && c && !c->fmtspecs.empty() && nominal) PinLoc(lv);
    switch (t->kind) {
        case TY_INT: {
            auto vt = t->intstorage == IS_VARINT ? ast.inttypes[IS_I64] : t;
            auto x = LoadLoc(lv, vt, ln);
            if (vt->intstorage == IS_U64)
                RenderN(out, cat("gs_fmt_u64(", Top(out.stk), ", ", x, ")"));
            else
                RenderN(out, cat("gs_fmt_i64(", Top(out.stk), ", (int64_t)(", x, "))"));
            return;
        }
        case TY_FLT: {
            auto x = LoadLoc(lv, t, ln);
            if (IsF32(t)) RenderN(out, cat("gs_fmt_f32(", Top(out.stk), ", ", x, ")"));
            else RenderN(out, cat("gs_fmt_f64(", Top(out.stk), ", (double)(", x, "))"));
            return;
        }
        case TY_BOOL: {
            auto x = LoadLoc(lv, t, ln);
            RenderN(out, cat("gs_fmt_bool(", Top(out.stk), ", ", x, ")"));
            return;
        }
        case TY_REF: {
            if (lv.t->kind != TY_REF) Fail(ln, "internal: reference render of a value");
            auto x = LoadLoc(lv, lv.t, ln);
            if (t->ref->optional) {
                auto isnull = IsResz(t->ref->sub) ? cat("(", x, ").hdr == 0")
                                                        : cat("(", x, ") == NULL");
                L("if (", isnull, ") {");
                ind++;
                RenderLit(out, "null");
                ind--;
                L("} else {");
                ind++;
                Loc pl = lv;
                DerefLoc(pl);
                RenderLoc(out, pl, t->ref->sub, nested, c, ln);
                ind--;
                L("}");
                return;
            }
            DerefLoc(lv);
            RenderLoc(out, lv, t->ref->sub, nested, c, ln);
            return;
        }
        case TY_ARRAY: case TY_SLICE: {
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            auto v = ArrayView(lv);
            if (IsU8(elem)) {
                if (nested) {
                    RenderN(out, cat("gs_fmt_quoted(", Top(out.stk), ", (const uint8_t *)(",
                                     v.elems, "), ", v.len, ")"));
                } else {
                    auto n = T();
                    L("int64_t ", n, " = ", v.len, ";");
                    L(CopyFn(), "(", Top(out.stk), ", (const uint8_t *)(", v.elems,
                      "), (size_t)", n, ");");
                    Bump(out.stk, n);
                    L(out.lenlv, " += ", n, ";");
                }
                return;
            }
            RenderLit(out, "[");
            auto i = T();
            auto cnt = T();
            L("int64_t ", cnt, " = ", v.len, ";");
            
            
            
            
            
            auto p = T();
            if (v.typedelems) L(CT(elem), " *", p, " = ", v.elems, ";");
            else L("uint8_t *", p, " = ", v.elems, ";");
            L("for (int64_t ", i, " = 0; ", i, " < ", cnt, "; ", i, "++) {");
            ind++;
            PushSc(SC_PLAIN);
            L("if (", i, ") {");
            ind++;
            RenderLit(out, ", ");
            ind--;
            L("}");
            Loc el;
            if (v.typedelems) {
                el.t = elem;
                el.val = true;
                el.s = cat(p, "[", i, "]");
                el.stk = lv.stk;
            } else if (IsFix(elem)) {
                el = BytesLoc(cat("(", p, " + ", i, " * ", FixedSize(elem), ")"), elem, lv);
            } else {
                el = BytesLoc(p, elem, lv);
            }
            
            el.viaref = elem->kind == TY_REF;
            RenderLoc(out, el, elem, true, c, ln);
            if (!v.typedelems && !IsFix(elem)) L(p, " += ", SizeX(elem, p), ";");
            PopSc();
            ind--;
            L("}");
            RenderLit(out, "]");
            return;
        }
        case TY_STRUCT: {
            auto si = SI(t);
            string name;
            t->Dump(name);
            RenderLit(out, cat(name, " { "));
            auto first = true;
            for (auto i = 0; i < (int)si->st->fields.size(); i++) {
                if (si->st->fields[i].ispad) continue;
                if (!first) RenderLit(out, ", ");
                first = false;
                RenderLoc(out, FieldLocAt(lv, i), si->ftypes[i], true, c, ln);
            }
            RenderLit(out, " }");
            return;
        }
        case TY_VARIANT:
            RenderVariant(out, lv, t, c, ln);
            return;
        case TY_ENUM: {
            auto ei = EIOf(t);
            auto varmode = t->enu->varmode;
            auto ts = TagSize(ei->en);
            string tag;
            if (lv.val) tag = cat(lv.s, ".tag");
            else if (varmode) tag = cat("*(", IntCT(TagStore(ei->en)), " *)(", lv.s, ")");
            else tag = cat("((", CT(t), " *)(", lv.s, "))->tag");
            L("switch (", tag, ") {");
            for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
                L("case ", TagConst(ei, (int)vi), ": {");
                ind++;
                PushSc(SC_PLAIN);
                auto vt = VariantType(t, (int)vi);
                Loc pl;
                if (!EmptyLayout(ei->en->variants[vi].fields)) {
                    if (lv.val) {
                        pl = lv;
                        pl.t = vt;
                        pl.s = cat(lv.s, ".u.v_", Sanitize(ei->en->variants[vi].name));
                    } else if (varmode) {
                        pl = BytesLoc(cat("((", lv.s, ") + ", ts, ")"), vt, lv);
                    } else {
                        pl = BytesLoc(cat("((uint8_t *)&((", CT(t), " *)(", lv.s, "))->u.v_",
                                          Sanitize(ei->en->variants[vi].name), ")"), vt, lv);
                    }
                    
                    
                    
                    
                    
                    
                    
                    if (!varmode && c && !c->fmtspecs.empty()) {
                        auto cp = T();
                        FixedLocal(vt, cp, pl.s);
                        pl.s = cp;
                        pl.viaref = false;
                    }
                } else {
                    pl = lv;
                    pl.t = vt;
                }
                
                
                
                RenderVariant(out, pl, vt, c, ln);
                PopSc();
                L("break;");
                ind--;
                L("}");
            }
            L("default: break;");
            L("}");
            return;
        }
        default:
            Fail(ln, cat("cannot render a value of type ", Mangle(t)));
    }
}


inline void CodeGen::RenderVariant(Loc &out, Loc lv, TypeExpr *t, Call *c, Line ln) {
    auto ei = EIVar(t);
    auto vi = ei->en->VariantIndex(t->var->variant);
    auto &v = ei->en->variants[vi];
    if (v.fields.empty()) {
        RenderLit(out, cat(ei->en->name, ".", v.name));
        return;
    }
    RenderLit(out, cat(v.name, " { "));
    auto first = true;
    for (auto i = 0; i < (int)v.fields.size(); i++) {
        if (v.fields[i].ispad) continue;
        if (!first) RenderLit(out, ", ");
        first = false;
        RenderLoc(out, FieldLocAt(lv, i), ei->vftypes[vi][i], true, c, ln);
    }
    RenderLit(out, " }");
}




inline string CodeGen::RefArg(const Loc &lv, TypeExpr *sub, Line ln) {
    if (IsResz(sub)) {
        if (lv.hdr.empty() || lv.stk.empty())
            Fail(ln, "a format overload by reference needs a resizable with its own header");
        auto rr = T();
        L("gs_rref ", rr, " = { (gs_rhdr *)&", lv.hdr, ", ", lv.stk, " };");
        return rr;
    }
    if (IsBytesT(sub)) return lv.val ? cat("(uint8_t *)&", lv.s) : lv.s;
    if (IsVarintT(lv.t)) {
        
        
        auto x = T();
        auto ct = CT(sub), val = LoadLoc(lv, sub, ln);
        L(ct, " ", x, " = ", val, ";");
        return cat("&", x);
    }
    return lv.val ? cat("&", lv.s) : cat("(", CT(sub), " *)(", lv.s, ")");
}




inline string CodeGen::PrefArg(const Loc &lv, Line ln) {
    if (lv.hdr.empty() || lv.stk.empty() || lv.fl.empty() || lv.flstk.empty())
        Fail(ln, "internal: a pool reference to storage that is no pool");
    auto p = T();
    L("gs_pref ", p, " = { &", lv.hdr, ", ", lv.stk, ", &", lv.fl, ", ", lv.flstk, " };");
    return p;
}


inline void CodeGen::EmitUserFormat(Loc &out, Loc lv, FnSpec *sp, Line ln) {
    assert(!out.hdr.empty() && !out.stk.empty());
    PushSc(SC_PLAIN);
    auto outpool = IsPoolParam(sp, 0), pool = IsPoolParam(sp, 1);
    string r;
    if (outpool) {
        r = PrefArg(out, ln);
    } else {
        r = T();
        L("gs_rref ", r, " = { (gs_rhdr *)&", out.hdr, ", ", out.stk, " };");
    }
    auto pt = sp->argtypes[1];
    auto arg = pool                ? PrefArg(lv, ln)
               : pt->kind == TY_REF ? RefArg(lv, pt->ref->sub, ln)
                                    : LoadLoc(lv, pt, ln);
    if (IsLargeFixed(pt)) arg = cat("&", Snapshot(pt, arg));
    MarkFlush();
    auto &ki = sinfo[sp];
    
    
    
    
    
    if (ki.needssp) NoStackAcrossCycleCall(sp, ln);
    if (ki.needssp) NoteStackCall(sp, ln);
    vector<string> args;
    PushArg(args, sp->argtypes[0], outpool, r);
    if (IsLargeFixed(pt)) args.push_back(arg);
    else PushArg(args, pt, pool, arg);
    if (ki.needssp) args.push_back(SpTop());
    if (ki.located) args.push_back(CallerLoc(ln));
    string argstr;
    for (auto &a : args) Append(argstr, argstr.empty() ? "" : ", ", a);
    L(ki.cname, "(", argstr, ");");
    MarkReload();   
    PopSc();
}





inline string CodeGen::RenderFn(TypeExpr *t, Line ln) {
    auto m = Mangle(t);
    if (auto it = renderfns.find(m); it != renderfns.end()) return it->second;
    auto name = Unique(cat("gs_render_", m));
    renderfns[m] = name;
    auto vt = IsResz(t) ? string("gs_rref") : IsBytesT(t) ? string("uint8_t *") : cat(CT(t), " *");
    auto sig = cat("static void ", name, "(gs_rref gs_out, ", vt, " gs_v)");
    Append(protos, sig, ";\n");
    renderqueue.push_back({ t, ln, sig });
    return name;
}

inline void CodeGen::EmitRenderCall(Loc &out, const Loc &lv, TypeExpr *t, Line ln) {
    assert(!out.hdr.empty() && !out.stk.empty());
    auto fn = RenderFn(t, ln);
    MarkFlush();
    auto r = T();
    L("gs_rref ", r, " = { (gs_rhdr *)&", out.hdr, ", ", out.stk, " };");
    auto arg = RefArg(lv, t, ln);
    L(fn, "(", r, ", ", arg, ");");
    MarkReload();
}













inline void CodeGen::EmitRenderFn(RenderFnReq r) {
    curspec = nullptr;
    curinfo = nullptr;
    ResetFnState();
    spexpr = "0";
    PushSc(SC_FN);
    auto out = FatRefLoc("gs_out", GrowU8());
    Loc v;
    if (IsResz(r.t)) {
        v = FatRefLoc("gs_v", r.t);
    } else {
        v.t = r.t;
        v.val = !IsBytesT(r.t);
        v.s = v.val ? PointeeLv("gs_v", r.t) : string("gs_v");
    }
    RenderLoc(out, v, r.t, true, nullptr, r.ln);
    cscopes.clear();
    assert(stkmax == 0);
    auto decls = HoistAggregateDecls(body);
    Append(code, r.sig, " {\n");
    code += decls;
    code += body;
    code += "}\n\n";
}








inline CodeGen::Loc CodeGen::RenderedLoc(Node *a) {
    auto c = Is<Call>(a);
    auto ib = Is<InlineBlock>(a);
    TypeExpr *rt = nullptr;
    if (c && !c->rettypes.empty()) rt = c->rettypes[0];
    else if (ib && ib->spec && ib->spec->rets.size() == 1) rt = ib->spec->rets[0];
    if (!rt || !IsPlainRef(rt) || IsVarintT(rt->ref->sub)) return GenLoc(a);
    Loc lv;
    lv.t = rt;
    lv.val = true;
    lv.s = T();
    if (c) {
        auto rets = EmitCall(c, Dst {});
        assert(!rets.empty());
        FixedLocal(rt, lv.s, rets[0]);
    } else {
        FixedLocal(rt, lv.s);
        GenAny(ib, Dst { DK_LVALUE, lv.s, rt });
    }
    return lv;
}


inline CodeGen::Loc CodeGen::RenderToTemp(Node *a, Call *c) {
    auto b = TempBuilder();
    auto lv = RenderedLoc(a);
    RenderLoc(b, lv, a->exprtype, false, c, a->line);
    return b;
}


inline string CodeGen::FmtCall(Node *a, const string &dst) {
    auto t = a->exprtype;
    if (t->kind == TY_INT) {
        if (t->intstorage == IS_U64) return cat("gs_fmt_u64(", dst, ", ", GenX(a), ")");
        return cat("gs_fmt_i64(", dst, ", (int64_t)(", GenX(a), "))");
    }
    if (IsF32(t)) return cat("gs_fmt_f32(", dst, ", ", GenX(a), ")");
    if (t->kind == TY_FLT) return cat("gs_fmt_f64(", dst, ", (double)(", GenX(a), "))");
    assert(t->kind == TY_BOOL);
    return cat("gs_fmt_bool(", dst, ", ", GenX(a), ")");
}





inline void CodeGen::EmitFormatInto(Loc lv, Node *a, Line ln, Call *c) {
    c = FmtContext(c, a);
    auto v = ArrayView(lv);
    auto limited = lv.t->arr->akind == A_LIMITED;
    auto t = a->exprtype;
    if (!SimpleText(c, t) && !limited) {
        
        Loc out = lv;
        out.lenlv = v.lenlv;
        if (out.hdr.empty()) Fail(ln, "internal: format destination without a header");
        RenderLoc(out, RenderedLoc(a), t, false, c, ln);
        return;
    }
    auto bytes = t->kind == TY_ARRAY || t->kind == TY_SLICE;
    auto n = T();
    string src;   
    if (!SimpleText(c, t)) {
        
        
        auto b = RenderToTemp(a, c);
        L("int64_t ", n, " = ", b.hdr, ".len;");
        src = cat(b.hdr, ".base");
        bytes = true;
    } else if (bytes) {
        auto se = GenSrcElems(a);
        L("int64_t ", n, " = ", se.n, ";");
        src = cat("(const uint8_t *)(", se.elems, ")");
    } else if (limited) {
        src = T();
        L("uint8_t ", src, "[GS_FMT_MAX];");
        L("int64_t ", n, " = ", FmtCall(a, src), ";");
    } else {
        L("int64_t ", n, " = ", FmtCall(a, Top(lv.stk)), ";");
    }
    
    
    if (limited || bytes) {
        AppendBytes(lv, src, n, ln);
        return;
    }
    
    assert(!lv.stk.empty());
    Bump(lv.stk, n);
    L(v.lenlv, " += ", n, ";");
}






inline vector<string> CodeGen::EmitStr(Call *c, vector<Node *> &an, Dst d0, Line ln) {
    EmitCoreTypes();
    string stk = d0.s, lenlv = d0.lenlv, pref, hdr;
    IntStorage ls = IS_U32;
    if (d0.k != DK_STACK) {
        
        hdr = RzTemp(GrowU8(), stk);
        lenlv = cat(hdr, ".len");
    } else if (lenlv.empty()) {
        if (!d0.t || d0.t->kind != TY_ARRAY || d0.t->arr->akind != A_VAR)
            Fail(ln, "str() needs a resizable or variable-array destination");
        ls = LenStore(d0.t->arr);
        pref = T();
        L("uint8_t *", pref, " = ", Top(stk), ";");
        Bump(stk, cat(PrefixBytes(ls)));
    }
    auto elems = T();
    L("uint8_t *", elems, " = ", Top(stk), ";");
    auto cnt = T();
    L("int64_t ", cnt, " = 0;");
    for (auto a : an) {
        auto context = FmtContext(c, a);
        auto t = a->exprtype;
        auto n = T();
        if (!SimpleText(context, t)) {
            auto b = RenderToTemp(a, context);
            L("int64_t ", n, " = ", b.hdr, ".len;");
            L("GS_MEMCPY(", Top(stk), ", ", b.hdr, ".base, (size_t)", n, ");");
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            auto se = GenSrcElems(a);
            L("int64_t ", n, " = ", se.n, ";");
            L(CopyFn(), "(", Top(stk), ", (const uint8_t *)(", se.elems, "), (size_t)",
              n, ");");
        } else {
            L("int64_t ", n, " = ", FmtCall(a, Top(stk)), ";");
        }
        Bump(stk, n);
        L(cnt, " += ", n, ";");
    }
    if (!pref.empty()) EmitPrefixPatch(pref, ls, stk, cnt, elems);
    else L(lenlv, " = ", cnt, ";");
    if (!hdr.empty()) return { hdr };
    return {};
}







inline const VarDef *CodeGen::NamedResult(Block *fnbody, SFunction *target,
                                         size_t nrets, size_t resultidx) {
    return NamedResultOf(fnbody, target, nrets, resultidx);
}





inline void CodeGen::DetectNrvo(FnSpec *sp) {
    nrvo.clear();
    
    
    if (sp->rets.empty()) return;
    for (size_t j = 0; j < sp->rets.size(); j++) {
        if (!IsBytesT(sp->rets[j])) continue;
        auto cand = NamedResult(sp->body, sp->sf, sp->rets.size(), j);
        
        auto ok = cand != nullptr && !nrvo.count(cand);
        
        
        
        
        auto rzarr = false;
        if (ok && cand) {
            auto rt = sp->rets[j];
            auto ct = cand->type;
            auto same = TEq(ct, rt);
            rzarr = IsResz(ct) && ct->kind == TY_ARRAY && rt->kind == TY_ARRAY &&
                    rt->arr->akind == A_VAR && TEq(ct->arr->sub, rt->arr->sub);
            if (!same && !rzarr) ok = false;
        }
        if (ok && cand) {
            NrvoDest nd;
            nd.stk = cat("gs_dst", j);
            if (rzarr) {
                nd.prefix = true;
                nd.ls = LenStore(sp->rets[j]->arr);
            } else {
                nd.lenlv = cat("(*gs_rl", j, ")");
                nd.fo = IsFrameObj(cand->type);
            }
            nrvo[cand] = nd;
        }
    }
}

}  
