




#pragma once

namespace faloya {




inline TypeCheck::LVal TypeCheck::CheckLValue(Node *n) {
    DestScope ds(*this, Dest {});
    NodeScope ns(*this, n);
    
    
    
    auto record = [&](const LVal &lv) {
        Val v;
        v.type = LoadType(lv.type);
        v.SetProv(lv);
        RecordVal(n, v);
    };
    if (auto id = Is<Ident>(n)) {
        if (!id->expansionerror.empty()) Error(id, string(id->expansionerror));
        auto vd = LookupVar(id->name, id->ns, n);
        if (!vd) {
            if (auto kind = ScopeNameKind(id->name))
                Error(n, cat(id->name, " names a ", kind, ", not a variable"));
            Error(n, cat("unknown variable: ", id->name));
        }
        
        
        
        if (vd->isglobal) RequireAssigned(vd, n);
        id->vdef = vd;
        LVal lv;
        lv.type = vd->narrowed ? vd->narrowed : vd->type;
        lv.var = vd;
        lv.Set(vd, true);
        lv.byteview = vd->contentbyteview;
        
        
        
        lv.writable = !vd->copybind && !(vd->type && vd->type->cq);
        lv.letbound = !vd->isvar;
        lv.letname = vd->name;
        lv.copyof = vd->copybind ? vd : nullptr;
        lv.reusable = vd->reusable;
        n->exprtype = vd->type;
        if (IsRefOrSlice(lv.type)) {
            Val v;
            v.type = LoadType(lv.type);
            v.SetProv(RefProvOf(vd));
            RecordVal(n, v);
        } else {
            record(lv);
        }
        return lv;
    }
    if (auto d = Is<Dot>(n)) {
        if (d->incomplete) IncompleteDot(d);
        auto lv = LValueBase(d->obj);
        auto base = lv.type;
        DerefLValue(lv, d->obj);
        CheckAccessorOwner(d, d->accessorowner, lv.type);
        ResolveMemberLValue(lv, d);
        if (lv.intemp) NoRelRefTempRead(d->obj, base, lv.type);
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    if (auto ix = Is<Index>(n)) {
        auto lv = LValueBase(ix->obj);
        DerefLValue(lv, ix->obj);
        SliceProvenance(lv, ix->obj);
        TypeExpr *elem;
        if (lv.type->kind == TY_SLICE) {
            elem = lv.type->sub;
        } else {
            if (lv.type->kind != TY_ARRAY)
                Error(n, cat("cannot index a value of type ", TypeStr(lv.type)));
            RequireComplete(lv.type, n->line);
            elem = lv.type->arr->sub;
        }
        if (ClassOf(elem) != SC_FIXED)
            Error(n, cat(lv.type->kind == TY_SLICE ? "slices" : "arrays",
                         " of variable-size elements cannot be indexed, only iterated; for "
                         "random access, keep your own array of references to them (§3.3)"));
        if (lv.intemp) NoRelRefTempRead(ix->obj, lv.type, elem);
        CheckIntAny(ix->idx);
        if (lv.type->cq) lv.writable = false;   
        lv.letbound = false;
        lv.type = elem;
        lv.var = nullptr;
        lv.fromstorage = true;
        lv.isslot = true;
        lv.isvarint = elem->kind == TY_INT && elem->intstorage == IS_VARINT;
        n->exprtype = lv.type;
        RecordVal(n, ContainerRead(lv));
        return lv;
    }
    Error(n, "not an assignable location");
}






inline TypeCheck::LVal TypeCheck::LValueBase(Node *n, bool cmpview) {
    DestScope ds(*this, Dest {});
    if (Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n)) return CheckLValue(n);
    auto v = CheckV(n, nullptr);
    NoUntypedEmptyArray(v, n, "an index, slice or field of it");
    if (!cmpview) NoTemporaryLiteral(n, v.type);
    n->exprtype = v.type;
    LVal lv;
    lv.type = v.type;
    lv.SetProv(v);
    
    
    lv.intemp = TempContents(v, lv.contents);
    if (lv.intemp) lv.Set(v.Root(), true);
    return lv;
}





inline void TypeCheck::NoTemporaryLiteral(Node *n, TypeExpr *t) {
    if (!Is<ArrayLit>(n) || ClassOf(t) == SC_FIXED) return;
    Error(n, cat("an array literal of variable-size elements is a ", TypeStr(t),
                 ", built only in a construction context (§4.2), never as a temporary "
                 "for a slice or reference to view; bind it to a variable first"));
}




inline void TypeCheck::DerefLValue(LVal &lv, Node *at) {
    if (lv.type->kind != TY_REF) return;
    if (lv.type->ref->optional)
        Error(at, "optional value must be narrowed (if/guard/assert) before use");
    
    if (lv.var) {
        RequireAssigned(lv.var, at);
        lv.SetProv(RefProvOf(lv.var));
    } else if (lv.fromstorage) {
        
        
        
        
        
        ReadBackLVal(lv);
    }
    lv.type = lv.type->ref->sub;
    lv.reached = LoadType(lv.type);
    lv.var = nullptr;
    lv.letbound = false;
    lv.throughref = true;
    
    
    
    lv.isslot = false;
    lv.ClearReads();
    if (lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT) lv.isvarint = true;
}




inline void TypeCheck::RequireAssigned(VarDef *vd, Node *at, string_view use) {
    if (vd->assigned) return;
    
    
    if (failedglobals.count(vd)) throw Abandoned {};
    auto order = vd->isglobal
                     ? cat(": globals initialize in declaration order, imported files first "
                           "(§11.1), so ", vd->name, " has no value until its initializer at ",
                           Where(vd->line), " has run")
                     : string();
    if (!use.empty()) Error(at, cat(use, " reads ", vd->name, " before it is assigned", order));
    Error(at, cat("variable ", vd->name, " may be used before it is assigned", order));
}




inline void TypeCheck::RequireIndexArray(TypeExpr *t, Node *at, string_view what) {
    auto a = t->ix->arr;
    RequireAssigned(a->componentowner ? a->componentowner : a, at,
                    cat(what, " checks against ", a->name, ".len, which"));
}




inline void TypeCheck::SliceProvenance(LVal &lv, Node *at) {
    if (lv.type->kind != TY_SLICE) return;
    if (!lv.var) {
        if (lv.fromstorage) ReadBackLVal(lv);
        else if (lv.throughref) lv.SetProv(SlotView(lv, lv.type));
        lv.reached = lv.type->sub;
        return;
    }
    RequireAssigned(lv.var, at);
    lv.SetProv(RefProvOf(lv.var));
    lv.reached = lv.type->sub;
    lv.var = nullptr;
}





inline void TypeCheck::ReadBackLVal(LVal &lv) {
    if (!IsRefOrSlice(lv.type)) return;
    
    
    
    
    
    
    lv.byteview = lv.byteview || lv.Any([&](const RootAlt &a) {
        return a.root && (a.root->contentbyteview ||
                          (a.root->heldholder && a.root->heldholder->contentbyteview) ||
                          (a.root->isglobal && lv.type->cq && IsU8(PointeeOf(lv.type))));
    });
    
    
    auto slotread = lv.isslot && SlotReadable(lv.type);
    auto rb = ReadBackRoot(lv.type, lv, lv.byteview, lv.intemp ? &lv.contents : nullptr,
                           slotread, true);
    lv.TakeAlts(rb);
    for (auto &a : lv.alts) a.slotread = slotread;
    lv.freshview = false;
    lv.reached = nullptr;
    lv.intemp = false;
    lv.writable = lv.isslot ? SlotLoadWritable(lv.type, lv.writable)
                            : lv.writable && !lv.type->cq;
}







inline bool TypeCheck::SlotLoadWritable(TypeExpr *t, bool path) {
    auto selfrel = IsSelfRelRef(t);
    return !t->cq && (path || !selfrel);
}



inline Val TypeCheck::ContainerRead(LVal lv) {
    Val v;
    if (lv.type->kind == TY_SLICE) {
        v.slot = lv;
        v.hasslot = true;
    }
    ReadBackLVal(lv);
    v.type = LoadType(lv.type);
    v.SetProv(lv);
    if (!IsRefOrSlice(v.type) && HoldsPlainRef(v.type)) {
        
        
        
        
        
        
        if (lv.intemp) {
            v.contents = lv.contents.roots;
        } else {
            v.contents = HeldAt(lv);
            for (auto &a : v.contents.alts) a.slotread = lv.isslot;
        }
        v.holderset = true;
        v.holderfrom = lv.intemp ? lv.contents.from : HolderSource(lv);
        MarkClassCopy(v);
    }
    
    
    
    if (!IsRefOrSlice(lv.type) && lv.type->cq) v.writable = false;
    v.lvalue = v.type->kind != TY_REF;
    
    
    if (IsVarintT(lv.type)) {
        v.isvarint = true;
        v.writable = false;
    }
    return v;
}










template<typename F> void TypeCheck::ForOperands(Node *n, F f) {
    if (auto c = Is<Call>(n)) {
        auto builtin = c->builtin >= 0;
        auto member = builtin && (BuiltinByKind(c->builtin).flags & BF_MEMBER);
        auto render = builtin && (c->builtin == B_PRINT || c->builtin == B_STR ||
                                  c->builtin == B_FORMAT);
        auto first = true;
        auto kind = [&]() {
            auto k = member && first ? HK_RECEIVER : render ? HK_NONE : HK_VALUE;
            first = false;
            return k;
        };
        if (auto d = Is<Dot>(c->callee)) f(d->obj, kind());
        for (auto a : c->args) f(a, kind());
        return;
    }
    if (auto ix = Is<Index>(n)) {
        f(ix->obj, HK_ELEMS);
        f(ix->idx, HK_VALUE);
        return;
    }
    if (auto se = Is<SliceExpr>(n)) {
        f(se->obj, HK_ELEMS);
        if (se->lo) f(se->lo, HK_VALUE);
        if (se->hi) f(se->hi, HK_VALUE);
        return;
    }
    if (auto b = Is<Binary>(n)) {
        f(b->left, HK_VIEW);
        f(b->right, HK_VALUE);
        return;
    }
    if (auto a = Is<Assign>(n)) {
        f(a->lval, HK_LOCATION);
        f(a->rhs, HK_VALUE);
        return;
    }
    if (auto al = Is<ArrayLit>(n)) {
        for (auto e : al->elems) f(e, HK_VALUE);
        if (al->fillval) f(al->fillval, HK_VALUE);
        if (al->fillcount) f(al->fillcount, HK_VALUE);
        if (al->capexpr) f(al->capexpr, HK_VALUE);
        return;
    }
    if (auto sl = Is<StructLit>(n)) {
        for (auto &fi : sl->inits) f(fi.val, HK_VALUE);
        return;
    }
    if (auto r = Is<Return>(n)) {
        for (auto v : r->vals) f(v, HK_VALUE);
        return;
    }
    if (auto vd = Is<VarDecl>(n)) {
        for (auto i : vd->inits) f(i, HK_VALUE);
        return;
    }
    if (Is<Unary>(n) || Is<AsCast>(n) || Is<Dot>(n) || Is<IncDec>(n) || Is<Break>(n)) {
        n->Children([&](Node *ch) { f(ch, HK_VALUE); });
        return;
    }
    
    
    if (auto fl = Is<ForLoop>(n)) {
        if (!Is<RangeExpr>(fl->iter)) f(fl->iter, HK_SEQUENCE);
        return;
    }
    
}





inline int TypeCheck::OperandIndex(Node *parent, Node *child, int next) {
    auto u = (size_t)next;
    Node *guess = nullptr;
    if (next < 0) {
    } else if (auto al = Is<ArrayLit>(parent)) {
        if (u < al->elems.size()) guess = al->elems[u];
    } else if (auto sl = Is<StructLit>(parent)) {
        if (u < sl->inits.size()) guess = sl->inits[u].val;
    } else if (auto c = Is<Call>(parent)) {
        if (Is<Dot>(c->callee)) u--;
        if (u < c->args.size()) guess = c->args[u];
    }
    if (guess && guess == child) return next;
    auto idx = -1, i = 0;
    ForOperands(parent, [&](Node *ch, HoldKind) {
        if (ch == child) idx = i;
        i++;
    });
    return idx;
}




inline bool TypeCheck::Descend(Node *n) {
    if (!cur.nodepath.empty() && cur.nodepath.back().node == n) return false;
    auto idx = cur.nodepath.empty() ? -1 : OperandIndex(cur.nodepath.back().node, n,
                                                        cur.nodepath.back().pos);
    if (idx >= 0) cur.nodepath.back().pos = idx;
    cur.nodepath.push_back({ n, idx, 0, false, (int)frames.size() - 1 });
    return true;
}




inline void TypeCheck::Ascend(Node *n) {
    if (cur.nodepath.empty() || cur.nodepath.back().node != n) return;
    auto e = cur.nodepath.back();
    cur.nodepath.pop_back();
    if (e.idx >= 0 && !cur.nodepath.empty()) cur.nodepath.back().pos = e.idx + 1;
}








template<typename F>
void TypeCheck::HoldAs(Node *n, const Val &v, HoldKind kind, Node *parent, const char *render,
                       F f) {
    if (!v.type || kind == HK_NONE) return;
    auto t = v.type;
    auto hold = [&](Val hv, bool location = false, bool elems = false) {
        f(Held { n, std::move(hv), location, render, elems });
    };
    switch (kind) {
        case HK_LOCATION:
            hold(v, true);
            return;
        case HK_ELEMS: {
            auto d = IsPlainRef(t) ? DecayRef(v) : v;
            TypeExpr *elem = d.type->kind == TY_ARRAY ? d.type->arr->sub
                             : d.type->kind == TY_SLICE ? d.type->sub : nullptr;
            if (!elem) return;
            if (IsTemp(d.Root())) d.Set(d.Root(), true);
            d.type = ast.SliceOf(elem, n->line);
            hold(d, false, true);
            return;
        }
        case HK_RECEIVER: {
            auto c = (Call *)parent;
            auto rt = IsPlainRef(t) ? t->ref->sub : t;
            TypeExpr *elem = rt->kind == TY_ARRAY ? rt->arr->sub
                             : rt->kind == TY_SLICE ? rt->sub : nullptr;
            auto held = v;
            auto elems = c->builtin == B_TO_BYTES || rt->kind == TY_SLICE;
            if (elems) {
                if (!elem) return;
                held.type = ast.SliceOf(elem, n->line);
            } else if (held.type->kind != TY_REF) {
                held.type = ast.RefTo(rt, n->line);
            }
            if (IsTemp(held.Root()) && t->kind != TY_REF && t->kind != TY_SLICE)
                held.Set(held.Root(), true);
            hold(held, false, elems);
            return;
        }
        case HK_SEQUENCE:
            HoldForSequence((ForLoop *)parent, v, f);
            return;
        default: break;
    }
    
    
    
    
    if (IsRefOrSlice(t)) {
        hold(v);
        return;
    }
    if (kind == HK_VIEW && t->kind == TY_ARRAY && ClassOf(t) != SC_FIXED) {
        auto view = v;
        view.type = ast.SliceOf(t->arr->sub, n->line);
        hold(view, false, true);
    }
    if (HoldsPlainRef(t)) {
        vector<TypeExpr *> pointees;
        RefPointees(t, pointees);
        auto held = v;
        held.TakeAlts(ContentsOf(v));
        for (auto pt : pointees) {
            held.type = ast.RefTo(pt, n->line);
            hold(held);
        }
    }
}










template<typename F> void TypeCheck::HoldForSequence(ForLoop *fl, const Val &v, F f) {
    if (fl->iterkind != IK_ARRAY && fl->iterkind != IK_SLICE) return;
    auto hold = [&](Node *n, Val hv, bool reread) {
        
        for (auto &a : hv.alts)
            if (IsTemp(a.root)) a.exact = true;
        f(Held { .node = n, .v = std::move(hv), .loop = fl, .reread = reread });
    };
    
    auto unwrap = [](Node *n) {
        auto u = Is<Unary>(n);
        if (u && u->op == T_BITAND && (Is<Ident>(u->child) || Is<Dot>(u->child) ||
                                       Is<Index>(u->child)) &&
            u->child->exprtype && u->child->exprtype->kind != TY_REF)
            return u->child;
        return n;
    };
    auto path = unwrap(fl->iter);
    if (!Is<Ident>(path)) {
        auto seq = v;
        if (IsPlainRef(seq.type) && seq.type->ref->sub->kind != TY_SLICE) seq = DecayRef(seq);
        auto st = seq.type;
        if (st->kind != TY_ARRAY) {
            hold(fl->iter, seq, false);
        } else if (ClassOf(st) != SC_RESIZABLE) {
            seq.type = ast.SliceOf(st->arr->sub, fl->iter->line);
            hold(fl->iter, seq, false);
        }
    }
    for (auto n = path;;) {
        Node *obj;
        if (auto d = Is<Dot>(n); d && !d->variantconst) obj = d->obj;
        else if (auto ix = Is<Index>(n)) obj = ix->obj;
        else break;
        
        auto ov = nodevals.find(obj);
        if (n->exprtype && IsRefOrSlice(n->exprtype) && ov) {
            auto slot = DecayRef(*ov);
            slot.byteview = false;
            slot.type = ast.SliceOf(LoadType(n->exprtype), n->line);
            hold(n, slot, true);
        }
        n = unwrap(obj);
    }
}











template<typename F> void TypeCheck::HeldOperands(F f) {
    for (auto &e : cur.nodepath) {
        auto own = e.discovering || (&e == &cur.nodepath.back() && Is<Call>(e.node));
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind kind) {
            auto at = i++;
            if (at >= e.pos) return;
            auto cv = nodevals.find(ch);
            if (!cv) return;
            if (ch == cur.renderarg && cur.rendering) {
                HoldAs(ch, *cv, HK_VIEW, e.node, cur.rendering, f);
                if (ch == cur.renderwhere) {
                    auto where = *cv;
                    where.type = ast.RefTo(where.type, ch->line);
                    f(Held { ch, std::move(where), false, cur.rendering });
                }
                for (auto &w : cur.renderwalks) {
                    auto walked = w;
                    walked.type = ast.RefTo(w.type, ch->line);
                    f(Held { .node = ch, .v = std::move(walked), .render = cur.rendering,
                             .inplace = true });
                }
                return;
            }
            if (own) return;
            HoldAs(ch, *cv, kind, e.node, nullptr, f);
        });
    }
}





