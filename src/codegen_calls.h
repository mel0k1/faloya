



#pragma once

namespace faloya {











inline void CodeGen::EmitSlidePrefix(const string &base, IntStorage ls, const string &stk,
                                     const string &lenlv) {
    string metasz, count;
    if (ls == IS_VARINT) {
        metasz = cat("GS_ULEB_SIZE(", base, ")");
        count = cat("GS_ULEB_READ(", base, ")");
    } else {
        metasz = cat(IntSize(ls));
        count = cat("(int64_t)*(", IntCT(ls), " *)", base);
    }
    auto ms = T();
    L("int64_t ", ms, " = ", metasz, ";");
    L(lenlv, " = ", count, ";");
    L("memmove(", base, ", ", base, " + ", ms, ", (size_t)(", Top(stk), " - ", base,
      " - ", ms, "));");
    L(TopW(stk), " -= ", ms, ";");
}

inline vector<string> CodeGen::EmitCall(Call *c, Dst d0, vector<Dst> *alldst) {
    
    
    
    
    
    
    
    
    if (d0.k == DK_STACK && !d0.lenlv.empty() && d0.t && d0.t->kind == TY_ARRAY &&
        d0.t->arr->akind == A_VAR) {
        auto vt = c->builtin >= 0 ? d0.t : nullptr;
        if (!c->dispatch.empty() && !c->dispatch[0]->rets.empty()) {
            auto rt = c->dispatch[0]->rets[0];
            if (rt->kind == TY_ARRAY && rt->arr->akind == A_VAR) vt = rt;
        }
        if (vt) {
            auto base = T();
            L("uint8_t *", base, " = ", Top(d0.s), ";");
            auto rets = c->builtin >= 0 ? EmitBuiltin(c, Dst { DK_STACK, d0.s, vt })
                                        : EmitDispatch(c, Dst { DK_STACK, d0.s, vt }, alldst);
            EmitSlidePrefix(base, LenStore(vt->arr), d0.s, d0.lenlv);
            return rets;
        }
    }
    if (c->builtin >= 0) return EmitBuiltin(c, d0);
    if (c->fvbody) return EmitFvCall(c, d0);
    if (!c->dispatch.empty()) return EmitDispatch(c, d0, alldst);
    assert(c->spec);
    if (c->spec->sf->isextern) return EmitExternCall(c, c->spec);
    return EmitSpecCall(c, c->spec, d0, alldst);
}






inline void CodeGen::EmitCallInto(Call *c, vector<Dst> &dsts) {
    auto rets = EmitCall(c, dsts.empty() ? Dst {} : dsts[0], &dsts);
    for (size_t i = 0; i < dsts.size() && i < rets.size(); i++) {
        if (dsts[i].k != DK_LVALUE || rets[i].empty()) continue;
        if (NeedsDeref(c->rettypes[i], dsts[i].t)) {
            Loc lv;
            lv.t = c->rettypes[i];
            lv.val = true;
            lv.s = rets[i];
            L(dsts[i].s, " = ", LoadLoc(lv, dsts[i].t, c->line), ";");
        } else if (rets[i] != dsts[i].s) {
            L(dsts[i].s, " = ", rets[i], ";");
        }
    }
}






inline vector<string> CodeGen::EmitExternCall(Call *c, FnSpec *sp) {
    if (std::find(usedexterns.begin(), usedexterns.end(), sp) == usedexterns.end())
        usedexterns.push_back(sp);
    auto callee = sp->sf->dylib.empty() ? sp->sf->cname : DlCall(sp);
    auto an = CallArgNodes(c, sp->argtypes.size());
    string argstr;
    
    
    
    
    
    
    
    auto grows = sp->sf->readsalltops;
    for (size_t i = 0; i < an.size(); i++) {
        auto pt = sp->argtypes[i];
        if (i) argstr += ", ";
        auto x = pt->kind == TY_REF ? GenX(an[i]) : GenXD(an[i], pt);
        argstr += Snapshot(pt, x);
        if (HoldsFatRef(pt)) grows = true;
    }
    if (sp->sf->takesloc) Append(argstr, an.empty() ? "" : ", ", CallerLoc(c->line));
    if (grows) MarkFlush();
    vector<string> rets;
    if (sp->rets.empty()) {
        L(callee, "(", argstr, ");");
    } else {
        auto r0 = T();
        L(CT(sp->rets[0]), " ", r0, " = ", callee, "(", argstr, ");");
        rets.push_back(r0);
    }
    if (grows) MarkReload();
    return rets;
}






inline string CodeGen::ExternProto(FnSpec *sp) {
    auto dynamic = !sp->sf->dylib.empty();
    auto ret = sp->rets.empty() ? string("void") : CT(sp->rets[0]);
    string s = dynamic ? "static " : "";
    s += ret;
    if (dynamic) Append(s, " (*", DlSlot(sp), ")(");
    else Append(s, " ", sp->sf->cname, "(");
    string params, args;
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        if (i) {
            s += ", ";
            params += ", ";
            args += ", ";
        }
        s += CT(sp->argtypes[i]);
        Append(params, CT(sp->argtypes[i]), " a", i);
        Append(args, "a", i);
    }
    if (sp->argtypes.empty()) s += "void", params = "void";
    s += ");\n";
    if (dynamic)
        Append(s, "static GS_DL_CALL ", ret, " ", DlCall(sp), "(", params, ") { ",
               sp->rets.empty() ? "" : "return ", DlSlot(sp), "(", args, "); }\n");
    return s;
}



