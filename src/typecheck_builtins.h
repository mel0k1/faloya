



#pragma once

namespace faloya {








inline Val TypeCheck::CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv) {
    c->builtin = d.kind;
    if (c->trailing) Error(c, cat(d.name, " takes no function value"));
    if (!(d.flags & BF_TYARGS) && !c->tyargs.empty())
        Error(c, cat(d.name, " takes no type arguments"));
    if ((int)args.size() < d.minargs || (int)args.size() > d.maxargs)
        Error(c, cat(d.name, " takes ", (int64_t)d.minargs,
                     d.minargs == d.maxargs ? string() : cat("-", (int64_t)d.maxargs),
                     " argument(s), ", (int64_t)args.size(), " given"));
    
    switch (d.kind) {
        case B_PRINT:
            for (size_t i = 0; i < args.size(); i++) CheckPrintable(c, d.name, args, i);
            return VoidVal();
        case B_STR: {
            
            
            for (size_t i = 0; i < args.size(); i++) CheckPrintable(c, d.name, args, i);
            auto t = GrowU8Array(c->line);
            c->rettypes.push_back(t);
            Val v;
            v.type = t;
            v.Set(TempRoot(), false);
            return v;
        }
        case B_ASSERT:
            CheckCond(args[0]);
            NarrowCond(args[0], true);  
            return VoidVal();
        case B_ABORT: case B_EXIT:
            
            
            if (d.kind == B_ABORT) CheckArg(args[0], u8slice);
            else CheckIntAny(args[0]);
            reachable = false;
            return VoidVal();
        case B_THREAD_SPAWN: case B_THREAD_SPAWN_CONTAINED: {
            auto wid = Is<Ident>(args[0]);
            vector<SFunction *> workers;
            
            
            auto wvd = wid ? LookupVar(wid->name, wid->ns) : nullptr;
            if (wid && !(wvd && !wvd->isglobal) && !ScopeNameKind(wid->name))
                for (auto sf : ast.LookupFunctions(wid->name, wid->ns))
                    if (sf->isthread) workers.push_back(sf);
            
            
            const FnValBind *fvb = nullptr;
            if (wid && !(wvd && !wvd->isglobal) &&
                !LookupTypeParam(wid->name, fvb) && fvb && fvb->named) {
                if (fvb->set) {
                    for (auto sf : *fvb->set) if (sf->isthread) workers.push_back(sf);
                } else if (fvb->named->isthread) {
                    workers.push_back(fvb->named);
                }
            }
            
            
            if (workers.size() > 1) {
                string which;
                for (auto sf : workers)
                    Append(which, "\n  candidate thread_fn ", sf->name, " at ", Where(sf->line));
                Error(c, cat("ambiguous ", d.name, " of ", wid->name, ": more than one thread_fn "
                             "has that name (§11.2)", which));
            }
            auto wsf = workers.empty() ? nullptr : workers[0];
            if (!wsf && fvb)
                Error(c, cat(d.name, "'s first argument names a thread_fn: ", wid->name,
                             " is bound to ", fvb->named ? cat(fvb->named->name, ", which is not one")
                                                         : string("a block")));
            if (!wsf) Error(c, cat(d.name, "'s first argument names a thread_fn"));
            wid->fnref = wsf;
            wid->exprtype = fntype;
            auto spec = EnsureThreadSpec(wsf, c->line);
            if (args.size() != 1 + spec->argtypes.size())
                Error(c, cat(d.name, "(", wsf->name, ", ...) takes ",
                             (int64_t)spec->argtypes.size(), " worker argument(s)"));
            {
                DestScope ds(*this, Dest {});
                for (size_t i = 0; i < spec->argtypes.size(); i++) {
                    if (HoldsIndexT(spec->argtypes[i])) ThreadIndexError(c, spec->argtypes[i]);
                    CheckArg(args[1 + i], spec->argtypes[i]);
                }
            }
            c->spec = spec;
            Val v;
            v.type = ast.inttypes[IS_I64];
            return v;
        }
        case B_THREAD_WAIT: {
            
            
            CheckIntAny(args[0]);
            if (args.size() == 2) CheckByteOut(c, args[1], "thread_wait(id, msg)");
            Val v;
            v.type = ast.booltype;
            return v;
        }
        case B_QPUT: {
            auto av = CheckValue(args[0], nullptr);
            NoUntypedEmptyArray(av, args[0], d.name);
            if (av.type->kind == TY_VOID || av.type->kind == TY_FN || !IsFlat(av.type))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(av.type)));
            if (HoldsIndexT(av.type)) ThreadIndexError(c, av.type);
            
            Val ok;
            ok.type = ast.booltype;
            return ok;
        }
        case B_QGET: case B_QPOLL: case B_QLIMIT: {
            if (c->tyargs.size() != 1)
                Error(c, cat(d.name,
                             d.kind == B_QLIMIT ? "<T>(n)" : args.empty() ? "<T>()" : "<T>(ms)",
                             " needs exactly one explicit type argument"));
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            if (!IsFlat(t))
                Error(c, cat("queue elements must be flat (§11.2), not ", TypeStr(t)));
            if (HoldsIndexT(t)) ThreadIndexError(c, t);
            
            if (!args.empty()) CheckArg(args[0], ast.inttypes[IS_I64]);
            if (d.kind == B_QLIMIT) {
                c->qtype = t;
                return VoidVal();
            }
            c->rettypes.push_back(t);
            Val first;
            first.type = t;
            first.Set(TempRoot(), false);
            lastcallrets.clear();
            lastcallrets.push_back(first);
            if (d.kind == B_QPOLL || !args.empty()) {
                Val b2;
                b2.type = ast.booltype;
                c->rettypes.push_back(ast.booltype);
                lastcallrets.push_back(b2);
            }
            return first;
        }
        case B_EMBED_SHADER:
        case B_EMBED_FILE: {
            
            
            
            
            c->shaderblob = c->builtin == B_EMBED_FILE ? EmbedFile(c, args) : EmbedShader(c, args);
            c->rettypes.push_back(cu8slice);
            Val v;
            v.type = cu8slice;
            v.Set(nullptr, true);   
            v.writable = false;
            return v;
        }
        case B_COPY: {
            
            
            auto av = CheckV(args[0], nullptr);
            args[0]->exprtype = av.type;
            auto v = DecayRef(av);
            if (ClassOf(v.type) == SC_ROOT)
                Error(c, "root owners cannot be copied; bind a reference instead");
            if (v.type->kind == TY_SLICE)
                Error(c, "copy takes a value or a reference, not a slice");
            if (!av.lvalue && !IsPlainRef(av.type))
                Error(c, "copy of a temporary: the value is fresh already");
            NoRelRefCopy(args[0], v.type);
            if ((v.type->kind == TY_ENUM || v.type->kind == TY_VARIANT) &&
                ClassOf(v.type) == SC_RESIZABLE)
                Error(c, "copying a resizable ADT or variant is not supported by the "
                         "C backend yet; construct a fresh value or pass the owning "
                         "value by reference");
            v.lvalue = false;
            
            
            if (!v.holderset && HoldsPlainRef(v.type)) {
                v.contents = Bounds(v);
                v.holderfrom = HolderSource(v);
                v.holderset = true;
            }
            v.Set(TempRoot(), true);
            v.writable = false;
            c->rettypes.push_back(v.type);
            return v;
        }
        case B_FROM_BYTES: {
            
            
            
            
            
            if (c->tyargs.size() != 1)
                Error(c, "from_bytes<T[>..]>(bytes) needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            
            
            
            
            
            auto ak = t->kind == TY_ARRAY ? t->arr->akind : A_FIXED;
            if (t->kind != TY_ARRAY || (ak != A_GROW && ak != A_GROWSHRINK && ak != A_VAR))
                Error(c, cat("from_bytes builds a resizable or variable array "
                             "(T[>..], T[>..<], T[]), not ", TypeStr(t)));
            auto el = t->arr->sub;
            string why;
            if (!VerifiableElem(el, el, why))
                Error(c, cat("from_bytes<", TypeStr(t), "> has no verifier: ", why));
            Reaches(el, [&](TypeExpr *x) {
                if (IsIndexT(x))
                    RequireIndexArray(x, c, cat("from_bytes<", TypeStr(t), "> verifying ",
                                                TypeStr(x)));
                return false;
            });
            
            
            
            
            
            if (ak == A_GROWSHRINK && HoldsRelRef(el))
                Error(c, cat("from_bytes<", TypeStr(t),
                             "> cannot hold relative references: a shrink would free "
                             "what they still name"));
            CheckArg(args[0], u8slice);
            c->rettypes.push_back(t);
            c->rettypes.push_back(ast.booltype);
            Val first;
            first.type = t;
            first.Set(TempRoot(), false);
            Val ok;
            ok.type = ast.booltype;
            lastcallrets.clear();
            lastcallrets.push_back(first);
            lastcallrets.push_back(ok);
            return first;
        }
        case B_DEFAULT: {
            
            
            if (c->tyargs.size() != 1)
                Error(c, "default<T>() needs exactly one explicit type argument");
            auto t = Subst(c->tyargs[0]);
            ValidateType(t, c->line, VT_LOCAL);
            
            
            auto vt = ValueType(t);
            if (ClassOf(vt) != SC_FIXED)
                Error(c, cat("default<T>() needs a fixed-size type, not ", TypeStr(t)));
            string why;
            if (!HasDefault(t, why))
                Error(c, cat("default<", TypeStr(t), ">() does not exist: ", why));
            c->rettypes.push_back(vt);
            if (!c->defaultinit) {
                if (t->kind == TY_STRUCT || t->kind == TY_ENUM || t->kind == TY_VARIANT) {
                    auto st = t->kind == TY_ENUM ? ast.VariantTypeOf(t, &t->enu->en->variants[0], c->line) : t;
                    auto sl = ast.New<StructLit>(c->line, st);
                    sl->defaultall = true;
                    sl->implicit = true;
                    c->defaultinit = sl;
                } else if (t->kind == TY_ARRAY && t->arr->akind == A_FIXED && ArraySize(t->arr))
                    c->defaultinit = DefaultCall(t->arr->sub, c->line);
            }
            if (c->defaultinit) {
                auto v = CheckValue(c->defaultinit, t->kind == TY_ARRAY ? t->arr->sub : t);
                v.type = t;
                
                
                if (t->kind == TY_ARRAY) v = TempCopy(v);
                return v;
            }
            Val v;
            v.type = vt;
            v.Set(nullptr, true);   
            v.writable = true;      
            
            
            if (!IsRefOrSlice(vt)) v = TempCopy(v);
            return v;
        }
        default: break;
    }
    
    Val rv;
    TypeExpr *elem = nullptr;
    TypeExpr *recvarr = nullptr;   
    auto ak = A_FIXED;
    if (d.flags & BF_MEMBER) {
        if (precv) {
            rv = *precv;
        } else {
            rv = CheckV(args[0], nullptr);
            args[0]->exprtype = rv.type;
        }
        NoUntypedEmptyArray(rv, args[0], d.name);
        auto rt = rv.type;
        if (IsPlainRef(rt)) {
            rt = rt->ref->sub;
            if (rt->kind == TY_SLICE) {
                
                auto sv = SlotView(rv, rt);
                sv.reached = nullptr;
                rv.SetProv(sv);
            } else {
                
                rv.ClearReads();
                rv.reached = LoadType(rt);
            }
        }
        if (rt->kind == TY_ARRAY) {
            ak = rt->arr->akind;
            elem = rt->arr->sub;
            recvarr = rt;
        } else if (rt->kind == TY_SLICE) {
            elem = rt->sub;
        }
        if (!(RecvKindOf(rt) & d.recv))
            Error(c, cat(".", d.name, " is not available on ", TypeStr(rv.type)));
        if ((d.flags & BF_WRITE) && !rv.writable)
            Error(c, cat("cannot .", d.name, " through a non-writable value "
                         "(let, const, or a read-only instantiation, §9.5)"));
        
        
        auto notpoolref = [&](int ru) -> string {
            if (rv.reusable) return "";
            for (auto &a : rv.alts)
                if (a.root && (a.root->reusable & ru))
                    return cat(": ", ExprStr(args[0]), " may point at ", a.root->name,
                               ", but is not a pool reference");
            return "";
        };
        if ((d.flags & BF_REUSABLE) && !(rv.reusable & RU_SLOTS))
            Error(c, cat(".", d.name, " exists on reusable pools only",
                         rv.reusable ? ", not on the slice pools of reusable[]" : "",
                         notpoolref(RU_SLOTS), " (§5.4)"));
        if ((d.flags & BF_SLICEPOOL) && !(rv.reusable & RU_SLICES))
            Error(c, cat(".", d.name, " exists on reusable[] pools only",
                         rv.reusable ? ", not on the slot pools of reusable" : "",
                         notpoolref(RU_SLICES), " (§5.4)"));
    }
    if (d.kind == B_MOVE_FROM) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, ".move_from requires fixed-size elements");
        auto sv = CheckV(args[1], nullptr);
        args[1]->exprtype = sv.type;
        if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
            Error(c, cat(".move_from takes a slice of ", TypeStr(elem), ", got ", TypeStr(sv.type)));
        if (HasRelRefT(elem))
            Error(c, ".move_from cannot copy elements containing self-relative references");
        AppendedCopies(args[1], sv, elem, rv);
        return VoidVal();
    }
    
    
    if (elem && elem->kind == TY_VOID) {
        auto rt = rv.type;
        if (IsPlainRef(rt)) rt = rt->ref->sub;
        if (d.args[0] == 'e') {
            auto av = DecayRef(CheckV(args[1], nullptr));
            
            JudgeCastAt(args[1], nullptr, av);
            CompletePending(rt, PendingElemFrom(av, args[1]), c->line);
        } else if (d.args[0] == 'a') {
            auto av = DecayRef(CheckV(args[1], nullptr));
            CompletePending(rt, PendingElemFromSeq(av, c), c->line);
        } else if (d.kind == B_FORMAT) {
            CompletePending(rt, ast.inttypes[IS_U8], c->line);
        } else {
            RequireComplete(rt, c->line);
        }
        elem = rt->arr->sub;
    }
    
    
    
    if (*d.grows && !(d.flags & BF_OUTARG))
        NoteGrow(c, rv, cat(d.grows, " ", ExprStr(args[0])), recvarr);
    
    
    
    
    
    if (d.kind == B_TO_BYTES || d.kind == B_BYTES_OF) {
        string why;
        if (!ImageSafe(elem, why))
            Error(c, cat(d.name, " cannot write ", TypeStr(rv.type), " out: ", why));
        if (d.kind == B_BYTES_OF) {
            NoTemporaryLiteral(args[0], rv.type);
            Val v;
            v.type = cu8slice;   
            v.SetProv(rv);
            
            
            
            v.writable = false;
            v.reusable = false;
            v.byteview = true;
            v.freshview = true;
            c->rettypes.push_back(v.type);
            return v;
        }
        if (args.size() == 2) {
            CheckByteOut(c, args[1], "to_bytes(a, out)");
            return VoidVal();
        }
        auto t = GrowU8Array(c->line);
        c->rettypes.push_back(t);
        Val v;
        v.type = t;
        v.Set(TempRoot(), false);
        return v;
    }
    
    
    if (d.kind == B_FORMAT) {
        if (!IsU8(elem))
            Error(c, cat(".format appends text to u8 arrays, not ", TypeStr(rv.type)));
        for (size_t i = 1; i < args.size(); i++) CheckPrintable(c, d.name, args, i, &rv);
        return VoidVal();
    }
    
    
    
    auto shrink = (d.flags & BF_SHRINKS) != 0;
    if (ak == A_GROW && shrink) {
        CheckGrowShrink(c, d.name, args[0], rv);
        if (d.kind != B_CLEAR && ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
    }
    
    
    if (ak == A_GROWSHRINK && shrink)
        ShrinkThrough(c, d.name, ExprStr(args[0]), rv,
                      IsPlainRef(rv.type) ? rv.type->ref->sub : rv.type,
                      d.kind == B_RESIZE && ResizesToMark(args[0], args[1]) ? SB_BALANCED
                                                                            : SB_UNBALANCED);
    
    if (d.kind == B_RESIZE) {
        CheckIntAny(args[1]);
        if (args.size() == 3) {
            ElemArg(args[2], elem, rv, true);
            
            
            if (HasRelRefT(elem) && (Is<StructLit>(args[2]) || Is<ArrayLit>(args[2])))
                Error(args[2], cat(".resize copies its fill value into every slot it adds: "
                                   "copying a value of type ", TypeStr(elem), ", which "
                                   "contains self-relative references, is not supported; push "
                                   "the elements, which constructs each in place"));
        }
        return VoidVal();
    }
    
    
    
    
    
    if (d.kind == B_INDEX_OF) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, cat(".index_of needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
        auto av = CheckV(args[1], nullptr);
        if (av.lvalue && av.type->kind != TY_REF && TypeEq(StorageType(av), elem))
            args[1] = AutoRef(args[1], av, false);
        else if (UserRefOf(args[1]))
            Warn(args[1], cat("redundant &: ", ExprStr(Is<Unary>(args[1])->child),
                              " is passed by reference without it (§4.1)"));
        args[1]->exprtype = av.type;
        if (!IsPlainRef(av.type) || !TypeEq(av.type->ref->sub, elem))
            Error(c, cat(".index_of takes a reference to an element of ", TypeStr(rv.type),
                         ", got ", TypeStr(StorageType(av))));
        CheckRootedAtReceiver(c, d.name, rv, av, elem, "a reference", "§3.3");
    }
    if (d.kind == B_RANGE_INDEX || d.kind == B_CHECK_LENGTH) {
        if (ClassOf(elem) != SC_FIXED)
            Error(c, cat(".", d.name, " needs fixed-size elements: ", TypeStr(rv.type),
                         " is sequential (§3.3)"));
        if (d.kind == B_RANGE_INDEX) {
            auto sv = CheckV(args[1], nullptr);
            args[1]->exprtype = sv.type;
            if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
                Error(c, cat(".range_index takes a slice of ", TypeStr(rv.type),
                             ", got ", TypeStr(sv.type)));
            
            
            c->poolcheck = !RootedAtReceiver(rv, sv, elem);
        }
        
        
        if (args.size() == 3) {
            CheckArg(args[2], u8slice);
            if (!Is<StrLit>(args[2]))
                Error(args[2], cat(".", d.name, " takes its abort message as a string literal"));
        }
    }
    
    
    
    
    if (d.kind == B_ALLOC_SLICE || d.kind == B_REALLOC_SLICE || d.kind == B_FREE_SLICE) {
        if (d.kind != B_ALLOC_SLICE) {
            auto sv = CheckV(args[1], nullptr);
            args[1]->exprtype = sv.type;
            if (sv.type->kind != TY_SLICE || !TypeEq(sv.type->sub, elem))
                Error(c, cat(".", d.name, " takes a slice of ", TypeStr(rv.type), ", got ",
                             TypeStr(sv.type)));
            if (!RootedAtReceiver(rv, sv, elem)) {
                
                
                
                
                
                auto own = [&](VarDef *r) {
                    return r && (r->isglobal || r->ownerspec == CurRealFrame().spec);
                };
                if (sv.Exact() && own(sv.Root()) && !rv.None() && !rv.Has(sv.Root()) &&
                    rv.All([&](const RootAlt &a) { return own(a.root); }) && !VerdictDeferred())
                    Error(c, cat(".", d.name, " needs a slice of the pool it is called on (§5.4); "
                                 "this one is rooted at ", sv.Root()->name));
                c->poolcheck = true;
            }
        }
        if (d.kind != B_FREE_SLICE) {
            CheckIntAny(args.back());
            string why;
            if (!HasDefault(elem, why))
                Error(c, cat(".", d.name, " fills the elements it adds with default values, "
                             "and ", TypeStr(elem), " has none: ", why, " (§5.4)"));
            if (!c->defaultinit) c->defaultinit = DefaultCall(elem, c->line);
            auto logbase = cur.growlog.size();
            ElemArg(c->defaultinit, elem, rv);
            CheckGrowsSince(logbase, rv, "the default elements allocated in a slice pool",
                            recvarr);
        }
        
        
        if (d.kind == B_REALLOC_SLICE && HasRelRefT(elem))
            Error(c, cat(".realloc_slice may move the slice, which a value of type ",
                         TypeStr(elem), " cannot survive: it contains self-relative "
                         "references (§3.9)"));
    }
    
    auto base = (d.flags & BF_MEMBER) ? 1 : 0;
    for (auto i = 0; d.args[i]; i++) {
        auto &an = args[base + i];
        switch (d.args[i]) {
            case 'i': CheckIntAny(an); break;
            case 'e': {
                
                
                auto logbase = cur.growlog.size();
                ElemArg(an, elem, rv);
                if (BuiltInPlace(elem))
                    CheckGrowsSince(logbase, rv,
                                    cat("the element ",
                                        d.kind == B_PUSH ? "pushed into " : "allocated in ",
                                        ExprStr(args[0])),
                                    recvarr);
                break;
            }
            case 'a': {  
                auto logbase = cur.growlog.size();
                
                
                auto al = Is<ArrayLit>(an);
                if (al && al->capexpr) al = nullptr;
                auto av = al ? CheckValueAt(an, AppendedRun(elem, al), Dest(rv, false, rv.reached))
                             : CheckV(an, nullptr);
                an->exprtype = av.type;
                auto t2 = av.type;
                if (IsPlainRef(t2)) t2 = t2->ref->sub;
                TypeExpr *selem = nullptr;
                if (t2->kind == TY_ARRAY) selem = t2->arr->sub;
                if (t2->kind == TY_SLICE) selem = t2->sub;
                if (av.strlit) selem = ast.inttypes[IS_U8];
                if (!selem || !TypeEq(selem, elem))
                    Error(c, cat(".", d.name, " takes an array or slice of ",
                                 TypeStr(elem), ", got ", TypeStr(av.type)));
                if (!al) AppendedCopies(an, av, elem, rv);
                
                
                
                
                
                
                auto inplace = al ? ClassOf(elem) != SC_FIXED || HasRelRefT(elem, true)
                                  : Is<Call>(an) && ak != A_LIMITED && ClassOf(t2) != SC_FIXED;
                if (inplace)
                    CheckGrowsSince(logbase, rv, cat("the run appended to ", ExprStr(args[0])),
                                    recvarr);
                break;
            }
            default: assert(false);
        }
    }
    
    Val v = VoidVal();
    
    
    
    auto heldread = [&] {
        LVal lv;
        lv.SetProv(rv);
        lv.type = elem;
        lv.fromstorage = true;
        lv.isslot = true;
        v.type = LoadType(elem);
        v.SetProv(ContainerRead(lv));
        c->rettypes.push_back(v.type);
    };
    switch (d.rets[0]) {
        case 0: break;
        case 'i': v.type = ast.inttypes[IS_I64]; break;
        case 'e':
            
            
            if (IsRefOrSlice(elem)) {
                heldread();
                break;
            }
            v.type = LoadType(elem);
            v.Set(TempRoot(), false);
            if (HoldsPlainRef(v.type)) {
                
                
                v.contents = HeldAt(rv);
                for (auto &a : v.contents.alts) a.slotread = true;
                v.holderset = true;
                v.holderfrom = HolderSource(rv);
                MarkClassCopy(v);
            }
            
            c->rettypes.push_back(v.type);
            break;
        case 'r':
            
            
            
            if (elem->kind == TY_REF) {
                heldread();
                break;
            }
            v.type = ast.RefTo(elem, c->line);
            v.TakeAlts(rv);
            v.ClearReads();
            v.writable = rv.writable;
            
            c->rettypes.push_back(v.type);
            break;
        case 's':
            v.type = ast.SliceOf(elem, c->line);
            v.TakeAlts(rv);
            v.ClearReads();
            v.writable = rv.writable;
            
            c->rettypes.push_back(v.type);
            break;
        default: assert(false);
    }
    return v;
}



