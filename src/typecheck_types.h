




#pragma once

namespace faloya {






inline TypeExpr *TypeCheck::LookupBindingOuter(string_view name) {
    if (extrabindings)
        for (auto &[n, t] : *extrabindings) if (n == name) return t;
    if (ownexclude)
        for (auto &g : *ownexclude) if (g.name == name) return nullptr;
    if (bindonly || frames.empty()) return nullptr;
    return LookupBinding(name);
}

inline void TypeCheck::BindGenerics(vector<GenericParam> &generics, vector<TypeExpr *> &args,
                                    string_view what, string_view name, Line l,
                                    vector<pair<string_view, TypeExpr *>> &out) {
    if (args.size() != generics.size())
        Error(l, cat(what, " ", name, " takes ", (int64_t)generics.size(),
                     " type argument(s), ", (int64_t)args.size(), " given"));
    for (size_t i = 0; i < generics.size(); i++)
        out.push_back({ generics[i].name, args[i] });
}






inline void TypeCheck::LimitNestedInsts(TypeExpr *t) {
    auto decl = [](TypeExpr *x) -> const void * {
        return x->kind == TY_STRUCT ? (const void *)x->struc->st : x->enu->en;
    };
    vector<TypeExpr *> nested;
    for (auto c : typechain) if (decl(c) == decl(t)) nested.push_back(c);
    if ((int)nested.size() < MAXNESTEDSPECS) return;
    string s = "instantiating ";
    DumpShort(s, t);
    Append(s, " would chain more than ", MAXNESTEDSPECS, " instantiations of ",
           t->kind == TY_STRUCT ? t->struc->st->qname : t->enu->en->qname,
           ", each needed by the one before (");
    for (auto j = 0; j < 3; j++) {
        DumpShort(s, nested[j]);
        s += ", ";
    }
    s += "...): a generic type must reach a finite set of instantiations (§3.2)";
    Error(t->line, s);
}




template<typename Inst> Inst *Reused(Inst *inst) {
    if (inst->failed) throw Abandoned {};
    return inst;
}

inline StructInst *TypeCheck::GetStructInst(TypeExpr *t) {
    auto st = t->struc->st;
    if (t->struc->inst) return Reused(t->struc->inst);
    for (auto inst : st->insts)
        if (TypeArgsEq(inst->args, t->struc->args)) return Reused(t->struc->inst = inst);
    LimitNestedInsts(t);
    auto inst = ast.NewStructInst();
    inst->st = st;
    if (st->isroot) {
        inst->sclass = SC_ROOT;
        
        inst->flat = false;
    }
    inst->args = t->struc->args;
    inst->run = { &st->fields, &inst->ftypes };
    st->insts.push_back(inst);
    t->struc->inst = inst;
    vector<pair<string_view, TypeExpr *>> bindings;
    BindGenerics(st->generics, inst->args, "struct", st->name, t->line, bindings);
    WithBindings(bindings, [&]() {
        for (auto &f : st->fields)
            inst->ftypes.push_back(f.ispad ? nullptr : Subst(f.type));
    });
    
    
    
    auto lastreal = LastRealField(st->fields);
    typechain.push_back(t);
    BeginTypeBuild();
    for (auto i = 0; i < (int)st->fields.size(); i++) {
        if (st->isroot && st->fields[i].ispad)
            Error(st->line, "root structs have no packed byte layout; padding belongs inside a component");
        if (st->fields[i].ispad) continue;
        auto ft = inst->ftypes[i];
        ValidateType(ft, st->line, VT_FIELD);
        if (auto zs = ZeroSizeKind(ft))
            Error(st->line, cat("field ", st->fields[i].name, " of struct ", st->name,
                                " cannot be ", zs, ": ", TypeStr(ft), " (§3.4)"));
        auto c = ClassOf(ft);
        if (c == SC_RESIZABLE && !st->isroot) {
            
            
            if (i + 1 < (int)st->fields.size())
                Error(st->line, cat("resizable field ", st->fields[i].name, " of struct ",
                                    st->name, " must be the final field",
                                    i == lastreal ? ", with no pad after it" : "",
                                    "; use root struct for independently stored fields"));
            inst->sclass = SC_RESIZABLE;
        } else if (c == SC_VARIABLE && inst->sclass == SC_FIXED) {
            inst->sclass = SC_VARIABLE;
        }
        inst->flat = inst->flat && IsFlat(ft);
    }
    if (inst->sclass == SC_RESIZABLE) {
        auto fo = true;
        for (auto i = 0; i < (int)st->fields.size(); i++) {
            if (st->fields[i].ispad) continue;
            auto ft = inst->ftypes[i];
            if (i == lastreal)
                fo &= ft->kind == TY_ARRAY ||
                      (ft->kind == TY_STRUCT && GetStructInst(ft)->frameobj);
            else
                fo &= ClassOf(ft) == SC_FIXED && !HasRelRefT(ft);
        }
        inst->frameobj = fo;
    }
    inst->validated = true;
    typechain.pop_back();
    EndTypeBuild();
    return inst;
}

inline EnumInst *TypeCheck::GetEnumInst(TypeExpr *t) {
    auto en = t->enu->en;
    if (t->enu->inst) return Reused(t->enu->inst);
    for (auto inst : en->insts)
        if (TypeArgsEq(inst->args, t->enu->args)) return Reused(t->enu->inst = inst);
    LimitNestedInsts(t);
    auto inst = ast.NewEnumInst();
    inst->en = en;
    inst->args = t->enu->args;
    en->insts.push_back(inst);
    t->enu->inst = inst;
    vector<pair<string_view, TypeExpr *>> bindings;
    BindGenerics(en->generics, inst->args, "enum", en->name, t->line, bindings);
    WithBindings(bindings, [&]() {
        for (auto &v : en->variants) {
            inst->vftypes.emplace_back();
            for (auto &f : v.fields)
                inst->vftypes.back().push_back(f.ispad ? nullptr : Subst(f.type));
        }
    });
    for (size_t vi = 0; vi < en->variants.size(); vi++)
        inst->runs.push_back({ &en->variants[vi].fields, &inst->vftypes[vi] });
    inst->vbuilt.assign(en->variants.size(), 0);
    typechain.push_back(t);
    BeginTypeBuild();
    for (size_t vi = 0; vi < en->variants.size(); vi++) BuildVariant(inst, vi);
    inst->validated = true;
    typechain.pop_back();
    EndTypeBuild();
    
    
    
    
    
    if (inst->selfrel && inst->varclass == SC_RESIZABLE) {
        auto resizable = [&](size_t vi) {
            return AnyFieldOf(RunsOf(inst, (int)vi),
                              [&](TypeExpr *ft) { return ClassOf(ft) == SC_RESIZABLE; });
        };
        size_t tail = 0;
        while (!resizable(tail)) tail++;
        for (size_t vi = 0; vi < en->variants.size(); vi++)
            if (!resizable(vi) &&
                AnyFieldOf(RunsOf(inst, (int)vi), [&](TypeExpr *ft) { return HasRelRefT(ft); }))
                Error(en->line, cat("enum ", TypeStr(ast.EnumOf(en, inst->args, false, en->line)),
                                    " is resizable (its variant ", en->variants[tail].name,
                                    "'s payload is), so a whole assignment may replace its "
                                    "variant (§4.4) and its payloads bind only by value (§8.1); "
                                    "the payload of its variant ", en->variants[vi].name,
                                    " holds self-relative references, which a copy does not "
                                    "keep (§3.9): make them plain or `in pool` references"));
    }
    return inst;
}





inline void TypeCheck::BuildVariant(EnumInst *inst, size_t vi) {
    if (inst->vbuilt[vi]) return;
    inst->vbuilt[vi] = 1;
    auto en = inst->en;
    auto &v = en->variants[vi];
    auto lastreal = LastRealField(v.fields);
    for (auto i = 0; i < (int)v.fields.size(); i++) {
        if (v.fields[i].ispad) continue;
        auto ft = inst->vftypes[vi][i];
        ValidateType(ft, en->line, VT_FIELD);
        if (auto zs = ZeroSizeKind(ft))
            Error(en->line, cat("field ", v.fields[i].name, " of variant ", en->name, ".", v.name,
                                " cannot be ", zs, ": ", TypeStr(ft), " (§3.4)"));
        auto c = ClassOf(ft);
        if (c == SC_RESIZABLE) {
            if (i + 1 < (int)v.fields.size())
                Error(en->line, cat("resizable field ", v.fields[i].name, " of variant ",
                                    en->name, ".", v.name, " must be the final field",
                                    i == lastreal ? ", with no pad after it" : ""));
            inst->varclass = SC_RESIZABLE;
        }
        if (c != SC_FIXED) inst->allfixed = false;
        if (HasRelRefT(ft)) inst->selfrel = true;
        inst->flat = inst->flat && IsFlat(ft);
    }
    inst->vbuilt[vi] = 2;
}




inline void TypeCheck::EndTypeBuild() {
    if (typesbuilding == 1) {
        for (size_t i = 0; i < laterpointees.size(); i++) {
            auto [t, l, chain] = laterpointees[i];
            swap(typechain, chain);
            ValidatePointee(t, l);
            swap(typechain, chain);
        }
        laterpointees.clear();
    }
    typesbuilding--;
}

inline TypeCheck::DefaultScope::DefaultScope(TypeCheck &t, FnSpec *env, Line callline) : tc(t) {
    Frame f;
    f.spec = tc.CurRealFrame().spec;
    f.lexspec = env;
    f.isfunval = true;  
    f.isdefault = true;
    f.scopebase = (int)tc.scopes.size();
    f.varbase = (int)tc.vars.size();
    f.callline = callline;
    f.bodyidx = (int)tc.outerbodies.size();
    tc.frames.push_back(f);
}




inline Val TypeCheck::CheckDefaultInit(Node *&n, TypeExpr *ft, TypeExpr *owner,
                                       const StructLit *sl, const Field &field) {
    EachDefaultInPlace([&](Frame &fr) {
        if (fr.defaultfield == &field)
            Error(sl, cat("the default of field ", field.name, " of ",
                          LitTypeStr(fr.defaultlit, fr.defaultowner),
                          " leads to this construction of ", LitTypeStr(sl, owner),
                          ", which takes it again (§3.2)"));
    });
    auto t = owner->kind == TY_VARIANT ? owner->var->adt : owner;
    auto env = ast.NewFunValEnv();
    if (t->kind == TY_STRUCT)
        BindGenerics(t->struc->st->generics, t->struc->args, "struct", t->struc->st->name,
                     n->line, env->bindings);
    else
        BindGenerics(t->enu->en->generics, t->enu->args, "enum", t->enu->en->name,
                     n->line, env->bindings);
    DefaultScope ds(*this, env, Line {});
    auto &fr = frames.back();
    fr.defaultfield = &field;
    fr.defaultlit = sl;
    fr.defaultowner = owner;
    return CheckValue(n, ft);
}

inline Call *TypeCheck::DefaultCall(TypeExpr *t, Line line) {
    auto c = ast.New<Call>(line, ast.New<Ident>(line, "::default"));
    c->tyargs.push_back(t);
    c->implicit = true;
    return c;
}






inline Node *TypeCheck::DefaultValue(TypeExpr *t, Line line) {
    if (t->kind == TY_REF) return ast.New<NullLit>(line);
    if (ClassOf(t) == SC_FIXED) return DefaultCall(t, line);
    switch (t->kind) {
        case TY_ARRAY: return ast.New<ArrayLit>(line);
        case TY_INT:   return ast.New<IntLit>(line, 0);
        default: {
            auto lt = t->kind == TY_ENUM
                          ? ast.VariantTypeOf(t, &t->enu->en->variants[0], line) : t;
            auto sl = ast.New<StructLit>(line, lt);
            sl->defaultall = true;
            sl->implicit = true;
            return sl;
        }
    }
}

inline span<const FieldRun> TypeCheck::FieldRuns(TypeExpr *t) {
    switch (t->kind) {
        case TY_STRUCT: return RunsOf(GetStructInst(t));
        case TY_ENUM: return RunsOf(GetEnumInst(t));
        case TY_VARIANT: {
            auto inst = GetEnumInst(t->var->adt);
            return RunsOf(inst, inst->en->VariantIndex(t->var->variant));
        }
        default: return {};
    }
}

inline SizeClass TypeCheck::ClassOf(TypeExpr *t) {
    switch (t->kind) {
        case TY_INT:  return t->intstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
        case TY_REF:
            
            
            return t->ref->lenstorage == IS_VARINT ? SC_VARIABLE : SC_FIXED;
        case TY_FLT: case TY_BOOL: case TY_SLICE: return SC_FIXED;
        case TY_STRUCT: {
            auto inst = GetStructInst(t);
            
            
            if (!inst->validated)
                Error(t->line, cat("struct ", inst->st->name, " contains itself by value"));
            return inst->sclass;
        }
        case TY_ENUM: {
            if (!t->enu->varmode) return SC_FIXED;
            auto inst = GetEnumInst(t);
            if (!inst->validated)
                Error(t->line, cat("enum ", inst->en->name, " contains itself by value"));
            return inst->varclass;
        }
        case TY_ARRAY:
            switch (t->arr->akind) {
                case A_FIXED:   return SC_FIXED;
                case A_VAR:     return SC_VARIABLE;
                case A_LIMITED: return ArraySize(t->arr) >= 0 ? SC_FIXED : SC_VARIABLE;
                default:        return SC_RESIZABLE;
            }
        case TY_VARIANT: {
            auto c = SC_FIXED;
            EachField(t, [&](TypeExpr *ft) { c = std::max(c, ClassOf(ft)); });
            return c;
        }
        default: return SC_FIXED;
    }
}


inline bool TypeCheck::IsFlat(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: case TY_SLICE: return false;
        case TY_STRUCT: return GetStructInst(t)->flat;
        case TY_ENUM:   return GetEnumInst(t)->flat;
        case TY_ARRAY:  return IsFlat(t->arr->sub);
        case TY_VARIANT: return !AnyField(t, [&](TypeExpr *ft) { return !IsFlat(ft); });
        default: return true;
    }
}