inline vector<Node *> CodeGen::CallArgNodes(Call *c, size_t nparams) {
    auto an = c->ArgNodes();
    an.resize(nparams);
    return an;
}


inline void CodeGen::EmitArg(FnSpec *sp, size_t i, Node *node, vector<string> &args,
                             bool later) {
    auto pt = sp->argtypes[i];
    if (IsPoolParam(sp, i)) {
        PushArg(args, pt, true, GenPrefVal(node));
        return;
    }
    if (IsResz(pt)) {
        
        
        
        string stk;
        auto h = RzTemp(pt, stk);
        GenConstruct(node, stk, pt, RzLenLv(pt, h));
        args.push_back(h);
        args.push_back(stk);
        return;
    }
    if (IsBytesT(pt)) {
        
        string stk;
        auto base = BytesTemp(stk);
        GenConstruct(node, stk, pt);
        args.push_back(base);
        return;
    }
    
    
    
    if (IsRelRef(pt)) {
        auto t = T();
        L(CT(pt), " ", t, ";");
        EmitRelStoreAt(cat("(uint8_t *)&", t), pt, GenX(node), node->line, true, RelValue(node));
        args.push_back(t);
        return;
    }
    
    
    
    
    
    auto x = GenXD(node, pt);
    auto named = splitroots.count(sp->params[i]) && !NamedRootDesc(x).empty();
    auto value = !named && (later || IsLargeFixed(pt) || (HasStmts(node) && !IsCName(x)))
                 ? Snapshot(pt, x) : x;
    if (IsLargeFixed(pt)) args.push_back(cat("&", value));
    else if (splitroots.count(sp->params[i])) PushRootMembers(args, pt, value);
    else PushArg(args, pt, false, value);
}


inline void CodeGen::EmitFvArg(const VarDef *fv, vector<string> &args) {
    auto name = VName(fv);
    
    auto ours = fv->ownerspec == curspec;
    if (fv->reusable) {
        
        if (ours) {
            auto &p = vpool[fv];
            auto t = T();
            L("gs_pref ", t, " = { &", name, ", ", vstk[fv], ", &", p.first, ", ",
              p.second, " };");
            PushArg(args, fv->type, true, t);
        } else {
            PushArg(args, fv->type, true, name);
        }
        return;
    }
    if (IsResz(fv->type)) {
        args.push_back(fvptr.count(fv) ? name : cat("&", name));
        args.push_back(ours ? vstk[fv] : cat(name, "_stk"));
        return;
    }
    if (IsBytesT(fv->type)) {
        args.push_back(name);
        return;
    }
    args.push_back(fvptr.count(fv) ? name : cat("&", name));
}




inline string CodeGen::BytesResultBase(const Dst &dd, string &stk) {
    if (dd.k == DK_STACK) stk = dd.s;
    else BytesTemp(stk);
    auto base = T();
    L("uint8_t *", base, " = ", Top(stk), ";");
    return base;
}

