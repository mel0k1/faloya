



#pragma once

namespace faloya {










inline const VarDef *CodeGen::OpenIbNrvo(InlineBlock *ib, const Dst &d) {
    if (d.k != DK_STACK || !ib->spec || ib->spec->rets.size() != 1) return nullptr;
    auto vd = NamedResult(ib->body, ib->sf, 1, 0);
    if (!vd || !IsBytesT(vd->type) || nrvo.count(vd)) return nullptr;
    auto ct = vd->type;
    NrvoDest nd;
    nd.stk = d.s;
    nd.inlined = true;
    if (!IsResz(ct)) {
        
        
        if (!d.lenlv.empty() || !d.t || !TEq(ct, d.t)) return nullptr;
        nrvo[vd] = nd;
        return vd;
    }
    if (!d.lenlv.empty()) {
        
        if (!d.t) return nullptr;
        auto elems = ct->kind == TY_ARRAY && d.t->kind == TY_ARRAY &&
                     (IsResz(d.t) || d.t->arr->akind == A_VAR) &&
                     TEq(ct->arr->sub, d.t->arr->sub);
        if (!TEq(ct, d.t) && !elems) return nullptr;
        nd.lenlv = d.lenlv;
        nd.fo = IsFrameObj(ct);
    } else {
        
        
        
        
        auto at = ib->spec->rets[0];
        if (!at || at->kind != TY_ARRAY || at->arr->akind != A_VAR) return nullptr;
        if (d.t) {
            if (d.t->kind != TY_ARRAY || d.t->arr->akind != A_VAR ||
                !TEq(d.t->arr->sub, at->arr->sub)) return nullptr;
            at = d.t;
        }
        if (ct->kind != TY_ARRAY || !TEq(ct->arr->sub, at->arr->sub)) return nullptr;
        nd.prefix = true;
        nd.ls = LenStore(at->arr);
    }
    nrvo[vd] = nd;
    return vd;
}









inline const VarDef *CodeGen::AliasTarget(VarDef *d, Node *init, Node *&path) {
    path = nullptr;
    auto cache_ref = [&](TypeExpr *t) {
        return IsFatRef(t) || (t->kind == TY_REF && IsRoot(t->ref->sub));
    };
    if (!d || !init || !curspec || d->isvar || d->isglobal || d->reusable || !d->type ||
        !cache_ref(d->type) || d->type->ref->optional || PrefVar(d) || d->captured ||
        capturedvars.count(d))
        return nullptr;
    
    
    auto fixedref = [&](const VarDef *t) -> const VarDef * {
        if (!t || !t->type || !cache_ref(t->type)) return nullptr;
        if (refalias.count(t)) return t;
        if (t->isvar || t->isglobal || t->reusable || PrefVar(t)) return nullptr;
        for (auto p : curspec->params) if (p == t) return t;
        for (auto fv : curinfo->freevars) if (fv == t) return t;
        return nullptr;
    };
    if (auto u = Is<Unary>(init); u && u->op == T_BITAND) {
        if (!u->child->exprtype || !TEq(d->type->ref->sub, u->child->exprtype)) return nullptr;
        if (auto id = Is<Ident>(u->child)) {
            auto t = id->vdef;
            if (!t || !t->type || t->reusable || (!IsResz(t->type) && !IsRoot(t->type)))
                return nullptr;
            return t;
        }
        
        
        
        
        auto n = u->child;
        Dot *component = nullptr;
        while (auto dot = Is<Dot>(n)) {
            if (!dot->IsField() || !dot->obj->exprtype) return nullptr;
            component = dot;
            n = dot->obj;
            if (!Is<Ident>(n) && n->exprtype->kind == TY_REF) return nullptr;
        }
        auto id = Is<Ident>(n);
        if (n == u->child || !id || !id->vdef || !id->vdef->type) return nullptr;
        auto t = id->vdef;
        if (component && IsRoot(t->type)) t = t->components[component->fieldidx];
        if (IsResz(t->type) ? t->reusable : !fixedref(t)) return nullptr;
        path = u->child;
        return t;
    }
    auto id = Is<Ident>(init);
    auto t = id ? id->vdef : nullptr;
    if (!t || !t->type || !init->exprtype || !TEq(init->exprtype, d->type)) return nullptr;
    
    if ((IsResz(t->type) || IsRoot(t->type)) && !t->reusable && TEq(d->type->ref->sub, t->type))
        return t;
    if (!TEq(t->type, d->type)) return nullptr;
    if (auto it = refalias.find(t); it != refalias.end()) {
        if (auto pit = aliaspath.find(t); pit != aliaspath.end()) path = pit->second;
        return it->second;
    }
    return fixedref(t);
}





inline bool CodeGen::HandedRef(Node *n) {
    auto un = Is<Unary>(n);
    return un && un->op == T_BITAND && Is<Ident>(FieldChainRoot(un->child, false));
}






inline Node *CodeGen::FieldChainRoot(Node *n, bool typed) {
    while (auto d = Is<Dot>(n)) {
        if (!d->IsField() || (typed && !d->obj->exprtype)) return nullptr;
        n = d->obj;
        if (!Is<Ident>(n) && n->exprtype && n->exprtype->kind == TY_REF) return nullptr;
    }
    return n;
}



inline void CodeGen::FindRefAliases(FnSpec *sp) {
    for (auto p : sp->params)
        if (auto it = knownrootparams.find(p); it != knownrootparams.end()) refalias[p] = it->second;
    auto bind = [&](VarDef *d, Node *init) {
        Node *path = nullptr;
        auto t = AliasTarget(d, init, path);
        if (!t) return;
        refalias[d] = t;
        if (path) aliaspath[d] = path;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto vd = Is<VarDecl>(n); vd && vd->defs.size() == vd->inits.size()) {
            for (size_t i = 0; i < vd->defs.size(); i++) bind(vd->defs[i], vd->inits[i]);
        }
        if (auto c = Is<Call>(n); c && c->fvbody) {
            for (size_t i = 0; i < c->fvparams.size() && i < c->args.size(); i++)
                bind(c->fvparams[i], c->args[i]);
        }
        RunChildren(n, walk);
    };
    walk(sp->body);
}



























