



#pragma once

namespace faloya {











inline bool CodeGen::IsPoolParam(FnSpec *sp, size_t i) {
    return sp->roots[i].reusable && CarriesPool(sp->argtypes[i]);
}






inline string CodeGen::HandedStacks(Node *n) {
    auto c = n;
    Dot *component = nullptr;
    if (auto un = Is<Unary>(n); un && un->op == T_BITAND) c = un->child;
    while (auto d = Is<Dot>(c)) {
        if (!d->IsField()) return "";
        component = d;
        c = d->obj;
        
        if (!Is<Ident>(c) && c->exprtype && c->exprtype->kind == TY_REF) return "";
    }
    auto id = Is<Ident>(c);
    auto v = id ? id->vdef : nullptr;
    if (v && component && IsRoot(v->type)) v = v->components[component->fieldidx];
    while (v && refalias.count(v)) v = const_cast<VarDef *>(refalias[v]);
    if (!v || !v->type || (!IsResz(v->type) && (!IsFatRef(v->type) || v->isvar)))
        return "";
    if (!v->isglobal && !vnames.count(v)) return "";
    auto lv = VarLoc(v);
    if (lv.t->kind == TY_REF) DerefLoc(lv);
    if (lv.stk.empty()) return "";
    return cat(lv.stk, "\x01", lv.flstk, "\x01");
}




inline string CodeGen::SyncReach(FnSpec *callee, const vector<string> &args,
                                 const vector<Node *> &an) {
    if (!cachetops || sinfo[callee].readsalltops) return "*";
    for (auto fv : sinfo[callee].freevars)
        if (fv->reusable || (fv->type && HoldsFatRef(fv->type))) return "*";
    string handed;
    for (size_t i = 0; i < callee->argtypes.size(); i++) {
        auto pt = callee->argtypes[i];
        if (!IsPoolParam(callee, i) && !HoldsFatRef(pt)) continue;
        
        
        
        if (i >= an.size() || pt->kind != TY_REF || HoldsFatRef(pt->ref->sub)) return "*";
        auto st = HandedStacks(an[i]);
        if (st.empty()) return "*";
        handed += st;
    }
    
    
    
    
    
    
    
    
    auto &ki = sinfo[callee];
    if ((reftops || topdst || topcap) && ki.hasrf) return "*";
    if ((reftops || topdst) && !ki.globals.empty()) return "*";
    auto s = handed;
    for (auto &a : args) { s += a; s += '\x01'; }
    for (auto d : sinfo[callee].globals) {
        
        
        if (HoldsFatRef(d->type)) return "*";
        auto it = gstks.find(d);
        if (it != gstks.end()) { s += it->second; s += '\x01'; }
        
        
        auto pit = gpools.find(d);
        if (pit != gpools.end()) { s += pit->second.second; s += '\x01'; }
    }
    return s;
}





inline void CodeGen::FindKnownRootParams() {
    struct Root {
        VarDef *owner = nullptr;
        bool unknown = false;
    };
    unordered_map<const VarDef *, Root> roots;
    vector<pair<VarDef *, Node *>> bindings;
    vector<pair<FnSpec *, FnSpec *>> edges;
    auto rootref = [&](const VarDef *v) {
        return v && v->type && v->type->kind == TY_REF &&
               !v->type->ref->optional && IsRoot(v->type->ref->sub);
    };
    auto admit = [&](VarDef *v) {
        if (!rootref(v)) return false;
        auto &r = roots[v];
        r.unknown |= v->isvar || v->isglobal || v->captured || capturedvars.count(v);
        return true;
    };
    auto bind = [&](VarDef *v, Node *arg) {
        if (admit(v)) bindings.push_back({ v, arg });
    };
    auto opaque = [&](FnSpec *sp) {
        if (sp) for (auto p : sp->params) if (admit(p)) roots[p].unknown = true;
    };
    for (auto sp : livespecs) {
        for (auto p : sp->params) admit(p);
        if (sp->sf->isexport || sp->sf->isthread) opaque(sp);
    }
    auto scan = [&](Node *body, FnSpec *caller) {
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto vd = Is<VarDecl>(n))
                for (size_t i = 0; i < vd->defs.size(); i++)
                    bind(vd->defs[i], vd->defs.size() == vd->inits.size() ? vd->inits[i] : nullptr);
            if (auto c = Is<Call>(n)) {
                auto edge = [&](FnSpec *k) {
                    if (caller && k && k != caller && sinfo.count(k)) edges.push_back({ caller, k });
                };
                if (c->spec) {
                    edge(c->spec);
                    if (c->builtin < 0 && c->dispatch.empty()) {
                        auto an = CallArgNodes(c, c->spec->params.size());
                        for (size_t i = 0; i < c->spec->params.size(); i++)
                            bind(c->spec->params[i], i < an.size() ? an[i] : nullptr);
                    } else opaque(c->spec);
                }
                for (auto k : c->dispatch) { edge(k); opaque(k); }
                for (auto [t, k] : c->fmtspecs) { edge(k); opaque(k); }
                for (size_t i = 0; i < c->fvparams.size(); i++)
                    bind(c->fvparams[i], i < c->args.size() ? c->args[i] : nullptr);
            }
            RunChildren(n, walk);
        };
        walk(body);
    };
    for (auto sp : livespecs) scan(sp->body, sp);
    for (auto g : ast.globals) scan(g, nullptr);
    auto value = [&](Node *arg) -> Root {
        if (auto u = Is<Unary>(arg); u && u->op == T_BITAND) arg = u->child;
        auto id = Is<Ident>(arg);
        auto v = id ? id->vdef : nullptr;
        if (v && v->isglobal && v->type && IsRoot(v->type)) return { v, false };
        auto it = roots.find(v);
        return it != roots.end() ? it->second : Root { nullptr, true };
    };
    for (auto changed = true; changed;) {
        changed = false;
        for (auto [v, arg] : bindings) {
            auto &to = roots[v];
            if (to.unknown) continue;
            auto from = value(arg);
            if (from.unknown || (from.owner && to.owner && from.owner != to.owner)) {
                to.unknown = true;
                changed = true;
            } else if (from.owner && !to.owner) {
                to.owner = from.owner;
                changed = true;
            }
        }
    }
    for (auto sp : livespecs)
        for (auto p : sp->params) {
            auto it = roots.find(p);
            if (it == roots.end() || it->second.unknown || !it->second.owner) continue;
            knownrootparams[p] = it->second.owner;
            
            
            sinfo[sp].globals.insert(it->second.owner);
        }
    for (auto changed = true; changed;) {
        changed = false;
        for (auto [caller, callee] : edges)
            for (auto g : sinfo[callee].globals)
                changed |= sinfo[caller].globals.insert(g).second;
    }
}












