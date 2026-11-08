




#pragma once

namespace faloya {

inline Val IntLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.inttypes[uns ? IS_U64 : IS_I64];
    if (fixedtype) { v.type = fixedtype; v.nonneg = uns || val >= 0; return v; }
    v.ck = CK_INT;
    v.ival = val;
    v.uns = uns;
    v.nonneg = uns || val >= 0;
    return v;
}

inline Val FltLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.flttypes[FS_F64];
    if (fixedtype) { v.type = fixedtype; return v; }
    v.ck = CK_FLT;
    v.fval = val;
    return v;
}

inline Val BoolLit::Check(TypeCheck &tc, TypeExpr *) {
    Val v;
    v.type = tc.ast.booltype;
    return v;
}

inline Val NullLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.isnull = true;
    v.writable = true;   
    
    v.type = expected && expected->kind == TY_REF && expected->ref->optional
                 ? expected : tc.nulltype;
    return v;
}



inline Val SelfRef::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "self is only valid as the initializer of a non-optional "
                   "relative-reference field of the literal containing it (§3.9)");
}



inline Val StrLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    v.strlit = true;
    v.Set(nullptr, true);   
    v.writable = false;
    if (expected) {
        
        if (expected->kind == TY_ARRAY && IsU8(expected->arr->sub)) {
            tc.CheckArrayCount(this, expected, (int64_t)val.size());
            if (expected->arr->akind == A_FIXED &&
                tc.ArraySize(expected->arr) != (int64_t)val.size())
                tc.Error(this, cat("string literal of length ", (int64_t)val.size(),
                                   " does not fit ", tc.TypeStr(expected)));
            if (expected->arr->akind == A_LIMITED && expected->arr->sizeexpr &&
                tc.ArraySize(expected->arr) < (int64_t)val.size())
                tc.Error(this, cat("string literal of length ", (int64_t)val.size(),
                                   " exceeds the capacity of ", tc.TypeStr(expected)));
            v.type = expected;
            return v;
        }
        if (expected->kind == TY_SLICE && IsU8(expected->sub)) {
            v.type = tc.cu8slice;
            return v;
        }
    }
    v.type = tc.cu8slice;
    return v;
}

inline Val Ident::Check(TypeCheck &tc, TypeExpr *) {
    if (!expansionerror.empty()) tc.Error(this, string(expansionerror));
    if (auto vd = tc.LookupVar(name, ns, this)) {
        vdef = vd;
        if (tc.deferbodies) tc.NoteDeferMention(vd);
        tc.RequireAssigned(vd, this);
        Val v;
        auto t = vd->narrowed ? vd->narrowed : vd->type;
        v.type = tc.LoadType(t);
        if (IsRefOrSlice(t)) {
            v.SetProv(tc.RefProvOf(vd));
            v.lvalue = t->kind == TY_SLICE;   
            if (v.lvalue) {
                v.slot.Set(vd, true);
                
                v.slot.writable = v.writable && !vd->copybind && !(vd->type && vd->type->cq);
                v.hasslot = true;
            }
        } else if (vd->unsized) {
            
            
            v.unsized = true;
            v.unsizedparam = vd->unsizedorigin ? vd->unsizedorigin : vd;
        } else {
            v.Set(vd, true);
            v.byteview = vd->contentbyteview;
            v.writable = !vd->copybind && !(vd->type && vd->type->cq);
            v.reusable = vd->reusable;
            v.nonneg = vd->nonneg;
            if (vd->nonneg) v.nonnegfrom = vd;
            if (vd->constlit && vd->type->kind == TY_FLT) {
                v.litfloat = true;
                if (vd->constflt) {
                    v.ck = CK_FLT;
                    v.fval = vd->constfval;
                }
                v.constfrom = vd;
            } else if (vd->constnot) {
                v.flexint = v.notconst = true;
                v.litlo = v.lithi = ~vd->constval;
                v.constfrom = vd;
            } else if (vd->constlit) {
                v.ck = CK_INT;
                v.ival = vd->constval;
                v.uns = vd->constuns;
                v.constfrom = vd;
            }
            v.lvalue = true;
            if (tc.HoldsPlainRef(v.type)) {
                
                
                
                
                
                v.holderset = true;
                v.holderfrom = vd;
                if (vd->isglobal) v.contents.Set(nullptr, false);
                else v.contents = vd->contents;
            }
        }
        return v;
    }
    
    
    const FnValBind *fb;
    if (auto t = tc.LookupTypeParam(name, fb))
        tc.Error(this, cat("type parameter ", name, " is bound to the type ", tc.TypeStr(t),
                           ", not a value"));
    if (fb) {
        Val v;
        v.type = tc.fntype;
        v.fnv = *fb;
        return v;
    }
    FnSpec *env = nullptr;
    if (auto fns = tc.LookupLocalFns(name, env); !fns.empty()) {
        fnref = fns[0];
        Val v;
        v.type = tc.fntype;
        v.fnv = tc.LocalFnValue(fns, env);
        return v;
    }
    if (auto &cands = tc.ast.LookupFunctions(name, ns); !cands.empty()) {
        fnref = cands[0];
        for (auto sf : cands)
            if (sf->brokensig) throw Abandoned {};
        Val v;
        v.type = tc.fntype;
        v.fnv.named = cands[0];
        if (cands.size() > 1) v.fnv.set = &cands;
        return v;
    }
    tc.Error(this, cat("unknown identifier: ", name));
}

