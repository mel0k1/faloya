





#pragma once

namespace faloya {

inline string ConstFor::CgX(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const for"); }
inline void ConstFor::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: unexpanded const for"); }
inline void ConstFor::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const for"); }
inline string ConstIf::CgX(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const if"); }
inline void ConstIf::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: unexpanded const if"); }
inline void ConstIf::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const if"); }
inline string ConstQuery::CgX(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const if query"); }
inline void ConstQuery::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: unexpanded const if query"); }
inline void ConstQuery::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: unexpanded const if query"); }



inline string IntLit::CgX(CodeGen &) {
    
    if (val < 0 && exprtype && exprtype->kind == TY_INT && exprtype->intstorage == IS_U64)
        return cat((uint64_t)val, "ULL");
    return CodeGen::IntStr(val);
}

inline string FltLit::CgX(CodeGen &) {
    return CodeGen::FltStr(val, exprtype && exprtype->kind == TY_FLT &&
                                exprtype->fltstorage == FS_F32);
}

inline string BoolLit::CgX(CodeGen &) { return val ? "1" : "0"; }

inline string NullLit::CgX(CodeGen &cg) {
    if (exprtype && exprtype->kind == TY_REF && cg.IsResz(exprtype->ref->sub)) {
        auto t = cg.T();
        cg.L("gs_rref ", t, " = { 0, 0 };");
        return t;
    }
    return "NULL";
}

inline string StrLit::CgX(CodeGen &cg) {
    auto et = exprtype;
    if (et->kind == TY_SLICE) {
        auto t = cg.T();
        cg.L(cg.CT(et), " ", t, " = { ", cg.StrRaw(val), ", ", val.size(), " };");
        return t;
    }
    
    assert(et->kind == TY_ARRAY && (et->arr->akind == A_FIXED || et->arr->akind == A_LIMITED));
    auto t = cg.T();
    cg.FixedLocal(et, t, et->arr->akind == A_LIMITED ? "{0}" : "");
    if (et->arr->akind == A_LIMITED) cg.L(t, ".len = ", val.size(), ";");
    if (!val.empty())
        cg.L("GS_MEMCPY(", t, ".e, ", cg.StrRaw(val), ", ", val.size(), ");");
    return t;
}

inline string Ident::CgX(CodeGen &cg) {
    assert(vdef);   
    auto x = cg.LoadLoc(cg.VarLoc(vdef), exprtype, line);
    
    
    
    
    if ((vdef->unsized || vdef->constlit) && exprtype && vdef->type) {
        auto narrower = (exprtype->kind == TY_INT && vdef->type->kind == TY_INT &&
                         exprtype->intstorage != IS_VARINT &&
                         exprtype->intstorage != vdef->type->intstorage) ||
                        (exprtype->kind == TY_FLT && vdef->type->kind == TY_FLT &&
                         exprtype->fltstorage != vdef->type->fltstorage);
        if (narrower) return cat("((", cg.CT(exprtype), ")", x, ")");
    }
    return x;
}

inline string Unary::CgX(CodeGen &cg) {
    if (op == T_BITAND) {
        
        
        
        
        if (exprtype && exprtype->kind != TY_REF)
            return cg.LoadLoc(cg.GenLoc(child), exprtype, line);
        return cg.GenRefVal(child, line);
    }
    if (op == T_MINUS && exprtype &&
        (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY)) {
        auto x = cg.GenVal(child);
        auto tv = cg.T();
        cg.FixedLocal(exprtype, tv);
        cg.GenElemwiseNegInto(exprtype, line, x, tv);
        return tv;
    }
    auto x = cg.GenX(child);
    switch (op) {
        case T_MINUS:
            if (child->exprtype->kind == TY_FLT) return cat("(-", x, ")");
            return cat("gs_neg_", cg.IntSfx(child->exprtype->intstorage), "(", x,
                       cg.OvfLocArgs(child->exprtype->intstorage, line), ")");
        case T_NOT: {
            auto ct = child->exprtype;
            if (ct->kind == TY_REF && cg.IsResz(ct->ref->sub))
                return cat("(", x, ".hdr == 0)");
            return cat("(uint8_t)(!", x, ")");
        }
        case T_BITNOT: {
            
            
            
            auto ot = cg.OperandT(child->exprtype);
            return cat("(", cg.IntCT(ot->intstorage), ")(~(", x, "))");
        }
        default: assert(false); return x;
    }
}

inline string Binary::CgX(CodeGen &cg) {
    if (op == T_DOTEQ || op == T_DOTNEQ) {
        
        
        
        auto side = [&](Node *n) -> string {
            if (Is<NullLit>(n)) return "NULL";
            string x;
            TypeExpr *t = n->exprtype;
            if (auto id = Is<Ident>(n); id && id->vdef && id->vdef->type->kind == TY_REF) {
                t = id->vdef->type;
                auto lv = cg.VarLoc(id->vdef);
                x = t->ref->lenstorage >= 0 ? cg.LoadLoc(lv, t, line) : lv.s;
            } else {
                x = cg.GenX(n);
            }
            return t->kind == TY_REF && cg.IsResz(t->ref->sub) ? cat(x, ".hdr") : x;
        };
        auto lx = side(left);
        auto l = cg.T();
        cg.L("void *", l, " = (void *)(", lx, ");");
        auto r = side(right);
        return cat("(uint8_t)((void *)(", l, ") ", op == T_DOTEQ ? "==" : "!=", " (void *)(", r, "))");
    }
    if (op == T_ANDAND || op == T_OROR) {
        auto l = cg.GenTruth(left);
        auto t = cg.T();
        cg.L("uint8_t ", t, " = (uint8_t)(", l, " != 0);");
        
        
        
        
        
        
        
        if (cg.SpeculativeForm(this)) {
            auto r = cg.GenTruth(right);
            return cat("(uint8_t)(", t, op == T_ANDAND ? " & " : " | ", "(uint8_t)(", r,
                       " != 0))");
        }
        
        
        
        
        cg.L("if (", op == T_ANDAND ? t : cat("!", t), ") {");
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        auto r = cg.GenTruth(right);
        cg.L(t, " = (uint8_t)(", r, " != 0);");
        cg.PopSc();
        cg.ind--;
        cg.L("}");
        return t;
    }
    auto lt = cg.OperandT(left->exprtype);
    
    if (op == T_EQ || op == T_NEQ) {
        auto lnull = Is<NullLit>(left) != nullptr, rnull = Is<NullLit>(right) != nullptr;
        if (lnull || rnull) {
            if (lnull && rnull) return op == T_EQ ? "1" : "0";
            auto other = lnull ? right : left;
            
            
            string x;
            TypeExpr *ot = other->exprtype;
            if (auto id = Is<Ident>(other); id && id->vdef && IsOptional(id->vdef->type)) {
                auto lv = cg.VarLoc(id->vdef);
                ot = id->vdef->type;
                if (ot->ref->lenstorage >= 0) {
                    
                    
                    string faddr, off;
                    cg.RelParts(lv, faddr, off);
                    return cat("(", off, op == T_EQ ? " == 0)" : " != 0)");
                }
                x = lv.s;
            } else {
                x = cg.GenX(other);
            }
            auto addr = ot->kind == TY_REF && cg.IsResz(ot->ref->sub)
                            ? cat(x, ".hdr") : x;
            return cat("(", addr, op == T_EQ ? " == NULL)" : " != NULL)");
        }
    }
    
    if ((op == T_EQ || op == T_NEQ) && cg.IsResz(cg.OperandT(left->exprtype))) {
        if (left->exprtype->kind != right->exprtype->kind ||
            cg.OperandT(left->exprtype)->kind != TY_ARRAY)
            cg.Fail(line, "comparing resizable structs is unsupported");
        auto la = cg.GenLoc(left);
        if (la.t->kind == TY_REF) cg.DerefLoc(la);
        
        auto lvw = cg.ArrayView(la);
        auto le = cg.T(), ln = cg.T();
        cg.L(lvw.typedelems ? cg.CT(lvw.elem) : string("uint8_t"), " *", le, " = ", lvw.elems,
             ";");
        cg.L("int64_t ", ln, " = ", lvw.len, ";");
        lvw.elems = le;
        lvw.len = ln;
        auto ra = cg.GenLoc(right);
        if (ra.t->kind == TY_REF) cg.DerefLoc(ra);
        auto rvw = cg.ArrayView(ra);
        auto eq = cg.GenRangeEq(lvw.elem, lvw.elems, lvw.len, rvw.elems, rvw.len);
        return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
    }
    
    
    
    auto l = cg.GenOperandVal(left, cg.HasStmts(right));
    auto r = cg.GenVal(right);
    if (exprtype && (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY))
        return cg.GenElemwise(this, l, r);
    auto isint = lt->kind == TY_INT && lt->intstorage != IS_VARINT;
    auto isflt = lt->kind == TY_FLT;
    
    
    auto ct = isint || isflt ? cg.CT(lt) : "";
    auto lc = isint || isflt ? cat("(", ct, ")(", l, ")") : l;
    auto rc = isint || isflt ? cat("(", ct, ")(", r, ")") : r;
    switch (op) {
        case T_PLUS: case T_MINUS: case T_MUL: case T_DIV: case T_MOD: {
            
            if (!isint && !isflt) return cg.GenElemwise(this, l, r);
            auto x = [&] { return cg.ArithX(op, lt, l, r, line, nonneg); };
            CodeGen::Division dv;
            return CodeGen::AsDivision(this, dv) ? cg.FixedDivX(dv, l, x, line) : x();
        }
        case T_LT: case T_GT: case T_LTEQ: case T_GTEQ: {
            const char *o = op == T_LT ? " < " : op == T_GT ? " > "
                            : op == T_LTEQ ? " <= " : " >= ";
            return cat("(uint8_t)(", lc, o, rc, ")");
        }
        case T_EQ: case T_NEQ: {
            if (isint || isflt) {
                auto eq = cat("(", lc, " == ", rc, ")");
                return op == T_EQ ? cat("(uint8_t)", eq) : cat("(uint8_t)(!", eq, ")");
            }
            auto eq = cg.GenEquality(lt, l, r);
            return op == T_EQ ? eq : cat("(uint8_t)(!", eq, ")");
        }
        case T_BITAND: case T_BITOR: case T_XOR: case T_SHL: case T_SHR:
            return cg.ArithX(op, lt, l, r, line, nonneg);
        default: assert(false); return l;
    }
}

inline string Dot::CgX(CodeGen &cg) {
    if (variantconst) {
        auto et = exprtype;
        auto ei = einst;
        auto vi = ei->en->VariantIndex(variantconst);
        if (et->kind == TY_ENUM && !et->enu->varmode) {
            auto t = cg.T();
            cg.FixedLocal(et, t);
            cg.L(t, ".tag = ", cg.TagConst(ei, vi), ";");
            return t;
        }
        cg.Fail(line, "variant constant in a non-fixed context reached GenX");
    }
    if (member >= 0) {   
        auto lv = cg.GenLoc(obj);
        if (lv.t->kind == TY_REF) cg.DerefLoc(lv);
        if (lv.t->kind == TY_ARRAY && member == B_CAP) {
            assert(lv.t->arr->akind == A_LIMITED);
            return cg.LimitedCap(lv);
        }
        auto v = cg.ArrayView(lv);
        return cat("(", v.len, ")");
    }
    auto lv = cg.GenLoc(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string Index::CgX(CodeGen &cg) {
    auto lv = cg.GenLocNow(this);
    return cg.LoadLoc(lv, exprtype, line);
}

inline string SliceExpr::CgX(CodeGen &cg) {
    auto s = cg.GenSlice(this);
    if (!cg.IsStaticLimited(exprtype)) return s;
    
    CodeGen::Loc slv;
    slv.val = true;
    slv.s = s;
    slv.t = cg.ast.SliceOf(exprtype->arr->sub, line);
    return cg.AdaptToFixed(slv, exprtype, line);
}






inline string AsCast::CgX(CodeGen &cg) {
    
    
    
    auto x = totype->ix && !nobc ? cg.IndexTypeCheck(cg.GenPure(child), totype, line)
                                 : cg.GenX(child);
    auto st = child->exprtype;
    auto tt = totype;
    
    
    auto su64 = st->kind == TY_INT && st->intstorage == IS_U64;
    if (tt->kind == TY_INT) {
        auto is = tt->intstorage;
        auto tct = cg.IntCT(is);
        auto named = [&] { return cat("\"", IntStorageName(is), "\", ", cg.LocArgs(line)); };
        if (st->kind == TY_FLT) {
            if (unchecked) return cat("(", tct, ")gs_f2iwrap(", x, ")");
            auto f32 = IsF32(st) ? "1" : "0";
            if (is == IS_U64)
                return cat("(uint64_t)GS_F2U(", x, ", ", f32, ", ", cg.LocArgs(line), ")");
            auto [lo, hi] = IntRange(is);
            return cat("(", tct, ")GS_F2I(", x, ", ", f32, ", ", cg.IntStr(lo), ", ",
                       cg.IntStr(hi), ", ", named(), ")");
        }
        if (unchecked || cg.TEq(st, tt)) return cat("(", tct, ")(", x, ")");
        if (su64) {
            
            
            if (is == IS_U64) return cat("(", x, ")");
            auto hi = IntRange(is).second;
            return cat("(", tct, ")GS_RANGE_U(", x, ", ", (uint64_t)hi, "ULL, ", named(), ")");
        }
        
        
        auto [slo, shi] = IntRange(st->intstorage);
        auto [lo, hi] = IntRange(is);
        if (slo >= lo && shi <= hi) return cat("(", tct, ")(", x, ")");
        return cat("(", tct, ")GS_RANGE((int64_t)(", x, "), ", cg.IntStr(lo), ", ",
                   cg.IntStr(hi), ", ", named(), ")");
    }
    assert(tt->kind == TY_FLT);
    if (tt->fltstorage == FS_F32) return cat("(float)(", x, ")");
    return cat("(double)(", x, ")");
}

inline string Call::CgX(CodeGen &cg) {
    
    if (cg.AdtFrom(this)) return cg.CtlValX(this);
    auto rets = cg.EmitCall(this, Dst {});
    assert(!rets.empty());
    return cg.CallVal0(this, rets[0]);
}





inline string StructLit::CgX(CodeGen &cg) {
    auto tv = cg.T();
    cg.FixedLocal(exprtype, tv, cg.HasUninitSlots(exprtype) ? "{0}" : "");
    cg.StructLitAt(this, tv, false);
    return tv;
}

inline string ArrayLit::CgX(CodeGen &cg) { return cg.GenFixedArrayLit(this); }

inline string Block::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string IfExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string MatchExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string EarlyBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string LoopExpr::CgX(CodeGen &cg) { return cg.CtlValX(this); }
inline string InlineBlock::CgX(CodeGen &cg) { return cg.CtlValX(this); }


inline string While::CgX(CodeGen &cg) { cg.Fail(line, "internal: While as value"); }
inline string ForLoop::CgX(CodeGen &cg) { cg.Fail(line, "internal: ForLoop as value"); }
inline string Return::CgX(CodeGen &cg) { cg.Fail(line, "internal: Return as value"); }
inline string Break::CgX(CodeGen &cg) { cg.Fail(line, "internal: Break as value"); }
inline string Continue::CgX(CodeGen &cg) { cg.Fail(line, "internal: Continue as value"); }
inline string Defer::CgX(CodeGen &cg) { cg.Fail(line, "internal: Defer as value"); }
inline string RangeExpr::CgX(CodeGen &cg) { cg.Fail(line, "internal: RangeExpr as value"); }
inline string FunVal::CgX(CodeGen &cg) { cg.Fail(line, "internal: FunVal as value"); }
inline string SelfRef::CgX(CodeGen &cg) { cg.Fail(line, "internal: self as value"); }
inline string VarDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: VarDecl as value"); }
inline string Assign::CgX(CodeGen &cg) { cg.Fail(line, "internal: Assign as value"); }
inline string IncDec::CgX(CodeGen &cg) { cg.Fail(line, "internal: IncDec as value"); }
inline string FnDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: FnDecl as value"); }
inline string StructDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string EnumDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }
inline string AliasDecl::CgX(CodeGen &cg) { cg.Fail(line, "internal: decl as value"); }



inline void Block::CgAny(CodeGen &cg, const Dst &d) {
    
    
    auto first = cg.GenInlineArgs(this);
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.L("{");
    cg.ind++;
    cg.GenBlockInner(this, d, first);
    cg.PopSc();
    cg.ind--;
    cg.L("}");
}

inline void IfExpr::CgAny(CodeGen &cg, const Dst &d) {
    
    
    
    
    
    auto ta = !flat && elseb ? cg.LeadingCheck(thenb) : nullptr;
    auto ea = ta ? cg.LeadingCheck(elseb) : nullptr;
    auto hoist = ea && cg.SameCheck(ta, ea);
    
    
    string lbl;
    if (!hoist && cg.BranchChain(cond)) {
        lbl = cg.Lbl();
        cg.GenBranch(cond, flat, lbl);
    }
    auto c = lbl.empty() ? cg.GenTruth(cond) : "";
    if (flat) {
        
        
        
        cg.L(lbl.empty() ? cat("if (!(", c, ")) {") : "{");
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (auto b = Is<Block>(elseb)) cg.GenBlockInner(b, d);
        else cg.GenAny(elseb, d);
        cg.cscopes.back().saves.clear();   
        cg.PopSc();
        cg.ind--;
        cg.L("}");
        if (!lbl.empty()) cg.L(lbl, ":;");
        cg.PushSc(CodeGen::SC_PLAIN);
        cg.GenBlockInner(thenb, d);
        cg.PopSc();
        cg.termjump = false;
        return;
    }
    if (hoist) {
        auto ct = cg.T();
        cg.L("uint8_t ", ct, " = (uint8_t)(", c, ");");
        c = ct;
        cg.EmitHoistedCheck(ta, ea, c);
        ta->nobc = ea->nobc = true;
    }
    cg.L(lbl.empty() ? cat("if (", c, ") {") : "{");
    cg.ind++;
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.GenBlockInner(thenb, d);
    cg.PopSc();
    cg.ind--;
    if (elseb) {
        string endlbl;
        if (lbl.empty()) {
            cg.L("} else {");
        } else {
            endlbl = cg.Lbl();
            cg.L("}");
            cg.L("goto ", endlbl, ";");
            cg.L(lbl, ":;");
            cg.L("{");
        }
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (auto b = Is<Block>(elseb)) cg.GenBlockInner(b, d);
        else cg.GenAny(elseb, d);
        cg.PopSc();
        cg.ind--;
        cg.L("}");
        if (!endlbl.empty()) cg.L(endlbl, ":;");
    } else {
        cg.L("}");
        if (!lbl.empty()) cg.L(lbl, ":;");
    }
    cg.termjump = false;
    
    if (hoist) ta->nobc = ea->nobc = false;
}

inline void MatchExpr::CgAny(CodeGen &cg, const Dst &d) {
    auto st = scrutinee->exprtype;
    TypeExpr *enumtype = nullptr;
    auto isref = false;
    if (st->kind == TY_REF && st->ref->sub->kind == TY_ENUM) {
        enumtype = st->ref->sub;
        isref = true;
    } else if (st->kind == TY_ENUM) {
        enumtype = st;
    }
    if (!enumtype) {   
        auto x = cg.GenPure(scrutinee);
        
        
        auto sct = cg.CT(scrutinee->exprtype);
        auto k = [&](int64_t v) { return cat("(", sct, ")", cg.IntStr(v)); };
        auto xs = cat("(", sct, ")(", x, ")");
        auto first = true;
        for (auto &arm : arms) {
            if (arm.pat.kind == P_WILDCARD) {
                cg.L(first ? "{" : "} else {");
            } else {
                auto several = arm.ranges.size() > 1;
                string c;
                for (auto &r : arm.ranges) {
                    if (!c.empty()) c += " || ";
                    if (r.hi == r.lo) Append(c, xs, " == ", k(r.lo));
                    else Append(c, several ? "(" : "", xs, " >= ", k(r.lo), " && ", xs, " <= ",
                                k(r.hi), several ? ")" : "");
                }
                cg.L(first ? "" : "} else ", "if (", c, ") {");
            }
            first = false;
            cg.ind++;
            cg.PushSc(CodeGen::SC_PLAIN);
            cg.GenAny(arm.body, d);
            cg.PopSc();
            cg.ind--;
        }
        cg.L("}");
        cg.termjump = false;
        return;
    }
    auto varmode = enumtype->enu->varmode;
    auto ei = cg.EIOf(enumtype);
    auto ts = cg.TagSize(ei->en);
    string p, sv, tag;
    if (varmode || isref) {
        if (cg.IsResz(enumtype)) {
            auto lv = cg.GenLoc(scrutinee);
            if (lv.t->kind == TY_REF) cg.DerefLoc(lv);
            p = lv.s;   
        } else if (isref) {
            auto x = cg.GenPure(scrutinee);
            p = varmode ? x : cat("((uint8_t *)", x, ")");
        } else {
            p = cg.GenPtr(scrutinee);
        }
        tag = varmode ? cat("*(", cg.IntCT(cg.TagStore(ei->en)), " *)", p)
                      : cat("((", cg.CT(enumtype), " *)", p, ")->tag");
    } else {
        
        
        sv = cg.GenX(scrutinee);
        if (!cg.IsCName(sv)) sv = cg.Snapshot(scrutinee->exprtype, sv);
        tag = cat(sv, ".tag");
    }
    cg.L("switch (", tag, ") {");
    auto haswild = false;
    for (auto &arm : arms) {
        if (arm.pat.kind == P_WILDCARD) {
            haswild = true;
            cg.L("default: {");
        } else {
            string cases;
            for (auto v : arm.variants)
                Append(cases, "case ", cg.TagConst(ei, ei->en->VariantIndex(v)), ": ");
            cg.L(cases, "{");
        }
        cg.ind++;
        cg.PushSc(CodeGen::SC_PLAIN);
        if (arm.binder) {
            auto variant = arm.variants[0];     
            auto vi = ei->en->VariantIndex(variant);
            auto vt = cg.VariantType(enumtype, vi);
            auto bn = cg.LocalName(arm.binder);
            string payload = varmode
                ? cat("(", p, " + ", ts, ")")
                : (isref ? cat("((uint8_t *)&((", cg.CT(enumtype), " *)", p, ")->u.v_",
                               cg.Sanitize(variant->name), ")")
                         : "");
            if (arm.pat.byref) {
                
                if (cg.IsBytesT(vt)) cg.L("uint8_t *", bn, " = ", payload, ";");
                else cg.L(cg.CT(vt), " *", bn, " = (", cg.CT(vt), " *)(", payload, ");");
            } else if (cg.IsBytesT(vt)) {
                
                auto stk = cg.AllocStk(true);
                cg.L("uint8_t *", bn, " = ", cg.Top(stk), ";");
                cg.SaveBase(true, stk, bn);
                cg.vstk[arm.binder] = stk;
                auto sz = cg.T();
                cg.L("int64_t ", sz, " = ", cg.SizeX(vt, payload), ";");
                cg.L("GS_MEMCPY(", cg.Top(stk), ", ", payload, ", (size_t)", sz, ");");
                cg.Bump(stk, sz);
            } else if (EmptyLayout(variant->fields)) {
                cg.FixedLocal(vt, bn, "{0}");
            } else if (!payload.empty()) {
                cg.FixedLocal(vt, bn, cat("*(", cg.CT(vt), " *)(", payload, ")"), true);
            } else {
                cg.FixedLocal(vt, bn, cat(sv, ".u.v_", cg.Sanitize(variant->name)), true);
            }
            cg.vnames[arm.binder] = bn;
        }
        cg.GenAny(arm.body, d);
        cg.PopSc();
        cg.ind--;
        cg.L("} break;");
    }
    if (!haswild)
        cg.L("default: GS_UNREACHABLE(", cg.LocArgs(line), ");");
    cg.L("}");
    cg.termjump = false;
}

inline void EarlyBlock::CgAny(CodeGen &cg, const Dst &d) {
    cg.PushSc(CodeGen::SC_BLOCK);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.L("{");
    cg.ind++;
    cg.EnterDst(si, d);
    cg.GenBlockInner(body, d);
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    if (brk) cg.L(lbl, ":;");
    cg.termjump = false;
}

inline void LoopExpr::CgAny(CodeGen &cg, const Dst &d) {
    CodeGen::ViewScope vs(cg, hoistrefs, hoistfields);
    cg.GenLoopBody({}, body, d);
}

inline void InlineBlock::CgAny(CodeGen &cg, const Dst &d) {
    
    
    if (auto from = cg.AdtFrom(this)) {
        cg.GenAdtAdapted(from, exprtype, d, line, [&](const Dst &nd) { EmitBody(cg, nd); });
        return;
    }
    
    
    
    
    
    auto rt = spec && spec->rets.size() == 1 ? spec->rets[0] : nullptr;
    auto want = d.t ? d.t : exprtype;
    if (d.k != DK_DISCARD && rt && rt->kind == TY_ARRAY && want->kind == TY_SLICE) {
        CodeGen::Loc lv;
        if (cg.IsResz(rt)) {
            string stk;
            auto h = cg.RzTemp(rt, stk);
            EmitBody(cg, Dst { DK_STACK, stk, rt, cg.RzLenLv(rt, h) });
            lv = cg.RzTempLoc(rt, h, stk);
        } else if (cg.IsBytesT(rt)) {
            string stk;
            auto base = cg.BytesTemp(stk);
            EmitBody(cg, Dst { DK_STACK, stk, rt });
            lv = cg.BytesLoc(base, rt, CodeGen::Loc {});
        } else {
            lv.t = rt;
            lv.val = true;
            lv.s = cg.T();
            cg.FixedLocal(rt, lv.s);
            EmitBody(cg, Dst { DK_LVALUE, lv.s, rt });
        }
        auto x = cg.LoadLoc(lv, want, line);
        if (d.k == DK_LVALUE) cg.L(d.s, " = ", x, ";");
        else cg.EmitValStore(d.s, want, x);
        return;
    }
    EmitBody(cg, d);
}

inline void InlineBlock::EmitBody(CodeGen &cg, const Dst &d) {
    auto named = cg.OpenIbNrvo(this, d);
    
    if (named) cg.openat[d.s]++;
    
    
    auto first = cg.GenInlineArgs(body);
    cg.PushSc(CodeGen::SC_IB);
    auto si = (int)cg.cscopes.size() - 1;
    cg.cscopes[si].ibsf = sf;
    cg.cscopes[si].brklbl = cg.Lbl();
    cg.PushSc(CodeGen::SC_PLAIN);
    cg.L("{");
    cg.ind++;
    cg.EnterDst(si, d);
    
    if (named) cg.cscopes[si].open0--;
    cg.GenBlockInner(body, d, first, si);
    cg.PopSc();
    cg.ind--;
    cg.L("}");
    auto brk = cg.cscopes.back().usedbrk;
    auto lbl = cg.cscopes.back().brklbl;
    cg.PopSc();
    if (brk) cg.L(lbl, ":;");
    if (named) {
        cg.nrvo.erase(named);
        cg.openat[d.s]--;
    }
    cg.termjump = false;
}

inline void Call::CgAny(CodeGen &cg, const Dst &d) {
    
    
    
    if (d.k == DK_STACK) {
        cg.GenConstruct(this, d.s, d.t, d.lenlv);
        return;
    }
    
    if (auto from = cg.AdtFrom(this)) {
        cg.GenAdtAdapted(from, exprtype, d, line, [&](const Dst &nd) { cg.GenCallAs(this, from, nd); });
        return;
    }
    auto rets = cg.EmitCall(this, d);
    
    
    
    
    
    
    if (!rets.empty() && !cg.IsVoidT(exprtype) &&
        (!cg.IsBytesT(exprtype) || IsVarintT(exprtype) || exprtype->kind == TY_REF)) {
        auto r0 = cg.CallVal0(this, rets[0], d.t);
        if (d.k == DK_LVALUE && d.t && IsRelRef(d.t))
            cg.EmitRelStoreAt(cat("(uint8_t *)&", d.s), d.t, r0, line, false);
        else if (d.k == DK_LVALUE && r0 != d.s) cg.L(d.s, " = ", r0, ";");
    }
}

inline void IntLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void FltLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void BoolLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StrLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void NullLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Ident::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void ArrayLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void StructLit::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Unary::CgAny(CodeGen &cg, const Dst &d) {
    
    
    if (d.k == DK_LVALUE && op == T_MINUS && exprtype &&
        (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY)) {
        cg.GenElemwiseNegInto(exprtype, line, cg.GenVal(child), d.s);
        return;
    }
    cg.LeafAny(this, d);
}
inline void Binary::CgAny(CodeGen &cg, const Dst &d) {
    
    
    
    if (d.k == DK_LVALUE && exprtype &&
        (exprtype->kind == TY_STRUCT || exprtype->kind == TY_ARRAY)) {
        string l, r;
        cg.ElemwiseOperands(this, l, r);
        cg.GenElemwiseInto(exprtype, op, line, l, r, d.s,
                           cg.OperandT(left->exprtype)->kind != exprtype->kind,
                           cg.OperandT(right->exprtype)->kind != exprtype->kind);
        return;
    }
    cg.LeafAny(this, d);
}
inline void Dot::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void Index::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void SliceExpr::CgAny(CodeGen &cg, const Dst &d) {
    
    if (d.k == DK_LVALUE && !d.pool && d.t && d.t->kind == TY_SLICE &&
        exprtype->kind == TY_SLICE) {
        auto dct = cg.CT(d.t);
        if (dct == cg.CT(exprtype) && CodeGen::IsCName(d.s)) {
            cg.GenSlice(this, d.s);
            return;
        }
    }
    cg.LeafAny(this, d);
}
inline void AsCast::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }
inline void RangeExpr::CgAny(CodeGen &cg, const Dst &d) { cg.LeafAny(this, d); }


inline void While::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void ForLoop::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Return::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Break::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Continue::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Defer::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FunVal::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: FunVal emitted"); }
inline void SelfRef::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: self emitted"); }
inline void VarDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void Assign::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void IncDec::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void FnDecl::CgAny(CodeGen &cg, const Dst &) { cg.GenStmt2(this); }
inline void StructDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void EnumDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }
inline void AliasDecl::CgAny(CodeGen &cg, const Dst &) { cg.Fail(line, "internal: decl emitted"); }



inline void VarDecl::CgStmt(CodeGen &cg) {
    
    
    if (names.size() > 1 && inits.size() == 1) {
        auto c = Is<Call>(inits[0]);
        assert(c);
        vector<Dst> dsts;
        for (auto d : defs) {
            if (cg.IsResz(d->type) || cg.IsBytesT(d->type)) {
                dsts.push_back(cg.DeclStackLocal(d, true));
            } else {
                auto name = cg.LocalName(d);
                cg.FixedLocal(d->type, name, "", true);
                cg.vnames[d] = name;
                dsts.push_back(Dst { DK_LVALUE, name, d->type });
            }
        }
        cg.EmitCallInto(c, dsts);
        return;
    }
    for (size_t i = 0; i < defs.size(); i++)
        cg.BindLocal(defs[i], i < inits.size() ? inits[i] : nullptr, !inline_arg);
}

inline void Assign::CgStmt(CodeGen &cg) {
    
    
    auto quiet = op == T_ASSIGN && !pointee && cg.QuietValue(rhs) && !cg.HasStmts(lval);
    auto lv = quiet || !cg.StableDest(lval) ? cg.GenLocNow(lval) : cg.GenLoc(lval);
    if (pointee) cg.DerefLoc(lv);
    
    
    
    
    
    
    
    if (lv.val) {
        if (!quiet && (lv.viaref || lv.ispref || !cg.StableDest(lval))) {
            auto p = cg.T();
            cg.L(lv.ispref ? string("gs_pref") : cg.CT(lv.t), " *", p, " = &(", lv.s, ");");
            lv.s = cat("(*", p, ")");
        }
    } else if (!cg.IsResz(lv.t)) {
        auto p = cg.T();
        cg.L("uint8_t *", p, " = ", lv.s, ";");
        lv.s = p;
    } else if (lv.viaref && !lv.lenlv.empty()) {
        auto p = cg.T();
        cg.L("gs_i64 *", p, " = &(", lv.lenlv, ");");
        lv.lenlv = cat("(*", p, ")");
    }
    if (lv.viaref && cg.IsResz(lv.t) && !lv.stk.empty() && !cg.CacheableStk(lv.stk)) {
        auto s = cg.T();
        cg.L("gs_stack *", s, " = ", lv.stk, ";");
        lv.stk = s;
    }
    if (op == T_DOTASSIGN) { cg.GenRebind(this, lv); return; }
    
    
    
    
    if (op != T_ASSIGN) {
        assert(lv.val);
        auto old = cg.HasStmts(rhs) ? cg.Snapshot(lv.t, lv.s) : lv.s;
        if (lv.t->kind == TY_STRUCT || lv.t->kind == TY_ARRAY) {
            auto r = cg.GenPureVal(rhs);
            cg.GenElemwiseInto(lv.t, op, line, old, r, lv.s, false,
                               cg.OperandT(rhs->exprtype)->kind != lv.t->kind);
            return;
        }
        
        
        
        
        auto r = cg.GenX(rhs);
        auto x = [&] { return cg.ArithX(op, lv.t, old, r, line, false, old, cat("(", r, ")")); };
        CodeGen::Division dv;
        cg.L(lv.s, " = ", CodeGen::AsDivision(this, dv) ? cg.FixedDivX(dv, old, x, line) : x(),
             ";");
        return;
    }
    
    auto t = lv.t;
    if (IsRelRef(t)) {
        
        cg.GenRelAssign(lv, lval, rhs, line);
        return;
    }
    if (cg.IsResz(t)) {
        
        
        if (cg.IsFrameObj(t) && lv.val) {
            assert(!lv.stk.empty());
            cg.L(cg.TopW(lv.stk), " = ", cg.FoTailHdr(t, lv.s), ".base;");
            cg.GenConstruct(rhs, lv.stk, lv.t, lv.s);
            return;
        }
        assert(!lv.val && !lv.stk.empty() && !lv.lenlv.empty());
        cg.L(cg.TopW(lv.stk), " = ", lv.s, ";");
        
        if (cg.IsFrameObj(t)) cg.GenFoAsBytes(rhs, lv.stk, t, lv.lenlv);
        else cg.GenConstruct(rhs, lv.stk, lv.t, lv.lenlv);
        return;
    }
    if (!lv.val) {
        
        assert(t->kind == TY_ARRAY && t->arr->akind == A_LIMITED);
        string srcstk;
        auto src = cg.GenPtr(rhs, &srcstk);
        auto nn = cg.T();
        cg.L("int64_t ", nn, " = (int64_t)*(gs_u32 *)(", src, " + 4);");
        cg.L("if (", nn, " > ", cg.LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ",
             cg.LocArgs(line), ");");
        cg.L("*(gs_u32 *)((", lv.s, ") + 4) = (uint32_t)", nn, ";");
        cg.L("GS_MEMCPY((", lv.s, ") + 8, ", src, " + 8, (size_t)(", nn, " * ",
          cg.FixedSize(t->arr->sub), "));");
        return;
    }
    
    
    
    if (Is<Ident>(rhs) && !lv.ispref && cg.WholeStore(t) &&
        (t->kind != TY_ARRAY || t->arr->akind == A_FIXED)) {
        auto x = cg.GenXD(rhs, t);
        if (CodeGen::IsCName(x)) cg.StoreWhole(cat("&", lv.s), t, x);
        else cg.L(lv.s, " = ", x, ";");
        return;
    }
    cg.GenAny(rhs, Dst { DK_LVALUE, lv.s, lv.t });
}

inline void IncDec::CgStmt(CodeGen &cg) {
    auto lv = cg.GenLoc(lval);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv);
    assert(lv.val);
    cg.L(lv.s, " = ", cg.ArithX(op == T_INC ? T_PLUS : T_MINUS, lv.t, lv.s, "1", line), ";");
}

inline void FnDecl::CgStmt(CodeGen &) {}   
inline void While::CgStmt(CodeGen &cg) {
    Dst d;
    CodeGen::ViewScope vs(cg, hoistrefs, hoistfields);
    if (countdown) {
        
        
        
        auto c = Is<Binary>(cond);
        auto var = Is<Ident>(c->op == T_GT ? c->left : c->right);
        auto lv = cg.GenLoc(var);
        auto ct = cg.CT(var->vdef->type);
        auto n = cg.T(), j = cg.T();
        cg.L("/* loop shape: countdown counted up */");
        cg.L(ct, " ", n, " = ", lv.s, ";");
        cg.GenLoopBody([&]() { cg.L(lv.s, " = (", ct, ")(", n, " - 1 - ", j, ");"); }, body, d,
                       cat("for (", ct, " ", j, " = 0; ", j, " < ", n, "; ", j, "++) {"), nullptr,
                       1);
        return;
    }
    cg.GenLoopBody([&]() {
        
        
        auto si = (int)cg.cscopes.size() - 1;
        assert(cg.cscopes[si].saves.empty());
        cg.GenBranch(cond, false, cg.cscopes[si].brklbl);
        cg.cscopes[si].usedbrk = true;
    }, body, d, "", cond);
}

inline void ForLoop::CgStmt(CodeGen &cg) {
    if (cg.BulkDefaultLoop(this)) return;
    auto d = Dst {};
    CodeGen::ViewScope vs(cg, hoistrefs, hoistfields);
    auto iv = cg.LocalName(vdef);
    auto ix = idxdef ? cg.LocalName(idxdef) : "";
    if (iterkind == IK_RANGE || iterkind == IK_COUNT) {
        
        
        
        
        auto r = Is<RangeExpr>(iter);
        auto ct = r ? r->exprtype : iter->exprtype;
        auto ict = cg.CT(ct);
        string lov = "0";
        if (r) {
            auto lo = cg.GenX(r->lo);
            lov = cg.T();
            cg.L(ict, " ", lov, " = ", lo, ";");
        }
        auto hi = cg.GenX(r ? r->hi : iter);
        auto hiv = cg.T();
        cg.L(ict, " ", hiv, " = ", hi, ";");
        if (!ix.empty()) cg.L("int64_t ", ix, " = 0;");
        auto wide = ct->intstorage != vdef->type->intstorage;
        auto ctr = wide ? cg.T() : iv;
        auto bind = [&]() {
            if (wide) cg.L(cg.CT(vdef->type), " ", iv, " = (", cg.CT(vdef->type), ")", ctr, ";");
        };
        auto step = cat(ctr, "++", ix.empty() ? "" : cat(", ", ix, "++"));
        if (auto k = cg.BlockSize(this)) {
            auto lit = r ? Is<IntLit>(r->lo) : nullptr;
            auto fromzero = !r || (lit && !lit->uns && lit->val == 0);
            cg.L(ict, " ", ctr, " = ", lov, ";");
            cg.GenBlocked(this, k, cg.AtLeastLeft(ct, hiv, ctr, k, fromzero), step, bind,
                          cat(ctr, " < ", hiv));
            return;
        }
        cg.GenLoopBody(bind, body, d,
            cat("for (", ict, " ", ctr, " = ", lov, "; ", ctr, " < ", hiv, "; ", step, ") {"),
            nullptr, 0, { vdef, idxdef });
        return;
    }
    
    
    
    
    
    
    auto lv = cg.GenLoc(iter);
    if (lv.t->kind == TY_REF) cg.DerefLoc(lv);
    auto v = cg.ArrayView(lv);
    
    
    
    
    
    
    
    auto memlen = v.len.find('*') != string::npos || v.len.find("->") != string::npos;
    auto hoist = [&]() {
        auto nv = cg.T();
        cg.L("int64_t ", nv, " = ", v.len, ";");
        auto bv = cg.T();
        if (v.typedelems) cg.L(cg.CT(v.elem), " *", bv, " = ", v.elems, ";");
        else cg.L("uint8_t *", bv, " = (uint8_t *)(", v.elems, ");");
        v.len = nv;
        v.elems = bv;
    };
    if (fixedlen && memlen) hoist();
    
    
    
    
    auto moves = !fixedlen && (lv.t->kind == TY_SLICE || lv.viaref);
    
    auto typedix = idxdef && idxdef->type->intstorage != IS_I64;
    auto gi = ix.empty() || typedix ? cg.T() : ix;
    auto bindix = [&]() {
        if (typedix)
            cg.L(cg.CT(idxdef->type), " ", ix, " = (", cg.CT(idxdef->type), ")", gi, ";");
    };
    auto et = vdef->type;
    
    
    auto held = v.elem->kind == TY_REF;
    auto fixedarr = lv.t->kind == TY_ARRAY && lv.t->arr->akind == A_FIXED;
    
    
    
    
    
    
    
    auto bound = cat("(", v.len, ")");
    auto exitlen = false;
    auto setbound = [&]() {
        if (fixedarr || lenbound < 0 || lenexact || lenbound > CodeGen::MAXTRIPBOUND ||
            CountNodes(body) > CodeGen::MAXTRIPBODY)
            return;
        bound = cat(lenbound);
        exitlen = true;
        cg.L("/* loop shape: trip count bounded by ", lenbound, " */");
    };
    auto testlen = [&]() {
        if (!exitlen) return;
        auto si = (int)cg.cscopes.size() - 1;
        cg.L("if (", gi, " >= (", v.len, ")) goto ", cg.cscopes[si].brklbl, ";");
        cg.cscopes[si].usedbrk = true;
    };
    if (!cg.IsFix(v.elem)) {
        
        
        auto p = cg.T();
        cg.L("uint8_t *", p, " = (uint8_t *)(", v.elems, ");");
        
        string at;
        if (moves) {
            at = cg.T();
            cg.L("uint8_t *", at, " = ", p, ";");
        }
        setbound();
        cg.GenLoopBody([&]() {
            testlen();
            if (moves) {
                auto e = cg.T();
                cg.L("uint8_t *", e, " = (uint8_t *)(", cg.ArrayView(lv).elems, ");");
                cg.L("if (", e, " != ", at, ") {");
                cg.ind++;
                cg.L(at, " = ", e, ";");
                cg.L(p, " = ", e, ";");
                auto k = cg.T();
                cg.L("for (int64_t ", k, " = 0; ", k, " < ", gi, "; ", k, "++) ", p, " += ",
                     cg.SizeX(v.elem, p), ";");
                cg.ind--;
                cg.L("}");
            }
            bindix();
            if (held) {
                CodeGen::Loc el;
                el.t = v.elem;
                el.s = p;
                auto vct = cg.CT(et), val = cg.LoadLoc(el, et, line);
                cg.L(vct, " ", iv, " = ", val, ";");
            } else if (!vdef->copybind) {
                cg.L("uint8_t *", iv, " = ", p, ";");
            }
        }, body, d,
            cat("for (int64_t ", gi, " = 0; ", gi, " < ", bound, "; ", gi, "++, ", p,
                " += ", cg.SizeX(v.elem, p), ") {"), nullptr, 0, { vdef, idxdef });
        return;
    }
    auto esz = cg.FixedSize(v.elem);
    auto nbinds = 0;
    string at;   
    auto bind = [&]() {
        
        
        
        if (nbinds++) {
            iv = cg.Unique2(cg.Sanitize(vdef->name));
            cg.vnames[vdef] = iv;
        }
        bindix();
        
        
        auto elems = moves ? cg.ArrayView(lv).elems : v.elems;
        string elem = !at.empty() ? (v.typedelems ? cat("(*", at, ")")
                                                  : cat("(*(", cg.CT(v.elem), " *)", at, ")"))
                      : v.typedelems
                          ? cat(elems, "[", gi, "]")
                          : cat("(*(", cg.CT(v.elem), " *)((", elems, ") + ", gi, " * ",
                                esz, "))");
        if (!vdef->copybind && !held) {
            cg.L(cg.CT(et), " ", iv, " = &", elem, ";");
        } else if (IsRelRef(v.elem)) {
            
            
            auto fa = cg.T();
            cg.L("uint8_t *", fa, " = (uint8_t *)&", elem, ";");
            auto off = cg.T();
            cg.L("int64_t ", off, " = (int64_t)*(", cg.RelCT(v.elem), " *)(", fa, ");");
            auto target = cat("(", cg.CT(et), ")(", cg.RelOrigin(v.elem, fa), " + ", off, ")");
            if (v.elem->ref->optional)
                cg.L(cg.CT(et), " ", iv, " = ", off, " ? ", target, " : NULL;");
            else
                cg.L(cg.CT(et), " ", iv, " = ", target, ";");
        } else {
            cg.FixedLocal(et, iv, elem, true);
            cg.vnames[vdef] = iv;
        }
    };
    
    
    
    if (!moves && (fixedlen || fixedarr)) {
        if (auto k = cg.BlockSize(this); k && (lenbound < 0 || lenbound >= 2 * k)) {
            cg.L("int64_t ", gi, " = 0;");
            cg.GenBlocked(this, k, cat("(", v.len, ") - ", gi, " >= ", k), cat(gi, "++"), bind,
                          cat(gi, " < (", v.len, ")"));
            return;
        }
    }
    setbound();
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (!idxdef && fixedlen && !fixedarr && !exitlen && CodeGen::CallsRecursive(body)) {
        if (!memlen) hoist();
        at = v.elems;
        auto endp = cg.T();
        cg.L("if (", v.len, " > 0) {");
        cg.ind++;
        cg.GenLoopBody(bind, body, d,
            v.typedelems
                ? cat("for (", cg.CT(v.elem), " *", endp, " = ", at, " + ", v.len, "; ", at,
                      " < ", endp, "; ", at, "++) {")
                : cat("for (uint8_t *", endp, " = ", at, " + ", v.len, " * ", esz, "; ", at,
                      " < ", endp, "; ", at, " += ", esz, ") {"),
            nullptr, 0, { vdef, idxdef });
        cg.ind--;
        cg.L("}");
        return;
    }
    cg.GenLoopBody([&]() {
        testlen();
        bind();
    }, body, d, cat("for (int64_t ", gi, " = 0; ", gi, " < ", bound, "; ", gi, "++) {"),
       nullptr, 0, { vdef, idxdef });
}

inline void Return::CgStmt(CodeGen &cg) {
    
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_IB && cg.cscopes[i].ibsf == target) {
            
            assert(vals.size() <= 1);
            if (!vals.empty()) cg.GenExitValue(vals[0], i);
            cg.EmitExitRestores(i);
            cg.cscopes[i].usedbrk = true;
            cg.L("goto ", cg.cscopes[i].brklbl, ";");
            cg.termjump = true;
            return;
        }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    if (cg.curspec && target == cg.curspec->sf) {
        cg.GenNormalReturn(vals);
        return;
    }
    cg.GenFromReturn(this);
}

inline void Break::CgStmt(CodeGen &cg) { cg.GenBreakPath(val); }
inline void Defer::CgStmt(CodeGen &cg) { cg.RegisterDefer(this); }
inline void Continue::CgStmt(CodeGen &cg) {
    auto si = -1;
    for (auto i = (int)cg.cscopes.size() - 1; i >= 0; i--) {
        if (cg.cscopes[i].kind == CodeGen::SC_LOOP) { si = i; break; }
        if (cg.cscopes[i].kind == CodeGen::SC_FN) break;
    }
    assert(si >= 0);
    
    
    cg.EmitExitRestores(si);
    cg.cscopes[si].usedcnt = true;
    cg.L("goto ", cg.cscopes[si].cntlbl, ";");
    cg.termjump = true;
}


inline void IntLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FltLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void BoolLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StrLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void NullLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Ident::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void ArrayLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void StructLit::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Unary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Binary::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Dot::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Index::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void SliceExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void AsCast::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void RangeExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Call::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void Block::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void IfExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void MatchExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void EarlyBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void LoopExpr::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void InlineBlock::CgStmt(CodeGen &cg) { cg.GenAny(this, Dst {}); }
inline void FunVal::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: FunVal statement"); }
inline void SelfRef::CgStmt(CodeGen &cg) { cg.Fail(line, "internal: self statement"); }
inline void StructDecl::CgStmt(CodeGen &) {}
inline void EnumDecl::CgStmt(CodeGen &) {}
inline void AliasDecl::CgStmt(CodeGen &) {}

}  