inline vector<string> CodeGen::EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst) {
    assert(sinfo.count(sp));
    auto &ki = sinfo[sp];
    auto an = CallArgNodes(c, sp->params.size());
    string ername, slidebase;
    Dst slide;
    
    
    
    
    string reprefixbase, reprefixcnt;
    Dst reprefix;
    vector<string> args;
    
    vector<bool> later(an.size() + 1, false);
    for (size_t i = an.size(); i-- > 0;) later[i] = later[i + 1] || HasStmts(an[i]);
    for (size_t i = 0; i < an.size(); i++) EmitArg(sp, i, an[i], args, later[i + 1]);
    for (auto fv : ki.freevars) EmitFvArg(c->FreeVarArg(fv), args);
    vector<string> retex(sp->rets.size());
    for (size_t i = 0; i < sp->rets.size(); i++) {
        auto rt = sp->rets[i];
        Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                              : (i == 0 ? d0 : Dst {});
        if (IsResz(rt)) {
            
            
            
            string stk, lenlv;
            if (dd.k == DK_STACK && !dd.lenlv.empty()) {
                stk = dd.s;
                lenlv = dd.lenlv;
                retex[i] = "";
            } else {
                auto h = RzTemp(rt, stk);
                lenlv = RzLenLv(rt, h);
                retex[i] = h;
            }
            args.push_back(stk);
            args.push_back(cat("&", lenlv));
        } else if (i == 0 && dd.k == DK_STACK && !dd.lenlv.empty() && rt->kind == TY_ARRAY &&
                   rt->arr->akind == A_VAR) {
            
            
            
            auto er = EnsureEr(sp);
            if (!er.empty()) {
                ername = er;
                args.push_back(dd.s);
                args.push_back(cat("&", dd.lenlv));
                retex[i] = "";
            } else {
                slide = dd;
                slidebase = T();
                L("uint8_t *", slidebase, " = ", Top(dd.s), ";");
                args.push_back(dd.s);
                retex[i] = "";
            }
        } else if (i == 0 && NeedsReprefix(dd, rt)) {
            
            
            
            
            reprefix = dd;
            reprefixbase = T();
            L("uint8_t *", reprefixbase, " = ", Top(dd.s), ";");
            auto er = EnsureEr(sp);
            if (!er.empty()) {
                ername = er;
                reprefixcnt = T();
                L("int64_t ", reprefixcnt, " = 0;");
                
                
                Bump(dd.s, cat(PrefixBytes(LenStore(dd.t->arr))));
                args.push_back(dd.s);
                args.push_back(cat("&", reprefixcnt));
            } else {
                args.push_back(dd.s);
            }
            retex[i] = reprefixbase;
        } else if (IsBytesT(rt)) {
            string stk;
            retex[i] = BytesResultBase(dd, stk);
            args.push_back(stk);
        } else if ((int)i != ki.cret) {
            
            
            if (dd.k == DK_LVALUE && !NeedsDeref(rt, dd.t) &&
                (!NeedsResultLocation(rt) || (dd.t && TEq(rt, dd.t)))) {
                retex[i] = dd.s;
                args.push_back(cat("&", dd.s));
            } else {
                auto tv = T();
                FixedLocal(rt, tv);
                retex[i] = tv;
                args.push_back(cat("&", tv));
            }
        }
    }
    if (ki.needssp) {
        NoStackAcrossCycleCall(sp, c->line);
        NoteStackCall(sp, c->line);
        args.push_back(SpTop());
    }
    if (ki.located) args.push_back(CallerLoc(c->line));
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    auto callee = ername.empty() ? ki.cname : ername;
    
    auto reach = SyncReach(sp, args, an);
    MarkFlush(reach);
    if (ki.cret >= 0) {
        auto r0 = T();
        L(CT(sp->rets[ki.cret]), " ", r0, " = ", callee, "(", argstr, ");");
        retex[ki.cret] = r0;
    } else {
        L(callee, "(", argstr, ");");
    }
    MarkReload(reach);
    
    
    
    if (ki.hasrf) EmitRfCheck(sp);
    if (!reprefixbase.empty()) EmitReprefix(sp, reprefix, reprefixbase, reprefixcnt);
    if (!slidebase.empty()) {
        
        
        EmitSlidePrefix(slidebase, LenStore(sp->rets[0]->arr), slide.s, slide.lenlv);
    }
    return retex;
}





inline bool CodeGen::NeedsReprefix(const Dst &d, TypeExpr *rt) {
    return d.k == DK_STACK && d.lenlv.empty() && d.t && d.t->kind == TY_ARRAY &&
           d.t->arr->akind == A_VAR && rt->kind == TY_ARRAY && rt->arr->akind == A_VAR &&
           LenStore(d.t->arr) != LenStore(rt->arr);
}