inline Val ArrayLit::Check(TypeCheck &tc, TypeExpr *expected) {
    Val v;
    TypeCheck::LitDeep deep;
    if (capexpr) {
        
        tc.CheckIntAny(capexpr);
        if (!expected || expected->kind != TY_ARRAY ||
            expected->arr->akind != A_LIMITED || expected->arr->sizeexpr)
            tc.Error(this, "[..cap] constructs a limited array of construction-time "
                           "capacity; it needs a T[..] destination");
        v.type = expected;
        return v;
    }
    
    auto litelems = true;
    int64_t litlo = INT64_MAX, lithi = INT64_MIN;
    auto noteelem = [&](const Val &ev) {
        int64_t lo = 0, hi = 0;
        if (ev.litelems) lo = ev.litlo, hi = ev.lithi;
        else if (!TypeCheck::IntConsts(ev, lo, hi) && !TypeCheck::LitFloat(ev)) litelems = false;
        litlo = std::min(litlo, lo);
        lithi = std::max(lithi, hi);
    };
    auto setlitelems = [&]() {
        v.litelems = litelems;
        v.litlo = litlo;
        v.lithi = lithi;
    };
    TypeExpr *elem = nullptr;
    int64_t wantcount = -1;
    int64_t capacity = -1;   
    
    
    
    auto natural = [&](int64_t count) {
        if (elem == tc.fntype) tc.Error(this, "function values cannot be stored in arrays (§7.6)");
        tc.NoZeroSizeElement(elem, line);
        if (tc.ClassOf(elem) == SC_FIXED) return tc.ast.ArrayOf(elem, A_FIXED, line, count);
        auto t = tc.ast.ArrayOf(elem, A_VAR, line);
        tc.ValidateType(t, line, VT_LOCAL);
        return t;
    };
    auto atslice = expected && expected->kind == TY_SLICE;
    if (expected && expected->kind == TY_ARRAY) {
        elem = expected->arr->sub;
        if (expected->arr->akind == A_FIXED) wantcount = tc.ArraySize(expected->arr);
        if (expected->arr->akind == A_LIMITED && expected->arr->sizeexpr)
            capacity = tc.ArraySize(expected->arr);
    } else if (expected && expected->kind == TY_SLICE) {
        elem = expected->sub;
    }
    if (fillval) {
        auto cnt = tc.FillCount(fillcount);
        if (cnt < 0) tc.Error(this, "array fill count cannot be negative");
        tc.CheckArrayCount(this, expected, cnt);
        
        
        
        fillcount = tc.ast.New<IntLit>(fillcount->line, cnt);
        fillcount->exprtype = tc.ast.inttypes[IS_I64];
        TypeCheck::SlotScope ss(tc, true);
        auto ev = tc.CheckValue(fillval, elem);
        if (!elem) {
            tc.NoUntypedEmptyArray(ev, fillval, "the array literal around it");
            elem = ev.type;
        }
        if (cnt) tc.NoteLitElem(deep, fillval, ev, elem);
        noteelem(ev);
        if (wantcount >= 0 && cnt != wantcount)
            tc.Error(this, cat("fill count ", cnt, " does not match array size ", wantcount));
        if (capacity >= 0 && cnt > capacity)
            tc.Error(this, cat("fill count ", cnt, " exceeds the capacity of ",
                               tc.TypeStr(expected)));
        v.type = expected && expected->kind == TY_ARRAY ? expected : natural(cnt);
        if (atslice) tc.NoTemporaryLiteral(this, v.type);
        v.Set(tc.TempRoot(), false);
        tc.HolderFromLit(v, deep);
        setlitelems();
        return v;
    }
    if (elems.empty() && !elem) {
        
        
        v.emptyarr = true;
        v.type = tc.ast.ArrayOf(tc.ast.voidtype, A_FIXED, line, 0);
        return v;
    }
    tc.CheckArrayCount(this, expected, (int64_t)elems.size());
    for (auto &e : elems) {
        TypeCheck::SlotScope ss(tc, true);   
        auto ev = tc.CheckValue(e, elem);
        if (!elem) {
            tc.NoUntypedEmptyArray(ev, e, "the array literal around it");
            elem = ev.type;
        }
        tc.NoteLitElem(deep, e, ev, elem);
        noteelem(ev);
    }
    if (elem->kind == TY_VOID) tc.Error(this, "cannot infer array element type");
    if (wantcount >= 0 && (int64_t)elems.size() != wantcount)
        tc.Error(this, cat((int64_t)elems.size(), " element(s) do not fill ",
                           tc.TypeStr(expected)));
    if (capacity >= 0 && (int64_t)elems.size() > capacity)
        tc.Error(this, cat((int64_t)elems.size(), " element(s) exceed the capacity of ",
                           tc.TypeStr(expected)));
    v.type = expected && expected->kind == TY_ARRAY ? expected
                                                    : natural((int64_t)elems.size());
    if (atslice) {
        
        tc.NoTemporaryLiteral(this, v.type);
    }
    
    
    
    v.Set(tc.TempRoot(), false);
    tc.HolderFromLit(v, deep);
    setlitelems();
    return v;
}