template<typename F> void TypeCheck::LaterOperands(F f) {
    for (size_t k = 0; k < cur.nodepath.size(); k++) {
        auto &e = cur.nodepath[k];
        auto last = k + 1 == cur.nodepath.size();
        auto i = 0;
        ForOperands(e.node, [&](Node *ch, HoldKind) {
            auto at = i++;
            if (at > e.pos || (last && at == e.pos)) f(ch, e.frame);
        });
        if (!last) AfterHead(e.node, cur.nodepath[k + 1].node, [&](Node *p) { f(p, e.frame); });
    }
}






template<typename F> void TypeCheck::AfterHead(Node *n, Node *next, F f) {
    if (auto fi = Is<IfExpr>(n)) {
        if (next != fi->cond) return;
        f(fi->thenb);
        if (fi->elseb) f(fi->elseb);
    } else if (auto m = Is<MatchExpr>(n)) {
        if (next != m->scrutinee) return;
        for (auto &arm : m->arms) f(arm.body);
    } else if (auto fl = Is<ForLoop>(n)) {
        auto r = Is<RangeExpr>(fl->iter);
        if (r && next == r->lo) f(r->hi);
        if (next == fl->iter || (r && (next == r->lo || next == r->hi))) f(fl->body);
    } else if (auto w = Is<While>(n)) {
        if (next == w->cond) f(w->body);
    }
}



inline VarDef *TypeCheck::HolderRootOf(const Val &v) { return ContentsOf(v).Root(); }





inline void TypeCheck::CheckAccessorOwner(Node *at, TypeExpr *owner, TypeExpr *t,
                                          StructLit *lit) {
    if (!owner) return;
    auto owned = lit && owner->kind == TY_VARIANT
                     ? lit->variant && owner->var->variant == lit->variant &&
                           TypeArgsEq(owner->var->adt->enu->args, lit->einst->args)
                     : TypeEq(ast.PlainOf(t), owner);
    if (!owned)
        Error(at, cat("const for accessor belongs to ", TypeStr(owner), ", not ", TypeStr(t)));
}



inline void TypeCheck::ResolveMemberLValue(LVal &lv, Dot *d) {
    auto t = lv.type;
    
    
    auto step = [&](const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                    bool inframe) {
        for (auto i = 0; i < (int)fields.size(); i++) {
            auto &f = fields[i];
            if (f.ispad || f.name != d->name) continue;
            d->fieldidx = i;
            if (lv.type->cq) lv.writable = false;   
            lv.letbound = f.isconst;
            lv.letname = f.name;
            lv.type = ftypes[i];
            lv.var = nullptr;
            lv.fromstorage = true;
            lv.isslot = true;
            lv.fotail = inframe && ClassOf(lv.type) == SC_RESIZABLE;
            lv.isvarint = lv.type->kind == TY_INT && lv.type->intstorage == IS_VARINT;
            return true;
        }
        return false;
    };
    if (t->kind == TY_STRUCT) {
        auto inst = GetStructInst(t);
        if (inst->st->isroot) {
            auto parent = lv.AsRoots();
            if (step(inst->st->fields, inst->ftypes, true)) {
                lv.TakeAlts(ProjectRoots(parent, t, d->fieldidx));
                return;
            }
        } else if (step(inst->st->fields, inst->ftypes, FieldsInFrame(d))) return;
        if (inst->st->broken) throw Abandoned {};
        Error(d, cat("struct ", inst->st->name, " has no field ", d->name));
    }
    if (t->kind == TY_VARIANT) {
        auto inst = GetEnumInst(t->var->adt);
        auto vi = inst->en->VariantIndex(t->var->variant);
        if (step(t->var->variant->fields, inst->vftypes[vi], false)) return;
        Error(d, cat("variant ", inst->en->name, ".", t->var->variant->name,
                     " has no field ", d->name));
    }
    
    
    if (IsOpaque(t)) {
        if (t->cq) lv.writable = false;
        lv.letbound = lv.fotail = lv.isvarint = false;
        lv.type = OpaqueMember();
        lv.var = nullptr;
        lv.fromstorage = true;
        lv.isslot = true;
        return;
    }
    Error(d, cat("no field access on a value of type ", TypeStr(t)));
}






inline Val TypeCheck::DecayRef(const Val &v) {
    if (!IsPlainRef(v.type)) return v;
    Val r;
    r.type = LoadType(v.type->ref->sub);
    r.pointee = true;
    
    
    
    
    
    
    
    
    
    
    
    
    if (r.type->kind == TY_SLICE) {
        auto sv = SlotView(v, r.type);
        r.TakeAlts(sv);
        r.writable = sv.writable;
        r.byteview = sv.byteview;
        r.freshview = sv.freshview;
        r.slot = v;
        r.hasslot = true;
    } else {
        r.TakeAlts(v);
        r.ClearReads();
        r.writable = v.writable && !IsVarintT(v.type->ref->sub);
        r.byteview = v.byteview && HoldsPlainRef(r.type);
        if (HoldsPlainRef(r.type)) {
            r.contents = HeldAt(r);
            r.holderset = true;
            r.holderfrom = HolderSource(r);
            MarkClassCopy(r);
        }
    }
    r.isvarint = IsVarintT(v.type->ref->sub);
    return r;
}


inline bool TypeCheck::KeepsRef(const Val &v, TypeExpr *dt) {
    if (!IsPlainRef(v.type)) return true;  
    if (dt->kind == TY_REF) return true;   
    
    if (dt->kind == TY_SLICE && v.type->ref->sub->kind == TY_ARRAY) return true;
    return false;
}






inline Node *TypeCheck::AutoRef(Node *n, Val &v, bool writes) {
    if (!Referenceable(n, v)) NoResizableRef(n);
    if (auto id = Is<Ident>(n); id && id->vdef && writes && v.writable)
        NoteWritableRef(id->vdef, n);
    auto u = ast.New<Unary>(n->line, T_BITAND, n);
    u->synth = true;
    SlotRoots(v);
    v.type = ast.RefTo(ast.PlainOf(StorageType(v)), n->line);
    v.type->cq = !v.writable;   
    v.lvalue = false;
    u->exprtype = v.type;
    return u;
}








inline void TypeCheck::SlotRoots(Val &v) {
    if (!v.hasslot || v.type->kind != TY_SLICE) return;
    v.held = v;
    v.hasheld = true;
    v.TakeAlts(v.slot);
    v.writable = v.slot.writable;
    v.hasslot = false;
    for (auto &a : v.alts)
        if (auto sv = SliceVarOf(a.root)) sv->slotref = true;
}





inline TypeExpr *TypeCheck::StorageType(const Val &v) {
    return (v.lvalue || v.storagebranches) && v.isvarint ? ast.inttypes[IS_VARINT] : v.type;
}

inline bool TypeCheck::BindsRef(const Val &v, TypeExpr *dt) {
    return dt->kind == TY_REF && v.lvalue && v.type->kind != TY_REF && !v.isnull &&
           TypeEq(StorageType(v), dt->ref->sub);
}

inline bool TypeCheck::IsNonFixedLValue(const Val &v) {
    return v.lvalue && !IsRefOrSlice(v.type) && ClassOf(v.type) != SC_FIXED;
}



inline bool TypeCheck::IsNonFixedRef(const Val &v) {
    return IsPlainRef(v.type) && ClassOf(LoadType(v.type->ref->sub)) != SC_FIXED;
}



inline bool TypeCheck::Referenceable(Node *n, const Val &v) {
    if (ClassOf(v.type) != SC_RESIZABLE) return true;
    if (Is<Ident>(n)) return true;
    auto d = Is<Dot>(n);
    return d && FieldsInFrame(d);
}





inline bool TypeCheck::FieldsInFrame(Dot *d) {
    auto ot = d->obj->exprtype;
    if (!ot) return false;
    auto pointee = ot->kind == TY_REF;
    if (pointee) ot = ot->ref->sub;
    if (ot->kind != TY_STRUCT) return false;
    if (ot->struc->st->isroot) return true;
    if (!GetStructInst(ot)->frameobj) return false;
    auto od = Is<Dot>(d->obj);
    return pointee || !od || FieldsInFrame(od);
}



[[noreturn]] inline void TypeCheck::NoResizableRef(Node *at) {
    Error(at, "cannot reference a resizable value nested in a variable-size prefix "
              "or an ADT payload; reference the owning variable instead");
}

inline bool TypeCheck::UserRefOf(Node *n) {
    auto u = Is<Unary>(n);
    return u && u->op == T_BITAND && !u->synth;
}





inline bool TypeCheck::ImplicitCopy(const Val &v, Node *n, TypeExpr *dt) {
    if (!reachable) return false;
    if (IsRefOrSlice(dt) || dt->kind == TY_VOID) return false;
    if (ClassOf(dt) == SC_FIXED) return false;
    auto src = IsPlainRef(v.type) ? v.type->ref->sub : v.type;
    
    if (ClassOf(src) == SC_FIXED || IsVarintT(src)) return false;
    if (!v.lvalue && !IsPlainRef(v.type)) return false;
    if (inreturn && v.lvalue && IsOwnLocal(n)) return false;
    return true;
}

inline void TypeCheck::ImplicitCopyError(Node *n) {
    auto u = Is<Unary>(n);
    auto what = ExprStr(u && u->op == T_BITAND ? u->child : n);
    Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1): pass copy(",
                 what, ") for a copy, or bind it by reference"));
}


inline void TypeCheck::RefCopyWarning(Node *n) {
    auto what = ExprStr(Is<Unary>(n)->child);
    Warn(n, cat("redundant &: the construct's value is a copy of ", what, " either way (§4.1); "
                "a reference-typed binding binds ", what, " without it"));
}









inline void TypeCheck::CheckBranchCopy(const Val &v, Node *n, TypeExpr *dt, Val &out) {
    out.storagebranches = v.storagebranches || (v.lvalue && Referenceable(n, v)) ||
                          IsPlainRef(v.type);
    out.implicitcopy = v.implicitcopy;
    out.refcopies = v.refcopies;
    if (!ImplicitCopy(v, n, dt)) return;
    if (argpath != n && joinpath != n) ImplicitCopyError(n);
    if (!out.implicitcopy) out.implicitcopy = n;
}


inline bool TypeCheck::IsOwnLocal(Node *n) {
    auto id = Is<Ident>(n);
    return id && id->vdef && !id->vdef->isglobal && id->vdef->ownerspec == frames.back().spec;
}








inline Val TypeCheck::CheckInferredResult(Node *&n, FnSpec *tspec) {
    if (auto u = Is<Unary>(n); UserRefOf(n)) {
        auto v = CheckV(n, nullptr);
        n->exprtype = v.type;
        if (IsNonFixedRef(v) && !IsOwnLocal(u->child))
            Warn(n, cat("redundant &: ", ExprStr(u->child),
                        " is returned by reference without it (§4.1)"));
        return v;
    }
    auto v = CheckValue(n, nullptr, false, false, true);
    if (IsUntypedEmptyArray(v.type))
        Error(n, "cannot infer the element type of []: an omitted result type is taken from "
                 "the value returned (§7.1); declare the function's result type");
    if (IsNonFixedLValue(v) && !IsOwnLocal(n)) n = AutoRef(n, v);
    
    if (auto c = Is<Call>(n); c && c->rettypes.size() > 1) return v;
    NoRelRefCopy(n, v.type);
    if (reachable && IsNonFixedRef(v)) {
        auto dies = v.Any([&](const RootAlt &a) {
            return (a.root && a.root->ownerspec == tspec) || IsTemp(a.root);
        });
        if (dies) {
            auto what = ExprStr(n);
            Error(n, cat(what, " is not fixed-size and is not copied implicitly (§4.1), and "
                         "its storage does not outlive this function: return copy(", what,
                         ")"));
        }
    }
    return v;
}