inline void TypeCheck::CheckByteOut(Call *c, Node *out, const string &form) {
    auto ov = CheckV(out, nullptr);
    out->exprtype = ov.type;
    auto ot = ov.type;
    if (IsPlainRef(ot)) ot = ot->ref->sub;
    auto ok = ot->kind == TY_ARRAY && IsU8(ot->arr->sub) &&
              (ot->arr->akind == A_GROW || ot->arr->akind == A_GROWSHRINK ||
               ot->arr->akind == A_LIMITED);
    if (!ok) Error(c, cat(form, " appends to a growable u8 array, not ", TypeStr(ov.type)));
    if (!ov.writable)
        Error(c, "cannot append through a non-writable value (let, or "
                 "non-writable provenance, §9.5)");
    NoteGrow(c, ov, cat(BuiltinByKind(c->builtin).grows, " ", ExprStr(out)), ot);
}










inline void TypeCheck::CheckPrintable(Call *c, const char *what, vector<Node *> &args, size_t i,
                                      const Val *out) {
    auto &a = args[i];
    auto av = CheckValue(a, nullptr);
    NoUntypedEmptyArray(av, a, what);
    
    
    if (!av.lvalue && !av.pointee) NoRelRefTempRead(a, av.type, av.type);
    
    
    
    
    
    
    
    
    
    if (!av.lvalue && !av.pointee && av.None() && !IsRefOrSlice(av.type)) {
        if (HoldsPlainRef(av.type)) {
            av.contents = ContentsOf(av);
            av.holderset = true;
        }
        av.Set(TempRoot(), true);
    }
    
    
    
    
    
    
    
    if (av.type->kind == TY_SLICE && !av.hasslot) {
        av.slot = Prov {};
        av.slot.Set(TempRoot(), true);
        av.hasslot = true;
    }
    
    
    
    
    if (av.isvarint) {
        Val tv;
        tv.type = av.type;
        tv.Set(TempRoot(), true);
        av = tv;
    }
    
    auto saverender = tuple(cur.renderarg, cur.renderwhere, cur.rendering,
                            std::move(cur.renderwalks));
    cur.renderarg = a;
    cur.renderwhere = nullptr;
    cur.rendering = what;
    cur.renderwalks.clear();
    struct Restore {
        TypeCheck &tc;
        tuple<Node *, Node *, const char *, vector<Val>> saved;
        ~Restore() {
            tc.cur.renderarg = get<0>(saved);
            tc.cur.renderwhere = get<1>(saved);
            tc.cur.rendering = get<2>(saved);
            tc.cur.renderwalks = std::move(get<3>(saved));
        }
    } restore { *this, std::move(saverender) };
    
    auto buffer = TempRoot();
    buffer->onearray = { ast.inttypes[IS_U8] };
    Val builder;
    builder.Set(buffer, true);
    builder.writable = true;
    if (out && ClassOf(DecayedType(*out)) == SC_RESIZABLE) builder = *out;
    auto context = ast.New<Call>(c->line, c->callee);
    RenderSeen seen;
    CheckRenderable(context, what, av.type, a, seen, av, builder);
    
    
    
    for (auto t : seen.recurring) {
        vector<TypeExpr *> walked;
        if (auto part = OverloadedPart(context, t, walked))
            Error(a, cat(what, " cannot render ", TypeStr(t), ", which reaches itself through "
                         "references, around the format overload for ", TypeStr(part),
                         ": give ", TypeStr(t), " a format overload of its own (§3.7)"));
    }
    c->fmtcontexts.push_back(context);
    c->fmtspecs.insert(c->fmtspecs.end(), context->fmtspecs.begin(), context->fmtspecs.end());
}






inline TypeExpr *TypeCheck::OverloadedPart(Call *c, TypeExpr *t, vector<TypeExpr *> &seen) {
    for (auto s : seen) if (TypeEq(s, t)) return nullptr;
    seen.push_back(t);
    for (auto &fs : c->fmtspecs) if (TypeEq(fs.first, t)) return t;
    switch (t->kind) {
        case TY_ARRAY: return OverloadedPart(c, t->arr->sub, seen);
        case TY_SLICE: return OverloadedPart(c, t->sub, seen);
        case TY_REF: return OverloadedPart(c, t->ref->sub, seen);
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT: {
            TypeExpr *part = nullptr;
            EachField(t, [&](TypeExpr *ft) { if (!part) part = OverloadedPart(c, ft, seen); });
            return part;
        }
        default: return nullptr;
    }
}

inline void TypeCheck::CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at,
                                       RenderSeen &seen, Val value, const Val &out) {
    
    
    
    
    for (auto s : seen.path) {
        if (!TypeEq(s, t)) continue;
        while (t->kind == TY_REF || t->kind == TY_SLICE || t->kind == TY_ARRAY)
            t = t->kind == TY_REF ? t->ref->sub : t->kind == TY_SLICE ? t->sub : t->arr->sub;
        for (auto r : seen.recurring) if (TypeEq(r, t)) return;
        seen.recurring.push_back(t);
        return;
    }
    seen.path.push_back(t);
    struct PopSeen { vector<TypeExpr *> &types; ~PopSeen() { types.pop_back(); } } pop { seen.path };
    value.type = t;
    value.writable &= !t->cq;
    if (UserFormat(c, t, value, out, seen.path.size() == 1 ? at : nullptr)) return;
    
    
    
    if (seen.path.size() == 1 && (t->kind == TY_STRUCT || t->kind == TY_ENUM ||
                                  t->kind == TY_VARIANT || t->kind == TY_ARRAY))
        cur.renderwhere = at;
    
    
    
    
    auto walk =(t->kind == TY_ENUM && t->enu->varmode) ||
                (t->kind == TY_ARRAY && ClassOf(t) != SC_FIXED);
    if (walk) cur.renderwalks.push_back(value);
    struct PopWalk {
        TypeCheck &tc;
        bool walk;
        ~PopWalk() { if (walk) tc.cur.renderwalks.pop_back(); }
    } popwalk { *this, walk };
    
    
    auto child = [&](TypeExpr *ft, bool throughref, int component = -1) {
        auto v = value;
        if (component >= 0) v.TakeAlts(ProjectRoots(value, t, component));
        v.hasslot = ft->kind == TY_SLICE;
        if (v.hasslot) v.slot = v;
        if (throughref) {
            ReadBack contents;
            auto hascontents = TempContents(value, contents);
            v.TakeAlts(ReadBackRoot(ft, v, value.byteview, hascontents ? &contents : nullptr));
            v.writable = SlotLoadWritable(ft, value.writable);
        }
        CheckRenderable(c, what, ft, at, seen, v, out);
    };
    switch (t->kind) {
        case TY_INT: case TY_FLT: case TY_BOOL: return;
        case TY_ARRAY: child(t->arr->sub, IsRefOrSlice(t->arr->sub)); return;
        case TY_SLICE: child(t->sub, IsRefOrSlice(t->sub)); return;
        case TY_REF: child(t->ref->sub, false); return;
        case TY_STRUCT: case TY_ENUM: case TY_VARIANT:
            if (ClassOf(t) == SC_ROOT) {
                auto si = GetStructInst(t);
                for (size_t i = 0; i < si->ftypes.size(); i++)
                    if (auto ft = si->ftypes[i]) child(ft, IsRefOrSlice(ft), (int)i);
                return;
            }
            
            
            
            
            
            if (t->kind == TY_ENUM && !t->enu->varmode) value = TempCopy(value);
            EachField(t, [&](TypeExpr *ft) { child(ft, IsRefOrSlice(ft)); });
            return;
        default:
            Error(at, cat(what, " cannot render a value of type ", TypeStr(t)));
    }
}








inline FnSpec *TypeCheck::UserFormat(Call *c, TypeExpr *t, const Val &value, const Val &out,
                                     Node *arg) {
    auto tns = NominalNs(t);
    if (auto sp = UserFormatIn(c, t, tns, value, out, arg)) return sp;
    return tns.empty() ? nullptr : UserFormatIn(c, t, {}, value, out, arg);
}

inline FnSpec *TypeCheck::UserFormatIn(Call *c, TypeExpr *t, string_view ns,
                                      const Val &value, const Val &out, Node *arg) {
    auto n = ast.FindNS(ns);
    if (!n) return nullptr;
    auto fit = n->functionmap.find("format");
    if (fit == n->functionmap.end()) return nullptr;
    
    
    
    
    
    for (int pass = 0; pass < 2; pass++)
    for (auto sf : fit->second) {
        if (sf->params.size() != 2 || !sf->params[0].type || !sf->params[1].type ||
            sf->generics.empty() != (pass == 0) || sf->isnested)
            continue;
        TypeExpr *pt1, *pt0;
        if (pass == 0) {
            pt1 = Subst(sf->params[1].type);
            auto p1 = IsPlainRef(pt1) ? pt1->ref->sub : pt1;
            if (!TypeEq(p1, t)) continue;
            pt0 = Subst(sf->params[0].type);
        } else {
            
            
            auto raw = sf->params[1].type;
            auto p1 = IsPlainRef(raw) ? raw->ref->sub : raw;
            if ((p1->kind != TY_STRUCT && p1->kind != TY_ENUM && p1->kind != TY_VARIANT) ||
                p1->kind != t->kind)
                continue;
            pt1 = IsPlainRef(raw) ? ast.RefTo(t, c->line) : t;
            pt0 = sf->params[0].type;
        }
        if (!IsPlainRef(pt0) || !IsArrayKind(pt0->ref->sub, A_GROW) ||
            !IsU8(pt0->ref->sub->arr->sub))
            continue;
        vector<Val> argvals(2);
        argvals[0] = out;
        argvals[0].type = pt0;
        argvals[1] = value;
        
        
        
        if (IsPlainRef(pt1) && t->kind == TY_SLICE) {
            SlotRoots(argvals[1]);
            NoteHeld(argvals[1], t);
        }
        argvals[1].type = pt1;
        if (!IsRefOrSlice(pt1)) argvals[1].writable = true; 
        MatchInfo mi;
        mi.sf = sf;
        mi.env = nullptr;
        string why;
        if (!TryMatch(sf, c, argvals, mi, why)) continue;
        if (pass == 1) {
            pt1 = mi.paramtypes[1];
            if (mi.tier > 1) continue;
            if (!TypeEq(IsPlainRef(pt1) ? pt1->ref->sub : pt1, t)) continue;
        }
        if (sf->isextern && IsRefOrSlice(pt1) && !pt1->cq && !argvals[1].writable)
            Error(c, "extern format hook needs a writable value or a const parameter");
        
        
        if (auto id = Is<Ident>(arg); id && id->vdef && value.lvalue && IsPlainRef(pt1) &&
                                      !pt1->cq && argvals[1].writable)
            NoteWritableRef(id->vdef, arg);
        
        
        if (!IsRefOrSlice(pt1) && ClassOf(pt1) != SC_FIXED)
            Error(c, cat(TypeStr(t), " is not fixed-size and is not copied implicitly into the "
                         "format overload taking it by value (§4.1): take it by reference"));
        
        
        if (!IsRefOrSlice(pt1) && HasRelRefT(pt1))
            Error(c, cat("copying a value of type ", TypeStr(t), ", which contains "
                         "self-relative references, into the format overload taking it by "
                         "value is not supported; take it by reference"));
        auto sp = GetOrCreateSpec(mi, argvals, c);
        
        
        ApplyCalleeShrinks(c, sp, argvals, "format", { nullptr, arg });
        ApplyCalleeRebinds(sp);
        ApplyCalleeGrows(c, sp, argvals, "format");
        CheckfreeCallee(c, sp->sf);
        c->fmtspecs.push_back({ t, sp });
        return sp;
    }
    return nullptr;
}









