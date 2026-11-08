



#pragma once

namespace faloya {





inline void TypeCheck::PushScope(int kind, Node *node) {
    Scope s;
    s.kind = kind;
    s.serial = ++scopeserial;
    s.varbase = (int)vars.size();
    s.fnbase = (int)localfns.size();
    s.node = node;
    scopes.push_back(s);
}

inline void TypeCheck::PopScope() {
    auto &s = scopes.back();
    
    
    for (auto i = s.varbase; i < (int)vars.size(); i++)
        if (IsPendingArray(vars[i]->type))
            Error(vars[i]->line, cat("the element type of ", vars[i]->name,
                                     " was never determined: nothing was pushed or "
                                     "appended into it, and no array was assigned to it"));
    vars.resize(s.varbase);
    localfns.resize(s.fnbase);
    scopes.pop_back();
}





inline bool TypeCheck::ContainsGrowShrink(TypeExpr *t) {
    switch (t->kind) {
        case TY_ARRAY: return t->arr->akind == A_GROWSHRINK;
        case TY_STRUCT: case TY_VARIANT:
            if (ClassOf(t) == SC_ROOT) return false; 
            return AnyField(t, [&](TypeExpr *ft) { return ContainsGrowShrink(ft); });
        default: return false;
    }
}



inline TypeExpr *TypeCheck::ResizableArrayIn(TypeExpr *t) {
    if (ClassOf(t) != SC_RESIZABLE) return nullptr;
    if (t->kind == TY_ARRAY) return t;
    TypeExpr *arr = nullptr;
    AnyField(t, [&](TypeExpr *ft) { return (arr = ResizableArrayIn(ft)) != nullptr; });
    return arr;
}



inline bool TypeCheck::IsGrowShrinkRoot(VarDef *r) {
    return r && (r->growshrink || (r->type && ContainsGrowShrink(r->type)));
}



inline void TypeCheck::GrowShrinkElems(TypeExpr *t, vector<TypeExpr *> &out) {
    switch (t->kind) {
        case TY_ARRAY:
            if (t->arr->akind == A_GROWSHRINK) out.push_back(t->arr->sub);
            else GrowShrinkElems(t->arr->sub, out);
            return;
        case TY_STRUCT: case TY_VARIANT:
            EachField(t, [&](TypeExpr *ft) { GrowShrinkElems(ft, out); });
            return;
        default: return;
    }
}




inline bool TypeCheck::GrowShrinkContains(TypeExpr *t, TypeExpr *of) {
    vector<TypeExpr *> elems;
    GrowShrinkElems(t, elems);
    for (auto e : elems)
        if (CanContain(e, of)) return true;
    return false;
}






inline bool TypeCheck::GrowShrinkCanHold(VarDef *r, TypeExpr *of) {
    if (!IsGrowShrinkRoot(r)) return false;
    if (!of) return true;
    if (r->type) return GrowShrinkContains(r->type, of);
    
    
    
    
    if (r->gsvia || r->gselems.empty()) return true;
    for (auto e : r->gselems)
        if (CanContain(e, of)) return true;
    return false;
}





inline bool TypeCheck::BoundReachesGrowShrink(VarDef *r, TypeExpr *of, bool byteview) {
    vector<TypeExpr *> reach;
    BoundReach(r, reach);
    for (auto t : reach)
        if ((of && GrowShrinkContains(t, of)) || (byteview && ContainsGrowShrink(t) && Viewable(t)))
            return true;
    return false;
}




inline bool TypeCheck::IntoGrowShrink(const Prov &v, VarDef *root, TypeExpr *t, bool holder) {
    vector<TypeExpr *> pointees;
    if (holder) RefPointees(t, pointees); else pointees.push_back(PointeeOf(t));
    auto intogs = v.byteview && IsGrowShrinkRoot(root) && MayBeViewed(root);
    for (auto pt : pointees) intogs |= GrowShrinkCanHold(root, pt);
    return intogs;
}








inline VarDef *TypeCheck::GrowShrinkTaint(const Prov &p, TypeExpr *t, bool *reach) {
    if (!t || !IsRefOrSlice(t)) return nullptr;
    for (auto &a : p.alts) {
        if (a.slotread) continue;
        if (IntoGrowShrink(p, a.root, t, false)) return a.root;
        if (!a.exact && BoundReachesGrowShrink(a.root, PointeeOf(t), p.freshview)) {
            if (reach) *reach = true;
            return a.root;
        }
    }
    if (t->kind == TY_REF && t->ref->sub->kind == TY_SLICE)
        return GrowShrinkTaint(SlotView(p, t->ref->sub), t->ref->sub, reach);
    return nullptr;
}






inline VarDef *TypeCheck::StoredIntoGrowShrink(const Val &v, const Roots &roots, TypeExpr *t,
                                               bool holder, bool *reach) {
    if (holder) {
        for (auto &a : roots.alts)
            if (!a.slotread && IntoGrowShrink(v, a.root, t, true)) return a.root;
        return nullptr;
    }
    auto p = Prov(v);
    p.TakeAlts(roots);
    return GrowShrinkTaint(p, t, reach);
}








inline string TypeCheck::NeverStoredError(VarDef *root, bool may, bool reach) {
    auto stored = ": such a reference lives in a variable, is passed down or returned, and "
                  "is never stored (§5.2)";
    if (reach)
        return cat("storing a reference that may point into a grow-shrink array ",
                   ReachedThroughStr(root), stored);
    if (may && (!root->type || IsRefOrSlice(root->type)))
        return cat("storing a reference that may point into a grow-shrink array, as ",
                   root->name, " may", stored);
    return cat("storing a reference ", may ? "that may point " : "", "into ", root->name,
               ", which holds a grow-shrink array", stored);
}




inline string TypeCheck::ReachedThroughStr(VarDef *root) {
    return root->type ? cat("reached through ", root->name, "'s references")
                      : cat("reached through the caller's storage behind ", root->name);
}







inline bool TypeCheck::MayBeViewed(VarDef *r) {
    if (!r || !r->type || IsRefOrSlice(r->type)) return true;
    return Viewable(LoadType(r->type));
}

inline bool TypeCheck::Viewable(TypeExpr *t) {
    if (t->kind == TY_ARRAY) {
        string why;
        return ImageSafe(t->arr->sub, why) || Viewable(t->arr->sub);
    }
    return AnyField(t, [&](TypeExpr *ft) { return Viewable(ft); });
}









inline bool TypeCheck::RefMayPointInto(VarDef *v, VarDef *root, bool growonly, TypeExpr *bound) {
    if (v->refrootknown) {
        for (auto &a : v->ref.alts) {
            if (DifferentComponents(a.root, a.exact || a.projected, root, !bound)) continue;
            if (growonly && a.classread && !v->isvar) {
                if (ClassReadMayPointInto(a.root, root, bound)) return true;
                continue;
            }
            if (a.root == root || (!a.exact && Depth(a.root) >= Depth(root))) return true;
        }
    }
    return RefMayRetarget(v, root);
}








inline bool TypeCheck::ClassReadMayPointInto(VarDef *cls, VarDef *root, TypeExpr *bound,
                                             bool inplace) {
    auto arrtype = ShrunkType(root, bound);
    Line where;
    PathsScope ps(*this, inplace);
    return HolderMayPointInto(cls, root, arrtype, LiveEventBase(cls), &where);
}






inline bool TypeCheck::RefMayRetarget(VarDef *v, VarDef *root) {
    if (!v->refrootknown) return !UnboundIsBottom() && Depth(v) >= Depth(root);
    return v->isvar && Depth(v->ref.Root()) == Depth(root);
}





inline bool TypeCheck::SlotReadable(TypeExpr *t) {
    return t->kind == TY_SLICE || (t->kind == TY_REF && t->ref->lenstorage < 0);
}























inline bool TypeCheck::HeldRefsMayPointInto(VarDef *v, const Prov &p, TypeExpr *t,
                                            VarDef *root, TypeExpr *bound, bool growonly) {
    auto slice = t->kind == TY_SLICE;
    auto sub = slice ? t->sub : t->ref->sub;
    
    
    
    
    auto slotof = [&](VarDef *r) -> VarDef * {
        if (slice) return nullptr;
        if (r && !r->type && sub->kind == TY_SLICE) return r->heldslice;
        return r && r->type && IsRefOrSlice(r->type) ? r : nullptr;
    };
    auto byteview = p.byteview || p.Any([&](const RootAlt &a) {
        auto slot = slotof(a.root);
        return (a.root && a.root->contentbyteview) || (slot && slot->ref.byteview);
    });
    
    
    vector<TypeExpr *> pointees;
    if (growonly) ReachedThroughRefs(sub, pointees);
    else RefPointees(sub, pointees);
    
    
    
    
    
    auto freed = false;
    for (auto pt : pointees)
        freed = freed || ((pathslive || ClassOf(pt) != SC_RESIZABLE) &&
                          ShrinkMayFree(root, bound, growonly, pt, byteview && IsU8(pt)));
    if (!freed) return false;
    if (v && !v->refrootknown) return !UnboundIsBottom() && Depth(v) >= Depth(root);
    for (auto &a : p.alts) {
        auto r = a.root;
        auto slot = slotof(r);
        
        
        if (growonly && a.classread && !(v && v->isvar)) {
            if (ClassReadMayPointInto(r, root, bound)) return true;
            continue;
        }
        auto holder = growonly && (slice || sub->kind != TY_SLICE) && r && !r->isglobal &&
                      a.exact && (r->type || IsClassRoot(r)) && !slot && !(v && v->isvar);
        
        
        
        
        size_t since = 0;
        auto emptied = holder && !slice && v && !pathslive &&
                       KilledFor(r, v, sub, ShrunkType(root, bound), since);
        
        
        
        if (r == root && (!holder || slice || bound) && !emptied) return true;
        if (growonly && (slice || sub->kind != TY_SLICE)) {
            
            
            
            if (!r || r->isglobal) continue;
            
            
            
            
            
            if (holder) {
                auto arrtype = ShrunkType(root, bound);
                Line where;
                auto from = LiveEventBase(r);
                if (emptied) from = std::max(from, since);
                if (HolderMayPointInto(r, root, arrtype, from, &where)) return true;
                continue;
            }
        }
        
        
        
        
        if (r && !r->type && a.exact && slot) {
            if (RefMayPointInto(slot, root) || (v && v->isvar && Depth(r) >= Depth(root)))
                return true;
            if (growonly && HoldsPlainRef(sub->sub) &&
                HeldRefsMayPointInto(slot, slot->ref, slot->type, root, bound, growonly))
                return true;
            continue;
        }
        if (growonly || !a.exact || (r && !r->type && sub->kind == TY_SLICE)) {
            if (Depth(r) >= Depth(root)) return true;
            continue;
        }
        if (slot) {
            if (RefMayPointInto(slot, root) || (v && v->isvar && Depth(slot) >= Depth(root)))
                return true;
            continue;
        }
        
        
        if (r && !r->type && !IsTemp(r)) {
            if (v && v->isvar && Depth(r) == Depth(root)) return true;
            continue;
        }
        if (Depth(r) == Depth(root)) return true;
    }
    return false;
}





















inline Prov TypeCheck::SlotView(const Prov &p, TypeExpr *slice) {
    Prov out = p;
    out.alts.clear();
    if (slice->cq) out.writable = false;
    for (auto &a : p.alts) {
        auto r = a.root;
        if (!r || !a.exact) {
            out.Add({ r, a.exact, nullptr, false });
            continue;
        }
        if (auto h = r->type ? nullptr : r->heldslice) {
            
            
            NoteEnvRead(h);
            out.Add(h->ref);
            out.writable = out.writable && h->ref.writable;
            out.byteview = out.byteview || h->ref.byteview;
            out.freshview = out.freshview || h->ref.freshview;
            continue;
        }
        if (r->type && IsRefOrSlice(r->type)) {
            auto v = RefProvOf(r);
            out.Add(v);
            out.writable = out.writable && v.writable;
            out.byteview = out.byteview || v.byteview;
            out.freshview = out.freshview || v.freshview;
            continue;
        }
        if (!r->type) {
            out.Add({ r, false, r, false });
            out.byteview = out.byteview || r->contentbyteview;
            out.freshview = out.freshview || r->contentbyteview;
            continue;
        }
        LVal lv;
        lv.type = slice;
        lv.SetProv(p);
        lv.Set(r, true);
        lv.isslot = true;
        ReadBackLVal(lv);
        out.Add(lv);
        out.byteview = out.byteview || lv.byteview;
    }
    return out;
}





inline void TypeCheck::BindProv(VarDef *vd, const Prov &p) {
    vd->ref = p;
    vd->ref.reached = nullptr;
    if (!vd->type || !CarriesPool(vd->type)) vd->ref.reusable = 0;
    vd->refrootknown = true;
    auto floor = p.None() ? 0 : (int)scopes.size();
    for (auto &a : p.alts) floor = min(floor, ActivationFloor(a.root));
    NoteFact(vd, floor);
}





inline void TypeCheck::BindRefProvenance(VarDef *vd, const Val &v) {
    if (v.isnull) return;
    if (v.None()) {
        if (vd->refrootknown && vd->ref.Unknown()) return;   
        vd->ref.SetUnknown();
        vd->refrootknown = true;
        NoteFact(vd);
        return;
    }
    BindProv(vd, v);
}



inline bool TypeCheck::BoundAnywhere(VarDef *vd) {
    auto t = vd->type;
    return vd->isglobal && vd->isvar && t && IsRefOrSlice(t) &&
           !IsSelfRelRef(t);
}











inline Prov TypeCheck::GlobalVarRead(VarDef *vd) {
    auto t = vd->type;
    Prov p = vd->ref;
    Roots self;
    self.Set(vd, true);
    auto slotread = SlotReadable(t);
    p.TakeAlts(ReadBackRoot(t, self, p.byteview, nullptr, slotread));
    for (auto &a : p.alts) a.slotread = slotread;
    return p;
}















inline Prov TypeCheck::RefProvOf(VarDef *vd) {
    if (BoundAnywhere(vd) && CurRealFrame().spec) return GlobalVarRead(vd);
    Prov p = vd->ref;
    if (!vd->refrootknown) {
        if (UnboundIsBottom()) {
            p.SetUnknown();
            return p;
        }
        auto optional = vd->type && vd->type->kind == TY_REF && vd->type->ref->optional;
        auto seen = !vd->isglobal || !vd->isvar || vd->ownerspec == CurRealFrame().spec;
        if (optional && seen) return p;
        p.Set(temproot, false);
    }
    
    
    if (vd->isglobal && vd->type && SlotReadable(vd->type))
        for (auto &a : p.alts) a.slotread = true;
    if (!vd->refrootknown && vd->type && vd->type->kind == TY_REF && vd->type->ref->optional) {
        auto cands = RootCandidates(LoadType(vd->type->ref->sub), Depth(vd), false,
                                    !vd->type->cq, true);
        if (cands.Any([](const RootAlt &a) { return a.root != nullptr; })) p.alts = cands.alts;
    }
    return p;
}

inline VarDef *TypeCheck::ResetLocal(VarDef *previous) {
    if (!previous) return ast.NewVarDef();
    
    
    
    
    auto captured = previous->captured;
    auto nonneguse = previous->nonneguse, markuse = previous->markuse;
    auto refwrite = previous->refwrite;
    auto slotref = previous->slotref;
    auto constuse = previous->constuse;
    auto constwhat = previous->constwhat;
    auto storedinto = previous->storedinto;
    auto components = std::move(previous->components);
    for (auto c : components) if (c) ResetLocal(c);
    *previous = VarDef {};
    previous->components = std::move(components);
    previous->captured = captured;
    previous->nonneguse = nonneguse;
    previous->markuse = markuse;
    previous->refwrite = refwrite;
    previous->slotref = slotref;
    previous->constuse = constuse;
    previous->constwhat = constwhat;
    previous->storedinto = storedinto;
    return previous;
}

inline VarDef *TypeCheck::RootComponent(VarDef *owner, TypeExpr *t, int field) {
    t = ast.PlainOf(t); 
    auto si = GetStructInst(t);
    assert(si->st->isroot && owner);
    VarDef *c = nullptr;
    if (IsClassRoot(owner)) {
        for (auto x : owner->components)
            if (x && x->componentfield == field && TypeEq(x->componentbase, t)) return x;
        c = ast.NewVarDef();
        owner->components.push_back(c);
    } else {
        owner->components.resize(si->ftypes.size(), nullptr);
        auto &slot = owner->components[field];
        if (!slot) {
            auto it = namedcomponents.find({ owner, si->st->fields[field].name });
            slot = it == namedcomponents.end() ? ast.NewVarDef() : it->second;
        }
        c = slot;
    }
    c->componentowner = owner;
    c->componentfield = field;
    c->componentbase = t;
    auto ft = si->ftypes[field];
    if (IsClassRoot(owner)) {
        c->poolclass = owner->poolclass;
        c->growshrink = ContainsGrowShrink(ft);
        GrowShrinkElems(ft, c->gselems);
        ReachedThroughRefs(ft, c->classreach);
        vector<TypeExpr *> elems, open;
        ArrayElemsReached(ft, elems, open);
        for (auto e : elems)
            if (ElemArrayPlaces(ft, e) == 1) c->onearray.push_back(e);
    } else c->type = ft;
    c->name = ast.Intern(cat(owner->name, ".", si->st->fields[field].name));
    c->line = owner->line;
    c->depth = owner->depth;
    c->ownerspec = owner->ownerspec;
    c->isglobal = owner->isglobal;
    c->isvar = owner->isvar;
    c->assigned = c->maybeassigned = true;
    
    
    if (IsClassRoot(owner))
        if (auto it = classuses.find(owner); it != classuses.end()) {
            auto uses = it->second;
            for (auto &u : uses) {
                ClassUse projected;
                projected.roots = ProjectRoots(u.roots, t, field);
                NoteClassUse(c, projected);
            }
        }
    return c;
}

inline Roots TypeCheck::ProjectRoots(const Roots &parents, TypeExpr *t, int field) {
    Roots out;
    out.unknown = parents.unknown;
    for (auto a : parents.alts) {
        if (a.root && (IsClassRoot(a.root) ||
                      (a.root->type && TypeEq(ast.PlainOf(a.root->type), ast.PlainOf(t))))) {
            a.root = RootComponent(a.root, t, field);
            a.projected = true;
            a.slotread = a.classread = false;
            out.Add(a);
        } else {
            
            
            auto candidates = RootCandidates(t, Depth(a.root), !a.root || a.root->isglobal,
                                             true, false);
            Roots projected;
            projected.unknown = candidates.unknown;
            for (auto b : candidates.alts) {
                
                
                if (!b.root) {
                    if (!b.exact) projected.Add(b);
                    continue;
                }
                if (b.root == a.root) continue;
                if (IsClassRoot(b.root) || (b.root->type &&
                    TypeEq(ast.PlainOf(b.root->type), ast.PlainOf(t)))) {
                    b.root = RootComponent(b.root, t, field);
                    b.projected = true;
                    b.slotread = b.classread = false;
                    projected.Add(b);
                }
            }
            if (projected.None()) projected.Add({ a.root, false, a.from, a.slotread, a.classread });
            out.Add(projected);
        }
    }
    return out;
}


inline int TypeCheck::ClassParam(FnSpec *spec, VarDef *root, bool &view) {
    for (auto r = root; r; r = r->componentowner)
        for (size_t p = 0; p < spec->params.size(); p++) {
            view = ViewClassOf(spec, p) == r;
            if (spec->params[p]->ref.Root() == r || view) return (int)p;
        }
    return -1;
}

inline Roots TypeCheck::ProjectClass(VarDef *root, VarDef *base, const Roots &args) {
    if (root == base) return args;
    assert(root && root->componentowner);
    return ProjectRoots(ProjectClass(root->componentowner, base, args),
                        root->componentbase, root->componentfield);
}

inline Roots TypeCheck::MapClass(FnSpec *spec, VarDef *root, const vector<Val> &args) {
    bool view = false;
    auto p = ClassParam(spec, root, view);
    if (p < 0 || p >= (int)args.size()) return RootsOf(root);
    auto base = view ? ViewClassOf(spec, p) : spec->params[p]->ref.Root();
    return ProjectClass(root, base, view ? args[p].held.AsRoots()
                                        : ClassArgRoots(spec->argtypes[p], args[p]));
}

inline VarDef *TypeCheck::NewVar(string_view name, TypeExpr *type, Line l, bool isvar,
                                VarDef *previous) {
    auto vd = ResetLocal(previous);
    vd->name = name;
    vd->type = type;
    vd->line = l;
    vd->isvar = isvar;
    vd->depth = CurDepth();
    vd->ownerspec = frames.back().spec;
    vars.push_back(vd);
    return vd;
}






inline VarDef *TypeCheck::LookupVar(string_view name, string_view ns, Node *use) {
    auto top = (int)frames.size() - 1;
    for (auto i = (int)vars.size() - 1; i >= frames[top].varbase; i--)
        if (vars[i]->name == name) return vars[i];
    VarDef *found = nullptr;
    ForOuterVars(top, [&](VarDef *v) {
        if (v->name != name) return false;
        v->captured = true;
        found = v;
        return true;
    });
    if (found) {
        if (use) NoteEnvRead(found);
        return found;
    }
    if (use) DefaultScopeName(name, use, false);
    if (ScopeNameKind(name)) return nullptr;
    auto g = ast.LookupGlobal(name, ns);
    return g && !g->defs.empty() ? g->defs[0] : nullptr;
}





inline const char *TypeCheck::ScopeNameKind(string_view name) {
    const FnValBind *fn;
    if (LookupTypeParam(name, fn) || fn) return "type parameter";
    if (LookupLocalFn(name)) return "nested function";
    return nullptr;
}








inline void TypeCheck::DefaultScopeName(string_view name, Node *at, bool fnonly) {
    if (name.find("::") != string_view::npos) return;
    auto fi = (int)frames.size() - 1;
    while (fi >= 0 && !frames[fi].decl && !frames[fi].defaultfn) fi = frames[fi].lexframe;
    if (fi < 0 || !frames[fi].defaultfn) return;
    auto &fr = frames[fi];
    auto sf = fr.defaultfn;
    auto error = [&](const string &what) {
        Error(at, cat("the default of parameter ", sf->params[fr.defaultparam].name, " of ",
                      sf->qname, " names ", what, ", which a default cannot: it names what a "
                      "top-level declaration can (§7.1)"));
    };
    if (!fnonly)
        for (auto &p : sf->params)
            if (p.name == name) error(cat("parameter ", name));
    
    
    
    auto typeparam = [&](SFunction *f) {
        for (auto &g : f->generics) {
            if (g.name != name) continue;
            for (auto &[n, t] : fr.lexspec->bindings)
                if (n == name) return true;
            error(cat("function value ", name));
        }
        return false;
    };
    if (typeparam(sf)) return;
    if (fr.defaultsite) {
        if (!fnonly)
            for (auto v : fr.defaultsite->vars)
                if (v->name == name) error(cat(name, ", a local where ", sf->name, " is declared"));
        for (auto &o : fr.defaultsite->fns)
            if (o.sf->name == name) error(cat(name, ", a nested function"));
    }
    for (auto o = sf->outer; o; o = o->outer)
        if (typeparam(o)) return;
}









inline void TypeCheck::DeclareLocalFn(FnDecl *fd) {
    for (auto &p : fd->sf->params)
        if (p.type) ConstNamesIn(p.type);
    for (auto t : fd->sf->rets) ConstNamesIn(t);
    auto top = (int)frames.size() - 1;
    auto &fr = frames[top];
    auto &site = declsites.emplace_back();
    
    auto visible = [&](VarDef *v) {
        for (auto &g : fd->sf->generics)
            if (g.name == v->name) return false;
        return true;
    };
    for (auto i = (int)vars.size() - 1; i >= fr.varbase; i--)
        if (visible(vars[i])) site.vars.push_back(vars[i]);
    ForOuterVars(top, [&](VarDef *v) {
        if (visible(v)) site.vars.push_back(v);
        return false;
    });
    for (auto bp = blockpos.rbegin(); bp != blockpos.rend() && bp->scopeidx >= fr.scopebase; ++bp) {
        auto &stmts = bp->block->stmts;
        auto serial = scopes[bp->scopeidx].serial;
        auto at = std::min((int)bp->idx, (int)stmts.size() - 1);
        for (auto i = at; i >= 0; i--)
            if (auto d = Is<FnDecl>(stmts[i]))
                site.fns.push_back({ d->sf, fr.lexspec, serial, i });
        for (auto i = at + 1; i < (int)stmts.size(); i++)
            if (auto d = Is<FnDecl>(stmts[i]))
                site.fns.push_back({ d->sf, fr.lexspec, serial, i });
    }
    ForOuterFns(top, [&](const OuterFn &o) {
        site.fns.push_back(o);
        return false;
    });
    declsiteof[{ fr.lexspec, fd->sf }] = &site;
    localfns.push_back({ (int)scopes.size() - 1, fd->sf });
}




inline int TypeCheck::FrameOfSpec(FnSpec *sp) {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (frames[i].isfunval ? frames[i].lexspec == sp : frames[i].spec == sp) return i;
    return -1;
}






inline int TypeCheck::LexFrame(FnSpec *env) {
    auto fi = FrameOfSpec(env);
    while (fi < 0 && env && env->isfunval) fi = FrameOfSpec(env = env->lexparent);
    return fi;
}




inline int TypeCheck::LexFrame(FnSpec *env, SFunction *sf) {
    return env ? LexFrame(env) : sf->isnested ? 0 : -1;
}



inline int TypeCheck::LexFrame(const FnValBind &fb) {
    return fb.named ? LexFrame(fb.env, fb.named) : fb.env ? LexFrame(fb.env) : 0;
}



inline int TypeCheck::FrameOfScope(int s) {
    for (auto fi = (int)frames.size() - 1; fi > 0; fi--)
        if (frames[fi].scopebase <= s) return fi;
    return 0;
}




inline bool TypeCheck::NamesFrame(int fi, int target) {
    for (; fi >= 0; fi = frames[fi].lexframe)
        if (fi == target) return true;
    return false;
}



inline FnSpec *TypeCheck::NamedSpec(FnSpec *env) {
    while (env && env->isfunval) env = env->lexparent;
    return env;
}

inline bool TypeCheck::LookupLocalFn(string_view name) {
    FnSpec *env;
    return !LookupLocalFns(name, env).empty();
}

inline small_vector<int, 8> TypeCheck::NamedFrames(int fi, FnSpec *spec) {
    small_vector<int, 8> chain;
    auto add = [&](int k) {
        for (; k >= 0; k = frames[k].lexframe) {
            if (find(chain.begin(), chain.end(), k) != chain.end()) break;
            chain.push_back(k);
            if (k == 0) break;
        }
    };
    auto envs = [&](FnSpec *sp) {
        if (!sp) return;
        for (auto &fv : sp->fnvals) add(LexFrame(fv.second.env));
    };
    add(fi);
    envs(spec);
    for (size_t k = 0; k < chain.size(); k++) envs(frames[chain[k]].spec);
    if (find(chain.begin(), chain.end(), 0) == chain.end()) chain.push_back(0);
    sort(chain.begin(), chain.end());
    return chain;
}

inline TypeCheck::FlowState TypeCheck::SaveFlow() {
    FlowState f;
    EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
        auto v = vars[i];
        f.locals.push_back({ i, v, { v->assigned, v->maybeassigned, v->narrowed } });
    });
    
    for (auto v : narrowedglobals)
        if (v->narrowed) f.globals.push_back({ v, v->narrowed });
    f.reachable = reachable;
    f.kills = cur.kills;
    return f;
}