inline void CodeGen::FindSplitRootParams() {
    auto rootref = [&](const VarDef *v) {
        return v && v->type && v->type->kind == TY_REF && !v->type->ref->optional &&
               IsRoot(v->type->ref->sub) && !v->isvar && !v->isglobal && !v->captured &&
               !capturedvars.count(v);
    };
    unordered_map<const VarDef *, const VarDef *> standsfor;
    set<const VarDef *> fixed;
    unordered_map<const VarDef *, vector<const VarDef *>> forwards;
    for (auto sp : livespecs)
        for (auto p : sp->params)
            if (rootref(p) && !knownrootparams.count(p)) standsfor[p] = p;
    auto opaque = [&](FnSpec *k) {
        if (k) for (auto p : k->params) fixed.insert(p);
    };
    for (auto sp : livespecs)
        if (sp->sf->isexport || sp->sf->isthread) opaque(sp);
    auto scan = [&](Node *body) {
        set<const Node *> handled;
        auto group = [&](Node *x) -> const VarDef * {
            auto id = Is<Ident>(x);
            auto it = id ? standsfor.find(id->vdef) : standsfor.end();
            return it != standsfor.end() ? it->second : nullptr;
        };
        auto bind = [&](VarDef *d, Node *init) {
            auto p = group(init);
            if (!p || !rootref(d) || !TEq(d->type, init->exprtype)) return;
            standsfor[d] = p;
            handled.insert(init);
        };
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto vd = Is<VarDecl>(n); vd && vd->defs.size() == vd->inits.size())
                for (size_t i = 0; i < vd->defs.size(); i++) bind(vd->defs[i], vd->inits[i]);
            if (auto c = Is<Call>(n)) {
                if (c->fvbody)
                    for (size_t i = 0; i < c->fvparams.size() && i < c->args.size(); i++)
                        bind(c->fvparams[i], c->args[i]);
                if (c->spec && c->builtin < 0 && c->dispatch.empty()) {
                    auto an = CallArgNodes(c, c->spec->params.size());
                    for (size_t i = 0; i < an.size(); i++)
                        if (auto p = group(an[i]); p && handled.insert(an[i]).second)
                            forwards[p].push_back(c->spec->params[i]);
                } else opaque(c->spec);
                for (auto k : c->dispatch) opaque(k);
                for (auto [t, k] : c->fmtspecs) opaque(k);
                for (auto [fv, v] : c->fvremap)
                    if (auto it = standsfor.find(v); it != standsfor.end()) fixed.insert(it->second);
            }
            if (auto d = Is<Dot>(n); d && d->IsField() && d->fieldidx >= 0 && group(d->obj))
                handled.insert(d->obj);
            if (auto p = group(n); p && !handled.count(n)) fixed.insert(p);
            RunChildren(n, walk);
        };
        walk(body);
    };
    for (auto sp : livespecs) scan(sp->body);
    for (auto g : ast.globals) scan(g);
    for (auto changed = true; changed;) {
        changed = false;
        for (auto &[p, ks] : forwards) {
            if (fixed.count(p)) continue;
            for (auto k : ks)
                if (!standsfor.count(k) || standsfor[k] != k || fixed.count(k)) {
                    fixed.insert(p);
                    changed = true;
                    break;
                }
        }
    }
    for (auto [v, p] : standsfor)
        if (v == p && !fixed.count(p)) splitroots.insert(p);
}