inline Node *TypeCheck::WholeSlice(Node *n) {
    auto se = ast.New<SliceExpr>(n->line, n);
    se->cmpview = true;
    return se;
}









inline Val TypeCheck::CheckValue(Node *&n, TypeExpr *expected, bool callsite, bool branchcopy,
                                 bool inferred) {
    auto orig = n;
    auto v = CheckV(n, expected);
    
    if (!callsite) JudgeCastAt(orig, expected && expected->kind != TY_VOID ? expected : nullptr, v);
    
    
    if (auto c = Is<Call>(n); c && c->builtin == B_DEFAULT &&
        v.type->kind != TY_ARRAY && Is<StructLit>(c->defaultinit))
        n = c->defaultinit;
    auto dt = expected && expected->kind != TY_VOID ? expected : DecayedType(v);
    
    
    
    auto refcopy = branchcopy && UserRefOf(n) && IsPlainRef(v.type) && !KeepsRef(v, dt) &&
                   ClassOf(dt) == SC_FIXED;
    if (refcopy && argpath != n) RefCopyWarning(n);
    Val copied;
    if (!expected || expected->kind == TY_VOID) {
        if (branchcopy) CheckBranchCopy(v, n, dt, copied);
        if (!inferred || !IsNonFixedRef(v)) v = DecayRef(v);
    } else {
        if (!callsite && expected->kind == TY_REF && UserRefOf(n))
            Warn(n, cat("redundant &: ", ExprStr(Is<Unary>(n)->child),
                        " binds by reference here without it (§4.1)"));
        if (BindsRef(v, expected)) n = AutoRef(n, v, !expected->cq);
        if (branchcopy) CheckBranchCopy(v, n, expected, copied);
        else RequireCopyable(v, n, expected);
        if (!KeepsRef(v, expected)) v = DecayRef(v);
        auto from = LoadType(v.type);
        auto litfloat = v.litfloat;
        MustFit(v, n, expected);
        NoRelRefCopy(n, expected);
        if (reachable && expected->kind == TY_FLT) {
            if (IsIntT(from)) ToFloat(n, from, expected);
            else if (litfloat && !TypeEq(from, expected)) RetypeFlex(n, expected);
        }
    }
    if (refcopy && argpath == n) copied.refcopies.push_back(n);
    v.storagebranches = copied.storagebranches;
    v.implicitcopy = copied.implicitcopy;
    v.refcopies = std::move(copied.refcopies);
    n->exprtype = v.type;
    RecordVal(n, v);
    return v;
}


inline Val TypeCheck::CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite) {
    DestScope ds(*this, d);
    return CheckValue(n, expected, callsite);
}


inline Val TypeCheck::Operand(Node *n) {
    DestScope ds(*this, Dest {});
    auto v = DecayRef(CheckV(n, nullptr));
    n->exprtype = v.type;
    
    
    
    if (IsIndexT(v.type)) v.type = ast.NoIndex(v.type);
    return v;
}

inline void TypeCheck::MustFit(Val &v, Node *n, TypeExpr *dt) {
    FlagScope recording(recordingrelative, true);
    auto vt = DecayedType(v);
    if (vt->kind == TY_ARRAY && vt->arr->akind == A_FIXED)
        CheckArrayCount(n, dt, ArraySize(vt->arr));
    if (!reachable) return;  
    fitfail.clear();
    fitnode = n;
    auto from = LoadType(v.type);
    if (FitsAt(v, dt)) {
        if (IsIntT(dt) && !TypeEq(from, dt)) {
            RelyOnNamed(v, n);
            if (v.flexint && dt->intstorage != IS_VARINT) RetypeFlexInt(n, dt);
        } else if (IsF32(dt) && from->kind == TY_FLT && !IsF32(from)) {
            RelyOnNamed(v, n);
        }
    } else {
        
        
        if (!ast.unknowntypes.empty() && (MentionsUnknown(dt) || MentionsUnknown(v.type)))
            throw Abandoned {};
        if (!fitfail.empty()) Error(n, fitfail);
        auto got = dt->kind == TY_REF ? StorageType(v) : v.type;
        string hint;
        if (v.type->kind == TY_INT && dt->kind == TY_INT)
            hint = " (narrowing and sign changes require an explicit `as`)";
        else if (v.type->kind == TY_FLT && dt->kind == TY_INT)
            hint = " (a float converts to an integer only with an explicit `as`)";
        else if (v.type->kind == TY_FLT && !IsF32(v.type) && IsF32(dt)) {
            hint = " (f64 to f32 requires an explicit `as f32` to round the value)";
            
            
            
            if (auto c = Is<Call>(n); c && c->spec && !c->spec->bindings.empty())
                Append(hint, "; this generic call has committed to f64; choose f32 before "
                             "a typed argument, explicit type argument or overload commits "
                             "the computation");
        }
        Error(n, cat("expected a value of type ", TypeStr(dt), ", got ", TypeStr(got), hint));
    }
}