inline bool TypeCheck::Reaches(TypeExpr *t, const function<bool(TypeExpr *)> &pred) {
    set<pair<void *, void *>> seen;
    function<bool(TypeExpr *)> walk = [&](TypeExpr *t) -> bool {
        if (pred(t)) return true;
        switch (t->kind) {
            case TY_REF: return walk(t->ref->sub);
            case TY_SLICE: return walk(t->sub);
            case TY_ARRAY: return walk(t->arr->sub);
            case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
                auto key = t->kind == TY_STRUCT ? pair<void *, void *>(GetStructInst(t), nullptr)
                           : t->kind == TY_ENUM ? pair<void *, void *>(GetEnumInst(t), nullptr)
                           : pair<void *, void *>(GetEnumInst(t->var->adt), t->var->variant);
                if (!seen.insert(key).second) return false;
                return AnyField(t, walk);
            }
            default: return false;
        }
    };
    return walk(t);
}




inline bool TypeCheck::HoldsIndexT(TypeExpr *t) {
    return Reaches(t, [](TypeExpr *t) { return t->kind == TY_INT && t->ix; });
}






inline bool TypeCheck::HoldsPlainRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage < 0;
        case TY_SLICE: return true;
        case TY_ARRAY: return HoldsPlainRef(t->arr->sub);
        default: return AnyField(t, [&](TypeExpr *ft) { return HoldsPlainRef(ft); });
    }
}