inline void CodeGen::EmitReprefix(FnSpec *sp, const Dst &dd, const string &base,
                                  const string &cnt) {
    auto ls = LenStore(dd.t->arr);
    if (!cnt.empty()) {
        EmitPrefixPatch(base, ls, dd.s, cnt, cat(base, " + ", PrefixBytes(ls)));
        return;
    }
    auto srcls = LenStore(sp->rets[0]->arr);
    auto count = T();
    auto oms = T();
    if (srcls == IS_VARINT) {
        L("int64_t ", count, " = GS_ULEB_READ(", base, ");");
        L("int64_t ", oms, " = GS_ULEB_SIZE(", base, ");");
    } else {
        L("int64_t ", count, " = (int64_t)*(", IntCT(srcls), " *)", base, ";");
        L("int64_t ", oms, " = ", IntSize(srcls), ";");
    }
    auto ms = T();
    string writepref;
    EmitLenCheck(ls, count);
    if (ls == IS_VARINT) {
        auto tmp = T();
        L("uint8_t ", tmp, "[10];");
        L("int64_t ", ms, " = gs_uleb_write(", tmp, ", (uint64_t)", count, ");");
        writepref = cat("GS_MEMCPY(", base, ", ", tmp, ", (size_t)", ms, ");");
    } else {
        L("int64_t ", ms, " = ", IntSize(ls), ";");
        writepref = cat("*(", IntCT(ls), " *)", base, " = (", IntCT(ls), ")", count, ";");
    }
    L("memmove(", base, " + ", ms, ", ", base, " + ", oms, ", (size_t)(", Top(dd.s), " - ",
      base, " - ", oms, "));");
    L(writepref);
    L(TopW(dd.s), " += ", ms, " - ", oms, ";");
}

inline void CodeGen::EmitRfCheck(FnSpec *callee) {
    if (norfcheck) return;
    L("if (gs_rf) {");
    ind++;
    auto caught = curspec && callee->needs.count(curspec);
    if (caught) {
        auto tid = fromids[curspec];
        EnsureFromChannels(curspec);
        L("if (gs_rf == ", tid, ") {");
        ind++;
        
        
        L("gs_rf = 0;");
        string retv;
        for (size_t i = 0; i < curspec->rets.size(); i++) {
            auto rt = curspec->rets[i];
            if (NeedsResultLocation(rt)) continue;   
            auto lret = cat("gs_lret_", tid, "_", i);
            
            
            if (IsBytesT(rt))
                LandValue(cat("gs_dst", i), DstTop0(i), cat("gs_fval_", tid, "_", i), rt, lret);
            if (IsResz(rt)) {
                L("*gs_rl", i, " = ", lret, ";");   
                continue;
            }
            if (IsBytesT(rt)) continue;
            if ((int)i == curinfo->cret) retv = lret;
            else L("*gs_r", i, " = ", lret, ";");
        }
        Epilogue(retv);
        ind--;
        L("} else {");
        ind++;
        if (curinfo && curinfo->hasrf) PropagateReturn("");
        else L("gs_panic(\"unexpected long-distance return\");");
        ind--;
        L("}");
    } else if (curinfo && curinfo->hasrf) {
        PropagateReturn("");
    } else {
        L("gs_panic(\"unexpected long-distance return\");");
    }
    ind--;
    L("}");
    termjump = false;
}