inline bool TypeCheck::FitsAt(Val &v, TypeExpr *dt) {
    auto t = v.type;
    
    
    if (BindsRef(v, dt)) {
        SlotRoots(v);
        t = v.type = ast.RefTo(ast.PlainOf(StorageType(v)), dt->line);
        v.type->cq = !v.writable;
        v.lvalue = false;
    }
    
    if (v.isnull) {
        if (dt->kind == TY_REF && dt->ref->optional) { v.type = dt; return true; }
        fitfail = cat("null is only a value of optional types, not ", TypeStr(dt));
        return false;
    }
    
    
    
    
    if (dt->kind == TY_SLICE) {
        auto at = IsPlainRef(t) && t->ref->sub->kind == TY_ARRAY ? t->ref->sub : t;
        if (at->kind == TY_ARRAY && TypeEq(at->arr->sub, dt->sub)) {
            if (at != t) v.ClearReads();   
            t = v.type = ast.SliceOf(at->arr->sub, dt->line);
            t->cq = !v.writable;
            v.lvalue = false;
            v.reusable = 0;   
        }
    }
    
    
    
    
    
    
    
    if (IsRefOrSlice(dt) && IsRefOrSlice(t) && !v.writable && !dt->cq && constslot) {
        fitfail = cat("storing a read-only ", dt->kind == TY_SLICE ? "slice" : "reference",
                      " of type ", TypeStr(t),
                      t->cq ? "" : " (read-only in this instantiation)",
                      " in a slot of type ", TypeStr(dt), " (§9.5); declare the slot const");
        return false;
    }
    auto holder = !IsRefOrSlice(dt) && !IsRefOrSlice(t) && HoldsPlainRef(dt);
    
    
    if (((IsRefOrSlice(dt) && IsRefOrSlice(t)) || holder) && !curdst.roots.None()) {
        const Roots &roots = holder ? ContentsOf(v) : v.AsRoots();
        
        
        
        
        
        
        
        auto reached = curdst.reached ? curdst.reached : dt;
        auto dsts = ShrinkTargets(curdst.roots, reached, curdst.slot);
        for (auto &a : roots.alts) {
            for (auto &d : dsts) {
                if (Depth(a.root) <= Depth(d.root)) continue;
                fitfail = cat("storing a reference rooted at ",
                              a.root ? a.root->name : string_view("static data"),
                              ", which does not outlive the destination");
                if (!curdst.roots.Exact())
                    Append(fitfail, ": it is reached through a reference that may point into ",
                           TargetStr(d));
                Append(fitfail, " (§9.2)");
                return false;
            }
        }
        
        
        if (!curdst.varbind || curdst.roots.None() || curdst.roots.Root()->isglobal) {
            auto reach = false;
            if (auto gs = StoredIntoGrowShrink(v, roots, t, holder, &reach)) {
                fitfail = NeverStoredError(gs, MayPointWording(roots, gs), reach);
                return false;
            }
        }
        
        
        
        auto spec = CurRealFrame().spec;
        auto ownvar = curdst.varbind && curdst.roots.Exact() &&
                      curdst.roots.Root()->ownerspec == spec;
        if (!CycleStorable(roots) && spec && !ownvar && (spec->incycle || spec->sf->isrec)) {
            
            
            
            
            
            
            auto joined = !spec->sf->isrec && spec->joinedat.line > 0
                              ? cat(spec->sf->name, " calls into its recursive cycle at ",
                                    Where(spec->joinedat))
                              : string();
            for (auto &a : roots.alts) {
                if (CycleStorable(a.root) || ThreadStorable(a.root)) continue;
                fitfail = CycleStoreError(a.root, joined);
                return false;
            }
            for (auto &d : dsts) {
                if (ThreadedChain(d.root)) continue;
                fitfail = CycleStoreError(d.root, joined);
                return false;
            }
        }
        
        
        
        if (!curdst.varbind) {
            for (auto &d : dsts) {
                if (holder) {
                    
                    
                    
                    if (!Is<StructLit>(fitnode) && !Is<ArrayLit>(fitnode))
                        RecordStore(d.root, roots, v.byteview, nullptr, v.holderfrom, reached,
                                    d.bound);
                } else {
                    RecordStore(d.root, roots, v.byteview, PointeeOf(t), nullptr, reached,
                                d.bound, curdst.slot,
                                t->kind == TY_REF && PointeeOf(t)->kind == TY_SLICE);
                    if (curdst.slot)
                        StoreIntoSlot(fitnode, d.root, d.bound, dt, v,
                                      curdst.roots.Exact()
                                          ? " through a reference"
                                          : " through a reference that may name it");
                }
            }
        } else if (curdst.roots.Exact() && curdst.roots.Root()->isglobal) {
            NoteGlobalBinding(curdst.roots.Root(), roots, v.byteview, PointeeOf(t),
                              fitnode ? fitnode->line : Line {});
        }
    }
    
    
    
    
    if (IsRefOrSlice(dt) ? TypeEq(t, dt) : TopConstEq(t, dt)) { v.type = dt; return true; }
    switch (dt->kind) {
        case TY_INT:
            if (t->kind != TY_INT) return false;
            
            
            
            
            if (dt->ix) {
                if (t->ix && t->ix->arr == dt->ix->arr && (dt->ix->upto || !t->ix->upto) &&
                    ImplicitInt(t->intstorage, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = cat("expected a value of type ", TypeStr(dt), ", got ", TypeStr(t),
                              v.ck == CK_INT || v.litint ? " constant" : "",
                              " (only `x as ", TypeStr(dt), "` makes one, checking x against ",
                              dt->ix->arr->name, ".len, §3.11)");
                return false;
            }
            
            
            if (v.unsized) {
                RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
                v.type = dt;
                return true;
            }
            if (v.flexint && dt->intstorage != IS_VARINT) {
                if (FlexFits(v, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = ConstsNoFit(v, dt);
                return false;
            }
            
            
            if (v.ck == CK_INT || (v.litint && dt->intstorage != IS_VARINT)) {
                if (v.litint ? ConstsFit(v, dt->intstorage)
                             : FitsIntStorage(v.ival, v.uns, dt->intstorage)) {
                    v.type = dt;
                    return true;
                }
                fitfail = ConstsNoFit(v, dt);
                return false;
            }
            if (dt->intstorage == IS_VARINT) {
                
                
                if (t->intstorage != IS_U64 && t->intstorage != IS_VARINT) {
                    v.type = dt;
                    return true;
                }
                return false;
            }
            if (ImplicitInt(t->intstorage, dt->intstorage)) {
                v.type = dt;
                return true;
            }
            return false;
        case TY_FLT:
            
            if (t->kind == TY_INT) {
                IntToFloat(v, dt);
                return true;
            }
            if (t->kind != TY_FLT) return false;
            
            
            if (IsF32(dt)) { if (!LitFloat(v)) return false; }
            else if (!IsF32(t)) return false;
            if (v.unsized) RecordLitAdapt(v, dt, fitnode ? fitnode->line : Line {});
            v.type = dt;
            return true;
        case TY_ARRAY: {
            
            
            
            TypeExpr *selem = nullptr;
            if (t->kind == TY_ARRAY) selem = t->arr->sub;
            else if (t->kind == TY_SLICE) selem = t->sub;
            else return false;
            if (v.emptyarr) {
                if (dt->arr->akind == A_FIXED && ArraySize(dt->arr) != 0) return false;
                v.type = dt;
                return true;
            }
            if (dt->arr->akind == A_FIXED) return false;
            if (!TypeEq(selem, dt->arr->sub)) return false;
            v.type = dt;
            return true;
        }
        case TY_SLICE:
            
            
            
            if (t->kind == TY_SLICE && TypeEq(t->sub, dt->sub)) {
                v.type = dt;
                return true;
            }
            return false;
        case TY_REF: {
            if (t->kind != TY_REF) return false;
            if (!TypeEq(t->ref->sub, dt->ref->sub)) return false;
            if (t->ref->lenstorage >= 0) return false;  
            if (dt->ref->lenstorage >= 0) {
                
                
                
                
                if (t->ref->optional && !dt->ref->optional) return false;
                
                
                
                
                
                
                
                
                
                
                
                auto nullalt = [&](const RootAlt &a) {
                    return !a.root && a.exact && t->ref->optional &&
                           (v.writable || !StaticCanContain(t->ref->sub));
                };
                Roots links;
                for (auto &a : v.alts) if (!nullalt(a)) links.Add(a);
                
                
                if (links.None() || (!dt->ref->pool && curdst.unknown)) {
                    v.type = dt;
                    return true;
                }
                
                
                auto want = dt->ref->pool ? dt->ref->pool : curdst.roots.Root();
                auto have = dt->ref->pool ? PoolOf(links.Root()) : links.Root();
                auto fits = dt->ref->pool ? want && links.Exact() && have == want
                    : RelativeDestination(links, curdst, fitnode, recordingrelative);
                if (!fits) {
                    auto why = links.Exact() ? string() : ReadBackWhy(links);
                    auto vroot = links.Root() ? links.Root()->name : string_view("static data");
                    fitfail = cat(dt->ref->pool
                                      ? cat("a relative reference in ", want->name,
                                            " must point into ", want->name, " (§3.9); ")
                                      : string("a relative reference must point within the "
                                               "same root as its location (§3.9); "),
                                  !why.empty() ? why
                                  : !links.Exact()
                                      ? cat("this reference's root is not known exactly, "
                                            "only that it outlives ", vroot)
                                  : cat("this reference is rooted at ", vroot));
                    return false;
                }
                v.type = dt;
                return true;
            }
            
            if (dt->ref->optional && !t->ref->optional) { v.type = dt; return true; }
            
            if (dt->ref->optional == t->ref->optional) { v.type = dt; return true; }
            return false;
        }
        case TY_ENUM: {
            
            
            auto adt = AdtOf(t);
            if (!adt || !SameAdt(adt, dt)) return false;
            if (!dt->enu->varmode && !GetEnumInst(dt)->allfixed) return false;
            v.type = dt;
            return true;
        }
        default: return false;
    }
}

inline string TypeCheck::ConstStr(const Val &v) {
    return v.uns ? cat((uint64_t)v.ival) : cat(v.ival);
}




inline Val TypeCheck::CheckCond(Node *n) {
    DestScope ds(*this, Dest {});
    FlagScope rs(inreturn, false);
    auto v = CheckV(n, nullptr);
    auto id = Is<Ident>(n);
    auto narrowedopt = id && id->vdef && IsOptional(id->vdef->type) && id->vdef->narrowed;
    if (!narrowedopt) v = DecayRef(v);
    n->exprtype = v.type;
    if (!narrowedopt && v.type->kind != TY_BOOL && !IsOptional(v.type))
        Error(n, cat("condition must be bool or an optional, got ", TypeStr(v.type)));
    return v;
}




inline TypeExpr *TypeCheck::UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue) {
    if (!a) return b;
    if (!b) return a;
    if (TypeEq(a, b)) return a;
    if (TopConstEq(a, b)) return a->cq ? a : b;   
    if (a->kind == TY_INT && b->kind == TY_INT && (a->ix || b->ix)) {
        
        
        
        if (a->ix && b->ix && a->ix->arr == b->ix->arr) {
            auto upto = a->ix->upto || b->ix->upto;
            if (ImplicitInt(a->intstorage, b->intstorage))
                return ast.IndexTypeOf(b->intstorage, b->ix->arr, upto, at->line);
            if (ImplicitInt(b->intstorage, a->intstorage))
                return ast.IndexTypeOf(a->intstorage, a->ix->arr, upto, at->line);
        }
        return UnifyBranch(ast.NoIndex(a), ast.NoIndex(b), at, wantvalue);
    }
    if (a->kind == TY_INT && b->kind == TY_INT) {
        if (ImplicitInt(a->intstorage, b->intstorage)) return b;
        if (ImplicitInt(b->intstorage, a->intstorage)) return a;
    }
    if (a->kind == TY_FLT && b->kind == TY_FLT) return ast.flttypes[FS_F64];
    
    if (a->kind == TY_INT && b->kind == TY_FLT) return b;
    if (a->kind == TY_FLT && b->kind == TY_INT) return a;
    if (!wantvalue) return ast.voidtype;
    Error(at, cat("branches have mismatched types: ", TypeStr(a), " vs ", TypeStr(b)));
}

inline Val TypeCheck::VoidVal() {
    Val v;
    v.type = ast.voidtype;
    return v;
}




inline Val TypeCheck::CheckRefOf(Unary *x) {
    DestScope ds(*this, Dest {});
    auto lv = CheckLValue(x->child);
    if (lv.var) RequireAssigned(lv.var, x);
    
    
    if (!lv.var && !lv.fotail && lv.type->kind != TY_REF &&
        ClassOf(lv.type) == SC_RESIZABLE)
        NoResizableRef(x);
    if (lv.type->kind == TY_REF) {
        
        if (!lv.var) return ContainerRead(lv);
        Val v;
        v.type = LoadType(lv.type);  
        v.SetProv(RefProvOf(lv.var));
        return v;
    }
    Val v;
    v.type = ast.RefTo(ast.PlainOf(lv.type), x->line);
    v.SetProv(lv);
    
    
    
    v.writable = lv.writable && !lv.isvarint && (lv.type->kind == TY_SLICE || !lv.type->cq);
    
    
    
    
    if (lv.var && lv.type->kind == TY_SLICE)
        v.writable = v.writable && RefProvOf(lv.var).writable;
    v.type->cq = !v.writable;   
    if (lv.var && v.writable) NoteWritableRef(lv.var, x);
    if (auto sv = SliceVarOf(lv.var)) sv->slotref = true;
    return v;
}









inline void TypeCheck::NoteWritableRef(VarDef *d, Node *at) {
    if (!d->refwrite) d->refwrite = at;
    if (d->nonneguse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "become negative, while the comparison with a u64 at ",
                      Where(d->nonneguse->line), " relies on its value being non-negative "
                      "(§4.4, §6.1); convert the signed side with `as` there"));
    if (d->markuse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "change, while the resize at ", Where(d->markuse->line), " relies on it "
                      "still holding the length it was bound to (§4.4, §5.2); bind the "
                      "reference to a copy of it, or make ", d->name, " a var"));
    if (d->constuse)
        Error(at, cat(d->name, " is bound to a writable reference here, through which it may "
                      "change, while the ", d->constwhat, " at ", Where(d->constuse->line),
                      " relies on it keeping its initializer's value, as a named constant "
                      "(§4.4, §11.1); bind the reference to a copy of it, or declare ",
                      d->name, " const"));
    if (d->extentuse && !TypeFixesExtent(d, d->extentcap)) {
        auto what = d->extentcap ? "capacity" : "length";
        Error(at, cat(d->name, " is bound to a writable reference here, through which its ",
                      what, " may change, while the ", d->extentwhat, " at ",
                      Where(d->extentuse->line), " relies on it keeping its initializer's "
                      "(§3.3, §4.4, §11.1); bind the reference to a copy of it, or give ",
                      d->name, " a type that fixes its ", what));
    }
}





inline VarDef *TypeCheck::RelyOnNonneg(const Val &v, Node *at) {
    for (auto d = v.nonnegfrom; d; d = d->nonnegfrom) {
        if (d->refwrite) return d;
        if (!d->nonneguse) d->nonneguse = at;
    }
    return nullptr;
}






inline void TypeCheck::RelyOnConstant(ConstUse &use, VarDef *d) {
    if (d->refwrite)
        Error(use.at, cat("the ", use.what, " relies on ", use.named->name,
                          " keeping its initializer's value",
                          d != use.named ? cat(", which rests on ", d->name, "'s") : string(),
                          ", but a writable reference bound to ", d->name, " at ",
                          Where(d->refwrite->line), " may change it (§4.4, §11.1); bind that "
                          "reference to a copy of it, or declare ", d->name, " const"));
    if (!d->constuse) {
        d->constuse = use.at;
        d->constwhat = use.what;
    }
}





inline void TypeCheck::RelyOnNamed(const Val &v, Node *at) {
    if (unifytrial || !v.constfrom) return;
    ConstUse use { at, "use as a literal", v.constfrom };
    for (auto d = v.constfrom; d; d = d->constfrom) RelyOnConstant(use, d);
}



inline bool TypeCheck::TypeFixesExtent(VarDef *d, bool cap) {
    auto t = d->type;
    if (!t || t->kind != TY_ARRAY) return false;
    return cap ? t->arr->akind == A_LIMITED && t->arr->sizeexpr : t->arr->akind == A_FIXED;
}













inline void TypeCheck::RelyOnExtent(ConstUse &use, VarDef *d, bool cap) {
    if (TypeFixesExtent(d, cap)) return;
    auto what = cap ? "capacity" : "length";
    auto rests = d != use.named ? cat(", which rests on ", d->name, "'s") : string();
    if (d->type && d->isvar)
        Error(use.at, cat("the ", use.what, " relies on ", use.named->name,
                          " keeping its initializer's ", what, rests, ", but ", d->name,
                          " is a var of type ", TypeStr(d->type), ", which does not fix it "
                          "(§3.3); declare ", d->name, " let, or give it a type that fixes "
                          "its ", what));
    if (d->refwrite)
        Error(use.at, cat("the ", use.what, " relies on ", use.named->name,
                          " keeping its initializer's ", what, rests,
                          ", but a writable reference bound to ", d->name, " at ",
                          Where(d->refwrite->line), " may change it (§3.3, §4.4, §11.1); bind "
                          "that reference to a copy of it, or give ", d->name, " a type that "
                          "fixes its ", what));
    if (!d->extentuse) {
        d->extentuse = use.at;
        d->extentwhat = use.what;
        d->extentcap = cap;
    }
}




inline void TypeCheck::CheckVarExtent(VarDef *d, Node *at) {
    if (!d->isvar || !d->extentuse || TypeFixesExtent(d, d->extentcap)) return;
    auto what = d->extentcap ? "capacity" : "length";
    Error(at, cat(d->name, " is a var of type ", TypeStr(d->type), ", which does not fix its ",
                  what, ", while the ", d->extentwhat, " at ", Where(d->extentuse->line),
                  " relies on it keeping its initializer's (§3.3); declare ", d->name,
                  " let, or give it a type that fixes its ", what));
}






inline void TypeCheck::FoldInt(TType op, Val &l, Val &r, Val &out, Node *at) {
    if (l.ck != CK_INT || r.ck != CK_INT) return;
    auto s = out.type->intstorage;
    if (s == IS_VARINT) return;
    if ((op == T_DIV || op == T_MOD) && !r.ival) {
        if (r.constfrom) return;
        Error(at, "constant division by zero");
    }
    int64_t res;
    if (!FoldIntOp(op, l.ival, r.ival, s, res)) return;
    out.ck = CK_INT;
    out.ival = res;
    out.uns = s == IS_U64 && res < 0;
    
    
    if (l.constfrom && r.constfrom) RelyOnNamed(r, at);
    out.constfrom = l.constfrom ? l.constfrom : r.constfrom;
}






inline TypeExpr *TypeCheck::UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt,
                                         TypeExpr *rt, bool cmp) {
    auto fail = [&](const string &msg) -> TypeExpr * {
        if (!unifytrial) Error(at, msg);
        return nullptr;
    };
    if (IsIntT(lt) && IsIntT(rt)) {
        if (TypeEq(lt, rt)) return lt;
        
        
        auto lconst = lv.ck == CK_INT || lv.litint || lv.flexint,
             rconst = rv.ck == CK_INT || rv.litint || rv.flexint;
        
        
        if (lv.unsized && !rv.unsized && !rconst) {
            RecordLitAdapt(lv, rt, at->line);
            return rt;
        }
        if (rv.unsized && !lv.unsized && !lconst) {
            RecordLitAdapt(rv, lt, at->line);
            return lt;
        }
        if (lconst && rconst) {
            if (lv.uns || rv.uns) {
                auto neg = [](const Val &x) {
                    return (x.litint || x.flexint) ? x.litlo < 0 : !x.uns && x.ival < 0;
                };
                if (neg(lv) || neg(rv))
                    return fail("constant operands have no common type (one is above "
                                "i64.max, the other negative)");
                return ast.inttypes[IS_U64];
            }
            return ast.inttypes[IS_I64];
        }
        auto fits = [&](const Val &c, TypeExpr *t) {
            return c.flexint ? FlexFits(c, t->intstorage)
                   : c.litint ? ConstsFit(c, t->intstorage)
                              : FitsIntStorage(c.ival, c.uns, t->intstorage);
        };
        if (lconst) {
            if (!fits(lv, rt)) return fail(ConstsNoFit(lv, rt));
            RelyOnNamed(lv, at);
            return rt;
        }
        if (rconst) {
            if (!fits(rv, lt)) return fail(ConstsNoFit(rv, lt));
            RelyOnNamed(rv, at);
            return lt;
        }
        if (ImplicitInt(lt->intstorage, rt->intstorage)) return rt;
        if (ImplicitInt(rt->intstorage, lt->intstorage)) return lt;
        
        
        
        
        
        
        
        if (cmp) {
            auto isu64 = [](TypeExpr *t) { return t->intstorage == IS_U64; };
            if (isu64(lt) != isu64(rt)) {
                auto &sv = isu64(lt) ? rv : lv;
                auto st = isu64(lt) ? rt : lt;
                if (!IsUnsigned(st->intstorage)) {
                    if (sv.nonneg) {
                        
                        
                        if (unifytrial) {
                            for (auto d = sv.nonnegfrom; d; d = d->nonnegfrom)
                                if (d->refwrite) return nullptr;
                            return ast.inttypes[IS_U64];
                        }
                        if (auto w = RelyOnNonneg(sv, at)) {
                            auto d = sv.nonnegfrom;
                            Error(at, cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                          " relies on ", d->name, " being non-negative",
                                          w != d ? cat(", which rests on ", w->name) : string(),
                                          ", but a writable reference bound to ", w->name,
                                          " at ", Where(w->refwrite->line), " may make it "
                                          "negative (§4.4, §6.1); convert it with `as`"));
                        }
                        return ast.inttypes[IS_U64];
                    }
                    return fail(cat("comparing ", TypeStr(lt), " with ", TypeStr(rt),
                                    " needs the signed side to be known non-negative "
                                    "(a literal, .len/.cap, or a `let` bound to one); "
                                    "convert it with `as` otherwise"));
                }
            }
        }
        return fail(cat("operands of ", TName(op), " have no common type: ", TypeStr(lt),
                        " and ", TypeStr(rt), " (convert one with `as`)"));
    }
    if (lt->kind == TY_FLT && rt->kind == TY_FLT) {
        if (TypeEq(lt, rt)) return lt;
        
        
        auto &wide = IsF32(lt) ? rv : lv;
        auto narrow = IsF32(lt) ? lt : rt;
        if (LitFloat(wide)) {
            RecordLitAdapt(wide, narrow, at->line);
            RelyOnNamed(wide, at);
            return narrow;
        }
        return ast.flttypes[FS_F64];
    }
    
    if (lt->kind == TY_FLT && IsIntT(rt)) return lt;
    if (IsIntT(lt) && rt->kind == TY_FLT) return rt;
    return nullptr;
}






inline void TypeCheck::RetypeOperand(Node *&n, Val &v, TypeExpr *ct) {
    if (auto t = LoadType(v.type); ct->kind == TY_FLT && IsIntT(t)) {
        ToFloat(n, t, ct);
    } else {
        if (v.litfloat && !TypeEq(v.type, ct)) RetypeFlex(n, ct);
        else if (v.flexint && !TypeEq(v.type, ct)) RetypeFlexInt(n, ct);
        else if (v.litint && !TypeEq(v.type, ct)) RetypeBranches(n, ct);
        n->exprtype = ct;
    }
    RetypeVal(v, ct);
}

inline void TypeCheck::RetypeOperands(Node *&left, Node *&right, Val &lv, Val &rv,
                                      TypeExpr *ct) {
    RetypeOperand(left, lv, ct);
    RetypeOperand(right, rv, ct);
}


inline void TypeCheck::RetypeVal(Val &v, TypeExpr *ct) {
    if (ct->kind == TY_FLT && IsIntT(LoadType(v.type))) IntToFloat(v, ct);
    else v.type = ct;
}

inline Val TypeCheck::CheckRefIdentity(Binary *b) {
    DestScope ds(*this, Dest {});
    
    
    
    auto lv = CheckV(b->left, nullptr);
    if (lv.lvalue && !IsRefOrSlice(lv.type)) {
        b->left = AutoRef(b->left, lv, false);
        RecordVal(b->left, lv);
    }
    auto rv = CheckV(b->right, nullptr);
    if (rv.lvalue && !IsRefOrSlice(rv.type))
        b->right = AutoRef(b->right, rv, false);
    b->left->exprtype = lv.type;
    b->right->exprtype = rv.type;
    auto isref = [&](const Val &v) { return v.isnull || v.type->kind == TY_REF; };
    if (!isref(lv) || !isref(rv))
        Error(b, cat(TName(b->op), " compares references by address; got ",
                     TypeStr(lv.type), " and ", TypeStr(rv.type)));
    if (!lv.isnull && !rv.isnull && !TypeEq(lv.type->ref->sub, rv.type->ref->sub))
        Error(b, cat(TName(b->op), " needs references to the same type, got ",
                     TypeStr(lv.type), " and ", TypeStr(rv.type)));
    Val v;
    v.type = ast.booltype;
    return v;
}

