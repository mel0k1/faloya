

#pragma once

namespace faloya {

inline void Indent(string &s, int ind) { s.append((size_t)ind * 4, ' '); }
inline void NL(string &s, int ind) { s += '\n'; Indent(s, ind); }

inline void EscapeString(string &s, string_view v, char quote) {
    s += quote;
    for (auto c : v) {
        switch (c) {
            case '\n': s += "\\n"; break;
            case '\t': s += "\\t"; break;
            case '\r': s += "\\r"; break;
            case 0:    s += "\\0"; break;
            case '\\': s += "\\\\"; break;
            case '"':  s += "\\\""; break;
            case '\'': s += "\\\'"; break;
            default:
                
                
                if ((uint8_t)c < 32 || c == 127) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", (uint8_t)c);
                    s += buf;
                } else {
                    s += c;
                }
        }
    }
    s += quote;
}

inline void DumpGenerics(string &s, const vector<GenericParam> &generics) {
    if (generics.empty()) return;
    s += "<";
    for (size_t i = 0; i < generics.size(); i++) {
        if (i) s += ", ";
        s += generics[i].name;
        if (generics[i].bound) { s += ": "; generics[i].bound->Dump(s); }
    }
    s += ">";
}

inline void TypeExpr::Dump(string &s, size_t limit) const {
    
    
    
    if (s.size() >= limit) return;
    
    
    
    
    
    auto chainhasrs = [](const TypeExpr *p) {
        for (; p; p = p->kind == TY_ARRAY ? p->arr->sub : nullptr)
            if (IsRefOrSlice(p)) return true;
        return false;
    };
    if (cq) s += "const ";
    auto SubDump = [&](const TypeExpr *inner) {
        
        auto parens = inner->kind == TY_FN || inner->cq || inner->ix ||
                      (cq && chainhasrs(inner));
        if (parens) s += "(";
        inner->Dump(s, limit);
        if (parens) s += ")";
    };
    auto ArgsDump = [&](const vector<TypeExpr *> &ts) {
        if (ts.empty()) return;
        s += "<";
        for (size_t i = 0; i < ts.size(); i++) {
            if (i) s += ", ";
            ts[i]->Dump(s, limit);
        }
        s += ">";
    };
    switch (kind) {
        case TY_INT:
            s += IntStorageName(intstorage);
            if (ix) Append(s, ix->upto ? " upto " : " in ", ix->name);
            break;
        case TY_FLT:  s += FltStorageName(fltstorage); break;
        case TY_BOOL: s += "bool"; break;
        case TY_STRUCT: s += struc->st->qname; ArgsDump(struc->args); break;
        case TY_ENUM:
            s += enu->en->qname;
            ArgsDump(enu->args);
            if (enu->varmode) s += "..";
            break;
        case TY_GENERIC:
        case TY_UNRESOLVED:
            s += named->name;
            ArgsDump(named->args);
            if (named->varmode) s += "..";
            break;
        case TY_FN:
            s += "fn";
            if (fn->has_sig) {
                s += "(";
                for (size_t i = 0; i < fn->args.size(); i++) {
                    if (i) s += ", ";
                    fn->args[i]->Dump(s, limit);
                }
                s += ")";
                if (!fn->rets.empty()) {
                    s += " -> ";
                    if (fn->rets.size() > 1) s += "(";
                    for (size_t i = 0; i < fn->rets.size(); i++) {
                        if (i) s += ", ";
                        fn->rets[i]->Dump(s, limit);
                    }
                    if (fn->rets.size() > 1) s += ")";
                }
            }
            break;
        case TY_ARRAY:
            SubDump(arr->sub);
            switch (arr->akind) {
                case A_FIXED:
                    s += "[";
                    
                    
                    if (arr->sizeexpr) arr->sizeexpr->Dump(s, 0);
                    else s += std::to_string(arr->size);
                    s += "]";
                    break;
                case A_VAR:
                    s += "[";
                    if (arr->lenstorage >= 0) s += IntStorageName(arr->lenstorage);
                    s += "]";
                    break;
                case A_LIMITED:
                    s += "[..";
                    if (arr->sizeexpr) arr->sizeexpr->Dump(s, 0);
                    s += "]";
                    break;
                case A_GROW:       s += "[>..]";  break;
                case A_GROWSHRINK: s += "[>..<]"; break;
            }
            break;
        case TY_SLICE: SubDump(sub); s += "[:]"; break;
        case TY_REF:
            SubDump(ref->sub);
            if (ref->lenstorage >= 0) {
                Append(s, "&<", IntStorageName(ref->lenstorage));
                if (!ref->poolname.empty()) Append(s, " in ", ref->poolname);
                s += ">";
                if (ref->optional) s += "?";
            } else {
                s += ref->optional ? "?" : "&";  
            }
            break;
        case TY_VARIANT:
            SubDump(var->adt);
            s += ".";
            s += var->name;
            break;
        case TY_VOID: s += "void"; break;
    }
}