inline string CodeGen::NamedRootDesc(const string &v) {
    string inner;
    if (v.size() > 3 && v.compare(0, 2, "(&") == 0 && v.back() == ')')
        inner = v.substr(2, v.size() - 3);
    else if (v.size() > 1 && v[0] == '&')
        inner = v.substr(1);
    auto global = inner.compare(0, 7, "GS_GL->") == 0;
    return IsCName(global ? inner.substr(7) : inner) ? inner : string();
}



inline void CodeGen::PushRootMembers(vector<string> &args, TypeExpr *t, string v) {
    auto desc = NamedRootDesc(v);
    if (desc.empty()) {
        if (!IsCName(v)) {
            auto x = T();
            L(CT(t), " ", x, " = ", v, ";");
            v = x;
        }
        desc = cat("(*", v, ")");
    }
    auto si = SI(t->ref->sub);
    for (size_t f = 0; f < si->ftypes.size(); f++) {
        if (!si->ftypes[f]) continue;
        auto m = cat(desc, ".", Sanitize(si->st->fields[f].name));
        auto fs = ParamFields(ast.RefTo(si->ftypes[f], t->line), false);
        if (fs.empty()) args.push_back(m);
        for (auto &fm : fs) args.push_back(cat(m, ".", fm.second));
    }
}

inline void CodeGen::CollectSpecs() {
    for (auto sp : ast.fnspecs)
        if (sp->live && sp->body) livespecs.push_back(sp);
    
    
    map<string_view, int> namecount;
    for (auto sp : livespecs) namecount[sp->sf->qname]++;
    for (auto sp : livespecs) {
        auto &si = sinfo[sp];
        auto base = Sanitize(sp->sf->ns, sp->sf->name);
        if (namecount[sp->sf->qname] > 1) Append(base, "_", sp->id);
        si.cname = Unique(base);
        si.hasrf = !sp->needs.empty();
        
        si.located = ast.StdlibFile(sp->sf->line.fileidx) && !sp->sf->isexport &&
                     sp->sf != ast.MainFunction();
        for (size_t i = 0; i < sp->rets.size(); i++)
            if (si.cret < 0 && IsFix(sp->rets[i]) && !IsLargeFixed(sp->rets[i]) &&
                !NeedsResultLocation(sp->rets[i]))
                si.cret = (int)i;
        for (auto t : sp->needs)
            if (!fromids.count(t)) fromids[t] = (int)fromids.size() + 1;
    }
    
    
    
    
    
    
    
    
    
    
    
    
    unordered_map<FnSpec *, vector<pair<FnSpec *, const Call *>>> callees;
    for (auto sp : livespecs) {
        auto &si = sinfo[sp];
        auto &calls = callees[sp];
        set<const VarDef *> seen;
        set<TypeExpr *> namedtypes;
        set<pair<FnSpec *, const Call *>> seencalls;
        for (auto pt : sp->argtypes) si.needssp |= NeedsStack(pt);
        for (auto rt : sp->rets) si.needssp |= NeedsStack(rt);
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            NodeNamedArrays(n, namedtypes, [&](VarDef *a, bool length) {
                if (length) si.globals.insert(a);
            });
            if (auto id = Is<Ident>(n)) {
                auto v = id->vdef;
                
                
                if (v && v->ownerspec != sp && !v->isglobal && seen.insert(v).second)
                    si.freevars.push_back(v);
                if (v && v->isglobal && v->type &&
                    (IsBytesT(v->type) || HoldsFatRef(v->type)))
                    si.globals.insert(v);
            }
            if (n->exprtype && n->exprtype->kind != TY_VOID && n->exprtype->kind != TY_FN &&
                n->exprtype->kind != TY_GENERIC && NeedsStack(n->exprtype))
                si.needssp = true;
            
            if (auto al = Is<ArrayLit>(n); al && al->exprtype->kind == TY_SLICE &&
                IsFix(al->exprtype->sub)) {
                auto count = LitCount(al);
                si.needssp |= count > NATIVE_VALUE_LIMIT /
                    std::max<int64_t>(FixedSize(al->exprtype->sub), 1);
            }
            
            if (auto m = Is<MatchExpr>(n))
                for (auto &arm : m->arms) si.needssp |= arm.binder && NeedsStack(arm.binder->type);
            
            
            
            if (auto from = AdtFrom(n)) si.needssp |= NeedsStack(from);
            if (auto c = Is<Call>(n)) {
                if (c->spec && c->spec->sf->isextern && c->spec->sf->readsalltops)
                    si.readsalltops = true;
                auto add = [&](FnSpec *k) {
                    pair<FnSpec *, const Call *> e { k, c->fvremap.empty() ? nullptr : c };
                    if (k && k != sp && sinfo.count(k) && seencalls.insert(e).second)
                        calls.push_back(e);
                };
                if (c->builtin < 0) add(c->spec);
                for (auto d : c->dispatch) add(d);
                
                
                for (auto &fs : c->fmtspecs) add(fs.second);
                if (c->builtin == B_PRINT || c->builtin == B_THREAD_SPAWN ||
                    c->builtin == B_THREAD_SPAWN_CONTAINED)
                    si.needssp = true;
                
                
                for (auto rt : c->rettypes) si.needssp |= NeedsStack(rt);
                
                
                if (c->builtin == B_FORMAT) {
                    auto an = c->ArgNodes();
                    auto recv = an[0]->exprtype;
                    if (recv->kind == TY_REF) recv = recv->ref->sub;
                    if (recv->kind == TY_ARRAY && recv->arr->akind == A_LIMITED)
                        for (size_t i = 1; i < an.size(); i++)
                            si.needssp |= !SimpleText(FmtContext(c, an[i]), an[i]->exprtype);
                }
                
                
                
                if (!c->dispatch.empty()) {
                    auto pos = c->dispatcharg;
                    auto st = CallArgNodes(c, c->dispatch[0]->params.size())[pos]->exprtype;
                    if (st->kind == TY_REF) st = st->ref->sub;
                    if (st->kind == TY_ENUM && st->enu->varmode)
                        for (auto d : c->dispatch) si.needssp |= d->argtypes[pos]->kind != TY_REF;
                }
            }
            RunChildren(n, walk);
        };
        walk(sp->body);
    }
    
    
    
    for (auto changed = true; changed;) {
        changed = false;
        for (auto sp : livespecs) {
            auto &si = sinfo[sp];
            set<const VarDef *> seen(si.freevars.begin(), si.freevars.end());
            for (auto [callee, via] : callees[sp]) {
                auto &ki = sinfo[callee];
                
                
                for (auto fv : ki.freevars) {
                    auto v = via ? via->FreeVarArg(fv) : fv;
                    if (v->ownerspec != sp && seen.insert(v).second) {
                        si.freevars.push_back(v);
                        changed = true;
                    }
                }
                for (auto v : ki.globals)
                    if (si.globals.insert(v).second) changed = true;
                if (ki.needssp && !si.needssp) {
                    si.needssp = true;
                    changed = true;
                }
                if (ki.readsalltops && !si.readsalltops) {
                    si.readsalltops = true;
                    changed = true;
                }
            }
        }
    }
}













