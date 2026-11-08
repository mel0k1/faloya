





#pragma once

namespace faloya {





inline Val TypeCheck::CheckCall(Call *c, TypeExpr *expected) {
    
    c->spec = nullptr;
    c->dispatch.clear();
    c->builtin = -1;
    c->rettypes.clear();
    c->fmtspecs.clear();
    c->fmtcontexts.clear();
    contextualfloats.erase(c);
    phase2waits.erase(c);
    lastcallrets.clear();
    c->args.erase(c->args.begin() + c->firstdefault,
                  c->args.begin() + c->firstdefault + c->ndefaults);
    c->firstdefault = c->ndefaults = 0;
    
    
    
    if (!c->implicit)
        for (auto t : c->tyargs) ConstNamesIn(t);
    if (auto fv = c->trailing) {
        for (auto &p : fv->params)
            if (p.type) ConstNamesIn(p.type);
        SignatureNames(fv->params, {}, {});
    }
    
    
    
    Saved<vector<Dest>> results(callresultdsts, c == multidestnode ? multidests
                                                                 : vector<Dest> { curdst });
    Saved<TypeExpr *> resulttype(callresulttype, expected);
    Saved<bool> forward(callforwarding, c == multidestnode);
    DestScope ds(*this, Dest {});
    SlotScope ss(*this, false);
    if (auto d = Is<Dot>(c->callee)) return CheckUfcsCall(c, d, expected);
    if (auto id = Is<Ident>(c->callee)) return CheckNamedCall(c, id, expected);
    Error(c, "this expression cannot be called");
}

inline Val TypeCheck::CheckNamedCall(Call *c, Ident *id, TypeExpr *expected) {
    if (!id->expansionerror.empty()) Error(id, string(id->expansionerror));
    
    
    
    
    if (auto vd = LookupVar(id->name, id->ns); vd && !vd->isglobal)
        Error(c, cat(id->name, " is a variable, not a function"));
    const FnValBind *fb;
    if (auto t = LookupTypeParam(id->name, fb))
        Error(c, cat("type parameter ", id->name, " is bound to the type ", TypeStr(t),
                     ", not a function"));
    if (fb) {
        id->vdef = nullptr;
        return CheckFunValCall(c, *fb, expected);
    }
    FnSpec *env = nullptr;
    auto cands = LookupLocalFns(id->name, env);
    if (cands.empty()) {
        DefaultScopeName(id->name, c, false);
        cands = ast.LookupFunctions(id->name, id->ns);
    }
    
    
    auto bd = LookupBuiltin(GlobalLeaf(id->name));
    if (!cands.empty()) {
        Node *nopre = nullptr;
        
        
        auto entryreach = reachable;
        auto nomatch = false;
        auto v = ResolveCall(c, cands, env, id->name, nullptr, nopre,
                             entryreach, bd ? &nomatch : nullptr, expected);
        if (!nomatch) return v;
        
        auto fallback = builtinfallback;
        fallback.insert(c->args.begin(), c->args.end());
        Saved<unordered_set<Node *>> fs(builtinfallback, std::move(fallback));
        reachable = entryreach;
        return CheckBuiltin(c, *bd, c->args, nullptr);
    }
    if (!bd) {
        if (LookupVar(id->name, id->ns))
            Error(c, cat(id->name, " is a variable, not a function"));
        Error(c, cat("unknown function: ", id->name));
    }
    if (bd->flags & BF_PROPERTY)
        Error(c, cat(id->name, " is a property (use a.", id->name, "), not a call"));
    return CheckBuiltin(c, *bd, c->args, nullptr);
}










inline vector<SFunction *> TypeCheck::LookupLocalFns(string_view name, FnSpec *&env) {
    vector<OuterFn> found;
    auto add = [&](const OuterFn &o) {
        for (auto &f : found) {
            if (f.sf->params.size() != o.sf->params.size()) continue;
            auto same = true;
            for (size_t i = 0; i < f.sf->params.size() && same; i++) {
                auto a = f.sf->params[i].type, b = o.sf->params[i].type;
                same = a && b ? TypeEq(a, b) : !a && !b;
            }
            if (same) return;
        }
        found.push_back(o);
    };
    auto top = (int)frames.size() - 1;
    auto local = false;
    for (auto i = (int)localfns.size() - 1; i >= 0 && localfns[i].first >= frames[top].scopebase;
         i--) {
        if (localfns[i].second->name != name) continue;
        env = frames[top].lexspec;
        local = true;
        auto si = localfns[i].first;
        for (auto j = i; j >= 0 && localfns[j].first >= si; j--)
            if (localfns[j].first == si && localfns[j].second->name == name)
                add(OuterFn { localfns[j].second, env, 0, j });
        break;
    }
    if (!local) {
        auto serial = 0;
        ForOuterFns(top, [&](const OuterFn &o) {
            if (o.sf->name != name) return false;
            if (found.empty()) {
                serial = o.scopeserial;
                env = o.env;
            } else if (o.scopeserial != serial) {
                return false;
            }
            add(o);
            return false;
        });
    }
    std::sort(found.begin(), found.end(),
              [](const OuterFn &a, const OuterFn &b) { return a.order < b.order; });
    vector<SFunction *> fns;
    for (auto &f : found) fns.push_back(f.sf);
    return fns;
}



inline FnValBind TypeCheck::LocalFnValue(const vector<SFunction *> &fns, FnSpec *env) {
    FnValBind fb;
    fb.named = fns[0];
    fb.env = env;
    if (fns.size() > 1) fb.set = &*localfnsets.insert(fns).first;
    return fb;
}

inline Val TypeCheck::CheckUfcsCall(Call *c, Dot *d, TypeExpr *expected) {
    if (d->accessorowner) Error(d, "const for field accessor is not callable");
    auto entryreach = reachable;
    Val ov;
    {
        PathScope ps(*this, d->obj);
        Discovering discovering(*this, c);
        ov = CheckV(d->obj, nullptr);
    }
    d->obj->exprtype = ov.type;
    auto rt = ov.type;
    auto optional = IsOptional(rt);
    if (rt->kind == TY_REF) rt = rt->ref->sub;
    
    
    
    
    auto bd = LookupBuiltin(d->name);
    if (bd && (bd->flags & BF_MEMBER) && !(bd->flags & BF_PROPERTY) &&
        rt->kind == TY_ARRAY) {
        CheckWaitingArg(c, d->obj, entryreach);
        if (optional) Error(c, "optional value must be narrowed (if/guard/assert) before use");
        
        
        if (ov.implicitcopy) ImplicitCopyError(ov.implicitcopy);
        RefCopyWarnings(ov);
        auto argnodes = c->ArgNodes();
        d->member = bd->kind;
        auto v = CheckBuiltin(c, *bd, argnodes, &ov);
        c->SetArgNodes(argnodes);
        return v;
    }
    if (rt->kind == TY_STRUCT) {
        for (auto &f : rt->struc->st->fields)
            if (!f.ispad && f.name == d->name)
                Error(c, cat("field ", d->name, " is not callable"));
    }
    
    
    
    
    const FnValBind *fb;
    if (auto t = LookupTypeParam(d->name, fb))
        Error(c, cat("type parameter ", d->name, " is bound to the type ", TypeStr(t),
                     ", not a function"));
    if (fb)
        Error(c, cat("type parameter ", d->name, " is bound to a function value, which is "
                     "called as ", d->name, "(...), not as a member"));
    FnSpec *env = nullptr;
    auto cands = LookupLocalFns(d->name, env);
    if (cands.empty()) {
        DefaultScopeName(d->name, c, true);
        cands = ast.LookupMethods(d->name, d->ns, rt);
    }
    if (!cands.empty())
        return ResolveCall(c, cands, env, d->name, &ov, d->obj, entryreach, nullptr, expected);
    CheckWaitingArg(c, d->obj, entryreach);
    
    NoArrayJoin(ov);
    if (bd && !(bd->flags & BF_PROPERTY)) {
        RefCopyWarnings(ov);
        auto argnodes = c->ArgNodes();
        auto v = CheckBuiltin(c, *bd, argnodes, &ov);
        c->SetArgNodes(argnodes);
        return v;
    }
    if (bd) Error(c, cat(".", d->name, " is a property, not a call"));
    if (LacksByError(rt)) throw Abandoned {};
    Error(c, cat("unknown function or member: ", d->name));
}







inline void TypeCheck::RefArg(Node *&a, Val &v) {
    
    
    if (IsNonFixedLValue(v) && !v.storagebranches && Referenceable(a, v)) a = AutoRef(a, v);
    else if (UserRefOf(a) && IsNonFixedRef(v) && !InGenericCode())
        Warn(a, cat("redundant &: ", ExprStr(Is<Unary>(a)->child),
                    " is passed by reference without it (§4.1)"));
}





inline FnSpec *TypeCheck::ParamDefaultEnv(const MatchInfo &mi) {
    auto env = ast.NewFunValEnv();
    env->bindings = mi.bindings;
    vector<string_view> hidden;
    for (auto &g : mi.sf->generics) hidden.push_back(g.name);
    for (auto sp = mi.env; sp; sp = sp->lexparent) {
        for (auto &b : sp->bindings)
            if (find(hidden.begin(), hidden.end(), b.first) == hidden.end())
                env->bindings.push_back(b);
        for (auto &b : sp->bindings) hidden.push_back(b.first);
        for (auto &[n, fb] : sp->fnvals) hidden.push_back(n);
    }
    return env;
}



template<typename F>
void TypeCheck::InParamDefault(Call *c, const MatchInfo &mi, FnSpec *env, size_t param, F f) {
    EachDefaultInPlace([&](Frame &fr) {
        if (fr.defaultfn == mi.sf && fr.defaultparam == (int)param)
            Error(c, cat("the default of parameter ", mi.sf->params[param].name, " of ",
                         mi.sf->qname, " leads to this call, which takes it again (§7.1)"));
    });
    DefaultScope ds(*this, env, c->line);
    auto &fr = frames.back();
    fr.defaultfn = mi.sf;
    fr.defaultparam = (int)param;
    if (auto it = declsiteof.find({ mi.env, mi.sf }); mi.sf->isnested && it != declsiteof.end())
        fr.defaultsite = it->second;
    f();
}







inline void TypeCheck::AddParamDefaults(Call *c, MatchInfo &best, vector<Node *> &argnodes,
                                        vector<Val> &argvals, bool receiver, FnSpec *env) {
    auto shift = receiver ? 1 : 0;
    auto P = best.paramtypes.size();
    c->firstdefault = (int)best.nwritten - shift;
    c->ndefaults = (int)(P - best.nwritten);
    Discovering discovering(*this, c);
    for (auto i = best.nwritten; i < P; i++) {
        auto &p = best.sf->params[i];
        auto pt = best.paramtypes[i];
        auto d = p.defaultval->Clone(ast);
        c->args.insert(c->args.begin() + (int)(i - shift), d);
        argnodes.insert(argnodes.begin() + (int)i, d);
        Val v;
        InParamDefault(c, best, env, i, [&]() {
            {
                PathScope ps(*this, d);
                v = CheckV(argnodes[i], nullptr);
            }
            RequireComplete(v.type, d->line);
            RefArg(argnodes[i], v);
            argnodes[i]->exprtype = v.type;
            vector<pair<string_view, TypeExpr *>> nobindings;
            auto tier = 0;
            fitfail.clear();
            if (p.type->kind != TY_SLICE) NoArrayJoin(v);
            if (!UnifyArg(pt, v, nobindings, tier))
                Error(d, cat("the default of parameter ", p.name, " cannot be passed as ",
                             TypeStr(pt), ": ",
                             fitfail.empty() ? cat("it is ", TypeStr(v.type)) : fitfail));
            BindBranchByRef(argnodes[i], v, pt, p.type);
        });
        c->args[i - shift] = argnodes[i];
        argvals.insert(argvals.begin() + (int)i, v);
    }
}



inline Val TypeCheck::ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env,
                                  string_view name, Val *preval, Node *&prenode, bool entryreach,
                                  bool *nomatch, TypeExpr *expected) {
    vector<Node *> argnodes;
    vector<Val> argvals;
    argnodes.reserve(c->args.size() + 1);
    argvals.reserve(c->args.size() + 1);
    if (prenode) {
        auto v = *preval;
        RefArg(prenode, v);
        argnodes.push_back(prenode);
        argvals.push_back(std::move(v));
    }
    {
        Discovering discovering(*this, c);
        for (auto &a : c->args) {
            Val v;
            {
                PathScope ps(*this, a);
                v = CheckV(a, nullptr);
            }
            
            
            
            if (nomatch && IsPendingArray(v.type)) {
                *nomatch = true;
                return Val {};
            }
            RequireComplete(v.type, a->line);
            RefArg(a, v);
            argnodes.push_back(a);
            a->exprtype = v.type;
            argvals.push_back(std::move(v));
        }
    }
    
    
    for (auto sf : cands)
        if (sf->brokensig) throw Abandoned {};
    MatchInfo best;
    vector<MatchInfo> tied, converting;
    string failures;
    MatchCandidates(c, cands, env, argvals, tied, converting, &failures, name);
    if (!tied.empty()) best = tied[0];
    auto ambiguous = [&](const vector<MatchInfo> &set) {
        
        
        string which;
        for (auto &mi : set) {
            Append(which, "\n  candidate ", name, " at ", Where(mi.sf->line));
            for (auto i = mi.nwritten; i < mi.sf->params.size(); i++)
                Append(which, i == mi.nwritten ? ", taking the default of " : ", ",
                       mi.sf->params[i].name);
        }
        Error(c, cat("ambiguous call to ", name, ": multiple overloads match equally well",
                     which));
    };
    if (tied.size() > 1) ambiguous(tied);
    if (tied.empty()) {
        
        auto v = TryDispatch(c, cands, env, argnodes, argvals, name, entryreach);
        if (v.type) return v;
        if (nomatch) {   
            *nomatch = true;
            return Val {};
        }
        
        
        
        if (converting.empty()) {
            
            
            for (auto &av : argvals)
                if (HasOpaque(av.type)) {
                    for (auto a : argnodes) CheckWaitingArg(c, a, entryreach);
                    Val v;
                    v.type = OpaqueMember();
                    return v;
                }
            for (auto &av : argvals) NoArrayJoin(av);
            
            
            if (!ast.unknowntypes.empty())
                for (auto sf : cands)
                    for (auto &p : sf->params)
                        if (p.type && MentionsUnknown(p.type)) throw Abandoned {};
            Error(c, cat("no matching overload for call to ", name, failures));
        }
        if (converting.size() > 1) ambiguous(converting);
        best = converting[0];
    }
    
    
    
    
    JudgeCallCasts(cands, best, argnodes, argvals, [&](vector<Val> &trial) {
        vector<MatchInfo> ties, conv;
        MatchCandidates(c, cands, env, trial, ties, conv, nullptr, name);
        if (ties.size() == 1) return SameMatch(ties[0], best);
        auto adt = any_of(trial.begin(), trial.end(), [](const Val &av) {
            auto t = IsPlainRef(av.type) ? av.type->ref->sub : av.type;
            return t && t->kind == TY_ENUM;
        });
        return ties.empty() && conv.size() == 1 && !nomatch && !adt && SameMatch(conv[0], best);
    });
    ContextualFloatCall(c, cands, env, argnodes, argvals, best, expected, name);
    FnSpec *denv = nullptr;
    if (best.nwritten < best.paramtypes.size()) {
        denv = ParamDefaultEnv(best);
        AddParamDefaults(c, best, argnodes, argvals, prenode != nullptr, denv);
    }
    LoadSliceArgs(argvals, best.paramtypes);
    RefSliceArgs(argvals, best.paramtypes, c->line);
    {
        FlagScope rs(reachable, entryreach);
        BindBranchesByRef(argnodes, argvals, best.paramtypes, best.sf, -1, best.nwritten);
    }
    auto spec = GetOrCreateSpec(best, argvals, c);
    ApplyCalleeShrinks(c, spec, argvals, name, argnodes);
    ApplyCalleeGrows(c, spec, argvals, name);
    
    
    
    auto argsreach = reachable;
    RecheckArgs(c, cands, best, denv, argnodes, -1, {}, entryreach);
    
    
    if (best.sf->isextern) {
        for (size_t i = 0; i < best.paramtypes.size() && i < argvals.size(); i++) {
            auto pt = best.paramtypes[i];
            if (!IsRefOrSlice(pt) || pt->cq || argvals[i].writable)
                continue;
            Error(argnodes[i], cat("extern fn ", name, ": parameter ", best.sf->params[i].name,
                                   " of type ", TypeStr(pt), " takes a writable value; a "
                                   "read-only one needs the parameter declared const (§9.5)"));
        }
    }
    
    c->SetArgNodes(argnodes);
    c->spec = spec;
    CheckCalleeRelative(c, spec, argvals);
    CalleesReturn(c, { spec }, argsreach);
    return CallResult(c, spec, argvals);
}








inline void TypeCheck::RecheckArgs(Call *c, vector<SFunction *> &cands, MatchInfo &mi,
                                   FnSpec *denv, vector<Node *> &argnodes, int skip,
                                   const function<void()> &atskip, bool entryreach) {
    auto waits = Phase2Waits(c);
    if (waits) phase2waits.insert(c);
    else reachable = entryreach;
    DestScope ds(*this, Dest {});
    for (size_t i = 0; i < mi.paramtypes.size(); i++) {
        if ((int)i == skip) {
            atskip();
            continue;
        }
        UnrefForValueParam(argnodes[i], mi.paramtypes[i]);
        
        
        
        
        auto &p = mi.sf->params[i];
        if (i < mi.nwritten && UserRefOf(argnodes[i]) && cands.size() == 1 && p.type &&
            !HasGenerics(p.type) && p.type->kind == TY_REF &&
            ClassOf(LoadType(p.type->ref->sub)) == SC_FIXED)
            Warn(argnodes[i], cat("redundant &: ", ExprStr(Is<Unary>(argnodes[i])->child),
                                  " is passed by reference without it (§4.1)"));
        if (!waits) {
            if (i < mi.nwritten) CheckArg(argnodes[i], mi.paramtypes[i]);
            else InParamDefault(c, mi, denv, i,
                                [&]() { CheckArg(argnodes[i], mi.paramtypes[i]); });
        }
        
        
        
        if (ClassOf(mi.paramtypes[i]) != SC_FIXED) argnodes[i]->nftemp = true;
        
        
        c->SetArgNode(i, argnodes[i]);
    }
}






inline void TypeCheck::CheckWaitingArg(Call *c, Node *a, bool entryreach) {
    if (!phase2waits.count(a)) return;
    Discovering discovering(*this, c);
    Saved<Node *> full(nowait, a);
    FlagScope rs(reachable, entryreach);
    PathScope ps(*this, a);
    CheckV(a, nullptr);
}