inline bool TypeCheck::ImageSafe(TypeExpr *t, string &why) {
    if (ClassOf(t) == SC_ROOT) {
        why = "a root owner has no contiguous byte image; serialize its fields separately";
        return false;
    }
    switch (t->kind) {
        case TY_SLICE:
            why = cat(TypeStr(t), " is a slice, which is an address");
            return false;
        case TY_REF:
            if (t->ref->lenstorage < 0) {
                why = cat(TypeStr(t), " is a plain reference, which is an address");
                return false;
            }
            if (t->ref->pool) {
                why = cat(TypeStr(t), " is measured from ", t->ref->pool->name,
                          ", not from the image");
                return false;
            }
            return true;
        case TY_ARRAY: return ImageSafe(t->arr->sub, why);
        case TY_FN:
            why = "function values are addresses";
            return false;
        default: return !AnyField(t, [&](TypeExpr *ft) { return !ImageSafe(ft, why); });
    }
}






inline bool TypeCheck::VerifiableElem(TypeExpr *t, TypeExpr *elem, string &why) {
    switch (t->kind) {
        case TY_REF: {
            if (!ImageSafe(t, why)) return false;
            auto p = t->ref->sub;
            if (TypeEq(p, elem)) return true;
            auto adt = p->kind == TY_VARIANT ? AdtOf(p) : nullptr;
            if (adt && elem->kind == TY_ENUM && SameAdt(adt, elem)) return true;
            why = cat(TypeStr(t), " points at ", TypeStr(p), ", not at an element of the "
                      "array or one of its variants");
            return false;
        }
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            return !AnyField(t, [&](TypeExpr *ft) { return !VerifiableElem(ft, elem, why); });
        case TY_ARRAY: return VerifiableElem(t->arr->sub, elem, why);
        default: return ImageSafe(t, why);
    }
}




inline bool TypeCheck::HoldsRelRef(TypeExpr *t) {
    switch (t->kind) {
        case TY_REF: return t->ref->lenstorage >= 0;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            return AnyField(t, [&](TypeExpr *ft) { return HoldsRelRef(ft); });
        case TY_ARRAY: return HoldsRelRef(t->arr->sub);
        default: return false;
    }
}






inline bool TypeCheck::HasDefault(TypeExpr *t, string &why) {
    switch (t->kind) {
        case TY_INT:
            
            if (t->ix) {
                why = cat(TypeStr(t), " has no value every state of ", t->ix->arr->name,
                          " allows");
                return false;
            }
            return true;
        case TY_FLT: case TY_BOOL: case TY_SLICE: return true;
        case TY_REF:
            if (t->ref->optional) return true;
            why = cat(TypeStr(t), " is a non-optional reference");
            return false;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
            auto runs = FieldRuns(t);
            if (t->kind == TY_ENUM) {
                if (runs.empty()) {
                    why = cat(TypeStr(t), " has no variants");
                    return false;
                }
                runs = runs.first(1);   
            }
            for (auto &run : runs) {
                for (size_t i = 0; i < run.fields->size(); i++) {
                    auto &f = (*run.fields)[i];
                    if (f.ispad || f.defaultval) continue;
                    if (!HasDefault((*run.ftypes)[i], why)) {
                        why = cat("field ", f.name, " has no declared default and ", why);
                        return false;
                    }
                }
            }
            return true;
        }
        case TY_ARRAY:
            if (t->arr->akind != A_FIXED) return true;   
            return HasDefault(t->arr->sub, why);
        default:
            why = cat(TypeStr(t), " has no default value");
            return false;
    }
}










inline TypeExpr *TypeCheck::PendingArray(Line l) {
    return ast.ArrayOf(ast.NewType(TY_VOID, l), A_GROW, l);
}


inline bool TypeCheck::IsPendingArray(TypeExpr *t) {
    return t && t->kind == TY_ARRAY && t->arr->akind == A_GROW && t->arr->sub->kind == TY_VOID;
}



inline bool TypeCheck::IsUntypedEmptyArray(const TypeExpr *t) {
    return t && t->kind == TY_ARRAY && t->arr->akind == A_FIXED && t->arr->sub->kind == TY_VOID;
}




inline void TypeCheck::NoUntypedEmptyArray(const Val &v, Node *at, string_view use) {
    if (IsUntypedEmptyArray(v.type))
        Error(at, cat("cannot infer the element type of []: ", use, " gives it none (§4.2)"));
}




inline TypeExpr *TypeCheck::PendingElemFrom(const Val &av, Node *at) {
    if (av.emptyarr || av.isnull || av.type->kind == TY_VOID || av.type == fntype)
        Error(at, "cannot infer the element type of this array from this value");
    if (av.strlit) return ast.ArrayOf(ast.inttypes[IS_U8], A_VAR, at->line);
    return av.type;
}



inline TypeExpr *TypeCheck::PendingElemFromSeq(const Val &av, Node *at) {
    auto t = av.type;
    TypeExpr *elem = nullptr;
    if (t->kind == TY_ARRAY) elem = t->arr->sub;
    else if (t->kind == TY_SLICE) elem = t->sub;
    if (av.strlit) elem = ast.inttypes[IS_U8];
    if (!elem || av.emptyarr)
        Error(at, "cannot infer the element type of this array from this value");
    return elem;
}

inline void TypeCheck::CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l) {
    arrt->arr->sub = elem;
    ValidateType(arrt, l, VT_LOCAL);
    auto pool = pendingpools.find(arrt);
    if (pool != pendingpools.end() && ClassOf(elem) != SC_FIXED)
        Error(l, cat(pool->second->reusable == RU_SLICES ? "reusable[]" : "reusable",
                     " applies to grow-only arrays of fixed-size elements (§5.4), and this "
                     "makes ", pool->second->name, " a ", TypeStr(arrt)));
}

inline void TypeCheck::RequireComplete(TypeExpr *t, Line l) {
    if (IsPendingArray(t))
        Error(l, "the element type of this array is not known yet (it was declared "
                 "with `= []`): push or append into it first, or annotate the "
                 "declaration");
}