inline void CodeGen::PlanTopClasses(FnSpec *sp) {
    topown = topglob = topcap = topdst = reftops = false;
    auto &fvs = sinfo[sp].freevars;
    auto isparam = [&](const VarDef *v) {
        return std::find(sp->params.begin(), sp->params.end(), v) != sp->params.end();
    };
    auto isfv = [&](const VarDef *v) { return std::find(fvs.begin(), fvs.end(), v) != fvs.end(); };
    
    auto carries = [&](const VarDef *v) {
        return v && (PrefVar(v) || (v->type && v->type->kind == TY_REF &&
                                   (IsResz(v->type->ref->sub) || IsRoot(v->type->ref->sub))));
    };
    auto fatparam = false, fatfv = false, capresz = false, globals = false, unknown = false;
    for (size_t i = 0; i < sp->params.size(); i++) {
        if (knownrootparams.count(sp->params[i])) globals = true;
        else if (carries(sp->params[i]) || IsPoolParam(sp, i)) fatparam = true;
    }
    for (auto fv : fvs) {
        if (fv->reusable || carries(fv)) fatfv = true;
        else if (fv->type && (IsResz(fv->type) || IsRoot(fv->type))) capresz = true;
    }
    
    
    set<SFunction *> localexits { sp->sf };
    auto farreturn = false;
    function<void(Node *)> ibs = [&](Node *n) {
        if (!n) return;
        if (auto ib = Is<InlineBlock>(n)) localexits.insert(ib->sf);
        RunChildren(n, ibs);
    };
    ibs(sp->body);
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (n->exprtype && (IsFatRef(n->exprtype) ||
            (n->exprtype->kind == TY_REF && IsRoot(n->exprtype->ref->sub)))) {
            if (!Is<Ident>(n) && !HandedRef(n)) unknown = true;
        }
        
        
        if (auto d = Is<Dot>(n); d && d->obj->exprtype &&
            d->obj->exprtype->kind == TY_REF && IsRoot(d->obj->exprtype->ref->sub) &&
            !Is<Ident>(d->obj)) unknown = true;
        if (auto r = Is<Return>(n); r && !localexits.count(r->target)) farreturn = true;
        NodeVars(n, [&](const VarDef *v) {
            if (!v) return;
            if (carries(v) && !refalias.count(v) && !isparam(v) && !isfv(v)) unknown = true;
            if (v->isglobal && v->type && (IsBytesT(v->type) || HoldsFatRef(v->type)))
                globals = true;
        });
        RunChildren(n, walk);
    };
    walk(sp->body);
    auto nonfixedret = false;
    for (auto rt : sp->rets) nonfixedret |= IsBytesT(rt);
    topown = !unknown;
    topglob = topown && !fatparam && !fatfv;
    topcap = topglob && capresz && !farreturn;
    topdst = topglob && !capresz && !globals && nonfixedret && !farreturn && !fromids.count(sp);
    reftops = !topglob && RefTopsOk(sp, farreturn);
    cachetops = topown || reftops;
}
