inline vector<string> CodeGen::EmitFvCall(Call *c, Dst d0) {
    auto et = c->fvbody->exprtype;
    auto wantsval = !IsVoidT(et);
    
    
    
    
    
    
    
    
    
    auto want = d0.t ? d0.t : c->exprtype;
    auto adapt = wantsval && (want->kind == TY_ARRAY || want->kind == TY_SLICE) &&
                 !TEq(want, et) && (IsFix(want) || d0.k == DK_STACK);
    string rv, stk;
    Dst d = d0;
    if (wantsval && !IsBytesT(et) && (adapt || d0.k != DK_LVALUE)) {
        rv = T();
        FixedLocal(et, rv);
        d = Dst { DK_LVALUE, rv, et };
    } else if (wantsval && IsResz(et) && (adapt || d0.k != DK_STACK)) {
        rv = RzTemp(et, stk);
        d = Dst { DK_STACK, stk, et, RzLenLv(et, rv) };
    } else if (wantsval && IsBytesT(et) && (adapt || d0.k != DK_STACK)) {
        rv = BytesTemp(stk);
        d = Dst { DK_STACK, stk, et };
    } else if (wantsval && d0.k == DK_LVALUE) {
        rv = d0.s;
    }
    PushSc(SC_PLAIN);
    L("{");
    ind++;
    for (size_t i = 0; i < c->fvparams.size(); i++)
        BindLocal(c->fvparams[i], i < c->args.size() ? c->args[i] : nullptr);
    for (auto st : c->fvbody->stmts) GenStmt(st);
    if (c->fvbody->tail) {
        if (wantsval) GenAny(c->fvbody->tail, d);
        else GenAny(c->fvbody->tail, Dst {});
    }
    PopSc();
    ind--;
    L("}");
    termjump = false;
    if (!wantsval) return {};
    if (adapt) {
        Loc lv;
        if (IsResz(et)) {
            lv = RzTempLoc(et, rv, stk);
        } else if (IsBytesT(et)) {
            lv = BytesLoc(rv, et, Loc {});
        } else {
            lv.t = et;
            lv.val = true;
            lv.s = rv;
        }
        if (d0.k == DK_STACK) {
            if (IsFix(want)) EmitValStore(d0.s, want, LoadLoc(lv, want, c->line));
            else ConstructFromLoc(lv, want, d0.s, d0.lenlv, c->line);
            return {};
        }
        rv = LoadLoc(lv, want, c->line);
        if (d0.k == DK_LVALUE) {
            L(d0.s, " = ", rv, ";");
            rv = d0.s;
        }
    }
    
    
    
    
    
    if (rv.empty()) return {};
    return { rv };
}