inline void TypeCheck::ValidateType(TypeExpr *t, Line l, int pos) {
    switch (t->kind) {
        case TY_GENERIC:
            if (t->named->opaque) return;
            Error(l, cat("unknown type: ", t->named->name));
        case TY_UNRESOLVED:
            assert(false);
            return;
        case TY_INT: {
            
            
            auto compound = pos == VT_FIELD || pos == VT_ELEM || pos == VT_POINTEE;
            if (t->intstorage == IS_VARINT && !compound)
                Error(l, "varint is a storage type: only fields and array elements");
            return;
        }
        case TY_FLT: return;
        case TY_BOOL: return;
        case TY_VOID:
            Error(l, "expression has no value here");
        case TY_FN:
            Error(l, "function value types are compile-time only and cannot be stored");
        case TY_STRUCT:
            
            
            
            if (t->struc->st->isroot && pos != VT_LOCAL && pos != VT_GLOBAL &&
                pos != VT_POINTEE)
                Error(l, cat("root struct ", TypeStr(t),
                             " can only be owned by a local or global variable; ",
                             pos == VT_PARAM ? "pass it by reference instead"
                             : pos == VT_RET ? "return a reference instead"
                             : "nested root owners are not supported"));
            GetStructInst(t);
            return;
        case TY_ENUM: {
            auto inst = GetEnumInst(t);
            if (!inst->validated && !t->enu->varmode)
                Error(l, cat("enum ", t->enu->en->name, " contains itself by value"));
            if (inst->validated && !t->enu->varmode && !inst->allfixed)
                Error(l, cat("enum ", t->enu->en->name, " has non-fixed-size payloads and "
                             "can only be used in variable mode (",
                             t->enu->en->name, "..)"));
            if (inst->validated && !t->enu->varmode && inst->selfrel)
                Error(l, cat("enum ", t->enu->en->name, " has payloads holding self-relative "
                             "references, which a copy does not keep (§3.9), and a fixed-mode ",
                             t->enu->en->name, " binds its payloads only by value (§3.5): use "
                             "it in variable mode (", t->enu->en->name, "..), whose payloads "
                             "bind by reference, or make the references `in pool`"));
            return;
        }
        case TY_VARIANT: {
            if (t->var->adt->kind != TY_ENUM)
                Error(l, cat("variant type of non-ADT type ", TypeStr(t->var->adt)));
            auto inst = GetEnumInst(t->var->adt);
            if (!inst->validated) {
                auto vi = (size_t)inst->en->VariantIndex(t->var->variant);
                if (inst->vbuilt[vi] == 1)
                    Error(l, cat("variant ", inst->en->name, ".", t->var->name,
                                 " contains itself by value"));
                BuildVariant(inst, vi);
            }
            break;
        }
        case TY_ARRAY: {
            RequireComplete(t, l);
            ValidateType(t->arr->sub, l, VT_ELEM);
            auto ec = ClassOf(t->arr->sub);
            switch (t->arr->akind) {
                case A_FIXED:
                    ArraySize(t->arr);
                    if (ec != SC_FIXED)
                        Error(l, cat("fixed array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_LIMITED:
                    if (t->arr->sizeexpr) ArraySize(t->arr);
                    if (ec != SC_FIXED)
                        Error(l, cat("limited array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_GROWSHRINK:
                    if (ec != SC_FIXED)
                        Error(l, cat("grow-shrink array elements must be fixed-size: ",
                                     TypeStr(t->arr->sub)));
                    break;
                case A_VAR: case A_GROW:
                    if (ec == SC_RESIZABLE)
                        Error(l, cat("array elements may not be resizable: ",
                                     TypeStr(t->arr->sub)));
                    break;
            }
            break;
        }
        case TY_SLICE: case TY_REF:
            if (typesbuilding) laterpointees.push_back({ t, l, typechain });
            else ValidatePointee(t, l);
            return;
    }
    if (pos == VT_ELEM) NoZeroSizeElement(t, l);
}

inline void TypeCheck::ValidatePointee(TypeExpr *t, Line l) {
    if (t->kind == TY_SLICE) {
        ValidateType(t->sub, l, VT_ELEM);
        return;
    }
    
    
    
    if (t->ref->pool && t->ref->pool->type && !HasGenerics(t->ref->sub)) ValidatePool(t);
    ValidateType(t->ref->sub, l, VT_POINTEE);
    if (t->ref->lenstorage >= 0 && ClassOf(t->ref->sub) == SC_ROOT)
        Error(l, "root struct references must be plain references, not relative references");
}







inline TypeExpr *TypeCheck::GrowU8Array(Line l) {
    return ast.ArrayOf(ast.inttypes[IS_U8], A_GROW, l);
}




inline TypeExpr *TypeCheck::LoadType(TypeExpr *t) {
    if (t->kind == TY_INT && t->intstorage == IS_VARINT) return ast.inttypes[IS_I64];
    
    
    if (t->cq && !IsRefOrSlice(t)) return ast.PlainOf(t);
    if (IsRelRef(t)) {
        auto r = ast.RefTo(t->ref->sub, t->line, t->ref->optional);
        r->cq = t->cq;
        return r;
    }
    return t;
}




inline bool TypeCheck::LoadedTypeEq(TypeExpr *t, TypeExpr *of) {
    if (t->kind == TY_INT && t->intstorage == IS_VARINT)
        return TypeEq(ast.inttypes[IS_I64], of);
    TypeExpr value = *t;
    if (t->cq && !IsRefOrSlice(t)) {
        value.cq = false;
        return TypeEq(&value, of);
    }
    if (IsRelRef(t)) {
        TypeRef ref;
        ref.sub = t->ref->sub;
        ref.optional = t->ref->optional;
        value.ref = &ref;
        return TypeEq(&value, of);
    }
    return TypeEq(t, of);
}




inline TypeExpr *TypeCheck::ValueType(TypeExpr *t) {
    return IsRelRef(t) ? LoadType(t) : t;
}







inline TypeExpr *TypeCheck::DeclType(TypeExpr *t, TypeExpr *s) {
    return IsRelRef(t) ? s : ValueType(s);
}




inline bool TypeCheck::ImplicitInt(IntStorage from, IntStorage to) {
    if (from == IS_VARINT || to == IS_VARINT) return false;
    if (from == to) return true;
    if (IntBits(from) >= IntBits(to)) return false;
    return IsUnsigned(to) ? IsUnsigned(from) : true;
}




inline Val TypeCheck::CheckIntAny(Node *n) {
    auto v = Operand(n);
    if (!IsIntT(v.type))
        Error(n, cat("an integer is expected here, got ", TypeStr(v.type)));
    return v;
}






inline bool TypeCheck::HasRelRefT(TypeExpr *t, bool inpool) {
    return HasRelRefOf(t, inpool, [&](TypeExpr *x) { return FieldRuns(x); });
}




inline bool TypeCheck::BuiltWhereReceived(Node *n, size_t result) {
    if (!n || (n->exprtype && n->exprtype->kind == TY_VOID)) return true;
    if (auto b = Is<Block>(n)) return BuiltWhereReceived(b->tail, result);
    if (auto x = Is<IfExpr>(n))
        return BuiltWhereReceived(x->thenb, result) && BuiltWhereReceived(x->elseb, result);
    if (auto m = Is<MatchExpr>(n))
        return all_of(m->arms.begin(), m->arms.end(), [&](const MatchArm &a) {
            return BuiltWhereReceived(a.body, result);
        });
    auto breaks = [&](const vector<Break *> &bs) {
        return all_of(bs.begin(), bs.end(), [&](Break *b) {
            return BuiltWhereReceived(b->val, result);
        });
    };
    if (auto l = Is<LoopExpr>(n)) return breaks(l->breaks);
    if (auto b = Is<EarlyBlock>(n))
        return breaks(b->breaks) && BuiltWhereReceived(b->body->tail, result);
    if (Is<StructLit>(n) || Is<ArrayLit>(n)) return true;   
    if (auto d = Is<Dot>(n); d && d->variantconst) return true;   
    
    
    
    if (auto c = Is<Call>(n); c && c->builtin == B_DEFAULT) return true;
    
    
    if (auto c = Is<Call>(n); c && c->fvbody && result == 0)
        return BuiltWhereReceived(c->fvbody->tail, 0);
    
    
    
    
    
    if (auto c = Is<Call>(n); c && c->builtin < 0 && result < c->rettypes.size() &&
                              !IsRefOrSlice(c->rettypes[result])) {
        auto user = [](FnSpec *s) { return s && s->sf && !s->sf->isextern; };
        auto &ds = c->dispatch;
        return ds.empty() ? user(c->spec) : all_of(ds.begin(), ds.end(), user);
    }
    return false;
}







inline void TypeCheck::NoRelRefCopy(Node *n, TypeExpr *t, size_t result, const VarDecl *bind,
                                    const Val *src) {
    if (!reachable || !t) return;
    if (IsRefOrSlice(t) || !HasRelRefT(t)) return;
    if (BuiltWhereReceived(n, result)) return;
    if (auto id = Is<Ident>(n); id && n == retvalnode && AllowNamedResult(id, t)) return;
    auto msg = cat("copying a value of type ", TypeStr(t), ", which contains self-relative "
                   "references, is not supported; construct it in place");
    
    
    auto c = Is<Call>(n);
    auto refcall = c && result < c->rettypes.size() && IsPlainRef(c->rettypes[result]);
    auto path = Is<Ident>(n) || Is<Dot>(n) || Is<Index>(n);
    if (bind && src && ((path && (src->lvalue || src->pointee)) || refcall)) {
        string names;
        for (size_t i = 0; i < bind->names.size(); i++)
            if (bind->inits.size() == 1 || bind->inits[i] == n)
                names += cat(names.empty() ? "" : ", ", bind->names[i]);
        msg += cat(", or bind a reference to it with .=: ", bind->isvar ? "var " : "let ", names,
                   " .= ", ExprStr(n));
    }
    Error(n, msg);
}






inline void TypeCheck::NoRelRefTempRead(Node *base, TypeExpr *t, TypeExpr *member) {
    if (!reachable || Is<Ident>(base) || Is<Dot>(base) || Is<Index>(base)) return;
    if (!HasRelRefT(member) || BuiltWhereReceived(base, 0)) return;
    Error(base, cat("copying a value of type ", TypeStr(t), ", which contains self-relative "
                    "references, is not supported: its links, read out of this copy, would be "
                    "measured from the copy; read them where the value is stored"));
}







inline bool TypeCheck::AllowNamedResult(Ident *id, TypeExpr *t) {
    auto &fr = frames.back();
    auto spec = fr.spec;
    auto vd = id->vdef;
    
    if (!spec || !fr.sf || fr.isfunval || fr.isdefault || !spec->body || !vd || !vd->type ||
        (spec->retsknown && spec->rets.size() != 1) || ClassOf(t) == SC_FIXED)
        return false;
    auto top = false;
    for (auto st : spec->body->stmts)
        if (auto d = Is<VarDecl>(st); d && d->defs.size() == 1 && d->defs[0] == vd) top = true;
    if (!top) return false;
    auto ct = vd->type;
    auto elems = IsArrayKind(ct, A_GROW) || IsArrayKind(ct, A_GROWSHRINK);
    if (!TypeEq(ct, t) && !(elems && IsArrayKind(t, A_VAR) && TypeEq(ct->arr->sub, t->arr->sub)))
        return false;
    if (!NamedResultKeepsLinks(ct)) return false;
    namedresultuses.push_back({ spec, id, vd });
    return true;
}





inline VarDef *TypeCheck::NamedResultOf(FnSpec *spec) {
    auto body = spec->body;
    set<VarDef *> top;
    for (auto st : body->stmts)
        if (auto d = Is<VarDecl>(st); d && d->defs.size() == 1) top.insert(d->defs[0]);
    VarDef *cand = nullptr;
    auto ok = true;
    auto consider = [&](Node *v) {
        auto id = Is<Ident>(v);
        if (!id || !id->vdef || !top.count(id->vdef) || (cand && cand != id->vdef)) {
            ok = false;
            return;
        }
        cand = id->vdef;
    };
    function<void(Node *)> walk = [&](Node *n) {
        if (!n || !ok) return;
        if (auto r = Is<Return>(n); r && (r->target == spec->sf || !r->target)) {
            if (r->vals.size() != 1) ok = false;
            else consider(r->vals[0]);
        }
        RunChildren(n, walk);
    };
    walk(body);
    if (body->tail && body->tail->exprtype && body->tail->exprtype->kind != TY_VOID)
        consider(body->tail);
    return ok ? cand : nullptr;
}



inline void TypeCheck::CheckNamedResultUses(FnSpec *spec) {
    VarDef *named = nullptr;
    auto found = false;
    for (size_t i = 0; i < namedresultuses.size();) {
        auto u = namedresultuses[i];
        if (u.spec != spec) {
            i++;
            continue;
        }
        namedresultuses.erase(namedresultuses.begin() + (ptrdiff_t)i);
        if (!found) {
            named = NamedResultOf(spec);
            found = true;
        }
        if (named != u.var)
            Error(u.at, cat("copying a value of type ", TypeStr(u.var->type), ", which contains "
                            "self-relative references, is not supported: ", u.var->name,
                            " is built where the result goes only where every return of ",
                            spec->sf->name, " gives it (§7.3); construct it in place"));
        spec->relnamedresult = true;
    }
}



inline void TypeCheck::SelfRelPointees(TypeExpr *t, vector<TypeExpr *> &out) {
    switch (t->kind) {
        case TY_REF:
            if (IsSelfRelRef(t)) out.push_back(LoadType(t->ref->sub));
            return;
        case TY_ARRAY: SelfRelPointees(t->arr->sub, out); return;
        default: EachField(t, [&](TypeExpr *ft) { SelfRelPointees(ft, out); });
    }
}





inline bool TypeCheck::NamedResultKeepsLinks(TypeExpr *t) {
    vector<TypeExpr *> head;
    auto tail = t;
    while (tail->kind == TY_STRUCT && ClassOf(tail) == SC_RESIZABLE) {
        vector<TypeExpr *> fts;
        EachField(tail, [&](TypeExpr *ft) { fts.push_back(ft); });
        if (fts.empty()) break;
        head.insert(head.end(), fts.begin(), fts.end() - 1);
        tail = fts.back();
    }
    if (tail->kind != TY_ARRAY && ClassOf(tail) == SC_RESIZABLE) return false;
    vector<TypeExpr *> pointees;
    SelfRelPointees(tail, pointees);
    for (auto h : head) {
        if (HasRelRefT(h)) return false;
        for (auto p : pointees)
            if (CanContain(h, p)) return false;
    }
    return true;
}








inline VarDef *TypeCheck::NamedArrayComponent(VarDef *global, string_view name, Line line) {
    auto dot = name.find('.');
    if (dot == string_view::npos) return global;
    auto field = name.substr(dot + 1);
    if (field.find('.') != string_view::npos)
        Error(line, cat("in ", name, ": a named array component must be a direct field of a "
                        "global root owner"));
    auto &c = namedcomponents[{ global, field }];
    if (!c) {
        c = ast.NewVarDef();
        c->name = ast.Intern(cat(global->name, ".", field));
        c->line = line;
        c->isglobal = true;
        c->isvar = global->isvar;
        c->componentowner = global;
    }
    return c;
}

inline void TypeCheck::ValidateNamedComponents() {
    for (auto &[key, c] : namedcomponents) {
        auto [owner, field] = key;
        auto t = owner->type;
        if (!t || t->kind != TY_STRUCT || !t->struc->st->isroot)
            Error(c->line, cat("in ", c->name, ": a named array component must be a direct "
                               "field of a global root owner"));
        if (c->componentfield < 0)
            Error(c->line, cat("in ", c->name, ": root struct ", t->struc->st->name,
                               " has no field ", field));
        if (t->cq || t->struc->st->fields[c->componentfield].isconst || c->type->cq)
            Error(c->line, cat("in ", c->name, ": a named array component must be mutable"));
    }
}

inline void TypeCheck::ResolvePools() {
    for (auto t : ast.alltypes) {
        if (t->kind != TY_REF || t->ref->poolname.empty()) continue;
        auto name = t->ref->poolname;
        auto g = ast.LookupGlobal(name.substr(0, name.find('.')), t->ref->poolns);
        if (!g || g->defs.empty())
            Error(t->line, cat("in ", t->ref->poolname,
                               ": a relative reference's pool must be a global variable; a "
                               "local or parameter pool has no name at this declaration, so "
                               "use the self-relative form ", TypeStr(t->ref->sub), "&<",
                               IntStorageName(t->ref->lenstorage), "> instead (§3.9)"));
        t->ref->pool = NamedArrayComponent(g->defs[0], name, t->line);
        poolglobals.insert(t->ref->pool);
        if (t->ref->pool->componentowner) poolowners.insert(t->ref->pool->componentowner);
    }
}




inline void TypeCheck::ResolveIndexTypes() {
    for (auto t : ast.alltypes) {
        if (t->kind != TY_INT || !t->ix || t->ix->arr) continue;
        auto name = t->ix->name;
        auto g = ast.LookupGlobal(name.substr(0, name.find('.')), t->ix->ns);
        if (!g || g->defs.empty())
            Error(t->line, cat("in ", t->ix->name, ": the array of an index type must be a "
                               "global variable; a local or parameter has no name where a "
                               "type is declared (§3.11)"));
        t->ix->arr = NamedArrayComponent(g->defs[0], name, t->line);
        indexarrays.insert(t->ix->arr);
    }
}



inline void TypeCheck::ValidateIndexType(TypeExpr *t) {
    auto a = t->ix->arr;
    auto at = a->type;
    if (!at || !IsArrayKind(at, A_GROW) || !a->isvar)
        Error(t->line, cat("in ", a->name, ": the array of an index type must be a grow-only "
                           "resizable global var (", a->name, ": T[>..]), not ",
                           at ? TypeStr(at) : string("an unresolved type"),
                           ", since the type relies on its length never going down (§3.11)"));
    if (ClassOf(at->arr->sub) != SC_FIXED)
        Error(t->line, cat("in ", a->name, ": ", TypeStr(at), " holds variable-size elements, "
                           "which cannot be indexed, so nothing can be an index of it (§3.11)"));
}




inline VarDef *TypeCheck::PoolOf(VarDef *r) {
    if (!r) return nullptr;
    if (r->isglobal) return poolglobals.count(r) || poolowners.count(r) ? r : nullptr;
    if (r->classpool) return r->classpool;
    if (r->componentowner) {
        auto owner = PoolOf(r->componentowner);
        if (owner && owner->type && TypeEq(owner->type, r->componentbase)) {
            auto c = owner->components[r->componentfield];
            return poolglobals.count(c) ? c : nullptr;
        }
    }
    return nullptr;
}



inline void TypeCheck::ValidatePool(TypeExpr *t) {
    auto pool = t->ref->pool;
    auto pt = pool->type;
    if (!pt || !IsArrayKind(pt, A_GROW))
        Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                           "grow-only resizable global (", pool->name, ": T[>..]), not ",
                           pt ? TypeStr(pt) : string("an unresolved type"), " (§3.9)"));
    if (!pool->isvar)
        Error(t->line, cat("in ", pool->name, ": a relative reference's pool must be a "
                           "var (§3.9)"));
    if (!CanContain(pt, t->ref->sub))
        Error(t->line, cat("in ", pool->name, ": ", TypeStr(pool->type),
                           " cannot hold a value of type ", TypeStr(t->ref->sub),
                           ", so nothing in it can be pointed at (§3.9)"));
}

















template<typename F> bool TypeCheck::StableTypeQuery(TypePairCache &cache, TypeExpr *t,
                                                     TypeExpr *of, F walk) {
    pair key { t, of };
    if (auto known = cache.find(key)) {
        
        assert(*known == walk());
        return *known;
    }
    if (typesbuilding || !stabletypes.Ready(t) || !stabletypes.Ready(of)) return walk();
    auto r = walk();
    cache[key] = r;
    return r;
}







inline bool TypeCheck::CanContain(TypeExpr *t, TypeExpr *of) {
    if (!t) return false;
    switch (t->kind) {
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: case TY_ARRAY:
            return StableTypeQuery(cancontain, t, of, [&]() { return CanContainWalk(t, of); });
        default: return CanContainWalk(t, of);
    }
}

inline bool TypeCheck::CanContainWalk(TypeExpr *t, TypeExpr *of) {
    if (TypeEq(t, of)) return true;
    auto loaded = LoadedTypeEq(t, of);
    assert(loaded == TypeEq(LoadType(t), of));
    if (loaded) return true;
    
    
    if (t->kind == TY_STRUCT && t->struc->st->isroot) return false;
    if (t->kind == TY_ARRAY) return CanContain(t->arr->sub, of);
    return AnyField(t, [&](TypeExpr *ft) { return CanContain(ft, of); });
}




inline bool TypeCheck::MayHoldValueOf(TypeExpr *t, TypeExpr *of) {
    if (MaySameType(t, of)) return true;
    if (t->kind == TY_ARRAY) return MayHoldValueOf(t->arr->sub, of);
    return AnyField(t, [&](TypeExpr *ft) { return MayHoldValueOf(ft, of); });
}




inline bool TypeCheck::MaySameType(TypeExpr *a, TypeExpr *b) {
    auto adt = [](TypeExpr *x) { return x->kind == TY_ENUM || x->kind == TY_VARIANT; };
    if (adt(a) || adt(b))
        return adt(a) && adt(b) &&
               (a->kind != TY_ENUM || b->kind != TY_ENUM || a->enu->en == b->enu->en);
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TY_STRUCT: return a->struc->st == b->struc->st;
        case TY_INT:
            return a->intstorage == b->intstorage || a->intstorage == IS_VARINT ||
                   b->intstorage == IS_VARINT;
        case TY_FLT: return a->fltstorage == b->fltstorage;
        case TY_ARRAY:
            return a->arr->akind == b->arr->akind && MaySameType(a->arr->sub, b->arr->sub);
        default: return true;
    }
}



inline TypeExpr *TypeCheck::PointeeOf(TypeExpr *t) {
    if (t->kind == TY_REF) return LoadType(t->ref->sub);
    if (t->kind == TY_SLICE) return t->sub;
    return nullptr;
}






inline void TypeCheck::LexicalVars(int fi, vector<bool> *seen,
                                   const function<void(VarDef *)> &f) {
    while (fi >= 0 && !(seen && (*seen)[fi])) {
        if (seen) (*seen)[fi] = true;
        auto &fr = frames[fi];
        auto limit = fi == (int)frames.size() - 1 ? (int)vars.size()
                                                  : frames[fi + 1].varbase;
        for (auto i = limit - 1; i >= fr.varbase; i--) f(vars[i]);
        fi = fr.isdefault ? fi - 1 : fr.lexframe;
    }
}







inline void TypeCheck::VisibleVars(const function<void(VarDef *)> &f) {
    LexicalVars((int)frames.size() - 1, nullptr, f);
}





inline void TypeCheck::ShrinkScanVars(const function<void(VarDef *)> &f) {
    vector<bool> seen(frames.size(), false);
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        LexicalVars(fi, &seen, f);
        if (!frames[fi].isfunval) break;
    }
}