inline void TypeCheck::CalleesReturn(Call *c, const vector<FnSpec *> &specs, bool argsreach) {
    auto mayreturn = false;
    for (auto sp : specs) {
        CheckfreeCallee(c, sp->sf);
        ApplyCalleeRebinds(sp);
        auto rec = RecordOf(sp);
        mayreturn = mayreturn || !rec || rec->mayreturn;
    }
    reachable = reachable && argsreach && mayreturn;
}











inline void TypeCheck::BindBranchesByRef(vector<Node *> &argnodes, vector<Val> &argvals,
                                         const vector<TypeExpr *> &paramtypes, SFunction *sf,
                                         int skip, size_t end) {
    for (size_t i = 0; i < paramtypes.size() && i < argvals.size() && i < end; i++)
        if ((int)i != skip)
            BindBranchByRef(argnodes[i], argvals[i], paramtypes[i], sf->params[i].type);
}





inline void TypeCheck::BindBranchByRef(Node *&n, Val &v, TypeExpr *pt, TypeExpr *declared) {
    if (!declared || declared->kind != TY_SLICE) NoArrayJoin(v);
    auto byref = v.storagebranches && pt->kind == TY_REF && pt->ref->lenstorage < 0;
    if (!byref) RefCopyWarnings(v);
    if (!byref && !ViewedWhole(n, v, pt)) return;
    DestScope ds(*this, Dest {});
    SlotScope ss(*this, false);
    FlagScope q(quiet, true);
    v = CheckValue(n, pt, true);
}





inline void TypeCheck::MatchCandidates(Call *c, vector<SFunction *> &cands, FnSpec *env,
                                       vector<Val> &argvals, vector<MatchInfo> &tied,
                                       vector<MatchInfo> &converting, string *failures,
                                       string_view name) {
    for (auto sf : cands) {
        MatchInfo mi;
        mi.sf = sf;
        mi.env = env;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why, true)) {
            if (failures)
                Append(*failures, "\n  candidate ", name, " at ", Where(sf->line), ": ", why);
            continue;
        }
        if (mi.tier == MatchInfo::INTTOFLOAT) {
            converting.push_back(mi);
        } else if (tied.empty() || mi.tier < tied[0].tier) {
            tied = { mi };
        } else if (mi.tier == tied[0].tier) {
            tied.push_back(mi);
        }
    }
}







inline void TypeCheck::ContextualFloatCall(Call *c, vector<SFunction *> &cands, FnSpec *env,
                                           vector<Node *> &argnodes, vector<Val> &argvals,
                                           MatchInfo &best, TypeExpr *expected,
                                           string_view name) {
    auto sf = best.sf;
    if (sf->isextern || sf->rets.size() != 1 || sf->rets[0]->kind != TY_GENERIC) return;
    auto generic = sf->rets[0]->named->name;
    auto own = false;
    for (size_t i = 0; i < sf->generics.size(); i++) {
        if (sf->generics[i].name != generic) continue;
        if (i < c->tyargs.size()) return;   
        own = true;
    }
    if (!own) return;
    auto bindings = best.bindings;
    auto floating = false;
    auto narrow = ast.flttypes[FS_F32];
    for (auto &[n, t] : bindings) {
        if (n != generic) continue;
        if (t->kind != TY_FLT || IsF32(t)) return;
        t = narrow;
        floating = true;
    }
    if (!floating) return;   

    auto trial = argvals;
    auto seen = false;
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto pt = sf->params[i].type;
        if (!pt || !NamesGeneric(pt, generic)) continue;
        
        
        if (pt->kind != TY_GENERIC || i >= best.nwritten) return;
        auto &av = argvals[i];
        
        
        auto integer = IsIntT(av.type) &&
                       (av.ck == CK_INT || av.unsized || av.litint || av.flexint);
        if (!LitFloat(av) && !integer && !contextualfloats.count(argnodes[i])) return;
        
        
        trial[i] = Val {};
        trial[i].type = narrow;
        seen = true;
    }
    if (!seen) return;
    vector<MatchInfo> tied, converting;
    MatchCandidates(c, cands, env, trial, tied, converting, nullptr, name);
    
    
    if (tied.size() != 1 || tied[0].sf != sf ||
        !BindingsEq(tied[0].bindings, bindings)) return;
    contextualfloats.insert(c);
    if (expected && IsF32(expected)) best = tied[0];
}




inline bool TypeCheck::SameMatch(MatchInfo &a, MatchInfo &b) {
    if (a.sf != b.sf || a.nwritten != b.nwritten || a.litparams != b.litparams ||
        a.paramtypes.size() != b.paramtypes.size() || !BindingsEq(a.bindings, b.bindings))
        return false;
    for (size_t k = 0; k < a.paramtypes.size(); k++)
        if (!TypeEq(a.paramtypes[k], b.paramtypes[k])) return false;
    return true;
}












inline void TypeCheck::JudgeCallCasts(vector<SFunction *> &cands, MatchInfo &best,
                                      vector<Node *> &argnodes, vector<Val> &argvals,
                                      const function<bool(vector<Val> &)> &resolves) {
    auto without = [&](const vector<size_t> &deleted) {
        auto trial = argvals;
        for (auto i : deleted) {
            auto &a = castalts[argnodes[i]];
            trial[i] = a.pending ? a.alt : a.raw;
        }
        return resolves(trial);
    };
    auto joint = cands.size() > 1 || !best.bindings.empty();
    for (auto &p : best.sf->params) joint = joint || !p.type || HasGenerics(p.type);
    vector<size_t> differing;
    for (size_t i = 0; i < best.nwritten && i < argnodes.size(); i++)
        if (casttyped.count(argnodes[i])) differing.push_back(i);
    auto together = !joint || differing.size() < 2 || (differing.size() == 2 && without(differing));
    auto pair = !together && differing.size() == 2;
    auto later = pair && without({ differing[1] });
    for (size_t i = 0; i < best.nwritten && i < argnodes.size(); i++) {
        auto it = castalts.find(argnodes[i]);
        if (it == castalts.end()) continue;
        auto a = it->second;
        auto ok = without({ i });
        if (ok && casttyped.count(argnodes[i]) && !together)
            ok = pair && (i == differing[1] || !later);
        if (ok) JudgeCastAt(argnodes[i], best.paramtypes[i], DecayRef(argvals[i]));
        else CastVerdict(a, "");
    }
}

inline bool TypeCheck::TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi,
                                string &why, bool defaults) {
    auto P = sf->params.size();
    auto N = argvals.size();
    auto R = P;   
    if (defaults)
        for (R = 0; R < P && !sf->params[R].defaultval;) R++;
    
    
    
    auto K = std::min(N, P);
    while (K > R && argvals[K - 1].type == fntype) K--;
    if (K < R) {
        why = cat("too few arguments: ", (int64_t)K, " given, ", R < P ? "at least " : "",
                  (int64_t)R, " needed");
        return false;
    }
    for (auto i = K; i < N; i++) {
        if (argvals[i].type != fntype) { why = "too many arguments"; return false; }
    }
    mi.nwritten = K;
    if (c->tyargs.size() > sf->generics.size()) {
        why = "too many explicit type arguments";
        return false;
    }
    for (size_t i = 0; i < c->tyargs.size(); i++) {
        auto t = Subst(c->tyargs[i]);
        ValidateType(t, c->line, VT_LOCAL);
        mi.bindings.push_back({ sf->generics[i].name, t });
    }
    mi.paramtypes.resize(P);
    
    
    
    
    
    auto owngeneric = [&](TypeExpr *t) {
        if (!t || t->kind != TY_GENERIC) return false;
        for (auto &g : sf->generics) if (g.name == t->named->name) return true;
        return false;
    };
    auto isliteral = [](const Val &av) { return av.ck != CK_NONE || av.unsized; };
    set<string_view> litbound;   
    auto paramsok = [&]() {
        
        
        Saved<const vector<GenericParam> *> ex(ownexclude, &sf->generics);
        
        
        FlagScope lr(litrecord, false);
        
        
        
        
        
        
        
        
        auto late = [&](const Val &av) {
            if (av.emptyarr || av.isnull) return 3;
            if (av.litelems) return ArrayLeaf(av.type)->kind == TY_FLT ? 1 : 2;
            return LitFloat(av) ? 1 : isliteral(av) || av.litint || av.flexint ? 2 : 0;
        };
        vector<size_t> order;
        for (auto k = 0; k < 4; k++)
            for (size_t i = 0; i < K; i++) if (late(argvals[i]) == k) order.push_back(i);
        for (auto i : order) {
            auto &p = sf->params[i];
            auto &av = argvals[i];
            auto unsized = false;
            if (isliteral(av) && !p.isvar && !sf->isextern) {
                if (!p.type) {
                    unsized = true;
                } else if (owngeneric(p.type)) {
                    auto name = p.type->named->name;
                    auto bound = false;
                    for (auto &[n, t] : mi.bindings) bound |= n == name;
                    unsized = !bound || litbound.count(name);
                    if (unsized) litbound.insert(name);
                }
            }
            if (unsized) mi.litparams.push_back((int)i);
            if (!p.type) {
                
                
                
                if (av.type == fntype) {
                    why = "function values bind to generic parameters, not untyped ones";
                    return false;
                }
                auto nt = NaturalType(av);
                if (!nt) {
                    why = cat("cannot infer a type for argument ", (int64_t)i + 1);
                    return false;
                }
                mi.paramtypes[i] = nt;
                mi.tier = std::max(mi.tier, 1);
                continue;
            }
            auto ct = UnifyArg(p.type, av, mi.bindings, mi.tier);
            if (!ct) {
                auto st = SubstOwn(p.type, mi.bindings);
                auto at = st && st->kind == TY_REF ? StorageType(av) : av.type;
                why = cat("argument ", (int64_t)i + 1, ": cannot pass ", TypeStr(at), " as ",
                          TypeStr(p.type));
                string litwhy;
                auto tofloat = false;
                if (av.litelems && st && !LitElemsAt(av, st, tofloat, &litwhy) &&
                    !litwhy.empty())
                    Append(why, ": ", litwhy);
                return false;
            }
            mi.paramtypes[i] = ct;
        }
        return true;
    }();
    if (!paramsok) return false;
    
    vector<string_view> unbound;
    for (auto &g : sf->generics) {
        auto found = false;
        for (auto &[n, t] : mi.bindings) found |= n == g.name;
        if (!found) unbound.push_back(g.name);
    }
    
    
    auto undecided = [&](string_view g) {
        for (auto i = K; i < P; i++) {
            if (!NamesGeneric(sf->params[i].type, g)) continue;
            why = cat("cannot infer ", g, ", which the default of parameter ",
                      sf->params[i].name, " does not decide (use explicit <...>)");
            return true;
        }
        return false;
    };
    auto need = (N - K) + (c->trailing ? 1 : 0);
    if (unbound.size() != need) {
        if (!need)
            for (auto g : unbound) if (undecided(g)) return false;
        why = need ? cat((int64_t)need, " function value(s) for ",
                         (int64_t)unbound.size(), " unbound generic parameter(s)")
                   : "cannot infer all generic parameters (use explicit <...>)";
        return false;
    }
    for (size_t k = 0; K + k < N; k++)
        mi.fnvals.push_back({ unbound[k], argvals[K + k].fnv });
    if (c->trailing) {
        FnValBind fb;
        fb.fv = c->trailing;
        fb.env = frames.back().lexspec;
        mi.fnvals.push_back({ unbound.back(), fb });
    }
    
    
    for (auto i = K; i < P; i++) {
        TypeExpr *pt;
        {
            Saved<const vector<GenericParam> *> ex(ownexclude, &sf->generics);
            pt = Subst(sf->params[i].type);
        }
        auto ct = DeclType(sf->params[i].type, SubstOwn(pt, mi.bindings));
        if (HasGenerics(ct)) {
            for (auto g : unbound) if (undecided(g)) return false;
            why = cat("cannot infer the type of parameter ", sf->params[i].name,
                      ", which its default does not decide (use explicit <...>)");
            return false;
        }
        mi.paramtypes[i] = ct;
    }
    return true;
}



inline TypeExpr *TypeCheck::NaturalType(const Val &av) {
    if (av.emptyarr || av.isnull) return nullptr;
    if (av.strlit) return cu8slice;
    return av.type;
}





inline TypeExpr *TypeCheck::UnifyArg(TypeExpr *pt, Val &av,
                                     vector<pair<string_view, TypeExpr *>> &b, int &tier) {
    auto nbind = b.size();
    if (auto ct = UnifyArgRaw(pt, av, b, tier)) return ct;
    if (IsPlainRef(av.type)) {
        b.resize(nbind);  
        auto dv = DecayRef(av);
        if (auto ct = UnifyArgRaw(pt, dv, b, tier)) return ct;
    }
    return nullptr;
}

inline TypeExpr *TypeCheck::UnifyArgRaw(TypeExpr *pt, Val &av,
                                        vector<pair<string_view, TypeExpr *>> &b, int &tier) {
    auto written = pt;
    pt = Subst(pt);  
    auto own = [&]() {
        auto ct = SubstOwn(pt, b);
        return ct ? DeclType(written, ct) : ct;
    };
    if (av.isnull) {
        auto ct = own();
        if (!ct || HasGenerics(ct) || !IsOptional(ct)) return nullptr;
        tier = std::max(tier, 2);
        return ct;
    }
    
    
    if (av.emptyarr) {
        auto ct = own();
        if (!ct || HasGenerics(ct) || (ct->kind != TY_ARRAY && ct->kind != TY_SLICE))
            return nullptr;
        tier = std::max(tier, 2);
        return ct;
    }
    
    
    
    
    if ((av.lvalue || av.storagebranches) && pt->kind == TY_REF && pt->ref->lenstorage < 0 &&
        av.type->kind != TY_REF) {
        Val rv = av;
        rv.type = ast.RefTo(StorageType(av), pt->line);
        rv.lvalue = false;
        return UnifyArgRaw(pt, rv, b, tier);
    }
    auto at = NaturalType(av);
    auto hadgen = HasGenerics(pt);
    if (hadgen) {
        if (!BindTypes(pt, at, b)) {
            
            
            if (pt->kind == TY_SLICE && at->kind == TY_ARRAY)
                BindTypes(pt->sub, at->arr->sub, b);
        }
    }
    auto ct = own();
    if (!ct || HasGenerics(ct)) return nullptr;
    if (TopConstEq(at, ct)) {
        if (hadgen) tier = std::max(tier, 1);
        return ct;
    }
    Val tmp = av;
    auto tofloat = IsIntT(at) && ct->kind == TY_FLT;
    if (av.litelems)
        if (auto lt = LitElemsAt(av, ct, tofloat)) tmp.type = lt;
    if (!FitsAt(tmp, ct)) return nullptr;
    tier = std::max(tier, tofloat ? MatchInfo::INTTOFLOAT : 2);
    return ct;
}






inline TypeExpr *TypeCheck::LitElemsAt(const Val &av, TypeExpr *dt, bool &tofloat, string *why) {
    auto leaf = ArrayLeaf(av.type);
    auto d = dt;
    for (auto t = av.type; t->kind == TY_ARRAY; t = t->arr->sub) {
        if (t->arr->akind != A_FIXED) return nullptr;
        if (d->kind == TY_ARRAY) d = d->arr->sub;
        else if (d->kind == TY_SLICE) d = d->sub;
        else return nullptr;
    }
    if (d->kind == TY_INT) {
        if (leaf->kind != TY_INT) return nullptr;
        if (!FitsIntStorage(av.litlo, false, d->intstorage) ||
            !FitsIntStorage(av.lithi, false, d->intstorage)) {
            if (why)
                *why = av.litlo == av.lithi
                           ? cat("constant ", av.litlo, " does not fit ", TypeStr(d))
                           : cat("its constants ", av.litlo, " to ", av.lithi, " do not all fit ",
                                 TypeStr(d));
            return nullptr;
        }
    } else if (d->kind == TY_FLT) {
        if (leaf->kind != TY_INT && leaf->kind != TY_FLT) return nullptr;
        tofloat = leaf->kind == TY_INT;
    } else {
        return nullptr;
    }
    std::function<TypeExpr *(TypeExpr *)> rebuild = [&](TypeExpr *t) {
        if (t->kind != TY_ARRAY) return d;
        return ast.ArrayOf(rebuild(t->arr->sub), A_FIXED, t->line, t->arr->size);
    };
    return rebuild(av.type);
}

inline bool TypeCheck::HasOpaque(TypeExpr *t) {
    switch (t->kind) {
        case TY_GENERIC: return t->named->opaque;
        case TY_STRUCT: for (auto a : t->struc->args) if (HasOpaque(a)) return true; return false;
        case TY_ENUM:   for (auto a : t->enu->args) if (HasOpaque(a)) return true; return false;
        case TY_ARRAY:  return HasOpaque(t->arr->sub);
        case TY_SLICE:  return HasOpaque(t->sub);
        case TY_REF:    return HasOpaque(t->ref->sub);
        case TY_VARIANT: return HasOpaque(t->var->adt);
        default: return false;
    }
}

inline bool TypeCheck::HasGenerics(TypeExpr *t) {
    switch (t->kind) {
        case TY_GENERIC: return !t->named->opaque;
        case TY_STRUCT: for (auto a : t->struc->args) if (HasGenerics(a)) return true; return false;
        case TY_ENUM:   for (auto a : t->enu->args) if (HasGenerics(a)) return true; return false;
        case TY_ARRAY:  return HasGenerics(t->arr->sub);
        case TY_SLICE:  return HasGenerics(t->sub);
        case TY_REF:    return HasGenerics(t->ref->sub);
        case TY_VARIANT: return HasGenerics(t->var->adt);
        case TY_FN:
            for (auto a : t->fn->args) if (HasGenerics(a)) return true;
            for (auto r : t->fn->rets) if (HasGenerics(r)) return true;
            return false;
        default: return false;
    }
}