inline vector<pair<string, string>> CodeGen::ParamFields(TypeExpr *t, bool pool) {
    if (pool)
        return { { "gs_rhdr *", "hdr" }, { "gs_stack *", "stk" }, { "gs_rhdr *", "fl" },
                 { "gs_stack *", "flstk" } };
    if (IsFatRef(t)) return { { "gs_rhdr *", "hdr" }, { "gs_stack *", "stk" } };
    if (t->kind == TY_SLICE) return { { "void *", "data" }, { "int64_t ", "len" } };
    return {};
}



inline void CodeGen::PushArg(vector<string> &args, TypeExpr *t, bool pool, const string &v) {
    auto fs = ParamFields(t, pool);
    if (fs.empty()) {
        args.push_back(v);
        return;
    }
    auto x = v;
    if (!IsCName(x)) {
        x = T();
        L(pool ? string("gs_pref") : CT(t), " ", x, " = ", v, ";");
    }
    for (auto &f : fs) args.push_back(cat(x, ".", f.second));
}





inline string CodeGen::SigParams(FnSpec *sp, bool decls, bool er) {
    auto &si = sinfo[sp];
    string s;
    auto add = [&](const string &d) {
        if (!s.empty()) s += ", ";
        s += d;
    };
    if (decls) paramcopies.clear();
    
    auto addval = [&](TypeExpr *t, bool pool, const string &ct, const string &pn) {
        auto fs = ParamFields(t, pool);
        if (fs.empty()) {
            add(cat(ct, " ", pn));
            return;
        }
        
        
        string sets;
        for (auto &f : fs) {
            auto m = decls ? Unique2(cat(pn, "_", f.second)) : cat(pn, "_", f.second);
            add(cat(f.first, m));
            Append(sets, " ", pn, ".", f.second, " = ", m, ";");
        }
        if (decls) Append(paramcopies, "    ", ct, " ", pn, ";", sets, "\n");
    };
    for (size_t i = 0; i < sp->params.size(); i++) {
        auto vd = sp->params[i];
        auto pt = sp->argtypes[i];
        auto pn = decls ? LocalName(vd) : cat("p", i);
        if (IsPoolParam(sp, i)) {
            EmitCoreTypes();
            addval(pt, true, "gs_pref", pn);
        } else if (splitroots.count(vd)) {
            
            
            auto ri = SI(pt->ref->sub);
            string sets;
            for (size_t f = 0; f < ri->ftypes.size(); f++) {
                if (!ri->ftypes[f]) continue;
                auto rt = ast.RefTo(ri->ftypes[f], pt->line);
                auto fn = Sanitize(ri->st->fields[f].name);
                auto fs = ParamFields(rt, false);
                if (fs.empty()) fs = { { cat(CT(rt), " "), "" } };
                for (auto &[ct, member] : fs) {
                    auto path = member.empty() ? fn : cat(fn, ".", member);
                    auto base = member.empty() ? cat(pn, "_", fn) : cat(pn, "_", fn, "_", member);
                    auto m = decls ? Unique2(base) : base;
                    add(cat(ct, m));
                    Append(sets, " ", pn, ".", path, " = ", m, ";");
                }
            }
            if (decls) Append(paramcopies, "    ", CT(pt->ref->sub), " ", pn, ";", sets, "\n");
        } else if (IsResz(pt)) {
            
            
            EmitCoreTypes();
            add(cat(RzCT(pt), " ", pn));
            add(cat("gs_stack *", pn, "_stk"));
            if (decls) vstk[vd] = cat(pn, "_stk");
        } else if (IsBytesT(pt)) {
            add(cat("uint8_t *", pn));
        } else if (IsLargeFixed(pt)) {
            
            
            add(cat(CT(pt), " *", pn));
            if (decls) fvptr.insert(vd);
        } else {
            addval(pt, false, CT(pt), pn);
        }
    }
    auto fvn = 0;
    for (auto fv : si.freevars) {
        auto fn = decls ? LocalName(const_cast<VarDef *>(fv)) : cat("fv", fvn++);
        auto ft = fv->type;
        if (fv->reusable) {
            EmitCoreTypes();
            addval(ft, true, "gs_pref", fn);
        } else if (IsResz(ft)) {
            EmitCoreTypes();
            add(cat(RzCT(ft), " *", fn));
            add(cat("gs_stack *", fn, "_stk"));
            if (decls) {
                vstk[fv] = cat(fn, "_stk");
                fvptr.insert(fv);
            }
        } else if (IsBytesT(ft)) {
            add(cat("uint8_t *", fn));
        } else {
            add(cat(VarCT(fv), " *", fn));
            if (decls) fvptr.insert(fv);
        }
    }
    for (size_t i = 0; i < sp->rets.size(); i++) {
        if (IsResz(sp->rets[i]) || (er && i == 0)) {
            
            
            
            
            add(cat("gs_stack *gs_dst", i));
            if (IsResz(sp->rets[i]) && IsFrameObj(sp->rets[i]))
                add(cat(CT(sp->rets[i]), " *gs_rl", i));
            else
                add(cat("gs_i64 *gs_rl", i));
        } else if (IsBytesT(sp->rets[i])) {
            add(cat("gs_stack *gs_dst", i));
        } else if ((int)i != si.cret) {
            add(cat(CT(sp->rets[i]), " *gs_r", i));
        }
    }
    if (si.needssp) add("int64_t gs_sp");
    if (si.located) add("const gs_loc *gs_at");
    if (s.empty()) s = "void";
    return s;
}