inline bool TypeCheck::StaticCanContain(TypeExpr *of) {
    return of->kind == TY_INT && of->intstorage == IS_U8;
}



inline void TypeCheck::ReachedThroughRefs(TypeExpr *t, vector<TypeExpr *> &out) {
    vector<TypeExpr *> work;
    RefPointees(t, work);
    while (!work.empty()) {
        auto p = work.back();
        work.pop_back();
        auto again = false;
        for (auto s : out) again = again || TypeEq(s, p);
        if (again) continue;
        out.push_back(p);
        RefPointees(p, work);
    }
}


inline bool TypeCheck::ReachesThroughRefs(TypeExpr *t, TypeExpr *of) {
    return StableTypeQuery(reachesthrough, t, of, [&]() {
        vector<TypeExpr *> reached;
        ReachedThroughRefs(t, reached);
        for (auto p : reached)
            if (CanContain(p, of)) return true;
        return false;
    });
}







inline void TypeCheck::BoundReach(VarDef *r, vector<TypeExpr *> &out) {
    if (!r) return;
    if (r->type) ReachedThroughRefs(r->type, out);
    else out.insert(out.end(), r->classreach.begin(), r->classreach.end());
}















inline Roots TypeCheck::RootCandidates(TypeExpr *of, int d, bool globalsonly, bool writable,
                                       bool slots) {
    Roots out;
    auto beyond = [&](VarDef *v, TypeExpr *t, int rd) {
        if (!v->isparam || !v->refrootknown || !ReachesThroughRefs(t, of)) return;
        for (auto &a : v->ref.alts)
            if (Depth(a.root) <= rd) out.Add({ a.root, false });
    };
    auto consider = [&](VarDef *v, int rd) {
        if (!v->type) return;
        if (IsRefOrSlice(v->type)) {
            if (!v->refrootknown) return;   
            if (slots && SliceVarOf(v) && (v->slotref || v->isglobal) && Depth(v) <= rd &&
                CanContain(v->type, of))
                out.Add({ v, true });
            auto pt = PointeeOf(v->type);
            if (!pt) return;
            if (CanContain(pt, of))
                for (auto &a : v->ref.alts)
                    if (Depth(a.root) <= rd) out.Add({ a.root, a.exact, a.from });
            beyond(v, pt, rd);
        } else {
            if (Depth(v) <= rd && CanContain(v->type, of)) out.Add({ v, true });
            
            beyond(v, v->type, rd);
        }
    };
    if (!globalsonly) VisibleVars([&](VarDef *v) { if (!v->isglobal) consider(v, d); });
    auto unchecked = false;
    for (auto g : ast.globals)
        for (auto gd : g->defs) {
            consider(gd, 0);
            for (auto c : gd->components) if (c) consider(c, 0);
            unchecked = unchecked || (!gd->type && !(g->type && IsRefOrSlice(g->type)));
        }
    
    
    
    auto hasstatic = out.Has(nullptr) || StaticCanContain(of);
    out.alts.erase(std::remove_if(out.alts.begin(), out.alts.end(),
                                  [](const RootAlt &a) { return !a.root; }),
                   out.alts.end());
    if (writable && !out.None()) hasstatic = false;
    if (hasstatic) out.Add({ nullptr, true });
    
    
    
    
    
    if (unchecked && CurRealFrame().spec) out.Add({ nullptr, false });
    return out;
}