inline bool CodeGen::RefTopsOk(FnSpec *sp, bool farreturn) {
    auto rootref = [&](TypeExpr *t) { return t->kind == TY_REF && IsRoot(t->ref->sub); };
    auto hasroots = false, hasfat = false;
    for (auto pt : sp->argtypes) {
        hasroots |= rootref(pt);
        hasfat |= IsFatRef(pt);
    }
    
    
    if (hasroots && hasfat) return false;
    for (auto fv : sinfo[sp].freevars)
        if (fv->type && (IsRoot(fv->type) ||
            (fv->type->kind == TY_REF && IsRoot(fv->type->ref->sub)))) return false;
    set<const VarDef *> fatparams, classes;
    vector<TypeExpr *> pointees;
    auto concrete = true;
    for (size_t i = 0; i < sp->params.size(); i++) {
        auto vd = sp->params[i];
        auto pt = sp->argtypes[i];
        if (!IsFatRef(pt) && !rootref(pt)) continue;
        if (rootref(pt) && vd->isvar) return false;
        if (!vd->refrootknown || !vd->ref.Root() || !vd->ref.Exact() ||
            !classes.insert(vd->ref.Root()).second)
            return false;
        fatparams.insert(vd);
        if (rootref(pt)) {
            for (auto ft : SI(pt->ref->sub)->ftypes)
                if (ft && IsResz(ft)) pointees.push_back(ft);
        } else pointees.push_back(pt->ref->sub);
        concrete &= i < sp->roots.size() && sp->roots[i].concrete;
    }
    if (fatparams.empty() || (fatparams.size() > 1 && !concrete)) return false;
    for (auto pt : sp->argtypes) if (IsResz(pt)) return false;
    for (auto rt : sp->rets) if (IsBytesT(rt)) return false;
    if (fromids.count(sp) || farreturn) return false;
    for (auto fv : sinfo[sp].freevars)
        if (fv->reusable || (fv->type && (IsResz(fv->type) || IsFatRef(fv->type))))
            return false;
    auto ok = true;
    
    
    
    auto maybeparam = [&](TypeExpr *gt) {
        for (;;) {
            for (auto pt : pointees) if (TEq(pt, gt)) return true;
            if (!IsFrameObj(gt)) return false;
            auto si = SI(gt);
            gt = si->ftypes[LastRealField(si->st->fields)];
        }
    };
    
    
    
    auto check = [&](const VarDef *v) {
        if (!v || !v->type) return;
        if ((IsFatRef(v->type) || rootref(v->type)) &&
            !fatparams.count(v) && !refalias.count(v)) ok = false;
        if (v->isglobal && IsBytesT(v->type) && maybeparam(v->type)) ok = false;
        if (v->isglobal && IsRoot(v->type))
            for (auto c : v->components)
                if (c && IsBytesT(c->type) && maybeparam(c->type)) ok = false;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !ok) return;
        if (auto d = Is<Dot>(n); d && d->obj->exprtype &&
            rootref(d->obj->exprtype) && !Is<Ident>(d->obj)) {
            ok = false;
            return;
        }
        if (n->exprtype && (IsFatRef(n->exprtype) || rootref(n->exprtype))) {
            
            
            auto id = Is<Ident>(n);
            if (!HandedRef(n) && (!id || !(fatparams.count(id->vdef) || refalias.count(id->vdef)))) {
                ok = false;
                return;
            }
        }
        NodeVars(n, check);
        
        
        
        if (auto c = Is<AsCast>(n); c && c->totype && c->totype->ix) check(c->totype->ix->arr);
        RunChildren(n, walk);
    };
    walk(sp->body);
    return ok;
}

inline void CodeGen::ResetFnState() {
    toporder.clear();
    lenslots.clear();
    growth.clear();
    loopparent.clear();
    loopcons.clear();
    loopstack.clear();
    markers = false;
    refstkexprs.clear();
    capstkexprs.clear();
    cachetops = false;
    reftops = false;
    topown = topglob = topcap = topdst = false;
    vnames.clear();
    refalias.clear();
    aliaspath.clear();
    aliasbound.clear();
    views.clear();
    fieldviews.clear();
    vstk.clear();
    vpool.clear();
    poolbases.clear();
    fvptr.clear();
    fnused.clear();
    fnnextsuffix.clear();
    nrvo.clear();
    fdstsaves.clear();
    openat.clear();
    dsttop0.clear();
    cscopes.clear();
    body.clear();
    tmpn = 0;
    stknext = 0;
    stkmax = 0;
    ind = 1;
    termjump = false;
}




inline string CodeGen::EnsureEr(FnSpec *sp) {
    auto it = ernames.find(sp);
    if (it != ernames.end()) return it->second;
    auto rt = sp->rets.size() == 1 ? sp->rets[0] : nullptr;
    auto ok = rt && rt->kind == TY_ARRAY && rt->arr->akind == A_VAR && sp->body &&
              !fromids.count(sp);
    if (!ok) return ernames[sp] = "";
    auto name = Unique(cat(sinfo[sp].cname, "_er"));
    ernames[sp] = name;
    auto ret = SigRet(sp), params = SigParams(sp, false, true);
    Append(protos, "static ", FnAttrs(sp), ret, " ", name, "(", params, ");\n");
    erqueue.push_back(sp);
    return name;
}



inline vector<string> CodeGen::BraceFields(const string &ct) {
    if (ct == "gs_rref") return { "hdr", "stk" };
    if (ct == "gs_rhdr") return { "base", "len" };
    if (slicects.count(ct)) return { "data", "len" };
    return {};
}