inline const VarDecl *MultiAssignOf(const Block *b) {
    auto vd = b->stmts.empty() ? nullptr : Is<VarDecl>(b->stmts[0]);
    if (!vd || !vd->results || b->tail || vd->inits.size() != 1 ||
        b->stmts.size() != vd->names.size() + 1)
        return nullptr;
    for (size_t i = 1; i < b->stmts.size(); i++) {
        auto a = Is<Assign>(b->stmts[i]);
        if (!a || a->op != T_ASSIGN) return nullptr;
    }
    return vd;
}

inline bool EndsInBlock(const Node *n) {
    if (auto b = Is<Block>(n); b && MultiAssignOf(b)) return false;
    if (Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<EarlyBlock>(n) || Is<While>(n) ||
        Is<LoopExpr>(n) || Is<ForLoop>(n) || Is<FnDecl>(n) || Is<Block>(n) ||
        Is<InlineBlock>(n) || Is<ConstFor>(n) || Is<ConstIf>(n)) return true;
    if (auto c = Is<Call>(n)) return c->trailing != nullptr;
    
    if (auto d = Is<Defer>(n))
        return d->body->stmts.size() != 1 || d->body->tail || EndsInBlock(d->body->stmts[0]);
    return false;
}





inline thread_local bool dumpwritten = false;



inline const Node *Written(const Node *n) {
    if (!dumpwritten) return n;
    if (auto a = Is<AsCast>(n); a && a->implicit) return Written(a->child);
    auto u = Is<Unary>(n);
    return u && u->synth ? u->child : n;
}







inline void DumpOperand(string &s, const Node *n, int ind, bool postfix = false) {
    n = Written(n);
    auto parens = EndsInBlock(n) || Is<StructLit>(n) ||
                  Is<Return>(n) || Is<Break>(n) || Is<Continue>(n) ||
                  (postfix && (Is<Unary>(n) || Is<IntLit>(n) || Is<FltLit>(n)));
    if (parens) s += "(";
    n->Dump(s, ind);
    if (parens) s += ")";
}



inline void DumpBlockBody(string &s, const Block *b, int ind) {
    for (auto st : b->stmts) {
        NL(s, ind + 1);
        st->Dump(s, ind + 1);
        if (!EndsInBlock(st)) s += ";";
    }
    if (b->tail) {
        NL(s, ind + 1);
        b->tail->Dump(s, ind + 1);
    }
    NL(s, ind);
    s += "}";
}

inline void Block::Dump(string &s, int ind) const {
    if (auto vd = MultiAssignOf(this)) {
        for (size_t i = 1; i < stmts.size(); i++) {
            if (i > 1) s += ", ";
            Is<Assign>(stmts[i])->lval->Dump(s, ind);
        }
        s += " = ";
        vd->inits[0]->Dump(s, ind);
        return;
    }
    s += "{";
    DumpBlockBody(s, this, ind);
}

inline void IntLit::Dump(string &s, int) const {
    if (!text.empty()) s += text;
    else if (uns) Append(s, (uint64_t)val);
    else Append(s, val);
}

inline void FltLit::Dump(string &s, int) const {
    if (!text.empty()) { s += text; return; }
    CatOne(s, val);
}

inline void BoolLit::Dump(string &s, int) const { s += val ? "true" : "false"; }
inline void StrLit::Dump(string &s, int) const { EscapeString(s, val, '"'); }
inline void Ident::Dump(string &s, int) const { s += name; }