inline bool TypeCheck::TempContents(const Val &v, ReadBack &contents) {
    if (!IsTemp(v.Root()) || IsRefOrSlice(v.type)) return false;
    contents.roots = ContentsOf(v);
    contents.from = v.holderfrom;
    return true;
}





inline Roots TypeCheck::ReadBackRoot(TypeExpr *rt, const Roots &container, bool byteview,
                                     const ReadBack *contents, bool slotread, bool inplace) {
    Roots out;
    
    
    if (container.Unknown()) {
        out.unknown = true;
        return out;
    }
    auto relative = IsRelRef(rt);
    if (contents && !relative && container.Any([&](const RootAlt &a) { return IsTemp(a.root); }))
        return contents->roots;
    
    
    if (relative && rt->ref->pool) {
        out.Set(rt->ref->pool, true);
        return out;
    }
    auto of = PointeeOf(rt);
    for (auto &c : container.alts) {
        auto croot = c.root;
        
        
        
        
        auto from = c.exact ? croot : nullptr;
        auto classread = from && IsClassRoot(croot);
        
        if (croot && !croot->isglobal && croot->contents.Unknown()) {
            out.unknown = true;
            continue;
        }
        if (byteview) {
            
            
            if (from && !croot->isglobal && !croot->contents.None()) {
                for (auto &a : croot->contents.alts) out.Add({ a.root, false, from });
            } else {
                out.Add({ croot, false, from, false, classread });
            }
            continue;
        }
        
        
        if (relative) {
            out.Add({ croot, c.exact, nullptr, false, false, c.projected });
            continue;
        }
        if (!of || !croot || IsTemp(croot)) {
            out.Add({ croot, false });
            continue;
        }
        
        
        
        
        
        
        
        auto global = croot->isglobal;
        if (c.exact && inplace && croot->heldholder) {
            NoteEnvRead(croot->heldholder);
            if (ContentsReadBack(croot->heldholder, of, out)) continue;
        }
        if (!global && (!c.exact || croot->ownerspec != CurRealFrame().spec)) {
            if (c.exact && inplace && croot->type && !IsTemp(croot) && croot->ownerspec &&
                OuterLocal(croot) && ContentsReadBack(croot, of, out))
                continue;
            
            
            
            
            if (ClassOf(of) == SC_ROOT && IsClassRoot(croot) && croot->componentowner) {
                auto base = croot;
                while (base->componentowner) base = base->componentowner;
                out.Add({ base, false, from });
                continue;
            }
            out.Add({ croot, false, from, false, classread });
            continue;
        }
        
        
        if (!global && inplace && ContentsReadBack(croot, of, out)) continue;
        
        
        
        
        
        
        auto cands = RootCandidates(of, Depth(croot), global, !rt->cq, rt->kind == TY_REF);
        
        
        
        
        
        if (slotread)
            erase_if(cands.alts, [&](const RootAlt &a) {
                return a.exact && GrowShrinkCanHold(a.root, of);
            });
        if (cands.None()) {
            
            out.Add({ croot, false, from });
            continue;
        }
        for (auto &a : cands.alts) out.Add({ a.root, a.exact, from });
    }
    return out;
}