inline void TypeCheck::RestoreFlow(const FlowState &f) {
    for (auto &e : f.locals) {
        if (!InScope(e.var, e.index)) continue;
        e.var->assigned = e.state.assigned;
        e.var->maybeassigned = e.state.maybeassigned;
        e.var->narrowed = e.state.narrowed;
    }
    for (auto v : narrowedglobals) v->narrowed = nullptr;
    for (auto [v, narrowed] : f.globals) v->narrowed = narrowed;
    reachable = f.reachable;
    cur.kills = f.kills;
}

inline TypeCheck::FlowState TypeCheck::JoinFlow(const FlowState &a, const FlowState &b) {
    auto narrow = [&](TypeExpr *an, TypeExpr *bn) {
        return !a.reachable ? bn : !b.reachable ? an : an && bn ? an : nullptr;
    };
    
    
    
    auto at = [](const FlowState &f, size_t &p, const FlowEntry &e) -> VarFlow {
        while (p < f.locals.size() && f.locals[p].index < e.index) p++;
        if (p < f.locals.size() && f.locals[p].index == e.index && f.locals[p].var == e.var)
            return f.locals[p].state;
        return {};
    };
    auto joined = SaveFlow();
    size_t p = 0, q = 0;
    for (auto &e : joined.locals) {
        auto aa = at(a, p, e), bb = at(b, q, e);
        e.state = { (!a.reachable || aa.assigned) && (!b.reachable || bb.assigned),
                    (a.reachable && aa.maybeassigned) || (b.reachable && bb.maybeassigned),
                    narrow(aa.narrowed, bb.narrowed) };
    }
    
    if (!a.reachable) joined.globals = b.globals;
    else if (!b.reachable) joined.globals = a.globals;
    else {
        joined.globals.clear();
        for (auto &g : a.globals)
            for (auto &h : b.globals)
                if (h.first == g.first) joined.globals.push_back(g);
    }
    
    
    if (!a.reachable) joined.kills = b.kills;
    else if (!b.reachable) joined.kills = a.kills;
    else {
        joined.kills.clear();
        for (auto &k : a.kills)
            for (auto &l : b.kills)
                if (k.container == l.container && k.base == l.base && k.path == l.path)
                    joined.kills.push_back({ k.container, k.base, k.path, std::min(k.at, l.at) });
    }
    joined.reachable = a.reachable || b.reachable;
    return joined;
}