inline StrLit *TypeCheck::ConstStrLit(Node *n) {
    if (auto s = Is<StrLit>(n)) return s;
    auto id = Is<Ident>(n);
    if (!id) return nullptr;
    if (auto vd = LookupVar(id->name, id->ns); !vd || !vd->isglobal) return nullptr;
    set<VarDecl *> visiting;
    for (;;) {
        auto g = ast.LookupGlobal(id->name, id->ns);
        if (!g || g->isvar || g->inits.size() != 1 || !visiting.insert(g).second) return nullptr;
        if (auto s = Is<StrLit>(g->inits[0])) return s;
        if (!(id = Is<Ident>(g->inits[0]))) return nullptr;
    }
}






inline const string *TypeCheck::EmbedFile(Call *c, vector<Node *> &args) {
    auto &callfile = ast.sources[c->line.fileidx].first;
    auto lit = ConstStrLit(args[0]);
    if (!lit)
        Error(args[0], "embed_file takes a string literal, or a let or const global initialized "
                       "with one");
    if (lit != args[0]) args[0] = ast.New<StrLit>(lit->line, lit->val, lit->multiline);
    args[0]->exprtype = cu8slice;
    auto path = EmbeddedShaderPath(callfile, lit->val);
    auto key = cat("file\n", path);
    auto it = ast.shaders.find(key);
    if (it == ast.shaders.end()) {
        string bytes;
        if (!LoadFile(path, bytes)) Error(c, cat("embed_file: cannot open ", path));
        it = ast.shaders.emplace(key, std::move(bytes)).first;
    }
    return &it->second;
}







inline const string *TypeCheck::EmbedShader(Call *c, vector<Node *> &args) {
    auto &callfile = ast.sources[c->line.fileidx].first;
    vector<StrLit *> lits;
    vector<bool> named;
    for (auto &a : args) {
        auto lit = ConstStrLit(a);
        if (!lit)
            Error(a, "embed_shader takes string literals, and let or const globals "
                     "initialized with one");
        
        
        named.push_back(lit != a);
        if (lit != a) a = ast.New<StrLit>(lit->line, lit->val, lit->multiline);
        a->exprtype = cu8slice;
        lits.push_back(lit);
    }
    if (lits.size() == 1) {
        if (lits[0]->multiline || lits[0]->val.find('\n') != string::npos)
            Error(c, "embed_shader(\"x.frag\") takes a shader file's path; shader source "
                     "follows its stage: embed_shader(\"frag\", source)");
        auto path = EmbeddedShaderPath(callfile, lits[0]->val);
        auto it = ast.shaders.find(path);
        if (it == ast.shaders.end()) {
            try {
                it = ast.shaders.emplace(path, CompileShaderFile(path)).first;
            } catch (CompileError &e) {
                Error(c, cat("embed_shader: ", e.msg));
            }
        }
        return &it->second;
    }
    auto stage = ShaderStageNamed(lits[0]->val);
    if (stage < 0)
        Error(args[0], cat("embed_shader: the stage is \"vert\", \"frag\" or \"comp\", not \"",
                           lits[0]->val, "\""));
    
    string source;
    vector<int> starts;
    for (size_t i = 1; i < lits.size(); i++) {
        if (i > 1) source += '\n';
        starts.push_back(1 + (int)count(source.begin(), source.end(), '\n'));
        source += lits[i]->val;
    }
    auto key = cat(callfile, "\n", lits[0]->val, "\n", source);
    auto it = ast.shaders.find(key);
    if (it != ast.shaders.end()) return &it->second;
    try {
        it = ast.shaders.emplace(key, CompileShader(source, callfile, stage)).first;
    } catch (CompileError &e) {
        int line;
        string msg;
        if (!ShaderMessageAt(e.msg, callfile, line, msg)) Error(c, cat("embed_shader: ", e.msg));
        if (!line) Error(c, cat("embed_shader: ", msg));
        auto part = upper_bound(starts.begin(), starts.end(), line) - starts.begin() - 1;
        auto lit = lits[part + 1];
        auto at = lit->line;
        if (lit->multiline) {
            auto lines = (int)count(lit->val.begin(), lit->val.end(), '\n') + 1;
            at.line += min(line - starts[part] + 1, lines);
        }
        Error(at, cat("embed_shader: ", msg,
                      named[part + 1] ? cat(" (in the shader embedded at ", Where(c->line), ")")
                                      : string()));
    }
    return &it->second;
}












inline void TypeCheck::CheckGrowShrink(Node *at, const char *op, Node *recv, const Val &rv) {
    auto id = Is<Ident>(recv);
    auto vd = id ? id->vdef : nullptr;
    auto at_type = rv.type->kind == TY_REF ? rv.type->ref->sub : rv.type;
    assert(at_type->kind == TY_ARRAY);
    Roots roots;
    if (vd) {
        
        auto viaref = vd->type && vd->type->kind == TY_REF;
        roots = viaref ? RefRootsOf(vd) : RootsOf(vd);
    } else {
        roots = rv.AsRoots();
    }
    if (!ShrinkRootsKnown(roots))
        Error(at, cat(op, " through a reference whose array is not known (§5.1)"));
    ShrinkThrough(at, op, ExprStr(recv), roots, at_type);
}






inline void TypeCheck::GrowOnlyAssignKnown(Assign *a, const Roots &built, bool whole) {
    if (!ShrinkRootsKnown(built))
        Error(a, cat("cannot assign ", whole ? "a grow-only array" : "a value holding a "
                     "grow-only array", " through a reference whose array is not known (§5.1)"));
}




inline bool TypeCheck::ShrinkMayFree(VarDef *root, TypeExpr *bound, bool growonly,
                                     TypeExpr *of, bool byteview) {
    if (byteview) return bound ? Viewable(bound) : MayBeViewed(root);
    if (bound) return !of || (growonly ? CanContain(bound, of) : GrowShrinkContains(bound, of));
    if (growonly) {
        auto st = ShrunkType(root, nullptr);
        return !of || !st || CanContain(st, of);
    }
    return GrowShrinkCanHold(root, of);
}





inline bool TypeCheck::ShrinkMayMove(VarDef *root, TypeExpr *bound, TypeExpr *t) {
    if (!t) return true;
    if (bound) return CanContain(bound, t) || CanContain(t, bound);
    return !root->type || CanContain(LoadType(root->type), t);
}




inline void TypeCheck::CheckHeldShrinks(Node *at, const string &op, VarDef *root,
                                        const string &what, bool growonly, TypeExpr *bound) {
    HeldOperands([&](const Held &h) {
        auto &[node, v, location, render, elems, loop, reread, inplace] = h;
        
        
        
        if (loop && loop->vdef && root && !root->isglobal && !IsClassRoot(root) &&
            Depth(root) >= Depth(loop->vdef))
            return;
        auto path = !inplace && v.type->kind == TY_REF &&
                    ClassOf(v.type->ref->sub) == SC_RESIZABLE;
        auto mayfree = inplace
                           ? ShrinkMayMove(root, bound, v.type->ref->sub)
                           : ShrinkMayFree(root, bound, growonly, PointeeOf(v.type), v.byteview);
        auto held = !path && mayfree &&
                    v.Any([&](const RootAlt &a) {
                        
                        
                        
                        
                        
                        
                        
                        
                        if (!growonly && !inplace && a.slotread) return false;
                        if ((growonly || inplace) && a.classread)
                            return ClassReadMayPointInto(a.root, root, bound, inplace);
                        return a.root == root || (!a.exact && Depth(a.root) >= Depth(root));
                    });
        
        
        
        
        
        
        auto reaches = v.type->kind == TY_REF
                           ? growonly ? HoldsPlainRef(v.type->ref->sub)
                                      : v.type->ref->sub->kind == TY_SLICE
                           : growonly && v.type->kind == TY_SLICE && HoldsPlainRef(v.type->sub);
        auto elemsrefer = false;
        if (!held && !location && !reread && reaches) {
            held = HeldRefsMayPointInto(nullptr, v, v.type, root, bound, growonly);
            elemsrefer = held && v.type->kind == TY_SLICE;
        }
        if (!held) return;
        auto sec = growonly ? " (§5.1)" : " (§5.2)";
        
        auto shrink = cat("cannot ", op, growonly ? cat(" ", what) : string(), ": ");
        auto expr = ExprStr(node);
        if (render) {
            if (expr.find('\n') != string::npos) expr = "its argument";
            Error(at, cat(shrink, render, " runs it in the middle of rendering ", expr,
                          ", which may refer into ", what, sec));
        }
        if (loop) {
            if (expr.find('\n') != string::npos)
                expr = reread ? "a reference or slice on the path to its sequence"
                              : "its sequence";
            Error(at, cat(shrink, "the for loop at ", Where(loop->line),
                          reread ? cat(" reads ", expr, " again on every iteration, which may lie in ")
                          : elemsrefer ? cat(" iterates ", expr, ", whose elements may refer into ")
                          : IsRefOrSlice(node->exprtype)
                              ? cat(" iterates ", expr, ", which may refer into ")
                              : cat(" iterates ", expr, " in place, which may lie in "),
                          what, sec));
        }
        if (expr.find('\n') != string::npos) expr = "the value";
        if (node->line.line != at->line.line || node->line.fileidx != at->line.fileidx)
            expr = cat(expr, " (at ", Where(node->line), ")");
        if (location)
            Error(at, cat(shrink, expr, ", which the assignment writes after it, may be in ",
                          what, sec));
        if (elems)
            Error(at, cat(shrink, "the elements of ", expr, ", which the statement reads after it, ",
                          elemsrefer ? "may refer into " : "may be in ", what, sec));
        Error(at, cat(shrink, expr, ", evaluated earlier in the statement, may still refer into ",
                      what, sec));
    });
}






inline bool TypeCheck::KillPath(Node *n, VarDef *&base, string &path) {
    if (auto id = Is<Ident>(n)) {
        auto vd = id->vdef;
        if (!vd || vd->isglobal || vd->istemp) return false;
        if (IsRefOrSlice(vd->type) && (vd->type->kind != TY_REF || vd->isvar)) return false;
        base = vd;
        path.clear();
        return true;
    }
    auto d = Is<Dot>(n);
    if (!d || !d->IsField() || d->fieldidx < 0 || !d->obj->exprtype) return false;
    auto ot = d->obj->exprtype;
    if (ot->kind == TY_REF) {
        auto id = Is<Ident>(d->obj);
        if (!id || !KillPath(id, base, path)) return false;
    } else if (ot->kind != TY_STRUCT || !KillPath(d->obj, base, path)) {
        return false;
    }
    Append(path, ".", d->name);
    return true;
}








inline void TypeCheck::NoteKill(Node *lval, const Roots &loc, TypeExpr *target, const Val &v,
                                size_t at) {
    if (!target || !HoldsPlainRef(target)) return;
    auto held = ContentsOf(v);
    if (!held.OnlyStatic() || held.unknown || !loc.Exact()) return;
    auto container = loc.Root();
    if (!container || container->isglobal || IsTemp(container)) return;
    VarDef *base = nullptr;
    string path;
    if (!KillPath(lval, base, path)) return;
    if (!IsRefOrSlice(base->type) && base != container) return;
    erase_if(cur.kills, [&](const Kill &k) {
        return k.container == container && k.base == base && k.path == path;
    });
    cur.kills.push_back({ container, base, path, at });
}








inline bool TypeCheck::KilledFor(VarDef *c, VarDef *base, TypeExpr *t, TypeExpr *arr,
                                 size_t &since) {
    since = SIZE_MAX;
    if (cur.kills.empty()) return false;
    
    
    auto whole = [&](VarDef *b) {
        if (b == c || !b->type || b->type->kind != TY_REF || !c->type || IsRefOrSlice(c->type))
            return b;
        return TypeEq(LoadType(b->type->ref->sub), LoadType(c->type)) ? c : b;
    };
    base = whole(base);
    function<bool(TypeExpr *, const string &)> emptied = [&](TypeExpr *ft, const string &path) {
        for (auto &k : cur.kills) {
            if (k.container != c) continue;
            auto kb = whole(k.base);
            if (!(kb == c && k.path.empty()) && (kb != base || k.path != path)) continue;
            since = std::min(since, k.at);
            return true;
        }
        if (!CanHoldViewInto(ft, arr)) return true;
        if (ft->kind != TY_STRUCT) return false;
        auto inst = GetStructInst(ft);
        auto &fields = inst->st->fields;
        for (size_t i = 0; i < fields.size() && i < inst->ftypes.size(); i++) {
            if (fields[i].ispad || !inst->ftypes[i]) continue;
            if (!emptied(inst->ftypes[i], cat(path, ".", fields[i].name))) return false;
        }
        return true;
    };
    return emptied(t, "") && since != SIZE_MAX;
}


inline TypeExpr *TypeCheck::KillPartType(const Kill &k) {
    auto t = k.base->type;
    if (!t) return nullptr;
    if (t->kind == TY_REF) t = t->ref->sub;
    t = LoadType(t);
    size_t at = 0;
    while (at < k.path.size()) {
        auto end = k.path.find('.', at + 1);
        if (end == string::npos) end = k.path.size();
        auto name = string_view(k.path).substr(at + 1, end - at - 1);
        at = end;
        if (t->kind != TY_STRUCT) return nullptr;
        auto inst = GetStructInst(t);
        TypeExpr *ft = nullptr;
        for (size_t i = 0; i < inst->st->fields.size() && i < inst->ftypes.size(); i++)
            if (!inst->st->fields[i].ispad && inst->st->fields[i].name == name) ft = inst->ftypes[i];
        if (!ft) return nullptr;
        t = LoadType(ft);
    }
    return t;
}









inline bool TypeCheck::CallMayWrite(FnSpec *spec, const vector<Val> &argvals, const Kill &k) {
    auto part = KillPartType(k);
    if (!part || spec->sf->isnested || spec->isfunval) return true;
    for (auto &[name, fb] : spec->fnvals) {
        if (fb.fv) return true;
        if (fb.set) {
            for (auto sf : *fb.set)
                if (sf->isnested) return true;
        } else if (fb.named && fb.named->isnested) {
            return true;
        }
    }
    auto overlaps = [&](TypeExpr *t) {
        return t && (MayHoldValueOf(t, part) || MayHoldValueOf(part, t));
    };
    for (size_t i = 0; i < spec->argtypes.size(); i++) {
        auto pt = spec->argtypes[i];
        if (pt->kind == TY_FN) return true;
        TypeExpr *reach = pt;
        if (IsRefOrSlice(pt)) {
            reach = PointeeOf(pt);
            auto mayname = i >= argvals.size() || argvals[i].Unknown() ||
                           argvals[i].Any([&](const RootAlt &a) {
                               return !a.root ||
                                      MayAliasRoots(a.root, a.exact, k.container, true) != AL_NO;
                           });
            if (mayname && overlaps(reach)) return true;
        }
        vector<TypeExpr *> reached;
        ReachedThroughRefs(reach, reached);
        for (auto t : reached)
            if (overlaps(t)) return true;
    }
    return false;
}