inline Val StructLit::Check(TypeCheck &tc, TypeExpr *expected) {
    tc.ExpandInitializers(inits);
    auto expandednamed = !inits.empty() && !inits[0].name.empty();
    for (auto &fi : inits)
        if (expandednamed != !fi.name.empty())
            tc.Error(fi.val, "struct literal mixes named and positional initializers");
    fieldindices.clear();
    sinst = nullptr;
    einst = nullptr;
    variant = nullptr;
    if (!implicit) tc.ConstNamesIn(type);
    auto t = tc.Subst(type);
    if (t->kind == TY_VARIANT) {
        if (t->var->adt->kind != TY_ENUM)
            tc.Error(this, cat("variant literal of non-ADT type ", tc.TypeStr(t->var->adt)));
        auto ei = tc.GetEnumInst(t->var->adt);
        auto var = t->var->variant;
        auto vi = ei->en->VariantIndex(var);
        einst = ei;
        variant = var;
        Val v;
        
        
        
        
        v.type = expected && expected->kind == TY_ENUM && tc.SameAdt(expected, t->var->adt)
                     ? expected : t;
        auto deep = tc.CheckInits(this, var->fields, ei->vftypes[vi], ei->en->name, v.type);
        v.Set(tc.TempRoot(), false);   
        tc.HolderFromLit(v, deep);
        return v;
    }
    if (t->kind == TY_STRUCT) {
        auto st = t->struc->st;
        if (t->struc->args.empty() && !st->generics.empty()) {
            
            
            
            
            
            
            
            vector<FieldInit *> written;
            if (sourcefieldindices.empty()) {
                for (auto &fi : inits) written.push_back(&fi);
            } else {
                for (auto idx : sourcefieldindices)
                    for (auto &fi : inits)
                        if (fi.name == st->fields[idx].name) written.push_back(&fi);
            }
            vector<pair<string_view, TypeExpr *>> b;
            auto named = !written.empty() && !written[0]->name.empty();
            auto pos = 0;
            for (auto fi : written) {
                Field *field = nullptr;
                if (named) {
                    for (auto &f2 : st->fields)
                        if (!f2.ispad && f2.name == fi->name) { field = &f2; break; }
                } else {
                    while (pos < (int)st->fields.size() && st->fields[pos].ispad) pos++;
                    if (pos < (int)st->fields.size()) field = &st->fields[pos++];
                }
                if (!field) continue;  
                
                
                if (Is<SelfRef>(fi->val)) continue;
                auto av = tc.CheckV(fi->val, nullptr);
                fi->val->exprtype = av.type;
                
                tc.JudgeCastAt(fi->val, nullptr, av);
                auto nt = tc.NaturalType(av);
                
                
                
                if (nt && !tc.BindTypes(field->type, nt, b) && field->type->kind == TY_SLICE) {
                    auto at = IsPlainRef(nt) ? nt->ref->sub : nt;
                    if (at->kind == TY_ARRAY) tc.BindTypes(field->type->sub, at->arr->sub, b);
                }
            }
            vector<TypeExpr *> args;
            for (auto &g : st->generics) {
                TypeExpr *bound = nullptr;
                for (auto &[n, bt] : b) if (n == g.name) bound = bt;
                if (!bound)
                    tc.Error(this, cat("cannot infer generic parameter ", g.name, " of ",
                                       st->name, "; use ", st->name, "<...> { }"));
                args.push_back(bound);
            }
            t = tc.ast.StructOf(st, std::move(args), line);
        }
        auto inst = tc.GetStructInst(t);
        sinst = inst;
        if (st->isroot) {
            if (tc.rootinit != this || !tc.rootowner)
                tc.Error(this, "a root owner must be constructed directly at its declaration");
            tc.rootowner->type = t;
            for (size_t i = 0; i < inst->ftypes.size(); i++)
                if (inst->ftypes[i]) tc.RootComponent(tc.rootowner, t, (int)i);
        }
        auto deep = tc.CheckInits(this, st->fields, inst->ftypes, st->name, t);
        Val v;
        v.type = t;
        v.Set(tc.TempRoot(), false);
        tc.HolderFromLit(v, deep);
        return v;
    }
    tc.Error(this, cat("cannot construct a value of type ", tc.TypeStr(t), " with a literal"));
}