inline bool TypeCheck::BindTypes(TypeExpr *pt, TypeExpr *at,
                                 vector<pair<string_view, TypeExpr *>> &b) {
    switch (pt->kind) {
        case TY_GENERIC: {
            auto name = pt->named->name;
            auto bindto = at;
            if (pt->named->varmode) {
                if (at->kind != TY_ENUM) return false;
                if (at->enu->varmode)
                    bindto = ast.EnumOf(at->enu->en, at->enu->args, false, at->line);
            }
            for (auto &[n, t] : b)
                if (n == name) return TypeEq(t, bindto);
            b.push_back({ name, bindto });
            return true;
        }
        case TY_STRUCT:
            if (at->kind != TY_STRUCT || at->struc->st != pt->struc->st ||
                at->struc->args.size() != pt->struc->args.size()) return false;
            for (size_t i = 0; i < pt->struc->args.size(); i++)
                if (!BindTypes(pt->struc->args[i], at->struc->args[i], b)) return false;
            return true;
        case TY_ENUM:
            if (at->kind != TY_ENUM || at->enu->en != pt->enu->en ||
                at->enu->args.size() != pt->enu->args.size()) return false;
            for (size_t i = 0; i < pt->enu->args.size(); i++)
                if (!BindTypes(pt->enu->args[i], at->enu->args[i], b)) return false;
            return true;
        case TY_ARRAY:
            if (at->kind != TY_ARRAY || at->arr->akind != pt->arr->akind) return false;
            return BindTypes(pt->arr->sub, at->arr->sub, b);
        case TY_SLICE:
            if (at->kind != TY_SLICE) return false;
            return BindTypes(pt->sub, at->sub, b);
        case TY_REF:
            if (at->kind != TY_REF) return false;
            return BindTypes(pt->ref->sub, at->ref->sub, b);
        case TY_VARIANT: {
            if (at->kind != TY_VARIANT || at->var->name != pt->var->name) return false;
            return BindTypes(pt->var->adt, at->var->adt, b);
        }
        default:
            return TypeEq(pt, at);
    }
}

inline TypeExpr *TypeCheck::SubstOwn(TypeExpr *pt, vector<pair<string_view, TypeExpr *>> &b) {
    TypeExpr *r = nullptr;
    WithBindings(b, [&]() { r = Subst(pt); });
    return r;
}




inline Val TypeCheck::TryDispatch(Call *c, vector<SFunction *> &cands, FnSpec *env,
                                  vector<Node *> &argnodes, vector<Val> &argvals,
                                  string_view name, bool entryreach) {
    auto found = -1;
    vector<MatchInfo> matches;  
    TypeExpr *enumtype = nullptr;
    auto byref = false;
    
    
    auto adtat = [&](const vector<Val> &vals, size_t pos, bool &isref) -> TypeExpr * {
        auto at = vals[pos].type;
        TypeExpr *et = nullptr;
        isref = false;
        if (at->kind == TY_ENUM) {
            et = at;
            
            
            isref = vals[pos].storagebranches;
        } else if (IsPlainRef(at) && at->ref->sub->kind == TY_ENUM) {
            et = at->ref->sub;
            isref = true;
        }
        
        
        isref = isref && et && et->enu->varmode;
        return et;
    };
    
    
    
    
    
    
    auto dispatches = [&](vector<Val> &vals, size_t pos, TypeExpr *et, bool isref,
                          bool defaults, vector<MatchInfo> &vm) {
        for (auto &var : et->enu->en->variants) {
            auto vt = ast.VariantTypeOf(et, &var, c->line);
            auto saved = vals[pos];
            vals[pos].type = isref ? ast.RefTo(vt, c->line) : vt;
            MatchInfo onlymatch, onlyconverting;
            auto count = 0, converting = 0;
            for (auto sf : cands) {
                MatchInfo mi;
                mi.sf = sf;
                mi.env = env;
                string why;
                if (!TryMatch(sf, c, vals, mi, why, defaults)) continue;
                if (mi.tier == MatchInfo::INTTOFLOAT) {
                    onlyconverting = mi;
                    converting++;
                } else {
                    onlymatch = mi;
                    count++;
                }
            }
            vals[pos] = saved;
            if (!count) {
                count = converting;
                onlymatch = onlyconverting;
            }
            if (count != 1) return false;
            vm.push_back(onlymatch);
        }
        return true;
    };
    auto withdefaults = false;
    for (size_t pos = 0; pos < argvals.size(); pos++) {
        auto isref = false;
        auto et = adtat(argvals, pos, isref);
        if (!et) continue;
        vector<MatchInfo> vm;
        if (!dispatches(argvals, pos, et, isref, false, vm)) {
            vector<MatchInfo> unused;
            withdefaults = withdefaults || dispatches(argvals, pos, et, isref, true, unused);
            continue;
        }
        if (found >= 0)
            Error(c, cat("call to ", name, " could dispatch on more than one argument "
                         "(v1 allows a single dispatch position)"));
        found = (int)pos;
        matches = std::move(vm);
        enumtype = et;
        byref = isref;
    }
    if (found < 0) {
        if (withdefaults)
            Error(c, cat("call to ", name, " dispatches on a variant only with every argument "
                         "written: a dispatched call passes no case's defaults (§8.2)"));
        return Val {};
    }
    CheckWaitingArg(c, argnodes[found], entryreach);
    
    
    
    JudgeCallCasts(cands, matches[0], argnodes, argvals, [&](vector<Val> &trial) {
        vector<MatchInfo> ties, conv;
        MatchCandidates(c, cands, env, trial, ties, conv, nullptr, name);
        if (!ties.empty()) return false;
        auto alike = false;
        for (size_t pos = 0; pos < trial.size(); pos++) {
            auto isref = false;
            auto et = adtat(trial, pos, isref);
            vector<MatchInfo> vm;
            if (!et || !dispatches(trial, pos, et, isref, false, vm)) continue;
            if ((int)pos != found) return false;
            alike = true;
            for (size_t vi = 0; vi < vm.size(); vi++)
                alike = alike && SameMatch(vm[vi], matches[vi]);
        }
        return alike;
    });
    if (ClassOf(enumtype) == SC_RESIZABLE)
        Error(c, "dispatching a resizable ADT payload is not supported by the C backend "
                 "yet; match its tag without a payload binder, or use a standalone "
                 "resizable struct");
    
    
    auto en = enumtype->enu->en;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
        auto vt = ast.VariantTypeOf(enumtype, &en->variants[vi], c->line);
        if (matches[vi].paramtypes[found]->kind == TY_REF) {
            if (!enumtype->enu->varmode)
                Error(c, cat("cannot pass the payload of fixed-mode ", en->name,
                             " by reference to a case of ", name, " (§3.5); take it by "
                             "value: ", TypeStr(vt)));
        } else if (HasRelRefT(vt)) {
            Error(c, cat("cannot copy the payload of ", en->variants[vi].name, ", which "
                         "contains self-relative references, into a case of ", name,
                         " taking it by value (§3.9); take it by reference: ", TypeStr(vt),
                         "&"));
        }
    }
    
    
    
    if (byref && !IsPlainRef(argvals[found].type)) {
        FlagScope rs(reachable, entryreach);
        DestScope ds(*this, Dest {});
        SlotScope ss(*this, false);
        argvals[found] = CheckValue(argnodes[found], ast.RefTo(enumtype, c->line), true);
    }
    if (argvals[found].implicitcopy) ImplicitCopyError(argvals[found].implicitcopy);
    RefCopyWarnings(argvals[found]);
    {
        FlagScope rs(reachable, entryreach);
        BindBranchesByRef(argnodes, argvals, matches[0].paramtypes, matches[0].sf, found);
    }
    
    
    c->dispatcharg = found;
    LoadSliceArgs(argvals, matches[0].paramtypes);
    RefSliceArgs(argvals, matches[0].paramtypes, c->line);
    vector<Val> armvals = argvals;
    FnSpec *first = nullptr;
    for (size_t vi = 0; vi < en->variants.size(); vi++) {
        auto &mi = matches[vi];
        auto vt = ast.VariantTypeOf(enumtype, &en->variants[vi], c->line);
        armvals[found] = argvals[found];
        armvals[found].type = byref ? ast.RefTo(vt, c->line) : vt;
        auto spec = GetOrCreateSpec(mi, armvals, c);
        ApplyCalleeShrinks(c, spec, armvals, name, argnodes);
        ApplyCalleeGrows(c, spec, armvals, name);
        if (first) {
            if (spec->rets.size() != first->rets.size())
                Error(c, cat("case functions of ", name, " disagree on return counts"));
            for (size_t r = 0; r < spec->rets.size(); r++)
                if (!TypeEq(spec->rets[r], first->rets[r]))
                    Error(c, cat("case functions of ", name, " disagree on return types: ",
                                 TypeStr(spec->rets[r]), " vs ", TypeStr(first->rets[r])));
            for (size_t p = 0; p < matches[vi].paramtypes.size(); p++)
                if ((int)p != found &&
                    !TypeEq(matches[vi].paramtypes[p], matches[0].paramtypes[p]))
                    Error(c, cat("case functions of ", name,
                                 " disagree on non-dispatch parameter types"));
        } else {
            first = spec;
        }
        c->dispatch.push_back(spec);
    }
    
    
    
    
    
    if (auto cur = frames.back().spec; cur && cur->incycle) {
        FnSpec *into = nullptr;
        for (auto spec : c->dispatch)
            if (spec->incycle && CycleHead(spec) == CycleHead(cur)) into = spec;
        for (size_t vi = 0; into && vi < en->variants.size(); vi++) {
            auto vt = ast.VariantTypeOf(enumtype, &en->variants[vi], c->line);
            if (matches[vi].paramtypes[found]->kind == TY_REF || !NeedsCycleStack(vt)) continue;
            Error(c, cat(FrameFnName((int)frames.size() - 1), " calls into its recursive cycle "
                         "through ", into->sf->name, " while case parameter ",
                         matches[vi].sf->params[found].name, " holds a by-value copy of "
                         "non-fixed-size ", TypeStr(vt), " (§7.8): take it by reference: ",
                         TypeStr(vt), "&"));
        }
    }
    
    
    auto argsreach = reachable;
    RecheckArgs(c, cands, matches[0], nullptr, argnodes, found, [&]() {
        
        
        auto byreference = false;
        for (auto sp : c->dispatch) byreference |= sp->argtypes[found]->kind == TY_REF;
        RecordVal(argnodes[found], byreference ? argvals[found] : DecayRef(argvals[found]));
    }, entryreach);
    
    for (auto spec : c->dispatch) CheckCalleeRelative(c, spec, argvals);
    CalleesReturn(c, c->dispatch, argsreach);
    
    
    
    
    vector<Val> results;
    vector<bool> reached;
    for (auto spec : c->dispatch) {
        CallResult(c, spec, argvals);
        results.resize(lastcallrets.size());
        reached.resize(lastcallrets.size());
        for (size_t r = 0; r < lastcallrets.size(); r++) {
            auto reaches = spec->inprogress ||
                           (r < spec->record.retroots.size() && spec->record.retroots[r].set);
            results[r] = MergeVals(results[r], reached[r], lastcallrets[r], reaches, c, true);
            reached[r] = reached[r] || reaches;
        }
    }
    lastcallrets = results;
    return results.empty() ? VoidVal() : results[0];
}




inline void TypeCheck::LoadSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes) {
    for (size_t i = 0; i < argvals.size() && i < ptypes.size(); i++) {
        auto &av = argvals[i];
        if (ptypes[i]->kind == TY_SLICE && IsPlainRef(av.type) &&
            av.type->ref->sub->kind == TY_SLICE)
            av.SetProv(SlotView(av, av.type->ref->sub));
    }
}





inline void TypeCheck::RefSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes,
                                    Line at) {
    for (size_t i = 0; i < argvals.size() && i < ptypes.size(); i++) {
        auto &av = argvals[i];
        auto pt = ptypes[i];
        if (!IsPlainRef(pt) || pt->ref->sub->kind != TY_SLICE) continue;
        if (BindsRef(av, pt)) {
            SlotRoots(av);
            av.type = ast.RefTo(ast.PlainOf(av.type), at);
            av.type->cq = !av.writable;
            av.lvalue = false;
        }
        NoteHeld(av, pt->ref->sub);
    }
}




inline void TypeCheck::NoteHeld(Val &av, TypeExpr *slice) {
    if (av.hasheld && IsTemp(av.Root())) return;
    av.held = SlotView(av, slice);
    av.hasheld = true;
}







inline bool TypeCheck::HasView(Val &av, TypeExpr *pt, int reach, SFunction *sf) {
    if (sf->isrec || sf->isextern || !IsPlainRef(pt) ||
        (pt->ref->sub->kind != TY_SLICE && !HoldsPlainRef(pt->ref->sub)) ||
        av.alts.size() != 1)
        return false;
    
    
    auto r = av.Root();
    if (!r || (!av.alts[0].exact && !IsTemp(r)) || ClassDepth(r) <= reach) return false;
    if (pt->ref->sub->kind == TY_SLICE) {
        if (r->type ? r->type->kind != TY_SLICE : !IsTemp(r) && !r->heldslice) return false;
        if (!av.hasheld) NoteHeld(av, pt->ref->sub);
    } else {
        
        
        if (IsTemp(r) || (r->type ? IsRefOrSlice(r->type) : !r->heldholder)) return false;
        auto held = HeldAt(av);
        if (!held.Exact()) return false;
        av.held.TakeAlts(held);
        av.held.writable = true;
        av.held.byteview = r->contentbyteview ||
                           (r->heldholder && r->heldholder->contentbyteview);
        av.hasheld = true;
    }
    return true;
}