inline void TypeCheck::GrowOnlyShrinkAt(Node *c, const string &op, VarDef *vd,
                                        const string &what, TypeExpr *bound, TypeExpr *arr,
                                        bool viaclass) {
    struct ClassType {
        TypeCheck &tc;
        VarDef *cls;
        TypeExpr *type;
        ClassType(TypeCheck &t) : tc(t), cls(t.shrunkclass), type(t.shrunkclasstype) {}
        ~ClassType() {
            tc.shrunkclass = cls;
            tc.shrunkclasstype = type;
        }
    } classtype(*this);
    if (!bound && !vd->type && arr) {
        shrunkclass = vd;
        shrunkclasstype = arr;
    }
    if (!frames.back().spec)
        Error(c, cat("cannot ", op, " ", what, " in a global initializer (§5.1)"));
    if (vd->reusable)
        Error(c, cat("cannot ", op, " reusable pool ", what,
                     ": its slots stay live for the freelist (§5.4)"));
    
    
    
    for (auto ia : indexarrays) {
        if (ia == vd || (bound && !vd->isglobal && ia->type && TypeEq(LoadType(ia->type), bound)))
            Error(c, cat("cannot ", op, " ", what, ia == vd ? "" : cat(", which may be ", ia->name),
                         ": an index type names ", ia->name, ", so its length never goes "
                         "down (§3.11)"));
    }
    
    
    auto arrtype = ShrunkType(vd, bound);
    CheckHeldShrinks(c, op, vd, what, true, bound);
    ShrinkScanVars([&](VarDef *v) {
        
        
        if (!v->type || (v == vd && (bound || !RefsBesideTail(arrtype)))) return;
        PathsScope ps(*this, rebuilding && ExposedLater(v));
        auto t = v->type;
        if (IsRefOrSlice(t) && !v->componentowner) {
            
            
            
            
            
            
            
            
            
            auto path = t->kind == TY_REF && ClassOf(t->ref->sub) == SC_RESIZABLE;
            auto into = !path && ShrinkMayFree(vd, bound, true, PointeeOf(t), v->ref.byteview) &&
                        RefMayPointInto(v, vd, true, bound);
            
            
            
            auto held = t->kind == TY_REF ? t->ref->sub : t->sub;
            auto via = !into && HoldsPlainRef(held) &&
                       HeldRefsMayPointInto(v, v->ref, t, vd, bound, true);
            if ((!into && !via) || !UsedAfter(v)) return;
            if (via)
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                             t->kind == TY_SLICE
                                 ? held->kind == TY_SLICE
                                       ? "the slices it views may point into it"
                                       : "what it views may hold a reference or slice into it"
                             : held->kind == TY_SLICE
                                 ? "the slice it refers to may point into it"
                                 : "what it refers to may hold a reference or slice into it",
                             " (§5.1)"));
        } else {
            
            
            
            
            
            if (!HoldsPlainRef(t)) return;
            Line where;
            size_t hit = 0;
            TypeExpr *via = nullptr;
            VarDef *incoming = nullptr;
            
            
            auto from = LiveEventBase(v);
            size_t since;
            if (!pathslive && KilledFor(v, v, LoadType(t), arrtype, since))
                from = std::max(from, since);
            if (!HolderMayPointInto(v, vd, arrtype, from, &where, &hit, &via, &incoming) ||
                !UsedAfter(v))
                return;
            
            
            
            if (incoming && !CarriedEvent(hit)) {
                auto whose = incoming == v
                                 ? cat(v->name, "'s views are")
                                 : cat("it may hold ", incoming->name, "'s views, which are");
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                             whose,
                             " not of one array exactly, so they may point into it (§5.1)"));
            }
            auto stored = !via ? "a reference into it"
                          : via->kind == TY_SLICE
                              ? "a reference to a slice that may point into it"
                              : "a reference to what may hold a reference or slice into it";
            
            
            if (CarriedEvent(hit))
                Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is in scope: ",
                             stored, " is stored there at ", Where(where),
                             ", which the next iteration reaches (§5.1)"));
            Error(c, cat("cannot ", op, " ", what, " while ", v->name, " is still used: ",
                         stored, " was stored there at ", Where(where), " (§5.1)"));
        }
        auto why = v->ref.Exact() ? string() : ReadBackWhy(v->ref);
        Error(c, cat("cannot ", op, " ", what, " while ", v->name,
                     " is still used: it may hold a reference or slice into it",
                     why.empty() ? "" : "; ", why, " (§5.1)"));
    });
    
    
    
    if (viaclass) return;
    
    
    if (vd->isglobal) NoteGlobalShrink(c, cat("cannot ", op, " ", what), vd, bound);
    NoteLiveViews(c, cat("cannot ", op, " ", what), vd, what, true, bound);
    NoteShrink(vd, bound, SB_UNBALANCED);
    NoteShrinkOrder(vd, bound);
}






inline void TypeCheck::NoteLitElem(LitDeep &deep, Node *at, const Val &v, TypeExpr *t) {
    if (!t) return;
    auto isrs = IsRefOrSlice(t);
    if (!isrs && !HoldsPlainRef(t)) return;
    if (v.isnull) return;
    const Roots &roots = isrs ? v.AsRoots() : ContentsOf(v);
    auto reach = false;
    if (auto gs = StoredIntoGrowShrink(v, roots, t, !isrs, &reach))
        Error(at, NeverStoredError(gs, MayPointWording(roots, gs), reach));
    deep.roots.Add(roots);
    deep.byteview = deep.byteview || v.byteview;
}

inline void TypeCheck::HolderFromLit(Val &v, const LitDeep &deep) {
    if (!v.type || !HoldsPlainRef(v.type)) return;
    v.holderset = true;
    v.contents = deep.roots;
    v.byteview = deep.byteview;
}




inline void TypeCheck::AddStoreEvent(const StoreEvent &e) {
    e.container->storedinto = true;
    storeevents.push_back(e);
    madestores.push_back(e);
    NoteSlotStore(e);
    if (e.container->type || e.container->isglobal) return;
    auto spec = CurRealFrame().spec;
    if (!spec) return;
    
    
    auto origin = [&](size_t m) {
        if (!replay.classorigins || m < replay.classbase) return;
        auto &os = *replay.classorigins;
        auto k = m - replay.classbase;
        if (k >= os.size()) {
            os.resize(k + 1, replay.origin);
            return;
        }
        os[k] = os[k] < 0 || replay.origin < 0 ? -1 : std::min(os[k], replay.origin);
    };
    
    
    
    auto &evs = spec->record.classevents;
    for (size_t m = 0; m < evs.size(); m++) {
        auto &o = evs[m];
        if (o.container == e.container && o.root == e.root && o.src == e.src &&
            o.exact == e.exact && o.byteview == e.byteview && o.bound == e.bound &&
            o.slot == e.slot && o.sliceref == e.sliceref && !o.pointee == !e.pointee &&
            (!o.pointee || TypeEq(o.pointee, e.pointee)) &&
            !o.reached == !e.reached && (!o.reached || TypeEq(o.reached, e.reached))) {
            o.classread = o.classread && e.classread;
            origin(m);
            return;
        }
    }
    evs.push_back(e);
    origin(evs.size() - 1);
}














inline void TypeCheck::NoteSlotStore(const StoreEvent &e) {
    auto x = e.container;
    if (x->type || x->isglobal || IsTemp(x)) return;
    auto own = CurRealFrame().spec;
    set<FnSpec *> seen;
    for (auto fi = (int)frames.size() - 1; fi >= RealFrameIndex(); fi--) {
        for (auto s = frames[fi].lexspec; s && seen.insert(s).second; s = s->lexparent) {
            for (auto slot : s->classroots) {
                if (slot && slot->heldholder) {
                    
                    
                    auto h = slot->heldholder;
                    RootAlt a { e.root, e.exact };
                    if (h->ownerspec != own) a = { slot, false, nullptr, false, true };
                    AddContents(h, a, e.classread, e.src);
                    if (e.byteview && !h->contentbyteview) {
                        h->contentbyteview = true;
                        NoteFact(h);
                    }
                }
                auto h = slot ? slot->heldslice : nullptr;
                if (!h) continue;
                if (x != slot && ((e.pointee && !TopConstEq(e.pointee, h->type->sub)) ||
                                  (e.reached && !CanContain(e.reached, h->type))))
                    continue;
                RootAlt a { e.root, e.exact };
                if (h->ownerspec != own) a = { slot, false };
                auto changed = h->ref.Add(a);
                if (e.byteview && !h->ref.byteview) {
                    h->ref.byteview = true;
                    changed = true;
                }
                if (changed) NoteFact(h);
            }
        }
    }
}






inline void TypeCheck::RecordStore(VarDef *container, const Roots &roots, bool byteview,
                                    TypeExpr *pointee, VarDef *src, TypeExpr *reached,
                                    bool bound, bool slot, bool sliceref) {
    if (!container) return;
    
    
    
    
    if (src == container) {
        DropKills(container);
        return;
    }
    auto holds = container->type &&
                 (!IsRefOrSlice(container->type) || container->componentowner);
    container->contentbyteview |= byteview;
    if (roots.Unknown()) DropKills(container);
    if (holds && roots.Unknown()) container->contents.unknown = true;
    for (auto &a : roots.alts) {
        
        
        
        
        
        auto from = a.classread ? a.root : a.from;
        if (!a.exact && from == container && !bound) {
            DropKills(container);
            continue;
        }
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        
        
        
        auto copied = StoreSource(src);
        e.src = copied;
        
        
        
        if (!e.src && !a.exact) e.src = StoreSource(from);
        
        
        
        
        e.classread = IsClassRoot(e.src) && (copied || a.classread);
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.reached = reached;
        e.bound = bound;
        e.slot = slot;
        e.sliceref = sliceref;
        if (fitnode) e.at = fitnode->line;
        AddStoreEvent(e);
        if (holds) AddContents(container, a, e.classread, e.src);
    }
}







inline Roots TypeCheck::ClassArgRoots(TypeExpr *pt, const Val &v) {
    return IsRefOrSlice(pt) ? v.AsRoots() : ContentsOf(v);
}














inline void TypeCheck::ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at,
                                         vector<int> *origins) {
    auto base = storeevents.size();
    
    auto tag = [&](size_t from, int origin) {
        if (!origins) return;
        origins->resize(storeevents.size() - base, -1);
        for (auto i = std::max(from, base); i < storeevents.size(); i++)
            (*origins)[i - base] = origin;
    };
    
    
    auto classat = [&](VarDef *cr, Roots &out) -> int {
        bool view = false;
        auto p = ClassParam(spec, cr, view);
        if (p >= 0 && p < (int)argvals.size()) out = MapClass(spec, cr, argvals);
        return p;
    };
    auto paramof = [&](VarDef *cr) -> int {
        for (size_t p = 0; p < spec->params.size() && p < argvals.size(); p++)
            if (cr && spec->params[p]->ref.Root() == cr) return (int)p;
        return -1;
    };
    auto push = [&](VarDef *container, const RootAlt &a, TypeExpr *pointee, VarDef *src,
                    bool classread, bool byteview, TypeExpr *reached, bool bound, bool slot,
                    bool sliceref) {
        
        
        
        if (!src && !a.exact && a.classread && IsClassRoot(a.root)) {
            src = a.root;
            classread = true;
        }
        if (!container || (src == container && !bound)) return;
        StoreEvent e;
        e.container = container;
        e.root = a.root;
        e.src = src;
        e.classread = classread;
        e.exact = a.exact;
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.at = at->line;
        e.reached = reached;
        e.bound = bound;
        e.slot = slot;
        e.sliceref = sliceref;
        container->contentbyteview |= byteview;
        if (container->type && (!IsRefOrSlice(container->type) || container->componentowner))
            AddContents(container, a, classread, src);
        AddStoreEvent(e);
    };
    
    
    auto mapped = [&](VarDef *cr, bool exact) -> Roots {
        Roots r;
        if (classat(cr, r) < 0) {
            r.Set(cr, exact);
            return r;
        }
        return exact ? r : Bounds(r);
    };
    
    
    auto callerside = [&](VarDef *x) {
        if (!x || x->isglobal || IsClassRoot(x)) return true;
        auto named = false;
        EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
            named = named || vars[i] == x;
        });
        return named;
    };
    
    
    
    
    
    
    
    
    
    auto source = [&](VarDef *src) {
        Roots r;
        if (classat(src, r) < 0) return callerside(src) ? src : nullptr;
        return r.Exact() ? StoreSource(r.alts[0].root) : nullptr;
    };
    NoteClassUses(spec, argvals);
    
    
    auto rec = RecordOf(spec);
    if (!rec) return;
    
    
    
    
    
    
    
    
    
    
    
    
    
    auto lexical_container = [&](VarDef *x) {
        auto named = false;
        EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
            named = named || vars[i] == x;
        });
        return named;
    };
    if (!spec->inprogress) {
        Roots unused;
        auto ofcallee = [&](const RootAlt &a) { return classat(a.root, unused) >= 0; };
        EachNamedVar((int)frames.size() - 1, CurRealFrame().spec, [&](int i) {
            auto x = vars[i];
            if (!x->type || !IsRefOrSlice(x->type) || x->isglobal || !x->ref.Any(ofcallee))
                return;
            spec->storesout = true;
            Roots bound;
            for (auto &a : x->ref.alts)
                if (ofcallee(a)) bound.Add(RetAltVal(spec, a, argvals, x->type, at));
            erase_if(x->ref.alts, ofcallee);
            AddBinding(x, bound);
        });
    }
    auto end = min(rec->eventend, storeevents.size());
    for (auto i = rec->eventstart; i < end; i++) {
        
        
        auto e = storeevents[i];
        if (auto k = i - rec->eventstart; k < rec->bodyevents.size()) {
            auto &b = rec->bodyevents[k];
            e.container = b.container;
            e.root = b.root;
            e.src = b.src;
            e.exact = b.exact;
            e.classread = b.classread;
        }
        
        
        
        
        if (spec->inprogress && e.container && e.container->ownerspec == spec) continue;
        auto r = mapped(e.root, e.exact);
        auto src = source(e.src);
        e.classread = e.classread && IsClassRoot(src);
        Roots own;
        auto x = e.container;
        if (!spec->inprogress && classat(e.root, own) >= 0 && x && x->type &&
            !IsRefOrSlice(x->type) && !x->isglobal && !IsTemp(x) && lexical_container(x)) {
            spec->storesout = true;
            erase_if(x->contents.alts, [&](const RootAlt &c) { return c.root == e.root; });
            for (auto &a : r.alts) AddContents(x, a, e.classread, src);
        }
        for (size_t k = 0; k < r.alts.size(); k++) {
            auto &a = r.alts[k];
            e.root = a.root;
            e.exact = a.exact;
            e.src = src;
            if (k == 0) storeevents[i] = e;
            else storeevents.push_back(e);
        }
    }
    
    
    
    
    
    auto slotval = [&](const StoreEvent &e) {
        Val v;
        v.type = e.reached;
        v.byteview = e.byteview;
        auto q = paramof(e.root);
        auto qt = q >= 0 ? spec->argtypes[(size_t)q] : nullptr;
        if (qt && qt->kind == TY_REF && qt->ref->sub->kind == TY_SLICE && !e.exact &&
            !IsRefOrSlice(qt->ref->sub->sub) && !HoldsPlainRef(qt->ref->sub->sub)) {
            auto held = SlotView(argvals[(size_t)q], qt->ref->sub);
            v.TakeAlts(held);
            v.byteview = v.byteview || held.byteview;
            v.freshview = held.freshview;
        } else {
            v.TakeAlts(mapped(e.root, e.exact));
        }
        return v;
    };
    struct SlotStore {
        VarDef *root;
        bool bound;
        StoreEvent e;
        string via;
    };
    vector<SlotStore> slotstores;
    
    
    
    
    
    auto mappedheld = [&](VarDef *cr) -> Roots {
        Roots r, out;
        classat(cr, r);
        out.unknown = r.unknown;
        for (auto &a : r.alts) {
            Roots one;
            one.alts.push_back(a);
            if (a.exact && IsClassRoot(a.root)) {
                out.Add({ a.root, false, nullptr, a.slotread, true });
                continue;
            }
            out.Add(a.exact ? HeldAt(one) : Bounds(one));
        }
        return out;
    };
    
    
    struct Made {
        const StoreEvent *e;
        VarDef *target;
        VarDef *root;
    };
    vector<Made> made;
    
    
    
    auto applyevent = [&](const StoreEvent &e, const Roots &mappedroots, bool first,
                          const StoreEvent *key) {
        
        
        
        
        Roots r = mappedroots;
        if (e.pointee && !e.byteview)
            erase_if(r.alts, [&](const RootAlt &a) {
                return a.exact && a.root && a.root->type &&
                       !MayHoldValueOf(LoadType(a.root->type), e.pointee);
            });
        Roots cr;
        auto p = classat(e.container, cr);
        auto src = source(e.src);
        auto classread = e.classread && IsClassRoot(src);
        auto once = [&](VarDef *target, VarDef *root) {
            for (auto &m : made)
                if (m.e == key && m.target == target && m.root == root) return false;
            made.push_back({ key, target, root });
            return true;
        };
        if (p < 0) {
            if (e.container && e.container->type && !e.container->isglobal &&
                !lexical_container(e.container)) return;
            
            
            
            for (auto &a : r.alts)
                if (once(e.container, a.root))
                    push(e.container, a, e.pointee, src, classread, e.byteview, e.reached,
                         e.bound, e.slot, e.sliceref);
            return;
        }
        
        
        
        auto widen = e.bound || !cr.Exact();
        if (e.bound) cr.Weaken();
        
        
        
        
        
        auto cyclecall = spec->inprogress || spec->incycle;
        auto cyclelocal = [](VarDef *x) {
            auto o = x ? x->ownerspec : nullptr;
            return o && (o->incycle || (o->sf && o->sf->isrec));
        };
        auto pname = spec->sf->params[(size_t)p].name;
        for (auto &t : ShrinkTargets(cr, e.reached, e.slot)) {
            for (auto &a : r.alts) {
                if ((widen || (cyclecall && !cyclelocal(a.root))) &&
                    Depth(a.root) > Depth(t.root)) {
                    auto rname = a.root ? a.root->name : string_view("static data");
                    Error(at, cat("call ", spec->sf->name, " stores a reference rooted at ",
                                  rname,
                                  e.bound ? cat(" into what its parameter ", pname,
                                                " leads to, which may be ")
                                          : cat(" through its parameter ", pname,
                                                ", whose argument ",
                                                cr.Exact() ? "points" : "may point", " into "),
                                  TargetStr(t), ", which ", rname, " does not outlive (§9.2)"));
                }
                if (once(t.root, a.root))
                    push(t.root, a, e.pointee, src, classread, e.byteview, e.reached, t.bound,
                         e.slot, e.sliceref);
            }
            if (e.slot && first)
                slotstores.push_back({ t.root, t.bound, e,
                                       cat(" through ", spec->sf->name, "'s parameter ", pname,
                                           widen ? ", which may name it" : "") });
        }
    };
    
    
    
    
    
    
    
    
    
    struct Leaf {
        RootAlt a;
        VarDef *src;
        bool classread;
    };
    map<VarDef *, vector<size_t>> bodystores;
    map<VarDef *, vector<Leaf>> leaves;
    deque<vector<Leaf>> partial;
    set<VarDef *> failed, onpath;
    static const vector<Leaf> none;
    auto cycled = false;
    std::function<const vector<Leaf> *(VarDef *)> holds = [&](VarDef *x) -> const vector<Leaf> * {
        if (onpath.count(x)) {
            cycled = true;
            return &none;
        }
        if (!x || !x->type || IsRefOrSlice(x->type) || callerside(x) || failed.count(x))
            return nullptr;
        if (auto it = leaves.find(x); it != leaves.end()) return &it->second;
        vector<Leaf> out;
        set<tuple<VarDef *, VarDef *, VarDef *, int>> seen;
        auto add = [&](const Leaf &l) {
            if (seen.insert({ l.a.root, l.a.from, l.src,
                              (int)l.a.exact | l.a.slotread << 1 | l.a.classread << 2 |
                                  l.classread << 3 })
                    .second)
                out.push_back(l);
        };
        auto ok = true;
        auto p = find(spec->params.begin(), spec->params.end(), x) - spec->params.begin();
        if (p < (ptrdiff_t)min(spec->params.size(), argvals.size())) {
            auto &v = argvals[(size_t)p];
            ok = !x->storedinto && !ContentsOf(v).unknown;
            auto copied = StoreSource(v.holderfrom);
            for (auto &h : ContentsOf(v).alts) {
                auto from = h.classread ? h.root : h.from;
                auto src = h.exact ? nullptr : from ? StoreSource(from) : copied;
                add({ h, src, IsClassRoot(src) && (h.classread || src == copied) });
            }
        } else {
            if (bodystores.empty())
                for (size_t k = 0; k < rec->bodyevents.size(); k++)
                    bodystores[rec->bodyevents[k].container].push_back(k);
            onpath.insert(x);
            auto outer = cycled;
            cycled = false;
            for (auto k : bodystores[x]) {
                auto &b = rec->bodyevents[k];
                if (b.src && !callerside(b.src)) {
                    auto l = holds(b.src);
                    if (!l) {
                        ok = false;
                        break;
                    }
                    for (auto &f : *l) add(f);
                    continue;
                }
                auto src = source(b.src);
                for (auto &m : mapped(b.root, b.exact).alts) {
                    ok = ok && callerside(m.root);
                    add({ m, src, b.classread && IsClassRoot(src) });
                }
            }
            onpath.erase(x);
            auto incomplete = cycled && !onpath.empty();
            cycled = outer || incomplete;
            if (ok && incomplete) return &partial.emplace_back(std::move(out));
        }
        if (!ok) {
            failed.insert(x);
            return nullptr;
        }
        return &(leaves[x] = std::move(out));
    };
    auto applystore = [&](const StoreEvent &e, const Roots &mappedroots, bool first) {
        auto ls = spec->inprogress ? nullptr : holds(e.src);
        if (!ls) {
            applyevent(e, mappedroots, first, &e);
            return;
        }
        for (auto &l : *ls) {
            auto c = e;
            c.root = l.a.root;
            c.exact = l.a.exact;
            c.src = l.src;
            c.classread = l.classread;
            Roots one;
            one.alts.push_back(l.a);
            applyevent(c, one, first, &e);
        }
    };
    
    
    
    
    vector<pair<const StoreEvent *, Roots>> held;
    for (auto &e : rec->classevents) {
        if (e.src == e.container && !e.bound) continue;
        Roots unused;
        if (e.classread && e.src == e.root && !e.exact && classat(e.root, unused) >= 0) {
            held.push_back({ &e, Roots {} });
            continue;
        }
        auto from = storeevents.size();
        replay.origin = (int)(&e - rec->classevents.data());
        applystore(e, mapped(e.root, e.exact), true);
        replay.origin = -1;
        tag(from, (int)(&e - rec->classevents.data()));
    }
    for (auto again = !held.empty(), first = true; again; first = false) {
        again = false;
        for (auto &[e, last] : held) {
            auto r = mappedheld(e->root);
            if (!first && r.Same(last)) continue;
            last = r;
            again = true;
            auto from = storeevents.size();
            replay.origin = (int)(e - rec->classevents.data());
            applystore(*e, r, first);
            replay.origin = -1;
            tag(from, (int)(e - rec->classevents.data()));
        }
    }
    
    
    
    for (auto again = !slotstores.empty(); again;) {
        again = false;
        for (auto &s : slotstores)
            again = StoreIntoSlot(at, s.root, s.bound, s.e.reached, slotval(s.e), s.via) || again;
    }
    tag(storeevents.size(), -1);
}