inline Val TypeCheck::CheckLogical(Binary *b) {
    CheckCond(b->left);
    auto snap = SaveFlow();
    NarrowCond(b->left, b->op == T_ANDAND);
    auto mid = SaveFlow();
    CheckCond(b->right);
    
    
    
    
    b->rightkills.clear();
    vector<VarDef *> mayassign;
    for (auto &e : mid.locals) {
        if (!InScope(e.var, e.index)) continue;
        auto v = e.var;
        if (e.state.narrowed && !v->narrowed) b->rightkills.push_back(v);
        if (v->maybeassigned) mayassign.push_back(v);
    }
    for (auto [gv, gn] : mid.globals)
        if (gn && !gv->narrowed) b->rightkills.push_back(gv);
    
    auto rightkills = cur.kills;
    RestoreFlow(snap);
    erase_if(cur.kills, [&](const Kill &k) {
        return none_of(rightkills.begin(), rightkills.end(), [&](const Kill &l) {
            return l.container == k.container && l.base == k.base && l.path == k.path;
        });
    });
    for (auto kv : b->rightkills) kv->narrowed = nullptr;
    for (auto v : mayassign) v->maybeassigned = true;
    Val v;
    v.type = ast.booltype;
    return v;
}

inline Val TypeCheck::CheckBinaryResult(Binary *b, Val &lv, Val &rv) {
    NoUntypedEmptyArray(lv, b->left, TName(b->op));
    NoUntypedEmptyArray(rv, b->right, TName(b->op));
    auto lt = LoadType(lv.type), rt = LoadType(rv.type);
    
    
    if (IsOpaque(lt) || IsOpaque(rt)) {
        Val v;
        auto compare = b->op == T_EQ || b->op == T_NEQ || b->op == T_LT || b->op == T_GT ||
                       b->op == T_LTEQ || b->op == T_GTEQ;
        v.type = compare ? ast.booltype : IsOpaque(lt) ? lt : rt;
        return v;
    }
    if (ClassOf(lt) == SC_ROOT || ClassOf(rt) == SC_ROOT)
        Error(b, "root owners do not support value operators; operate on their fields instead");
    auto numeric = [](TypeExpr *t) { return IsIntT(t) || t->kind == TY_FLT; };
    if (b->op == T_EQ || b->op == T_NEQ) {
        Val v;
        v.type = ast.booltype;
        
        
        if (lv.isnull || rv.isnull) {
            auto othernode = lv.isnull ? b->right : b->left;
            auto &other = lv.isnull ? rt : lt;
            auto oid = Is<Ident>(othernode);
            auto narrowedopt = oid && oid->vdef && IsOptional(oid->vdef->type);
            if (!IsOptional(other) && !narrowedopt && !(lv.isnull && rv.isnull))
                Error(b, cat("only optionals compare against null, not ",
                             TypeStr(other)));
            return v;
        }
        if (!numeric(lt) || !numeric(rt)) {
            if (lt->kind == TY_FN || lt->kind == TY_VOID)
                Error(b, "these values cannot be compared");
            
            
            auto elemof = [](TypeExpr *t) -> TypeExpr * {
                if (t->kind == TY_SLICE) return t->sub;
                if (t->kind == TY_ARRAY) return t->arr->sub;
                return nullptr;
            };
            if (!TypeEq(lt, rt) && elemof(lt) && elemof(rt) &&
                TypeEq(elemof(lt), elemof(rt))) {
                if (lt->kind != TY_SLICE) {
                    b->left = WholeSlice(b->left);
                    lv = Operand(b->left);
                    lt = LoadType(lv.type);
                }
                if (rt->kind != TY_SLICE) b->right = WholeSlice(b->right);
                
                
                
                rv = Operand(b->right);
                rt = LoadType(rv.type);
            }
            if (!TopConstEq(lt, rt))
                Error(b, cat("== requires operands of the same type, got ",
                             TypeStr(lt), " and ", TypeStr(rt)));
            return v;
        }
    }
    if ((b->op == T_PLUS || b->op == T_MINUS || b->op == T_MUL || b->op == T_DIV || b->op == T_MOD) &&
        (!numeric(lt) || !numeric(rt))) {
        
        
        if (TypeEq(lt, rt) && ElementwiseOK(lt)) {
            Val v;
            v.type = lt;
            return v;
        }
        if ((b->op == T_MUL || b->op == T_DIV) && numeric(lt) != numeric(rt)) {
            auto aggregate = numeric(lt) ? rt : lt;
            if (auto scalar = ElementwiseScalarType(aggregate)) {
                auto &sn = numeric(lt) ? b->left : b->right;
                auto &sv = numeric(lt) ? lv : rv;
                auto fitted = sv;
                MustFit(fitted, sn, scalar);
                RetypeOperand(sn, sv, scalar);
                Val v;
                v.type = aggregate;
                return v;
            }
        }
        Error(b, cat("operator ", TName(b->op), " cannot be applied to ",
                     TypeStr(lt), " and ", TypeStr(rt)));
    }
    
    auto l = b->left, r = b->right;
    TypeExpr *ct = nullptr;
    auto v = NumericBinary(b, lv, rv, ct, false);
    JudgeBinaryCasts(b, l, r, lv, rv, v, ct);
    return v;
}






inline void TypeCheck::DecidedComparison(Binary *b, const Val &lv, const Val &rv, TypeExpr *lt,
                                         TypeExpr *rt) {
    auto isk = [](const Val &v) { return v.ck == CK_INT && !v.uns && !v.unsized; };
    if (isk(lv) == isk(rv) || InGenericCode()) return;
    
    auto op = b->op;
    if (isk(lv)) op = op == T_LT ? T_GT : op == T_GT ? T_LT : op == T_LTEQ ? T_GTEQ : T_LTEQ;
    auto xt = isk(lv) ? rt : lt;
    auto k = isk(lv) ? lv.ival : rv.ival;
    if (!xt || !IsIntT(xt)) return;
    auto [lo, hi] = IntRange(xt->intstorage);
    
    auto hiknown = xt->intstorage != IS_U64;
    int r = -1;   
    switch (op) {
        case T_LT:   r = lo >= k ? 0 : hiknown && hi < k ? 1 : -1; break;
        case T_LTEQ: r = lo > k ? 0 : hiknown && hi <= k ? 1 : -1; break;
        case T_GT:   r = lo > k ? 1 : hiknown && hi <= k ? 0 : -1; break;
        case T_GTEQ: r = lo >= k ? 1 : hiknown && hi < k ? 0 : -1; break;
        default: break;
    }
    if (r < 0) return;
    Warn(b, cat("comparison is always ", r ? "true" : "false", ": ", TypeStr(xt), " values are ",
                hiknown ? cat(lo, " to ", hi) : cat(lo, " or more")));
}





inline Val TypeCheck::NumericBinary(Binary *b, Val lv, Val rv, TypeExpr *&ct, bool trial) {
    auto op = b->op;
    auto lt = LoadType(lv.type), rt = LoadType(rv.type);
    FlagScope ut(unifytrial, unifytrial || trial);
    FlagScope lr(litrecord, litrecord && !trial);
    ct = nullptr;
    auto fail = [&](const string &msg) {
        if (!trial) Error(b, msg);
        return Val {};
    };
    auto retype = [&]() {
        if (trial) {
            RetypeVal(lv, ct);
            RetypeVal(rv, ct);
        } else {
            RetypeOperands(b->left, b->right, lv, rv, ct);
        }
    };
    
    auto fold = [&](Val &v) {
        if (trial && lv.ck == CK_INT && rv.ck == CK_INT && (op == T_DIV || op == T_MOD) &&
            !rv.ival && !rv.constfrom)
            return false;
        FoldInt(op, lv, rv, v, b);
        return true;
    };
    
    
    
    
    auto flexint = [&](Val &v, bool shift) {
        int64_t llo, lhi, rlo, rhi;
        if (v.ck == CK_INT || !FlexConsts(lv, llo, lhi)) return;
        if (shift) {
            v.flexint = true;
            v.litlo = llo;
            v.lithi = lhi;
        } else if ((lv.flexint || rv.flexint || lv.litint || rv.litint) &&
                   FlexConsts(rv, rlo, rhi)) {
            v.flexint = true;
            v.litlo = std::min(llo, rlo);
            v.lithi = std::max(lhi, rhi);
        }
        if (!trial) b->flexint = v.flexint;
    };
    Val v;
    switch (op) {
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ:
            ct = UnifyNumeric(b, op, lv, rv, lt, rt, true);
            if (!ct)
                return fail(cat("ordering comparison requires numeric operands, got ",
                                TypeStr(lt), " and ", TypeStr(rt)));
            retype();
            if (!trial) DecidedComparison(b, lv, rv, lt, rt);
            v.type = ast.booltype;
            return v;
        case T_EQ: case T_NEQ:
            ct = UnifyNumeric(b, op, lv, rv, lt, rt, true);
            if (!ct) return Val {};
            retype();
            v.type = ast.booltype;
            return v;
        case T_SHL: case T_SHR:
            
            
            if (!IsIntT(lt) || !IsIntT(rt))
                return fail(cat("shift requires integer operands, got ", TypeStr(lt), " and ",
                                TypeStr(rt)));
            ct = v.type = lv.ck == CK_INT ? ast.inttypes[lv.uns ? IS_U64 : IS_I64] : lt;
            if (!trial) b->left->exprtype = v.type;
            lv.type = v.type;
            if (!fold(v)) return Val {};
            flexint(v, true);
            return v;
        case T_BITAND: case T_BITOR: case T_XOR:
            if (!IsIntT(lt) || !IsIntT(rt))
                return fail(cat("bitwise operator requires integer operands, got ", TypeStr(lt),
                                " and ", TypeStr(rt)));
            ct = UnifyNumeric(b, op, lv, rv, lt, rt);
            if (!ct) return Val {};
            retype();
            v.type = ct;
            if (!fold(v)) return Val {};
            flexint(v, false);
            return v;
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            ct = UnifyNumeric(b, op, lv, rv, lt, rt);
            if (!ct) return Val {};
            
            
            
            auto literal = [&](const Val &x, TypeExpr *t) { return IsIntT(t) || LitFloat(x); };
            auto flex = ct->kind == TY_FLT && literal(lv, lt) && literal(rv, rt);
            retype();
            v.type = ct;
            if (ct->kind == TY_INT) {
                if (!fold(v)) return Val {};
                flexint(v, false);
            } else if (lv.ck == CK_FLT && rv.ck == CK_FLT && op != T_MOD) {
                
                auto x = lv.fval, y = rv.fval;
                if (IsF32(ct)) { x = (float)x; y = (float)y; }
                v.ck = CK_FLT;
                switch (op) {
                    case T_PLUS:  v.fval = x + y; break;
                    case T_MINUS: v.fval = x - y; break;
                    case T_MUL:   v.fval = x * y; break;
                    default:      v.fval = y != 0 ? x / y : 0; break;
                }
                if (IsF32(ct)) v.fval = (double)(float)v.fval;
            }
            v.litfloat = flex && v.ck != CK_FLT;
            if (!trial) b->litfloat = v.litfloat;
            if (flex) {
                
                if (lv.constfrom && rv.constfrom) RelyOnNamed(rv, b);
                v.constfrom = lv.constfrom ? lv.constfrom : rv.constfrom;
            }
            return v;
        }
        default:
            assert(false);
            return Val {};
    }
}




inline Val TypeCheck::NumericUnary(Unary *u, const Val &cv, bool trial) {
    auto v = cv;
    if (v.notconst && u->op == T_MINUS) {
        v.flexint = v.notconst = false;
        v.ck = CK_INT;
        v.ival = ~v.litlo;
    }
    auto t = LoadType(v.type);
    auto fail = [&](const string &msg) {
        if (!trial) Error(u, msg);
        return Val {};
    };
    Val r;
    switch (u->op) {
        case T_MINUS:
            if (IsIntT(t)) {
                
                
                
                if (v.ck == CK_INT && (v.uns || v.ival != INT64_MIN)) {
                    
                    
                    if (v.uns && v.ival != INT64_MIN)
                        return fail("negated constant too large for i64");
                    r.type = ast.inttypes[IS_I64];
                    r.ck = CK_INT;
                    r.ival = v.uns ? INT64_MIN : -v.ival;
                    r.constfrom = v.constfrom;
                    if (!trial) u->child->exprtype = r.type;
                    return r;
                }
                if (IsUnsigned(t->intstorage))
                    return fail(cat("cannot negate a value of unsigned type ", TypeStr(t),
                                    " (convert with `as`)"));
                r.type = t;
            } else if (t->kind == TY_FLT) {
                r.type = t;
                if (v.ck == CK_FLT) {
                    r.ck = CK_FLT;
                    r.fval = -v.fval;
                } else {
                    r.litfloat = LitFloat(v);
                }
                r.constfrom = v.constfrom;
                if (!trial) u->litfloat = r.litfloat;
            } else if (ElementwiseOK(t)) {
                
                
                if (auto leaf = UnsignedLeaf(t))
                    return fail(cat("cannot negate a value of type ", TypeStr(t),
                                    ", which holds the unsigned type ", TypeStr(leaf)));
                r.type = t;
            } else {
                return fail(cat("cannot negate a value of type ", TypeStr(t)));
            }
            return r;
        case T_BITNOT:
            if (!IsIntT(t)) return fail(cat("~ requires an integer, got ", TypeStr(t)));
            
            
            
            if (v.ck == CK_INT && !v.uns && v.ival >= 0) {
                r.type = ast.inttypes[IS_I64];
                r.flexint = r.notconst = true;
                r.litlo = r.lithi = v.ival;
                r.constfrom = v.constfrom;
                if (!trial) {
                    u->child->exprtype = r.type;
                    u->flexint = true;
                }
                return r;
            }
            if (v.ck == CK_INT) {
                r.type = ast.inttypes[v.uns ? IS_U64 : IS_I64];
                r.ck = CK_INT;
                r.ival = ~v.ival;
                r.uns = v.uns && r.ival < 0;
                r.constfrom = v.constfrom;
                if (!trial) u->child->exprtype = r.type;
                return r;
            }
            r.type = t;
            if (v.flexint) {
                r.flexint = true;
                r.litlo = v.litlo;
                r.lithi = v.lithi;
            }
            if (!trial) u->flexint = r.flexint;
            return r;
        default:
            assert(false);
            return Val {};
    }
}




inline void TypeCheck::IntToFloat(Val &v, TypeExpr *ft) {
    if (v.ck == CK_INT) {
        v.ck = CK_FLT;
        v.fval = v.uns ? (double)(uint64_t)v.ival : (double)v.ival;
        if (IsF32(ft)) v.fval = v.uns ? (double)(float)(uint64_t)v.ival : (double)(float)v.ival;
        v.uns = false;
    }
    v.unsized = false;
    v.unsizedparam = nullptr;
    v.litint = false;
    v.flexint = false;
    v.nonneg = false;
    v.lvalue = false;
    v.type = ft;
}