inline bool TypeCheck::SameFlow(const FlowState &a, const FlowState &b) {
    return a == b;
}

inline void TypeCheck::MergeFlow(const FlowState &a, const FlowState &b) {
    RestoreFlow(JoinFlow(a, b));
}



inline void TypeCheck::NarrowCond(Node *cond, bool sense) {
    if (auto id = Is<Ident>(cond)) {
        if (!id->vdef) return;
        auto t = id->vdef->type;
        if (t && t->kind == TY_REF && t->ref->optional && sense && !id->vdef->narrowed)
            SetNarrowed(id->vdef, NarrowedRef(t, cond->line));
        return;
    }
    if (auto u = Is<Unary>(cond)) {
        if (u->op == T_NOT) NarrowCond(u->child, !sense);
        return;
    }
    if (auto b = Is<Binary>(cond)) {
        if ((b->op == T_ANDAND && sense) || (b->op == T_OROR && !sense)) {
            NarrowCond(b->left, sense);
            for (auto v : b->rightkills) v->narrowed = nullptr;
            NarrowCond(b->right, sense);
            return;
        }
        
        if (b->op == T_EQ || b->op == T_NEQ) {
            auto other = Is<NullLit>(b->left) ? b->right
                                              : Is<NullLit>(b->right) ? b->left : nullptr;
            if (other) NarrowCond(other, b->op == T_NEQ ? sense : !sense);
        }
        return;
    }
}




inline void TypeCheck::NoteHolderBinding(VarDef *d, const Val &v, Node *at) {
    auto saved = fitnode;
    fitnode = at;
    RecordStore(d, ContentsOf(v), v.byteview, nullptr, v.holderfrom);
    fitnode = saved;
}






inline TypeExpr *TypeCheck::NarrowedRef(TypeExpr *t, Line l) {
    auto r = ast.RefTo(t->ref->sub, l);
    r->cq = t->cq;
    return r;
}




inline Val TypeCheck::MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at,
                                bool wantvalue) {
    if (!areach) return b;
    if (!breach) return a;
    Val v;
    
    
    
    
    
    
    
    
    auto isint = [&](const Val &x) { return x.type && IsIntT(LoadType(x.type)); };
    auto intlit = [&](const Val &x) {
        return x.unsized || x.ck == CK_INT || x.litint;
    };
    auto fltlit = [&](const Val &x) { return LitFloat(x) || isint(x); };
    auto adapt = [&](const Val &c, const Val &o) {
        if (!o.type || !c.type || TypeEq(c.type, o.type)) return false;
        if (o.type->kind == TY_FLT) {
            if (!LitFloat(c) || LitFloat(o)) return false;
            if (IsF32(o.type)) RelyOnNamed(c, at);
            return true;
        }
        if (o.type->kind != TY_INT || !isint(c) || intlit(o)) return false;
        if (c.unsized) {
            
            
            RecordLitAdapt(c, ast.NoIndex(o.type), at->line);
            return true;
        }
        if (!ConstsFit(c, o.type->intstorage)) return false;
        RelyOnNamed(c, at);
        return true;
    };
    
    
    
    if (a.type && b.type && IsUntypedEmptyArray(a.type) != IsUntypedEmptyArray(b.type) &&
        wantvalue)
        Error(at, "cannot infer the element type of []: a [] branch takes none from the other "
                  "branches (§6.4)");
    int64_t alo, ahi, blo, bhi;
    if (IntConsts(a, alo, ahi) && IntConsts(b, blo, bhi)) {
        
        RelyOnNamed(a, at);
        RelyOnNamed(b, at);
        v.type = TypeEq(a.type, b.type) ? a.type : ast.inttypes[IS_I64];
        v.litint = true;
        v.litlo = std::min(alo, blo);
        v.lithi = std::max(ahi, bhi);
        v.nonneg = v.litlo >= 0;
    } else if (fltlit(a) && fltlit(b) && (LitFloat(a) || LitFloat(b))) {
        v.type = !LitFloat(a) ? b.type : !LitFloat(b) ? a.type
                                        : UnifyBranch(a.type, b.type, at, wantvalue);
        v.litfloat = true;
        if (a.constfrom && b.constfrom) RelyOnNamed(b, at);
        v.constfrom = a.constfrom ? a.constfrom : b.constfrom;
    } else if (adapt(a, b)) {
        
        v.type = ast.NoIndex(b.type);
    } else if (adapt(b, a)) {
        v.type = ast.NoIndex(a.type);
    } else {
        v.type = UnifyBranch(a.type, b.type, at, wantvalue);
    }
    
    if (a.holderset || b.holderset) {
        v.holderset = true;
        v.contents = ContentsOf(a);
        v.contents.Add(ContentsOf(b));
    }
    v.isnull = a.isnull && b.isnull;   
    
    v.emptyarr = a.emptyarr && b.emptyarr;
    
    if (a.litelems && b.litelems && TypeEq(a.type, b.type) && TypeEq(v.type, a.type)) {
        v.litelems = true;
        v.litlo = std::min(a.litlo, b.litlo);
        v.lithi = std::max(a.lithi, b.lithi);
    }
    v.storagebranches = a.storagebranches && b.storagebranches;
    v.isvarint = a.isvarint && b.isvarint;
    v.implicitcopy = a.implicitcopy ? a.implicitcopy : b.implicitcopy;
    v.refcopies = a.refcopies;
    v.refcopies.insert(v.refcopies.end(), b.refcopies.begin(), b.refcopies.end());
    
    v.TakeAlts(a);
    v.Add(b);
    v.writable = a.writable && b.writable;
    v.reusable = a.reusable & b.reusable;
    v.byteview = a.byteview || b.byteview;
    v.freshview = a.freshview || b.freshview;
    return v;
}









inline Val TypeCheck::JoinBranches(const Val &a, bool areach, const Val &b, bool breach,
                                   Node *at, bool wantvalue, bool onjoin) {
    if (!areach) return b;
    if (!breach) return a;
    auto elem = [](const Val &x) -> TypeExpr * {
        if (!x.type) return nullptr;
        if (x.type->kind == TY_SLICE) return x.type->sub;
        if (x.type->kind == TY_ARRAY) return x.type->arr->sub;
        return nullptr;
    };
    auto isslice = [](const Val &x) { return x.type->kind == TY_SLICE && !x.joinslice; };
    auto ea = elem(a), eb = elem(b);
    auto joins = onjoin && wantvalue && ea && eb && TypeEq(ea, eb);
    
    if (joins && !a.joinslice && !b.joinslice && isslice(a) == isslice(b) &&
        (isslice(a) || TypeEq(a.type, b.type)))
        joins = false;
    if (!joins) {
        
        
        NoArrayJoin(a);
        NoArrayJoin(b);
        return MergeVals(a, areach, b, breach, at, wantvalue);
    }
    Val v;
    v.type = ast.SliceOf(ea, at->line);
    
    
    auto readonly = [](const Val &x) {
        return x.type->kind == TY_SLICE ? x.type->cq : !x.writable;
    };
    v.type->cq = readonly(a) || readonly(b);
    v.joinslice = true;
    v.joinhasslice = a.joinhasslice || b.joinhasslice || isslice(a) || isslice(b);
    if (a.joinslice || b.joinslice) {
        auto &j = a.joinslice && !a.joinhasslice ? a : b;
        v.joinat = j.joinat;
        v.joina = j.joina;
        v.joinb = j.joinb;
    } else {
        v.joinat = at;
        v.joina = a.type;
        v.joinb = b.type;
    }
    return v;
}





inline bool TypeCheck::CycleStorable(VarDef *r) {
    auto spec = CurRealFrame().spec;
    return !r || r->isglobal || r->poolclass ||
           (r->ownerspec && r->ownerspec != spec && !r->ownerspec->incycle &&
            !r->ownerspec->sf->isrec);
}


inline bool TypeCheck::CycleStorable(const Roots &r) {
    return r.All([&](const RootAlt &a) { return CycleStorable(a.root); });
}







inline void TypeCheck::CheckBranchRoot(const Val &v, int depth, Node *at, const char *construct) {
    auto t = v.type;
    if (!t || v.isnull) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    for (auto &a : roots.alts) {
        auto root = a.root;
        
        if (!root || root == temproot || Depth(root) <= depth + (IsTemp(root) ? 1 : 0))
            continue;
        auto what = !isrs ? "holds references" : t->kind == TY_SLICE ? "is a slice"
                                                                       : "is a reference";
        
        
        
        if (IsTemp(root))
            Error(at, cat("the ", construct, "'s value ", what, " rooted at a temporary, which "
                          "does not outlive it (§9.2): a temporary lasts until the end of its "
                          "statement, or of the block whose final expression made it; make the "
                          "value in a variable before the statement",
                          t->kind == TY_SLICE
                              ? ", or, where every branch makes a new array of one type, slice "
                                "the whole construct: `(if c { a } else { b })[..]`"
                              : "",
                          t->kind == TY_SLICE && IsU8(t->sub)
                              ? "; for byte-string branches (including mixed literals and "
                                "builders), declare an explicitly owned result: "
                                "`let text: u8[>..] = if c { a } else { b };`"
                              : ""));
        Error(at, cat("the ", construct, "'s value ", what, " rooted at ", root->name,
                      ", which does not outlive it (§9.2)"));
    }
}














inline Val TypeCheck::TempCopy(Val v) {
    if (v.type && ClassOf(v.type) == SC_ROOT)
        Error(v.type->line, "root owners cannot be copied into a temporary");
    if (!v.type || v.isnull || v.type->kind == TY_REF || v.type->kind == TY_VOID ||
        v.type->kind == TY_FN)
        return v;
    if (v.type->kind == TY_SLICE) {
        v.lvalue = false;
        v.slot = Prov {};
        v.hasslot = false;
        return v;
    }
    if (!v.holderset && HoldsPlainRef(v.type)) {
        
        
        v.contents = HeldAt(v);
        v.holderfrom = HolderSource(v);
        v.holderset = true;
    }
    v.Set(TempRoot(), false);
    v.reached = nullptr;
    v.lvalue = false;
    v.writable = false;
    return v;
}




inline void TypeCheck::RetypeBranches(Node *x, TypeExpr *t) {
    auto block = [&](Block *b) {
        if (!b || !b->tail || !b->exprtype || b->exprtype->kind == TY_VOID) return;
        b->exprtype = t;
        RetypeBranch(b->tail, t);
    };
    auto breaks = [&](vector<Break *> &bs) { for (auto br : bs) RetypeBranch(br->val, t); };
    if (auto b = Is<Block>(x)) {
        block(b);
    } else if (auto e = Is<EarlyBlock>(x)) {
        block(e->body);
        breaks(e->breaks);
    } else if (auto l = Is<LoopExpr>(x)) {
        breaks(l->breaks);
    } else if (auto i = Is<IfExpr>(x)) {
        block(i->thenb);
        if (i->elseb) RetypeBranch(i->elseb, t);
    } else if (auto m = Is<MatchExpr>(x)) {
        for (auto &arm : m->arms) RetypeBranch(arm.body, t);
    }
}