inline void ArrayLit::Dump(string &s, int ind) const {
    s += "[";
    if (capexpr) {
        s += "..";
        capexpr->Dump(s, ind);
        s += "]";
        return;
    }
    if (fillval) {
        fillval->Dump(s, ind);
        s += "; ";
        fillcount->Dump(s, ind);
    } else {
        for (size_t i = 0; i < elems.size(); i++) {
            if (i) s += ", ";
            elems[i]->Dump(s, ind);
        }
    }
    s += "]";
}

inline void StructLit::Dump(string &s, int ind) const {
    type->Dump(s);
    s += " { ";
    for (size_t i = 0; i < inits.size(); i++) {
        if (i) s += ", ";
        if (!inits[i].name.empty()) Append(s, inits[i].name, ": ");
        inits[i].val->Dump(s, ind);
    }
    if (defaultall) s += inits.empty() ? ".." : ", ..";
    s += inits.empty() && !defaultall ? "}" : " }";
}

inline void DumpArms(string &s, const vector<MatchArm> &arms, int ind);
inline void DumpFragments(string &s, int ind, const Block *body, bool armfragments,
                          const vector<MatchArm> &arms, const vector<FieldInit> &inits);

inline void ConstFor::Dump(string &s, int ind) const {
    s += "const for <";
    for (size_t i = 0; i < names.size(); i++) {
        if (i) s += ", ";
        s += names[i];
    }
    s += "> in ";
    if (source == ES_CONSTANTS) {
        s += "[";
        for (size_t i = 0; i < constants.size(); i++) {
            if (i) s += ", ";
            constants[i]->Dump(s, ind);
        }
        s += "]";
    } else {
        s += source == ES_FIELDS ? "fields<" : source == ES_VARIANTS ? "variants<" : "types<";
        for (size_t i = 0; i < types.size(); i++) {
            if (i) s += ", ";
            types[i]->Dump(s);
        }
        s += ">";
    }
    s += " ";
    DumpFragments(s, ind, body, armfragments, arms, inits);
}



inline void DumpFragments(string &s, int ind, const Block *body, bool armfragments,
                          const vector<MatchArm> &arms, const vector<FieldInit> &inits) {
    if (body) body->Dump(s, ind);
    else if (armfragments) {
        s += "{";
        DumpArms(s, arms, ind);
        s += "}";
    } else {
        s += "{";
        for (auto &fi : inits) {
            NL(s, ind + 1);
            if (!fi.name.empty()) Append(s, fi.name, ": ");
            fi.val->Dump(s, ind + 1);
            s += ",";
        }
        NL(s, ind);
        s += "}";
    }
}

inline void ConstQuery::Dump(string &s, int) const {
    if (query == Q_SAME) {
        s += "same<";
        types[0]->Dump(s);
        s += ", ";
        types[1]->Dump(s);
    } else {
        Append(s, "has_default<", name);
    }
    s += ">";
}

inline void ConstIf::Dump(string &s, int ind) const {
    for (auto &br : branches) {
        if (&br != &branches[0]) s += " else ";
        if (br.cond) {
            s += "const if ";
            DumpOperand(s, br.cond, ind);
            s += " ";
        }
        DumpFragments(s, ind, br.body, armfragments, br.arms, br.inits);
    }
}

inline void Unary::Dump(string &s, int ind) const {
    if (Written(this) != this) {
        child->Dump(s, ind);
        return;
    }
    s += TName(op);
    
    DumpOperand(s, child, ind, true);
}

inline void Binary::Dump(string &s, int ind) const {
    
    s += "(";
    DumpOperand(s, left, ind);
    Append(s, " ", TName(op), " ");
    DumpOperand(s, right, ind);
    s += ")";
}

inline void Dot::Dump(string &s, int ind) const {
    if (enumtype) enumtype->Dump(s);
    else DumpOperand(s, obj, ind, true);
    Append(s, ".", name);
}

inline void Call::Dump(string &s, int ind) const {
    DumpOperand(s, callee, ind, true);
    if (!tyargs.empty()) {
        s += "<";
        for (size_t i = 0; i < tyargs.size(); i++) {
            if (i) s += ", ";
            tyargs[i]->Dump(s);
        }
        s += ">";
    }
    s += "(";
    auto first = true;
    for (size_t i = 0; i < args.size(); i++) {
        if (dumpwritten && IsDefaultArg(i)) continue;
        if (!first) s += ", ";
        first = false;
        args[i]->Dump(s, ind);
    }
    s += ")";
    if (trailing) {
        s += " ";
        trailing->Dump(s, ind);
    }
}