inline void TypeCheck::ToFloat(Node *&n, TypeExpr *from, TypeExpr *ft) {
    auto a = Is<AsCast>(n);
    if (!a || !a->implicit) {
        n->exprtype = from;
        a = ast.New<AsCast>(n->line, n, ft, false);
        a->implicit = true;
        n = a;
    }
    a->type = a->totype = ft;
    a->exprtype = ft;
}







inline void TypeCheck::RetypeFlex(Node *&n, TypeExpr *t) {
    n->exprtype = t;
    if (auto b = Is<Binary>(n); b && b->litfloat) {
        RetypeFlex(b->left, t);
        RetypeFlex(b->right, t);
    } else if (auto u = Is<Unary>(n); u && u->litfloat) {
        RetypeFlex(u->child, t);
    } else if (auto a = Is<AsCast>(n); a && a->implicit) {
        a->type = a->totype = t;
    } else {
        RetypeBranches(n, t);
    }
}





inline void TypeCheck::RetypeFlexInt(Node *n, TypeExpr *t) {
    n->exprtype = t;
    if (auto b = Is<Binary>(n); b && b->flexint) {
        RetypeFlexInt(b->left, t);
        if (b->op != T_SHL && b->op != T_SHR) RetypeFlexInt(b->right, t);
    } else if (auto u = Is<Unary>(n); u && u->flexint) {
        RetypeFlexInt(u->child, t);
    } else {
        RetypeBranches(n, t);
    }
}














inline bool TypeCheck::InGenericCode() {
    auto generic = [](const SFunction *sf) { return sf && sf->IsGeneric(); };
    for (auto i = (int)frames.size() - 1; i >= 0;) {
        auto &fr = frames[i];
        if (fr.isfunval && !fr.isdefault) return true;
        if (generic(fr.sf) || generic(fr.defaultfn)) return true;
        for (auto sp = fr.lexspec; sp; sp = sp->lexparent)
            if (!sp->bindings.empty() || generic(sp->sf)) return true;
        
        if (fr.isdefault || fr.lexframe < 0 || fr.lexframe >= i) return false;
        i = fr.lexframe;
    }
    return false;
}





inline bool TypeCheck::Converts(const Val &v, TypeExpr *t) {
    auto vt = LoadType(v.type);
    if (t->kind == TY_INT) {
        if (vt->kind != TY_INT) return false;
        if (t->ix)
            return vt->ix && vt->ix->arr == t->ix->arr && (t->ix->upto || !vt->ix->upto) &&
                   ImplicitInt(vt->intstorage, t->intstorage);
        if (vt->intstorage == t->intstorage) return true;
        if (v.ck == CK_INT || (v.litint && t->intstorage != IS_VARINT))
            return v.litint ? ConstsFit(v, t->intstorage)
                            : FitsIntStorage(v.ival, v.uns, t->intstorage);
        if (t->intstorage == IS_VARINT) return vt->intstorage != IS_U64;
        return ImplicitInt(vt->intstorage, t->intstorage);
    }
    if (t->kind != TY_FLT) return false;
    if (vt->kind == TY_INT) return true;
    if (vt->kind != TY_FLT) return false;
    return vt->fltstorage == t->fltstorage || (IsF32(t) ? LitFloat(v) : IsF32(vt));
}



inline bool TypeCheck::SameNumVal(const Val &a, const Val &b) {
    if (!a.type || !b.type || !SameNum(LoadType(a.type), LoadType(b.type))) return false;
    if (a.ck != b.ck || a.litfloat != b.litfloat || a.litint != b.litint ||
        a.flexint != b.flexint || a.unsized != b.unsized || a.notconst != b.notconst)
        return false;
    if (a.notconst && a.litlo != b.litlo) return false;
    if (a.ck == CK_INT && (a.ival != b.ival || a.uns != b.uns)) return false;
    if (a.ck == CK_FLT && a.fval != b.fval) return false;
    return !a.litint || (a.litlo == b.litlo && a.lithi == b.lithi);
}







inline bool TypeCheck::SameReach(const CastAlt &a, TypeExpr *at, bool typed) {
    auto &cv = a.operand;
    auto st = LoadType(cv.type), tt = a.cast->totype;
    if ((cv.litfloat && cv.ck == CK_NONE) || cv.litint) return SameNum(st, tt) && SameNum(tt, at);
    if (typed && IsBranchConstruct(a.cast->child) && !SameNum(st, at)) return false;
    auto rounds = IsIntT(st) ? tt->kind == TY_FLT : !SameNum(st, tt);
    return !rounds || SameNum(tt, at);
}




inline bool TypeCheck::Reaches(const CastAlt &a, TypeExpr *at, bool typed) {
    return a.pending ? !a.settle || SameNum(at, a.settle) : SameReach(a, at, typed);
}





inline void TypeCheck::NoteCast(AsCast *x, const Val &raw, const Val &cv, const Val &v) {
    auto removable = !InGenericCode() && Converts(cv, x->totype);
    if (auto it = castalts.find(x->child); it != castalts.end()) {
        auto a = it->second;
        
        
        
        auto same = a.pending ? !a.settle || !IsF32(a.settle) : SameReach(a, x->totype, false);
        CastVerdict(a, same ? CastReason(a, a.reach ? a.reach : x->totype, !a.pending) : "",
                    removable ? x->Origin() : nullptr);
    }
    if (!removable) return;
    CastAlt a;
    a.cast = x;
    a.operand = a.alt = cv;
    a.raw = raw;
    castalts[x] = a;
    if (!SameNumVal(cv, v)) casttyped.insert(x);
}






inline void TypeCheck::FollowCast(Node *n, const CastAlt &a, const Val &tv, const Val &v,
                                  TypeExpr *at) {
    if (!tv.type) return CastVerdict(a, "");
    auto next = a;
    next.alt = tv;
    next.pending = true;
    if (tv.litfloat && !v.litfloat && v.type->kind == TY_FLT) {
        
        
        auto &cv = a.operand;
        auto settle = a.pending                        ? a.settle
                      : cv.litfloat && cv.ck == CK_NONE ? LoadType(cv.type)
                                                        : a.cast->totype;
        if (!settle || !SameNum(settle, LoadType(v.type)) || !Reaches(a, settle, false))
            return CastVerdict(a, "");
        next.settle = settle;
        next.reach = a.reach ? a.reach : settle;
    } else {
        if (!Reaches(a, at, false)) return CastVerdict(a, "");
        if (SameNumVal(tv, v)) return CastVerdict(a, CastReason(a, a.reach ? a.reach : at));
        if (!SameNum(LoadType(tv.type), LoadType(v.type)) || tv.litfloat || v.litfloat ||
            tv.litint || v.litint || tv.flexint || v.flexint)
            return CastVerdict(a, "");
        next.settle = nullptr;
        next.reach = a.reach ? a.reach : at;
    }
    castalts[n] = next;
    casttyped.insert(n);
}








inline void TypeCheck::JudgeBinaryCasts(Binary *b, Node *l, Node *r, const Val &lv,
                                        const Val &rv, const Val &v, TypeExpr *ct) {
    auto cmp = v.type->kind == TY_BOOL;
    auto same = [&](const Val &tv, TypeExpr *tct) {
        return tv.type && (cmp ? SameNum(tct, ct) : SameNumVal(tv, v));
    };
    CastAlt alts[2];
    Val tvs[2];
    TypeExpr *tcts[2] = { nullptr, nullptr };
    bool has[2], alone[2] = { false, false };
    for (auto s = 0; s < 2; s++) {
        auto it = castalts.find(s ? r : l);
        has[s] = it != castalts.end();
        if (!has[s]) continue;
        alts[s] = it->second;
        tvs[s] = NumericBinary(b, s ? lv : alts[s].alt, s ? alts[s].alt : rv, tcts[s], true);
        alone[s] = same(tvs[s], tcts[s]) && Reaches(alts[s], tcts[s], false);
    }
    auto both = casttyped.count(l) && casttyped.count(r);
    auto together = false;
    if (both) {
        TypeExpr *tct = nullptr;
        auto tv = NumericBinary(b, alts[0].alt, alts[1].alt, tct, true);
        together = same(tv, tct) && Reaches(alts[0], tct, false) && Reaches(alts[1], tct, false);
    }
    for (auto s = 0; s < 2; s++) {
        if (!has[s]) continue;
        auto &a = alts[s];
        if (!cmp && !both) {
            FollowCast(b, a, tvs[s], v, tcts[s]);
            continue;
        }
        auto redundant = alone[s] && (!both || together || (s ? true : !alone[1]));
        CastVerdict(a, redundant ? CastReason(a, a.reach ? a.reach : tcts[s]) : "");
    }
}



inline void TypeCheck::JudgeUnaryCast(Unary *u, Node *c, const Val &r) {
    auto it = castalts.find(c);
    if (it == castalts.end()) return;
    auto a = it->second;
    auto tr = NumericUnary(u, a.alt, true);
    FollowCast(u, a, tr, r, tr.type ? LoadType(tr.type) : nullptr);
}








inline void TypeCheck::JudgeCastAt(Node *n, TypeExpr *dt, const Val &v) {
    auto it = castalts.find(n);
    if (it == castalts.end()) return;
    auto a = it->second;
    auto &tv = a.alt;
    auto same = false;
    if (builtinfallback.count(n)) {
        same = false;
    } else if (!dt) {
        dt = LoadType(v.type);
        same = SameNumVal(tv, v);
    } else {
        same = (dt->kind == TY_INT || dt->kind == TY_FLT) && Converts(tv, dt) &&
               Reaches(a, dt, true);
    }
    CastVerdict(a, same ? CastReason(a, a.reach ? a.reach : dt) : "");
}




inline string TypeCheck::CastReason(const CastAlt &a, TypeExpr *reach, bool bycast) {
    auto &cv = a.operand;
    auto st = LoadType(cv.type), tt = a.cast->totype;
    auto what = ExprStr(a.cast->child);
    auto an = [&](TypeExpr *t) {
        auto s = TypeStr(t);
        return cat(s[0] == 'i' || s[0] == 'f' ? "an " : "a ", s);
    };
    auto literal = cv.ck != CK_NONE || cv.litint || cv.litfloat;
    if (!literal && SameNum(st, tt)) return cat(what, " is already ", TypeStr(tt));
    if (bycast) return cat("the cast to ", TypeStr(reach), " around it converts ", what, " as well");
    if (!literal && SameNum(st, reach)) return cat(what, " is already ", TypeStr(reach), " here");
    if (cv.ck == CK_INT && IsIntT(reach))
        return cat(cv.constfrom ? cat(what, " is ", ConstStr(cv), ", which") : ConstStr(cv),
                   " fits ", TypeStr(reach), " here");
    if (cv.ck != CK_NONE || cv.litfloat) return cat(what, " is ", an(reach), " here");
    if (cv.litint)
        return cat("its constants ", cv.litlo, " to ", cv.lithi, " fit ", TypeStr(reach), " here");
    return cat(an(st), " converts to ", TypeStr(reach), " implicitly here");
}






inline void TypeCheck::CastVerdict(const CastAlt &a, const string &reason,
                                   const AsCast *dependson) {
    if (quiet) return;
    Warning w;
    w.cast = a.cast->Origin();
    w.redundant = !reason.empty();
    if (w.redundant) {
        w.diag.where = LocOf(a.cast);
        w.diag.message = cat("redundant `as", a.cast->unchecked ? "! " : " ",
                             TypeStr(a.cast->totype), "`: ", reason);
        w.dependson = dependson;
    }
    if (WarningsHeld()) cur.pendingwarnings.push_back(std::move(w));
    else castverdicts.push_back(std::move(w));
}




inline void TypeCheck::ReportRedundantCasts() {
    FlushWarnings();
    struct Tally {
        size_t first = SIZE_MAX;
        bool needed = false;
        vector<const AsCast *> deps;
    };
    unordered_map<const AsCast *, Tally> tally;
    for (size_t i = 0; i < castverdicts.size(); i++) {
        auto &w = castverdicts[i];
        auto &t = tally[w.cast];
        if (!w.redundant) {
            t.needed = true;
            continue;
        }
        if (w.dependson) t.deps.push_back(w.dependson);
        if (t.first == SIZE_MAX) t.first = i;
    }
    unordered_map<const AsCast *, bool> warns;
    function<bool(const AsCast *)> warn = [&](const AsCast *x) {
        if (auto it = warns.find(x); it != warns.end()) return it->second;
        auto it = tally.find(x);
        auto w = it != tally.end() && !it->second.needed && it->second.first != SIZE_MAX;
        if (w)
            for (auto d : it->second.deps) w = w && !warn(d);
        return warns[x] = w;
    };
    vector<size_t> out;
    for (auto &[x, t] : tally) if (warn(x)) out.push_back(t.first);
    auto key = [&](size_t i) {
        auto l = castverdicts[i].cast->line;
        return tuple(l.fileidx, l.line, i);
    };
    sort(out.begin(), out.end(), [&](size_t i, size_t j) { return key(i) < key(j); });
    for (auto i : out) ast.Warn(castverdicts[i].diag);
}










inline void TypeCheck::ConstName(Ident *id, const char *what) {
    if (!id->expansionerror.empty()) Error(id, string(id->expansionerror));
    if (auto vd = LookupVar(id->name, id->ns); vd && !vd->isglobal)
        Error(id, cat(what, " ", id->name, " names a local variable, not a constant"));
    if (auto kind = ScopeNameKind(id->name))
        Error(id, cat(what, " ", id->name, " names a ", kind, ", not a constant"));
    DefaultScopeName(id->name, id, false);
}

inline void TypeCheck::ConstNames(Node *n, const char *what) {
    ConstExprNames(n, what, [&](Ident *id, const char *w) { ConstName(id, w); });
}






inline void TypeCheck::ConstNamesIn(TypeExpr *t) {
    SizeNames(t, [&](Ident *id, const char *w) { ConstName(id, w); });
}





inline void TypeCheck::SignatureNames(const vector<Param> &params,
                                      const vector<TypeExpr *> &rets,
                                      const vector<GenericParam> &generics) {
    auto check = [&](TypeExpr *t) {
        SizeNames(t, [&](Ident *id, const char *what) {
            for (auto &p : params)
                if (p.name == id->name)
                    Error(id, cat(what, " ", id->name, " names a parameter, not a constant"));
        });
        TypeParamSizes(t, generics);
    };
    for (auto &p : params)
        if (p.type) check(p.type);
    for (auto t : rets) check(t);
}



inline void TypeCheck::TypeParamSizes(TypeExpr *t, const vector<GenericParam> &generics) {
    SizeNames(t, [&](Ident *id, const char *what) {
        for (auto &g : generics)
            if (g.name == id->name)
                Error(id, cat(what, " ", id->name, " names a type parameter, not a constant"));
    });
}

inline int64_t TypeCheck::FillCount(Node *n) {
    ConstNames(n, "array fill count");
    return ConstIntOrError(n, "array fill count");
}