inline Val Unary::Check(TypeCheck &tc, TypeExpr *) {
    litfloat = false;
    flexint = false;
    tc.ForgetCastAlt(this);
    if (op == T_BITAND) return tc.CheckRefOf(this);
    auto v = tc.Operand(child);
    if (op == T_NOT) {
        
        auto t = tc.LoadType(v.type);
        if (t->kind != TY_BOOL && !IsOptional(t))
            tc.Error(this, cat("! requires bool, got ", tc.TypeStr(t)));
        Val r;
        r.type = tc.ast.booltype;
        return r;
    }
    if (auto t = tc.LoadType(v.type); IsOpaque(t)) {
        Val r;
        r.type = t;
        return r;
    }
    auto r = tc.NumericUnary(this, v, false);
    tc.JudgeUnaryCast(this, child, r);
    return r;
}



inline Val Binary::Check(TypeCheck &tc, TypeExpr *) {
    litfloat = false;
    flexint = false;
    tc.ForgetCastAlt(this);
    if (op == T_DOTEQ || op == T_DOTNEQ) return tc.CheckRefIdentity(this);
    if (op == T_ANDAND || op == T_OROR) return tc.CheckLogical(this);
    auto lv = tc.Operand(left);
    auto rv = tc.Operand(right);
    return tc.CheckBinaryResult(this, lv, rv);
}