inline void TypeCheck::NoteClassUses(FnSpec *spec, const vector<Val> &argvals) {
    for (size_t p = 0; p < spec->params.size() && p < argvals.size() &&
                       p < spec->argtypes.size(); p++) {
        if (auto vr = ViewClassOf(spec, p)) {
            ClassUse u;
            u.roots = argvals[p].held;
            u.byteview = argvals[p].held.byteview;
            NoteClassUse(vr, u);
        }
        auto cr = spec->params[p]->ref.Root();
        if (!IsClassRoot(cr)) continue;
        auto pt = spec->argtypes[p];
        ClassUse u;
        u.roots = ClassArgRoots(pt, argvals[p]);
        if (!IsRefOrSlice(pt) && !IsTemp(argvals[p].holderfrom)) u.src = argvals[p].holderfrom;
        u.byteview = argvals[p].byteview;
        NoteClassUse(cr, u);
        for (auto c : cr->components) {
            ClassUse projected;
            projected.roots = ProjectRoots(u.roots, c->componentbase, c->componentfield);
            NoteClassUse(c, projected);
        }
    }
}

inline void TypeCheck::NoteClassUse(VarDef *cr, const ClassUse &u) {
    auto same = [&](const ClassUse &o) {
        return o.src == u.src && o.byteview == u.byteview &&
               o.roots.Same(u.roots, Roots::Compare::GlobalReach);
    };
    auto &uses = classuses[cr];
    if (none_of(uses.begin(), uses.end(), same)) uses.push_back(u);
}



inline void TypeCheck::NoteGlobalBinding(VarDef *gd, const Roots &roots, bool byteview,
                                         TypeExpr *pointee, Line at) {
    for (auto &a : roots.alts) {
        StoreEvent e;
        e.container = gd;
        e.root = a.root;
        e.exact = a.exact;
        if (!a.exact && a.from != gd) e.src = StoreSource(a.from);
        e.pointee = byteview ? nullptr : pointee;
        e.byteview = byteview;
        e.at = at;
        madestores.push_back(e);
    }
}



inline void TypeCheck::ThreadIndexError(Node *at, TypeExpr *t) {
    Error(at, cat(TypeStr(t), " cannot pass between threads: it holds an index type, and each "
                  "thread program's copy of the array it names grows on its own (§3.11)"));
}




inline void TypeCheck::NoteGlobalShrink(Node *at, const string &prefix, VarDef *vd,
                                        TypeExpr *bound) {
    for (auto &gs : globalshrinks)
        if (gs.arr == vd && !gs.bound == !bound && (!bound || TypeEq(gs.bound, bound))) return;
    globalshrinks.push_back({ vd, bound, at->line, prefix, Chain({}) });
}







inline void TypeCheck::CheckGlobalShrinks() {
    if (globalshrinks.empty()) return;
    map<VarDef *, vector<size_t>> bycontainer;
    for (size_t i = 0; i < madestores.size(); i++)
        bycontainer[madestores[i].container].push_back(i);
    for (auto &gs : globalshrinks) CheckItem([&] {
        vector<VarDef *> arrays;
        if (!gs.bound) arrays.push_back(gs.arr);
        else {
            auto consider = [&](VarDef *gd) {
                if (gd && gd->type && !IsRefOrSlice(gd->type) &&
                    CanContain(LoadType(gd->type), gs.bound))
                    arrays.push_back(gd);
            };
            for (auto g : ast.globals)
                for (auto gd : g->defs) {
                    consider(gd);
                    for (auto c : gd->components) consider(c);
                }
        }
        for (auto arr : arrays) {
            auto arrtype = gs.bound ? gs.bound : LoadType(arr->type);
            for (auto g : ast.globals) {
                for (auto gd : g->defs) {
                    if (gd == arr || !gd->type || !HoldsPlainRef(gd->type)) continue;
                    
                    
                    vector<TypeExpr *> ps;
                    RefPointees(gd->type, ps);
                    auto fits = false;
                    for (auto pt : ps)
                        fits = fits || (IsU8(pt) && Viewable(arrtype)) || CanContain(arrtype, pt);
                    if (!fits) continue;
                    auto into = cat(gs.prefix, ": global ", gd->name,
                                    " may hold a reference into ",
                                    gs.bound ? string(arr->name) : string("it"));
                    GlobalReach reach { *this, arr, arrtype, bycontainer, {}, {} };
                    Line where;
                    if (!reach.Holds(gd, &where)) continue;
                    ErrorIn(LocOf(gs.at), cat(into, ", stored at ", Where(where), " (§5.1)"),
                            gs.chain);
                }
            }
        }
    });
}



inline bool TypeCheck::GlobalReach::Fits(TypeExpr *pointee, bool byteview) {
    if (byteview) return tc.Viewable(arrtype);
    return !pointee || tc.CanContain(arrtype, pointee);
}






inline bool TypeCheck::GlobalReach::Root(VarDef *r, bool exact, VarDef *from, TypeExpr *pointee,
                                         bool byteview) {
    if (tc.IsClassRoot(r)) return Class(r, exact, pointee, byteview);
    if (exact) return r == arr;
    if (from && !IsTemp(from)) return Holds(from);
    return Fits(pointee, byteview);
}





inline bool TypeCheck::GlobalReach::Class(VarDef *cr, bool exact, TypeExpr *pointee,
                                          bool byteview) {
    if (!rooted.insert({ cr, exact }).second) return false;
    auto it = tc.classuses.find(cr);
    if (it == tc.classuses.end()) return false;
    for (auto &u : it->second) {
        if (u.src && Holds(u.src)) return true;
        for (auto &a : u.roots.alts) {
            if (u.src && Covered(u.src, a.root, a.exact)) continue;
            if (Root(a.root, a.exact && exact, a.from, pointee, byteview || u.byteview))
                return true;
        }
    }
    return false;
}






inline bool TypeCheck::GlobalReach::Holds(VarDef *x, Line *where) {
    if (!x || !held.insert(x).second) return false;
    if (x->type && tc.ClassOf(x->type) == SC_ROOT)
        for (auto c : x->components) if (c && Holds(c, where)) return true;
    if (tc.IsClassRoot(x)) {
        auto it = tc.classuses.find(x);
        if (it == tc.classuses.end()) return false;
        for (auto &u : it->second)
            for (auto &a : u.roots.alts)
                if (!a.exact || (a.root && Holds(a.root))) return true;
        return false;
    }
    if (auto it = bycontainer.find(x); it != bycontainer.end()) {
        for (auto i : it->second) {
            auto &e = tc.madestores[i];
            if (!Event(e)) continue;
            if (where) *where = e.at;
            return true;
        }
    }
    if (x->type && IsRefOrSlice(x->type) && x->refrootknown) {
        for (auto &a : x->ref.alts) {
            if (!Root(a.root, a.exact, a.from, tc.PointeeOf(x->type), x->ref.byteview)) continue;
            if (where) *where = x->line;
            return true;
        }
    }
    return false;
}



inline bool TypeCheck::GlobalReach::Event(const StoreEvent &e) {
    if (e.src) {
        if (Holds(e.src)) return true;
        if (Covered(e.src, e.root, e.exact)) return false;
    }
    return Root(e.root, e.exact, nullptr, e.pointee, e.byteview);
}





inline bool TypeCheck::GlobalReach::Covered(VarDef *src, VarDef *root, bool exact) {
    if (!exact && (root == src || (!root && src->isglobal))) return true;
    for (auto &c : src->contents.alts)
        if (c.root == root && (exact || !c.exact)) return true;
    return false;
}










inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, size_t *hit,
                                          TypeExpr **via, VarDef **incoming) {
    set<VarDef *> seen;
    return HolderMayPointInto(holder, arr, arrtype, from, where, seen, hit, via, incoming);
}

inline bool TypeCheck::HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype,
                                          size_t from, Line *where, set<VarDef *> &seen,
                                          size_t *hitat, TypeExpr **via, VarDef **incoming) {
    if (!seen.insert(holder).second) return false;
    if (holder->type && ClassOf(holder->type) == SC_ROOT) {
        for (auto c : holder->components) {
            if (!c) continue;
            auto start = from;
            size_t since;
            if (!pathslive && KilledFor(c, c, LoadType(c->type), arrtype, since))
                start = std::max(start, since);
            if (HolderMayPointInto(c, arr, arrtype, start, where, seen, hitat, via, incoming))
                return true;
        }
    }
    
    
    if (!pathslive && holder->type && !IsRefOrSlice(holder->type) &&
        !CanHoldViewInto(LoadType(holder->type), arrtype))
        return false;
    auto contains = [&](TypeExpr *pt) { return !arrtype || CanContain(arrtype, pt); };
    auto found = [&](size_t i, TypeExpr *through) {
        auto &e = storeevents[i];
        if (!e.src || through) *where = e.at;
        if (hitat) *hitat = i;
        if (via && through) *via = through;
        if (incoming && e.incoming && !through) *incoming = holder;
        return true;
    };
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder || StoredLater(i)) continue;
        auto hit = false;
        if (e.src && e.src->isglobal) {
            
            
            
            vector<TypeExpr *> ps;
            if (e.src->type) RefPointees(e.src->type, ps);
            for (auto pt : ps)
                hit |= (IsU8(pt) && (!arrtype || Viewable(arrtype))) || contains(pt);
        } else if (e.src && !e.src->type && e.classread) {
            
            
            
            hit = HolderMayPointInto(e.src, arr, arrtype, 0, where, seen, nullptr, via,
                                     incoming);
        } else if (e.src && !e.src->type) {
            
            
            
            hit = WithinDepth(arr, e.src) && (!e.pointee || contains(e.pointee));
            if (hit) *where = e.at;
        } else if (e.src) {
            
            
            hit = HolderMayPointInto(e.src, arr, arrtype, 0, where, seen, nullptr, via,
                                     incoming);
        } else if (e.exact) {
            hit = e.root == arr;
        } else if (e.root) {
            hit = WithinDepth(arr, e.root) && (!e.pointee || contains(e.pointee));
        }
        if (hit) return found(i, nullptr);
    }
    
    
    
    
    
    
    
    
    for (auto i = from; i < storeevents.size(); i++) {
        auto &e = storeevents[i];
        if (e.container != holder || (e.src && (e.src->type || e.src->isglobal)) ||
            StoredLater(i))
            continue;
        auto through = e.src ? StoredSlotMayPointInto(holder, e.pointee, e.sliceref, e.src,
                                                      e.classread, arr, arrtype, where, seen)
                             : StoredSlotMayPointInto(holder, e.pointee, e.sliceref, e.root,
                                                      e.exact, arr, arrtype, where, seen);
        if (through) return found(i, through);
    }
    return false;
}



















inline TypeExpr *TypeCheck::StoredSlotMayPointInto(VarDef *holder, TypeExpr *pointee,
                                                   bool sliceref, VarDef *r, bool exact,
                                                   VarDef *arr, TypeExpr *arrtype, Line *where,
                                                   set<VarDef *> &seen) {
    if (!r || r->isglobal) return nullptr;
    vector<pair<TypeExpr *, bool>> slots;
    if (pointee) slots.push_back({ pointee, sliceref });
    else if (holder->type) RefSlots(holder->type, slots);
    auto byteview = r->contentbyteview || (r->type && IsRefOrSlice(r->type) && r->ref.byteview);
    auto reaches = [&](TypeExpr *st) {
        if (!HoldsPlainRef(st)) return false;
        vector<TypeExpr *> ps;
        ReachedThroughRefs(st, ps);
        for (auto pt : ps)
            if (!arrtype || CanContain(arrtype, pt) ||
                (byteview && IsU8(pt) && Viewable(arrtype)))
                return true;
        return false;
    };
    
    
    TypeExpr *via = nullptr, *slicevia = nullptr, *contentsvia = nullptr;
    for (auto [st, slice] : slots) {
        auto &kind = slice ? slicevia : contentsvia;
        if (kind || !reaches(st)) continue;
        kind = st;
        if (!via) via = st;
    }
    if (!via) return nullptr;
    if (exact && r->type && !IsRefOrSlice(r->type))
        return HolderMayPointInto(r, arr, arrtype, 0, where, seen, nullptr) ? via : nullptr;
    if (exact && IsClassRoot(r)) {
        if (slicevia && WithinDepth(arr, r)) return slicevia;
        if (contentsvia && HolderMayPointInto(r, arr, arrtype, 0, where, seen, nullptr))
            return contentsvia;
        return nullptr;
    }
    return WithinDepth(arr, r) ? via : nullptr;
}




inline void TypeCheck::RefPointees(TypeExpr *t, vector<TypeExpr *> &out) {
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage < 0) out.push_back(LoadType(t->ref->sub));
            return;
        case TY_SLICE: out.push_back(t->sub); return;
        case TY_ARRAY: RefPointees(t->arr->sub, out); return;
        default: EachField(t, [&](TypeExpr *ft) { RefPointees(ft, out); }); return;
    }
}





inline void TypeCheck::RefSlots(TypeExpr *t, vector<pair<TypeExpr *, bool>> &out) {
    switch (t->kind) {
        case TY_REF:
            if (t->ref->lenstorage < 0) {
                auto st = LoadType(t->ref->sub);
                out.push_back({ st, st->kind == TY_SLICE });
            }
            return;
        case TY_SLICE: out.push_back({ t->sub, false }); return;
        case TY_ARRAY: RefSlots(t->arr->sub, out); return;
        default: EachField(t, [&](TypeExpr *ft) { RefSlots(ft, out); }); return;
    }
}




inline bool TypeCheck::RefsBesideTail(TypeExpr *t) {
    if (t->kind == TY_ARRAY) return false;
    return AnyField(t, [&](TypeExpr *ft) {
        return ResizableArrayIn(ft) ? RefsBesideTail(ft) : HoldsPlainRef(ft);
    });
}