inline string CodeGen::SigRet(FnSpec *sp) {
    auto &si = sinfo[sp];
    return si.cret >= 0 ? CT(sp->rets[si.cret]) : "void";
}





inline bool CodeGen::CacheableStk(const string &stk) {
    if (!cachetops) return false;
    if (stk.compare(0, 3, "GS(") == 0) return topown;
    if (gstkexprs.count(stk)) return topglob;
    if (reftops && refstkexprs.count(stk)) return true;
    if (topcap && capstkexprs.count(stk)) return true;
    if (topdst && stk.compare(0, 6, "gs_dst") == 0 && stk.size() > 6) {
        for (size_t i = 6; i < stk.size(); i++)
            if (!isdigit((unsigned char)stk[i])) return false;
        return true;
    }
    return false;
}


inline int CodeGen::TopIdx(const string &stk) {
    for (size_t i = 0; i < toporder.size(); i++) if (toporder[i] == stk) return (int)i;
    toporder.push_back(stk);
    return (int)toporder.size() - 1;
}


inline bool CodeGen::InConsOf(const string &stk) {
    for (auto id : loopstack) if (loopcons[id] == stk) return true;
    return false;
}





inline string CodeGen::Top(const string &stk) {
    if (!markers || (!CacheableStk(stk) && !InConsOf(stk))) return cat(stk, "->top");
    return cat(TOPMARK, TopIdx(stk), "@@");
}










inline string CodeGen::TopW(const string &stk) {
    if (CacheableStk(stk)) {
        auto loop = -1, block = -1;
        for (auto it = loopstack.rbegin(); it != loopstack.rend() && loop < 0; ++it) {
            if (loopcons[*it].empty()) loop = *it;
            else if (block < 0 && loopcons[*it] == BLOCKREGION) block = *it;
        }
        growth.push_back({ TopIdx(stk), loop >= 0 ? loop : block });
    } else if (markers) {
        for (auto it = loopstack.rbegin(); it != loopstack.rend(); ++it)
            if (loopcons[*it] == stk) {
                growth.push_back({ TopIdx(stk), *it });
                break;
            }
    }
    return Top(stk);
}



inline int CodeGen::OpenRegion(const string &cons) {
    auto id = (int)loopparent.size();
    loopparent.push_back(loopstack.empty() ? -1 : loopstack.back());
    loopcons.push_back(cons);
    loopstack.push_back(id);
    L(LOOPMARK, "b", id);
    return id;
}


inline int CodeGen::MarkLoopBegin() { return markers ? OpenRegion("") : -1; }