inline FnSpec *TypeCheck::GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode) {
    auto sf = mi.sf;
    if (sf->brokensig) throw Abandoned {};
    
    
    
    
    if (sf->isthread)
        Error(callnode, cat("thread_fn ", sf->name, " is spawned with thread_spawn, not called"));
    
    
    
    
    if (sf->isnested && LexFrame(mi.env, sf) >= 0 && !declsiteof.count({ mi.env, sf }))
        Error(callnode, cat(sf->name, " (declared at ", Where(sf->line),
                            ") is called before its declaration is reached (§7.5)"));
    auto narrowedenv = NarrowedOptionals(mi);
    
    vector<RootArg> roots(mi.paramtypes.size());
    vector<RootArg> views(mi.paramtypes.size());
    for (auto &va : views) va.cls = -1;
    vector<VarDef *> argroots(mi.paramtypes.size(), nullptr);
    vector<VarDef *> distinct;
    
    vector<pair<RootArg *, VarDef *>> members;
    auto classify = [&](RootArg &ra, VarDef *r) {
        if (!r) {
            ra.cls = 0;
            return;
        }
        auto idx = -1;
        
        
        
        
        
        
        
        
        if (ra.exact)
            for (auto [m, mr] : members)
                if (mr == r && m->exact) idx = m->cls - 1;
        if (idx < 0) {
            
            
            auto ins = distinct.size();
            while (ins > 0 && ClassDepth(distinct[ins - 1]) > ClassDepth(r)) ins--;
            distinct.insert(distinct.begin() + ins, r);
            for (auto [m, mr] : members) if (m->cls > (int)ins) m->cls++;
            idx = (int)ins;
        }
        ra.cls = idx + 1;
        members.push_back({ &ra, r });
    };
    for (size_t i = 0; i < mi.paramtypes.size(); i++) {
        auto pt = mi.paramtypes[i];
        auto isrs = IsRefOrSlice(pt);
        
        
        
        auto holder = !isrs && HoldsPlainRef(pt);
        if (!isrs && !holder) continue;
        
        
        const Roots &ar = holder ? ContentsOf(argvals[i]) : argvals[i].AsRoots();
        auto r = ar.Root();
        argroots[i] = r;
        auto &ra = roots[i];
        
        
        ra.unknown = isrs ? ar.None() : ar.Unknown();
        
        ra.writable = argvals[i].writable && !pt->cq;
        
        ra.reusable = CarriesPool(pt) ? argvals[i].reusable : 0;
        ra.exact = ar.Exact();
        ra.heldexact = holder && ra.exact && !sf->isrec;
        
        
        
        
        
        
        
        
        auto pointee = isrs ? PointeeOf(pt) : nullptr;
        auto gsroot = ar.Any([&](const RootAlt &a) {
            if (!IsGrowShrinkRoot(a.root)) return false;
            if (!pointee || argvals[i].byteview || ContainsGrowShrink(pointee)) return true;
            return GrowShrinkCanHold(a.root, pointee) ||
                   (!a.exact && BoundReachesGrowShrink(a.root, pointee, false));
        });
        auto gspointee = isrs && ar.Any([&](const RootAlt &a) {
            return !a.exact && ContainsGrowShrink(PointeeOf(pt));
        });
        ra.growshrink = gsroot || gspointee || (isrs && GrowShrinkTaint(argvals[i], pt));
        ra.gsvia = ra.growshrink && !(ar.Exact() && IsGrowShrinkRoot(r));
        
        
        if (ra.growshrink && !ra.gsvia) {
            if (r->type) GrowShrinkElems(r->type, ra.gselems);
            else ra.gselems = r->gselems;
            ra.gsvia = ra.gselems.empty();
        }
        ra.slotread = (gsroot || gspointee) && ar.AllSlotRead();
        
        
        ra.byteview = argvals[i].byteview ||
                      (pt->kind == TY_REF && pt->ref->sub->kind == TY_SLICE &&
                       ar.Any([](const RootAlt &a) {
                           return a.root && a.root->type && IsRefOrSlice(a.root->type) &&
                                  a.root->ref.byteview;
                       }));
        if (ra.exact) ra.pool = PoolOf(r);
        classify(ra, r);
    }
    
    
    for (size_t i = 0; i < roots.size(); i++) {
        auto r = argroots[i];
        if (!r || !r->componentowner || !roots[i].exact) continue;
        for (size_t j = 0; j < roots.size(); j++) {
            auto pt = mi.paramtypes[j];
            if (!roots[j].exact || argroots[j] != r->componentowner ||
                pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_ROOT) continue;
            roots[i].ownerparam = (int)j;
            roots[i].ownerfield = r->componentfield;
            break;
        }
    }
    
    
    auto reach = EnvReach(mi);
    for (size_t i = 0; i < mi.paramtypes.size(); i++) {
        auto pt = mi.paramtypes[i];
        if (!HasView(argvals[i], pt, reach, sf)) continue;
        auto &hp = argvals[i].held;
        auto hr = hp.Root();
        auto &va = views[i];
        va.unknown = hp.None();
        va.writable = hp.writable && !pt->ref->sub->cq;
        va.reusable = CarriesPool(pt->ref->sub) ? hp.reusable : 0;
        va.exact = hp.Exact();
        va.heldexact = pt->ref->sub->kind != TY_SLICE && hp.Exact();
        auto contents = pt->ref->sub;
        auto gsroot = hp.Any([&](const RootAlt &a) { return IsGrowShrinkRoot(a.root); });
        auto holder = contents->kind != TY_SLICE;
        auto gspointee = !holder && hp.Any([&](const RootAlt &a) {
            return !a.exact && ContainsGrowShrink(PointeeOf(contents));
        });
        va.growshrink = gsroot || gspointee || (!holder && GrowShrinkTaint(hp, contents));
        va.gsvia = va.growshrink && !(hp.Exact() && IsGrowShrinkRoot(hr));
        if (va.growshrink && !va.gsvia) {
            if (hr->type) GrowShrinkElems(hr->type, va.gselems);
            else va.gselems = hr->gselems;
            va.gsvia = va.gselems.empty();
        }
        va.slotread = (gsroot || gspointee) && hp.AllSlotRead();
        va.byteview = hp.byteview;
        if (va.exact) va.pool = PoolOf(hr);
        classify(va, hr);
    }
    
    
    
    for (size_t i = 0; i < roots.size(); i++) {
        auto &ra = roots[i];
        if (ra.cls <= 0 || !ra.exact) continue;
        vector<TypeExpr *> elems;
        for (size_t j = 0; j < roots.size(); j++)
            if (roots[j].cls == ra.cls) AppendArrayElemsReached(mi.paramtypes[j], elems);
        for (auto e : elems)
            if (OneArrayOf(argroots[i], e)) ra.onearray.push_back(e);
    }
    
    
    
    
    
    vector<int> depthkeys(distinct.size());
    for (size_t k = 0, rank = 0; k < distinct.size() && !sf->isextern; k++) {
        auto d = ClassDepth(distinct[k]);
        if (d <= reach) {
            depthkeys[k] = d;
            continue;
        }
        if (k == 0 || ClassDepth(distinct[k - 1]) != d) rank++;
        depthkeys[k] = -(int)rank;
    }
    for (auto [ra, r] : members) {
        ra->depth = ClassDepth(distinct[ra->cls - 1]);
        ra->depthkey = depthkeys[ra->cls - 1];
    }
    
    
    
    
    
    
    
    
    
    
    vector<pair<FnSpec *, int>> classes(roots.size(), { nullptr, -1 });
    for (size_t i = 0; i < roots.size(); i++)
        for (auto fi = (int)frames.size() - 1; argroots[i] && !classes[i].first && fi >= 0; fi--)
            for (size_t j = 0; frames[fi].spec && j < frames[fi].spec->params.size(); j++)
                if (ProjectedFrom(argroots[i], frames[fi].spec->params[j]->ref.Root())) {
                    classes[i] = { frames[fi].spec, (int)j };
                    break;
                }
    auto within = [](FnSpec *s, FnSpec *p) {
        for (; s; s = s->lexparent) if (s == p) return true;
        return false;
    };
    auto fat = [&](size_t i) {
        auto pt = mi.paramtypes[i];
        return argroots[i] && pt->kind == TY_REF && pt->ref->lenstorage < 0 &&
               (ClassOf(pt->ref->sub) == SC_RESIZABLE || ClassOf(pt->ref->sub) == SC_ROOT);
    };
    FnSpec *owner = nullptr;
    auto external = false;
    for (size_t i = 0; i < roots.size(); i++) {
        if (!fat(i) || !classes[i].first) continue;
        external = external || (owner && classes[i].first != owner);
        owner = classes[i].first;
    }
    for (size_t i = 0; i < roots.size(); i++) {
        auto r = argroots[i];
        if (owner && fat(i) && !classes[i].first && !(r->ownerspec && within(r->ownerspec, owner)))
            external = true;
    }
    for (size_t i = 0; i < roots.size(); i++) {
        auto r = argroots[i];
        if (!r) continue;
        if (classes[i].first) {
            roots[i].concrete = !external;
            roots[i].via.push_back(classes[i]);
        } else {
            roots[i].concrete = (r->isglobal || r->ownerspec) &&
                                (!owner || (!external && r->ownerspec && within(r->ownerspec, owner)));
        }
    }
    for (auto spec : sf->specs) {
        
        
        if (spec->id < exemplarbase) continue;
        if (spec->lexparent != mi.env) continue;
        if (!spec->inprogress && spec->narrowedenv != narrowedenv) continue;
        
        
        
        if (!spec->inprogress && !spec->stale && !spec->failed &&
            (spec->storesout || !EnvUnchanged(spec)))
            continue;
        
        
        if (spec->stale && !CycleHead(spec)->inprogress) continue;
        if (!TypeArgsEq(spec->argtypes, mi.paramtypes)) continue;
        
        
        if (!BindingsEq(spec->bindings, mi.bindings)) continue;
        if (spec->litparams != mi.litparams) continue;
        if (spec->fnvals.size() != mi.fnvals.size()) continue;
        auto fvok = true;
        for (size_t i = 0; i < mi.fnvals.size(); i++)
            fvok &= spec->fnvals[i].second == mi.fnvals[i].second;
        if (!fvok) continue;
        auto rootsok = spec->roots == roots && spec->views == views;
        for (size_t i = 0; rootsok && i < roots.size(); i++)
            rootsok = TypeArgsEq(spec->roots[i].gselems, roots[i].gselems) &&
                      TypeArgsEq(spec->views[i].gselems, views[i].gselems) &&
                      TypeArgsEq(spec->roots[i].onearray, roots[i].onearray);
        
        
        
        auto onearrayok = true;
        for (size_t i = 0; i < roots.size() && i < spec->roots.size(); i++)
            for (auto e : spec->roots[i].onearray) {
                auto has = false;
                for (auto f : roots[i].onearray) has = has || TypeEq(e, f);
                onearrayok = onearrayok && has;
            }
        if (!onearrayok) continue;
        auto depthsok = rootsok;
        for (size_t i = 0; depthsok && i < roots.size(); i++)
            depthsok = spec->roots[i].depthkey == roots[i].depthkey &&
                       spec->views[i].depthkey == views[i].depthkey;
        
        
        
        
        
        if (!depthsok && !spec->inprogress) continue;
        
        
        
        
        auto needsok = true;
        for (auto target : spec->needs) {
            for (auto i = (int)frames.size() - 1; i >= 0; i--) {
                if (frames[i].sf != target->sf || frames[i].isfunval) continue;
                needsok &= frames[i].spec == target;
                break;
            }
        }
        if (!needsok && !spec->inprogress) continue;
        
        
        
        if (spec->failed) throw Abandoned {};
        
        
        
        
        for (size_t i = 0; i < roots.size() && i < spec->roots.size(); i++) {
            auto &sr = spec->roots[i];
            sr.exact = sr.exact && roots[i].exact;
            sr.concrete = sr.concrete && roots[i].concrete && rootsok;
            for (auto &v : roots[i].via)
                if (find(sr.via.begin(), sr.via.end(), v) == sr.via.end()) sr.via.push_back(v);
            if (rootsok) spec->views[i].exact = spec->views[i].exact && views[i].exact;
        }
        if (spec->inprogress) {
            for (auto v : spec->narrowedenv)
                if (!v->narrowed)
                    Error(callnode, cat("recursive call requires optional ", v->name,
                                        " to remain narrowed (§3.8)"));
            ValidateCycle(spec, callnode);
            ValidatePoolArgs(spec, argvals, callnode);
            ValidateThreadArgs(spec, argvals, callnode);
        } else if (CycleHead(spec)->inprogress) {
            
            
            JoinCycle(spec, callnode);
            ValidateThreadArgs(spec, argvals, callnode);
        }
        vector<pair<SFunction *, FnSpec *>> path;
        for (auto &f : frames) path.push_back({ f.isfunval ? nullptr : f.sf, f.spec });
        spec->neededges.push_back({ callnode, std::move(path) });
        ValidateNeeds(spec, callnode);
        NoteLitArgs(spec, argvals, callnode);
        
        if (spec->stale) {
            spec->stale = false;
            CheckSpecBodyOnce(spec, &argvals, callnode->line, Is<Call>(callnode));
        } else if (!spec->inprogress) {
            ReplayOuterExits(spec);
            ReplayEnvExits(spec);
        }
        NoteCalleeEnvReads(spec);
        return spec;
    }
    
    
    auto nested = 0;
    for (auto s : sf->specs) nested += s->inprogress;
    if (nested >= MAXNESTEDSPECS) {
        string inst;
        DumpInstance(inst, sf, mi.paramtypes, mi.litparams, mi.bindings, mi.fnvals);
        Error(callnode, cat("instantiating ", inst, " would put more than ", MAXNESTEDSPECS,
                            " specializations of ", sf->qname, " in progress on this call path: "
                            "a recursive call must reach a finite set of instantiations (§7.8)"));
    }
    auto spec = ast.NewFnSpec();
    spec->sf = sf;
    spec->lexparent = mi.env;
    spec->narrowedenv = narrowedenv;
    spec->argtypes = mi.paramtypes;
    spec->roots = roots;
    spec->views = views;
    spec->litparams = mi.litparams;
    spec->fnvals = mi.fnvals;
    spec->bindings = mi.bindings;
    
    
    NoteLitArgs(spec, argvals, callnode);
    sf->specs.push_back(spec);
    CheckSpecBody(spec, &argvals, callnode->line, Is<Call>(callnode));
    NoteCalleeEnvReads(spec);
    return spec;
}



inline vector<VarDef *> TypeCheck::NarrowedOptionals(const MatchInfo &mi) {
    vector<VarDef *> out;
    set<VarDef *> seenvars;
    set<FnSpec *> seenenvs;
    auto optional = [](VarDef *v) {
        return v->narrowed && v->type && v->type->kind == TY_REF && v->type->ref->optional;
    };
    auto add = [&](VarDef *v) {
        if (optional(v) && seenvars.insert(v).second) out.push_back(v);
    };
    for (auto v : narrowedglobals) add(v);
    
    
    function<void(FnSpec *, int)> addenv = [&](FnSpec *e, int fi) {
        if (e && !seenenvs.insert(e).second) return;
        for (; fi >= 0; fi = frames[fi].lexframe) {
            auto end = fi + 1 < (int)frames.size() ? frames[fi + 1].varbase : (int)vars.size();
            for (auto i = frames[fi].varbase; i < end; i++) add(vars[i]);
        }
        for (auto sp = e; sp; sp = sp->lexparent)
            for (auto &fv : sp->fnvals) addenv(fv.second.env, LexFrame(fv.second));
    };
    addenv(mi.env, LexFrame(mi.env, mi.sf));
    for (auto &fv : mi.fnvals) addenv(fv.second.env, LexFrame(fv.second));
    return out;
}

inline EnvRead TypeCheck::EnvReadOf(VarDef *vd) {
    return { vd, vd->assigned, vd->maybeassigned, vd->refrootknown, vd->ref, vd->contents,
             vd->contentbyteview, vd->slotref };
}







inline void TypeCheck::NoteEnvRead(VarDef *vd) {
    auto spec = CurRealFrame().spec;
    if (!spec || vd->isglobal || vd->ownerspec == spec) return;
    for (auto &r : spec->envreads) if (r.var == vd) return;
    spec->envreads.push_back(EnvReadOf(vd));
}





inline void TypeCheck::NoteCalleeEnvReads(FnSpec *callee) {
    auto spec = CurRealFrame().spec;
    if (!spec || spec == callee) return;
    for (auto &r : callee->envreads) {
        if (r.var->ownerspec == spec) continue;
        auto known = false;
        for (auto &s : spec->envreads) known = known || s.var == r.var;
        if (!known) spec->envreads.push_back(r);
    }
}





inline bool TypeCheck::EnvIs(const VarDef *vd, const EnvRead &r) {
    return vd->assigned == r.assigned &&
           (vd->isvar || vd->maybeassigned == r.maybeassigned) &&
           vd->refrootknown == r.refrootknown &&
           vd->ref == r.ref && vd->contents == r.contents &&
           vd->contentbyteview == r.contentbyteview && vd->slotref == r.slotref;
}



inline bool TypeCheck::EnvUnchanged(const FnSpec *spec) {
    for (auto &r : spec->envreads) if (!EnvIs(r.var, r)) return false;
    return true;
}





inline void TypeCheck::RecordEnvExits(FnSpec *spec) {
    spec->envexits.clear();
    for (auto &r : spec->envreads) spec->envexits.push_back(EnvReadOf(r.var));
}







inline void TypeCheck::ReplayEnvExits(FnSpec *spec) {
    for (auto &x : spec->envexits) {
        auto v = x.var;
        
        
        
        v->assigned = x.assigned;
        v->maybeassigned = v->maybeassigned || x.maybeassigned;
        if (EnvIs(v, x)) continue;
        v->refrootknown = x.refrootknown;
        v->ref = x.ref;
        v->contents = x.contents;
        v->contentbyteview = x.contentbyteview;
        v->slotref = v->slotref || x.slotref;
        NoteFact(v);
    }
}








inline void TypeCheck::NoteExit(int tf, const OuterExit *added) {
    auto e = frames[tf].exits;
    if (!reachable || !e) return;
    auto isassigned = [&](VarDef *v) {
        return v->assigned || (added && added->assigned.count(v));
    };
    auto maybe = [&](VarDef *v) {
        return v->maybeassigned || (added && added->maybeassigned.count(v));
    };
    for (size_t i = 0; i < e->vars.size(); i++) {
        e->assigned[i] = (!e->reached || e->assigned[i]) && isassigned(e->vars[i]);
        e->maybeassigned[i] = (e->reached && e->maybeassigned[i]) || maybe(e->vars[i]);
    }
    e->reached = true;
    auto target = frames[tf].spec;
    for (auto k = tf + 1; k < (int)frames.size(); k++) {
        auto mine = frames[k].exits;
        if (!mine) continue;
        OuterExit x { target };
        for (auto v : e->vars) {
            auto at = find(mine->vars.begin(), mine->vars.end(), v);
            if (at == mine->vars.end()) continue;
            if (isassigned(v)) x.assigned.insert(v);
            if (maybe(v) && !mine->maybeonentry[at - mine->vars.begin()])
                x.maybeassigned.insert(v);
        }
        auto &outer = frames[k].spec->outerexits;
        auto known = false;
        for (auto &o : outer) {
            if (o.target != target) continue;
            
            
            std::erase_if(o.assigned, [&](VarDef *v) { return !x.assigned.count(v); });
            o.maybeassigned.insert(x.maybeassigned.begin(), x.maybeassigned.end());
            known = true;
        }
        if (!known) outer.push_back(std::move(x));
    }
}






inline void TypeCheck::ReplayOuterExits(FnSpec *spec) {
    for (auto &x : spec->outerexits) {
        for (auto i = (int)frames.size() - 1; i >= 0; i--) {
            if (frames[i].isfunval || frames[i].spec != x.target) continue;
            NoteExit(i, &x);
            break;
        }
    }
}



inline void TypeCheck::ShareCycleEnvReads(FnSpec *head) {
    auto &members = head->cyclemembers;
    vector<EnvRead> all;
    auto add = [](vector<EnvRead> &to, const EnvRead &r) {
        for (auto &s : to) if (s.var == r.var) return;
        to.push_back(r);
    };
    for (auto m : members)
        for (auto &r : m->envreads)
            if (find(members.begin(), members.end(), r.var->ownerspec) == members.end())
                add(all, r);
    for (auto m : members)
        for (auto &r : all) add(m->envreads, r);
}

inline void TypeCheck::ApplyCalleeRebinds(FnSpec *spec) {
    
    
    auto rec = RecordOf(spec);
    if (!rec) return;
    auto caller = CurRealFrame().spec;
    for (auto v : rec->reboundoptionals) {
        v->narrowed = nullptr;
        if (caller && v->ownerspec != caller) caller->record.reboundoptionals.insert(v);
    }
}



inline void TypeCheck::RecordLitAdapt(const Val &v, TypeExpr *t, Line at) {
    if (!litrecord || !v.unsizedparam) return;
    auto vd = v.unsizedparam;
    auto spec = vd->ownerspec;
    if (!spec) return;
    for (size_t i = 0; i < spec->params.size(); i++) {
        if (spec->params[i] != vd) continue;
        for (auto &la : spec->litadapts)
            if (la.param == (int)i && TypeEq(la.type, t)) return;
        spec->litadapts.push_back(LitAdapt { (int)i, t, at });
    }
}




inline void TypeCheck::NoteLitArgs(FnSpec *spec, vector<Val> &argvals, Node *at) {
    for (auto li : spec->litparams) {
        if (li >= (int)argvals.size()) continue;
        auto &av = argvals[li];
        if (av.ck != CK_NONE) {
            RelyOnNamed(av, at);
            litchecks.push_back(LitCheck { spec, li, av, at });
        } else if (av.unsized && av.unsizedparam && av.unsizedparam->ownerspec) {
            auto from = av.unsizedparam->ownerspec;
            for (size_t i = 0; i < from->params.size(); i++)
                if (from->params[i] == av.unsizedparam)
                    from->litflows.push_back(LitFlow { (int)i, spec, li });
        }
    }
}

inline void TypeCheck::VerifyLiterals() {
    for (auto &lc : litchecks) CheckItem([&] {
        set<pair<FnSpec *, int>> seen;
        vector<pair<FnSpec *, int>> todo { { lc.spec, lc.param } };
        while (!todo.empty()) {
            auto [spec, param] = todo.back();
            todo.pop_back();
            if (!seen.insert({ spec, param }).second) continue;
            for (auto &la : spec->litadapts) {
                if (la.param != param || la.type->kind != TY_INT || lc.lit.ck != CK_INT) continue;
                if (FitsIntStorage(lc.lit.ival, lc.lit.uns, la.type->intstorage)) continue;
                Error(lc.at, cat("argument ", (int64_t)lc.param + 1, " of ", lc.spec->sf->name,
                                 ": constant ", ConstStr(lc.lit), " does not fit ",
                                 TypeStr(la.type), ", which parameter ",
                                 spec->sf->params[param].name, " takes at ", Where(la.at),
                                 " (§7.7)"));
            }
            for (auto &lf : spec->litflows)
                if (lf.param == param) todo.push_back({ lf.to, lf.toparam });
        }
    });
}

inline void TypeCheck::ValidateCycle(FnSpec *spec, Node *callnode) {
    if (!spec->sf->isrec)
        Error(callnode, cat("recursive call cycle through ", spec->sf->name,
                            ", which is not declared `recursive fn` (§7.8)"));
    auto fi = FrameOfSpec(spec);
    if (fi < 0) fi = 0;
    for (auto i = fi; i < (int)frames.size(); i++) {
        if (!frames[i].spec || !frames[i].sf || frames[i].isfunval) continue;
        for (auto &p : frames[i].sf->params)
            if (!p.type)
                Error(callnode, cat("function ", frames[i].sf->name, " is in a recursive "
                                    "cycle and needs fully explicit parameter types"));
    }
    JoinCycle(spec, callnode);
    
    
    
    spec->retsknown = true;
}

inline FnSpec *TypeCheck::CycleHead(FnSpec *s) {
    while (s->cyclelink) s = s->cyclelink;
    return s;
}