inline void TypeCheck::RetypeBranch(Node *&n, TypeExpr *t) {
    if (!n || !n->exprtype || n->exprtype->kind == TY_VOID) return;   
    if (Is<Block>(n) || Is<EarlyBlock>(n) || Is<IfExpr>(n) || Is<MatchExpr>(n) ||
        Is<LoopExpr>(n)) {
        n->exprtype = t;
        RetypeBranches(n, t);
        return;
    }
    auto nt = LoadType(n->exprtype);
    if (TypeEq(nt, t)) return;
    auto b = Is<Binary>(n);
    auto u = Is<Unary>(n);
    if (IsIntT(nt) && t->kind == TY_FLT) ToFloat(n, nt, t);
    else if (IsIntT(t) && ((b && b->flexint) || (u && u->flexint))) RetypeFlexInt(n, t);
    else if (IsIntT(nt) && IsIntT(t) && !ImplicitInt(nt->intstorage, t->intstorage))
        n->exprtype = t;
    else if (nt->kind == TY_FLT && IsF32(t)) RetypeFlex(n, t);
}













template<typename F> Val TypeCheck::CheckJoin(Node *x, TypeExpr *expected, F check) {
    if ((expected && expected->kind != TY_VOID) || joinpath == x) return check(expected);
    auto entry = SaveFlow();
    auto warnbase = cur.pendingwarnings.size();
    auto growbase = cur.growlog.size();
    Val v;
    {
        struct Probe {
            TypeCheck &tc;
            JoinPathScope js;
            Probe(TypeCheck &t, Node *n) : tc(t), js(t, n) { tc.cur.joinprobes++; }
            ~Probe() { tc.cur.joinprobes--; }
        } probe(*this, x);
        v = check(expected);
    }
    
    
    if (v.joinslice && !v.joinhasslice) {
        if (argpath != x) NoArrayJoin(v);
        if (!WarningsHeld()) FlushWarnings();
        return v;
    }
    if (v.joinslice) {
        RestoreFlow(entry);
        cur.pendingwarnings.resize(warnbase);
        
        
        cur.growlog.resize(growbase);
        
        
        DestScope ds(*this, Dest {});
        SlotScope ss(*this, false);
        return check(v.type);
    }
    if (!WarningsHeld()) FlushWarnings();
    
    if (v.implicitcopy && argpath != x) ImplicitCopyError(v.implicitcopy);
    
    
    if (v.type && (IsIntT(v.type) || v.type->kind == TY_FLT)) RetypeBranches(x, v.type);
    return TempCopy(v);
}

inline Val TypeCheck::CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    CheckCond(x->cond);
    auto entry = SaveFlow();
    NarrowCond(x->cond, true);
    Val tv;
    {
        PathScope ps(*this, onpath ? x->thenb : nullptr);
        JoinPathScope js(*this, onjoin ? x->thenb : nullptr);
        tv = CheckBlockVal(x->thenb, expected, wantvalue);
    }
    auto aflow = SaveFlow();
    RestoreFlow(entry);
    Val ev = VoidVal();
    NarrowCond(x->cond, false);
    PathScope ps(*this, onpath ? x->elseb : nullptr);
    JoinPathScope js(*this, onjoin ? x->elseb : nullptr);
    if (auto ei = Is<IfExpr>(x->elseb)) {
        
        
        NodeScope ns(*this, ei);
        ev = CheckIf(ei, expected, wantvalue);
    } else if (x->elseb) {
        ev = CheckBlockVal((Block *)x->elseb, expected, wantvalue);
    } else if (wantvalue) {
        Error(x, "an if used as a value requires an else branch");
    }
    
    
    if (x->elseb) x->elseb->exprtype = ev.type ? ev.type : ast.voidtype;
    auto bflow = SaveFlow();
    Node *last = nullptr;
    if (!blockpos.empty()) {
        auto b = blockpos.back().block;
        last = b->tail ? b->tail : b->stmts.empty() ? nullptr : b->stmts.back();
    }
    x->flat = last == x && x->elseb && entry.reachable && !bflow.reachable;
    RestoreFlow(entry);
    MergeFlow(aflow, bflow);
    if (!wantvalue) return VoidVal();
    auto v = JoinBranches(tv, aflow.reachable, ev, bflow.reachable, x, wantvalue, onjoin);
    
    return onjoin ? v : TempCopy(v);
}

inline Val TypeCheck::CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == b;
    auto onjoin = joinpath == b;
    PushScope(b->expansionboundary ? SK_EXPANSION : SK_PLAIN);
    BlockScope bs(*this, b);
    CheckStmts(b);
    Val v = VoidVal();
    if (b->tail) {
        if (wantvalue) {
            PathScope ps(*this, onpath ? b->tail : nullptr);
            JoinPathScope js(*this, onjoin ? b->tail : nullptr);
            v = CheckValue(b->tail, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(b->tail);
        }
    } else if (wantvalue && reachable && expected && expected->kind != TY_VOID) {
        Error(b, "block used as a value must end in an expression");
    }
    if (!reachable) v.type = nullptr;  
    PopScope();
    CheckBranchRoot(v, CurDepth(), b->tail, "block");
    b->exprtype = v.type ? v.type : ast.voidtype;
    return v;
}

inline Val TypeCheck::CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue) {
    
    
    ExpandArms(m->arms);
    if (m->arms.empty()) Error(m, "match has no arms: its const for generated none");
    auto onpath = argpath == m;
    auto onjoin = joinpath == m;
    Val sv;
    {
        FlagScope rs(inreturn, false);
        DestScope ds(*this, Dest {});
        sv = CheckV(m->scrutinee, nullptr);
    }
    
    
    if (IsPlainRef(sv.type) && IsIntT(LoadType(sv.type->ref->sub))) sv = DecayRef(sv);
    m->scrutinee->exprtype = sv.type;
    auto st = sv.type;
    TypeExpr *enumtype = nullptr;
    if (st->kind == TY_REF) {
        if (st->ref->optional)
            Error(m, "optional value must be narrowed (if/guard/assert), not matched");
        if (st->ref->sub->kind == TY_ENUM) enumtype = st->ref->sub;
    } else if (st->kind == TY_ENUM) {
        enumtype = st;
    }
    
    
    for (auto &arm : m->arms) {
        for (auto &pi : arm.pat.items) {
            auto owner = pi.variantowner;
            if (owner && (!enumtype || !SameAdt(owner, enumtype)))
                Error(arm.body, cat("const for variant ", ExprStr(pi.lo), " belongs to ",
                                    TypeStr(owner), ", not ", TypeStr(enumtype ? enumtype : st)));
        }
    }
    auto entry = SaveFlow();
    Val result;
    auto resultreach = false;
    auto first = true;
    FlowState acc;
    auto DoArm = [&](MatchArm &arm, VarDef *binder) {
        RestoreFlow(entry);
        PushScope(SK_PLAIN);
        if (binder) vars.push_back(binder);
        Val av;
        if (wantvalue) {
            PathScope ps(*this, onpath ? arm.body : nullptr);
            JoinPathScope js(*this, onjoin ? arm.body : nullptr);
            av = CheckValue(arm.body, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(arm.body);
        }
        auto aflow = SaveFlow();
        if (!reachable) av.type = nullptr;
        PopScope();
        CheckBranchRoot(av, CurDepth(), arm.body, "match arm");
        if (first) {
            result = av;
            resultreach = aflow.reachable;
            acc = aflow;
            first = false;
        } else {
            result = JoinBranches(result, resultreach, av, aflow.reachable, m, wantvalue, onjoin);
            resultreach = resultreach || aflow.reachable;
            
            acc = JoinFlow(acc, aflow);
        }
    };
    for (size_t i = 0; i + 1 < m->arms.size(); i++)
        if (m->arms[i].pat.kind == P_WILDCARD)
            Error(m->arms[i + 1].body, "_ must be the last match arm");
    if (enumtype) {
        auto inst = GetEnumInst(enumtype);
        auto en = inst->en;
        vector<bool> covered(en->variants.size(), false);
        auto haswild = false;
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) {
                haswild = true;
                DoArm(arm, nullptr);
                continue;
            }
            arm.variants.clear();
            for (auto &pi : arm.pat.items) {
                auto id = Is<Ident>(pi.lo);
                if (!id || pi.hi || id->name.find("::") != string_view::npos)
                    Error(arm.body, "ADT match arms are variant names (or _)");
                auto found = en->FindVariant(id->name);
                if (!found && en->broken) throw Abandoned {};
                if (!found)
                    Error(arm.body, cat("enum ", en->name, " has no variant named ", id->name));
                auto vi = en->VariantIndex(found);
                if (covered[vi]) {
                    auto &vs = arm.variants;
                    Error(arm.body, find(vs.begin(), vs.end(), found) != vs.end()
                                        ? cat("variant ", id->name, " is listed twice")
                                        : cat("variant ", id->name, " is already matched by "
                                              "an earlier arm"));
                }
                covered[vi] = true;
                arm.variants.push_back(found);
            }
            
            auto found = arm.variants[0];
            VarDef *binder = nullptr;
            if (!arm.pat.binder.empty()) {
                
                
                
                if (!found->has_payload && !arm.generated)
                    Error(arm.body, cat("variant ", found->name, " has no payload to bind"));
                auto vt = ast.VariantTypeOf(enumtype, found, m->line);
                
                
                
                if (ClassOf(vt) == SC_RESIZABLE)
                    Error(arm.body, "binding a resizable ADT payload is not supported by "
                                    "the C backend yet; match its tag without a payload "
                                    "binder, or use a standalone resizable struct");
                binder = ResetLocal(arm.binder);
                binder->name = arm.pat.binder;
                binder->line = m->line;
                binder->depth = CurDepth() + 1;
                binder->ownerspec = frames.back().spec;
                binder->assigned = binder->maybeassigned = true;
                if (arm.pat.byref) {
                    
                    
                    
                    
                    
                    
                    if (!enumtype->enu->varmode)
                        Error(arm.body, cat("cannot bind the payload of fixed-mode ",
                                            enumtype->enu->en->name, " by reference "
                                            "(§3.5); bind by value: ", found->name,
                                            " ", arm.pat.binder));
                    if (ClassOf(enumtype) == SC_RESIZABLE)
                        Error(arm.body, cat("cannot bind the payload of resizable ",
                                            enumtype->enu->en->name, " by reference: a "
                                            "whole assignment may replace its variant "
                                            "(§3.5); bind by value: ", found->name,
                                            " ", arm.pat.binder));
                    binder->type = ast.RefTo(vt, m->line);
                    BindProv(binder, sv);
                    ReadBack held;
                    if (TempContents(sv, held)) NoRelRefTempRead(m->scrutinee, enumtype, vt);
                } else {
                    if (HasRelRefT(vt))
                        Error(arm.body, cat("payload of ", found->name, " contains "
                                            "self-relative references; bind it by reference "
                                            "(&", arm.pat.binder, ")"));
                    binder->type = vt;  
                    binder->isvar = false;
                    binder->copybind = true;
                    if (HoldsPlainRef(vt)) {
                        
                        
                        
                        
                        
                        ReadBack contents;
                        auto intemp = TempContents(sv, contents);
                        Roots held = intemp ? contents.roots : HeldAt(sv, true);
                        if (!intemp)
                            for (auto &a : held.alts) a.slotread = true;
                        auto saved = fitnode;
                        fitnode = m;
                        RecordStore(binder, held, sv.byteview, nullptr,
                                    intemp ? contents.from : HolderSource(sv));
                        fitnode = saved;
                    }
                }
                arm.binder = binder;
            }
            DoArm(arm, binder);
        }
        if (!haswild)
            for (size_t i = 0; i < en->variants.size(); i++)
                if (!covered[i])
                    Error(m, cat("match does not cover variant ", en->name, ".",
                                 en->variants[i].name));
    } else if (IsIntT(LoadType(st))) {
        auto sit = LoadType(st)->intstorage;
        auto haswild = false;
        
        
        
        auto PatternValue = [&](Node *n, bool &uns) {
            auto u = Is<Unary>(n);
            if (auto id = Is<Ident>(u ? u->child : n)) {
                ConstName(id, "match pattern");
                auto g = ast.LookupGlobal(id->name, id->ns);
                if (!g) Error(n, cat("unknown constant ", id->name, " in an integer match"));
                if (g->isvar)
                    Error(n, cat("match pattern ", id->name, " names a var global, "
                                 "not a constant"));
            }
            Val v;
            bool literal;
            set<VarDecl *> visiting;
            ConstUse use { n, "match pattern" };
            if (!ConstIntValue(n, v, literal, visiting, &use))
                Error(n, "constant integer expression expected for match pattern");
            
            
            if (v.notconst && FitsIntStorage(v.litlo, false, sit)) {
                v.ival = WrapStorage(v.ival, sit);
                v.uns = sit == IS_U64 && v.ival < 0;
            }
            uns = v.uns;
            return v.ival;
        };
        
        
        auto key = [&](int64_t v) {
            return sit == IS_U64 ? (uint64_t)v : (uint64_t)v ^ (1ull << 63);
        };
        auto written = [&](const PatItem &pi) {
            return pi.hi ? cat(ExprStr(pi.lo), "..", ExprStr(pi.hi)) : ExprStr(pi.lo);
        };
        
        
        auto literal = [](const PatItem &pi) {
            return Is<IntLit>(pi.lo) && (!pi.hi || Is<IntLit>(pi.hi));
        };
        auto shown = [&](const PatItem &pi, const ArmRange &r) {
            if (literal(pi)) return written(pi);
            auto num = [&](int64_t v) {
                return sit == IS_U64 ? to_string((uint64_t)v) : to_string(v);
            };
            return cat(written(pi), " (", num(r.lo),
                       pi.hi ? cat("..", num((int64_t)((uint64_t)r.hi + 1))) : "", ")");
        };
        
        
        vector<pair<uint64_t, uint64_t>> earlier;
        for (auto &arm : m->arms) {
            if (arm.pat.kind == P_WILDCARD) { haswild = true; DoArm(arm, nullptr); continue; }
            if (!arm.pat.binder.empty())
                Error(arm.pat.items[0].lo, cat("constant pattern ", ExprStr(arm.pat.items[0].lo),
                                               " has no payload to bind"));
            arm.ranges.clear();
            for (auto &pi : arm.pat.items) {
                auto ul = false, uh = false;
                ArmRange r;
                r.lo = PatternValue(pi.lo, ul);
                auto end = pi.hi ? PatternValue(pi.hi, uh) : r.lo;
                
                
                if (sit != IS_U64) {
                    if (!FitsIntStorage(r.lo, ul, sit) || (pi.hi && !FitsIntStorage(end, uh, sit)))
                        Error(arm.body, cat("match pattern does not fit the scrutinee "
                                            "type ", TypeStr(st)));
                }
                
                
                r.hi = r.lo;
                if (pi.hi) {
                    if (sit == IS_U64 ? (uint64_t)end <= (uint64_t)r.lo : end <= r.lo)
                        Error(arm.body, "empty range in match pattern");
                    r.hi = (int64_t)((uint64_t)end - 1);
                }
                auto klo = key(r.lo), khi = key(r.hi);
                for (auto &[a, b] : earlier)
                    if (a <= klo && khi <= b)
                        Error(pi.lo, cat("match pattern ", shown(pi, r), " never matches: "
                                         "earlier arms match all of its values"));
                
                
                for (size_t j = 0; j < arm.ranges.size(); j++) {
                    auto &pj = arm.pat.items[j];
                    auto &rj = arm.ranges[j];
                    if (std::max(klo, key(rj.lo)) > std::min(khi, key(rj.hi))) continue;
                    if (written(pi) == written(pj))
                        Error(pi.lo, cat("match pattern ", written(pi), " is listed twice"));
                    if (literal(pi) && literal(pj))
                        Error(pi.lo, cat("match patterns ", written(pj), " and ", written(pi),
                                         " overlap"));
                }
                arm.ranges.push_back(r);
            }
            for (auto &r : arm.ranges) earlier.push_back({ key(r.lo), key(r.hi) });
            sort(earlier.begin(), earlier.end());
            vector<pair<uint64_t, uint64_t>> merged;
            for (auto &kr : earlier) {
                if (merged.empty() || (merged.back().second != UINT64_MAX &&
                                       kr.first > merged.back().second + 1))
                    merged.push_back(kr);
                else
                    merged.back().second = std::max(merged.back().second, kr.second);
            }
            earlier = std::move(merged);
            DoArm(arm, nullptr);
        }
        if (!haswild) Error(m, "integer match requires a _ arm");
    } else {
        Error(m, cat("cannot match on a value of type ", TypeStr(st)));
    }
    if (!first) RestoreFlow(acc);
    reachable = resultreach;
    if (!wantvalue) return VoidVal();
    return onjoin ? result : TempCopy(result);
}