inline bool TypeCheck::ContentsReadBack(VarDef *h, TypeExpr *of, Roots &out) {
    if (!h->type || (IsRefOrSlice(h->type) && !h->componentowner)) return false;
    Roots r;
    auto known = false;
    for (auto &a : h->contents.alts) {
        if (a.classread) r.Add({ a.root, false, h, false, true });
        else if (!a.root) r.Add({ nullptr, a.exact });
        else if (a.exact) {
            if (!of || !a.root->type || CanContain(LoadType(a.root->type), of))
                r.Add({ a.root, true, h });
        }
        else if (IsClassRoot(a.root)) r.Add({ a.root, false, h });
        else return false;
        known = known || a.classread || a.exact || IsClassRoot(a.root);
    }
    if (!known) return false;
    out.Add(r);
    return true;
}




inline string TypeCheck::ReadBackWhy(const Roots &r) {
    auto from = r.From();
    if (!from) return {};
    auto n = r.alts.size();
    auto global = BoundAnywhere(from);
    string s = global ? cat(from->name, " is a global var, which any function may bind, and may "
                                        "point into ")
                      : cat("it was read out of ", from->name, " and may point into ");
    for (size_t i = 0; i < n; i++) {
        auto &a = r.alts[i];
        if (i) s += i + 1 == n ? " or " : ", ";
        if (!a.root) { s += a.exact ? "static data" : "any global"; continue; }
        if (global && a.root == from) { s += "any global"; continue; }
        if (!a.exact && !a.root->type && !a.root->isglobal) s += "the caller's storage behind ";
        s += a.root->name;
    }
    return s;
}