inline void TypeCheck::JoinCycle(FnSpec *spec, Node *callnode) {
    auto head = CycleHead(spec);
    auto fi = FrameOfSpec(head);
    if (fi < 0) fi = 0;
    auto last = (int)frames.size() - 1;
    for (auto i = fi; i <= last; i++) {
        auto &f = frames[i];
        
        
        
        f.cyclecall = callnode->line;
        for (auto j = i + 1; j <= last; j++)
            if (frames[j].callline.line > 0) { f.cyclecall = frames[j].callline; break; }
        f.cyclecalls++;
        if (!f.spec || !f.sf || f.isfunval) continue;
        f.spec->incycle = true;
        
        
        if (auto h = CycleHead(f.spec); h != head) {
            h->cyclelink = head;
            for (auto m : h->cyclemembers) head->cyclemembers.push_back(m);
            h->cyclemembers.clear();
        }
        auto &members = head->cyclemembers;
        if (find(members.begin(), members.end(), f.spec) == members.end())
            members.push_back(f.spec);
        if (!f.spec->joinedat.line) f.spec->joinedat = f.cyclecall;
        NoInferredRefResult(f.spec, f.cyclecall);
    }
    for (auto i = fi; i <= last; i++) {
        auto end = i < last ? frames[i + 1].varbase : (int)vars.size();
        for (auto vi = frames[i].varbase; vi < end; vi++) {
            auto v = vars[vi];
            if (!v->type || !NeedsCycleStack(v->type)) continue;
            if (v->isparam)
                Error(frames[i].cyclecall,
                      cat(FrameFnName(i), " calls into its recursive cycle while by-value "
                          "non-fixed-size parameter ", v->name, " is in scope, as it is for "
                          "the whole body (§7.8): take it by reference or as a slice"));
            Error(frames[i].cyclecall,
                  cat(FrameFnName(i), " calls into its recursive cycle while non-fixed-size "
                      "local ", v->name, " (declared at ", Where(v->line), ") is in scope "
                      "(§7.8): end its scope before the call"));
        }
    }
    for (auto i = fi; i <= last; i++) CycleCallTemps(i, callnode);
    CycleCallResult(spec, callnode);
}














inline void TypeCheck::NoteTemp(Node *n, const Val &v, int cyclecalls) {
    auto nonfixed = v.type && NeedsCycleStack(v.type);
    n->nftemp = nonfixed && !v.lvalue && !v.pointee;
    auto fi = (int)frames.size() - 1;
    if (frames[fi].cyclecalls == cyclecalls) return;
    if (auto c = Is<Call>(n); c && c->fvbody && nonfixed) {
        auto &path = StateOf(fi).nodepath;
        auto k = (int)path.size() - 1;
        if (k >= 0 && path[k].node == n && !BuiltInPlace(fi, k, { v.type }))
            Error(n, cat(FrameFnName(fi), " calls into its recursive cycle inside the block it "
                         "calls here, whose non-fixed-size value would land in a temporary, "
                         "holding a data stack across the call (§7.8): return it as the whole "
                         "result, or take an output buffer by reference and build into it in "
                         "place"));
        return;
    }
    const char *where = nullptr;
    if (auto c = Is<Call>(n)) {
        if (c->builtin == B_PRINT) where = "the arguments of print(...), whose text";
        else if (c->builtin == B_STR) where = "the arguments of str(...), whose text";
    } else if (nonfixed && Is<ArrayLit>(n)) {
        where = "a non-fixed-size array literal, which";
    } else if (nonfixed && Is<StructLit>(n)) {
        where = "a non-fixed-size struct literal, which";
    }
    if (!where) return;
    Error(frames[fi].cyclecall,
          cat(FrameFnName(fi), " calls into its recursive cycle inside ", where, " is under "
              "construction (§7.8): a value under construction holds a data stack across the "
              "call; make the call in a statement of its own first, or take an output buffer "
              "by reference and build into it in place"));
}








inline Node *TypeCheck::FindTemp(Node *n, bool self) {
    if (!n) return nullptr;
    if (self && n->nftemp) return n;
    if (auto ie = Is<IfExpr>(n)) return FindTemp(ie->cond);
    if (auto m = Is<MatchExpr>(n)) return FindTemp(m->scrutinee);
    if (Is<Block>(n) || Is<EarlyBlock>(n) || Is<LoopExpr>(n) || Is<While>(n) ||
        Is<ForLoop>(n) || Is<FunVal>(n) || Is<InlineBlock>(n))
        return nullptr;
    if (auto b = Is<Binary>(n); b && (b->op == T_ANDAND || b->op == T_OROR))
        return FindTemp(b->left);
    Node *found = nullptr;
    n->Children([&](Node *ch) { if (!found) found = FindTemp(ch); });
    return found;
}













inline void TypeCheck::CycleCallTemps(int fi, Node *callnode) {
    auto &path = StateOf(fi).nodepath;
    for (size_t k = 0; k < path.size(); k++) {
        auto &e = path[k];
        if (e.frame != fi) continue;
        auto n = e.node;
        auto inplace = [&](int at) {
            if (Is<Return>(n) || Is<VarDecl>(n)) return true;
            if (auto a = Is<Assign>(n)) return at == 1 && a->op == T_ASSIGN;
            auto c = Is<Call>(n);
            return c && (c->builtin == B_PUSH || c->builtin == B_APPEND) && at == 1;
        };
        Node *t = nullptr;
        auto i = 0;
        auto direct = false;
        ForOperands(n, [&](Node *ch, HoldKind) {
            auto at = i++;
            if (t || at >= e.pos) return;
            t = FindTemp(ch, !inplace(at));
            direct = t == ch && n == callnode;
        });
        if (!t && k + 1 < path.size()) {
            auto next = path[k + 1].node;
            if (auto ie = Is<IfExpr>(n)) {
                if (next != ie->cond) t = FindTemp(ie->cond);
            } else if (auto m = Is<MatchExpr>(n)) {
                if (next != m->scrutinee) t = FindTemp(m->scrutinee);
            } else if (auto w = Is<While>(n)) {
                if (next != w->cond) t = FindTemp(w->cond);
            } else if (auto fl = Is<ForLoop>(n)) {
                auto r = Is<RangeExpr>(fl->iter);
                if (next != fl->iter && !(r && (next == r->lo || next == r->hi)))
                    t = FindTemp(fl->iter);
            }
        }
        if (!t) continue;
        if (direct)
            Error(frames[fi].cyclecall,
                  cat(FrameFnName(fi), " calls into its recursive cycle with the non-fixed-size "
                      "temporary ", ExprStr(t), " as an argument (§7.8): pass it in from outside "
                      "the cycle"));
        Error(frames[fi].cyclecall,
              cat(FrameFnName(fi), " calls into its recursive cycle while the non-fixed-size "
                  "temporary ", ExprStr(t), " (evaluated at ", Where(t->line), ") is live "
                  "(§7.8): a temporary lives to the end of its statement, so build it in a "
                  "statement before the call, or pass it in from outside the cycle"));
    }
}








inline void TypeCheck::CycleCallResult(FnSpec *spec, Node *callnode) {
    auto last = (int)frames.size() - 1;
    auto fi = FrameOfSpec(CycleHead(spec));
    if (fi < 0) fi = 0;
    for (auto i = fi; i <= last; i++) {
        if (!frames[i].spec) continue;
        auto callee = i == last ? spec : frames[i + 1].spec;
        if (i < last && (!callee || !frames[i + 1].sf || frames[i + 1].isfunval)) continue;
        auto nonfixed = false;
        for (auto rt : callee->rets) nonfixed |= ClassOf(rt) != SC_FIXED;
        if (!nonfixed) continue;
        auto &path = StateOf(i).nodepath;
        auto k = (int)path.size() - 1;
        while (k >= 0 && (path[k].frame != i || !Is<Call>(path[k].node))) k--;
        if (k < 0 || (i == last && path[k].node != callnode)) continue;
        if (BuiltInPlace(i, k, callee->rets)) continue;
        Error(path[k].node, cat(FrameFnName(i), " calls into its recursive cycle through ",
                                callee->sf->name, ", whose non-fixed-size result would land in "
                                "a temporary here, holding a data stack across the call (§7.8): "
                                "return it as the whole result, or take an output buffer by "
                                "reference and build into it in place"));
    }
}



















inline bool TypeCheck::BuiltInPlace(int fi, int k, const vector<TypeExpr *> &rets) {
    auto &path = StateOf(fi).nodepath;
    auto returned = [&](FnSpec *target, size_t at, size_t count) {
        if (!target || !target->retsknown) return false;
        if (count == 1 && rets.size() > 1) {
            
            if (target->rets.size() != rets.size()) return false;
            for (size_t r = 0; r < rets.size(); r++)
                if (!TypeEq(rets[r], target->rets[r])) return false;
            return true;
        }
        return rets.size() == 1 && at < target->rets.size() && TypeEq(rets[0], target->rets[at]);
    };
    auto exiting = false;
    for (k--; k >= 0; k--) {
        auto parent = path[k].node, child = path[k + 1].node;
        
        if (path[k].frame != fi) {
            auto c = Is<Call>(parent);
            return !exiting && frames[fi].tailvalue && c && c->fvbody && c->fvbody->tail == child;
        }
        
        
        if (exiting) {
            if (Is<LoopExpr>(parent) || Is<EarlyBlock>(parent)) exiting = false;
            else if (Is<While>(parent) || Is<ForLoop>(parent)) return false;
            continue;
        }
        if (Is<Break>(parent)) { exiting = true; continue; }
        if (auto b = Is<Block>(parent)) { if (b->tail == child) continue; return false; }
        if (auto ie = Is<IfExpr>(parent)) { if (child != ie->cond) continue; return false; }
        if (auto m = Is<MatchExpr>(parent)) { if (child != m->scrutinee) continue; return false; }
        if (Is<EarlyBlock>(parent) || Is<LoopExpr>(parent)) continue;
        if (auto r = Is<Return>(parent)) {
            size_t at = 0;
            while (at < r->vals.size() && r->vals[at] != child) at++;
            return returned(r->targetspec, at, r->vals.size());
        }
        if (auto c = Is<Call>(parent))
            return (c->builtin == B_PUSH || c->builtin == B_APPEND) && OperandIndex(c, child) == 1;
        return Is<VarDecl>(parent) != nullptr;
    }
    
    
    
    auto body = frames[fi].isfunval ? nullptr : frames[fi].spec->body;
    return !exiting && body && path[0].node == body->tail && returned(frames[fi].spec, 0, 1);
}




inline void TypeCheck::NoInferredRefResult(FnSpec *spec, Line at) {
    if (!spec->sf || spec->sf->has_rets) return;
    for (auto rt : spec->rets)
        if (IsPlainRef(rt))
            Error(at, cat("function ", spec->sf->name, " is in a recursive cycle and returns "
                          "a reference, which needs an explicit result type (§7.8)"));
}



inline string_view TypeCheck::FrameFnName(int fi) {
    for (; fi >= 0; fi--)
        if (frames[fi].sf) return frames[fi].sf->name;
    return "global initialization";
}




inline VarDef *TypeCheck::UltimateRoot(VarDef *v) {
    while (v && v->classfrom) v = v->classfrom;
    return v;
}






inline void TypeCheck::ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode) {
    for (size_t i = 0; i < spec->params.size() && i < argvals.size(); i++) {
        auto pr = spec->params[i]->ref.Root();
        if (!pr || argvals[i].None()) continue;   
        
        
        if (pr->classpool &&
            (!argvals[i].Exact() || PoolOf(argvals[i].Root()) != pr->classpool))
            Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                " rooted outside ", pr->classpool->name,
                                ", which the cycle's entry call rooted there (§3.9)"));
        if (!pr->poolclass) continue;
        if (!argvals[i].Exact() || UltimateRoot(pr) != UltimateRoot(argvals[i].Root()))
            Error(callnode, cat("recursive call passes ", spec->params[i]->name,
                                " rooted differently from the cycle's entry call; this "
                                "parameter must keep the same storage throughout the cycle (§7.8)"));
    }
}










inline void TypeCheck::ValidateThreadArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode) {
    for (size_t i = 0; i < spec->params.size() && i < argvals.size(); i++) {
        auto pr = spec->params[i]->ref.Root();
        auto it = pr ? threadedclasses.find(pr) : threadedclasses.end();
        if (it == threadedclasses.end() || it->second.broken) continue;
        
        auto holder = !IsRefOrSlice(spec->argtypes[i]);
        auto av = holder ? ContentsOf(argvals[i]) : argvals[i].AsRoots();
        if (av.None()) continue;   
        auto ar = av.Root();
        if (av.Exact() && ar == pr) continue;
        
        
        auto rooted = !holder && (av.Exact() || (it->second.bounded && av.alts.size() == 1));
        auto why = cat(spec->inprogress ? "the recursive call at " : "the call at ",
                       Where(callnode->line), " passes ", spec->params[i]->name,
                       " rooted differently from ",
                       spec->inprogress ? "the cycle's entry call"
                                        : cat("the first call of ", spec->sf->name));
        if (rooted && UltimateRoot(pr) == UltimateRoot(ar) && ThreadedChain(ar)) {
            if (auto at = threadedclasses.find(ar); at != threadedclasses.end()) {
                auto &heirs = at->second.heirs;
                if (find(heirs.begin(), heirs.end(), pr) == heirs.end()) heirs.push_back(pr);
            }
        } else if (vector<VarDef *> through; PointsOutside(pr, av, through)) {
            for (auto r : through) {
                auto &heirs = threadedclasses[r].heirs;
                if (find(heirs.begin(), heirs.end(), pr) == heirs.end()) heirs.push_back(pr);
            }
            BoundThread(pr, why);
        } else {
            Unthread(pr, why);
        }
    }
}










inline bool TypeCheck::PointsOutside(VarDef *pr, const Roots &v, vector<VarDef *> &through) {
    return !v.unknown && v.All([&](const RootAlt &a) {
        if (a.root == pr) return true;
        if (Depth(a.root) > Depth(pr)) return false;
        if (CycleStorable(a.root)) return true;
        if (!ThreadStorable(a.root)) return false;
        through.push_back(a.root);
        return true;
    });
}







inline bool TypeCheck::ThreadedChain(VarDef *r) {
    if (!r || !r->classfrom || r->poolclass) return true;
    auto it = threadedclasses.find(r);
    return it != threadedclasses.end() && !it->second.broken && !it->second.bounded;
}





inline void TypeCheck::NoteThreadedClass(VarDef *cls) {
    auto from = cls->classfrom;
    ThreadedClass *parent = nullptr;
    if (from && from->classfrom && !from->poolclass) {
        auto it = threadedclasses.find(from);
        if (it == threadedclasses.end()) return;
        parent = &it->second;
    }
    auto &tc = threadedclasses[cls];
    if (!parent) return;
    tc.bounded = parent->bounded;
    if (parent->broken) {
        tc.broken = true;
        tc.why = parent->why;
    } else {
        parent->heirs.push_back(cls);
    }
}




inline bool TypeCheck::ThreadStorable(VarDef *r) {
    auto it = threadedclasses.find(r);
    if (it == threadedclasses.end() || it->second.broken) return false;
    auto u = UltimateRoot(r);
    return u && (u->isglobal ||
                 (u->ownerspec && !u->ownerspec->incycle && !u->ownerspec->sf->isrec));
}





inline void TypeCheck::Unthread(VarDef *cls, const string &why) {
    auto it = threadedclasses.find(cls);
    if (it == threadedclasses.end() || it->second.broken) return;
    auto &tc = it->second;
    tc.broken = true;
    tc.why = why;
    for (size_t k = 0; k < tc.heirs.size(); k++) Unthread(tc.heirs[k], why);
}





inline void TypeCheck::BoundThread(VarDef *cls, const string &why) {
    auto it = threadedclasses.find(cls);
    if (it == threadedclasses.end() || it->second.broken || it->second.bounded) return;
    auto &tc = it->second;
    tc.bounded = true;
    tc.why = why;
    threadbounds++;
    for (size_t k = 0; k < tc.heirs.size(); k++) BoundThread(tc.heirs[k], why);
}




inline string TypeCheck::CycleStoreError(VarDef *root, const string &more) {
    string s = "references may only be passed down, not stored, inside a recursive cycle (§7.8)";
    auto sep = ": ";
    if (auto it = threadedclasses.find(root);
        it != threadedclasses.end() && !it->second.why.empty()) {
        Append(s, sep, it->second.why);
        sep = "; ";
    }
    if (!more.empty()) Append(s, sep, more);
    return s;
}






template<typename F>
void TypeCheck::NeedOnPath(FnSpec *spec, FnSpec *t, Node *at, int n, F step) {
    auto found = n - 1;
    while (found >= 0 && step(found).first != t->sf) found--;
    if (found < 0)
        Error(at, cat("call to ", spec->sf->name, " requires an enclosing call of ", t->sf->name,
                      " (it does `return ... from ", t->sf->name, "`)"));
    if (step(found).second != t)
        Error(at, "recursive call changes the enclosing specialization of a long-distance return");
    for (auto i = found + 1; i < n; i++) AddNeed(step(i).second, t);
}



inline void TypeCheck::ValidateNeeds(FnSpec *spec, Node *callnode) {
    for (auto t : spec->needs)
        NeedOnPath(spec, t, callnode, (int)frames.size(), [&](int i) {
            return pair<SFunction *, FnSpec *>(frames[i].isfunval ? nullptr : frames[i].sf,
                                               frames[i].spec);
        });
}





inline void TypeCheck::AddNeed(FnSpec *s, FnSpec *t) {
    if (!s || s == t || !s->needs.insert(t).second) return;
    for (auto &e : s->neededges)
        NeedOnPath(s, t, e.first, (int)e.second.size(), [&](int i) { return e.second[i]; });
}





inline SFunction *TypeCheck::TextFn() {
    for (auto i = frames.size(); i-- > 0;)
        if (frames[i].sf) return frames[i].sf;
    return nullptr;
}

inline void TypeCheck::CheckfreeCallee(Node *at, SFunction *callee) {
    auto t = TextFn();
    if (!t || !t->checkfree || !callee || callee->checkfree || callee->isextern) return;
    Error(at, cat("checkfree fn ", t->qname, " calls ", callee->qname, ", which is not "
                  "checkfree: the bounds checks left in it would run inside ", t->qname,
                  " (§7.14)"));
}



inline bool TypeCheck::ExternValueOk(TypeExpr *t, string &why) {
    if (HoldsIndexT(t)) {
        why = "C code is not checked against the array an index type names (§3.11)";
        return false;
    }
    switch (t->kind) {
        case TY_INT:
            if (t->intstorage == IS_VARINT) { why = "varints have no C form"; return false; }
            return true;
        case TY_FLT: case TY_BOOL: return true;
        case TY_REF: case TY_SLICE: why = "nested references and slices do not cross"; return false;
        default: break;
    }
    if (ClassOf(t) != SC_FIXED) { why = "only fixed-size values cross by value"; return false; }
    if (!IsFlat(t)) { why = "values holding references do not cross"; return false; }
    return true;
}