inline bool TypeCheck::ConstIntValue(Node *n, Val &v, bool &literal,
                                     set<VarDecl *> &visiting, ConstUse *use) {
    if (auto i = Is<IntLit>(n)) {
        v.type = i->fixedtype ? i->fixedtype : ast.inttypes[i->uns ? IS_U64 : IS_I64];
        v.ck = CK_INT;
        v.ival = i->val;
        v.uns = i->uns;
        literal = !i->fixedtype;
        return true;
    }
    if (auto id = Is<Ident>(n)) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1) return false;
        if (!visiting.insert(g).second)
            Error(n, cat("cycle in constant initializer: ", id->name));
        if (use) {
            
            if (visiting.size() == 1) use->named = g->defs[0];
            RelyOnConstant(*use, g->defs[0]);
        }
        auto ok = ConstIntValue(g->inits[0], v, literal, visiting, use);
        visiting.erase(g);
        if (!ok) return false;
        if (g->type) {
            auto t = g->type;
            if (!IsIntT(t)) return false;
            if (v.notconst) {
                
                if (!FitsIntStorage(v.litlo, false, t->intstorage)) Error(n, ConstsNoFit(v, t));
                v.ival = WrapStorage(v.ival, t->intstorage);
                v.uns = t->intstorage == IS_U64 && v.ival < 0;
                v.notconst = false;
            }
            if (!FitsIntStorage(v.ival, v.uns, t->intstorage))
                Error(n, cat("constant ", ConstStr(v), " does not fit ", TypeStr(t)));
            v.type = ast.PlainOf(t);
            v.uns = t->intstorage == IS_U64 && v.ival < 0;
        }
        literal = false;
        return true;
    }
    if (auto u = Is<Unary>(n)) {
        if (u->op != T_MINUS && u->op != T_BITNOT) return false;
        if (!ConstIntValue(u->child, v, literal, visiting, use)) return false;
        
        
        auto adapts = literal;
        if (auto id = Is<Ident>(u->child)) {
            auto g = ast.LookupGlobal(id->name, id->ns);
            adapts = g && !g->type;
        }
        auto s = v.type->intstorage;
        if (u->op == T_MINUS) {
            if (v.uns && (!literal || v.ival != INT64_MIN))
                Error(n, "negated constant too large for i64");
            if (!literal && IsUnsigned(s))
                Error(n, cat("cannot negate a value of unsigned type ", TypeStr(v.type)));
            if (!v.uns && v.ival == INT64_MIN)
                Error(n, "signed overflow in constant expression");
            v.ival = (int64_t)(0u - (uint64_t)v.ival);
            if (literal) v.type = ast.inttypes[IS_I64];
            v.uns = false;
            v.notconst = false;
        } else if (adapts && s == IS_I64 && !v.notconst && v.ival >= 0) {
            
            
            v.notconst = true;
            v.litlo = v.lithi = v.ival;
            v.ival = ~v.ival;
        } else {
            v.ival = ~v.ival;
            auto bits = IntBits(s);
            if (IsUnsigned(s) && bits < 64)
                v.ival = (int64_t)((uint64_t)v.ival & ((1ull << bits) - 1));
            v.uns = s == IS_U64 && v.ival < 0;
            v.notconst = false;
        }
        if (!FitsIntStorage(v.ival, v.uns, v.type->intstorage))
            Error(n, "signed overflow in constant expression");
        return true;
    }
    if (auto b = Is<Binary>(n)) {
        switch (b->op) {
            case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD:
            case T_BITAND: case T_BITOR: case T_XOR: case T_SHL: case T_SHR: break;
            default: return false;
        }
        Val l, r;
        bool llit, rlit;
        if (!ConstIntValue(b->left, l, llit, visiting, use) ||
            !ConstIntValue(b->right, r, rlit, visiting, use)) return false;
        if (b->op == T_SHL || b->op == T_SHR) {
            v.type = l.type;
        } else {
            auto lf = l, rf = r;
            if (!llit) lf.ck = CK_NONE;
            if (!rlit) rf.ck = CK_NONE;
            v.type = UnifyNumeric(n, b->op, lf, rf, l.type, r.type);
        }
        if (b->op == T_DIV && !IsUnsigned(v.type->intstorage) &&
            l.ival == INT64_MIN && r.ival == -1)
            Error(n, "constant division overflow");
        FoldInt(b->op, l, r, v, n);
        if (v.ck != CK_INT) Error(n, "signed overflow in constant expression");
        literal = llit && rlit;
        return true;
    }
    if (auto d = Is<Dot>(n)) {
        
        
        auto id = Is<Ident>(d->obj);
        auto bd = LookupBuiltin(d->name);
        if (!id || !bd || (bd->kind != B_LEN && bd->kind != B_CAP)) return false;
        int64_t extent;
        if (!ConstExtent(id, bd->kind == B_CAP, extent, visiting, use)) return false;
        v.type = ast.inttypes[IS_I64];
        v.ck = CK_INT;
        v.ival = extent;
        v.uns = false;
        literal = false;
        return true;
    }
    return false;
}









inline bool TypeCheck::ConstExtent(Ident *id, bool cap, int64_t &extent,
                                   set<VarDecl *> &visiting, ConstUse *use) {
    auto g = ast.LookupGlobal(id->name, id->ns);
    if (!g || g->byref || g->reusable || g->names.size() != 1 || g->inits.size() > 1)
        return false;
    if (!visiting.insert(g).second)
        Error(id, cat("cycle in constant initializer: ", id->name));
    if (use && visiting.size() == 1) use->named = g->defs[0];
    auto ok = false;
    Val v;
    bool literal;
    if (auto t = g->type) {
        if (t->kind == TY_ARRAY && t->arr->sizeexpr &&
            t->arr->akind == (cap ? A_LIMITED : A_FIXED) &&
            ConstIntValue(t->arr->sizeexpr, v, literal, visiting, use)) {
            extent = v.ival;
            ok = true;
        }
    } else if (!g->inits.empty()) {
        auto init = g->inits[0];
        if (cap) {
            if (auto from = Is<Ident>(init)) ok = ConstExtent(from, cap, extent, visiting, use);
        } else if (auto al = Is<ArrayLit>(init); al && !al->capexpr) {
            if (!al->fillcount) {
                extent = (int64_t)al->elems.size();
                ok = true;
            } else if (ConstIntValue(al->fillcount, v, literal, visiting, use)) {
                extent = v.ival;
                ok = true;
            }
        } else if (auto s = Is<StrLit>(init)) {
            extent = (int64_t)s->val.size();
            ok = true;
        } else if (auto from = Is<Ident>(init)) {
            ok = ConstExtent(from, cap, extent, visiting, use);
        }
        if (ok && use) RelyOnExtent(*use, g->defs[0], cap);
    }
    visiting.erase(g);
    return ok;
}





inline bool TypeCheck::ElementwiseOK(TypeExpr *t) {
    int isint = -1;
    function<bool(TypeExpr *)> rec = [&](TypeExpr *t2) -> bool {
        switch (t2->kind) {
            case TY_INT:
                if (t2->intstorage == IS_VARINT || t2->ix) return false;
                if (isint == 0) return false;
                isint = 1;
                return true;
            case TY_FLT:
                if (isint == 1) return false;
                isint = 0;
                return true;
            case TY_STRUCT: {
                auto inst = GetStructInst(t2);
                for (size_t i = 0; i < inst->ftypes.size(); i++)
                    if (inst->ftypes[i] && !rec(inst->ftypes[i])) return false;
                return true;
            }
            case TY_ARRAY:
                return t2->arr->akind == A_FIXED && rec(t2->arr->sub);
            default:
                return false;
        }
    };
    return (t->kind == TY_STRUCT || t->kind == TY_ARRAY) && rec(t);
}


inline TypeExpr *TypeCheck::UnsignedLeaf(TypeExpr *t) {
    switch (t->kind) {
        case TY_INT: return IsUnsigned(t->intstorage) ? t : nullptr;
        case TY_STRUCT:
            for (auto ft : GetStructInst(t)->ftypes)
                if (auto leaf = ft ? UnsignedLeaf(ft) : nullptr) return leaf;
            return nullptr;
        case TY_ARRAY: return UnsignedLeaf(t->arr->sub);
        default: return nullptr;
    }
}




inline TypeExpr *TypeCheck::ElementwiseScalarType(TypeExpr *t) {
    if (!ElementwiseOK(t)) return nullptr;
    TypeExpr *leaf = nullptr;
    function<bool(TypeExpr *)> rec = [&](TypeExpr *tt) {
        if (tt->kind == TY_STRUCT) {
            for (auto ft : GetStructInst(tt)->ftypes)
                if (ft && !rec(ft)) return false;
            return true;
        }
        if (tt->kind == TY_ARRAY) return rec(tt->arr->sub);
        if (!leaf) leaf = LoadType(tt);
        return TopConstEq(leaf, tt);
    };
    return rec(t) ? leaf : nullptr;
}

inline Val TypeCheck::CheckVariantConst(Dot *d, SEnum *en, TypeExpr *written) {
    if (!written && !en->generics.empty())
        Error(d, cat("generic enum ", en->name, " needs type arguments to name a variant"));
    auto t = ast.EnumOf(en, written ? written->enu->args : vector<TypeExpr *> {}, false, d->line);
    auto inst = GetEnumInst(t);
    auto found = en->FindVariant(d->name);
    if (!found && en->broken) throw Abandoned {};
    if (!found) Error(d, cat("enum ", en->name, " has no variant named ", d->name));
    if (found->has_payload) {
        auto spelled = written ? TypeStr(t) : string(en->name);
        Error(d, cat("variant ", spelled, ".", d->name,
                     " has a payload; construct it with ", spelled, ".", d->name, " { ... }"));
    }
    d->variantconst = found;
    d->einst = inst;
    if (!inst->allfixed || inst->selfrel) t->enu->varmode = true;
    Val v;
    v.type = t;
    return v;
}








inline bool TypeCheck::InitOrderStoreSafe(Node *n, TypeExpr *dest) {
    if (!n || !dest || dest->kind == TY_VOID) return true;
    if (HasRelRefT(dest, true)) return Is<NullLit>(n) != nullptr;
    if (auto b = Is<Block>(n)) return InitOrderStoreSafe(b->tail, dest);
    if (auto i = Is<IfExpr>(n))
        return InitOrderStoreSafe(i->thenb, dest) && InitOrderStoreSafe(i->elseb, dest);
    if (auto m = Is<MatchExpr>(n)) {
        for (auto &arm : m->arms) if (!InitOrderStoreSafe(arm.body, dest)) return false;
        return true;
    }
    if (dest->kind != TY_ARRAY) return true;
    
    
    TypeExpr *from = nullptr;
    if (Is<StrLit>(n) || Is<ArrayLit>(n)) from = n->exprtype;
    else if (auto id = Is<Ident>(n); id && id->vdef) from = id->vdef->type;
    else if (auto c = Is<Call>(n)) {
        if (c->defaultinit) return InitOrderStoreSafe(c->defaultinit, dest);
        if (c->spec && c->spec->rets.size() == 1) from = c->spec->rets[0];
    } else if (auto u = Is<Unary>(n); u && u->op == T_BITAND)
        return InitOrderStoreSafe(u->child, dest);
    if (from) from = LoadType(from->kind == TY_REF ? from->ref->sub : from);
    return from && TypeEq(from, dest);
}





inline bool TypeCheck::InitOrderSafe(Node *n, set<FnSpec *> &visiting, FnSpec *callee) {
    if (!n) return true;
    auto safe = [&](Node *child) { return InitOrderSafe(child, visiting, callee); };
    if (Is<IntLit>(n) || Is<FltLit>(n) || Is<BoolLit>(n) || Is<StrLit>(n) ||
        Is<NullLit>(n) || Is<SelfRef>(n) || Is<Ident>(n)) return true;
    if (auto d = Is<Dot>(n)) return d->variantconst || safe(d->obj);
    if (auto u = Is<Unary>(n)) {
        
        
        
        if (u->op == T_MINUS) {
            auto t = LoadType(u->child->exprtype);
            if (!IsIntT(t) && t->kind != TY_FLT) return false;
            if (IsIntT(t) && !IsUnsigned(t->intstorage)) return false;
        }
        return safe(u->child);
    }
    if (auto b = Is<Binary>(n)) {
        auto t = LoadType(b->left->exprtype);
        if (b->op == T_ANDAND || b->op == T_OROR || b->op == T_EQ || b->op == T_NEQ ||
            b->op == T_DOTEQ || b->op == T_DOTNEQ) return safe(b->left) && safe(b->right);
        if (!IsIntT(t) && t->kind != TY_FLT && t->kind != TY_BOOL) return false;
        
        if (IsIntT(t) && (b->op == T_DIV || b->op == T_MOD ||
            (!IsUnsigned(t->intstorage) &&
             (b->op == T_PLUS || b->op == T_MINUS || b->op == T_MUL)))) return false;
        return safe(b->left) && safe(b->right);
    }
    if (auto c = Is<AsCast>(n)) {
        auto from = LoadType(c->child->exprtype), to = c->totype;
        
        
        if (to->ix) return false;
        bool lossless = IsIntT(from) && IsIntT(to) &&
            ((IsUnsigned(from->intstorage) == IsUnsigned(to->intstorage) &&
              IntBits(to->intstorage) >= IntBits(from->intstorage)) ||
             (IsUnsigned(from->intstorage) && !IsUnsigned(to->intstorage) &&
              IntBits(to->intstorage) > IntBits(from->intstorage)));
        return (c->unchecked || c->implicit || to->kind == TY_FLT || lossless) &&
               safe(c->child);
    }
    if (auto c = Is<Call>(n)) {
        if (c->defaultinit) return safe(c->defaultinit);
        auto sp = c->spec;
        if (!sp || !sp->body || sp->inprogress || !visiting.insert(sp).second) return false;
        auto args = c->ArgNodes();
        bool ok = args.size() == sp->argtypes.size() && sp->rets.size() <= 1;
        for (size_t i = 0; ok && i < args.size(); i++)
            ok = InitOrderStoreSafe(args[i], sp->argtypes[i]) && safe(args[i]);
        if (!sp->rets.empty()) ok = ok && InitOrderStoreSafe(sp->body, sp->rets[0]);
        ok = ok && InitOrderSafe(sp->body, visiting, sp);
        visiting.erase(sp);
        return ok;
    }
    if (auto r = Is<Return>(n)) {
        if (!callee || r->target != callee->sf) return false;
        if (r->vals.size() != callee->rets.size()) return false;
        for (size_t i = 0; i < r->vals.size(); i++)
            if (!InitOrderStoreSafe(r->vals[i], callee->rets[i]) || !safe(r->vals[i])) return false;
        return true;
    }
    if (auto d = Is<VarDecl>(n)) {
        
        
        if (!callee) return false;
        if (d->inits.empty()) return true;
        if (d->inits.size() != d->defs.size()) return false;
        for (size_t i = 0; i < d->inits.size(); i++)
            if (!InitOrderStoreSafe(d->inits[i], d->defs[i]->type) || !safe(d->inits[i]))
                return false;
        return true;
    }
    if (auto a = Is<ArrayLit>(n)) {
        
        if (a->capexpr || (a->fillcount && !Is<IntLit>(a->fillcount))) return false;
        auto elem = a->exprtype->kind == TY_ARRAY ? a->exprtype->arr->sub : a->exprtype->sub;
        if (!InitOrderStoreSafe(a->fillval, elem)) return false;
        for (auto e : a->elems) if (!InitOrderStoreSafe(e, elem)) return false;
    } else if (auto s = Is<StructLit>(n)) {
        const auto &types = s->sinst ? s->sinst->ftypes
            : s->einst->vftypes[s->einst->en->VariantIndex(s->variant)];
        for (size_t i = 0; i < s->inits.size(); i++)
            if (!InitOrderStoreSafe(s->inits[i].val, types[s->fieldindices[i]])) return false;
    } else if (!Is<Block>(n) && !Is<IfExpr>(n) && !Is<MatchExpr>(n)) {
        
        return false;
    }
    bool ok = true;
    n->Children([&](Node *child) { ok = ok && safe(child); });
    return ok;
}