inline int CodeGen::MarkBlockBegin() { return markers ? OpenRegion(BLOCKREGION) : -1; }

inline void CodeGen::MarkLoopEnd(int id) {
    if (id < 0) return;
    assert(loopstack.back() == id);
    loopstack.pop_back();
    L(LOOPMARK, "e", id);
}








inline int CodeGen::MarkConsBegin(const string &stk) {
    if (!markers || CacheableStk(stk) || InConsOf(stk)) return -1;
    return OpenRegion(stk);
}





inline CodeGen::TopCachePlan CodeGen::PlanTopCaches() {
    TopCachePlan plan;
    plan.fnlocals.resize(toporder.size());
    vector<int> fnwide(toporder.size(), 0);
    for (auto g : growth) {
        if (g.loop < 0) fnwide[g.stk] = 1;
        else if (!plan.IsRegion(g.stk, g.loop)) plan.regions.push_back(g);
    }
    for (size_t i = 0; i < plan.regions.size();) {
        auto r = plan.regions[i];
        auto drop = fnwide[r.stk] != 0;
        for (auto p = loopparent[r.loop]; !drop && p >= 0; p = loopparent[p])
            drop = plan.IsRegion(r.stk, p);
        if (!drop) { i++; continue; }
        plan.regions.erase(plan.regions.begin() + i);
    }
    for (size_t i = 0; i < toporder.size(); i++) if (fnwide[i]) plan.fnlocals[i] = T();
    return plan;
}

inline bool CodeGen::LineIs(string_view s, const char *pfx) {
    return s.compare(0, strlen(pfx), pfx) == 0;
}




inline string_view CodeGen::GotoTarget(string_view line) {
    auto g = line.find("goto ");
    if (g == string_view::npos) return {};
    auto b = g + 5;
    auto e = line.find(';', b);
    return e == string_view::npos ? string_view() : line.substr(b, e - b);
}


inline string_view CodeGen::LabelHere(string_view line) {
    if (line.size() < 3 || line.compare(line.size() - 2, 2, ":;") != 0) return {};
    auto name = line.substr(0, line.size() - 2);
    for (auto c : name)
        if (!isalnum((unsigned char)c) && c != '_') return {};
    return name;
}



inline string_view CodeGen::NextLine(const string &b, size_t &i, size_t &ind0) {
    auto eol = b.find('\n', i);
    if (eol == string::npos) eol = b.size();
    auto line = string_view(b).substr(i, eol - i);
    i = eol + 1;
    auto n = line.find_first_not_of(' ');
    ind0 = n == string_view::npos ? line.size() : n;
    return n == string_view::npos ? string_view() : line.substr(n);
}



inline void CodeGen::NoteLen(const string &stk, const string &lenlv, const string &component) {
    string root = component;
    if (!CacheableStk(stk) || (root.empty() && !LenRoot(lenlv, root))) return;
    auto k = TopIdx(stk);
    for (auto &s : lenslots) if (s.stk == k && s.lenlv == lenlv) return;
    lenslots.push_back({ k, lenlv, root });
}






inline bool CodeGen::LenRoot(const string &lenlv, string &root) {
    auto ident = [&](size_t b, size_t e) {
        if (b >= e || isdigit((unsigned char)lenlv[b])) return false;
        for (auto i = b; i < e; i++)
            if (!isalnum((unsigned char)lenlv[i]) && lenlv[i] != '_') return false;
        return true;
    };
    auto ends = [&](const char *suf) {
        auto n = strlen(suf);
        return lenlv.size() > n && lenlv.compare(lenlv.size() - n, n, suf) == 0;
    };
    auto n = lenlv.size();
    if (ends(".hdr->len") && ident(0, n - 9)) {
        root = lenlv.substr(0, n - 5);
        return true;
    }
    if (lenlv.compare(0, 7, "GS_GL->") == 0 && ends(".len") && ident(7, n - 4)) {
        root = lenlv.substr(0, n - 4);
        return true;
    }
    if (lenlv.compare(0, 2, "(*") == 0 && ends(").len") && ident(2, n - 5)) {
        root = lenlv.substr(0, n - 4);
        return true;
    }
    return false;
}










inline string CodeGen::ExpandTopMarkers(const string &b, TopCachePlan &plan) {
    if (!markers) return b;
    vector<bool> lenok(lenslots.size(), true);
    for (;;) {
        auto clean = true;
        auto out = ExpandTopMarkers1(b, plan, lenok, clean);
        if (clean) return out;
    }
}