inline bool TypeCheck::HoldsByValue(TypeExpr *t, TypeExpr *of, vector<TypeExpr *> &open) {
    if (TypeEq(t, of)) return true;
    for (auto o : open)
        if (TypeEq(o, t)) return false;
    open.push_back(t);
    auto holds = t->kind == TY_ARRAY ? HoldsByValue(t->arr->sub, of, open)
                                     : AnyField(t, [&](TypeExpr *ft) {
                                           return HoldsByValue(ft, of, open);
                                       });
    open.pop_back();
    return holds;
}





inline int TypeCheck::ElemArrayPlaces(TypeExpr *t, TypeExpr *of) {
    vector<TypeExpr *> open;
    if (TypeEq(t, of)) return 2;
    if (t->kind == TY_ARRAY) {
        if (!TypeEq(t->arr->sub, of)) return HoldsByValue(t->arr->sub, of, open) ? 2 : 0;
        return AnyField(of, [&](TypeExpr *ft) { return HoldsByValue(ft, of, open); }) ? 2 : 1;
    }
    auto n = 0;
    EachField(t, [&](TypeExpr *ft) { n = min(2, n + ElemArrayPlaces(ft, of)); });
    return n;
}





inline bool TypeCheck::OneArrayOf(VarDef *r, TypeExpr *elem) {
    if (!r) return false;
    if (r->type) return ElemArrayPlaces(r->type, elem) == 1;
    for (auto e : r->onearray)
        if (TypeEq(e, elem)) return true;
    return false;
}





inline void TypeCheck::ArrayElemsReached(TypeExpr *t, vector<TypeExpr *> &out,
                                         vector<TypeExpr *> &open) {
    for (auto o : open)
        if (TypeEq(o, t)) return;
    open.push_back(t);
    if (t->kind == TY_REF) {
        ArrayElemsReached(LoadType(t->ref->sub), out, open);
    } else if (t->kind == TY_SLICE) {
        ArrayElemsReached(t->sub, out, open);
    } else if (t->kind == TY_ARRAY) {
        auto seen = false;
        for (auto e : out) seen = seen || TypeEq(e, t->arr->sub);
        if (!seen) out.push_back(t->arr->sub);
        ArrayElemsReached(t->arr->sub, out, open);
    } else {
        EachField(t, [&](TypeExpr *ft) { ArrayElemsReached(ft, out, open); });
    }
}




inline void TypeCheck::AppendArrayElemsReached(TypeExpr *t, vector<TypeExpr *> &out) {
    if (typesbuilding || !stabletypes.Ready(t)) {
        vector<TypeExpr *> open;
        ArrayElemsReached(t, out, open);
        return;
    }
    
    
    
    auto root = t;
    while (root->kind == TY_REF || root->kind == TY_SLICE)
        root = root->kind == TY_REF ? root->ref->sub : root->sub;
    auto key = root->kind == TY_STRUCT ? (const void *)root->struc->inst : t;
    auto it = arrayelements.find(key);
    if (it == arrayelements.end()) {
        vector<TypeExpr *> elems, open;
        ArrayElemsReached(t, elems, open);
        it = arrayelements.emplace(key, std::move(elems)).first;
    }
#ifndef NDEBUG
    
    
    vector<TypeExpr *> check, open;
    ArrayElemsReached(t, check, open);
    assert(TypeArgsEq(check, it->second));
#endif
    
    if (out.empty()) {
        out = it->second;
        return;
    }
    for (auto e : it->second) {
        auto found = false;
        for (auto x : out) if (TypeEq(x, e)) { found = true; break; }
        if (!found) out.push_back(e);
    }
}






inline bool TypeCheck::RootedAtReceiver(const Val &rv, const Val &av, TypeExpr *elem) {
    if (!av.Exact() || !rv.Exact()) return false;
    auto recvroot = rv.Root(), aroot = av.Root();
    auto recvpool = PoolOf(recvroot);
    if (aroot != recvroot && !(recvpool && PoolOf(aroot) == recvpool))
        return false;
    return OneArrayOf(recvroot, elem);
}





inline string TypeCheck::LibraryParamArg(VarDef *root) {
    auto &f = CurRealFrame();
    if (!root || !f.spec || !f.call || !ast.StdlibFile(f.sf->line.fileidx)) return "";
    auto an = f.call->ValueArgNodes();
    for (size_t p = 0; p < f.spec->params.size() && p < an.size(); p++)
        if (f.spec->params[p]->ref.Root() == root) return ExprStr(an[p]);
    return "";
}


inline void TypeCheck::CheckRootedAtReceiver(Call *c, const char *op, const Val &rv,
                                             const Val &av, TypeExpr *elem, const char *what,
                                             const char *sec) {
    if (RootedAtReceiver(rv, av, elem)) return;
    if (av.None() || rv.None()) return;   
    auto why = av.Exact() ? string() : ReadBackWhy(av);
    auto root = av.Root() ? av.Root()->name : string_view("static data");
    
    
    auto shared = av.Exact() && rv.Exact() && av.Root() && av.Root() == rv.Root();
    
    
    auto arg = av.Exact() ? LibraryParamArg(av.Root()) : string();
    Error(c, cat(".", op, " needs ", what, " rooted at the array itself (", sec, "); ",
                 !why.empty() ? why
                 : !arg.empty() ? cat("this one is ", arg, ", which ", CurRealFrame().sf->name,
                                      " takes as a parameter, from any root")
                 : !av.Exact() ? cat("this one's root is not known exactly, only that it "
                                     "outlives ", root)
                 : shared ? cat("this one is rooted at ", av.Root()->type ? "" : "the caller's "
                                "storage behind ", root, ", which may hold ", TypeStr(elem),
                                " values outside that array")
                 : cat("this one is rooted at ", root)));
}

}  