inline void Index::Dump(string &s, int ind) const {
    DumpOperand(s, obj, ind, true);
    s += "[";
    idx->Dump(s, ind);
    s += "]";
}

inline void SliceExpr::Dump(string &s, int ind) const {
    DumpOperand(s, obj, ind, true);
    s += "[";
    if (lo) {
        if (lo_from_end) s += "^";
        lo->Dump(s, ind);
    }
    s += "..";
    if (hi) {
        if (hi_from_end) s += "^";
        hi->Dump(s, ind);
    }
    s += "]";
}

inline void AsCast::Dump(string &s, int ind) const {
    if (Written(this) != this) {
        child->Dump(s, ind);
        return;
    }
    s += "(";
    DumpOperand(s, child, ind, true);
    s += unchecked ? " as! " : " as ";
    type->Dump(s);
    s += ")";
}

inline void NullLit::Dump(string &s, int) const { s += "null"; }

inline void SelfRef::Dump(string &s, int) const { s += "self"; }

inline void RangeExpr::Dump(string &s, int ind) const {
    DumpOperand(s, lo, ind);
    s += "..";
    DumpOperand(s, hi, ind);
}

inline void IfExpr::Dump(string &s, int ind) const {
    s += "if ";
    DumpOperand(s, cond, ind);
    s += " ";
    thenb->Dump(s, ind);
    if (elseb) {
        s += " else ";
        elseb->Dump(s, ind);
    }
}




inline void DumpArms(string &s, const vector<MatchArm> &arms, int ind) {
    for (auto &arm : arms) {
        NL(s, ind + 1);
        if (arm.expansion) {
            arm.expansion->Dump(s, ind + 1);
            s += ",";
            continue;
        }
        if (arm.pat.kind == P_WILDCARD) s += "_";
        for (auto &pi : arm.pat.items) {
            if (&pi != &arm.pat.items[0]) s += ", ";
            pi.lo->Dump(s, ind);
            if (pi.hi) {
                s += "..";
                pi.hi->Dump(s, ind);
            }
        }
        if (!arm.pat.binder.empty()) Append(s, arm.pat.byref ? " &" : " ", arm.pat.binder);
        s += " => ";
        
        
        
        auto brk = Is<Break>(arm.body);
        auto ret = Is<Return>(arm.body);
        if ((brk && !brk->val) || (ret && ret->vals.empty() && ret->from.empty())) {
            s += "{";
            NL(s, ind + 2);
            arm.body->Dump(s, ind + 2);
            s += ";";
            NL(s, ind + 1);
            s += "}";
        } else if (ret) {
            s += "(";
            arm.body->Dump(s, ind + 1);
            s += ")";
        } else {
            arm.body->Dump(s, ind + 1);
        }
        s += ",";
    }
    NL(s, ind);
}

inline void MatchExpr::Dump(string &s, int ind) const {
    s += "match ";
    DumpOperand(s, scrutinee, ind);
    s += " {";
    DumpArms(s, arms, ind);
    s += "}";
}

inline void EarlyBlock::Dump(string &s, int ind) const {
    s += "block ";
    body->Dump(s, ind);
}

inline void While::Dump(string &s, int ind) const {
    s += "while ";
    DumpOperand(s, cond, ind);
    s += " ";
    body->Dump(s, ind);
}

inline void LoopExpr::Dump(string &s, int ind) const {
    s += "loop ";
    body->Dump(s, ind);
}

inline void ForLoop::Dump(string &s, int ind) const {
    s += "for ";
    if (byref) s += "&";
    s += var;
    
    
    auto binder = [&](TypeExpr *t) {
        s += ": ";
        if (t->ix) s += "(";
        t->Dump(s);
        if (t->ix) s += ")";
    };
    if (vartype) binder(vartype);
    if (!idxvar.empty()) Append(s, ", ", idxvar);
    if (idxtype) binder(idxtype);
    s += " in ";
    DumpOperand(s, iter, ind);
    s += " ";
    body->Dump(s, ind);
}