inline TypeCheck::LitDeep TypeCheck::CheckInits(StructLit *sl, vector<Field> &fields,
                                               vector<TypeExpr *> &ftypes,
                                               string_view what, TypeExpr *selft) {
    LitDeep deep;
    auto named = !sl->inits.empty() && !sl->inits[0].name.empty();
    vector<bool> got(fields.size(), false);
    auto pos = 0;
    for (auto &fi : sl->inits) {
        auto idx = -1;
        CheckAccessorOwner(fi.val, fi.accessorowner, selft, sl);
        if (named) {
            for (auto i = 0; i < (int)fields.size(); i++)
                if (!fields[i].ispad && fields[i].name == fi.name) { idx = i; break; }
            if (idx < 0 && LacksByError(selft)) throw Abandoned {};
            if (idx < 0) Error(fi.val, cat(what, " has no field ", fi.name));
            if (got[idx]) Error(fi.val, cat("duplicate initializer for field ", fi.name));
        } else {
            while (pos < (int)fields.size() && fields[pos].ispad) pos++;
            if (pos >= (int)fields.size() && LacksByError(selft)) throw Abandoned {};
            if (pos >= (int)fields.size())
                Error(fi.val, cat("too many initializers for ", what));
            idx = pos++;
        }
        got[idx] = true;
        sl->fieldindices.push_back(idx);
    }
    if (sl->sourcefieldindices.empty()) sl->sourcefieldindices = sl->fieldindices;
    const auto &sourceorder = sl->sourcefieldindices;
    vector<FieldInit> ordered(fields.size());
    for (size_t i = 0; i < sl->inits.size(); i++) ordered[sl->fieldindices[i]] = sl->inits[i];
    sl->inits.clear();
    sl->fieldindices.clear();
    for (auto i = 0; i < (int)fields.size(); i++) {
        if (fields[i].ispad) continue;
        auto fi = ordered[i];
        fi.name = fields[i].name;
        if (!fi.val) {
            if (fields[i].defaultval) {
                fi.val = fields[i].defaultval->Clone(ast);
                fi.fromdefault = true;
            } else if (IsOptional(ftypes[i])) continue;
            else if (sl->defaultall) {
                string why;
                if (!HasDefault(ftypes[i], why))
                    Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                                  " (it has no declared default, and ", why, ")"));
                fi.val = DefaultValue(ftypes[i], sl->line);
            }
            
            else if (!ast.unknowntypes.empty() && MentionsUnknown(ftypes[i])) throw Abandoned {};
            else Error(sl, cat("missing initializer for field ", fields[i].name, " of ", what,
                               " (it has no default)"));
        }
        sl->inits.push_back(fi);
        sl->fieldindices.push_back(i);
    }
    
    
    
    for (size_t k = 0; k < sl->inits.size(); k++) {
        auto &fi = sl->inits[k];
        auto ft = ftypes[sl->fieldindices[k]];
        auto component = selft->kind == TY_STRUCT && selft->struc->st->isroot
                             ? RootComponent(rootowner, selft, sl->fieldindices[k]) : nullptr;
        DestScope ds(*this, component ? Dest(component) : curdst);
        if (Is<SelfRef>(fi.val)) CheckSelfInit(fi.val, ft, selft);
        else {
            SlotScope ss(*this, true);
            auto fv = fi.fromdefault
                          ? CheckDefaultInit(fi.val, ft, selft, sl, fields[sl->fieldindices[k]])
                          : CheckValue(fi.val, ft);
            NoteLitElem(deep, fi.val, fv, ft);
            if (component && HoldsPlainRef(ft)) NoteHolderBinding(component, fv, fi.val);
        }
    }
    
    
    if (named) {
        vector<bool> supplied(fields.size(), false);
        for (auto idx : sourceorder) supplied[idx] = true;
        vector<int> reorderable(fields.size(), -1);
        auto safe = [&](int idx) {
            if (reorderable[idx] < 0) {
                set<FnSpec *> visiting;
                auto init = sl->InitFor(idx);
                reorderable[idx] = InitOrderStoreSafe(init, ftypes[idx]) &&
                                   InitOrderSafe(init, visiting);
            }
            return reorderable[idx] != 0;
        };
        for (size_t a = 0; a < sourceorder.size(); a++)
            for (size_t b = a + 1; b < sourceorder.size(); b++) {
                int hi = sourceorder[a], lo = sourceorder[b];
                if (hi <= lo) continue;
                bool ok = safe(lo) && safe(hi);
                
                
                for (int i = lo + 1; i < hi; i++)
                    if (!supplied[i] && !fields[i].ispad) ok = ok && safe(i);
                if (ok) continue;
                string order;
                for (int i = 0; i < (int)fields.size(); i++)
                    if (supplied[i]) Append(order, order.empty() ? "" : ", ", fields[i].name);
                Error(sl->InitFor(lo), cat("field initializers must follow declaration order: ",
                      fields[lo].name, " comes before ", fields[hi].name,
                      "; reordering may cross a side effect, runtime check, or nonlocal exit",
                      "; supplied fields in declaration order: ", order,
                      "; fields are evaluated and constructed front-to-back"));
            }
    }
    return deep;
}





inline void TypeCheck::CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft) {
    if (ft->kind != TY_REF || ft->ref->lenstorage < 0)
        Error(n, cat("self initializes relative-reference fields (T&<u32> and friends), "
                     "not ", TypeStr(ft)));
    if (ft->ref->optional)
        Error(n, cat("self cannot initialize the optional relative reference ",
                     TypeStr(ft), ": offset 0 is its null (§3.9)"));
    if (!TypeEq(ft->ref->sub, selft))
        Error(n, cat("self here is a value of type ", TypeStr(selft), ", which does not "
                     "fit a field of type ", TypeStr(ft)));
    
    
    
    Roots poolroot;
    if (ft->ref->pool) poolroot.Set(ft->ref->pool, true);
    if (ft->ref->pool && !RelativeDestination(poolroot, curdst, n, true))
        Error(n, cat("self initializes ", TypeStr(ft), " only in a literal being built "
                     "inside ", ft->ref->pool->name, " (a push, an append, an alloc, or an "
                     "element store), since it stores the value's own offset in it (§3.9)"));
    if (ft->ref->pool && curdst.fill)
        Error(n, cat("self cannot initialize ", TypeStr(ft), " in a .resize fill value, which "
                     "is built once and copied into every slot the resize adds, while self "
                     "stores the value's own offset in ", ft->ref->pool->name, " (§3.9)"));
    
    
    
    if (ClassOf(selft) == SC_RESIZABLE)
        Error(n, cat("self cannot be stored relative: ", TypeStr(selft),
                     " is resizable, and a relative reference is an offset alone (§3.9)"));
    n->exprtype = ft;
}




inline void TypeCheck::CheckStmts(Block *b) {
    
    
    
    auto lost = [&](size_t i) { return (int)i == b->brokenat; };
    for (size_t i = 0; i < b->stmts.size(); i++) {
        if (!reachable) {
            b->stmts.resize(i);
            break;
        }
        if (lost(i)) throw Abandoned {};
        blockpos.back().idx = i;
        if (auto cf = Is<ConstFor>(b->stmts[i])) b->stmts[i] = ExpandStatements(cf);
        if (auto ci = Is<ConstIf>(b->stmts[i])) b->stmts[i] = ExpandSelection(ci);
        CheckStmt(b->stmts[i]);
    }
    if (lost(b->stmts.size())) throw Abandoned {};
    
    
    
    if (!reachable) b->tail = nullptr;
    blockpos.back().idx = b->stmts.size();
}




inline bool TypeCheck::MentionsName(Node *n, string_view name, set<SFunction *> &seen) {
    if (!n) return false;
    if (auto id = Is<Ident>(n); id && id->name == name) return true;
    if (auto fd = Is<FnDecl>(n)) {
        if (fd->sf->body && seen.insert(fd->sf).second &&
            MentionsName(fd->sf->body, name, seen))
            return true;
    }
    if (auto c = Is<Call>(n)) {
        string_view callee;
        if (auto id = Is<Ident>(c->callee)) callee = id->name;
        else if (auto d = Is<Dot>(c->callee)) callee = d->name;
        if (!callee.empty()) {
            for (auto &[si, sf] : localfns)
                if (sf->name == callee && sf->body && seen.insert(sf).second &&
                    MentionsName(sf->body, name, seen))
                    return true;
        }
    }
    auto hit = false;
    n->Children([&](Node *ch) { hit = hit || MentionsName(ch, name, seen); });
    return hit;
}




inline bool TypeCheck::EscapingContinue(Node *n) {
    if (!n || Is<FnDecl>(n) || Is<FunVal>(n)) return false;
    if (Is<Continue>(n)) return true;
    Node *body = nullptr;
    if (auto w = Is<While>(n)) body = w->body;
    else if (auto l = Is<LoopExpr>(n)) body = l->body;
    else if (auto fl = Is<ForLoop>(n)) body = fl->body;
    auto hit = false;
    n->Children([&](Node *ch) { hit = hit || (ch != body && EscapingContinue(ch)); });
    return hit;
}





inline bool TypeCheck::ExposedLater(VarDef *v) {
    auto vframe = FrameOfScope(Depth(v) - 1);
    
    auto fieldtype = [&](Node *n) -> TypeExpr * {
        vector<string_view> names;
        while (auto d = Is<Dot>(n)) {
            names.push_back(d->name);
            n = d->obj;
        }
        auto id = Is<Ident>(n);
        if (!id || id->name != v->name || names.empty() || !v->type) return nullptr;
        auto t = v->type;
        for (auto i = names.size(); i-- > 0;) {
            if (t->kind == TY_REF) t = t->ref->sub;
            if (t->kind != TY_STRUCT) return nullptr;
            auto inst = GetStructInst(t);
            TypeExpr *ft = nullptr;
            for (size_t k = 0; k < inst->st->fields.size() && k < inst->ftypes.size(); k++)
                if (inst->st->fields[k].name == names[i]) ft = inst->ftypes[k];
            if (!ft) return nullptr;
            t = ft;
        }
        return t;
    };
    function<bool(Node *)> exposes = [&](Node *n) -> bool {
        set<SFunction *> seen;
        if (!n || !MentionsName(n, v->name, seen)) return false;
        if (auto ft = fieldtype(n)) return HoldsPlainRef(ft);
        if (Is<Ident>(n)) return true;
        
        auto inchild = false, any = false;
        n->Children([&](Node *ch) {
            set<SFunction *> s;
            if (!MentionsName(ch, v->name, s)) return;
            inchild = true;
            any = any || exposes(ch);
        });
        return any || !inchild;
    };
    auto exposed = false;
    LaterOperands([&](Node *n, int fi) {
        exposed = exposed || (NamesFrame(fi, vframe) && exposes(n));
    });
    return exposed;
}
















inline bool TypeCheck::UsedAfter(VarDef *v) {
    
    
    if (v->componentowner && v->type) v = v->componentowner;
    set<SFunction *> seen;
    auto vframe = FrameOfScope(Depth(v) - 1);
    auto mentions = [&](Node *n, int fi) {
        return NamesFrame(fi, vframe) && MentionsName(n, v->name, seen);
    };
    auto later = false;
    LaterOperands([&](Node *n, int fi) { later = later || mentions(n, fi); });
    if (later) return true;
    
    
    auto cut = (int)scopes.size();
    auto exitat = SIZE_MAX;
    if (!blockpos.empty()) {
        auto &bp = blockpos.back();
        auto &stmts = bp.block->stmts;
        auto frameidx = (int)frames.size() - 1;
        if (bp.scopeidx == (int)scopes.size() - 1 && FrameOfScope(bp.scopeidx) == frameidx &&
            bp.idx < stmts.size() && !EscapingContinue(stmts[bp.idx])) {
            for (auto i = bp.idx + 1; i <= stmts.size(); i++) {
                auto s = i < stmts.size() ? stmts[i] : bp.block->tail;
                if (!s || EscapingContinue(s)) break;
                auto to = Is<Return>(s) ? frames[frameidx].scopebase
                          : Is<Break>(s) ? FindBreakScope(false)
                                         : -1;
                if (to < 0) continue;
                cut = to;
                exitat = i;
                break;
            }
        }
    }
    for (auto i = 0; i < (int)scopes.size() && i < cut; i++) {
        if (scopes[i].kind != SK_LOOP) continue;
        
        if (auto fl = Is<ForLoop>(scopes[i].node); fl && (fl->vdef == v || fl->idxdef == v))
            return true;
        if (Depth(v) <= i && scopes[i].node && mentions(scopes[i].node, FrameOfScope(i)))
            return true;
    }
    for (auto &bp : blockpos) {
        if (bp.scopeidx < Depth(v) - 1) continue;
        auto fi = FrameOfScope(bp.scopeidx);
        auto &fr = frames[fi];
        
        if (fr.isfunval && !fr.isdefault && bp.scopeidx == fr.scopebase &&
            Depth(v) - 1 < bp.scopeidx && mentions(bp.block, fi))
            return true;
        auto &stmts = bp.block->stmts;
        auto exits = &bp == &blockpos.back() && exitat != SIZE_MAX;
        if (bp.scopeidx >= cut && !exits) continue;
        auto end = exits ? min(exitat + 1, stmts.size()) : stmts.size();
        for (auto i = bp.idx + 1; i < end; i++)
            if (mentions(stmts[i], fi)) return true;
        if ((!exits || exitat == stmts.size()) && bp.idx < stmts.size() && bp.block->tail &&
            mentions(bp.block->tail, fi))
            return true;
    }
    return false;
}

inline void TypeCheck::CheckStmt(Node *n) {
    DestScope ds(*this, Dest {});
    ast.CheckCancel();
    
    
    FlagScope rs(inreturn, false);
    NodeScope ns(*this, n);
    if (auto vd = Is<VarDecl>(n)) { CheckVarDecl(vd, false); return; }
    if (auto a = Is<Assign>(n)) { CheckAssign(a); return; }
    if (auto x = Is<IncDec>(n)) { CheckIncDec(x); return; }
    if (auto fd = Is<FnDecl>(n)) { DeclareLocalFn(fd); return; }
    if (auto d = Is<Defer>(n)) { CheckDefer(d); return; }
    CheckStmtExpr(n);
}

}  