inline Val TypeCheck::CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue) {
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    x->breaks.clear();
    PushScope(SK_BLOCK, x);
    if (wantvalue) {
        scopes.back().breakexpected = expected;
        scopes.back().breakdest = curdst;
        scopes.back().onargpath = onpath;
        scopes.back().onjoinpath = onjoin;
        scopes.back().inreturn = inreturn;
    }
    BlockScope bs(*this, x->body);
    CheckStmts(x->body);
    Val v = VoidVal();
    if (x->body->tail) {
        if (wantvalue) {
            PathScope ps(*this, onpath ? x->body->tail : nullptr);
            JoinPathScope js(*this, onjoin ? x->body->tail : nullptr);
            v = CheckValue(x->body->tail, expected, false, CopiesBranch(expected));
        } else {
            CheckStmtExpr(x->body->tail);
        }
    }
    if (!reachable) v.type = nullptr;
    auto sc = scopes.back();
    PopScope();
    x->body->exprtype = v.type ? v.type : ast.voidtype;
    JoinBreakFlow(sc, true);
    reachable = reachable || sc.hasbreak;  
    if (!wantvalue) return VoidVal();
    CheckBranchRoot(v, CurDepth(), x->body->tail, "block");
    if (sc.breaktype) CheckBranchRoot(sc.breakvalue, CurDepth(), x, "block");
    Val r = sc.breaktype ? JoinBranches(v, v.type != nullptr, sc.breakvalue, true, x, wantvalue,
                                        onjoin) : v;
    if (!r.type) r.type = ast.voidtype;
    return onjoin ? r : TempCopy(r);
}



inline void TypeCheck::CheckLoopBody(Block *body) {
    BlockScope bs(*this, body);
    CheckStmts(body);
    if (body->tail) CheckStmtExpr(body->tail);
}














inline TypeCheck::Scope TypeCheck::CheckLoopPasses(Node *x, FlowState &head,
                                                   const function<void()> &pass) {
    auto entry = head;
    auto firstbase = storeevents.size();
    auto warnbase = cur.pendingwarnings.size();
    auto settled = false;
    Scope sc;
    
    
    
    auto maxpasses = 16 + (int)head.locals.size();
    for (auto passes = 0;; passes++) {
        if (passes >= maxpasses)
            Error(x, cat("this loop's checking does not settle (internal limit of ", maxpasses,
                         " passes)"));
        RestoreFlow(head);
        cur.pendingwarnings.resize(warnbase);
        cur.looppasses.push_back({ (int)scopes.size(), firstbase, storeevents.size(), settled });
        PushScope(SK_LOOP, x);
        pass();
        auto &s = scopes.back();
        if (reachable) {
            s.backedge = s.backedges ? JoinFlow(s.backedge, SaveFlow()) : SaveFlow();
            s.backedges = true;
        }
        sc = s;
        PopScope();
        auto lp = cur.looppasses.back();
        cur.looppasses.pop_back();
        auto next = sc.backedges ? JoinFlow(entry, sc.backedge) : entry;
        auto changed = lp.changed || !SameFlow(next, head);
        head = next;
        if (changed) continue;
        if (settled || !(lp.sawunbound || lp.deferred)) break;
        settled = true;
    }
    RestoreFlow(head);
    if (!WarningsHeld()) FlushWarnings();
    return sc;
}

inline Val TypeCheck::CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue) {
    auto head = SaveFlow();
    auto onpath = argpath == x;
    auto onjoin = joinpath == x;
    auto sc = CheckLoopPasses(x, head, [&] {
        x->breaks.clear();
        if (wantvalue) {
            scopes.back().breakexpected = expected;
            scopes.back().breakdest = curdst;
            scopes.back().onargpath = onpath;
            scopes.back().onjoinpath = onjoin;
            scopes.back().inreturn = inreturn;
        }
        CheckLoopBody(x->body);
    });
    JoinBreakFlow(sc, false);
    reachable = sc.hasbreak;  
    if (!wantvalue || !sc.breaktype) return VoidVal();
    CheckBranchRoot(sc.breakvalue, CurDepth(), x, "loop");
    return onjoin ? sc.breakvalue : TempCopy(sc.breakvalue);
}

inline void TypeCheck::CheckWhile(While *x) {
    
    
    
    
    auto head = SaveFlow();
    FlowState exit;
    auto sc = CheckLoopPasses(x, head, [&] {
        CheckCond(x->cond);
        exit = SaveFlow();
        NarrowCond(x->cond, true);
        CheckLoopBody(x->body);
    });
    RestoreFlow(exit);
    JoinBreakFlow(sc, true);
}

inline void TypeCheck::CheckFor(ForLoop *x) {
    FlagScope rs(inreturn, false);   
    auto byref = x->byref;
    TypeExpr *bindtype = nullptr;
    TypeExpr *elemtype = nullptr;   
    Prov iterprov;   
    ReadBack contents;   
    auto intemp = false;
    
    auto written = [&](TypeExpr *te, const char *what) -> TypeExpr * {
        if (!te) return nullptr;
        auto t = Subst(te);
        ValidateType(t, x->line, VT_LOCAL);
        if (!IsIntT(t))
            Error(x, cat("a for loop's ", what, " takes an integer type, not ", TypeStr(t)));
        return t;
    };
    auto vartype = written(x->vartype, "binder");
    auto idxtype = written(x->idxtype, "index binder");
    
    
    
    
    
    
    
    
    
    auto pastend = [&](Node *end) {
        if (IntBits(vartype->intstorage) >= 64) return false;
        auto named = false;
        ConstExprNames(end, nullptr, [&](Ident *, const char *) { named = true; });
        Val c;
        bool literal;
        set<VarDecl *> visiting;
        return !named && ConstIntValue(end, c, literal, visiting) && literal && !c.uns &&
               c.ival == IntRange(vartype->intstorage).second + 1;
    };
    if (auto r = Is<RangeExpr>(x->iter)) {
        x->iterkind = IK_RANGE;
        if (x->byref) Error(x, "cannot iterate an integer range by reference");
        if (vartype) {
            
            
            CheckValue(r->lo, vartype);
            if (pastend(r->hi)) {
                CheckIntAny(r->hi);
                r->exprtype = r->hi->exprtype = ast.inttypes[IS_I64];
            } else {
                CheckValue(r->hi, vartype);
                r->exprtype = vartype;
            }
            bindtype = vartype;
        } else {
            auto lo = CheckIntAny(r->lo);
            auto hi = CheckIntAny(r->hi);
            
            
            
            auto lot = r->lo->exprtype, hit = r->hi->exprtype;
            auto arr = IsIndexT(lot) && IsIndexT(hit) && lot->ix->arr == hit->ix->arr
                           ? lot->ix->arr : nullptr;
            auto ct = UnifyNumeric(r, T_DOTDOT, lo, hi, lo.type, hi.type);
            RetypeOperands(r->lo, r->hi, lo, hi, ct);
            r->exprtype = ct;
            bindtype = ct;
            if (arr && IsIntT(ct) && IsUnsigned(ct->intstorage))
                bindtype = ast.IndexTypeOf(ct->intstorage, arr, false, x->line);
        }
    } else {
        auto iv = CheckV(x->iter, nullptr);
        NoUntypedEmptyArray(iv, x->iter, "a for loop");
        NoTemporaryLiteral(x->iter, iv.type);
        x->iter->exprtype = iv.type;
        auto t = iv.type;
        iterprov = iv;
        intemp = TempContents(iv, contents);
        if (t->kind == TY_REF && !t->ref->optional) {
            t = t->ref->sub;  
            iterprov.ClearReads();   
            if (t->kind == TY_SLICE) iterprov = SlotView(iv, t);
        }
        t = LoadType(t);
        RequireComplete(t, x->line);
        if (IsIntT(t)) {
            x->iterkind = IK_COUNT;
            if (x->byref) Error(x, "cannot iterate an integer count by reference");
            
            
            auto nv = DecayRef(iv);
            x->iter->exprtype = nv.type;
            
            bindtype = t->ix ? ast.IndexTypeOf(t->intstorage, t->ix->arr, false, x->line) : t;
            if (vartype) {
                
                if (!pastend(x->iter)) {
                    MustFit(nv, x->iter, vartype);
                    if (nv.litint) RetypeBranches(x->iter, vartype);
                    x->iter->exprtype = vartype;
                }
                bindtype = vartype;
            }
        } else if (t->kind == TY_ARRAY || t->kind == TY_SLICE) {
            if (vartype)
                Error(x, cat("an array's element binds at its element type; a type on a for "
                             "loop's binder is for an integer range or count"));
            auto elem = t->kind == TY_ARRAY ? t->arr->sub : t->sub;
            elemtype = elem;
            if (intemp) NoRelRefTempRead(x->iter, t, elem);
            x->iterkind = t->kind == TY_ARRAY ? IK_ARRAY : IK_SLICE;
            
            if (x->byref && ClassOf(elem) != SC_FIXED) {
                Warn(x, "redundant &: elements of this type bind by reference without it "
                        "(§4.1)");
            }
            byref = x->byref || ClassOf(elem) != SC_FIXED;
            
            
            if (byref && elem->kind != TY_REF) {
                bindtype = ast.RefTo(elem, x->line);
            } else {
                
                
                
                
                if (HasRelRefT(elem) && elem->kind != TY_REF)
                    Error(x, "elements containing relative references require the "
                             "&x binding form (copies are not supported)");
                bindtype = LoadType(elem);
            }
        } else {
            Error(x, cat("cannot iterate a value of type ", TypeStr(t)));
        }
        
        
        
        if (idxtype && (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE)) {
            auto len = int64_t(1) << 48;
            if (t->kind == TY_ARRAY &&
                (t->arr->akind == A_FIXED || (t->arr->akind == A_LIMITED && t->arr->sizeexpr)))
                len = ArraySize(t->arr);
            if (len > 0 && !FitsIntStorage(len - 1, false, idxtype->intstorage))
                Error(x, cat("an index into ", TypeStr(t), " can reach ",
                             len == int64_t(1) << 48 ? string("2^48 - 1") : cat(len - 1),
                             ", which ", TypeStr(idxtype), " does not hold; bind the index as "
                             "an i64 and convert it with `as` (§6.5)"));
        }
    }
    if (idxtype && x->iterkind != IK_ARRAY && x->iterkind != IK_SLICE)
        Error(x, "the index of a range or count counts its iterations as an i64; a type on "
                 "the loop's binder gives the values their type");
    
    
    
    
    
    
    auto readback = IsRefOrSlice(bindtype) && elemtype && (!byref || elemtype->kind == TY_REF);
    if (readback) iterprov.writable = SlotLoadWritable(elemtype, iterprov.writable);
    auto seqprov = iterprov;
    auto head = SaveFlow();
    auto sc = CheckLoopPasses(x, head, [&] {
        if (readback) {
            auto slotread = SlotReadable(elemtype);
            auto rb = ReadBackRoot(elemtype, seqprov, seqprov.byteview,
                                   intemp ? &contents : nullptr, slotread, true);
            iterprov.TakeAlts(rb);
            for (auto &a : iterprov.alts) a.slotread = slotread;
        }
        auto vd = NewVar(x->var, bindtype, x->line, false, x->vdef);
        vd->assigned = vd->maybeassigned = true;
        vd->copybind = (x->iterkind == IK_ARRAY || x->iterkind == IK_SLICE) && !byref;
        if (!IsRefOrSlice(bindtype) && HoldsPlainRef(bindtype)) {
            
            
            
            
            Roots held = intemp ? contents.roots : HeldAt(iterprov, true);
            if (!intemp)
                for (auto &a : held.alts) a.slotread = true;
            auto saved = fitnode;
            fitnode = x;
            RecordStore(vd, held, iterprov.byteview, nullptr,
                        intemp ? contents.from : HolderSource(iterprov));
            fitnode = saved;
        }
        if (IsRefOrSlice(bindtype)) BindProv(vd, iterprov);
        x->vdef = vd;
        if (!x->idxvar.empty()) {
            auto idx = NewVar(x->idxvar, idxtype ? idxtype : ast.inttypes[IS_I64], x->line,
                              false, x->idxdef);
            idx->assigned = idx->maybeassigned = true;
            x->idxdef = idx;
        }
        CheckLoopBody(x->body);
    });
    JoinBreakFlow(sc, true);
}