inline string CodeGen::ExpandTopMarkers1(const string &b, TopCachePlan &plan,
                                         vector<bool> &lenok, bool &clean) {
    
    
    vector<int> loopbeg(loopparent.size(), 0), loopend(loopparent.size(), 0);
    vector<string> lbname;
    vector<int> lbline;
    for (size_t i = 0, ind0 = 0, ln = 0; i < b.size(); ln++) {
        auto rest = NextLine(b, i, ind0);
        if (LineIs(rest, LOOPMARK)) {
            auto t = rest.substr(strlen(LOOPMARK));
            auto id = atoi(string(t.substr(1)).c_str());
            (t[0] == 'b' ? loopbeg : loopend)[id] = (int)ln;
        } else if (auto lb = LabelHere(rest); !lb.empty()) {
            lbname.push_back(string(lb));
            lbline.push_back((int)ln);
        }
    }
    vector<string> active = plan.fnlocals;   
    vector<int> open;
    
    
    vector<string> lenlocal(lenslots.size());
    vector<bool> synced(toporder.size(), false);
    plan.fnlens.clear();
    for (size_t j = 0; j < lenslots.size(); j++) {
        if (!lenok[j] || plan.fnlocals[lenslots[j].stk].empty()) continue;
        lenlocal[j] = T();
        Append(plan.fnlens, "    int64_t ", lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
    }
    auto store = [&](string &out, size_t ind0, int k) {
        out.append(ind0, ' ');
        Append(out, toporder[k], "->top = ", active[k], ";\n");
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenslots[j].stk != k || lenlocal[j].empty()) continue;
            out.append(ind0, ' ');
            Append(out, lenslots[j].lenlv, " = ", lenlocal[j], ";\n");
        }
    };
    auto load = [&](string &out, size_t ind0, int k) {
        out.append(ind0, ' ');
        Append(out, active[k], " = ", toporder[k], "->top;\n");
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenslots[j].stk != k || lenlocal[j].empty()) continue;
            out.append(ind0, ' ');
            Append(out, lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
        }
    };
    auto isid = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    
    
    auto occurs = [&](const string &s, size_t at, const string &w) {
        if (s.compare(at, w.size(), w) != 0) return false;
        if (at > 0 && isid(w[0])) {
            auto c = s[at - 1];
            if (isid(c) || c == '.' || c == '>') return false;
        }
        auto e = at + w.size();
        return e >= s.size() || !isid(s[e]) || !isid(w.back());
    };
    string out;
    for (size_t i = 0, ind0 = 0; i < b.size();) {
        auto rest = NextLine(b, i, ind0);
        if (LineIs(rest, LOOPMARK)) {
            auto t = rest.substr(strlen(LOOPMARK));
            auto beg = t[0] == 'b';
            auto id = atoi(string(t.substr(1)).c_str());
            if (beg) open.push_back(id); else open.pop_back();
            for (size_t k = 0; k < toporder.size(); k++) {
                if (!plan.IsRegion((int)k, id)) continue;
                if (beg) {
                    active[k] = T();
                    out.append(ind0, ' ');
                    Append(out, "uint8_t *", active[k], " = ", toporder[k], "->top;\n");
                    for (size_t j = 0; j < lenslots.size(); j++) {
                        if (lenslots[j].stk != (int)k || !lenok[j]) continue;
                        lenlocal[j] = T();
                        out.append(ind0, ' ');
                        Append(out, "int64_t ", lenlocal[j], " = ", lenslots[j].lenlv, ";\n");
                    }
                    synced[k] = false;
                } else {
                    store(out, ind0, (int)k);
                    active[k].clear();
                    for (size_t j = 0; j < lenslots.size(); j++)
                        if (lenslots[j].stk == (int)k) lenlocal[j].clear();
                }
            }
            continue;
        }
        auto isflush = LineIs(rest, FLUSHMARK);
        if (isflush || LineIs(rest, RELOADMARK)) {
            auto reach = rest.substr(strlen(isflush ? FLUSHMARK : RELOADMARK));
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty()) continue;
                
                
                if (reach != "*" && CacheableStk(toporder[k]) &&
                    reach.find(toporder[k]) == string_view::npos)
                    continue;
                if (isflush) store(out, ind0, (int)k);
                else load(out, ind0, (int)k);
                synced[k] = isflush;
            }
            continue;
        }
        if (auto tgt = GotoTarget(rest); !tgt.empty()) {
            auto at = -1;
            for (size_t j = 0; j < lbname.size(); j++) if (lbname[j] == tgt) at = lbline[j];
            for (size_t k = 0; k < toporder.size(); k++) {
                if (active[k].empty() || !plan.fnlocals[k].empty()) continue;
                auto reg = -1;
                for (auto id : open) if (plan.IsRegion((int)k, id)) reg = id;
                if (reg >= 0 && at > loopbeg[reg] && at < loopend[reg]) continue;
                store(out, ind0, (int)k);
            }
        }
        string line;
        for (size_t p = 0; p < rest.size();) {
            auto m = rest.find(TOPMARK, p);
            if (m == string_view::npos) { line.append(rest.substr(p)); break; }
            line.append(rest.substr(p, m - p));
            auto ds = m + strlen(TOPMARK);
            auto e = rest.find("@@", ds);
            assert(e != string_view::npos);
            auto k = atoi(string(rest.substr(ds, e - ds)).c_str());
            if (active[k].empty()) Append(line, toporder[k], "->top");
            else line.append(active[k]);
            p = e + 2;
        }
        for (size_t j = 0; j < lenslots.size(); j++) {
            if (lenlocal[j].empty()) continue;
            auto &sl = lenslots[j];
            auto member = sl.lenlv.substr(sl.root.size(), sl.lenlv.size() - sl.root.size() - 3);
            string nl;
            for (size_t p = 0; p < line.size();) {
                if (!occurs(line, p, sl.root)) { nl += line[p++]; continue; }
                auto after = p + sl.root.size();
                if (occurs(line, p, sl.lenlv)) {
                    nl += lenlocal[j];
                    p += sl.lenlv.size();
                    continue;
                }
                
                
                auto base = member + "base";
                auto isbase = line.compare(after, base.size(), base) == 0 &&
                              (after + base.size() >= line.size() || !isid(line[after + base.size()]));
                if (!isbase && !synced[sl.stk]) {
                    lenok[j] = false;
                    clean = false;
                }
                nl.append(sl.root);
                p = after;
            }
            line = nl;
        }
        out.append(ind0, ' ');
        out += line;
        out += '\n';
    }
    return out;
}