inline void Return::Dump(string &s, int ind) const {
    s += "return";
    for (size_t i = 0; i < vals.size(); i++) {
        s += i ? ", " : " ";
        vals[i]->Dump(s, ind);
    }
    if (!from.empty()) Append(s, " from ", from);
}

inline void Break::Dump(string &s, int ind) const {
    s += "break";
    if (val) {
        s += " ";
        
        
        auto ret = Is<Return>(val);
        auto parens = ret && !(ret->vals.empty() && ret->from.empty());
        if (parens) s += "(";
        val->Dump(s, ind);
        if (parens) s += ")";
    }
}

inline void Continue::Dump(string &s, int) const { s += "continue"; }




inline void Defer::Dump(string &s, int ind) const {
    s += "defer ";
    if (body->stmts.size() == 1 && !body->tail && !Is<Block>(body->stmts[0])) {
        body->stmts[0]->Dump(s, ind);
        return;
    }
    body->Dump(s, ind);
}


inline void InlineBlock::Dump(string &s, int ind) const {
    Append(s, "inline ", sf->qname, "#", spec->id, " ");
    body->Dump(s, ind);
}

inline void FunVal::Dump(string &s, int ind) const {
    s += "{";
    if (explicit_params) {
        for (size_t i = 0; i < params.size(); i++) {
            s += i ? ", " : " ";
            if (params[i].isvar) s += "var ";
            s += params[i].name;
            if (params[i].type) { s += ": "; params[i].type->Dump(s); }
        }
        s += " =>";
    }
    DumpBlockBody(s, body, ind);
}

inline void VarDecl::Dump(string &s, int ind) const {
    if (reusable) s += reusable == RU_SLICES ? "reusable[] " : "reusable ";
    s += isconst ? "const " : isvar ? "var " : "let ";
    
    
    if (isglobal && !ns.empty()) Append(s, ns, "::");
    for (size_t i = 0; i < names.size(); i++) {
        if (i) s += ", ";
        s += names[i];
    }
    if (type) { s += ": "; type->Dump(s); }
    for (size_t i = 0; i < inits.size(); i++) {
        s += i ? ", " : byref ? " .= " : " = ";
        inits[i]->Dump(s, ind);
    }
}

inline void Assign::Dump(string &s, int ind) const {
    lval->Dump(s, ind);
    Append(s, " ", TName(op), " ");
    rhs->Dump(s, ind);
}

inline void IncDec::Dump(string &s, int ind) const {
    lval->Dump(s, ind);
    s += TName(op);
}

inline void DumpFields(string &s, const vector<Field> &fields, int ind) {
    for (auto &f : fields) {
        NL(s, ind + 1);
        if (f.ispad) {
            s += "pad";
            if (f.padsize >= 0) Append(s, " ", f.padsize);
        } else {
            
            auto sugar = f.isconst && f.type->cq;
            if (f.isconst) s += sugar ? "const " : "let ";
            Append(s, f.name, ": ");
            if (sugar) {
                TypeExpr plain = *f.type;
                plain.cq = false;
                plain.Dump(s);
            } else {
                f.type->Dump(s);
            }
            if (f.defaultval) {
                s += " = ";
                f.defaultval->Dump(s, ind + 1);
            }
        }
        s += ",";
    }
}

inline void FnDecl::Dump(string &s, int ind) const {
    if (sf->isexport) {
        s += "export ";
        if (sf->cname != sf->name) { EscapeString(s, sf->cname, '"'); s += " "; }
    }
    if (sf->isextern) {
        
        
        if (sf->dylib.empty()) s += "extern ";
    }
    
    if (sf->checkfree && !(sf->isnested && sf->outer && sf->outer->checkfree))
        s += "checkfree ";
    if (sf->issimd) s += "simd ";
    if (sf->isinline) s += "inline ";
    if (sf->isrec) s += "recursive ";
    s += sf->isthread ? "thread_fn " : "fn ";
    s += sf->isnested ? sf->name : sf->qname;   
    DumpGenerics(s, sf->generics);
    s += "(";
    for (size_t i = 0; i < sf->params.size(); i++) {
        if (i) s += ", ";
        if (sf->params[i].isvar) s += "var ";
        s += sf->params[i].name;
        if (sf->params[i].type) { s += ": "; sf->params[i].type->Dump(s); }
        if (sf->params[i].defaultval) {
            s += " = ";
            sf->params[i].defaultval->Dump(s, ind);
        }
    }
    s += ")";
    if (sf->has_rets) {
        s += " -> ";
        for (size_t i = 0; i < sf->rets.size(); i++) {
            if (i) s += ", ";
            sf->rets[i]->Dump(s);
        }
    }
    if (sf->isextern) {
        if (sf->cname != sf->name) { s += " = "; EscapeString(s, sf->cname, '"'); }
        s += ";";
        return;
    }
    s += " ";
    sf->body->Dump(s, ind);
}