inline bool TypeCheck::CanHoldViewInto(TypeExpr *t, TypeExpr *arr) {
    vector<TypeExpr *> ps;
    ReachedThroughRefs(t, ps);
    for (auto pt : ps) {
        if (ClassOf(pt) == SC_RESIZABLE) continue;
        if (!arr || CanContain(arr, pt) || (IsU8(pt) && Viewable(arr))) return true;
    }
    return false;
}



inline bool TypeCheck::GrowOnlyTail(TypeExpr *t) {
    auto arr = ResizableArrayIn(t);
    return arr && arr->arr->akind == A_GROW;
}



inline bool TypeCheck::IsGrowOnlyRootVar(VarDef *r) {
    auto v = r;
    while (v && !v->type && v->classfrom) v = v->classfrom;
    return v && v->type && GrowOnlyTail(LoadType(v->type));
}





inline void TypeCheck::CheckShrinkHolders(Node *at, const string &op, VarDef *root,
                                          const string &what, TypeExpr *bound) {
    CheckHeldShrinks(at, op, root, what, false, bound);
    ShrinkScanVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        if (!IsRefOrSlice(v->type)) return;
        
        
        if (v->type->kind == TY_REF && ContainsGrowShrink(v->type->ref->sub)) return;
        
        
        
        
        
        
        
        
        auto into = ShrinkMayFree(root, bound, false, PointeeOf(v->type), v->ref.byteview) &&
                    (v->ref.Any([&](const RootAlt &a) {
                         return !DifferentComponents(a.root, a.exact || a.projected, root, !bound) &&
                                !a.slotread && (a.root == root ||
                                                (!a.exact && Depth(a.root) >= Depth(root)));
                     }) ||
                     RefMayRetarget(v, root));
        
        
        
        auto via = !into && v->type->kind == TY_REF && v->type->ref->sub->kind == TY_SLICE &&
                   HeldRefsMayPointInto(v, v->ref, v->type, root, bound, false);
        if ((!into && !via) || !UsedAfter(v)) return;
        Error(at, cat("cannot ", op, " while ", v->name, " (bound at ", Where(v->line),
                      ") is still used: ", via ? "the slice it refers to may point into "
                                               : "it may refer into ",
                      what, " (§5.2)"));
    });
}

template <typename P, typename X>
inline void TypeCheck::NoteRootEvent(VarDef *root, P param, X external) {
    auto current = CurRealFrame().spec;
    if (!current) return;
    
    
    if (root->componentowner && IsClassRoot(root)) {
        external(current, root);
        return;
    }
    if (root->type) {
        if (root->isglobal || root->ownerspec != current) external(current, root);
        return;
    }
    for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
        auto spec = frames[fi].spec;
        if (!spec) continue;
        auto found = false;
        
        
        for (size_t i = 0; i < spec->params.size(); i++) {
            if (RefRootOf(spec->params[i]) == root) {
                param(spec, (int)i);
                found = true;
            }
            if (ViewClassOf(spec, i) == root) {
                param(spec, (int)(spec->params.size() + i));
                found = true;
            }
        }
        if (found) {
            if (spec != current) external(current, root);
            return;
        }
    }
}




inline void TypeCheck::NoteShrink(VarDef *root, TypeExpr *bound, ShrinkBalance balance) {
    auto note = [&](auto &bounds, auto key) {
        for (auto &b : bounds)
            if (b.key == key && TypeEq(b.type, bound)) {
                b.balance = std::max(b.balance, balance);
                return;
            }
        bounds.push_back({ key, bound, balance });
    };
    auto mark = [&](auto &entries, auto key) {
        auto [it, fresh] = entries.try_emplace(key, balance);
        if (!fresh) it->second = std::max(it->second, balance);
    };
    NoteRootEvent(root,
                  [&](FnSpec *s, int i) {
                      if (bound) note(s->record.shrinkparambounds, i);
                      else mark(s->record.shrinkparams, i);
                  },
                  [&](FnSpec *s, VarDef *r) {
                      if (bound) note(s->record.shrinkexternalbounds, r);
                      else mark(s->record.shrinkexternals, r);
                  });
}






inline void TypeCheck::NoteShrinkOrder(VarDef *root, TypeExpr *bound) {
    auto current = CurRealFrame().spec;
    auto note = [&](FnSpec *s, int param, VarDef *external) {
        auto known = s == current && !s->isfunval && !s->incycle && !s->sf->isrec;
        FnRecord::ShrinkOrder o;
        o.param = param;
        o.external = external;
        o.bound = bound;
        o.before = known ? s->record.classevents.size() : SIZE_MAX;
        
        
        
        if (known && callorder.classorigins) {
            auto &os = *callorder.classorigins;
            auto b = callorder.classbase;
            for (size_t k = 0; k < os.size(); k++)
                if (os[k] < 0 || (size_t)os[k] < callorder.before)
                    b = std::max(b, callorder.classbase + k + 1);
            if (o.before <= callorder.classbase + os.size()) o.before = std::min(o.before, b);
        }
        for (size_t p = 0; known && p < s->params.size() && p < s->argtypes.size(); p++) {
            if (!IsPlainRef(s->argtypes[p])) continue;
            for (auto &k : cur.kills)
                if (k.base == s->params[p] && k.container == RefRootOf(s->params[p]))
                    o.kills.push_back({ (int)p, k.path });
        }
        for (auto &e : s->record.shrinkorders) {
            if (e.param != param || e.external != external || !e.bound != !bound ||
                (bound && !TypeEq(e.bound, bound)))
                continue;
            e.before = std::max(e.before, o.before);
            erase_if(e.kills, [&](const pair<int, string> &k) {
                return find(o.kills.begin(), o.kills.end(), k) == o.kills.end();
            });
            return;
        }
        s->record.shrinkorders.push_back(o);
    };
    NoteRootEvent(root, [&](FnSpec *s, int i) { note(s, i, nullptr); },
                  [&](FnSpec *s, VarDef *r) { note(s, -1, r); });
}

inline void TypeCheck::ShrinkGrowShrink(Node *at, const string &op, VarDef *root,
                                        const string &what, TypeExpr *bound,
                                        ShrinkBalance balance) {
    if (!root) return;
    CheckShrinkHolders(at, op, root, what, bound);
    NoteLiveViews(at, cat("cannot ", op), root, what, false, bound);
    NoteShrink(root, bound, balance);
}









inline bool TypeCheck::ResizesToMark(Node *recv, Node *len) {
    auto id = Is<Ident>(len);
    auto m = id ? LookupVar(id->name, id->ns) : nullptr;
    auto spec = CurRealFrame().spec;
    if (!m || !m->markof || m->isvar || !spec || m->ownerspec != spec || m->refwrite ||
        !SamePath(m->markof, recv))
        return false;
    if (!m->markuse) m->markuse = len;
    return true;
}





inline bool TypeCheck::SamePath(Node *a, Node *b) {
    auto da = Is<Dot>(a), db = Is<Dot>(b);
    if (da || db) {
        auto field = [](Dot *d) {
            return d && d->fieldidx >= 0 && d->exprtype && !IsRefOrSlice(d->exprtype);
        };
        return field(da) && field(db) && da->fieldidx == db->fieldidx && da->name == db->name &&
               SamePath(da->obj, db->obj);
    }
    auto ia = Is<Ident>(a), ib = Is<Ident>(b);
    if (!ia || !ib || !ia->vdef || ia->vdef != ib->vdef) return false;
    auto v = ia->vdef;
    return v->type && !(v->isvar && IsRefOrSlice(v->type));
}
















inline vector<TypeCheck::ShrinkTarget> TypeCheck::ShrinkTargets(const Roots &roots,
                                                                 TypeExpr *arr, bool slots,
                                                                 bool classbound) {
    vector<ShrinkTarget> out;
    auto add = [&](VarDef *r, bool bound, bool viaclass) {
        for (auto &t : out)
            if (t.root == r) {
                t.bound = t.bound && bound;
                t.viaclass = t.viaclass && viaclass;
                return;
            }
        out.push_back({ r, bound, viaclass });
    };
    auto own = classbound ? CurRealFrame().spec : nullptr;
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (a.exact) {
            if (root) add(root, false, false);
            continue;
        }
        auto behind = own && IsClassRoot(root) && (!a.from || a.from == root) &&
                      find(own->classroots.begin(), own->classroots.end(), root) !=
                          own->classroots.end();
        auto cands = RootCandidates(arr, Depth(root), false, true, slots);
        if (root) {
            auto isbound = false;
            if (!IsTemp(root)) {
                isbound = true;
                for (auto &c : cands.alts) if (c.root == root && c.exact) isbound = false;
            }
            add(root, isbound, false);
        }
        for (auto &c : cands.alts)
            if (c.root && c.root != root) add(c.root, !c.exact, behind);
    }
    return out;
}



inline string TypeCheck::TargetStr(const ShrinkTarget &t) {
    if (!t.bound) return string(t.root->name);
    if (!t.root->type) return cat("the caller's storage behind ", t.root->name);
    return cat("what ", t.root->name, "'s references lead to");
}





inline void TypeCheck::ShrinkThrough(Node *at, const string &verb, const string &recv,
                                     const Roots &roots, TypeExpr *arr, ShrinkBalance balance) {
    auto root = roots.Root();
    auto growonly = GrowOnlyTail(arr);
    for (auto &t : ShrinkTargets(roots, arr, false, growonly)) {
        auto bound = t.bound ? arr : nullptr;
        if (growonly) {
            auto what = t.root == root ? string(t.root->name)
                                       : cat(t.root->name, " (which ", recv, " may point at)");
            GrowOnlyShrinkAt(at, verb, t.root, what, bound, arr, t.viaclass);
        } else {
            auto what = t.root == root ? recv : cat(t.root->name, ", which ", recv,
                                                    " may point at");
            ShrinkGrowShrink(at, cat(verb, " ", recv), t.root, what, bound, balance);
        }
        NoteShrinkEvent(at, t.root, bound, cat(verb, " ", recv), arr);
    }
}




inline bool TypeCheck::CallersJudge(VarDef *r, VarDef *root) {
    return r && r != root && !IsTemp(r) && (IsClassRoot(r) || IsClassRoot(root));
}








inline void TypeCheck::NoteLiveViews(Node *at, const string &prefix, VarDef *root,
                                     const string &what, bool growonly, TypeExpr *bound) {
    auto current = CurRealFrame().spec;
    
    
    
    if (!current || (root->type && !root->isglobal && root->ownerspec == current)) return;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    auto arrtype = ShrunkType(root, bound);
    auto views = [&](const Prov &p, TypeExpr *t, bool slotonly, bool isvar, bool inplace,
                     VarDef *base) {
        vector<LiveView> out;
        if (inplace || t->kind != TY_REF || ClassOf(t->ref->sub) != SC_RESIZABLE) {
            auto own = p;
            if ((growonly || inplace) && !isvar) {
                Prov held = p;
                erase_if(held.alts, [](const RootAlt &a) { return !a.classread; });
                erase_if(own.alts, [](const RootAlt &a) { return a.classread; });
                auto first = out.size();
                HeldViews(held, t, false, out);
                for (auto i = first; i < out.size(); i++) out[i].inplace = inplace;
            }
            out.push_back({ own, PointeeOf(t), false, false, inplace });
        }
        if (slotonly || inplace) return out;
        if (t->kind == TY_SLICE) {
            if (growonly && HoldsPlainRef(t->sub)) HeldViews(p, t->sub, isvar, out);
            return out;
        }
        if (t->kind != TY_REF) return out;
        auto sub = t->ref->sub;
        auto r = p.Root();
        if (sub->kind == TY_SLICE) {
            auto sv = p;
            if (r && p.Exact() && r->type && IsRefOrSlice(r->type)) {
                sv = r->ref;
                if (!r->refrootknown) {
                    if (UnboundIsBottom()) sv.Clear();
                    else sv.Set(temproot, false);
                }
            } else {
                sv = SlotView(p, sub);
            }
            out.push_back({ sv, sub->sub, true });
            if (growonly && HoldsPlainRef(sub->sub)) HeldViews(sv, sub->sub, isvar, out);
        } else if (growonly && HoldsPlainRef(sub)) {
            HeldViews(p, sub, isvar, out, nullptr, base, arrtype);
        }
        return out;
    };
    
    
    
    
    
    
    
    auto judges = [&](const LiveView &w, const RootAlt &a) {
        if (growonly && !pathslive && w.contents && !w.inplace && w.pointee && arrtype &&
            !CanHoldViewInto(w.pointee, arrtype))
            return false;
        return (w.contents || CallersJudge(a.root, root)) &&
               (growonly || w.reached || w.inplace || !a.slotread);
    };
    
    auto note = [&](const LiveView &w, const string &name) {
        for (auto &a : w.p.alts) {
            if (!judges(w, a)) continue;
            LiveShrink ls { .shrunk = root, .shrunkexact = !bound, .bound = bound,
                            .live = a.root, .liveexact = a.exact, .liveprojected = a.projected,
                            .pointee = w.pointee,
                            .byteview = w.p.byteview, .growonly = growonly, .name = name,
                            .contents = w.contents, .inplace = w.inplace };
            if (!NoteLiveShrink(ls, current))
                Error(at, cat(prefix, " while ", name, " is still used: it may refer into ", what,
                              growonly ? " (§5.1)" : " (§5.2)"));
        }
    };
    auto judged = [&](vector<LiveView> &vs) {
        vs.erase(std::remove_if(vs.begin(), vs.end(),
                                [&](const LiveView &w) {
                                    return !w.p.Any(
                                        [&](const RootAlt &a) { return judges(w, a); });
                                }),
                 vs.end());
        return !vs.empty();
    };
    HeldOperands([&](const Held &h) {
        auto vs = views(h.v, h.v.type, h.location || h.reread, false, h.inplace, nullptr);
        if (!judged(vs)) return;
        auto name = ExprStr(h.node);
        if (name.find('\n') != string::npos)
            name = h.loop ? "a for loop's sequence" : "an earlier expression value";
        for (auto &w : vs) note(w, name);
    });
    ShrinkScanVars([&](VarDef *v) {
        if (v == root || !v->type) return;
        PathsScope ps(*this, rebuilding && ExposedLater(v));
        auto t = v->type;
        vector<LiveView> vs;
        if (IsRefOrSlice(t) && !v->componentowner) {
            if (!v->refrootknown) return;
            vs = views(v->ref, t, false, v->isvar, false, v);
        } else if (growonly && HoldsPlainRef(t)) {
            
            
            
            set<VarDef *> seen;
            auto from = LiveEventBase(v);
            size_t since;
            if (!pathslive && KilledFor(v, v, LoadType(t), arrtype, since))
                from = std::max(from, since);
            RecordedViews(v, from, t, vs, seen);
        }
        if (!judged(vs) || !UsedAfter(v)) return;
        for (auto &w : vs) note(w, string(v->name));
    });
}








inline void TypeCheck::RecordedViews(VarDef *h, size_t from, TypeExpr *ht,
                                     vector<LiveView> &out, set<VarDef *> &seen) {
    if (!seen.insert(h).second) return;
    EachHolderRoot(h, from, [&](const StoreEvent &e) {
        if (e.classread) {
            Prov cv;
            cv.Set(e.src, true);
            cv.byteview = e.byteview || e.src->contentbyteview;
            out.push_back({ cv, ht, true, true });
            return;
        }
        StoredViews(e.root, e.exact, e.pointee, e.sliceref, e.byteview, ht, out, seen);
    });
}












inline void TypeCheck::StoredViews(VarDef *r, bool exact, TypeExpr *pointee, bool sliceref,
                                   bool byteview, TypeExpr *ht, vector<LiveView> &out,
                                   set<VarDef *> &seen) {
    Prov ep;
    ep.Set(r, exact);
    ep.byteview = byteview;
    out.push_back({ ep, pointee, true });
    if (!r || r->isglobal) return;
    vector<pair<TypeExpr *, bool>> slots;
    if (pointee) slots.push_back({ pointee, sliceref });
    else if (ht) RefSlots(ht, slots);
    for (auto [st, slice] : slots) {
        if (!HoldsPlainRef(st)) continue;
        if (exact && r->type && !IsRefOrSlice(r->type))
            return RecordedViews(r, 0, r->type, out, seen);
        if (exact && IsClassRoot(r) && !slice) {
            RecordedViews(r, 0, st, out, seen);
            Prov cv;
            cv.Set(r, true);
            cv.byteview = byteview || r->contentbyteview;
            out.push_back({ cv, st, true, true });
            continue;
        }
        Prov bv;
        bv.Set(r, false);
        bv.byteview = r->contentbyteview || (r->type && IsRefOrSlice(r->type) && r->ref.byteview);
        vector<TypeExpr *> ps;
        ReachedThroughRefs(st, ps);
        for (auto pt : ps) out.push_back({ bv, pt, true });
    }
}