inline void CodeGen::PushSc(int kind) {
    CScope s;
    s.kind = kind;
    s.stkbase = stknext;
    cscopes.push_back(s);
}

inline void CodeGen::EmitRestores(const CScope &s) {
    if (s.defers.empty()) {
        for (auto i = (int)s.saves.size() - 1; i >= 0; i--)
            L(Top(s.saves[i].first), " = ", s.saves[i].second, ";");
        return;
    }
    
    
    
    auto saves = s.saves;
    auto defers = s.defers;
    auto di = (int)defers.size() - 1;
    for (auto k = (int)saves.size(); k >= 0; k--) {
        for (; di >= 0 && (int)defers[di].first == k; di--) EmitDeferBody(defers[di].second);
        if (k > 0) L(Top(saves[k - 1].first), " = ", saves[k - 1].second, ";");
    }
    assert(di < 0);
}




inline void CodeGen::RegisterDefer(Defer *d) {
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (cscopes[i].kind == SC_STMT) continue;
        cscopes[i].defers.push_back({ cscopes[i].saves.size(), d });
        return;
    }
    assert(false);
}









inline void CodeGen::EmitDeferBody(Defer *d) {
    auto tj = termjump;
    NameScope ns(*this);
    PushSc(SC_PLAIN);
    L("{");
    ind++;
    for (auto st : d->body->stmts) GenStmt(st);
    if (d->body->tail) GenStmt(d->body->tail);
    PopSc();
    ind--;
    L("}");
    termjump = tj;
}

inline void CodeGen::PopSc() {
    EmitRestores(cscopes.back());
    stknext = cscopes.back().stkbase;
    cscopes.pop_back();
}


inline void CodeGen::EmitExitRestores(int to) {
    for (auto i = (int)cscopes.size() - 1; i >= to; i--) EmitRestores(cscopes[i]);
}







inline string CodeGen::AllocStk(bool forlocal) {
    auto k = stknext++;
    stkmax = std::max(stkmax, stknext);
    if (forlocal)
        for (auto i = (int)cscopes.size() - 1; i >= 0 && cscopes[i].kind == SC_STMT; i--)
            cscopes[i].stkbase = std::max(cscopes[i].stkbase, stknext);
    return SpIdx(k);
}

inline void CodeGen::SaveBase(bool forlocal, const string &stk, const string &basevar) {
    for (auto i = (int)cscopes.size() - 1; i >= 0; i--) {
        if (forlocal && cscopes[i].kind == SC_STMT) continue;
        cscopes[i].saves.push_back({ stk, basevar });
        return;
    }
    assert(false);
}

inline string CodeGen::PoolBase(const VarDef *pool) {
    
    
    
    if (!curspec) return cat(gnames[pool], ".base");
    for (auto &p : poolbases) if (p.first == pool) return p.second;
    auto stem = pool->componentowner
        ? cat(Sanitize(pool->componentowner->name), "_", pool->componentfield)
        : Sanitize(pool->name);
    auto name = Unique2(cat("gs_pb_", stem));
    poolbases.push_back({ pool, name });
    return name;
}




inline string CodeGen::PoolBaseOr(const VarDef *vd, const string &hdrbase) {
    return poolglobals.count(vd) ? PoolBase(vd) : hdrbase;
}

inline string CodeGen::LocalName(VarDef *vd) {
    assert(!vd->isglobal);
    auto it = vnames.find(vd);
    if (it != vnames.end()) return it->second;
    if (vd->componentowner)
        return vnames[vd] = Unique2(cat("gs_component_", Sanitize(vd->componentowner->name),
                                       "_", vd->componentfield));
    return vnames[vd] = Unique2(Sanitize(vd->name));
}

inline string CodeGen::VName(const VarDef *vd) {
    auto it = vnames.find(vd);
    if (it != vnames.end()) return it->second;
    auto git = gnames.find(vd);
    assert(git != gnames.end());
    return git->second;
}



inline string CodeGen::Unique2(const string &base) {
    auto name = base;
    if (fnused.count(name) || used.count(name)) {
        auto &next = fnnextsuffix.try_emplace(base, 2).first->second;
        do name = cat(base, "_", next++);
        while (fnused.count(name) || used.count(name));
    }
    fnused.insert(name);
    return name;
}

}  