inline bool TypeCheck::ExternParamOk(TypeExpr *t, string &why) {
    if (t->kind == TY_REF) {
        if (t->ref->optional || t->ref->lenstorage >= 0) {
            why = "only plain references cross";
            return false;
        }
        auto s = t->ref->sub;
        
        
        if (s->kind == TY_ARRAY && s->arr->akind == A_GROW) return ExternValueOk(s->arr->sub, why);
        return ExternValueOk(s, why);
    }
    if (t->kind == TY_SLICE) return ExternValueOk(t->sub, why);
    return ExternValueOk(t, why);
}









inline bool TypeCheck::DylibValueOk(SFunction *sf, TypeExpr *t, string &why) {
    if (sf->dylib.empty() || t->kind == TY_REF || t->kind == TY_SLICE) return true;
    auto padded = Reaches(t, [&](TypeExpr *t) {
        if (t->kind != TY_STRUCT) return false;
        for (auto &f : GetStructInst(t)->st->fields) if (f.ispad) return true;
        return false;
    });
    if (!padded) return true;
    why = "a struct with pad passes by value unlike the library's own struct, whose padding "
          "the calling convention leaves out; pass it by reference (§7.10)";
    return false;
}


inline bool IsCKeyword(string_view n) {
    static const unordered_set<string_view> ckeywords = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
        "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
        "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
        "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas",
        "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
        "_Static_assert", "_Thread_local"
    };
    return ckeywords.count(n) != 0;
}



inline void TypeCheck::CheckExternSpec(FnSpec *spec) {
    auto sf = spec->sf;
    if (!sf->generics.empty())
        Error(sf->line, cat("extern fn ", sf->name, " cannot be generic"));
    
    
    
    
    auto ident = !sf->cname.empty() && !isdigit((unsigned char)sf->cname[0]);
    for (auto c : sf->cname)
        if (!(isalnum((unsigned char)c) || c == '_')) ident = false;
    if (!ident)
        Error(sf->line, cat("extern fn ", sf->name, ": C symbol \"", sf->cname,
                            "\" is not a C identifier"));
    else if (IsCKeyword(sf->cname))
        Error(sf->line, cat("extern fn ", sf->name, ": C symbol ", sf->cname,
                            " is a C keyword"));
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        if (!p.type) Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name,
                                         " needs a type"));
        auto pt = spec->argtypes[i];
        ValidateType(pt, sf->line, VT_PARAM);
        string why;
        if (!ExternParamOk(pt, why) || !DylibValueOk(sf, pt, why))
            Error(sf->line, cat("extern fn ", sf->name, ": parameter ", p.name, " of type ",
                                TypeStr(pt), " cannot cross to C: ", why));
        auto vd = ast.NewVarDef();
        vd->name = p.name;
        vd->type = pt;
        vd->line = p.line.line ? p.line : sf->line;
        vd->isparam = true;
        vd->ownerspec = spec;
        spec->params.push_back(vd);
    }
    if (sf->rets.size() > 1)
        Error(sf->line, cat("extern fn ", sf->name, " returns at most one value"));
    spec->rets.clear();
    for (auto rt : sf->rets) {
        auto t = Subst(rt);
        ValidateType(t, sf->line, VT_LOCAL);
        string why;
        if (!ExternValueOk(t, why) || !DylibValueOk(sf, t, why))
            Error(sf->line, cat("extern fn ", sf->name, " cannot return ", TypeStr(t), ": ",
                                why));
        spec->rets.push_back(t);
    }
    spec->retsknown = true;
    spec->inprogress = false;
}

inline void TypeCheck::CheckExport(SFunction *sf) {
    if (sf->brokensig) throw Abandoned {};
    auto fail = [&](const string &why) {
        Error(sf->line, cat("export fn ", sf->name, ": ", why));
    };
    if (sf->isnested || sf->isthread || sf->isextern)
        fail("must be a top-level Faloya function");
    if (!sf->generics.empty()) fail("cannot be generic");
    if (sf->cname.empty()) fail("needs a C symbol");
    if (sf->cname == "main" || sf->cname == "faloya_init" || sf->cname == "faloya_exit")
        fail(cat("C symbol ", sf->cname, " is reserved"));
    if (sf->cname.rfind("gs_", 0) == 0)
        fail("C symbols beginning with gs_ are reserved for Faloya internals");
    auto valid_ident = [](string_view n) {
        if (n.empty() || n[0] == '_' || !isalpha((unsigned char)n[0])) return false;
        for (auto c : n)
            if (!(isalnum((unsigned char)c) || c == '_')) return false;
        return true;
    };
    if (!valid_ident(sf->cname)) fail("C symbol must be a C identifier");
    
    
    if (IsCKeyword(sf->cname)) fail("C symbol cannot be a C keyword");
    for (auto other : ast.functions)
        if (other != sf && other->isexport && other->cname == sf->cname)
            fail(cat("C symbol ", sf->cname, " is exported more than once"));
    for (auto &p : sf->params) {
        if (!p.type) fail(cat("parameter ", p.name, " needs a type"));
        if (p.defaultval) fail(cat("parameter ", p.name, " cannot have a default"));
    }
    set<string_view> names = { sf->name };
    auto foreign = false;
    if (sf->body) ScanForeignFrom(sf->body, names, foreign);
    if (foreign) fail("cannot use return ... from across the C boundary");
    if (sf->specs.size() != 1)
        fail("could not resolve one concrete function signature");
    auto spec = sf->specs[0];
    
    
    if (spec->failed) throw Abandoned {};
    if (spec->params.size() != sf->params.size())
        fail("could not resolve one concrete function signature");
    for (size_t i = 0; i < spec->argtypes.size(); i++) {
        auto t = spec->argtypes[i];
        ValidateType(t, sf->line, VT_PARAM);
        string why;
        if (!ExternParamOk(t, why))
            fail(cat("parameter ", sf->params[i].name, " of type ", TypeStr(t),
                     " cannot cross to C: ", why));
        if (CarriesPool(t))
            fail(cat("parameter ", sf->params[i].name,
                     " uses a reusable pool and has no public C representation"));
        if (t->kind == TY_REF && t->ref->sub->kind == TY_ARRAY &&
            t->ref->sub->arr->akind == A_GROW && IsU8(t->ref->sub->arr->sub))
            fail(cat("parameter ", sf->params[i].name,
                     " is a Faloya string builder and cannot be passed from C"));
    }
    if (spec->rets.size() > 1) fail("returns at most one value");
    if (!sf->has_rets && !spec->rets.empty())
        fail("a returned value needs an explicit return type");
    for (auto t : spec->rets) {
        ValidateType(t, sf->line, VT_LOCAL);
        if (t->kind == TY_SLICE && IsU8(t->sub)) {
            if (!t->cq)
                fail("a returned u8 slice must be read-only (const u8[:])");
            continue;
        }
        string why;
        if (!ExternValueOk(t, why))
            fail(cat("cannot return ", TypeStr(t), " to C: ", why));
    }
}






inline int TypeCheck::ClassDepth(VarDef *r) {
    return IsTemp(r) ? CurDepth() + 1 : Depth(r);
}





inline int TypeCheck::EnvReach(const MatchInfo &mi) {
    auto reach = 0;
    auto add = [&](FnSpec *env, int fi) {
        if (!env && fi < 0) return;
        auto open = fi < 0 || fi + 1 == (int)frames.size() ? CurDepth() : frames[fi + 1].scopebase;
        reach = max(reach, open);
    };
    add(mi.env, LexFrame(mi.env, mi.sf));
    for (auto &fv : mi.fnvals) add(fv.second.env, LexFrame(fv.second));
    return reach;
}















inline void TypeCheck::CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline,
                                     Call *call) {
    ast.CheckCancel();
    struct RoundScope {
        TypeCheck &tc;
        RoundScope(TypeCheck &t) : tc(t) { tc.cyclerounds.push_back({ tc.CurDepth(), false }); }
        ~RoundScope() { tc.cyclerounds.pop_back(); }
    } roundscope(*this);
    CheckSpecBodyOnce(spec, argvals, callline, call);
    while (spec->incycle && CycleHead(spec) == spec) {
        
        
        if (spec->rounds >= 8)
            Error(callline, cat("the recursive cycle through ", spec->sf->name,
                                " does not settle (internal limit of 8 rounds)"));
        auto broken = 0;
        for (auto &[cls, tc] : threadedclasses) broken += tc.broken;
        auto bounds = threadbounds;
        auto members = spec->cyclemembers;
        if (getenv("FALOYA_ROUNDS"))
            fprintf(stderr, "round %d of %.*s begins (%d members)\n", spec->rounds + 1,
                    (int)spec->sf->name.size(), spec->sf->name.data(), (int)members.size());
        for (auto m : members) {
            m->prev = make_unique<FnRecord>(std::exchange(m->record, FnRecord {}));
            m->stale = m != spec;
        }
        cyclerounds.back().changed = false;
        CheckSpecBodyOnce(spec, argvals, callline, call);
        for (auto &[cls, tc] : threadedclasses) broken -= tc.broken;
        auto outer = cyclerounds.back().changed;
        if (outer && getenv("FALOYA_ROUNDS"))
            fprintf(stderr, "round %d of %.*s: a variable outside it changed\n", spec->rounds,
                    (int)spec->sf->name.size(), spec->sf->name.data());
        auto settled = broken == 0 && bounds == threadbounds && !outer;
        for (auto m : members)
            if (!m->stale) settled = settled && SameRecord(m->record, *m->prev, m);
        for (auto m : members) m->prev = nullptr;
        if (settled) break;
    }
    if (spec->incycle && CycleHead(spec) == spec) ShareCycleEnvReads(spec);
}

inline bool TypeCheck::SameRecord(const FnRecord &a, const FnRecord &b, const FnSpec *spec) {
    
    
    auto sameroots = [&](const Roots &x, const Roots &y) {
        return x.Same(y, Roots::Compare::Cycle);
    };
    
    
    auto why = [&](const char *what) {
        if (getenv("FALOYA_ROUNDS"))
            fprintf(stderr, "round %d of %.*s: %s changed\n", spec->rounds, (int)spec->sf->name.size(),
                    spec->sf->name.data(), what);
        return false;
    };
    auto sametype = [&](TypeExpr *x, TypeExpr *y) { return !x == !y && (!x || TypeEq(x, y)); };
    if (a.relativestores.size() != b.relativestores.size()) return why("relative destinations");
    for (auto &p : a.relativestores)
        if (none_of(b.relativestores.begin(), b.relativestores.end(), [&](const auto &q) {
                return p.result == q.result && sameroots(p.source, q.source) &&
                       sameroots(p.destination, q.destination);
            }))
            return why("relative destinations");
    if (a.mayreturn != b.mayreturn) return why("normal completion");
    if (a.retroots.size() != b.retroots.size()) return why("returns");
    for (size_t i = 0; i < a.retroots.size(); i++) {
        auto &p = a.retroots[i], &q = b.retroots[i];
        if (!sameroots(p.alts, q.alts) || p.writable != q.writable || p.byteview != q.byteview ||
            p.freshview != q.freshview || p.set != q.set || p.heldparam != q.heldparam)
            return why("return roots");
    }
    if (a.shrinkexternals != b.shrinkexternals || a.shrinkparams != b.shrinkparams)
        return why("shrinks");
    auto samegrows = [&](const auto &x, const auto &y) {
        if (x.size() != y.size()) return false;
        for (auto i = x.begin(), j = y.begin(); i != x.end(); ++i, ++j) {
            if (i->first != j->first || i->second.size() != j->second.size()) return false;
            for (size_t k = 0; k < i->second.size(); k++)
                if (!sametype(i->second[k], j->second[k])) return false;
        }
        return true;
    };
    if (!samegrows(a.growexternals, b.growexternals) || !samegrows(a.growparams, b.growparams))
        return why("growths");
    if (a.reboundoptionals != b.reboundoptionals) return why("rebinds");
    auto samebounds = [&](const auto &x, const auto &y) {
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); i++)
            if (x[i].key != y[i].key || x[i].balance != y[i].balance ||
                !sametype(x[i].type, y[i].type))
                return false;
        return true;
    };
    if (!samebounds(a.shrinkexternalbounds, b.shrinkexternalbounds) ||
        !samebounds(a.shrinkparambounds, b.shrinkparambounds))
        return why("shrink bounds");
    if (a.liveshrinks.size() != b.liveshrinks.size()) return why("live shrinks");
    for (size_t i = 0; i < a.liveshrinks.size(); i++) {
        auto &p = a.liveshrinks[i], &q = b.liveshrinks[i];
        if (p.shrunk != q.shrunk || p.shrunkexact != q.shrunkexact || p.live != q.live ||
            p.liveexact != q.liveexact || p.liveprojected != q.liveprojected ||
            p.byteview != q.byteview || p.growonly != q.growonly ||
            p.contents != q.contents || p.inplace != q.inplace || !sametype(p.bound, q.bound) ||
            !sametype(p.pointee, q.pointee))
            return why("live shrinks");
    }
    auto sameevents = [&](const StoreEvent &p, const StoreEvent &q) {
        return p.container == q.container && p.root == q.root && p.src == q.src &&
               p.exact == q.exact && p.byteview == q.byteview && p.bound == q.bound &&
               p.slot == q.slot && p.sliceref == q.sliceref && p.classread == q.classread &&
               sametype(p.pointee, q.pointee) && sametype(p.reached, q.reached);
    };
    if (a.classevents.size() != b.classevents.size()) return why("class stores");
    for (size_t i = 0; i < a.classevents.size(); i++)
        if (!sameevents(a.classevents[i], b.classevents[i])) return why("class stores");
    return true;
}