inline Val Dot::Check(TypeCheck &tc, TypeExpr *) {
    TypeCheck::DestScope ds(tc, TypeCheck::Dest {});
    if (incomplete) tc.IncompleteDot(this);
    
    
    if (enumtype) {
        tc.ConstNamesIn(enumtype);
        auto t = tc.Subst(enumtype);
        if (t->kind != TY_ENUM)
            tc.Error(this, cat(tc.TypeStr(t), " is not an enum: it has no variant ", name));
        return tc.CheckVariantConst(this, t->enu->en, t);
    }
    if (auto id = Is<Ident>(obj); id && id->expansionerror.empty()) {
        const FnValBind *fb;
        if (!tc.LookupVar(id->name, id->ns) && !tc.LookupTypeParam(id->name, fb) && !fb)
            if (auto en = tc.ast.LookupEnum(id->name, id->ns))
                return tc.CheckVariantConst(this, en);
    }
    auto ov = tc.CheckV(obj, nullptr);
    if (tc.IsUntypedEmptyArray(ov.type)) tc.NoUntypedEmptyArray(ov, obj, cat(".", name));
    obj->exprtype = ov.type;
    auto t = ov.type;
    if (t->kind == TY_REF) {
        if (t->ref->optional)
            tc.Error(this, "optional value must be narrowed (if/guard/assert) before use");
        t = t->ref->sub;  
        
        ov.ClearReads();
        ov.reached = tc.LoadType(t);
    }
    tc.CheckAccessorOwner(this, accessorowner, t);
    
    if (auto bd = LookupBuiltin(name); bd && (bd->flags & BF_PROPERTY)) {
        if (RecvKindOf(t) & bd->recv) {
            member = bd->kind;
            Val v;
            assert(bd->rets[0] == 'i');
            v.type = tc.ast.inttypes[IS_I64];
            v.nonneg = true;   
            return v;
        }
        if (t->kind == TY_ARRAY || t->kind == TY_SLICE)
            tc.Error(this, cat(".", name, " is not available on ", tc.TypeStr(t)));
    }
    TypeCheck::LVal lv;
    lv.type = t;
    lv.SetProv(ov);
    lv.intemp = tc.TempContents(ov, lv.contents);
    tc.ResolveMemberLValue(lv, this);
    if (lv.intemp) tc.NoRelRefTempRead(obj, t, lv.type);
    return tc.ContainerRead(lv);
}

inline Val Call::Check(TypeCheck &tc, TypeExpr *expected) { return tc.CheckCall(this, expected); }

inline Val Index::Check(TypeCheck &tc, TypeExpr *) {
    TypeCheck::DestScope ds(tc, TypeCheck::Dest {});
    return tc.ContainerRead(tc.CheckLValue(this));
}

inline Val SliceExpr::Check(TypeCheck &tc, TypeExpr *) {
    TypeCheck::DestScope ds(tc, TypeCheck::Dest {});
    auto lv = tc.LValueBase(obj, cmpview);
    tc.DerefLValue(lv, obj);
    tc.SliceProvenance(lv, obj);
    TypeExpr *elem;
    if (lv.type->kind == TY_SLICE) {
        elem = lv.type->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "slices of variable-size elements can only be re-sliced "
                           "whole ([..])");
    } else if (lv.type->kind == TY_ARRAY) {
        elem = lv.type->arr->sub;
        if (tc.ClassOf(elem) != SC_FIXED && (lo || hi))
            tc.Error(this, "arrays of variable-size elements can only be sliced "
                           "whole ([..])");
    } else {
        tc.Error(this, cat("cannot slice a value of type ", tc.TypeStr(lv.type)));
    }
    
    if (lv.intemp && !cmpview) tc.NoRelRefTempRead(obj, lv.type, elem);
    if (lo) tc.CheckIntAny(lo);
    if (hi) tc.CheckIntAny(hi);
    Val v;
    v.type = tc.ast.SliceOf(elem, line);
    v.SetProv(lv);
    v.reusable = false;   
    if (lv.type->cq) v.writable = false;   
    v.type->cq = !v.writable;   
    return v;
}