inline vector<string> CodeGen::SplitBraceInit(const string &init) {
    vector<string> parts;
    auto trim = [](string s) {
        auto b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
        return b == string::npos ? string() : s.substr(b, e - b + 1);
    };
    if (init.size() < 2 || init.front() != '{' || init.back() != '}') return parts;
    auto body = init.substr(1, init.size() - 2);
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < body.size(); i++) {
        auto c = body[i];
        if (c == '"' || c == '\'') {
            for (i++; i < body.size() && body[i] != c; i++)
                if (body[i] == '\\') i++;
        } else if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            depth--;
        } else if (c == ',' && !depth) {
            parts.push_back(trim(body.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(body.substr(start)));
    for (auto &p : parts)
        if (p.empty()) return {};
    return parts;
}











inline string CodeGen::HoistAggregateDecls(string &b) {
    static const unordered_set<string> keep = {
        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
        "uint64_t", "float", "double", "int", "char", "size_t", "void", "return", "goto",
        "break", "continue", "else", "do", "case", "default", "static", "const", "extern",
        "volatile", "register", "unsigned", "signed", "long", "short", "struct", "union",
        "enum", "typedef", "if", "for", "while", "switch",
    };
    auto isid = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    string decls, out;
    out.reserve(b.size());
    size_t pos = 0;
    while (pos < b.size()) {
        auto nl = b.find('\n', pos);
        if (nl == string::npos) nl = b.size();
        string_view line(b.data() + pos, nl - pos);
        pos = nl + 1;
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') i++;
        auto indent = i;
        auto ts = i;
        while (i < line.size() && isid(line[i])) i++;
        string type(line.substr(ts, i - ts));
        bool hoist = false;
        string name, init;
        if (!type.empty() && !isdigit((unsigned char)type[0]) && !keep.count(type) &&
            i < line.size() && line[i] == ' ') {
            i++;
            auto ns = i;
            while (i < line.size() && isid(line[i])) i++;
            name = string(line.substr(ns, i - ns));
            if (!name.empty() && !isdigit((unsigned char)name[0])) {
                auto rest = line.substr(i);
                if (rest == ";") hoist = true;
                else if (rest.size() > 4 && rest.substr(0, 3) == " = " && rest.back() == ';') {
                    init = string(rest.substr(3, rest.size() - 4));
                    hoist = true;
                }
            }
        }
        if (!hoist) {
            out.append(line);
            out += '\n';
            continue;
        }
        Append(decls, "    ", type, " ", name, ";\n");
        if (!init.empty()) {
            out.append(indent, ' ');
            if (init[0] != '{') {
                Append(out, name, " = ", init, ";\n");
                continue;
            }
            
            
            
            
            
            
            auto fs = BraceFields(type);
            auto vals = fs.empty() ? vector<string>() : SplitBraceInit(init);
            if (fs.empty() || vals.size() != fs.size()) {
                Append(out, name, " = (", type, ")", init, ";\n");
                continue;
            }
            for (size_t k = 0; k < fs.size(); k++)
                Append(out, k ? " " : "", name, ".", fs[k], " = ", vals[k], ";");
            out += '\n';
        }
    }
    b = out;
    return decls;
}










inline void CodeGen::EmitSimdVersions(FnSpec *sp, const string &name, const string &params,
                                      const string &fnbody) {
    
    
    string args;
    if (params != "void") {
        for (size_t b = 0; b <= params.size();) {
            auto e = std::min(params.find(',', b), params.size());
            auto nb = e;
            while (nb > b && (isalnum((unsigned char)params[nb - 1]) || params[nb - 1] == '_'))
                nb--;
            assert(nb < e);
            Append(args, args.empty() ? "" : ", ", string_view(params).substr(nb, e - nb));
            b = e + 1;
        }
    }
    auto ret = SigRet(sp);
    vector<string> versions(SIMD_LEVELS + 1);
    
    
    for (int lv = SIMD_LEVELS; lv >= 1; lv--) {
        versions[lv] = Unique(cat(name, "_simd", lv));
        Append(code, "#if GS_SIMD >= ", lv,
               "\n#undef GS_MUL_ADD_MODE\n#define GS_MUL_ADD_MODE fused\nGS_SIMD_TARGET", lv,
               " static ", ret, " ", versions[lv], "(", params, ") {\n", fnbody,
               "}\n#undef GS_MUL_ADD_MODE\n#define GS_MUL_ADD_MODE GS_MUL_ADD_BASE\n#endif\n");
    }
    Append(code, "static ", ret, " ", name, "(", params, ") {\n",
           "#if GS_SIMD >= 1\n    switch (gs_simd_level()) {\n");
    for (int lv = SIMD_LEVELS; lv >= 1; lv--) {
        if (lv > 1) Append(code, "#if GS_SIMD >= ", lv, "\n");
        auto call = cat(versions[lv], "(", args, ")");
        if (ret == "void") Append(code, "    case ", lv, ": ", call, "; return;\n");
        else Append(code, "    case ", lv, ": return ", call, ";\n");
        if (lv > 1) Append(code, "#endif\n");
    }
    Append(code, "    }\n#endif\n", fnbody, "}\n\n");
}

inline void CodeGen::EmitSpec(FnSpec *sp, bool er) {
    curspec = sp;
    curinfo = &sinfo[sp];
    emiter = er;
    ResetFnState();
    spexpr = curinfo->needssp ? "gs_sp" : "0";
    FindRefAliases(sp);
    PlanTopClasses(sp);
    markers = true;
    PushSc(SC_FN);
    auto params = SigParams(sp, true, er);
    
    if (reftops) {
        for (size_t i = 0; i < sp->params.size(); i++) {
            auto pt = sp->argtypes[i];
            if (pt->kind == TY_REF && IsRoot(pt->ref->sub)) {
                auto owner = VarLoc(sp->params[i]);
                DerefLoc(owner);
                auto si = SI(owner.t);
                for (size_t f = 0; f < si->ftypes.size(); f++)
                    if (si->ftypes[f] && IsResz(si->ftypes[f]))
                        refstkexprs.insert(FieldLocAt(owner, (int)f).stk);
                continue;
            }
            if (!IsFatRef(sp->argtypes[i])) continue;
            auto pn = LocalName(sp->params[i]);
            refstkexprs.insert(cat(pn, ".stk"));
            if (IsPoolParam(sp, i)) refstkexprs.insert(cat(pn, ".flstk"));
        }
    }
    if (topcap)
        for (auto fv : curinfo->freevars)
            if (fv->type && IsResz(fv->type) && !fv->reusable) capstkexprs.insert(vstk[fv]);
    
    
    
    if (!er) DetectNrvo(sp);
    if (fromids.count(sp)) {
        
        
        EnsureFromChannels(sp);
        auto tid = fromids[sp];
        for (size_t i = 0; i < sp->rets.size(); i++) {
            if (NeedsResultLocation(sp->rets[i])) {
                auto sav = T(), chan = cat("gs_lret_", tid, "_", i);
                L(CT(sp->rets[i]), " *", sav, " = ", chan, ";");
                L(chan, " = gs_r", i, ";");
                fdstsaves.push_back(cat(chan, " = ", sav, ";"));
                continue;
            }
            if (!IsBytesT(sp->rets[i])) continue;
            auto sav = T();
            L("gs_stack *", sav, " = gs_fdst_", tid, "_", i, ";");
            L("gs_fdst_", tid, "_", i, " = gs_dst", i, ";");
            fdstsaves.push_back(cat("gs_fdst_", tid, "_", i, " = ", sav, ";"));
        }
    }
    for (auto st : sp->body->stmts) GenStmt(st);
    if (auto tail = sp->body->tail) {
        auto asvalue = !sp->rets.empty();
        if (auto fi = Is<IfExpr>(tail); fi && !fi->elseb) asvalue = false;
        if (asvalue && !IsVoidT(tail->exprtype)) {
            vector<Node *> vals = { tail };
            GenNormalReturn(vals);
        } else {
            GenStmt(tail);
        }
    }
    if (!termjump) {
        if (curinfo->cret >= 0) {
            L("GS_UNREACHABLE(", LocArgs(sp->sf->line), ");");
            auto d = T();
            L(CT(sp->rets[curinfo->cret]), " ", d, " = {0};");
            L("return ", d, ";");
        } else {
            Epilogue("");
        }
    }
    cscopes.clear();
    
    if (stkmax > 0 && !curinfo->needssp)
        Fail(sp->sf->line, cat("internal: ", curinfo->cname, " uses data stacks without gs_sp"));
    
    stackown[sp] = std::max(stackown[sp], stkmax);
    
    
    auto plan = PlanTopCaches();
    auto bodyout = ExpandTopMarkers(body, plan);
    assert(bodyout.find("@@gs") == string::npos);
    auto decls = HoistAggregateDecls(bodyout);
    string fnbody = paramcopies + decls;
    
    
    for (size_t i = 0; i < toporder.size(); i++)
        if (!plan.fnlocals[i].empty())
            Append(fnbody, "    uint8_t *", plan.fnlocals[i], " = ", toporder[i], "->top;\n");
    fnbody += plan.fnlens;
    
    
    for (auto &p : poolbases)
        Append(fnbody, "    uint8_t *", p.second, " = ", gnames[p.first], ".base;\n");
    fnbody += bodyout;
    auto name = er ? ernames[sp] : curinfo->cname;
    if (sp->sf->issimd) EmitSimdVersions(sp, name, params, fnbody);
    else Append(code, "static ", FnAttrs(sp), SigRet(sp), " ", name, "(", params, ") {\n", fnbody,
                "}\n\n");
    curspec = nullptr;
    curinfo = nullptr;
    emiter = false;
    markers = false;
}










inline bool CodeGen::StaticInitX(Node *n, TypeExpr *t, string &out) {
    if (!n || !t || !IsFix(t) || HasRelRef(t)) return false;
    if (auto id = Is<Ident>(n)) {
        auto it = id->vdef ? gstatic.find(id->vdef) : gstatic.end();
        
        
        if (it == gstatic.end() ||
            !(TEq(id->vdef->type, t) || (t->kind == TY_INT && id->vdef->type->kind == TY_INT)))
            return false;
        return StaticInitX(it->second, t, out);
    }
    switch (t->kind) {
        case TY_INT: {
            auto i = Is<IntLit>(n);
            if (!i || t->intstorage == IS_VARINT) return false;
            out = i->val < 0 && t->intstorage == IS_U64 ? cat((uint64_t)i->val, "ULL")
                                                        : IntStr(i->val);
            return true;
        }
        case TY_FLT: {
            auto f = Is<FltLit>(n);
            if (!f) return false;
            out = FltStr(f->val, t->fltstorage == FS_F32);
            return true;
        }
        case TY_BOOL: {
            auto b = Is<BoolLit>(n);
            if (!b) return false;
            out = b->val ? "1" : "0";
            return true;
        }
        case TY_STRUCT: {
            auto sl = Is<StructLit>(n);
            auto si = SI(t);
            if (!sl || sl->sinst != si) return false;
            string s = "{ ";
            auto any = false;
            for (size_t fi = 0; fi < si->st->fields.size(); fi++) {
                if (si->st->fields[fi].ispad) continue;   
                string fx;
                if (!StaticInitX(sl->InitFor((int)fi), si->ftypes[fi], fx)) return false;
                Append(s, any ? ", " : "", ".", Sanitize(si->st->fields[fi].name),
                       " = ", fx);
                any = true;
            }
            out = any ? s + " }" : "{ 0 }";
            return true;
        }
        case TY_ARRAY: {
            auto &a = *t->arr;
            if (a.akind != A_FIXED) return false;
            auto k = ArrSize(t->arr);
            if (k < 1) return false;
            string s = "{ { ";
            if (auto str = Is<StrLit>(n)) {
                if (k > MAXSTATICELEMS || !IsU8(a.sub) || (int64_t)str->val.size() != k) return false;
                for (int64_t e = 0; e < k; e++)
                    Append(s, e ? ", " : "", (int)(uint8_t)str->val[(size_t)e]);
            } else if (auto al = Is<ArrayLit>(n)) {
                string ex;
                if (al->fillval) {
                    if (k > MAXSTATICELEMS || !StaticInitX(al->fillval, a.sub, ex)) return false;
                    for (int64_t e = 0; e < k; e++) Append(s, e ? ", " : "", ex);
                } else {
                    if ((int64_t)al->elems.size() != k) return false;
                    for (int64_t e = 0; e < k; e++) {
                        if (!StaticInitX(al->elems[(size_t)e], a.sub, ex)) return false;
                        Append(s, e ? ", " : "", ex);
                    }
                }
            } else {
                return false;
            }
            out = s + " } }";
            return true;
        }
        default: return false;
    }
}

inline void CodeGen::EmitGlobalDecls() {
    for (auto g : ast.globals) {
        for (size_t di = 0; di < g->defs.size(); di++) {
            auto d = g->defs[di];
            auto name = Unique(Sanitize(g->ns, d->name));
            
            auto reg = [&](const string &s) { gstkexprs.insert(s); };
            
            
            auto member = [&](const string &decl) { Append(gblock, "    ", decl, ";\n"); };
            gnames[d] = cat("GS_GL->", name);
            if (IsRoot(d->type)) {
                member(cat(CT(d->type), " ", name));
                for (auto c : d->components) {
                    if (!c) continue;
                    auto cn = Unique(cat("gs_component_", name, "_", c->componentfield));
                    gnames[c] = cat("GS_GL->", cn);
                    auto ct = c->type;
                    member(cat(IsResz(ct) && !IsFrameObj(ct) ? string("gs_rhdr")
                               : IsBytesT(ct) && !IsFrameObj(ct) ? string("uint8_t *")
                               : CT(ct), " ", cn));
                    if (IsBytesT(ct)) {
                        auto stk = Unique(cat("gs_gstk_", cn));
                        member(cat("gs_stack ", stk));
                        gstks[c] = cat("(&GS_GL->", stk, ")");
                        reg(gstks[c]);
                    }
                }
            } else if (IsResz(d->type)) {
                EmitCoreTypes();
                auto stk = Unique(cat("gs_gstk_", name));
                member(cat(RzCT(d->type), " ", name));
                member(cat("gs_stack ", stk));
                gstks[d] = cat("(&GS_GL->", stk, ")");
                reg(gstks[d]);
                if (d->reusable) {
                    auto fln = Unique(cat(name, "_fl"));
                    auto flstk = Unique(cat("gs_gstk_", fln));
                    member(cat("gs_rhdr ", fln));
                    member(cat("gs_stack ", flstk));
                    gpools[d] = { cat("GS_GL->", fln), cat("(&GS_GL->", flstk, ")") };
                    reg(gpools[d].second);
                }
            } else if (IsBytesT(d->type)) {
                auto stk = Unique(cat("gs_gstk_", name));
                member(cat("uint8_t *", name));
                member(cat("gs_stack ", stk));
                gstks[d] = cat("(&GS_GL->", stk, ")");
                reg(gstks[d]);
            } else {
                string init;
                
                
                
                
                
                
                
                if (!d->isvar && d->type->cq && !PrefVar(d) &&
                    StaticInitX(g->inits[di], d->type, init)) {
                    Append(data, "static const ", VarCT(d), " ", name, " = ", init, ";\n");
                    gstatic[d] = g->inits[di];
                    gnames[d] = cat("(*(", VarCT(d), " *)&", name, ")");
                } else if (!d->isvar && d->type->cq && !PrefVar(d) && IsFlatFixed(d->type)) {
                    
                    
                    
                    Append(data, "static ", VarCT(d), " ", name, ";\n");
                    gshared.insert(d);
                    gnames[d] = name;
                } else {
                    member(cat(VarCT(d), " ", name));
                }
            }
        }
    }
}

inline void CodeGen::InitGlobalValue(VarDef *d, Node *init) {
    if (IsRoot(d->type)) {
        auto sl = Is<StructLit>(init);
        assert(sl && sl->sinst->st->isroot);
        for (auto c : d->components) {
            if (!c) continue;
            auto i = c->componentfield;
            InitGlobalValue(c, sl->InitFor(i));
            Ident id(c->line, c->name);
            id.vdef = c;
            id.exprtype = c->type;
            auto ref = c->type->kind == TY_REF ? cat("&", VarLoc(c).s)
                                              : GenRefVal(&id, init->line);
            L(gnames[d], ".", Sanitize(sl->sinst->st->fields[i].name), " = ", ref, ";");
        }
    } else if (IsResz(d->type)) {
        InitGlobalStack(d);
        GenConstruct(init, gstks[d], d->type, GlobalLenLv(d));
    } else if (IsBytesT(d->type)) {
        InitGlobalStack(d);
        GenConstruct(init, gstks[d], d->type);
    } else if (PrefVar(d)) {
        L(gnames[d], " = ", GenPrefVal(init), ";");
    } else if (IsRelRef(d->type)) {
        EmitRelStoreAt(cat("(uint8_t *)&", gnames[d]), d->type, GenX(init),
                       init->line, true, RelValue(init));
    } else {
        GenAny(init, Dst { DK_LVALUE, gnames[d], d->type });
    }
}

inline void CodeGen::EmitGlobalInit() {
    curspec = nullptr;
    curinfo = nullptr;
    ResetFnState();
    spexpr = "0";
    PushSc(SC_FN);
    for (auto g : ast.globals) {
        if (g->inits.empty()) continue;
        PushSc(SC_STMT);
        for (size_t i = 0; i < g->defs.size(); i++) {
            auto d = g->defs[i];
            if (gstatic.count(d)) continue;   
            InitGlobalValue(d, g->inits[i]);
        }
        if (termjump) cscopes.back().saves.clear();
        PopSc();
        termjump = false;
    }
    EmitExitRestores(0);
    cscopes.clear();
    stackown[nullptr] = std::max(stackown[nullptr], stkmax);
    auto decls = HoistAggregateDecls(body);
    Append(code, "static void gs_init_globals(void) {\n");
    code += decls;
    code += body;
    code += "}\n\n";
}



inline string CodeGen::IndexTypeCheck(const string &x, TypeExpr *t, Line ln) {
    auto n = GlobalLenLv(t->ix->arr);
    if (!t->ix->upto) return cat("GS_IDX(", x, ", ", n, ", ", LocArgs(ln), ")");
    return cat("((uint64_t)(", x, ") <= (uint64_t)(", n, ") ? (", x, ") : (gs_idxfail((int64_t)(",
               x, "), (", n, "), ", LocArgs(ln), "), (int64_t)0))");
}



inline string CodeGen::GlobalLenLv(VarDef *d) { return RzLenLv(d->type, gnames[d]); }

inline void CodeGen::InitGlobalStack(VarDef *d) {
    auto stk = gstks[d];
    globalregions++;
    L("gs_stack_init(", stk, ");");
    if (IsResz(d->type) && IsFrameObj(d->type)) {
        
    } else if (IsResz(d->type)) {
        L(gnames[d], ".base = ", Top(stk), ";");
        L(gnames[d], ".len = 0;");
    } else {
        L(gnames[d], " = ", Top(stk), ";");
    }
    if (d->reusable) {
        auto &p = gpools[d];
        globalregions++;
        L("gs_stack_init(", p.second, ");");
        L(p.first, ".base = ", Top(p.second), ";");
        L(p.first, ".len = 0;");
    }
}

inline void CodeGen::EmitProgramInit() {
    Append(data, "static int gs_program_initialized;\n");
    
    
    
    BoundStacks();
    auto mainstacks = ProgramStacks(MainRoots());
    auto mainsf = ast.MainFunction();
    CheckStackLimit("the main program", mainstacks, mainsf ? mainsf->line : Line {});
    int64_t workerregions = 0;
    for (auto &[name, sp] : WorkerEntries()) {
        auto stacks = ProgramStacks({ sp });
        CheckStackLimit(cat("thread program ", name), stacks, sp->sf->line);
        EmitThreadThunk(sp, stacks);
        workerregions = std::max(workerregions, stacks + thunkregions[sp]);
    }
    Append(code, "static void gs_program_init(int argc, char **argv) {\n"
                 "    if (gs_program_initialized) return;\n"
                 "    gs_rt_init(argc, argv, ", mainstacks, ", ", mainstacks + globalregions,
           ", ", workerregions, ");\n");
    
    
    
    
    
    auto dllibs = DlLibs();
    for (auto sp : usedexterns) dlbinds += !sp->sf->dylib.empty();
    if (!dllibs.empty())
        Append(code, "    gs_dl_bind(gs_dllibs, ", dllibs.size(), ", gs_dlbinds, ", dlbinds,
               ");\n");
    
    vector<string> qnames;
    for (auto &[m, q] : queues) qnames.push_back(q);
    sort(qnames.begin(), qnames.end());
    for (auto &q : qnames) Append(code, "    gs_qinit(&", q, ");\n");
    Append(code, "    gs_gl = &gs_globals_main;\n    gs_init_globals();\n");
    Append(code, "    gs_program_initialized = 1;\n}\n\n");
    
    
    
    
    
    
    if (library) {
        Append(code, "void faloya_init(int argc, char **argv) {\n"
                     "    if (gs_program_initialized) return;\n"
                     "    gs_program_init(argc, argv);\n",
                     MainCall(), "}\n\n");
        Append(code, "void faloya_exit(void) {\n"
                     "    if (!gs_program_initialized) return;\n"
                     "    gs_rt_stop();\n"
                     "    free(gs_stks);\n"
                     "    gs_stks = NULL;\n"
                     "    gs_nstks = 0;\n");
        for (auto &q : qnames) Append(code, "    gs_qfree(&", q, ");\n");
        Append(code, "    gs_program_initialized = 0;\n}\n\n");
    }
}


inline string CodeGen::MainCall() {
    auto mainsf = ast.MainFunction();
    if (!mainsf || mainsf->specs.empty() || !sinfo.count(mainsf->specs[0])) return "";
    auto &mi = sinfo[mainsf->specs[0]];
    return cat("    ", mi.cname, "(", mi.needssp ? "0" : "", ");\n");
}

inline void CodeGen::EmitExports() {
    for (auto sf : ast.functions) {
        if (!sf->isexport) continue;
        if (sf->specs.size() != 1 || !sinfo.count(sf->specs[0]))
            Fail(sf->line, cat("export fn ", sf->name, " has no emitted specialization"));
        auto sp = sf->specs[0];
        auto &si = sinfo[sp];
        auto returnslice = !sp->rets.empty() && sp->rets[0]->kind == TY_SLICE;
        string params, args;
        vector<string> argv;
        for (size_t i = 0; i < sp->argtypes.size(); i++) {
            auto arg = Unique(cat("gs_export_arg", i));
            Append(params, params.empty() ? "" : ", ", CT(sp->argtypes[i]), " ", arg);
            PushArg(argv, sp->argtypes[i], IsPoolParam(sp, i), arg);
        }
        for (auto &a : argv) Append(args, args.empty() ? "" : ", ", a);
        auto outlen = Unique("gs_export_len");
        if (returnslice) Append(params, params.empty() ? "" : ", ", "gs_i64 *", outlen);
        if (params.empty()) params = "void";
        if (!args.empty() && si.needssp) args += ", ";
        if (si.needssp) args += "0";
        auto ret = sp->rets.empty() ? string("void")
                   : returnslice ? string("const uint8_t *") : CT(sp->rets[0]);
        Append(exportprotos, ret, " ", sf->cname, "(", params, ");\n");
        Append(code, ret, " ", sf->cname, "(", params, ") {\n"
                     "    if (!gs_program_initialized)\n"
                     "        gs_panic(\"faloya_init must be called before exported functions\");\n");
        if (returnslice) {
            auto value = Unique("gs_export_result");
            Append(code, "    if (!", outlen,
                   ") gs_panic(\"exported slice length pointer is null\");\n");
            Append(code, "    ", CT(sp->rets[0]), " ", value, " = ", si.cname, "(", args,
                   ");\n"
                   "    *", outlen, " = ", value, ".len;\n"
                   "    return (const uint8_t *)", value, ".data;\n");
        } else if (sp->rets.empty()) {
            Append(code, "    ", si.cname, "(", args, ");\n");
        } else {
            Append(code, "    return ", si.cname, "(", args, ");\n");
        }
        Append(code, "}\n\n");
    }
}

inline string CodeGen::ExportHeader() {
    
    
    
    string header = "#pragma once\n#include <stdint.h>\n"
                    "#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)\n"
                    "#define GS_ALIGN1 __attribute__((aligned(1)))\n"
                    "#else\n#define GS_ALIGN1\n#endif\n"
                    "typedef int16_t gs_i16 GS_ALIGN1;\n"
                    "typedef int32_t gs_i32 GS_ALIGN1;\n"
                    "typedef int64_t gs_i64 GS_ALIGN1;\n"
                    "typedef uint16_t gs_u16 GS_ALIGN1;\n"
                    "typedef uint32_t gs_u32 GS_ALIGN1;\n"
                    "typedef uint64_t gs_u64 GS_ALIGN1;\n"
                    "typedef float gs_f32 GS_ALIGN1;\n"
                    "typedef double gs_f64 GS_ALIGN1;\n"
                    "typedef struct gs_stack gs_stack;\n"
                    "#pragma pack(push, 1)\n";
    header += tdecls;
    header += "#pragma pack(pop)\n\n";
    header += "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
              "void faloya_init(int argc, char **argv);\n"
              "void faloya_exit(void);\n";
    header += exportprotos;
    header += "#ifdef __cplusplus\n}\n#endif\n";
    return header;
}

inline void CodeGen::EmitMain() {
    Append(code, "int main(int argc, char **argv) {\n    gs_program_init(argc, argv);\n",
           MainCall(), "    gs_rt_stats();\n    return 0;\n}\n");
}

}  