inline void TypeCheck::CheckSpecBodyOnce(FnSpec *spec, vector<Val> *argvals, Line callline,
                                         Call *call) {
    
    
    spec->inprogress = true;
    
    
    if (StackLow()) {
        auto depth = 0;
        for (auto &f : frames) depth += f.sf && !f.isfunval;
        Error(callline, cat("compile-time call path too deep for the compiler's stack (",
                            depth, " nested calls)"));
    }
    auto sf = spec->sf;
    if (sf->isextern) { CheckExternSpec(spec); return; }
    spec->record.eventstart = storeevents.size();
    spec->envreads.clear();
    
    
    auto oldparams = std::move(spec->params);
    spec->params.clear();
    
    
    
    
    
    
    
    vector<pair<VarDef *, TypeExpr *>> outernarrowed;
    vector<pair<VarDef *, bool>> outerassigned;
    BodyExits exits;
    auto lexframe = LexFrame(spec->lexparent, sf);
    EachNamedVar(lexframe, spec, [&](int i) {
        outernarrowed.push_back({ vars[i], vars[i]->narrowed });
        if (vars[i]->assigned) {
            outerassigned.push_back({ vars[i], vars[i]->maybeassigned });
            return;
        }
        exits.vars.push_back(vars[i]);
        exits.maybeonentry.push_back(vars[i]->maybeassigned);
    });
    exits.assigned.resize(exits.vars.size());
    exits.maybeassigned.resize(exits.vars.size());
    for (auto g : ast.globals) for (auto v : g->defs) outernarrowed.push_back({ v, v->narrowed });
    Frame f;
    f.sf = sf;
    f.spec = spec;
    f.lexspec = spec;
    f.lexframe = lexframe;
    if (f.lexframe >= 0)
        if (auto it = declsiteof.find({ spec->lexparent, sf }); it != declsiteof.end())
            f.decl = it->second;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = callline;
    f.expansiontrace = ActiveExpansionTrace();
    f.exits = &exits;
    f.argvals = argvals;
    f.call = call;
    frames.push_back(f);
    auto fi = (int)frames.size() - 1;
    
    
    
    
    BodyScope bodyscope(*this);
    frames[fi].bodyidx = (int)outerbodies.size();
    auto savereach = reachable;
    DestScope ds(*this, Dest {});
    Saved<vector<Dest>> cds(callresultdsts, {});
    Saved<TypeExpr *> cdt(callresulttype, nullptr);
    
    
    
    SlotScope ss(*this, false);
    FlagScope rs(inreturn, false);
    reachable = true;
    PushScope(SK_FN);
    
    
    
    auto &classroots = spec->classroots;
    auto nclasses = 0;
    for (auto &ra : spec->roots) nclasses = max(nclasses, ra.cls);
    for (auto &va : spec->views) nclasses = max(nclasses, va.cls);
    classroots.resize(nclasses + 1, nullptr);
    
    vector<bool> exactrefs(classroots.size(), true);
    for (size_t i = 0; i < sf->params.size(); i++) {
        auto &p = sf->params[i];
        auto pt = spec->argtypes[i];
        ValidateType(pt, sf->line, VT_PARAM);
        auto vd = NewVar(p.name, pt, p.line.line ? p.line : sf->line, p.isvar,
                         i < oldparams.size() ? oldparams[i] : nullptr);
        vd->isparam = true;
        vd->assigned = vd->maybeassigned = true;
        for (auto li : spec->litparams) if (li == (int)i) vd->unsized = true;
        if (IsRefOrSlice(pt)) {
            auto &ra = spec->roots[i];
            if (ra.unknown) {
                vd->ref.SetUnknown();   
            } else if (ra.cls == 0) {
                vd->ref.Set(nullptr, true);  
            } else {
                if (!classroots[ra.cls]) {
                    auto rv = ast.NewVarDef();
                    rv->name = p.name;
                    rv->depth = ra.depth;
                    rv->classfrom = argvals ? (*argvals)[i].Root() : nullptr;
                    rv->poolclass = true;
                    rv->classpool = ra.pool;
                    rv->growshrink = ra.growshrink;
                    rv->gsvia = ra.gsvia;
                    rv->gselems = ra.gselems;
                    rv->onearray = ra.onearray;
                    classroots[ra.cls] = rv;
                }
                ReachedThroughRefs(pt, classroots[ra.cls]->classreach);
                
                
                if (!ra.pool) classroots[ra.cls]->classpool = nullptr;
                
                
                
                if (pt->kind != TY_REF ||
                    (ClassOf(pt->ref->sub) != SC_RESIZABLE &&
                     !(ClassOf(pt->ref->sub) == SC_ROOT && NeedsCycleStack(pt->ref->sub))) ||
                    !ra.exact)
                    classroots[ra.cls]->poolclass = false;
                if (!ra.exact) exactrefs[ra.cls] = false;
                
                
                
                
                vd->ref.Set(classroots[ra.cls], true, nullptr, ra.slotread);
                classroots[ra.cls]->contentbyteview |= ra.byteview;
            }
            vd->refrootknown = true;
            vd->ref.writable = ra.writable;
            vd->ref.reusable = ra.reusable;
            vd->ref.byteview = ra.byteview;
        } else if (HoldsPlainRef(pt)) {
            
            
            
            
            auto &ra = spec->roots[i];
            if (ra.unknown) {
                
                vd->ref.Clear();
                vd->refrootknown = true;
                vd->contents.SetUnknown();
                spec->params.push_back(vd);
                continue;
            }
            VarDef *cr = nullptr;
            if (ra.cls != 0) {
                if (!classroots[ra.cls]) {
                    auto rv = ast.NewVarDef();
                    rv->name = p.name;
                    rv->depth = ra.depth;
                    rv->classfrom = argvals ? HolderRootOf((*argvals)[i]) : nullptr;
                    rv->growshrink = ra.growshrink;
                    rv->gsvia = ra.gsvia;
                    rv->gselems = ra.gselems;
                    rv->onearray = ra.onearray;
                    classroots[ra.cls] = rv;
                }
                classroots[ra.cls]->poolclass = false;
                ReachedThroughRefs(pt, classroots[ra.cls]->classreach);
                exactrefs[ra.cls] = false;
                cr = classroots[ra.cls];
            }
            vd->contentbyteview = ra.byteview;
            
            
            
            vd->ref.Set(cr, ra.heldexact);
            vd->refrootknown = true;
            
            
            Roots held;
            held.Set(cr, ra.heldexact, nullptr, ra.slotread);
            auto stored = vd->storedinto;
            auto first = storeevents.size();
            RecordStore(vd, held, ra.byteview, nullptr);
            vd->storedinto = stored;
            if (!ra.heldexact)
                for (auto k = first; k < storeevents.size(); k++) storeevents[k].incoming = true;
        }
        spec->params.push_back(vd);
    }
    for (size_t i = 0; i < spec->roots.size(); i++) {
        auto &ra = spec->roots[i];
        if (ra.ownerparam < 0) continue;
        auto parent = spec->params[ra.ownerparam]->ref.Root();
        auto t = spec->argtypes[ra.ownerparam]->ref->sub;
        auto component = RootComponent(parent, t, ra.ownerfield);
        auto old = classroots[ra.cls];
        classroots[ra.cls] = component;
        for (auto p : spec->params)
            for (auto &a : p->ref.alts) if (a.root == old) a.root = component;
    }
    
    
    
    set<VarDef *> heldset;
    for (size_t i = 0; i < sf->params.size() && i < spec->views.size(); i++) {
        auto &va = spec->views[i];
        if (va.cls < 0) continue;
        auto pt = spec->argtypes[i];
        auto vd = spec->params[i];
        VarDef *vr = nullptr;
        if (va.cls > 0) {
            auto &rv = classroots[va.cls];
            if (!rv) {
                rv = ast.NewVarDef();
                rv->name = sf->params[i].name;
                rv->depth = va.depth;
                rv->classfrom = argvals ? (*argvals)[i].held.Root() : nullptr;
                rv->classpool = va.pool;
                rv->growshrink = va.growshrink;
                rv->gsvia = va.gsvia;
                rv->gselems = va.gselems;
            }
            ReachedThroughRefs(pt->ref->sub, rv->classreach);
            rv->poolclass = false;
            if (!va.pool) rv->classpool = nullptr;
            rv->contentbyteview |= va.byteview;
            if (pt->ref->sub->kind == TY_SLICE || !va.heldexact)
                exactrefs[va.cls] = false;
            vr = rv;
        }
        auto slot = vd->ref.Root();
        if (!slot || !heldset.insert(slot).second) continue;
        auto holder = pt->ref->sub->kind != TY_SLICE;
        auto &saved = holder ? slot->heldholder : slot->heldslice;
        auto h = saved;
        if (h) *h = VarDef {};
        else h = saved = ast.NewVarDef();
        h->name = sf->params[i].name;
        h->type = pt->ref->sub;
        h->line = sf->line;
        h->depth = vd->depth;
        h->ownerspec = spec;
        h->assigned = true;
        h->refrootknown = true;
        if (holder) {
            if (va.unknown) h->contents.SetUnknown();
            else h->contents.Set(vr, va.heldexact);
            h->contentbyteview = va.byteview;
            continue;
        }
        if (va.unknown) h->ref.SetUnknown();
        else h->ref.Set(vr, true, nullptr, va.slotread);
        h->ref.writable = va.writable;
        h->ref.reusable = va.reusable;
        h->ref.byteview = va.byteview;
        h->ref.freshview = va.byteview;
    }
    
    
    
    
    
    
    auto readthrough = [&](size_t k) {
        if (!argvals) return false;
        auto any = false;
        for (size_t i = 0; i < sf->params.size() && i < argvals->size(); i++) {
            if (i < spec->views.size() && spec->views[i].cls == (int)k) return false;
            if (spec->roots[i].cls != (int)k) continue;
            auto holder = !IsRefOrSlice(spec->argtypes[i]);
            auto av = holder ? ContentsOf((*argvals)[i]) : (*argvals)[i].AsRoots();
            if (av.unknown || av.alts.size() != 1 || (av.alts[0].exact && !holder))
                return false;
            auto r = av.alts[0].root;
            if (!r) return false;
            if (!r->classfrom) {
                if (IsClassRoot(r)) return false;
            } else if (!r->poolclass) {
                auto it = threadedclasses.find(r);
                if (it == threadedclasses.end() || it->second.broken || it->second.bounded)
                    return false;
            }
            any = true;
        }
        return any;
    };
    for (size_t k = 1; !spec->rounds && k < classroots.size(); k++) {
        if (!classroots[k] || classroots[k]->poolclass) continue;
        if (exactrefs[k]) {
            NoteThreadedClass(classroots[k]);
        } else if (readthrough(k)) {
            NoteThreadedClass(classroots[k]);
            if (auto it = threadedclasses.find(classroots[k]); it != threadedclasses.end())
                it->second.bounded = true;
        }
    }
    if (sf->has_rets && !spec->retsknown) {
        for (auto rt : sf->rets) {
            auto ct = Subst(rt);
            
            
            
            if (IsRelRef(rt))
                Error(sf->line, cat("function ", sf->name, " cannot return ", TypeStr(ct),
                                    ": a result is a value, never a relative reference "
                                    "(§3.9); return ", TypeStr(LoadType(ct)),
                                    ", which a relative slot receiving it encodes"));
            ct = ValueType(ct);
            ValidateType(ct, sf->line, VT_RET);
            spec->rets.push_back(ct);
        }
        spec->retsknown = true;
    }
    
    
    
    
    
    if (!spec->body) spec->body = (Block *)sf->body->Clone(ast);
    
    
    
    
    
    
    auto abandoned = false;
    {
        auto nscopes = scopes.size(), nvars = vars.size(), nlocalfns = localfns.size();
        auto ntypechain = typechain.size(), nlater = laterpointees.size();
        auto building = typesbuilding;
        auto savefit = fitnode;
        try {
            
            
            
            BlockScope bs(*this, spec->body);
            CheckStmts(spec->body);
            if (spec->body->tail) {
                auto tail = spec->body->tail;
                auto asvalue = !(spec->retsknown && spec->rets.empty());
                if (IsValuelessTail(tail)) asvalue = false;
                if (!asvalue) {
                    CheckStmtExpr(tail);
                    if (reachable && spec->retsknown && !spec->rets.empty())
                        Error(tail, cat("function ", sf->name, " must return value(s)"));
                } else {
                    auto expected = spec->retsknown && spec->rets.size() == 1 ? spec->rets[0]
                                                                              : nullptr;
                    Val tv;
                    {
                        FlagScope ret(inreturn, true);
                        RetValScope rv(*this, spec->body->tail);
                        DestScope rd(*this, ResultDest(spec));
                        Saved<Node *> mn(multidestnode, spec->body->tail);
                        vector<Dest> dests;
                        for (size_t i = 0; i < max<size_t>(1, spec->rets.size()); i++)
                            dests.push_back(ResultDest(spec, (int)i));
                        Saved<vector<Dest>> md(multidests, std::move(dests));
                        tv = spec->retsknown ? CheckValue(spec->body->tail, expected)
                                             : CheckInferredResult(spec->body->tail, spec);
                    }
                    tail = spec->body->tail;
                    if (reachable) {
                        if (tv.type->kind == TY_VOID) {
                            if (spec->retsknown && !spec->rets.empty())
                                Error(tail, cat("function ", sf->name, " must return value(s)"));
                        } else {
                            vector<Val> vals = { tv };
                            RecordReturn(spec, vals, tail);
                            NoteExit(fi);
                            reachable = false;
                        }
                    }
                }
            }
        } catch (Abandoned &) {
            if (!sf->broken) throw;
            abandoned = true;
            frames.resize(fi + 1);
            scopes.resize(nscopes);
            vars.resize(nvars);
            localfns.resize(nlocalfns);
            typechain.resize(ntypechain);
            laterpointees.resize(nlater);
            typesbuilding = building;
            fitnode = savefit;
            reachable = false;
        }
    }
    if (abandoned) {
        exits.reached = true;
        spec->record.retroots.resize(spec->rets.size());
        for (auto &rr : spec->record.retroots) {
            rr.alts.SetUnknown();
            rr.writable = true;
            rr.set = true;
        }
    } else if (reachable) {
        NoteExit(fi);
        if (spec->retsknown && !spec->rets.empty())
            Error(sf->body, cat("function ", sf->name,
                                " can fall off the end without returning value(s)"));
    }
    spec->retsknown = true;   
    if (!abandoned) {
        NamedResultCopyWarning(spec);
        CheckNamedResultUses(spec);
    }
    PopScope();
    frames.pop_back();
    for (auto [v, n] : outernarrowed) v->narrowed = n;
    
    
    
    for (auto [v, m] : outerassigned) v->maybeassigned = m;
    
    
    if (exits.reached) {
        for (size_t i = 0; i < exits.vars.size(); i++) {
            exits.vars[i]->assigned = exits.assigned[i];
            exits.vars[i]->maybeassigned = exits.maybeassigned[i];
        }
    }
    spec->record.mayreturn = exits.reached;
    reachable = savereach;
    RecordEnvExits(spec);
    spec->inprogress = false;
    spec->record.eventend = storeevents.size();
    spec->record.bodyevents.clear();
    for (auto i = spec->record.eventstart; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        spec->record.bodyevents.push_back({ e.container, e.root, e.src, e.exact, e.classread });
    }
    spec->rounds++;
}





inline void TypeCheck::NamedResultCopyWarning(FnSpec *spec) {
    for (size_t j = 0; j < spec->rets.size(); j++) {
        if (ClassOf(spec->rets[j]) == SC_FIXED) continue;
        NamedResultStop ns;
        if (faloya::NamedResultOf(spec->body, spec->sf, spec->rets.size(), j, &ns) || !ns.at)
            continue;
        Warn(ns.at, cat("`", ns.local->name, "` is copied on return rather than built where the ",
                        "result goes: ", ns.why, " (§7.3)"));
    }
}




inline int TypeCheck::HeldParam(FnSpec *spec, const Val &v) {
    auto x = v.holderfrom;
    if (!x || !x->type || IsRefOrSlice(x->type) || x->storedinto) return -1;
    auto p = find(spec->params.begin(), spec->params.end(), x) - spec->params.begin();
    return p < (ptrdiff_t)spec->params.size() ? (int)p : -1;
}




inline void TypeCheck::RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at) {
    
    vector<TypeExpr *> types;
    for (auto &v : vals) {
        if (v.type == fntype) Error(at, "function values cannot be returned (§7.6)");
        types.push_back(v.type);
    }
    
    
    auto inferredearlier = tspec->retsknown && tspec->sf && !tspec->sf->has_rets;
    if (!tspec->retsknown) {
        for (auto &v : vals) {
            if (v.type->kind == TY_VOID)
                Error(at, "cannot return a valueless expression");
            tspec->rets.push_back(v.type);
        }
        tspec->retsknown = true;
        if (tspec->incycle) NoInferredRefResult(tspec, at->line);
    } else {
        if (vals.size() != tspec->rets.size())
            Error(at, cat("returning ", (int64_t)vals.size(), " value(s), function ",
                          tspec->sf ? tspec->sf->name : string_view("?"), " has ",
                          (int64_t)tspec->rets.size()));
    }
    if (tspec->record.retroots.size() < tspec->rets.size())
        tspec->record.retroots.resize(tspec->rets.size());
    for (size_t i = 0; i < vals.size(); i++) {
        auto rt = tspec->rets[i];
        auto isrs = IsRefOrSlice(rt);
        auto holder = !isrs && HoldsPlainRef(rt);
        if (!isrs && !holder) continue;
        
        if (vals[i].isnull) continue;
        
        
        
        
        Roots roots = holder ? ContentsOf(vals[i]) : vals[i].AsRoots();
        
        
        
        for (auto &a : roots.alts) a.from = nullptr;
        auto &rr = tspec->record.retroots[i];
        for (auto &a : roots.alts) {
            auto root = a.root;
            
            
            
            if (root && root->ownerspec == tspec)
                Error(at, cat("returning a reference rooted in ", root->name,
                              ", which dies with this function (§9.2)",
                              inferredearlier ? cat("; the result type ", TypeStr(rt),
                                                    " is inferred from an earlier return")
                                              : string()));
            if (IsTemp(root))
                Error(at, "returning a reference into a temporary");
        }
        
        
        auto held = holder ? HeldParam(tspec, vals[i]) : -1;
        rr.heldparam = !rr.set || rr.heldparam == held ? held : -1;
        rr.alts.Add(roots);
        rr.writable = rr.writable && vals[i].writable;
        rr.byteview = rr.byteview || vals[i].byteview;
        rr.freshview = rr.freshview || (isrs && vals[i].freshview);
        rr.set = true;
    }
}









inline Val TypeCheck::RetAltVal(FnSpec *spec, const RootAlt &alt, vector<Val> &argvals,
                                TypeExpr *t, Node *at) {
    Val m;
    m.type = t;
    
    
    
    m.Set(alt.root, alt.exact && (alt.root || !spec->inprogress), alt.from, alt.slotread);
    m.alts[0].classread = alt.classread;
    m.alts[0].projected = alt.projected;
    m.writable = true;
    if (!alt.root || alt.root->isglobal || alt.root->ownerspec) return m;
    auto v = m;
    auto first = true;
    for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++) {
        auto view = ProjectedFrom(alt.root, ViewClassOf(spec, p));
        if (!ProjectedFrom(alt.root, spec->params[p]->ref.Root()) && !view) continue;
        auto &a = argvals[p];
        auto x = m;
        auto base = view ? ViewClassOf(spec, p) : spec->params[p]->ref.Root();
        auto ar = ProjectClass(alt.root, base,
                               view ? a.held.AsRoots() : ClassArgRoots(spec->argtypes[p], a));
        x.TakeAlts(alt.exact || alt.projected ? ar : Bounds(ar));
        for (auto &xa : x.alts) xa.slotread = alt.slotread && xa.slotread;
        if (alt.classread)
            for (size_t k = 0; k < x.alts.size(); k++)
                x.alts[k].classread = ar.alts[k].exact && IsClassRoot(ar.alts[k].root);
        
        
        x.freshview = (view ? a.held.freshview : IsRefOrSlice(spec->argtypes[p]) && a.freshview) &&
                      !alt.slotread;
        x.writable = view ? a.held.writable : a.writable;
        v = first ? x : MergeVals(v, true, x, true, at, true);
        first = false;
        if (!spec->inprogress) break;
    }
    return v;
}

inline Val TypeCheck::CallResult(Call *c, FnSpec *spec, vector<Val> &argvals) {
    c->rettypes = spec->rets;
    lastcallrets.clear();
    
    
    
    
    
    
    
    
    
    auto rec = RecordOf(spec);
    auto unkeyed = false;
    for (size_t p = 0; spec->inprogress && p < spec->params.size() && p < argvals.size(); p++) {
        auto pt = spec->argtypes[p];
        if ((IsRefOrSlice(pt) || HoldsPlainRef(pt)) && spec->roots[p].cls == 0 &&
            ClassArgRoots(pt, argvals[p]).Root())
            unkeyed = true;
    }
    for (size_t i = 0; i < spec->rets.size(); i++) {
        Val v;
        v.type = spec->rets[i];
        auto holder = !IsRefOrSlice(v.type) && HoldsPlainRef(v.type);
        if (IsRefOrSlice(v.type) || holder) {
            RetRoot none;
            auto &ri = rec && i < rec->retroots.size() ? rec->retroots[i] : none;
            auto backedge = spec->inprogress;
            if (backedge && holder && unkeyed) {
                
                
                
                
                auto t = ast.NewVarDef();
                t->name = "<recursive result>";
                t->depth = CurDepth();
                t->ownerspec = CurRealFrame().spec;
                t->growshrink = true;
                v.Set(t, false);
            } else if (holder && !backedge && ri.heldparam >= 0 &&
                       ri.heldparam < (int)argvals.size()) {
                
                
                v.TakeAlts(ContentsOf(argvals[(size_t)ri.heldparam]));
                v.writable = ri.writable;
            } else {
                
                auto first = true;
                for (auto &alt : ri.alts.alts) {
                    auto m = RetAltVal(spec, alt, argvals, v.type, c);
                    v = first ? m : MergeVals(v, true, m, true, c, true);
                    first = false;
                }
                if (first) {
                    
                    
                    
                    if (!rec || ri.alts.unknown) v.SetUnknown();
                    v.writable = true;
                }
                v.writable = v.writable && ri.writable;
            }
            v.writable = v.writable && !v.type->cq;
            
            
            auto u8view = false;
            if (backedge) {
                vector<TypeExpr *> ps;
                if (holder) RefPointees(v.type, ps); else ps.push_back(PointeeOf(v.type));
                for (auto pt : ps) u8view |= pt && IsU8(pt);
            }
            v.byteview = v.byteview || ri.byteview || u8view;
            
            v.freshview = !holder && (v.freshview || ri.freshview || u8view);
            
            
            
            
            if (backedge) v.ClearReads();
            if (holder) {
                
                
                v.contents = v;
                v.holderset = true;
                v.Set(TempRoot(), false);
                v.writable = false;
            }
        } else {
            v.Set(TempRoot(), false);
            v.writable = false;
        }
        lastcallrets.push_back(v);
    }
    if (spec->rets.empty()) return VoidVal();
    return lastcallrets[0];
}