inline Val AsCast::Check(TypeCheck &tc, TypeExpr *) {
    TypeCheck::DestScope ds(tc, TypeCheck::Dest {});
    
    
    
    if (implicit) return tc.Operand(child);
    tc.ForgetCastAlt(this);
    
    
    auto raw = tc.CheckV(child, nullptr);
    auto cv = tc.DecayRef(raw);
    child->exprtype = cv.type;
    auto st = tc.LoadType(cv.type);
    
    
    Val ov;
    if (IsOpaque(st)) {
        totype = ov.type = tc.Subst(type);
        return ov;
    }
    if (!IsIntT(st) && st->kind != TY_FLT)
        tc.Error(this, cat("as requires a numeric source, got ", tc.TypeStr(st)));
    auto tt = tc.Subst(type);
    if (IsOpaque(tt)) {
        totype = ov.type = tt;
        return ov;
    }
    if (tt->kind == TY_INT && tt->intstorage == IS_VARINT)
        tc.Error(this, "cannot cast to varint (varints are written at construction only)");
    if (tt->kind != TY_INT && tt->kind != TY_FLT)
        tc.Error(this, cat("as can only convert between numeric types, not to ", tc.TypeStr(tt)));
    Val v;
    totype = v.type = tt;
    
    
    
    if (tt->ix) {
        if (unchecked)
            tc.Error(this, cat("as! cannot make a ", tc.TypeStr(tt), ": only the checked `as` "
                               "converts into an index type (§3.11)"));
        if (!IsIntT(st))
            tc.Error(this, cat("only an integer converts to ", tc.TypeStr(tt), ", not ",
                               tc.TypeStr(st), " (§3.11)"));
        tc.RequireIndexArray(tt, this, cat("converting to ", tc.TypeStr(tt)));
        return v;
    }
    tc.NoteCast(this, raw, cv, v);
    return v;
}

inline Val RangeExpr::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "range expressions are only valid in for headers");
}

inline Val ConstFor::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "const for is only valid in statement, constructor initializer or match arm lists");
}

inline Val ConstIf::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "const if is only valid in statement, constructor initializer or match arm lists");
}

inline Val ConstQuery::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, cat(query == Q_SAME ? "same" : "has_default",
                       " is only valid in a const if condition"));
}

inline Val Block::Check(TypeCheck &tc, TypeExpr *expected) {
    
    auto onjoin = tc.joinpath == this;
    auto v = tc.CheckBlockVal(this, expected, true);
    return tc.VoidIfBottom(onjoin ? v : tc.TempCopy(v));
}

inline Val IfExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.VoidIfBottom(
        tc.CheckJoin(this, expected, [&](TypeExpr *e) { return tc.CheckIf(this, e, true); }));
}

inline Val MatchExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.VoidIfBottom(
        tc.CheckJoin(this, expected, [&](TypeExpr *e) { return tc.CheckMatch(this, e, true); }));
}

inline Val EarlyBlock::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.VoidIfBottom(tc.CheckJoin(
        this, expected, [&](TypeExpr *e) { return tc.CheckEarlyBlock(this, e, true); }));
}

inline Val While::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckWhile(this);
    return tc.VoidVal();
}

inline Val LoopExpr::Check(TypeCheck &tc, TypeExpr *expected) {
    return tc.VoidIfBottom(
        tc.CheckJoin(this, expected, [&](TypeExpr *e) { return tc.CheckLoop(this, e, true); }));
}

inline Val ForLoop::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckFor(this);
    return tc.VoidVal();
}

inline Val Return::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckReturn(this);
    return tc.VoidVal();
}

inline Val Break::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckBreak(this);
    return tc.VoidVal();
}

inline Val Continue::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckContinue(this);
    return tc.VoidVal();
}

inline Val Defer::Check(TypeCheck &tc, TypeExpr *) {
    tc.CheckDefer(this);
    return tc.VoidVal();
}

inline Val FunVal::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "function values can only be passed to calls (§7.6)");
}

inline Val VarDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val Assign::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an assignment is a statement, not a value");
}

inline Val IncDec::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "an increment is a statement, not a value");
}

inline Val FnDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val StructDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val EnumDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val AliasDecl::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "a declaration is a statement, not a value");
}

inline Val InlineBlock::Check(TypeCheck &tc, TypeExpr *) {
    tc.Error(this, "internal: optimizer node reached the typechecker");
}

}  