inline void StructDecl::Dump(string &s, int ind) const {
    if (st->isroot) s += "root ";
    Append(s, "struct ", st->qname);
    DumpGenerics(s, st->generics);
    s += " {";
    DumpFields(s, st->fields, ind);
    NL(s, ind);
    s += "}";
}

inline void EnumDecl::Dump(string &s, int ind) const {
    Append(s, "enum ", en->qname);
    DumpGenerics(s, en->generics);
    s += " {";
    for (auto &v : en->variants) {
        NL(s, ind + 1);
        s += v.name;
        if (v.has_payload) {
            s += " {";
            DumpFields(s, v.fields, ind + 1);
            NL(s, ind + 1);
            s += "}";
        }
        s += ",";
    }
    NL(s, ind);
    s += "}";
}

inline void AliasDecl::Dump(string &s, int) const {
    Append(s, "type ", al->qname, " = ");
    al->type->Dump(s);
    s += ";";
}

inline void Ast::Dump(string &s) const {
    
    
    
    string openlib;
    for (auto d : topdecls) {
        auto fd = Is<FnDecl>(d);
        auto lib = fd && !fd->sf->dylib.empty() ? fd->sf->dylib : string();
        if (!openlib.empty() && lib != openlib) {
            s += "}\n";
            openlib.clear();
        }
        if (!lib.empty() && openlib.empty()) {
            s += "extern ";
            EscapeString(s, lib, '"');
            s += " {\n";
            openlib = lib;
        }
        if (!openlib.empty()) s += "    ";
        d->Dump(s, 0);
        if (Is<VarDecl>(d)) s += ";";  
        s += "\n";
    }
    if (!openlib.empty()) s += "}\n";
}






inline void DumpDocs(Ast &ast, string &s) {
    auto doc = [&](const string &head, string_view text) {
        if (text.empty()) return;
        Append(s, head, "\n");
        for (size_t pos = 0; pos <= text.size();) {
            auto end = text.find('\n', pos);
            if (end == string_view::npos) end = text.size();
            auto line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            auto first = line.find_first_not_of(" \t");
            Append(s, "    ", first == string_view::npos ? string_view() : line.substr(first),
                   "\n");
            pos = end + 1;
        }
    };
    auto fields = [&](const string &owner, const vector<Field> &fs) {
        for (auto &f : fs)
            if (!f.ispad) doc(cat("field ", owner, ".", f.name), f.doc);
    };
    for (auto d : ast.topdecls) {
        if (auto fd = Is<FnDecl>(d)) {
            doc(cat("fn ", fd->sf->qname), fd->sf->doc);
        } else if (auto sd = Is<StructDecl>(d)) {
            doc(cat("struct ", sd->st->qname), sd->st->doc);
            fields(string(sd->st->qname), sd->st->fields);
        } else if (auto ed = Is<EnumDecl>(d)) {
            doc(cat("enum ", ed->en->qname), ed->en->doc);
            for (auto &v : ed->en->variants) {
                auto name = cat(ed->en->qname, ".", v.name);
                doc(cat("variant ", name), v.doc);
                fields(name, v.fields);
            }
        } else if (auto ad = Is<AliasDecl>(d)) {
            doc(cat("type ", ad->al->qname), ad->al->doc);
        } else if (auto vd = Is<VarDecl>(d)) {
            doc(cat(vd->isconst ? "const " : vd->isvar ? "var " : "let ",
                    ast.QualifiedName(vd->ns, vd->names[0])), vd->doc);
        }
    }
}

}  