inline void TypeCheck::HeldViews(const Prov &p, TypeExpr *held, bool isvar,
                                 vector<LiveView> &out, const TempHolds *temps, VarDef *base,
                                 TypeExpr *arrtype) {
    vector<TypeExpr *> pointees;
    ReachedThroughRefs(held, pointees);
    for (auto &a : p.alts) {
        auto r = a.root;
        if (!r || r->isglobal) continue;
        set<VarDef *> seen;
        
        
        
        size_t since = 0;
        auto emptied = base && a.exact && !isvar && !pathslive &&
                       KilledFor(r, base, held, arrtype, since);
        auto from = emptied ? std::max(LiveEventBase(r), since) : LiveEventBase(r);
        if (a.exact && r->type && !IsRefOrSlice(r->type) && !isvar) {
            RecordedViews(r, from, r->type, out, seen);
            continue;
        }
        if ((a.exact || a.classread) && IsClassRoot(r) && !isvar) {
            RecordedViews(r, from, held, out, seen);
            if (emptied) continue;
            Prov cv;
            cv.Set(r, true);
            cv.byteview = p.byteview || r->contentbyteview;
            out.push_back({ cv, held, true, true });
            continue;
        }
        const Roots *made = nullptr;
        if (temps && IsTemp(r))
            for (auto &[t, c] : *temps)
                if (t == r) made = &c;
        if (made) {
            for (auto &c : made->alts)
                if (c.root)
                    StoredViews(c.root, c.exact, nullptr, false, p.byteview, held, out, seen);
            continue;
        }
        Prov hv;
        hv.Set(r, false);
        hv.byteview = p.byteview || r->contentbyteview;
        for (auto pt : pointees) out.push_back({ hv, pt, true });
    }
}








template<typename F> void TypeCheck::EachHolderRoot(VarDef *holder, size_t from, F f) {
    set<VarDef *> seen;
    function<void(VarDef *, size_t)> walk = [&](VarDef *h, size_t start) {
        if (!seen.insert(h).second) return;
        if (h->type && ClassOf(h->type) == SC_ROOT)
            for (auto c : h->components) if (c) walk(c, start);
        for (auto i = start; i < storeevents.size(); i++) {
            auto e = storeevents[i];
            if (e.container != h || StoredLater(i)) continue;
            if (e.src && !e.src->isglobal && e.src->type) {
                walk(e.src, 0);
                continue;
            }
            if (e.classread) {
                walk(e.src, 0);
                f(e);
                continue;
            }
            
            
            
            if (e.src) {
                e.root = e.src;
                e.exact = false;
                if (e.src->isglobal) e.pointee = nullptr;
            }
            if (e.root) f(e);
        }
    };
    walk(holder, from);
}








inline bool TypeCheck::NoteLiveShrink(LiveShrink ls, FnSpec *current) {
    auto s = ls.shrunk, l = ls.live;
    if (!s || !l || IsTemp(s) || IsTemp(l)) return true;
    auto outside = [&](VarDef *v) {
        return IsClassRoot(v) || (v->type && (v->isglobal || v->ownerspec != current));
    };
    if (ls.contents) {
        
        
        
        
        if (!current) return false;
        if (!outside(s)) return true;
    } else {
        if (DifferentComponents(l, ls.liveexact || ls.liveprojected, s, ls.shrunkexact)) return true;
        
        
        
        auto moves = ls.inplace && (!ls.pointee || ClassOf(ls.pointee) != SC_FIXED);
        auto mayfree = moves ? ShrinkMayMove(s, ls.bound, ls.pointee)
                             : ShrinkMayFree(s, ls.bound, ls.growonly, ls.pointee, ls.byteview);
        
        
        
        if (!mayfree && !ls.shrunkexact && !ls.bound) {
            vector<TypeExpr *> reach;
            BoundReach(s, reach);
            for (auto t : reach) {
                auto arr = ResizableArrayIn(t);
                if (!arr) continue;
                mayfree = mayfree ||
                          (moves ? ShrinkMayMove(s, t, ls.pointee)
                                 : GrowOnlyTail(arr) == ls.growonly &&
                                       ShrinkMayFree(s, arr, ls.growonly, ls.pointee, ls.byteview));
            }
        }
        if (!mayfree) return true;
        
        
        if (ls.bound && l->type && ls.liveexact && !CanContain(LoadType(l->type), ls.bound))
            return true;
        if (MayAliasRoots(l, ls.liveexact, s, ls.shrunkexact, current) == AL_NO) return true;
        if (!current || s == l || (!IsClassRoot(s) && !IsClassRoot(l)) || !outside(s) ||
            !outside(l))
            return false;
    }
    for (auto &e : current->record.liveshrinks) {
        if (e.shrunk != s || e.live != l || !e.bound != !ls.bound || e.contents != ls.contents ||
            e.inplace != ls.inplace || e.growonly != ls.growonly)
            continue;
        if (e.bound && !TypeEq(e.bound, ls.bound)) continue;
        if (e.contents && !TypeEq(e.pointee, ls.pointee)) continue;
        e.shrunkexact = e.shrunkexact && ls.shrunkexact;
        e.liveexact = e.liveexact && ls.liveexact;
        e.liveprojected = e.liveprojected && ls.liveprojected;
        if (e.pointee && (!ls.pointee || !TypeEq(e.pointee, ls.pointee))) e.pointee = nullptr;
        e.byteview = e.byteview || ls.byteview;
        return true;
    }
    current->record.liveshrinks.push_back(ls);
    return true;
}









inline void TypeCheck::ApplyCalleeLiveShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                              string_view name) {
    auto record = RecordOf(spec);
    if (!record) return;   
    auto caller = CurRealFrame().spec;
    
    
    
    vector<Roots> args, views;
    TempHolds temps;
    args.reserve(argvals.size());
    views.reserve(argvals.size());
    for (size_t q = 0; q < spec->argtypes.size() && q < argvals.size(); q++) {
        args.push_back(ClassArgRoots(spec->argtypes[q], argvals[q]));
        views.push_back(ViewClassOf(spec, q) ? argvals[q].held.AsRoots() : Roots {});
        ReadBack held;
        if (TempContents(argvals[q], held)) temps.push_back({ argvals[q].Root(), held.roots });
    }
    
    
    auto mapped = [&](VarDef *r, bool exact, bool projected = false) -> Roots {
        bool view = false;
        auto p = ClassParam(spec, r, view);
        if (p >= 0 && p < (int)args.size()) {
            auto base = view ? ViewClassOf(spec, p) : spec->params[p]->ref.Root();
            auto m = ProjectClass(r, base, view ? views[p] : args[p]);
            if (!exact && !projected) m.Weaken();
            return m;
        }
        Roots one;
        one.Set(r, exact);
        one.alts[0].projected = projected;
        return one;
    };
    
    
    
    auto storage = [&](VarDef *r) -> TypeExpr * {
        if (r && r->componentbase)
            return LoadType(GetStructInst(r->componentbase)->ftypes[r->componentfield]);
        for (size_t p = 0; r && p < spec->params.size() && p < spec->argtypes.size(); p++) {
            auto pt = spec->argtypes[p];
            if (spec->params[p]->ref.Root() == r && pt->kind == TY_REF &&
                ClassOf(pt->ref->sub) == SC_RESIZABLE)
                return LoadType(pt->ref->sub);
        }
        return nullptr;
    };
    
    for (size_t k = 0; k < record->liveshrinks.size(); k++) {
        auto orig = record->liveshrinks[k];
        auto livename = orig.name;
        auto shrunk = mapped(orig.shrunk, orig.shrunkexact);
        auto live = mapped(orig.live, orig.liveexact, orig.liveprojected);
        auto shrunkat = orig.shrunkexact && !orig.bound ? storage(orig.shrunk) : nullptr;
        
        
        
        auto liveparam = false;
        for (size_t p = 0; p < spec->params.size(); p++)
            liveparam = liveparam || spec->params[p]->ref.Root() == orig.live;
        auto slotsafe = liveparam && !orig.growonly && !orig.inplace && !orig.contents;
        for (auto &sa : shrunk.alts) {
            for (auto &la : live.alts) {
                if (slotsafe && la.slotread) continue;
                auto ls = orig;
                ls.shrunk = sa.root;
                ls.shrunkexact = sa.exact;
                if (!sa.exact && shrunkat) ls.bound = shrunkat;
                ls.live = la.root;
                ls.liveexact = la.exact;
                ls.liveprojected = la.projected;
                if (!ls.shrunk || !ls.live) continue;
                
                
                if (ls.live != orig.live && IsClassRoot(ls.live)) ls.name = string(ls.live->name);
                
                
                
                
                
                
                
                
                
                
                
                
                auto contents = orig.contents;
                auto held = orig.pointee;
                if (!contents && (orig.growonly || orig.inplace) && orig.pointee && la.classread) {
                    contents = true;
                    held = ast.SliceOf(orig.pointee, at->line);
                }
                vector<LiveShrink> each;
                if (!contents) {
                    each.push_back(ls);
                } else if (IsTemp(la.root) &&
                           std::none_of(temps.begin(), temps.end(),
                                        [&](auto &t) { return t.first == la.root; })) {
                    auto x = ls;
                    x.contents = false;
                    x.live = ls.shrunk;
                    x.liveexact = false;
                    x.liveprojected = false;
                    x.pointee = nullptr;
                    each.push_back(x);
                } else {
                    Prov lp;
                    lp.alts = { la };
                    lp.byteview = orig.byteview;
                    vector<LiveView> vs;
                    HeldViews(lp, held, false, vs, &temps);
                    for (auto &w : vs) {
                        for (auto &b : w.p.alts) {
                            auto x = ls;
                            x.live = b.root;
                            x.liveexact = b.exact;
                            x.liveprojected = b.projected;
                            x.pointee = w.pointee;
                            x.byteview = w.p.byteview;
                            x.contents = w.contents;
                            if (x.live) each.push_back(x);
                        }
                    }
                }
                for (auto &x : each) {
                    if (NoteLiveShrink(x, caller)) continue;
                    
                    
                    
                    
                    auto arr = x.shrunkexact || !x.liveexact ? x.shrunk : x.live;
                    auto unnamed = x.bound && arr != x.live;
                    auto st = x.bound ? x.bound
                                      : x.shrunk->type ? LoadType(x.shrunk->type) : nullptr;
                    if (!x.shrunkexact && !x.liveexact && st) {
                        Roots one;
                        one.Set(x.shrunk, false);
                        for (auto &t : ShrinkTargets(one, st)) {
                            if (t.bound ||
                                MayAliasRoots(x.live, false, t.root, true, caller) == AL_NO)
                                continue;
                            arr = t.root;
                            unnamed = false;
                            break;
                        }
                    }
                    auto what = unnamed ? cat("an array ", x.shrunk->name, " leads to")
                                        : string(arr->name);
                    Error(at, cat("cannot call ", name, ": it ",
                                  x.bound || !shrunk.Exact() ? "may shrink " : "shrinks ",
                                  what, " while ", livename, " is still used, and ", livename,
                                  " may refer into ", unnamed ? "it" : what,
                                  x.growonly ? " (§5.1)" : " (§5.2)"));
                }
            }
        }
    }
}








inline void TypeCheck::ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals,
                                          string_view name, const vector<Node *> &argnodes) {
    
    
    
    
    
    erase_if(cur.kills, [&](const Kill &k) { return CallMayWrite(spec, argvals, k); });
    
    
    
    
    struct Hit {
        VarDef *root;
        TypeExpr *bound;
        TypeExpr *arr;   
        bool growonly;
        ShrinkBalance balance;
        const char *how;
        const FnRecord::ShrinkOrder *order;   
        bool viaclass;
    };
    vector<Hit> hits;
    auto storesbase = storeevents.size();
    vector<int> origins;
    auto current = CurRealFrame().spec;
    auto classbase = current ? current->record.classevents.size() : 0;
    vector<int> classorigins;
    
    
    
    
    
    
    auto argkill = [&](int p, const string &path) {
        if (p < 0 || (size_t)p >= argnodes.size() || (size_t)p >= argvals.size()) return;
        auto &v = argvals[(size_t)p];
        if (!v.Exact()) return;
        auto x = v.Root();
        if (!x || x->isglobal || IsTemp(x) || x->captured) return;
        auto n = argnodes[(size_t)p];
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) n = u->child;
        VarDef *base = nullptr;
        string bpath;
        if (!KillPath(n, base, bpath)) return;
        if (IsRefOrSlice(base->type) ? !base->ref.Exact() || base->ref.Root() != x : base != x)
            return;
        cur.kills.push_back({ x, base, bpath + path, storesbase });
    };
    auto apply = [&](const Hit &h, ShrinkBalance judged) {
        auto what = cat("call ", name, ", which ", h.how);
        auto op = cat(what, " ", h.root->name);
        if (h.growonly) {
            
            
            
            
            struct Restore {
                TypeCheck &tc;
                CallOrder order;
                size_t kills;
                ~Restore() {
                    tc.callorder = order;
                    tc.cur.kills.resize(std::min(kills, tc.cur.kills.size()));
                }
            } restore { *this, callorder, cur.kills.size() };
            if (h.order) {
                callorder = { storesbase, &origins, h.order->before, &classorigins, classbase };
                for (auto &[p, path] : h.order->kills) argkill(p, path);
            }
            GrowOnlyShrinkAt(at, what, h.root, string(h.root->name), h.bound, h.arr,
                             h.viaclass);
        } else if (judged == SB_UNBALANCED) {
            ShrinkGrowShrink(at, op, h.root, string(h.root->name), h.bound);
        } else {
            NoteShrink(h.root, h.bound, judged);
        }
        NoteShrinkEvent(at, h.root, h.bound, op, h.arr);
    };
    
    
    
    
    auto shrink = [&](const Roots &roots, TypeExpr *arr, const char *how,
                      ShrinkBalance balance, const FnRecord::ShrinkOrder *order,
                      bool classbound = false) {
        auto growonly = arr ? GrowOnlyTail(arr) : IsGrowOnlyRootVar(roots.Root());
        for (auto &t : ShrinkTargets(roots, arr, false, classbound && growonly))
            hits.push_back({ t.root, t.bound ? arr : nullptr, arr, growonly,
                             growonly ? SB_UNBALANCED : balance,
                             roots.Exact() && t.root == roots.Root() ? how : "may shrink",
                             order, t.viaclass });
    };
    {
        struct Restore {
            Replay &r;
            Replay saved;
            ~Restore() { r = saved; }
        } restore { replay, replay };
        replay = { -1, &classorigins, classbase };
        ApplyCalleeStores(spec, argvals, at, &origins);
    }
    
    
    auto rec = RecordOf(spec);
    if (!rec) return;
    
    
    
    auto ordered = rec == &spec->record && !spec->inprogress && !spec->incycle &&
                   !spec->sf->isrec && !spec->isfunval;
    auto orderof = [&](int param, VarDef *external,
                       TypeExpr *bound) -> const FnRecord::ShrinkOrder * {
        if (!ordered) return nullptr;
        for (auto &o : rec->shrinkorders)
            if (o.param == param && o.external == external && !o.bound == !bound &&
                (!bound || TypeEq(o.bound, bound)))
                return &o;
        return nullptr;
    };
    for (size_t i = 0; i < argvals.size() && i < spec->argtypes.size(); i++) {
        auto vi = (int)(spec->params.size() + i);
        if (auto view = rec->shrinkparams.find(vi); view != rec->shrinkparams.end())
            shrink(argvals[i].held, nullptr, "shrinks", view->second,
                   orderof(vi, nullptr, nullptr));
        auto pt = spec->argtypes[i];
        auto entry = rec->shrinkparams.find((int)i);
        auto recorded = entry != rec->shrinkparams.end();
        if (!recorded) continue;
        
        
        
        
        
        if (!IsRefOrSlice(pt)) {
            shrink(ClassArgRoots(pt, argvals[i]), nullptr, "shrinks", entry->second,
                   orderof((int)i, nullptr, nullptr));
            continue;
        }
        
        
        if (!argvals[i].Root() || pt->kind != TY_REF || ClassOf(pt->ref->sub) != SC_RESIZABLE)
            continue;
        shrink(argvals[i], LoadType(pt->ref->sub), "shrinks", entry->second,
               orderof((int)i, nullptr, nullptr));
    }
    
    
    
    for (auto &b : rec->shrinkparambounds) {
        if (b.key >= (int)spec->params.size()) {
            auto p = b.key - spec->params.size();
            if (p < argvals.size()) {
                Roots roots = argvals[p].held;
                roots.Weaken();
                shrink(roots, b.type, "may shrink", b.balance,
                       orderof(b.key, nullptr, b.type), true);
            }
            continue;
        }
        if (b.key >= (int)argvals.size()) continue;
        auto pt = spec->argtypes[b.key];
        auto roots = ClassArgRoots(pt, argvals[b.key]);
        roots.Weaken();
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        auto own = CurRealFrame().spec;
        auto ownclass = [&](VarDef *r) {
            return own && IsClassRoot(r) &&
                   find(own->classroots.begin(), own->classroots.end(), r) !=
                       own->classroots.end();
        };
        auto holder = IsRefOrSlice(pt) ? HolderSource(argvals[b.key]) : nullptr;
        if (holder && (!holder->type || holder->isglobal || IsRefOrSlice(holder->type)))
            holder = nullptr;
        auto held = holder ? HeldAt(argvals[b.key]) : Roots {};
        if (!held.None() && !held.unknown && held.All([&](const RootAlt &a) {
                return a.exact ? a.root && a.root->type && !IsRefOrSlice(a.root->type)
                               : ownclass(a.root) || a.root == holder;
            })) {
            roots.Clear();
            for (auto a : held.alts) {
                a.from = nullptr;
                a.classread = false;
                if (!a.exact && ownclass(a.root)) {
                    roots.Add(a);
                    continue;
                }
                if (a.root == holder && !a.exact) {
                    if (!MayHoldValueOf(LoadType(holder->type), b.type)) continue;
                    a.exact = true;
                }
                auto rt = LoadType(a.root->type);
                vector<TypeExpr *> reached;
                ReachedThroughRefs(rt, reached);
                auto leads = any_of(reached.begin(), reached.end(),
                                    [&](TypeExpr *t) { return MayHoldValueOf(t, b.type); });
                if (!leads && !MayHoldValueOf(rt, b.type)) continue;
                a.exact = !leads && CanContain(rt, b.type);
                roots.Add(a);
            }
        }
        shrink(roots, b.type, "may shrink", b.balance, orderof(b.key, nullptr, b.type), true);
    }
    for (auto &[vd, balance] : rec->shrinkexternals) {
        auto storage = vd->componentbase
                           ? LoadType(GetStructInst(vd->componentbase)->ftypes[vd->componentfield])
                           : nullptr;
        shrink(MapClass(spec, vd, argvals), storage, "shrinks", balance,
               orderof(-1, vd, nullptr));
    }
    for (auto &b : rec->shrinkexternalbounds) {
        auto one = MapClass(spec, b.key, argvals);
        one.Weaken();
        shrink(one, b.type, "may shrink", b.balance, orderof(-1, b.key, b.type));
    }
    
    
    
    
    
    
    auto judge = [&](const Hit &h) {
        if (h.growonly || h.balance == SB_UNBALANCED) return SB_UNBALANCED;
        auto judged = h.balance;
        for (auto &u : hits) {
            if (u.growonly || MayAliasRoots(h.root, !h.bound, u.root, !u.bound) == AL_NO)
                continue;
            if (u.balance == SB_UNBALANCED) return SB_UNBALANCED;
            judged = std::max(judged, u.balance);
        }
        return judged;
    };
    for (auto &h : hits) apply(h, judge(h));
    ApplyCalleeLiveShrinks(at, spec, argvals, name);
}