inline vector<string> CodeGen::EmitDispatch(Call *c, Dst d0, vector<Dst> *alldst) {
    auto sp0 = c->dispatch[0];
    auto an = CallArgNodes(c, sp0->params.size());
    auto pos = c->dispatcharg;
    
    auto sn = an[pos];
    auto st = sn->exprtype;
    TypeExpr *enumtype = nullptr;
    auto isref = false;
    if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
        enumtype = st->ref->sub;
        isref = true;
    } else {
        assert(st->kind == TY_ENUM);
        enumtype = st;
    }
    auto varmode = enumtype->enu->varmode;
    auto ei = EIOf(enumtype);
    auto ts = TagSize(ei->en);
    string p, copied, sv, tag;
    vector<string> snap(ei->en->variants.size());
    auto scrutinee = [&]() {
        if (varmode) {
            if (isref) {
                auto x = GenX(sn);
                p = T();
                L(CT(st), " ", p, " = ", x, ";");
            } else {
                p = GenPtr(sn);
            }
            tag = cat("*(", IntCT(TagStore(ei->en)), " *)", p);
            
            
            
            
            
            
            auto wholecopy = false;
            for (size_t vi = 0; vi < snap.size(); vi++)
                wholecopy |= c->dispatch[vi]->argtypes[pos]->kind != TY_REF &&
                             IsBytesT(VariantType(enumtype, (int)vi));
            if (wholecopy) {
                string stk;
                copied = BytesTemp(stk);
                auto sz = T();
                L("int64_t ", sz, " = ", SizeX(enumtype, p), ";");
                L("GS_MEMCPY(", Top(stk), ", ", p, ", (size_t)", sz, ");");
                Bump(stk, sz);
            } else {
                for (size_t vi = 0; vi < snap.size(); vi++) {
                    if (c->dispatch[vi]->argtypes[pos]->kind == TY_REF ||
                        EmptyLayout(ei->en->variants[vi].fields))
                        continue;
                    auto vt = VariantType(enumtype, (int)vi);
                    snap[vi] = T();
                    FixedLocal(vt, snap[vi], "{0}");
                    auto tagk = TagConst(ei, (int)vi), vct = CT(vt);
                    L("if (", tag, " == ", tagk, ") ", snap[vi], " = *(", vct, " *)(", p, " + ", ts,
                      ");");
                }
            }
        } else {
            auto value = GenXD(sn, enumtype);
            sv = T();
            FixedLocal(enumtype, sv, value);
            tag = cat(sv, ".tag");
        }
    };
    
    
    
    
    vector<vector<string>> shared(an.size());
    for (size_t i = 0; i < an.size(); i++) {
        if ((int)i == pos) scrutinee();
        else EmitArg(sp0, i, an[i], shared[i], true);
    }
    
    vector<string> retex(sp0->rets.size());
    vector<string> dststk(sp0->rets.size());
    vector<string> dstlen(sp0->rets.size());
    
    
    
    
    Dst reprefix;
    for (size_t i = 0; i < sp0->rets.size(); i++) {
        auto rt = sp0->rets[i];
        Dst dd = alldst && i < alldst->size() ? (*alldst)[i]
                                              : (i == 0 ? d0 : Dst {});
        if (IsResz(rt)) {
            if (dd.k == DK_STACK && !dd.lenlv.empty()) {
                dststk[i] = dd.s;
                dstlen[i] = dd.lenlv;
            } else {
                auto h = RzTemp(rt, dststk[i]);
                dstlen[i] = RzLenLv(rt, h);
                retex[i] = h;
            }
        } else if (IsBytesT(rt)) {
            string stk;
            retex[i] = BytesResultBase(dd, stk);
            dststk[i] = stk;
            if (i == 0 && NeedsReprefix(dd, rt)) reprefix = dd;
        } else if (NeedsResultLocation(rt) && dd.k == DK_LVALUE && TEq(rt, dd.t)) {
            retex[i] = dd.s;
        } else {
            auto tv = T();
            FixedLocal(rt, tv);
            retex[i] = tv;
        }
    }
    L("switch (", tag, ") {");
    for (size_t vi = 0; vi < ei->en->variants.size(); vi++) {
        auto sp = c->dispatch[vi];
        auto &ki = sinfo[sp];
        L("case ", TagConst(ei, (int)vi), ": {");
        ind++;
        PushSc(SC_PLAIN);
        auto vt = VariantType(enumtype, (int)vi);
        auto byref = sp->argtypes[pos]->kind == TY_REF;
        string varg;
        string payload = varmode ? cat("(", byref ? p : copied, " + ", ts, ")") : "";
        if (byref) {
            if (IsBytesT(vt)) varg = payload;
            else {
                auto tv = T();
                L(CT(vt), " *", tv, " = (", CT(vt), " *)(", payload, ");");
                varg = tv;
            }
        } else if (IsBytesT(vt)) {
            
            
            varg = payload;
        } else if (EmptyLayout(ei->en->variants[vi].fields)) {
            auto tv = T();
            FixedLocal(vt, tv, "{0}");
            varg = tv;
        } else if (!snap[vi].empty()) {
            varg = snap[vi];
        } else if (!payload.empty()) {
            auto tv = T();
            FixedLocal(vt, tv, cat("*(", CT(vt), " *)(", payload, ")"));
            varg = tv;
        } else {
            auto tv = T();
            FixedLocal(vt, tv, cat(sv, ".u.v_", Sanitize(ei->en->variants[vi].name)));
            varg = tv;
        }
        
        vector<string> args;
        for (size_t i = 0; i < an.size(); i++) {
            if ((int)i != pos) {
                args.insert(args.end(), shared[i].begin(), shared[i].end());
                continue;
            }
            auto pt = sp->argtypes[i];
            if (IsResz(pt)) Fail(c->line, "resizable by-value dispatch payloads are unsupported");
            if (IsLargeFixed(pt)) args.push_back(cat("&", varg));
            else PushArg(args, pt, IsPoolParam(sp, i), varg);
        }
        for (auto fv : ki.freevars) EmitFvArg(c->FreeVarArg(fv), args);
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (IsResz(sp->rets[i])) {
                args.push_back(dststk[i]);
                args.push_back(cat("&", dstlen[i]));
            } else if (IsBytesT(sp->rets[i])) args.push_back(dststk[i]);
            else if ((int)i != ki.cret) args.push_back(cat("&", retex[i]));
        }
        if (ki.needssp) {
            NoStackAcrossCycleCall(sp, c->line);
            NoteStackCall(sp, c->line);
            args.push_back(SpTop());
        }
        if (ki.located) args.push_back(CallerLoc(c->line));
        string argstr;
        for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
        auto reach = SyncReach(sp, args, an);
        MarkFlush(reach);
        if (ki.cret >= 0) L(retex[ki.cret], " = ", ki.cname, "(", argstr, ");");
        else L(ki.cname, "(", argstr, ");");
        MarkReload(reach);
        if (ki.hasrf) EmitRfCheck(sp);
        PopSc();
        ind--;
        L("} break;");
    }
    L("default: GS_UNREACHABLE(", LocArgs(c->line), ");");
    L("}");
    if (reprefix.t) EmitReprefix(sp0, reprefix, retex[0], "");
    return retex;
}

}  