inline int TypeCheck::RealFrameIndex() {
    for (auto i = (int)frames.size() - 1; i >= 0; i--)
        if (!frames[i].isfunval) return i;
    return 0;
}

inline int TypeCheck::FindBreakScope(bool forcontinue) {
    for (auto i = (int)scopes.size() - 1; i >= frames.back().scopebase; i--) {
        if (scopes[i].kind == SK_LOOP) return i;
        if (!forcontinue && scopes[i].kind == SK_BLOCK) return i;
        if (scopes[i].kind == SK_FN) break;
        if (scopes[i].kind == SK_EXPANSION) return -2;
        if (scopes[i].kind == SK_DEFER) return -3;
    }
    return -1;
}



inline void TypeCheck::JumpLeavesDefer(Node *at, const char *what) {
    Error(at, cat(what, " cannot leave a defer body (§6.6)"));
}

inline void TypeCheck::CheckBreak(Break *b) {
    auto si = FindBreakScope(false);
    if (si == -2) Error(b, "break cannot cross a const for expansion boundary");
    if (si == -3) JumpLeavesDefer(b, "break");
    if (si < 0) Error(b, "break outside of a loop or block");
    if (b->val) {
        if (!Is<LoopExpr>(scopes[si].node) && !Is<EarlyBlock>(scopes[si].node))
            Error(b, "break with a value exits loop/block only");
        if (scopes[si].valuelessbreak)
            Error(b, "this construct mixes valueless and valued breaks");
        
        
        
        
        
        
        
        
        auto be = scopes[si].breakexpected;
        auto prior = scopes[si].breakvalue;
        auto agree = scopes[si].breaktype && !prior.joinslice && !prior.litint && !prior.litfloat;
        auto expected = agree ? prior.type : be;
        auto unstored = agree && CopiesBranch(be) && IsRefOrSlice(expected);
        Val v;
        {
            
            PathScope ps(*this, scopes[si].onargpath ? b->val : nullptr);
            JoinPathScope js(*this, scopes[si].onjoinpath ? b->val : nullptr);
            FlagScope rs(inreturn, scopes[si].inreturn);
            DestScope ds(*this, unstored ? Dest {} : scopes[si].breakdest);
            v = CheckValue(b->val, expected, false, CopiesBranch(be));
        }
        
        
        
        
        
        Val exit;
        exit.SetProv(v);
        exit.type = v.type;
        if (IntConsts(v, exit.litlo, exit.lithi)) {
            exit.litint = true;
            exit.nonneg = exit.litlo >= 0;
        }
        exit.litfloat = LitFloat(v);
        exit.constfrom = v.constfrom;
        exit.isnull = v.isnull;
        exit.contents = v.contents;
        exit.holderset = v.holderset;
        exit.holderfrom = v.holderfrom;
        exit.storagebranches = v.storagebranches;
        exit.isvarint = v.isvarint;
        exit.implicitcopy = v.implicitcopy;
        exit.refcopies = v.refcopies;
        exit.joinslice = v.joinslice;
        exit.joinhasslice = v.joinhasslice;
        exit.joinat = v.joinat;
        exit.joina = v.joina;
        exit.joinb = v.joinb;
        exit.fnv = v.fnv;  
        
        
        if (v.emptyarr) Error(b->val, "cannot infer array element type");
        
        
        
        auto &sc = scopes[si];
        sc.breakvalue = JoinBranches(sc.breakvalue, sc.breaktype != nullptr, exit, true, b, true,
                                     sc.onjoinpath);
        if (!sc.breaktype) sc.breaktype = v.type;
        if (auto e = Is<EarlyBlock>(sc.node)) e->breaks.push_back(b);
        else if (auto l = Is<LoopExpr>(sc.node)) l->breaks.push_back(b);
    } else {
        auto &sc = scopes[si];
        if (sc.breaktype)
            Error(b, "this construct mixes valueless and valued breaks");
        sc.valuelessbreak = true;
    }
    NoteBreak(si);
    reachable = false;
}



inline void TypeCheck::NoteBreak(int si) {
    auto &sc = scopes[si];
    sc.hasbreak = true;
    if (!reachable) return;
    sc.breakflow = sc.breakflows ? JoinFlow(sc.breakflow, SaveFlow()) : SaveFlow();
    sc.breakflows = true;
}






inline void TypeCheck::JoinBreakFlow(const Scope &sc, bool ownexit) {
    if (!sc.breakflows) return;
    if (ownexit) MergeFlow(SaveFlow(), sc.breakflow);
    else RestoreFlow(sc.breakflow);
}



inline void TypeCheck::CheckContinue(Node *n) {
    auto si = FindBreakScope(true);
    if (si == -2) Error(n, "continue cannot cross a const for expansion boundary");
    if (si == -3) JumpLeavesDefer(n, "continue");
    if (si < 0) Error(n, "continue outside of a loop");
    auto &sc = scopes[si];
    sc.backedge = sc.backedges ? JoinFlow(sc.backedge, SaveFlow()) : SaveFlow();
    sc.backedges = true;
    reachable = false;
}




















inline void TypeCheck::CheckDefer(Defer *d) {
    auto &real = CurRealFrame();
    if (frames.back().isdefault || !real.sf || !real.spec)
        Error(d, "defer outside of a function: a global initializer has no scope exit (§6.6)");
    real.spec->hasdefer = true;
    auto saved = SaveFlow();
    for (auto &e : saved.locals)
        if (e.var->isvar) e.var->narrowed = nullptr;
    for (auto g : ast.globals)
        for (auto v : g->defs)
            if (v->isvar) v->narrowed = nullptr;
    PushScope(SK_DEFER, d);
    deferbodies++;
    CheckBlockVal(d->body, nullptr, false);
    deferbodies--;
    PopScope();
    RestoreFlow(saved);
    d->exprtype = ast.voidtype;
}





inline bool TypeCheck::InDeferBody() {
    for (auto fi = (int)frames.size() - 1; fi >= 0;) {
        auto hi = fi + 1 < (int)frames.size() ? frames[fi + 1].scopebase : (int)scopes.size();
        for (auto i = hi - 1; i >= frames[fi].scopebase; i--)
            if (scopes[i].kind == SK_DEFER) return true;
        if (!frames[fi].isfunval) break;
        fi = frames[fi].lexframe;
    }
    return false;
}








inline void TypeCheck::NoteDeferMention(VarDef *v) {
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi = frames[fi].lexframe) {
        auto hi = fi + 1 < (int)frames.size() ? frames[fi + 1].scopebase : (int)scopes.size();
        for (auto i = hi - 1; i >= frames[fi].scopebase && i >= Depth(v); i--) {
            if (scopes[i].kind != SK_DEFER) continue;
            auto &m = defermentions[(Defer *)scopes[i].node];
            if (find(m.begin(), m.end(), v) == m.end()) m.push_back(v);
        }
    }
}




























inline void TypeCheck::CheckDefers() {
    vector<FnSpec *> withdefers;
    for (auto sp : ast.fnspecs)
        if (sp->hasdefer && sp->body && !sp->failed) withdefers.push_back(sp);
    if (withdefers.empty()) return;
    
    
    
    unordered_map<FnSpec *, vector<FnSpec *>> callees;
    unordered_map<FnSpec *, FnSpec *> rfvia;
    auto add = [&](vector<FnSpec *> &to, FnSpec *k) {
        if (k && find(to.begin(), to.end(), k) == to.end()) to.push_back(k);
    };
    function<void(Node *, vector<FnSpec *> &)> collect = [&](Node *n, vector<FnSpec *> &to) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (c->builtin < 0) add(to, c->spec);
            for (auto k : c->dispatch) add(to, k);
            for (auto &[t, k] : c->fmtspecs) add(to, k);
        }
        RunChildren(n, [&](Node *ch) { collect(ch, to); });
    };
    for (auto sp : ast.fnspecs) {
        if (!sp->body) continue;
        collect(sp->body, callees[sp]);
        if (!sp->needs.empty()) rfvia[sp] = sp;
    }
    for (auto changed = true; changed;) {
        changed = false;
        for (auto &[sp, ks] : callees) {
            if (rfvia.count(sp)) continue;
            for (auto k : ks) {
                if (!rfvia.count(k)) continue;
                rfvia[sp] = k;
                changed = true;
                break;
            }
        }
    }
    for (auto sp : withdefers) {
        
        vector<const VarDef *> named;
        for (size_t j = 0; j < sp->rets.size(); j++)
            if (ClassOf(sp->rets[j]) != SC_FIXED)
                if (auto v = faloya::NamedResultOf(sp->body, sp->sf, sp->rets.size(), j))
                    named.push_back(v);
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            if (auto d = Is<Defer>(n)) {
                CheckItem([&] {
                    vector<FnSpec *> ks;
                    collect(d->body, ks);
                    for (auto k : ks) {
                        if (!rfvia.count(k)) continue;
                        
                        
                        string chain;
                        for (auto s = k; rfvia[s] != s; s = rfvia[s])
                            Append(chain, " calls ", rfvia[s]->sf->qname, ", which");
                        Error(d, cat("defer body calls ", k->sf->qname, ", which", chain,
                                     " uses a long-distance return (a `return ... from` site, a "
                                     "frame one passes through, or a catch, §7.9): a defer body "
                                     "runs while such a return may be in flight (§6.6)"));
                    }
                    auto isnamed = [&](const VarDef *v) {
                        return find(named.begin(), named.end(), v) != named.end();
                    };
                    auto why = [&](const VarDef *v) {
                        return cat(v->name, ", which ", sp->sf->qname, " returns by name and so "
                                   "builds at its destination (§7.3): a defer runs after the value "
                                   "is complete (§6.6)");
                    };
                    if (auto it = defermentions.find(d); it != defermentions.end())
                        for (auto v : it->second)
                            if (isnamed(v)) Error(d, cat("defer body names ", why(v)));
                    for (auto k : ks)
                        for (auto &r : k->envreads)
                            if (isnamed(r.var))
                                Error(d, cat("defer body calls ", k->sf->qname, ", which names ",
                                             why(r.var)));
                });
            }
            RunChildren(n, walk);
        };
        walk(sp->body);
    }
}