inline bool TypeCheck::BuiltInPlace(TypeExpr *elem) {
    return ClassOf(elem) != SC_FIXED || (elem->kind != TY_REF && HasRelRefT(elem, true));
}



inline TypeExpr *TypeCheck::AppendedRun(TypeExpr *elem, ArrayLit *al) {
    if (ClassOf(elem) != SC_FIXED) return ast.ArrayOf(elem, A_VAR, al->line);
    
    auto n = al->fillval ? FillCount(al->fillcount) : (int64_t)al->elems.size();
    return ast.ArrayOf(elem, A_FIXED, al->line, std::max<int64_t>(n, 0));
}






inline void TypeCheck::AppendedCopies(Node *an, const Val &av, TypeExpr *elem, const Val &rv) {
    if (HasRelRefT(elem))
        Error(an, cat(".append copies the elements of ", ExprStr(an), ": copying a value of "
                      "type ", TypeStr(elem), ", which contains self-relative references, is "
                      "not supported; append an array literal, which constructs them in place"));
    if (!HoldsPlainRef(elem)) return;
    auto ev = av;
    if (IsRefOrSlice(av.type)) {
        LVal lv;
        lv.SetProv(av);
        lv.type = elem;
        lv.fromstorage = true;
        lv.isslot = true;
        ev = ContainerRead(lv);
    }
    DestScope ds(*this, Dest(rv, false, rv.reached));
    MustFit(ev, an, ev.type);
}

inline TypeCheck::Alias TypeCheck::MayAliasRoots(VarDef *a, bool aexact, VarDef *b,
                                                 bool bexact) {
    return MayAliasRoots(a, aexact, b, bexact, CurRealFrame().spec);
}


inline TypeCheck::Alias TypeCheck::MayAliasRoots(VarDef *a, bool aexact, VarDef *b,
                                                 bool bexact, FnSpec *current) {
    if (!a || !b || IsTemp(a) || IsTemp(b)) return AL_NO;
    if (a == b) return AL_YES;
    if (aexact && bexact && a->componentowner && b->componentowner &&
        a->componentowner == b->componentowner && a->componentfield != b->componentfield &&
        TypeEq(a->componentbase, b->componentbase)) return AL_NO;
    auto isclass = [](VarDef *v) { return !v->type; };
    auto own = [&](VarDef *v, bool exact) {
        return exact && v->type && !v->isglobal && v->ownerspec == current;
    };
    if (isclass(a) || isclass(b)) {
        
        
        
        
        if (own(a, aexact) || own(b, bexact)) return AL_NO;
        return isclass(a) && isclass(b) ? AL_DEFER : AL_YES;
    }
    
    
    if (!aexact && Depth(a) >= Depth(b)) return AL_YES;
    if (!bexact && Depth(b) >= Depth(a)) return AL_YES;
    return AL_NO;
}

inline TypeExpr *TypeCheck::SoleResizableIn(TypeExpr *t) {
    if (!t || ClassOf(t) != SC_RESIZABLE) return nullptr;
    if (t->kind == TY_ARRAY) return t;
    TypeExpr *arr = nullptr;
    auto mixed = AnyField(t, [&](TypeExpr *ft) {
        if (ClassOf(ft) != SC_RESIZABLE) return false;
        auto a = SoleResizableIn(ft);
        if (!a || (arr && !TypeEq(a, arr))) return true;
        arr = a;
        return false;
    });
    return mixed ? nullptr : arr;
}

inline void TypeCheck::NoteGrow(Node *at, const Roots &roots, const string &what, TypeExpr *t) {
    auto arr = SoleResizableIn(t);
    auto note = [&](vector<TypeExpr *> &types) {
        for (auto u : types)
            if (!u == !arr && (!u || TypeEq(u, arr))) return;
        types.push_back(arr);
    };
    for (auto &a : roots.alts) {
        auto root = a.root;
        if (!root || IsTemp(root)) continue;
        cur.growlog.push_back({ at, root, a.exact, what, false, arr });
        NoteRootEvent(root, [&](FnSpec *s, int i) { note(s->record.growparams[i]); },
                      [&](FnSpec *s, VarDef *r) { note(s->record.growexternals[r]); });
    }
}




inline void TypeCheck::NoteShrinkEvent(Node *at, VarDef *root, TypeExpr *bound,
                                       const string &what, TypeExpr *t) {
    if (!root || IsTemp(root)) return;
    cur.growlog.push_back({ at, root, !bound, what, true, SoleResizableIn(t) });
}








inline void TypeCheck::CheckGrowsSince(size_t base, const Roots &built, const string &what,
                                       TypeExpr *t) {
    auto arr = SoleResizableIn(t);
    for (auto i = base; i < cur.growlog.size(); i++) {
        auto e = cur.growlog[i];
        if (arr && e.arr && !TopConstEq(arr, e.arr)) continue;
        for (auto &b : built.alts) {
            auto may = MayAliasRoots(e.root, e.exact, b.root, b.exact);
            if (may == AL_NO) continue;
            auto msg = cat("cannot ", e.what, ": ", what, " is still under construction, and "
                           "the ", e.shrink ? "shrink would free the storage it is built in"
                                            : "growth would land inside it", " (§1.3)");
            if (may == AL_YES) Error(e.at, msg);
            growconflicts.push_back({ e.at, CurRealFrame().spec, e.root, b.root, msg });
        }
    }
}







inline void TypeCheck::ApplyCalleeGrows(Node *at, FnSpec *spec, vector<Val> &argvals,
                                        string_view name) {
    
    auto grows = [&](size_t i, const char *how, TypeExpr *arr) {
        auto view = i >= spec->params.size();
        if (view) i -= spec->params.size();
        if (i >= argvals.size()) return;
        auto roots = view ? argvals[i].held.AsRoots()
                          : ClassArgRoots(spec->argtypes[i], argvals[i]);
        auto root = roots.Root();
        if (!root || IsTemp(root)) return;
        NoteGrow(at, roots, cat("call ", name, ", which ", how, " ", root->name), arr);
    };
    if (spec->sf->isextern) {
        for (size_t i = 0; i < spec->argtypes.size(); i++) {
            auto pt = spec->argtypes[i];
            if (IsPlainRef(pt) && ClassOf(pt->ref->sub) == SC_RESIZABLE)
                grows(i, "may grow", pt->ref->sub);
        }
        return;
    }
    
    
    auto rec = RecordOf(spec);
    if (!rec) return;
    for (auto &[pi, arrs] : rec->growparams)
        for (auto arr : arrs) grows((size_t)pi, "grows", arr);
    for (auto &[vd, arrs] : rec->growexternals)
        for (auto arr : arrs)
            NoteGrow(at, MapClass(spec, vd, argvals), cat("call ", name, ", which grows ", vd->name), arr);
}







inline bool TypeCheck::MayHoldBuilt(TypeExpr *y, Node *lval, TypeExpr *target) {
    for (auto t = target; t;) {
        if (TypeEq(y, t) || TypeEq(LoadType(y), t)) return true;
        if (t->kind == TY_ARRAY) break;
        TypeExpr *next = nullptr;
        AnyField(t, [&](TypeExpr *ft) { return ResizableArrayIn(ft) && (next = ft); });
        t = next;
    }
    if (lval->exprtype && lval->exprtype->kind == TY_REF) return CanContain(y, target);
    if (auto d = Is<Dot>(lval)) {
        auto h = d->obj->exprtype;
        if (h && h->kind == TY_REF) h = h->ref->sub;
        return !h || CanContain(y, h);
    }
    auto id = Is<Ident>(lval);
    if (id && id->vdef && id->vdef->type && id->vdef->type->kind != TY_REF) return false;
    return CanContain(y, target);
}







inline TypeCheck::Alias TypeCheck::ReachesBuilt(VarDef *v, VarDef *built, bool exact,
                                                Node *lval, TypeExpr *target, TypeExpr *arr,
                                                VarDef *&root) {
    auto t = v->type;
    if (!t || t->kind == TY_SLICE || IsRelRef(t))
        return AL_NO;
    auto isref = t->kind == TY_REF;
    if (isref ? !MayHoldBuilt(t->ref->sub, lval, target) : !CanContain(t, arr)) return AL_NO;
    auto roots = isref ? RefRootsOf(v) : RootsOf(v);
    auto worst = AL_NO;
    for (auto &a : roots.alts) {
        auto may = MayAliasRoots(a.root, a.exact, built, exact);
        if (may == AL_NO) continue;
        if (worst == AL_NO || may == AL_YES) root = a.root;
        worst = may == AL_YES ? AL_YES : worst == AL_YES ? AL_YES : AL_DEFER;
    }
    return worst;
}








inline void TypeCheck::CheckBuiltUses(Node *rhs, Node *lval, TypeExpr *target,
                                      const Roots &built, TypeExpr *arr) {
    if (!built.Root() || IsTemp(built.Root())) return;
    auto what = ExprStr(lval);
    auto base = lval;
    while (auto d = Is<Dot>(base)) base = d->obj;
    auto lvvar = Is<Ident>(base) ? Is<Ident>(base)->vdef : nullptr;
    
    
    auto report = [&](Node *at, Alias may, VarDef *root, VarDef *b, const string &how,
                      const string &why) {
        auto msg = cat("cannot ", how, " in the value assigned to ", what, ": ",
                       why.empty() ? string() : cat(why, ", and "),
                       "that value is built over the old contents of ", what,
                       " (§4.4); build it in a variable of its own, and assign copy() of "
                       "that");
        if (may == AL_YES) Error(at, msg);
        growconflicts.push_back({ at, CurRealFrame().spec, root, b, msg });
    };
    auto check = [&](Node *at, VarDef *v, const string &how, const string &via) {
        for (auto &b : built.alts) {
            if (!b.root || IsTemp(b.root)) continue;
            VarDef *root = nullptr;
            auto may = ReachesBuilt(v, b.root, b.exact, lval, target, arr, root);
            if (may == AL_NO) continue;
            
            auto refers = v->type->kind == TY_REF && v != lvvar;
            auto why = via.empty() ? (refers ? cat(v->name, " may refer to ", what) : string())
                                   : cat(via, refers ? cat(", which may refer to ", what) : "");
            report(at, may, root, b.root, how, why);
        }
    };
    
    
    
    
    auto checkread = [&](Ident *id, Node *path) {
        auto v = id->vdef;
        
        TypeExpr *vt = nullptr;
        if (auto d = Is<Dot>(path); d && d->fieldidx >= 0) {
            auto ot = d->obj->exprtype;
            if (ot && ot->kind == TY_REF) ot = ot->ref->sub;
            auto runs = ot ? FieldRuns(ot) : span<const FieldRun>();
            if (runs.size() == 1) vt = (*runs[0].ftypes)[d->fieldidx];
        }
        if (!v || !v->type || !vt) return;
        vector<TypeExpr *> reached;
        ReachedThroughRefs(vt, reached);
        if (none_of(reached.begin(), reached.end(),
                    [&](TypeExpr *y) { return MayHoldBuilt(y, lval, target); }))
            return;
        Roots at;
        if (IsRefOrSlice(v->type)) at = Bounds(RefRootsOf(v));
        else if (!v->isglobal && !v->contents.Unknown()) at = v->contents;
        else at = Bounds(RootsOf(v));
        for (auto &b : built.alts) {
            if (!b.root || IsTemp(b.root)) continue;
            for (auto &a : at.alts) {
                auto may = MayAliasRoots(a.root, a.exact, b.root, b.exact);
                if (may == AL_NO) continue;
                report(id, may, a.root, b.root, cat("use ", ExprStr(path)),
                       cat(ExprStr(path), " may refer to ", what));
                break;
            }
        }
    };
    EachUse(rhs,
            [&](Ident *id, Node *path) {
                if (FieldsApart(path, lval, target)) return;
                check(id, id->vdef, cat("use ", ExprStr(path)), string());
                if (path != id) checkread(id, path);
            },
            [&](Call *c, FnSpec *sp) {
                vector<VarDef *> named;
                auto pending = NamedOutside(sp, named);
                for (auto v : named)
                    check(c, v, cat("call ", sp->sf->name),
                          cat(sp->sf->name, pending ? ", still being checked, may use "
                                                    : " uses ", v->name));
            });
}






template<typename F, typename G> void TypeCheck::EachUse(Node *n, F named, G called) {
    function<void(Node *)> walk = [&](Node *n) {
        if (!n) return;
        if (auto a = Is<Assign>(n); a && a->op == T_DOTASSIGN && Is<Ident>(a->lval)) {
            walk(a->rhs);
            return;
        }
        auto base = n;
        for (auto d = Is<Dot>(base); d && d->fieldidx >= 0; d = Is<Dot>(base)) base = d->obj;
        if (auto id = Is<Ident>(base); id && id->vdef) {
            named(id, n);
            if (base != n) return;
        }
        if (auto c = Is<Call>(n)) {
            if (c->spec) called(c, c->spec);
            for (auto sp : c->dispatch) called(c, sp);
            for (auto &fs : c->fmtspecs) called(c, fs.second);
        }
        RunChildren(n, walk);
    };
    walk(n);
}







inline bool TypeCheck::FieldsApart(Node *use, Node *lval, TypeExpr *target) {
    auto path = [](Node *n, vector<Dot *> &fields) -> VarDef * {
        for (auto d = Is<Dot>(n); d; d = Is<Dot>(n)) {
            if (d->fieldidx < 0) return nullptr;
            fields.push_back(d);
            n = d->obj;
        }
        auto id = Is<Ident>(n);
        return id ? id->vdef : nullptr;
    };
    vector<Dot *> uf, lf;
    auto var = path(use, uf);
    if (!var || var != path(lval, lf)) return false;
    for (size_t i = 1; i <= uf.size() && i <= lf.size(); i++) {
        if (uf[uf.size() - i]->fieldidx == lf[lf.size() - i]->fieldidx) continue;
        auto ot = uf[0]->obj->exprtype;
        if (ot && ot->kind == TY_REF) ot = ot->ref->sub;
        auto runs = ot ? FieldRuns(ot) : span<const FieldRun>();
        if (runs.size() != 1) return false;
        auto ft = (*runs[0].ftypes)[uf[0]->fieldidx];
        
        
        if (lval->exprtype && lval->exprtype->kind == TY_REF && CanContain(ft, target))
            return false;
        if (IsFlat(ft)) return true;
        vector<TypeExpr *> reached;
        ReachedThroughRefs(ft, reached);
        for (auto y : reached)
            if (MayHoldBuilt(y, lval, target)) return false;
        return true;
    }
    return false;
}








inline bool TypeCheck::NamedOutside(FnSpec *spec, vector<VarDef *> &out) {
    if (auto it = namedoutside.find(spec); it != namedoutside.end()) {
        out = it->second;
        return false;
    }
    set<FnSpec *> walked;
    vector<VarDef *> named;
    auto pending = false;
    function<void(FnSpec *)> visit = [&](FnSpec *sp) {
        if (sp->sf->isextern || !walked.insert(sp).second) return;
        
        
        auto rec = RecordOf(sp);
        if (!rec) {
            pending = true;
            return;
        }
        
        
        EachUse(sp->body, [&](Ident *id, Node *) { named.push_back(id->vdef); },
                [&](Call *, FnSpec *callee) { visit(callee); });
    };
    visit(spec);
    set<VarDef *> seen;
    auto add = [&](VarDef *v) {
        if (seen.insert(v).second) out.push_back(v);
        for (auto c : v->components)
            if (c && seen.insert(c).second) out.push_back(c);
    };
    
    for (auto v : named)
        if (v->isglobal || !walked.count(v->ownerspec)) add(v);
    if (pending) {
        for (auto g : ast.globals)
            for (auto vd : g->defs) add(vd);
        for (auto vd : vars) add(vd);
    } else {
        namedoutside[spec] = out;
    }
    return pending;
}



inline void TypeCheck::ElemArg(Node *&n, TypeExpr *elem, Val &rv, bool fill) {
    SlotScope ss(*this, true);
    auto orig = n;
    Dest d(rv, false, rv.reached);
    d.fill = fill;
    auto v = CheckValueAt(n, elem, d, true);
    
    
    JudgeCastAt(orig, elem, v);
}

}  