inline void TypeCheck::ReturnThroughCalls(Val &v, int tf, Return *r) {
    auto isrs = IsRefOrSlice(v.type);
    if (!isrs && !HoldsPlainRef(v.type)) return;
    Val mapped;
    mapped.type = v.type;
    mapped.TakeAlts(isrs ? v.AsRoots() : ContentsOf(v));
    mapped.writable = true;
    for (auto k = (int)frames.size() - 1; k > tf; k--) {
        auto s = frames[k].spec;
        if (frames[k].isfunval || !s || s == frames[tf].spec) continue;
        s->storesout = true;
        auto classof = [&](const RootAlt &a) {
            return a.root && find(s->classroots.begin(), s->classroots.end(), a.root) !=
                                 s->classroots.end();
        };
        for (auto &a : mapped.alts)
            if (a.root && a.root->ownerspec == s)
                Error(r, cat("returning a reference rooted in ", a.root->name,
                             ", which dies with the call of ", s->sf->name,
                             " that this return leaves (§7.9)"));
        if (!mapped.Any(classof)) continue;
        Val out;
        auto first = true;
        for (auto &a : mapped.alts) {
            Val m;
            if (classof(a) && frames[k].argvals) {
                m = RetAltVal(s, a, *frames[k].argvals, mapped.type, r);
            } else {
                m.type = mapped.type;
                m.alts.push_back(a);
                m.writable = true;
            }
            out = first ? m : MergeVals(out, true, m, true, r, true);
            first = false;
        }
        if (auto left = find_if(out.alts.begin(), out.alts.end(), classof); left != out.alts.end())
            Error(r, cat("returning a reference rooted at parameter ", left->root->name, " of ",
                         s->sf->name, ", whose call this return leaves (§7.9)"));
        out.unknown = out.unknown || mapped.unknown;
        out.writable = out.writable && mapped.writable;
        out.byteview = out.byteview || mapped.byteview;
        out.freshview = out.freshview || mapped.freshview;
        mapped = out;
    }
    v.byteview = v.byteview || mapped.byteview;
    if (!isrs) {
        v.contents = mapped.AsRoots();
        v.holderset = true;
        return;
    }
    v.TakeAlts(mapped);
    v.writable = v.writable && mapped.writable;
    v.freshview = v.freshview || mapped.freshview;
}

inline void TypeCheck::CheckReturn(Return *r) {
    if (frames.back().isdefault) Error(r, "return outside of a function");
    
    
    
    auto tf = -1;
    if (!r->from.empty()) {
        
        
        
        
        
        FnSpec *env = nullptr;
        auto targets = LookupLocalFns(r->from, env);
        if (targets.empty()) targets = ast.LookupFunctions(r->from, r->ns);
        if (targets.empty()) Error(r, cat("return from ", r->from, ": unknown function"));
        for (auto i = (int)frames.size() - 1; i >= 1 && tf < 0; i--) {
            if (frames[i].isfunval || !frames[i].sf) continue;
            for (auto t : targets) if (frames[i].sf == t) tf = i;
        }
        if (tf < 0)
            Error(r, cat("return from ", r->from, ": no enclosing call of ", r->from,
                         " on this compile-time call path"));
    } else if (frames.back().isfunval) {
        
        auto named = NamedSpec(frames.back().lexspec);
        if (!named) Error(r, "return outside of a function");
        tf = FrameOfSpec(named);
        if (tf < 0) Error(r, "cannot resolve the enclosing function of this value");
    } else {
        tf = (int)frames.size() - 1;
        if (!frames[tf].sf) Error(r, "return outside of a function");
    }
    
    
    if (deferbodies && InDeferBody()) Error(r, "return cannot leave a defer body (§6.6)");
    auto tspec = frames[tf].spec;
    r->target = frames[tf].sf;
    r->targetspec = tspec;
    for (auto i = tf + 1; i < (int)frames.size(); i++) AddNeed(frames[i].spec, tspec);
    
    vector<Val> vals;
    auto expectone = [&](size_t i) -> TypeExpr * {
        return tspec->retsknown && i < tspec->rets.size() ? tspec->rets[i] : nullptr;
    };
    SlotScope ss(*this, false);   
    {
        FlagScope rs(inreturn, true);
        if (r->vals.size() == 1) {
            auto one = tspec->retsknown && tspec->rets.size() == 1 ? tspec->rets[0] : nullptr;
            Val v;
            {
                RetValScope rv(*this, tf == (int)frames.size() - 1 ? r->vals[0] : nullptr);
                DestScope rd(*this, ResultDest(tspec));
                Saved<Node *> mn(multidestnode, r->vals[0]);
                vector<Dest> dests;
                for (size_t i = 0; i < max<size_t>(1, tspec->rets.size()); i++)
                    dests.push_back(ResultDest(tspec, (int)i));
                Saved<vector<Dest>> md(multidests, std::move(dests));
                v = tspec->retsknown ? CheckValue(r->vals[0], one)
                                     : CheckInferredResult(r->vals[0], tspec);
            }
            if (auto call = Is<Call>(r->vals[0]); call && call->rettypes.size() > 1) {
                vals = lastcallrets;  
                
                
                if (tspec->retsknown && vals.size() == tspec->rets.size()) {
                    for (size_t i = 0; i < vals.size(); i++) {
                        auto rt = tspec->rets[i];
                        RequireCopyable(vals[i], r->vals[0], rt);
                        
                        
                        if (!KeepsRef(vals[i], rt)) {
                            vals[i] = DecayRef(vals[i]);
                            NoRelRefCopy(r->vals[0], rt, i);
                        }
                        MustFit(vals[i], r->vals[0], rt);
                    }
                }
            } else {
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                vals.push_back(v);
            }
        } else {
            for (size_t i = 0; i < r->vals.size(); i++) {
                DestScope rd(*this, ResultDest(tspec, (int)i));
                auto v = tspec->retsknown ? CheckValue(r->vals[i], expectone(i))
                                          : CheckInferredResult(r->vals[i], tspec);
                if (v.type->kind == TY_VOID) Error(r, "cannot return a valueless expression");
                vals.push_back(v);
            }
        }
    }
    if (vals.empty() && tspec->retsknown && !tspec->rets.empty())
        Error(r, cat("function ", frames[tf].sf->name, " must return value(s)"));
    if (!vals.empty() || !tspec->retsknown) {
        
        
        
        
        
        if (tf != (int)frames.size() - 1) {
            for (auto &v : vals) {
                if (frames.back().isfunval) {
                    ReturnThroughCalls(v, tf, r);
                    continue;
                }
                auto isrs = IsRefOrSlice(v.type);
                if (!isrs && !HoldsPlainRef(v.type)) continue;
                const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
                if (roots.Any([](const RootAlt &a) { return a.root && !a.root->isglobal; }))
                    Error(r, "a long-distance return may only carry references to "
                             "globals or static data");
            }
        }
        RecordReturn(tspec, vals, r);
    }
    NoteExit(tf);
    reachable = false;
}






inline FnSpec *TypeCheck::EnsureThreadSpec(SFunction *sf, Line l) {
    if (sf->brokensig) throw Abandoned {};
    
    
    if (!sf->specs.empty()) {
        
        
        if (sf->specs[0]->failed) throw Abandoned {};
        return sf->specs[0];
    }
    if (!sf->generics.empty()) Error(l, cat("thread_fn ", sf->name, " cannot be generic"));
    if (sf->has_rets) Error(l, cat("thread_fn ", sf->name, " cannot return values"));
    auto spec = ast.NewFnSpec();
    spec->sf = sf;
    for (auto &p : sf->params) {
        if (!p.type)
            Error(l, cat("thread_fn ", sf->name, " needs fully typed parameters"));
        auto t = Subst(p.type);
        ValidateType(t, sf->line, VT_PARAM);
        if (!IsFlat(t))
            Error(l, cat("thread_fn parameters must be flat (§11.2), not ", TypeStr(t)));
        spec->argtypes.push_back(t);
    }
    spec->roots.resize(spec->argtypes.size());
    RootArg noview;
    noview.cls = -1;
    spec->views.resize(spec->argtypes.size(), noview);
    sf->specs.push_back(spec);
    CheckSpecBody(spec, nullptr, l);
    return spec;
}





inline Val TypeCheck::CheckFunValCall(Call *c, const FnValBind &fb, TypeExpr *expected) {
    if (c->trailing)
        Error(c, "a function value call cannot itself take a trailing block");
    if (fb.named) {
        vector<SFunction *> cands = fb.set ? *fb.set : vector<SFunction *> { fb.named };
        Node *nopre = nullptr;
        return ResolveCall(c, cands, fb.env, fb.named->name, nullptr, nopre, reachable,
                           nullptr, expected);
    }
    auto fv = fb.fv;
    auto entryreach = reachable;
    
    
    cur.kills.clear();
    if (!c->tyargs.empty()) Error(c, "a block takes no type arguments");
    vector<Val> argvals;
    argvals.reserve(c->args.size());
    for (auto a : c->args) {
        PathScope ps(*this, a);
        auto v = CheckV(a, nullptr);
        a->exprtype = v.type;
        argvals.push_back(std::move(v));
    }
    vector<Param> params;
    if (fv->explicit_params) {
        params = fv->params;
        if (params.size() != argvals.size())
            Error(c, cat("this function value takes ", (int64_t)params.size(),
                         " argument(s), ", (int64_t)argvals.size(), " given"));
    } else if (argvals.size() == 1) {
        Param p;
        p.name = "it";
        params.push_back(p);
    } else if (!argvals.empty()) {
        Error(c, "a block with multiple arguments needs named parameters (x, y => ...)");
    }
    
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type) continue;
        RefArg(c->args[i], argvals[i]);
        c->args[i]->exprtype = argvals[i].type;
    }
    
    vector<TypeExpr *> ptypes;
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type) {
            auto t = SubstEnv(params[i].type, fb.env);
            ValidateType(t, c->line, VT_PARAM);
            ptypes.push_back(t);
        } else {
            auto nt = NaturalType(argvals[i]);
            if (!nt || nt->kind == TY_VOID || nt == fntype)
                Error(c->args[i], "cannot infer a type for this argument");
            ptypes.push_back(nt);
        }
        if (!params[i].type || params[i].type->kind != TY_SLICE) NoArrayJoin(argvals[i]);
    }
    RefSliceArgs(argvals, ptypes, c->line);
    auto argsreach = reachable;
    reachable = entryreach;
    {
        DestScope ds(*this, Dest {});
        for (size_t i = 0; i < ptypes.size(); i++) {
            auto byref = argvals[i].storagebranches && ptypes[i]->kind == TY_REF;
            if (!byref) RefCopyWarnings(argvals[i]);
            auto viewed = ViewedWhole(c->args[i], argvals[i], ptypes[i]);
            auto v = CheckArg(c->args[i], ptypes[i]);
            
            
            
            if (byref || viewed) argvals[i] = v;
        }
    }
    
    
    
    reachable = reachable && argsreach;
    auto named = NamedSpec(fb.env);
    auto env = ast.NewFunValEnv();
    env->sf = named ? named->sf : nullptr;
    env->lexparent = fb.env;
    
    
    
    auto writer = named ? named->sf : CurRealFrame().sf;
    if (auto t = TextFn(); t && t != writer && t->checkfree && !(writer && writer->checkfree))
        Error(c, cat("checkfree fn ", t->qname, " calls a block written in ",
                     writer ? cat("fn ", writer->qname) : string("a global initializer"),
                     ", which is not checkfree: the bounds checks left in it would run "
                     "inside ", t->qname, " (§7.14)"));
    Frame f;
    f.sf = writer;
    f.spec = CurRealFrame().spec;
    f.lexspec = env;
    f.lexframe = fb.env ? LexFrame(fb.env) : 0;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    f.callline = c->line;
    f.expansiontrace = c->expansiontrace;
    f.isfunval = true;
    f.bodyidx = (int)outerbodies.size();
    frames.push_back(f);
    PushScope(SK_FN);
    c->fvparams.clear();
    for (size_t i = 0; i < params.size(); i++) {
        auto vd = NewVar(params[i].name, ptypes[i], c->line, params[i].isvar);
        vd->assigned = vd->maybeassigned = true;
        if (IsRefOrSlice(ptypes[i])) {
            BindRefProvenance(vd, argvals[i]);
            if (ptypes[i]->cq) vd->ref.writable = false;
        } else if (HoldsPlainRef(ptypes[i])) {
            NoteHolderBinding(vd, argvals[i], c->args[i]);
        }
        
        if (argvals[i].unsized && !params[i].type && !params[i].isvar) {
            vd->unsized = true;
            vd->unsizedorigin = argvals[i].unsizedparam;
        }
        c->fvparams.push_back(vd);
    }
    c->fvbody = (Block *)fv->body->Clone(ast);
    BlockScope bs(*this, c->fvbody);
    CheckStmts(c->fvbody);
    Val v = VoidVal();
    auto resultdest = callresultdsts.empty() ? Dest {} : callresultdsts[0];
    bool needslocation = false;
    if (auto tail = c->fvbody->tail) {
        if (IsValuelessTail(tail)) {
            CheckStmtExpr(tail);
        } else {
            frames.back().tailvalue = true;
            auto dest = resultdest;
            dest.used = &needslocation;
            DestScope rd(*this, dest);
            v = CheckValue(c->fvbody->tail, nullptr);
            NoRelRefCopy(c->fvbody->tail, v.type);
        }
    }
    
    
    if (IsUntypedEmptyArray(v.type))
        Error(c->fvbody->tail, "cannot infer the element type of []: a block's value is checked "
                               "without its call's destination type (§7.6); end the block with an "
                               "annotated local instead, as in `let e: T[] = []; e`");
    c->fvbody->exprtype = v.type;
    if (needslocation && !ResultLayoutFits(v.type, expected))
        Error(c, "a block result containing relative links to its destination cannot change "
                 "representation; construct the receiver's type in the block");
    if (needslocation && resultdest.fill)
        Error(c, "a block result containing relative links to its destination cannot be a "
                 "resize fill value, which is constructed once and copied");
    if (needslocation && resultdest.used) *resultdest.used = true;
    PopScope();
    frames.pop_back();
    cur.kills.clear();
    
    
    
    return TempCopy(v);
}




inline bool TypeCheck::ResultLayoutFits(TypeExpr *from, TypeExpr *to) {
    if (!to || to->kind == TY_VOID || TopConstEq(from, to)) return true;
    return from->kind == TY_VARIANT && to->kind == TY_ENUM && SameAdt(AdtOf(from), to);
}






inline bool TypeCheck::RelativeDestination(const Roots &source, Dest dest, Node *at, bool record,
                                          bool forwarding) {
    if (source.None() || dest.unknown) return true;
    if (!source.Exact() || (!dest.result && !dest.roots.Exact())) return false;
    auto roots = source;
    auto spec = CurRealFrame().spec;
    if (dest.result && dest.result != spec) {
        auto target = FrameOfSpec(dest.result);
        if (target < 0) return false;
        for (auto k = RealFrameIndex(); k > target; k--) {
            auto s = frames[k].spec;
            if (frames[k].isfunval || !s || s == dest.result) continue;
            if (record) s->storesout = true;
            for (auto &a : roots.alts)
                if (IsTemp(a.root) || (a.root && a.root->ownerspec == s)) return false;
            if (!frames[k].argvals) continue;
            Roots mapped;
            for (auto &a : roots.alts)
                mapped.Add(RetAltVal(s, a, *frames[k].argvals, ast.voidtype, at).AsRoots());
            roots = mapped;
        }
        spec = dest.result;
        if (!roots.Exact()) return false;
    }
    auto have = roots.Root(), want = dest.roots.Root();
    auto same = !dest.result && have && want &&
                (have == want || (PoolOf(have) && PoolOf(have) == PoolOf(want)));
    
    
    if (same) {
        if (record && dest.used) *dest.used = true;
        return true;
    }
    if (!dest.result && !dest.used && !forwarding) return false;
    auto local = [&](VarDef *r) {
        return IsTemp(r) || (r && r->ownerspec == spec && !r->isglobal);
    };
    if (!spec || !have || local(have) || (!dest.result && local(want))) return false;
    if (!dest.result && !IsClassRoot(have) && !IsClassRoot(want)) return false;
    if (!record) return true;
    if (dest.used) *dest.used = true;
    FnRecord::RelativeStore rs;
    rs.source.Set(have, true);
    if (dest.result) rs.result = dest.resultindex;
    else rs.destination.Set(want, true);
    auto &stores = spec->record.relativestores;
    if (none_of(stores.begin(), stores.end(), [&](const auto &s) {
            return s.result == rs.result && s.source == rs.source && s.destination == rs.destination;
        }))
        stores.push_back(rs);
    return true;
}

inline void TypeCheck::CheckCalleeRelative(Call *call, FnSpec *spec, vector<Val> &args) {
    if (!reachable) return;
    auto rec = RecordOf(spec);
    if (!rec) return;
    
    auto stores = rec->relativestores;
    auto mapped = [&](const Roots &roots) {
        Roots out;
        for (auto &a : roots.alts)
            out.Add(RetAltVal(spec, a, args, ast.voidtype, call).AsRoots());
        return out;
    };
    for (auto &s : stores) {
        auto source = mapped(s.source);
        Dest dest;
        if (s.result < 0) dest = Dest(mapped(s.destination));
        else {
            auto i = (size_t)s.result;
            if (i < callresultdsts.size()) dest = callresultdsts[i];
            else if (callforwarding && !callresultdsts.empty() && callresultdsts[0].result) {
                dest = callresultdsts[0];
                dest.resultindex += s.result;
            }
            if (dest.fill)
                Error(call, "a result containing relative links to its destination cannot be a "
                            "resize fill value, which is constructed once and copied");
            auto want = i == 0 ? callresulttype : nullptr;
            if (!want && dest.result && (size_t)dest.resultindex < dest.result->rets.size())
                want = dest.result->rets[dest.resultindex];
            if (i < spec->rets.size() && !ResultLayoutFits(spec->rets[i], want))
                Error(call, "a result containing relative links to its destination cannot change "
                            "representation; return the receiver's type");
        }
        if (!RelativeDestination(source, dest, call, true, true)) {
            auto target = source.Root() ? string(source.Root()->name) : string("static data");
            auto receiver = dest.result ? cat("result ", dest.resultindex + 1, " of ", dest.result->sf->name)
                : dest.roots.Root() ? string(dest.roots.Root()->name) : string("a temporary or argument slot");
            Error(call, cat("a relative reference must point within the same root as its location "
                            "(§3.9); call of ", spec->sf->name, " links a target rooted at ",
                            target, " to receiving storage ", receiver));
        }
    }
}

inline TypeExpr *TypeCheck::SubstEnv(TypeExpr *t, FnSpec *env) {
    Frame f;
    f.lexspec = env;
    f.lexframe = -1;
    f.scopebase = (int)scopes.size();
    f.varbase = (int)vars.size();
    frames.push_back(f);
    auto r = Subst(t);
    frames.pop_back();
    return r;
}

}  