inline void TypeCheck::CheckStmtExpr(Node *n) {
    DestScope ds(*this, Dest {});
    FlagScope rs(inreturn, false);
    NodeScope ns(*this, n);
    if (auto x = Is<IfExpr>(n)) { CheckIf(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<Block>(n)) {
        CheckBlockVal(x, nullptr, false);
        n->exprtype = ast.voidtype;
        return;
    }
    if (auto x = Is<MatchExpr>(n)) { CheckMatch(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<EarlyBlock>(n)) { CheckEarlyBlock(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    if (auto x = Is<LoopExpr>(n)) { CheckLoop(x, nullptr, false); n->exprtype = ast.voidtype; return; }
    NoUntypedEmptyArray(CheckValue(n, nullptr), n, "a statement");
}

inline void TypeCheck::CheckVarDecl(VarDecl *vd, bool global) {
    TypeExpr *ann = nullptr;
    if (vd->type) {
        ConstNamesIn(vd->type);
        ann = DeclType(vd->type, Subst(vd->type));
        ValidateType(ann, vd->line, global ? VT_GLOBAL : VT_LOCAL);
        if (vd->isconst) ann = ast.ConstOf(ann);   
    }
    auto MakeDef = [&](size_t i) -> VarDef * {
        if (global) return vd->defs[i];  
        if (vd->defs.size() <= i) vd->defs.push_back(nullptr);
        auto d = vd->defs[i] = ResetLocal(vd->defs[i]);
        d->name = vd->names[i];
        d->line = vd->line;
        d->isvar = vd->isvar;
        d->reusable = vd->reusable;
        d->resulttemp = vd->results;
        d->depth = CurDepth();
        d->ownerspec = frames.back().spec;
        return d;
    };
    
    
    auto fi = (int)frames.size() - 1;
    auto calls = frames[fi].cyclecalls;
    auto Finish = [&](VarDef *d, TypeExpr *t, const Val *v, Node *init) {
        
        
        
        if (vd->isconst || (!ann && v && IsRefOrSlice(t) && !v->writable)) t = ast.ConstOf(t);
        if (t->kind == TY_VOID) Error(vd, "initializer has no value");
        if (t->kind == TY_FN)
            Error(vd, "function values are compile-time only and cannot be stored (§7.6)");
        d->type = t;
        if (global) CheckVarExtent(d, vd);
        if (v && IsRefOrSlice(t)) {
            BindRefProvenance(d, *v);
            if (t->cq) d->ref.writable = false;
            
            if (global && !ann && !v->isnull)
                NoteGlobalBinding(d, v->AsRoots(), v->byteview, PointeeOf(t), init->line);
        } else if (v && HoldsPlainRef(t)) {
            NoteHolderBinding(d, *v, init);
        }
        if (vd->reusable) {
            auto kw = vd->reusable == RU_SLICES ? "reusable[]" : "reusable";
            if (!vd->isvar) Error(vd, cat(kw, " requires var"));
            if (!IsArrayKind(t, A_GROW) || ClassOf(t->arr->sub) != SC_FIXED)
                Error(vd, cat(kw, " applies to grow-only arrays of fixed-size "
                                  "elements (§5.4)"));
            
            
            if (IsPendingArray(t)) pendingpools[t] = d;
        }
        if (!global) {
            CheckCycleInit(d, fi, calls);
            vars.push_back(d);
            for (auto c : d->components) if (c) vars.push_back(c);
        }
    };
    
    
    if (global && vd->inits.empty() && ann) {
        string why;
        if (!HasDefault(ann, why))
            Error(vd, cat("global ", vd->names[0], " needs an initializer, since ",
                          TypeStr(ann), " has no default value: ", why));
        vd->inits.push_back(DefaultValue(ann, vd->line));
    }
    if (vd->inits.empty()) {
        if (!ann) Error(vd, "a declaration without an initializer needs a type");
        if (!UninitOK(ann))
            Error(vd, cat("a value of type ", TypeStr(ann),
                          " must be constructed at its declaration (§4.2)"));
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            d->assigned = d->maybeassigned = false;
            Finish(d, ann, nullptr, nullptr);
        }
        return;
    }
    if (vd->inits.size() == 1 && vd->names.size() > 1) {
        
        if (ann) Error(vd, "a type annotation is not supported on multi-value bindings");
        vector<VarDef *> defs;
        vector<Dest> dests;
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = MakeDef(i);
            defs.push_back(d);
            dests.emplace_back(d);
        }
        {
            Saved<Node *> mn(multidestnode, vd->inits[0]);
            Saved<vector<Dest>> md(multidests, std::move(dests));
            CheckValue(vd->inits[0], nullptr);
        }
        auto call = Is<Call>(vd->inits[0]);
        if (!call || call->rettypes.size() != vd->names.size())
            Error(vd, cat((int64_t)vd->names.size(), vd->results ? " targets" : " names",
                          " need that many values", call ? cat(", and the call gives ",
                          (int64_t)call->rettypes.size()) : string()));
        auto rets = lastcallrets;
        for (size_t i = 0; i < vd->names.size(); i++) {
            auto d = defs[i];
            d->assigned = d->maybeassigned = true;
            
            
            
            auto rv = vd->byref || IsNonFixedRef(rets[i]) ? rets[i] : DecayRef(rets[i]);
            
            
            
            if (IsPlainRef(rets[i].type) && !IsRefOrSlice(rv.type))
                NoRelRefCopy(vd->inits[0], rv.type, i, vd, &rv);
            CheckBindingRoot(d, rv, vd->inits[0]);
            Finish(d, rv.type, &rv, vd->inits[0]);
        }
        return;
    }
    if (vd->inits.size() != vd->names.size())
        Error(vd, cat((int64_t)vd->names.size(), " name(s) with ",
                      (int64_t)vd->inits.size(), " initializer(s)"));
    for (size_t i = 0; i < vd->names.size(); i++) {
        auto d = MakeDef(i);
        Val v;
        calls = frames[fi].cyclecalls;
        {
            
            
            
            DestScope ds(*this, Dest(d, ann && (ann->kind == TY_REF || ann->kind == TY_SLICE)));
            RootInitScope ris(*this, vd->inits[i], d);
            SlotScope ss(*this, ann != nullptr);
            auto refinit = Is<Unary>(vd->inits[i]);
            if (refinit && refinit->synth) {
                vd->inits[i] = refinit->child;
                refinit = nullptr;
            }
            if (vd->byref && !ann) {
                
                
                v = CheckV(vd->inits[i], nullptr);
                if (v.isnull) Error(vd->inits[i], "null needs an annotated optional type");
                if (v.lvalue && !IsRefOrSlice(v.type)) {
                    vd->inits[i] = AutoRef(vd->inits[i], v);
                } else if (!IsRefOrSlice(v.type)) {
                    Error(vd->inits[i], ".= binds a reference: the initializer must be a "
                                        "reference, a slice, or storage (a variable, field "
                                        "or element)");
                }
                vd->inits[i]->exprtype = v.type;
            } else if (!ann && refinit && refinit->op == T_BITAND) {
                
                
                v = CheckV(vd->inits[i], nullptr);
                vd->inits[i]->exprtype = v.type;
                if (IsNonFixedRef(v))
                    Warn(vd->inits[i], cat("redundant &: ", ExprStr(refinit->child),
                                           " binds by reference without it (§4.1)"));
            } else {
                v = CheckValue(vd->inits[i], ann, false, false, !ann);
                
                
                
                
                if (!ann && IsNonFixedLValue(v)) vd->inits[i] = AutoRef(vd->inits[i], v);
            }
        }
        if (IsUntypedEmptyArray(v.type) && !ann) {
            
            
            if (!Is<ArrayLit>(vd->inits[i]))
                Error(vd->inits[i], cat("cannot infer the element type of []: only a var whose "
                                        "whole initializer is [] takes it from what goes into it "
                                        "first (§4.2); annotate the type of ", vd->names[i]));
            if (!vd->isvar)
                Error(vd->inits[i], "a let bound to [] can never receive elements; "
                                    "annotate its type, or make it a var");
            v.type = PendingArray(vd->inits[i]->line);
            vd->inits[i]->exprtype = v.type;
            v.emptyarr = false;
        }
        if (v.isnull && !ann)
            Error(vd->inits[i], "null needs an annotated optional type");
        if (!ann) {
            NoRelRefCopy(vd->inits[i], v.type, 0, vd, &v);
            CheckBindingRoot(d, v, vd->inits[i]);
        }
        d->assigned = d->maybeassigned = true;
        
        
        
        
        d->nonneg = !vd->isvar && v.nonneg;
        d->nonnegfrom = d->nonneg ? v.nonnegfrom : nullptr;
        auto vt = LoadType(v.type);
        auto fltlit = vt && vt->kind == TY_FLT && !IsF32(vt) && LitFloat(v);
        d->constlit = !vd->isvar && !ann && !vd->byref &&
                      ((global && (v.ck == CK_INT || v.notconst)) || fltlit);
        d->constnot = d->constlit && v.notconst;
        d->constval = v.notconst ? ~v.litlo : v.ival;
        d->constuns = v.uns;
        d->constflt = v.ck == CK_FLT;
        d->constfval = v.fval;
        d->constfrom = d->constlit ? v.constfrom : nullptr;
        Finish(d, ann ? ann : v.type, &v, vd->inits[i]);
        auto len = Is<Dot>(vd->inits[i]);
        if (!vd->isvar && !global && len && len->member == B_LEN &&
            d->type->kind == TY_INT && d->type->intstorage == IS_I64)
            d->markof = len->obj;
    }
}






inline void TypeCheck::CheckBindingRoot(VarDef *d, const Val &v, Node *at) {
    auto t = v.type;
    if (!t || v.isnull) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    
    
    if (d->isglobal) {
        auto reach = false;
        if (auto gs = StoredIntoGrowShrink(v, roots, t, !isrs, &reach))
            Error(at, NeverStoredError(gs, MayPointWording(roots, gs), reach));
    }
    for (auto &a : roots.alts) {
        auto root = a.root;
        
        if (!root || root == temproot || Depth(root) <= Depth(d)) continue;
        auto what = !isrs ? "a value holding references" : t->kind == TY_SLICE ? "a slice"
                                                                                : "a reference";
        if (IsTemp(root))
            Error(at, cat("binding ", d->name, " to ", what, " rooted at a temporary, which "
                          "does not outlive it (§9.2): a temporary lasts until the end of its "
                          "statement, so bind it to a variable of its own first"));
        Error(at, cat("binding ", d->name, " to ", what, " rooted at ", root->name,
                      ", which does not outlive it (§9.2)"));
    }
}





inline void TypeCheck::CheckCycleInit(VarDef *d, int fi, int calls) {
    if (frames[fi].cyclecalls == calls || !NeedsCycleStack(d->type)) return;
    Error(frames[fi].cyclecall,
          cat(FrameFnName(fi), " calls into its recursive cycle while initializing "
              "non-fixed-size local ", d->name, ", which is built in place (§7.8)"));
}


inline void TypeCheck::AssignableClassCheck(TypeExpr *t, Node *at) {
    auto cls = ClassOf(t);
    if (cls == SC_ROOT) Error(at, "a root owner cannot be assigned as a whole; assign its fields instead");
    if (cls == SC_VARIABLE && !(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED))
        Error(at, cat("a value of type ", TypeStr(t),
                      " is frozen at construction (§4.4); rebuild its container instead"));
}










inline Val TypeCheck::CheckAssignedValue(Assign *a, TypeExpr *target, TypeExpr *arr,
                                         const Roots &built, Dest dest) {
    if (arr) {
        {
            struct Restore {
                bool &flag, saved;
                ~Restore() { flag = saved; }
            } restore { rebuilding, rebuilding };
            rebuilding = true;
            ShrinkThrough(a, "assign", ExprStr(a->lval), built, target);
        }
        NoteGrow(a, built, cat("assign ", ExprStr(a->lval)), target);
    }
    SlotScope ss(*this, true);
    auto base = cur.growlog.size();
    auto v = CheckValueAt(a->rhs, target, dest);
    if (arr && !built.None()) {
        CheckGrowsSince(base, built, cat("the value assigned to ", ExprStr(a->lval)), target);
        CheckBuiltUses(a->rhs, a->lval, target, built, arr);
    }
    if (v.type->kind == TY_VOID && reachable)
        Error(a, "the right-hand side has no value");
    return v;
}

inline void TypeCheck::CheckAssign(Assign *a) {
    
    
    
    if (auto id = Is<Ident>(a->rhs)) {
        auto r = LookupVar(id->name, id->ns);
        if (r && r->resulttemp && r->type) {
            auto t = r->type;
            if (IsPlainRef(t) && ClassOf(t->ref->sub) != SC_FIXED)
                Error(a, cat("result ", id->name.substr(id->name.find('#') + 1), " of the call is a "
                             "reference to a value that is not fixed-size, which is not copied "
                             "implicitly (§4.1): bind it with `let`, and assign a copy(...) of it"));
            if (!IsRefOrSlice(t) && ClassOf(t) != SC_FIXED) {
                auto cp = ast.New<Call>(a->rhs->line, ast.New<Ident>(a->rhs->line, "copy"));
                cp->args.push_back(a->rhs);
                a->rhs = cp;
            }
        }
    }
    auto lv = CheckLValue(a->lval);
    
    
    
    if (a->op == T_DOTASSIGN && (lv.var ? lv.var->type : lv.type)->kind != TY_REF &&
        InGenericBody())
        a->op = T_ASSIGN;
    auto held = lv;
    auto throughref = a->op != T_DOTASSIGN && IsPlainRef(held.type);
    if (throughref) DerefLValue(held, a->lval);
    
    
    
    
    
    auto owns = [&](TypeExpr *t) {
        return t && t->kind != TY_SLICE &&
               (t->kind != TY_REF || ClassOf(t->ref->sub) == SC_RESIZABLE);
    };
    auto infields = [&](Node *n) {
        while (auto d = Is<Dot>(n)) {
            n = d->obj;
            if (!Is<Ident>(n) && !owns(n->exprtype)) return false;
        }
        auto id = Is<Ident>(n);
        return id && id->vdef && owns(id->vdef->type);
    };
    
    
    if (throughref || !infields(a->lval)) {
        Val loc;
        loc.type = ast.RefTo(held.type, a->line);
        loc.SetProv(held);
        nodevals[a->lval] = loc;
    } else {
        nodevals.erase(a->lval);
    }
    if (a->op == T_DOTASSIGN) { CheckRebind(a, lv); return; }
    if (lv.isvarint)
        Error(a, "varint fields are written only at construction (§3.6)");
    if (lv.type->kind == TY_REF && lv.type->ref->lenstorage == IS_VARINT)
        Error(a, "varint-width relative references are written only at construction");
    
    
    
    if (IsPlainRef(lv.type)) {
        if (a->op == T_ASSIGN) PointeeAssign(a, lv, held);
        else CompoundAssign(a, lv.type->ref->sub, PointeeWritable(lv, a));
        a->pointee = true;
        return;
    }
    if (IsOptional(lv.type))
        Error(a, "optional value must be narrowed before writing through it, "
                 "or rebound with .=");
    if (a->op != T_ASSIGN) {
        NoCopyWrite(a, lv);
        NoLetAssign(a, lv);
        CompoundAssign(a, lv.type, WholeWritable(lv));
        if (lv.var) RequireAssigned(lv.var, a);
        return;
    }
    
    auto target = lv.var ? lv.var->type : lv.type;
    if (lv.var && !lv.var->assigned) {
        
    } else {
        NoCopyWrite(a, lv);
        NoLetAssign(a, lv);
        if (!WholeWritable(lv))
            Error(a, "cannot assign through this path (const, or a read-only "
                     "instantiation, §9.5)");
        AssignableClassCheck(target, a);
    }
    if (IsPendingArray(target)) {
        
        auto av = DecayRef(CheckV(a->rhs, nullptr));
        CompletePending(target, PendingElemFromSeq(av, a), a->line);
    }
    
    
    
    Roots built;
    auto arr = ResizableArrayIn(target);
    if (arr && (!lv.var || lv.var->assigned)) {
        if (lv.var) built.Set(lv.var, true); else built = lv;
        if (arr->arr->akind == A_GROW) GrowOnlyAssignKnown(a, built, arr == target);
    } else {
        arr = nullptr;
    }
    auto killat = storeevents.size();
    auto v = CheckAssignedValue(a, target, arr, built,
                                Dest(lv, lv.var && IsRefOrSlice(target), lv.reached));
    NoteKill(a->lval, lv, target, v, killat);
    if (lv.var) {
        NoLetReassign(a, lv.var, "assign to", "assigned");
        
        if (target->kind == TY_SLICE) {
            if (!lv.var->refrootknown || lv.var->ref.Unknown()) BindRefProvenance(lv.var, v);
            else CheckRefRebindRoot(a, lv.var, v);
        }
        lv.var->assigned = lv.var->maybeassigned = true;
        KillNarrow(lv.var);
    }
}




inline bool TypeCheck::InGenericBody() {
    for (auto sf = frames.back().sf; sf; sf = sf->outer)
        if (sf->IsGeneric()) return true;
    return false;
}


inline void TypeCheck::CheckRebind(Assign *a, LVal &lv) {
    auto target = lv.var ? lv.var->type : lv.type;
    if (target->kind != TY_REF)
        Error(a, cat(".= rebinds references; the target has type ", TypeStr(target)));
    
    
    if (target->ref->lenstorage == IS_VARINT)
        Error(a, "varint-width relative references are written only at construction");
    if (lv.var) {
        if (!lv.var->isvar && lv.var->assigned)
            Error(a, cat("cannot rebind let ", lv.var->name));
    } else {
        NoLetAssign(a, lv);
        if (!lv.writable)
            Error(a, "cannot assign through this path (const, or a read-only "
                     "instantiation, §9.5)");
    }
    Val v;
    bool wasplain;
    auto killat = storeevents.size();
    {
        DestScope ds(*this, lv.var ? Dest(lv.var, true) : Dest(lv, false, lv.reached));
        
        
        SlotScope ss(*this, true);
        v = CheckV(a->rhs, target);
        if (BindsRef(v, target)) a->rhs = AutoRef(a->rhs, v, !target->cq);
        wasplain = IsPlainRef(v.type);
        MustFit(v, a->rhs, target);
    }
    a->rhs->exprtype = v.type;
    if (lv.var) {
        NoLetReassign(a, lv.var, "rebind", "bound");
        if (!lv.var->refrootknown || lv.var->ref.Unknown()) BindRefProvenance(lv.var, v);
        else if (!v.isnull) CheckRefRebindRoot(a, lv.var, v);
        lv.var->assigned = lv.var->maybeassigned = true;
        
        
        if (target->ref->optional) {
            auto spec = CurRealFrame().spec;
            if (spec && lv.var->ownerspec != spec)
                spec->record.reboundoptionals.insert(lv.var);
            if (!v.isnull && wasplain) {
                SetNarrowed(lv.var, NarrowedRef(target, a->line));
            } else {
                lv.var->narrowed = nullptr;
            }
        }
    } else {
        
        
        NoteKill(a->lval, lv, target, v, killat);
    }
}


inline bool TypeCheck::PointeeWritable(LVal &lv, Node *at) {
    if (lv.var) {
        RequireAssigned(lv.var, at);
        return lv.var->ref.writable;
    }
    
    
    return !lv.type->cq;
}




inline void TypeCheck::PointeeAssign(Assign *a, LVal &lv, const LVal &at) {
    auto pt = lv.type->ref->sub;
    if (!PointeeWritable(lv, a))
        Error(a, "cannot write through this reference: non-writable provenance (§9.5)");
    if (pt->kind == TY_INT && pt->intstorage == IS_VARINT)
        Error(a, "varint fields are written only at construction (§3.6)");
    AssignableClassCheck(pt, a);
    auto arr = ResizableArrayIn(pt);
    Roots built;
    if (arr) built = at;
    if (arr && arr->arr->akind == A_GROW) GrowOnlyAssignKnown(a, built, arr == pt);
    Dest dest(at, false, at.reached);
    dest.slot = pt->kind == TY_SLICE;
    auto killat = storeevents.size();
    auto v = CheckAssignedValue(a, pt, arr, built, dest);
    
    
    if (Is<Ident>(a->lval)) NoteKill(a->lval, at, pt, v, killat);
}














inline bool TypeCheck::StoreIntoSlot(Node *at, VarDef *r, bool bound, TypeExpr *slice,
                                     const Val &v, const string &via) {
    if (bound) return false;
    if (auto sv = SliceVarOf(r)) return RebindSliceVar(at, sv, v, via);
    if (!IsClassRoot(r)) return false;
    Val nv = v;
    erase_if(nv.alts, [&](const RootAlt &a) { return !a.exact && a.from == r; });
    if (nv.None()) return false;
    auto changed = false;
    VisibleVars([&](VarDef *x) {
        auto sv = SliceVarOf(x);
        if (sv && sv->slotref && !sv->isglobal && Depth(sv) <= Depth(r) &&
            TypeEq(sv->type, slice))
            changed = RebindSliceVar(at, sv, nv, " through a reference that may name it") ||
                      changed;
    });
    return changed;
}






inline bool TypeCheck::RebindSliceVar(Node *at, VarDef *sv, const Val &v, const string &via) {
    if (!sv->refrootknown || sv->ref.Unknown()) return false;
    NoteEnvRead(sv);
    return CheckRefRebindRoot(at, sv, v, via);
}





inline bool TypeCheck::CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv,
                                          const string &via) {
    auto changed = false;
    if (rv.byteview && !vd->ref.byteview) {
        vd->ref.byteview = true;
        NoteFact(vd);
        changed = true;
    }
    if (rv.freshview && !vd->ref.freshview) {
        vd->ref.freshview = true;
        NoteFact(vd);
        changed = true;
    }
    auto rootname = [](VarDef *r) { return r ? r->name : string_view("static data"); };
    
    
    
    auto added = rv.Any([&](const RootAlt &a) { return !vd->ref.Has(a.root); });
    auto nr = rv.Root();
    if (added && Depth(nr) != Depth(vd->ref.Root())) {
        if (!via.empty())
            Error(at, cat("storing a slice rooted at ", rootname(nr), " into ", vd->name, via,
                          ": ", vd->name, " is bound to one rooted at ",
                          rootname(vd->ref.Root()), ", at a different scope depth (§9.2)"));
        
        
        auto describe = [&](VarDef *r) -> string {
            if (!r) return "static data";
            if (IsTemp(r)) return "a temporary";
            if (!IsClassRoot(r)) return string(r->name);
            auto from = UltimateRoot(r);
            return cat("the caller's storage behind ", r->name,
                       from && from->type && !from->isglobal ? cat(" (", from->name, ")") : "");
        };
        Error(at, cat("re-binding ", vd->name, " with a reference rooted at a different "
                      "scope depth is not supported; declare a new variable (§9.2): ",
                      vd->name, " is bound to one rooted at ", describe(vd->ref.Root()),
                      ", and this one is rooted at ", describe(nr)));
    }
    
    
    
    auto was = GrowShrinkTaint(vd->ref, vd->type);
    if (!was) {
        auto reach = false;
        if (auto gs = GrowShrinkTaint(rv, vd->type, &reach))
            Error(at, cat("re-binding ", vd->name, " to a reference that may point into a "
                          "grow-shrink array (", reach ? ReachedThroughStr(gs) : string(gs->name),
                          "), where it pointed into none, is not supported; declare a new "
                          "variable (§5.2)"));
    }
    
    
    
    if (vd->ref.reusable & ~rv.reusable) {
        if (vd->ref.reusable == (RU_SLOTS | RU_SLICES)) {
            
            
            vd->ref.reusable &= rv.reusable;
            NoteFact(vd);
            changed = true;
        } else {
            auto kind = [](int ru) { return ru == RU_SLICES ? "reusable[]" : "reusable"; };
            Error(at, cat("re-binding ", vd->name, " to a reference ",
                          rv.reusable ? cat("to a ", kind(rv.reusable), " pool")
                                      : string("that is not a pool reference"),
                          ", where it was bound to a ", kind(vd->ref.reusable),
                          " pool, is not supported; declare a new variable (§5.4)"));
        }
    }
    
    
    
    return AddBinding(vd, rv) || changed;
}


inline void TypeCheck::NoLetAssign(Node *at, const LVal &lv) {
    if (lv.letbound && !lv.copyof)
        Error(at, cat("cannot assign to let ", lv.letname, " (§4.4)"));
}



inline void TypeCheck::NoLetReassign(Node *at, const VarDef *vd, const char *verb,
                                     const char *done) {
    if (!vd->isvar && vd->maybeassigned)
        Error(at, cat("cannot ", verb, " let ", vd->name, ", which may be ", done,
                      " already (§4.4)"));
}



inline void TypeCheck::NoCopyWrite(Node *at, const LVal &lv) {
    if (lv.copyof)
        Error(at, cat("cannot write ", lv.copyof->name, ": a by-value binding is a copy "
                      "of the element; bind it by reference (&", lv.copyof->name,
                      ") to write the element (§6.5)"));
}






inline bool TypeCheck::WholeWritable(const LVal &lv) {
    return lv.var || lv.writable;
}

inline void TypeCheck::CompoundAssign(Assign *a, TypeExpr *st, bool writable) {
    if (!writable)
        Error(a, "cannot assign through this path (const, or a read-only "
                 "instantiation, §9.5)");
    auto isbit = a->op == T_ANDEQ || a->op == T_OREQ || a->op == T_XOREQ;
    auto isshift = a->op == T_SHLEQ || a->op == T_SHREQ;
    if (IsIndexT(st)) NoIndexUpdate(a, TName(a->op), st);
    if (IsOpaque(LoadType(st))) {
        
        Operand(a->rhs);
    } else if (st->kind == TY_INT && st->intstorage != IS_VARINT && isshift) {
        
        CheckIntAny(a->rhs);
    } else if (st->kind == TY_INT && st->intstorage != IS_VARINT) {
        
        
        CheckValue(a->rhs, st);
    } else if (st->kind == TY_FLT && !isbit && !isshift) {
        CheckValue(a->rhs, st);
    } else if (!isbit && !isshift && ElementwiseOK(st)) {
        
        
        
        auto rv = Operand(a->rhs);
        NoUntypedEmptyArray(rv, a->rhs, TName(a->op));
        auto rt = LoadType(rv.type);
        if ((a->op == T_MULEQ || a->op == T_DIVEQ) &&
            (IsIntT(rt) || rt->kind == TY_FLT)) {
            if (auto scalar = ElementwiseScalarType(st)) {
                auto fitted = rv;
                MustFit(fitted, a->rhs, scalar);
                RetypeOperand(a->rhs, rv, scalar);
                return;
            }
        }
        if (!TypeEq(LoadType(st), rt))
            Error(a, cat("operator ", TName(a->op), " cannot be applied to ",
                         TypeStr(st), " and ", TypeStr(rt)));
    } else if (st->kind == TY_INT) {
        Error(a, "varint fields are written only at construction (§3.6)");
    } else {
        Error(a, cat("operator ", TName(a->op), " cannot be applied to ", TypeStr(st)));
    }
}

inline void TypeCheck::CheckIncDec(IncDec *x) {
    auto lv = CheckLValue(x->lval);
    auto st = lv.type;
    auto writable = WholeWritable(lv);
    if (IsPlainRef(lv.type)) {
        st = lv.type->ref->sub;
        writable = PointeeWritable(lv, x);
    } else {
        NoCopyWrite(x, lv);
        NoLetAssign(x, lv);
        if (lv.var) RequireAssigned(lv.var, x);
    }
    if (!writable) Error(x, "cannot modify through this path (const, or a read-only "
                            "instantiation, §9.5)");
    if (!IsIntT(st))
        Error(x, cat(TName(x->op), " requires an integer lvalue, got ", TypeStr(st)));
    if (st->ix) NoIndexUpdate(x, TName(x->op), st);
}



inline void TypeCheck::NoIndexUpdate(Node *at, const char *op, TypeExpr *st) {
    Error(at, cat(op, " cannot update a ", TypeStr(st), ": what it computes is a plain ",
                  IntStorageName(st->intstorage), "; compute it, then convert it back with `as ",
                  TypeStr(st), "` (§3.11)"));
}

}  
