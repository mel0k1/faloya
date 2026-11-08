


#pragma once

namespace faloya {

inline string CodeGen::QueueFor(TypeExpr *t) {
    usesthreads = true;
    auto m = Mangle(t);
    auto it = queues.find(m);
    if (it != queues.end()) return it->second;
    auto name = Unique(cat("gs_q_", m));
    Append(data, "static gs_queue ", name, " = GS_QUEUE_INIT;\n");
    return queues[m] = name;
}







inline CodeGen::Loc CodeGen::RecvLoc(Node *n) {
    auto lv = GenLoc(n);
    if (lv.t->kind == TY_REF) {
        auto orig = lv;
        DerefLoc(orig);
        if (lv.viaref && lv.val && lv.t->ref->lenstorage < 0 && !lv.ispref &&
            (orig.stk.empty() || !CacheableStk(orig.stk))) {
            lv.s = Snapshot(lv.t, lv.s);
            DerefLoc(lv);
        } else {
            lv = orig;
        }
        return lv;
    }
    if (!lv.viaref || (!lv.stk.empty() && CacheableStk(lv.stk))) return lv;
    PinLoc(lv);
    return lv;
}


inline string CodeGen::LenCast(const Loc &lv) {
    auto ak = lv.t->arr->akind;
    if (ak == A_LIMITED) return lv.val ? IntCT(LenStore(lv.t->arr)) : "uint32_t";
    if (ak == A_VAR) return IntCT(LenStore(lv.t->arr));
    return "int64_t";
}

inline vector<string> CodeGen::EmitBuiltin(Call *c, Dst d0) {
    auto an = c->ArgNodes();
    auto ln = c->line;
    switch ((BuiltinKind)c->builtin) {
        case B_PRINT: {
            
            
            
            auto b = TempBuilder();
            for (auto a : an) EmitFormatInto(b, a, ln, c);
            RenderLit(b, "\n");
            L("gs_out_bytes(", b.hdr, ".base, ", b.hdr, ".len);");
            return {};
        }
        case B_STR: return EmitStr(c, an, d0, ln);
        case B_FORMAT: {
            auto lv = RecvLoc(an[0]);
            for (size_t i = 1; i < an.size(); i++) EmitFormatInto(lv, an[i], ln, c);
            return {};
        }
        case B_ASSERT: {
            if (BranchChain(an[0])) {
                auto ok = Lbl();
                GenBranch(an[0], true, ok);
                L("gs_abort(GS_E_ASSERT, ", LocArgs(ln), ");");
                L(ok, ":;");
                return {};
            }
            auto x = GenTruth(an[0]);
            L("if (!(", x, ")) gs_abort(GS_E_ASSERT, ", LocArgs(ln), ");");
            return {};
        }
        case B_ABORT: {
            auto x = GenPure(an[0]);
            L("gs_abort_msg(", x, ".data, ", x, ".len, ", LocArgs(ln), ");");
            return {};
        }
        case B_EXIT:
            L("gs_exit(", GenX(an[0]), ");");
            return {};
        case B_COPY: {
            if (c->exprtype->kind != TY_SLICE)
                return { LoadLoc(GenLoc(an[0]), c->exprtype, ln) };
            
            auto slice = c->exprtype;
            c->exprtype = c->rettypes[0];
            auto lv = GenLoc(c);
            c->exprtype = slice;
            return { LoadLoc(lv, slice, ln) };
        }
        case B_MOVE_FROM: {
            auto dst = ArrayView(RecvLoc(an[0]));
            auto base = T(), count = T();
            L("uint8_t *", base, " = (uint8_t *)(", dst.elems, ");");
            L("int64_t ", count, " = ", dst.len, ";");
            auto src = GenPure(an[1]);
            L("if (", count, " != ", src,
              ".len) gs_abort_msg((const uint8_t *)\"move_from lengths differ\", "
              "sizeof(\"move_from lengths differ\") - 1, ", LocArgs(ln), ");");
            auto size = FixedSize(dst.elem);
            if (size) L("if (", count, ") memmove(", base, ", ", src, ".data, (size_t)", count, " * ", size, ");");
            return {};
        }
        case B_TO_BYTES: return EmitToBytes(an, d0, ln);
        case B_BYTES_OF: return EmitBytesOf(c, an, ln);
        case B_FROM_BYTES: return EmitFromBytes(c, an, d0, ln);
        case B_DEFAULT: {
            auto t = c->rettypes[0];
            auto tv = T();
            FixedLocal(t, tv);
            if (!HasFieldDefaults(t)) EmitDefaultInto(tv, t);
            else if (t->kind == TY_ARRAY) {
                auto i = T();
                L("for (int64_t ", i, " = 0; ", i, " < ", ArrSize(t->arr), "; ", i, "++) {");
                ind++;
                PushSc(SC_PLAIN);
                GenAny(c->defaultinit, Dst { DK_LVALUE, cat(tv, ".e[", i, "]"), t->arr->sub });
                PopSc();
                ind--;
                L("}");
            } else GenAny(c->defaultinit, Dst { DK_LVALUE, tv, t });
            return { tv };
        }
        case B_HARDWARE_THREADS: return { "gs_hardware_threads()" };
        case B_EMBED_SHADER:
        case B_EMBED_FILE: {
            auto &blob = *c->shaderblob;
            auto t = T();
            L(CT(c->rettypes[0]), " ", t, " = { ", BlobRaw(blob), ", ", blob.size(), " };");
            return { t };
        }
        case B_THREAD_WAIT: {
            usesthreads = true;
            
            
            auto t = T();
            if (an.size() == 1) {
                auto id = GenX(an[0]);
                L("uint8_t ", t, " = gs_thread_wait(", id, ", NULL, NULL, ", LocArgs(ln), ");");
                return { t };
            }
            
            
            auto id = GenX(an[0]);
            auto m = T(), n = T();
            L("uint8_t *", m, ";");
            L("int64_t ", n, ";");
            L("uint8_t ", t, " = gs_thread_wait(", id, ", &", m, ", &", n, ", ", LocArgs(ln),
              ");");
            L("if (", m, ") {");
            ind++;
            AppendBytes(RecvLoc(an[1]), m, n, ln);
            L("gs_free_msg(", m, ");");
            ind--;
            L("}");
            return { t };
        }
        case B_THREAD_SPAWN: case B_THREAD_SPAWN_CONTAINED: return EmitThreadSpawn(c, an);
        case B_QPUT: {
            auto t = an[0]->exprtype;
            auto q = QueueFor(t);
            
            
            auto ok = T();
            if (IsResz(t)) {
                auto src = GenLoc(an[0]);
                if (src.t->kind == TY_REF) DerefLoc(src);
                string stk;
                auto base = BytesTemp(stk);
                EmitRzImage(src, t, stk, ln);
                L("uint8_t ", ok, " = gs_qput(&", q, ", ", base, ", ", Top(stk), " - ", base, ");");
            } else if (IsBytesT(t)) {
                auto p = GenPtr(an[0]);
                L("uint8_t ", ok, " = gs_qput(&", q, ", ", p, ", ", SizeX(t, p), ");");
            } else {
                auto tv = T();
                FixedLocal(t, tv, GenX(an[0]));
                L("uint8_t ", ok, " = gs_qput(&", q, ", &", tv, ", ", FixedSize(t), ");");
            }
            return { ok };
        }
        case B_QLIMIT: {
            auto q = QueueFor(c->qtype);
            auto n = GenX(an[0]);
            L("gs_qlimit(&", q, ", ", n, ", ", LocArgs(ln), ");");
            return {};
        }
        case B_QGET: case B_QPOLL: {
            auto t = c->rettypes[0];
            auto q = QueueFor(t);
            auto nn = T();
            
            auto poll = c->rettypes.size() > 1;
            if (c->builtin == B_QPOLL) L("gs_qnode *", nn, " = gs_qpoll(&", q, ");");
            else if (poll) L("gs_qnode *", nn, " = gs_qwait(&", q, ", ", GenX(an[0]), ");");
            else L("gs_qnode *", nn, " = gs_qget(&", q, ");");
            string got;
            if (poll) {
                got = T();
                L("uint8_t ", got, " = ", nn, " != NULL;");
            }
            if (IsResz(t)) {
                
                
                
                
                
                EmitCoreTypes();
                string stk = d0.k == DK_STACK && !d0.lenlv.empty() ? d0.s : "";
                string lenlv = d0.k == DK_STACK ? d0.lenlv : "";
                string hv;
                if (stk.empty()) {
                    hv = RzTemp(t, stk);
                    lenlv = RzLenLv(t, hv);
                }
                auto fo = IsFrameObj(t);
                auto cnt = fo ? cat(FoTailHdr(t, lenlv), ".len") : lenlv;
                auto image = cat("(uint8_t *)(", nn, " + 1) + 8");
                auto n = cat("(", nn, "->size - 8)");
                if (fo) {
                    
                    
                    
                    L("GS_MEMZERO(&", lenlv, ", sizeof(", lenlv, "));");
                    L(FoTailHdr(t, lenlv), ".base = ", Top(stk), ";");
                } else if (poll) {
                    L(lenlv, " = 0;");
                }
                L(poll ? cat("if (", nn, ") {") : string("{"));
                ind++;
                if (fo) {
                    auto pre = FoPrefixSize(t);
                    L("GS_MEMCPY(&", lenlv, ", ", image, ", ", pre, ");");
                    image = cat(image, " + ", pre);
                    n = cat("(", nn, "->size - 8 - ", pre, ")");
                }
                L(cnt, " = *(gs_i64 *)(", nn, " + 1);");
                L("GS_MEMCPY(", Top(stk), ", ", image, ", (size_t)", n, ");");
                Bump(stk, n);
                L("free(", nn, ");");
                ind--;
                L("}");
                return poll ? vector<string> { hv, got } : vector<string> { hv };
            }
            if (IsBytesT(t)) {
                string stk = d0.k == DK_STACK ? d0.s : "";
                string base = T();
                if (stk.empty()) {
                    stk = AllocStk(false);
                    L("uint8_t *", base, " = ", Top(stk), ";");
                    SaveBase(false, stk, base);
                } else {
                    L("uint8_t *", base, " = ", Top(stk), ";");
                }
                if (poll) L("if (", nn, ") {");
                else L("{");
                ind++;
                L("GS_MEMCPY(", Top(stk), ", ", nn, " + 1, (size_t)", nn, "->size);");
                Bump(stk, cat(nn, "->size"));
                L("free(", nn, ");");
                ind--;
                if (poll) {
                    
                    L("} else {");
                    ind++;
                    L("GS_MEMZERO(", Top(stk), ", ", MinBytes(t, true), ");");
                    Bump(stk, cat(MinBytes(t, true)));
                    ind--;
                }
                L("}");
                return poll ? vector<string> { base, got } : vector<string> { base };
            }
            auto tv = T();
            FixedLocal(t, tv);
            if (!FixedSize(t)) {
                
                
                L("GS_MEMZERO(&", tv, ", sizeof(", tv, "));");
                L("free(", nn, ");");
            } else if (poll) {
                L("GS_MEMZERO(&", tv, ", sizeof(", tv, "));");
                L("if (", nn, ") { GS_MEMCPY(&", tv, ", ", nn, " + 1, sizeof(", tv,
                  ")); free(", nn, "); }");
            } else {
                L("GS_MEMCPY(&", tv, ", ", nn, " + 1, sizeof(", tv, "));");
                L("free(", nn, ");");
            }
            return poll ? vector<string> { tv, got } : vector<string> { tv };
        }
        case B_PUSH: return EmitPush(an, ln);
        case B_APPEND: EmitAppend(an, ln); return {};
        case B_INDEX_OF: {
            
            
            
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            auto rx = GenX(an[1]);
            return { cat("(((uint8_t *)(", rx, ") - (uint8_t *)(", v.elems, ")) / ",
                         FixedSize(v.elem), ")") };
        }
        case B_CHECK_LENGTH: {
            auto lv = RecvLoc(an[0]);
            return { SliceLen(an[1], ArrayView(lv).elem, ln, "GS_E_ARRAYLEN",
                              an.size() > 2 ? an[2] : nullptr) };
        }
        case B_RANGE_INDEX: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            auto sv = GenPure(an[1]);
            auto esz = FixedSize(v.elem);
            auto fail = FailCall("GS_E_ARRAYSLICE", an.size() > 2 ? an[2] : nullptr, ln);
            auto i = T();
            L("int64_t ", i, " = 0;");
            if (!esz) {
                L("if ((uint64_t)", sv, ".len > (uint64_t)(", v.len, ")) ", fail, ";");
            } else if (!c->poolcheck) {
                L("if (", sv, ".len) ", i, " = ((uint8_t *)", sv,
                  ".data - (uint8_t *)(", v.elems, ")) / ", esz, ";");
            } else {
                L("if (", sv, ".len) {");
                ind++;
                auto off = T();
                L("uint64_t ", off, " = (uint64_t)((uintptr_t)", sv,
                  ".data - (uintptr_t)(", v.elems, "));");
                L("if (", off, " % ", esz, " || ", off, " / ", esz,
                  " >= (uint64_t)(", v.len, ") || (uint64_t)", sv,
                  ".len > (uint64_t)(", v.len, ") - ", off, " / ", esz, ") ", fail, ";");
                L(i, " = (int64_t)(", off, " / ", esz, ");");
                ind--;
                L("}");
            }
            return { i };
        }
        case B_POP: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            auto elem = v.elem;
            auto esz = FixedSize(elem);
            auto ak = lv.t->arr->akind;
            string at;
            if (ak == A_GROWSHRINK || ak == A_GROW) {
                NoteLen(lv.stk, v.lenlv, lv.lenroot);
                
                if (!c->nobc)
                    L("if (", v.len, " <= 0) gs_abort(GS_E_POP, ", LocArgs(ln), ");");
                L(v.lenlv, "--;");
                
                L(TopW(lv.stk), " -= ", esz, ";");
                at = Top(lv.stk);
            } else {
                auto nl = T();
                L("int64_t ", nl, " = ", v.len, " - 1;");
                
                if (!c->nobc) L("if (", nl, " < 0) gs_abort(GS_E_POP, ", LocArgs(ln), ");");
                L(v.lenlv, " = (", LenCast(lv), ")", nl, ";");
                at = cat("(", ElemAddr(v, nl), ")");
            }
            
            
            
            
            
            
            if (IsRelRef(elem))
                return { LoadLoc(BytesLoc(at, elem, lv), c->rettypes[0], ln) };
            return { cat("(*(", CT(elem), " *)", at, ")") };
        }
        case B_POP_N: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            auto ak = lv.t->arr->akind;
            if (ak != A_LIMITED) NoteLen(lv.stk, v.lenlv, lv.lenroot);
            auto nn = GenPure(an[1]);
            
            auto ol = T();
            L("int64_t ", ol, " = ", v.len, ";");
            
            
            L("if ((uint64_t)(", nn, ") > (uint64_t)", ol, ") gs_abort(GS_E_POPN, ",
              LocArgs(ln), ");");
            L(v.lenlv, " = (", LenCast(lv), ")(", ol, " - (int64_t)(", nn, "));");
            if (ak == A_GROWSHRINK || ak == A_GROW)
                L(TopW(lv.stk), " -= (int64_t)(", nn, ") * ", FixedSize(v.elem), ";");
            return {};
        }
        case B_RESIZE: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            auto elem = v.elem;
            auto esz = FixedSize(elem);
            auto ak = lv.t->arr->akind;
            auto relref = IsRelRef(elem);
            if (ak != A_LIMITED) NoteLen(lv.stk, v.lenlv, lv.lenroot);
            auto nn = GenPure(an[1]);
            string fv;
            if (an.size() > 2) {
                if (relref) {
                    fv = T();
                    L("uint8_t *", fv, " = (uint8_t *)(", GenX(an[2]), ");");
                } else fv = GenPure(an[2]);
            }
            auto ol = T();
            L("int64_t ", ol, " = ", v.len, ";");
            
            
            
            L("if ((int64_t)(", nn, ") < 0) gs_abort(GS_E_RESIZENEG, ", LocArgs(ln),
              ");");
            L("if (", nn, " < ", ol, ") {");
            ind++;
            L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
            if (ak == A_GROWSHRINK || ak == A_GROW)
                L(TopW(lv.stk), " = (uint8_t *)(", v.elems, ") + ", nn, " * ", esz, ";");
            ind--;
            L("} else if (", nn, " > ", ol, ") {");
            ind++;
            if (fv.empty()) {
                L("gs_abort(GS_E_RESIZEFILL, ", LocArgs(ln), ");");
            } else {
                if (ak == A_LIMITED)
                    L("if (", nn, " > ", LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ",
                      LocArgs(ln), ");");
                
                
                
                
                string cur;
                if (ak != A_LIMITED && !(relref && elem->ref->lenstorage == IS_VARINT)) {
                    cur = T();
                    L("uint8_t *", cur, " = ", Top(lv.stk), ";");
                }
                int fillbyte = 0;
                if (!cur.empty() && !relref && UniformFillByte(an[2], elem, fillbyte)) {
                    auto bytes = cat("(", nn, " - ", ol, ") * ", esz);
                    L("memset(", cur, ", ", fillbyte, ", (size_t)(", bytes, "));");
                    L(cur, " += ", bytes, ";");
                } else {
                    auto iv = T();
                    L("for (int64_t ", iv, " = ", ol, "; ", iv, " < ", nn, "; ", iv, "++) {");
                    ind++;
                    if (relref) {
                        if (ak == A_LIMITED) {
                            EmitRelStoreAt(cat("(uint8_t *)(", ElemAddr(v, iv), ")"), elem, fv, ln,
                                           true);
                        } else if (cur.empty()) {
                            EmitRelStore(lv.stk, elem, fv, ln);
                        } else {
                            EmitRelStoreAt(cur, elem, fv, ln, true);
                            L(cur, " += ", esz, ";");
                        }
                    } else if (ak == A_LIMITED) {
                        if (v.typedelems) L(v.elems, "[", iv, "] = ", fv, ";");
                        else L("*(", CT(elem), " *)(", ElemAddr(v, iv), ") = ", fv, ";");
                    } else {
                        L("*(", CT(elem), " *)", cur, " = ", fv, ";");
                        L(cur, " += ", esz, ";");
                    }
                    ind--;
                    L("}");
                }
                if (!cur.empty()) L(TopW(lv.stk), " = ", cur, ";");
                L(v.lenlv, " = (", LenCast(lv), ")", nn, ";");
            }
            ind--;
            L("}");
            return {};
        }
        case B_CLEAR: {
            auto lv = RecvLoc(an[0]);
            auto v = ArrayView(lv);
            
            
            
            
            auto ak = lv.t->arr->akind;
            if (ak == A_GROWSHRINK || ak == A_GROW) {
                NoteLen(lv.stk, v.lenlv, lv.lenroot);
                L(TopW(lv.stk), " = (uint8_t *)(", v.elems, ");");
            }
            L(v.lenlv, " = 0;");
            return {};
        }
        case B_ALLOC_INDEX: case B_ALLOC_REF: return EmitAlloc(c, an, ln);
        case B_ALLOC_SLICE: case B_REALLOC_SLICE: case B_FREE_SLICE:
            return EmitSlicePool(c, an, ln);
        case B_FREE: {
            auto lv = RecvLoc(an[0]);
            assert(!lv.fl.empty());
            
            
            
            
            auto x = GenPure(an[1]);
            auto i = T();
            L("int64_t ", i, " = GS_IDX(", x, ", ", lv.lenlv, ", ", LocArgs(ln), ");");
            L("*(gs_i64 *)", Top(lv.flstk), " = ", i, ";");
            L(TopW(lv.flstk), " += 8;");
            L(lv.fl, ".len++;");
            return {};
        }
        default:
            Fail(ln, cat("builtin not implemented: ", builtindefs[c->builtin].name));
    }
}




inline bool CodeGen::LiteralBytes(Node *n, TypeExpr *t, vector<uint8_t> &out) {
    if (!n || !t || !IsFix(t)) return false;
    auto put = [&](uint64_t v, int64_t size) {
        for (int64_t i = 0; i < size; i++) out.push_back((uint8_t)(v >> (8 * i)));
    };
    if (Is<NullLit>(n)) {
        
        
        if (t->kind != TY_REF || !t->ref->optional) return false;
        put(0, FixedSize(t));
        return true;
    }
    if (n->exprtype && !TEq(n->exprtype, t)) return false;
    switch (t->kind) {
        case TY_INT: {
            auto i = Is<IntLit>(n);
            if (!i || t->intstorage == IS_VARINT) return false;
            put((uint64_t)i->val, IntSize(t->intstorage));
            return true;
        }
        case TY_BOOL: {
            auto b = Is<BoolLit>(n);
            if (!b) return false;
            put(b->val ? 1 : 0, 1);
            return true;
        }
        case TY_FLT: {
            auto f = Is<FltLit>(n);
            if (!f) return false;
            uint64_t bits = 0;
            if (t->fltstorage == FS_F32) {
                auto v = (float)f->val;
                uint32_t b32;
                memcpy(&b32, &v, 4);
                bits = b32;
                put(bits, 4);
            } else {
                memcpy(&bits, &f->val, 8);
                put(bits, 8);
            }
            return true;
        }
        case TY_STRUCT: {
            auto sl = Is<StructLit>(n);
            auto si = SI(t);
            if (!sl || sl->sinst != si) return false;
            for (size_t fi = 0; fi < si->st->fields.size(); fi++) {
                if (si->st->fields[fi].ispad) continue;
                auto init = sl->InitFor((int)fi);
                auto ft = si->ftypes[fi];
                
                if (!init) {
                    if (ft->kind != TY_REF || !ft->ref->optional) return false;
                    put(0, FixedSize(ft));
                } else if (!LiteralBytes(init, ft, out)) {
                    return false;
                }
            }
            return true;
        }
        case TY_ARRAY: {
            auto al = Is<ArrayLit>(n);
            if (!al || t->arr->akind != A_FIXED) return false;
            auto k = ArrSize(t->arr);
            if (al->fillval) {
                for (int64_t e = 0; e < k; e++)
                    if (!LiteralBytes(al->fillval, t->arr->sub, out)) return false;
                return true;
            }
            if ((int64_t)al->elems.size() != k) return false;
            for (auto e : al->elems)
                if (!LiteralBytes(e, t->arr->sub, out)) return false;
            return true;
        }
        default: return false;
    }
}



inline bool CodeGen::UniformFillByte(Node *n, TypeExpr *t, int &byte) {
    vector<uint8_t> img;
    if (!LiteralBytes(n, t, img) || img.empty()) return false;
    for (auto b : img) if (b != img[0]) return false;
    byte = img[0];
    return true;
}













inline string CodeGen::ElemValue(Node *n, TypeExpr *elem) {
    if (IsRelRef(elem)) {
        auto ev = T();
        L("uint8_t *", ev, " = (uint8_t *)(", GenX(n), ");");
        return ev;
    }
    if (IsBytesT(elem) || HasRelRef(elem)) return "";
    
    
    auto x = GenXD(n, elem);
    return Is<Ident>(n) && IsCName(x) ? x : Snapshot(elem, x);
}



inline void CodeGen::StoreElemAt(Node *n, TypeExpr *elem, const string &e, const string &ev,
                                 Line ln) {
    
    
    if (IsRelRef(elem))
        EmitRelStoreAt(cat("(uint8_t *)", e), elem, ev, ln, true, RelValue(n));
    else if (ev.empty()) GenAny(n, Dst { DK_LVALUE, cat("(*", e, ")"), elem });
    else StoreWhole(e, elem, ev);
}

inline vector<string> CodeGen::EmitPush(vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    auto v = ArrayView(lv);
    auto elem = v.elem;
    auto ak = lv.t->arr->akind;
    auto ev = ElemValue(an[1], elem);
    string ref;
    if (ak == A_LIMITED) {
        auto nl = T();
        L("int64_t ", nl, " = ", v.len, ";");
        L("if (", nl, " >= ", LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ", LocArgs(ln), ");");
        auto e = T();
        L(CT(elem), " *", e, " = (", CT(elem), " *)(", ElemAddr(v, nl), ");");
        StoreElemAt(an[1], elem, e, ev, ln);
        L(v.lenlv, " = (", LenCast(lv), ")(", nl, " + 1);");
        ref = e;
    } else {
        assert(!lv.stk.empty());
        NoteLen(lv.stk, v.lenlv, lv.lenroot);
        
        
        
        
        if (!ev.empty() && !IsRelRef(elem)) {
            EmitValStore(lv.stk, elem, ev);
            L(v.lenlv, "++;");
            ref = cat("((", CT(elem), " *)(", Top(lv.stk), " - ", FixedSize(elem), "))");
            return { NewElemResult(ref, elem, lv, ln) };
        }
        auto e = T();
        if (IsBytesT(elem)) {
            L("uint8_t *", e, " = ", Top(lv.stk), ";");
        } else {
            L(CT(elem), " *", e, " = (", CT(elem), " *)", Top(lv.stk), ";");
        }
        
        
        if (IsRelRef(elem))
            EmitRelStore(lv.stk, elem, ev, ln, RelValue(an[1]));
        else if (ev.empty()) GenConstruct(an[1], lv.stk, elem);
        else EmitValStore(lv.stk, elem, ev);
        L(v.lenlv, "++;");
        ref = e;
    }
    return { NewElemResult(ref, elem, lv, ln) };
}






inline string CodeGen::NewElemResult(const string &e, TypeExpr *elem, const Loc &lv, Line ln) {
    if (elem->kind != TY_REF) return e;
    if (elem->ref->lenstorage >= 0) return LoadLoc(BytesLoc(e, elem, lv), elem, ln);
    return Snapshot(elem, cat("*", e));
}

inline void CodeGen::EmitAppend(vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    auto v = ArrayView(lv);
    auto elem = v.elem;
    auto ak = lv.t->arr->akind;
    auto src = an[1];
    
    
    if (auto c = Is<Call>(src); c && c->builtin == B_COPY) src = c->FirstArg();
    
    
    
    
    
    
    auto st = src->exprtype;
    auto asrun = IsResz(st) || (st->kind == TY_ARRAY && st->arr->akind == A_VAR);
    auto fresh = Is<Call>(src) || IsCtl(src);
    if (ak != A_LIMITED) NoteLen(lv.stk, v.lenlv, lv.lenroot);
    if (fresh && asrun && ak != A_LIMITED) {
        auto nn = T();
        L("int64_t ", nn, " = 0;");
        GenAny(src, Dst { DK_STACK, lv.stk, st, nn });
        L(v.lenlv, " += ", nn, ";");
        return;
    }
    if (fresh && st->kind == TY_ARRAY && st->arr->akind == A_LIMITED &&
        ArrSize(st->arr) < 0 && ak != A_LIMITED) {
        
        
        
        auto base = T(), nn = T(), bytes = T();
        L("uint8_t *", base, " = ", Top(lv.stk), ";");
        GenAny(src, Dst { DK_STACK, lv.stk, st });
        L("int64_t ", nn, " = *(gs_u32 *)(", base, " + 4);");
        L("int64_t ", bytes, " = ", nn, " * ", FixedSize(elem), ";");
        L("memmove(", base, ", ", base, " + 8, (size_t)", bytes, ");");
        L(TopW(lv.stk), " = ", base, " + ", bytes, ";");
        L(v.lenlv, " += ", nn, ";");
        return;
    }
    
    
    
    
    
    
    if (auto al = Is<ArrayLit>(src); al && (IsBytesT(elem) || HasRelRef(elem))) {
        if (IsBytesT(elem)) {
            auto nn = T();
            L("int64_t ", nn, " = 0;");
            GenArrayLit(al, lv.stk, nn);
            L(v.lenlv, " += ", nn, ";");
            return;
        }
        auto count = LitCount(al);
        string ol, at = Top(lv.stk);
        if (ak == A_LIMITED) {
            ol = T();
            L("int64_t ", ol, " = ", v.len, ";");
            L("if (", ol, " + ", count, " > ", LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ",
              LocArgs(ln), ");");
            at = ElemAddr(v, ol);
        }
        
        auto rt = CT(src->exprtype);
        auto p = T();
        L(rt, " *", p, " = (", rt, " *)(", at, ");");
        FixedArrayLitAt(al, cat("(*", p, ")"), true);
        if (ak == A_LIMITED) {
            L(v.lenlv, " = (", LenCast(lv), ")(", ol, " + ", count, ");");
        } else {
            Bump(lv.stk, cat(count * FixedSize(elem)));
            L(v.lenlv, " += ", count, ";");
        }
        return;
    }
    auto se = GenSrcElems(src);
    auto nn = T();
    L("int64_t ", nn, " = ", se.n, ";");
    if (ak == A_LIMITED) {
        auto ol = T();
        L("int64_t ", ol, " = ", v.len, ";");
        L("if (", ol, " + ", nn, " > ", LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ",
          LocArgs(ln), ");");
        L(CopyFn(), "(", ElemAddr(v, ol), ", ", se.elems, ", (size_t)(", nn, " * ",
          FixedSize(elem), "));");
        L(v.lenlv, " = (", LenCast(lv), ")(", ol, " + ", nn, ");");
        return;
    }
    assert(!lv.stk.empty());
    EmitCopyElems(lv.stk, elem, se.elems, nn);
    L(v.lenlv, " += ", nn, ";");
}

inline vector<string> CodeGen::EmitAlloc(Call *c, vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    assert(!lv.fl.empty() && !lv.stk.empty());
    auto v = ArrayView(lv);
    auto elem = v.elem;
    auto esz = FixedSize(elem);
    
    
    auto ev = ElemValue(an[1], elem);
    
    
    
    
    
    
    
    auto iv = T();
    L("int64_t ", iv, ";");
    L("if (", lv.fl, ".len > 0) {");
    ind++;
    L(lv.fl, ".len--;");
    L(TopW(lv.flstk), " -= 8;");
    L(iv, " = *(gs_i64 *)", Top(lv.flstk), ";");
    ind--;
    L("} else {");
    ind++;
    L(iv, " = ", lv.lenlv, ";");
    ind--;
    L("}");
    auto e = T();
    L(CT(elem), " *", e, " = (", CT(elem), " *)(", ElemAddr(v, iv), ");");
    StoreElemAt(an[1], elem, e, ev, ln);
    L("if (", iv, " == ", lv.lenlv, ") {");
    ind++;
    L(lv.lenlv, "++;");
    L(TopW(lv.stk), " += ", esz, ";");
    ind--;
    L("}");
    if (c->builtin == B_ALLOC_INDEX) return { iv };
    return { NewElemResult(e, elem, lv, c->line) };
}



inline vector<string> CodeGen::EmitSlicePool(Call *c, vector<Node *> &an, Line ln) {
    auto lv = RecvLoc(an[0]);
    assert(!lv.fl.empty() && !lv.stk.empty());
    auto v = ArrayView(lv);
    auto elem = v.elem;
    auto esz = FixedSize(elem);
    assert(esz > 0);   
    
    
    
    
    
    auto indexof = [&](const string &sv) {
        auto i = T();
        if (!c->poolcheck) {
            L("int64_t ", i, " = ", sv, ".len ? ((uint8_t *)", sv, ".data - (uint8_t *)", v.elems,
              ") / ", esz, " : 0;");
            return i;
        }
        L("int64_t ", i, " = 0;");
        L("if (", sv, ".len) {");
        ind++;
        auto off = T();
        L("uint64_t ", off, " = (uint64_t)((uintptr_t)", sv, ".data - (uintptr_t)", v.elems, ");");
        L("if (", off, " % ", esz, " || ", off, " / ", esz, " >= (uint64_t)", lv.lenlv,
          " || (uint64_t)", sv, ".len > (uint64_t)", lv.lenlv, " - ", off, " / ", esz,
          ") gs_abort(GS_E_POOLSLICE, ", LocArgs(ln), ");");
        L(i, " = (int64_t)(", off, " / ", esz, ");");
        ind--;
        L("}");
        return i;
    };
    if (c->builtin == B_FREE_SLICE) {
        auto sv = GenPure(an[1]);
        L("gs_spans_free(", SpanArgs(lv), ", ", indexof(sv), ", ", sv, ".len);");
        return {};
    }
    string i, n;
    if (c->builtin == B_ALLOC_SLICE) {
        n = SliceLen(an[1], elem, ln);
        i = T();
        L("int64_t ", i, " = ", PlaceRun(lv, n), ";");
        EmitSliceExtend(lv, cat(i, " + ", n), esz);
        EmitDefaultElems(v, i, n, c->defaultinit);
    } else {
        auto sv = GenPure(an[1]);
        n = SliceLen(an[2], elem, ln);
        i = indexof(sv);
        auto ol = T();
        L("int64_t ", ol, " = ", sv, ".len;");
        L("if (", n, " <= ", ol, ") {");
        ind++;
        L("gs_spans_free(", SpanArgs(lv), ", ", i, " + ", n, ", ", ol, " - ", n, ");");
        ind--;
        L("} else {");
        ind++;
        
        
        
        
        
        
        L("if (", ol, " == 0) {");
        ind++;
        L(i, " = ", PlaceRun(lv, n), ";");
        ind--;
        L("} else if (", i, " + ", ol, " != ", lv.lenlv, ") {");
        ind++;
        auto ni = T();
        L("int64_t ", ni, " = gs_spans_regrow(", SpanArgs(lv), ", ", lv.lenlv, ", ", i, ", ", ol,
          ", ", n, ");");
        L("if (", ni, " != ", i, ") memmove(", ElemAddr(v, ni), ", ", ElemAddr(v, i),
          ", (size_t)(", ol, " * ", esz, "));");
        L(i, " = ", ni, ";");
        ind--;
        L("}");
        EmitSliceExtend(lv, cat(i, " + ", n), esz);
        EmitDefaultElems(v, cat(i, " + ", ol), cat(n, " - ", ol), c->defaultinit);
        ind--;
        L("}");
    }
    auto r = T();
    auto sct = CT(ast.SliceOf(elem, ln)), ect = CT(elem);
    L(sct, " ", r, " = { (", ect, " *)(", ElemAddr(v, i), "), ", n, " };");
    return { r };
}




inline string CodeGen::PlaceRun(const Loc &lv, const string &n) {
    return cat("(", lv.fl, ".len ? gs_spans_alloc(", SpanArgs(lv), ", ", lv.lenlv, ", ", n,
               ") : ", lv.lenlv, ")");
}



inline string CodeGen::SpanArgs(const Loc &lv) {
    return cat(lv.fl, ".base, &", lv.fl, ".len, &(", TopW(lv.flstk), ")");
}



inline string CodeGen::SliceLen(Node *n, TypeExpr *elem, Line ln, const char *error,
                                Node *msg) {
    auto nv = T();
    L("int64_t ", nv, " = (int64_t)(", GenX(n), ");");
    auto esz = FixedSize(elem);
    if (esz)
        L("if ((uint64_t)", nv, " > GS_STACK_RESERVE / ", esz, ") ", FailCall(error, msg, ln), ";");
    else
        L("if (", nv, " < 0) ", FailCall(error, msg, ln), ";");
    return nv;
}



inline string CodeGen::FailCall(const char *error, Node *msg, Line ln) {
    if (auto s = msg ? Is<StrLit>(msg) : nullptr)
        return cat("gs_abort_msg((const uint8_t *)", CStr(s->val), ", ", s->val.size(), ", ",
                   LocArgs(ln), ")");
    return cat("gs_abort(", error, ", ", LocArgs(ln), ")");
}


inline void CodeGen::EmitSliceExtend(const Loc &lv, const string &end, int64_t esz) {
    auto ext = T();
    L("int64_t ", ext, " = ", end, " - ", lv.lenlv, ";");
    L("if (", ext, " > 0) {");
    ind++;
    L(lv.lenlv, " += ", ext, ";");
    Bump(lv.stk, cat(ext, " * ", esz));
    ind--;
    L("}");
}





inline bool CodeGen::BulkDefaultLoop(ForLoop *loop) {
    if (loop->idxdef || loop->vdef->captured || loop->vdef->type->kind != TY_INT ||
        loop->vdef->type->intstorage != IS_I64) return false;
    auto loopbody = loop->body;
    if (loopbody->stmts.size() != 1 || loopbody->tail) return false;
    auto unref = [](Node *n) {
        auto u = Is<Unary>(n);
        return u && u->op == T_BITAND ? u->child : n;
    };
    auto lenof = [&](Node *n, Node *array) {
        auto dot = Is<Dot>(n);
        return dot && !dot->IsField() && dot->name == "len" && SamePath(dot->obj, unref(array));
    };
    Node *array = nullptr, *value = nullptr, *end = nullptr;
    auto append = false;
    if (auto a = Is<Assign>(loopbody->stmts[0]); a &&
        (a->op == T_ASSIGN || a->op == T_DOTASSIGN) && loop->iterkind == IK_COUNT) {
        auto index = Is<Index>(a->lval);
        auto id = index ? Is<Ident>(index->idx) : nullptr;
        if (!id || id->vdef != loop->vdef || !lenof(loop->iter, index->obj)) return false;
        array = index->obj; value = a->rhs;
    } else if (auto call = Is<Call>(loopbody->stmts[0]); call &&
               call->builtin == B_PUSH && loop->iterkind == IK_RANGE) {
        auto args = call->ValueArgNodes();
        auto range = Is<RangeExpr>(loop->iter);
        if (args.size() != 2 || !range || !lenof(range->lo, args[0]) ||
            (!Is<Ident>(range->hi) && !Is<IntLit>(range->hi)) ||
            range->exprtype->kind != TY_INT || range->exprtype->intstorage != IS_I64)
            return false;
        array = args[0]; value = args[1]; end = range->hi; append = true;
    } else return false;
    auto type = unref(array)->exprtype;
    if (type->kind == TY_REF) type = type->ref->sub;
    if (append ? !IsResz(type) || type->kind != TY_ARRAY : type->kind != TY_SLICE) return false;
    auto elem = append ? type->arr->sub : type->sub;
    if (!FieldChainRoot(unref(array), true) || !ZeroDefaultValue(value, elem)) return false;
    auto lv = RecvLoc(array);
    auto view = ArrayView(lv);
    auto esz = FixedSize(elem);
    if (append) {
        NoteLen(lv.stk, view.lenlv, lv.lenroot);
        auto first = T(), limit = T();
        L("int64_t ", first, " = ", view.len, ";");
        L("int64_t ", limit, " = ", GenX(end), ";");
        L("if (", limit, " > ", first, ") {");
        ind++;
        
        L("if ((uint64_t)", limit, " > GS_STACK_RESERVE / ", esz, ") gs_stack_overflow(",
          Top(lv.stk), ");");
        L("GS_MEMZERO(", Top(lv.stk), ", (size_t)((", limit, " - ", first, ") * ", esz, "));");
        Bump(lv.stk, cat("(", limit, " - ", first, ") * ", esz));
        L(view.lenlv, " = ", limit, ";");
        ind--;
        L("}");
    } else {
        L("if (", view.len, ") GS_MEMZERO(", view.elems, ", (size_t)(", view.len, " * ", esz, "));");
    }
    return true;
}



inline void CodeGen::EmitDefaultElems(const ArrView &v, const string &first,
                                      const string &count, Node *init) {
    auto esz = FixedSize(v.elem);
    if (!HasFieldDefaults(v.elem)) {
        L("GS_MEMZERO(", ElemAddr(v, cat("(", first, ")")), ", (size_t)((", count, ") * ", esz,
          "));");
        return;
    }
    auto k = T();
    L("for (int64_t ", k, " = 0; ", k, " < ", count, "; ", k, "++) {");
    ind++;
    auto e = T();
    L(CT(v.elem), " *", e, " = (", CT(v.elem), " *)(", ElemAddr(v, cat("(", first, " + ", k, ")")),
      ");");
    PushSc(SC_PLAIN);
    GenAny(init, Dst { DK_LVALUE, cat("(*", e, ")"), v.elem });
    PopSc();
    ind--;
    L("}");
}




inline void CodeGen::EmitRzImage(Loc src, TypeExpr *t, const string &stk, Line ln) {
    if (IsFrameObj(t)) {
        if (!src.val) src = FoView(src);
        auto th = FoTailHdr(t, src.s);
        EmitValStore(stk, ast.inttypes[IS_I64], cat(th, ".len"));
        auto pre = FoPrefixSize(t);
        L("GS_MEMCPY(", Top(stk), ", &", src.s, ", ", pre, ");");
        Bump(stk, pre);
        EmitCopyElems(stk, FoTailArr(t)->arr->sub, cat(th, ".base"), cat(th, ".len"));
        return;
    }
    auto cntp = T();
    L("gs_i64 *", cntp, " = (gs_i64 *)", Top(stk), ";");
    auto lenv = T();
    L("int64_t ", lenv, ";");
    EmitValStore(stk, ast.inttypes[IS_I64], "0");
    EmitRzCopy(src, t, stk, lenv, ln);
    L("*", cntp, " = ", lenv, ";");
}




inline vector<VarDef *> &CodeGen::ThreadGlobals(FnSpec *entry) {
    auto it = threadglobals.find(entry);
    if (it != threadglobals.end()) return it->second;
    set<FnSpec *> seen;
    set<VarDef *> named;
    set<TypeExpr *> namedtypes;
    function<void(FnSpec *)> rec = [&](FnSpec *sp) {
        if (!sp || !sp->body || !seen.insert(sp).second) return;
        function<void(Node *)> walk = [&](Node *n) {
            if (!n) return;
            NodeNamedArrays(n, namedtypes, [&](VarDef *a, bool) { named.insert(a); });
            if (auto id = Is<Ident>(n))
                if (auto v = id->vdef; v && v->isglobal) named.insert(v);
            if (auto c = Is<Call>(n)) {
                rec(c->spec);
                for (auto d : c->dispatch) rec(d);
                for (auto &fs : c->fmtspecs) rec(fs.second);
            }
            RunChildren(n, walk);
        };
        walk(sp->body);
    };
    rec(entry);
    auto &out = threadglobals[entry];
    for (auto g : ast.globals)
        for (auto d : g->defs)
            if (named.count(d) && !gstatic.count(d) && !gshared.count(d)) out.push_back(d);
    return out;
}





inline vector<string> CodeGen::EmitThreadSpawn(Call *c, vector<Node *> &an) {
    usesthreads = true;
    auto sp = c->spec;
    auto thunk = EnsureThreadThunk(sp);
    string stk;
    auto base = BytesTemp(stk);
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        auto pt = sp->argtypes[i];
        if (!IsResz(pt)) {
            GenConstruct(an[1 + i], stk, pt);
            continue;
        }
        
        
        auto szp = T();
        L("gs_i64 *", szp, " = (gs_i64 *)", Top(stk), ";");
        Bump(stk, "8");
        auto cntp = T();
        L("gs_i64 *", cntp, " = (gs_i64 *)", Top(stk), ";");
        EmitValStore(stk, ast.inttypes[IS_I64], "0");
        if (IsFrameObj(pt)) {
            
            
            auto pre = FoPrefixSize(pt), pp = T(), h = T();
            L("uint8_t *", pp, " = ", Top(stk), ";");
            Bump(stk, pre);
            L(CT(pt), " ", h, ";");
            GenConstruct(an[1 + i], stk, pt, h);
            L("*", cntp, " = ", FoTailHdr(pt, h), ".len;");
            L("GS_MEMCPY(", pp, ", &", h, ", ", pre, ");");
        } else {
            GenConstruct(an[1 + i], stk, pt, cat("(*", cntp, ")"));
        }
        L("*", szp, " = ", Top(stk), " - (uint8_t *)(", szp, " + 1);");
    }
    for (auto d : ThreadGlobals(sp)) {
        auto szp = T();
        L("gs_i64 *", szp, " = (gs_i64 *)", Top(stk), ";");
        Bump(stk, "8");
        auto lv = VarLoc(d);
        if (IsResz(d->type)) {
            EmitRzImage(lv, d->type, stk, c->line);
            if (d->reusable) {
                
                auto &p = gpools[d];
                auto flsz = FlEntrySize(d);
                EmitValStore(stk, ast.inttypes[IS_I64], cat(p.first, ".len"));
                L("GS_MEMCPY(", Top(stk), ", ", p.first, ".base, (size_t)(", p.first,
                  ".len * ", flsz, "));");
                Bump(stk, cat(p.first, ".len * ", flsz));
            }
        } else if (IsBytesT(d->type)) {
            auto n = T();
            L("int64_t ", n, " = ", SizeX(d->type, lv.s), ";");
            L("GS_MEMCPY(", Top(stk), ", ", lv.s, ", (size_t)", n, ");");
            Bump(stk, n);
        } else {
            L("GS_MEMCPY(", Top(stk), ", &", lv.s, ", ", FixedSize(d->type), ");");
            Bump(stk, cat(FixedSize(d->type)));
        }
        L("*", szp, " = ", Top(stk), " - (uint8_t *)(", szp, " + 1);");
    }
    
    auto id = T();
    L("int64_t ", id, " = gs_thread_spawn(", thunk, ", ", base, ", ", Top(stk), " - ", base,
      c->builtin == B_THREAD_SPAWN_CONTAINED ? ", 1);" : ", 0);");
    return { id };
}



inline string CodeGen::EnsureThreadThunk(FnSpec *sp) {
    auto it = thunks.find(sp);
    if (it != thunks.end()) return it->second;
    auto name = Unique(cat("gs_tmain_", Sanitize(sp->sf->ns, sp->sf->name)));
    thunks[sp] = name;
    Append(protos, "static void ", name, "(uint8_t *p);\n");
    return name;
}






inline void CodeGen::EmitThreadThunk(FnSpec *sp, int64_t stacks) {
    auto name = thunks[sp];
    auto &ki = sinfo[sp];
    string b;
    Append(b, "static void ", name, "(uint8_t *p) {\n    gs_stack_block(", stacks, ");\n");
    vector<string> args;
    int regions = 0;    
    for (size_t i = 0; i < sp->argtypes.size(); i++) {
        auto pt = sp->argtypes[i];
        if (IsResz(pt)) {
            EmitCoreTypes();
            auto a = cat("a", i), stk = cat(a, "_stk");
            regions++;
            Append(b, "    gs_stack ", stk, "; gs_stack_init(&", stk, ");\n",
                   "    ", RzCT(pt), " ", a, ";\n",
                   "    {\n        int64_t sz = *(gs_i64 *)p; p += 8;\n");
            auto pre = IsFrameObj(pt) ? FoPrefixSize(pt) : string("0");
            auto hdr = IsFrameObj(pt) ? FoTailHdr(pt, a) : a;
            if (IsFrameObj(pt))
                Append(b, "        GS_MEMCPY(&", a, ", p + 8, ", pre, ");\n");
            Append(b, "        ", hdr, ".base = ", stk, ".top;\n",
                   "        ", hdr, ".len = *(gs_i64 *)p;\n",
                   "        GS_MEMCPY(", stk, ".top, p + 8 + ", pre,
                   ", (size_t)(sz - 8 - ", pre, "));\n",
                   "        ", stk, ".top += sz - 8 - ", pre, "; p += sz;\n    }\n");
            args.push_back(a);
            args.push_back(cat("&", stk));
        } else if (IsBytesT(pt)) {
            Append(b, "    uint8_t *a", i, " = p;\n");
            
            Append(b, "    p += ", SizeX(pt, cat("a", i)).c_str(), ";\n");
            args.push_back(cat("a", i));
        } else if (IsLargeFixed(pt)) {
            
            
            Append(b, "    ", CT(pt), " *a", i, " = (", CT(pt), " *)p; p += ",
                   FixedSize(pt), ";\n");
            args.push_back(cat("a", i));
        } else if (!FixedSize(pt)) {
            
            
            Append(b, "    ", CT(pt), " a", i, " = {0};\n");
            args.push_back(cat("a", i));
        } else {
            Append(b, "    ", CT(pt), " a", i, " = *(", CT(pt), " *)p; p += ",
                   FixedSize(pt), ";\n");
            PushArg(args, pt, IsPoolParam(sp, i), cat("a", i));
        }
    }
    assert(ki.freevars.empty() && !ki.hasrf && sp->rets.empty());
    if (ki.needssp) args.push_back("0");
    
    if (ki.located) args.push_back(CallerLoc(sp->sf->line));
    Append(b, "    gs_gl = calloc(1, sizeof(gs_globals_t));\n"
              "    if (!gs_gl) gs_panic(\"out of memory copying globals\");\n");
    for (auto d : ThreadGlobals(sp)) {
        auto gn = gnames[d];
        Append(b, "    {\n        int64_t sz = *(gs_i64 *)p; p += 8; uint8_t *img = p; p += sz;\n");
        if (IsResz(d->type)) {
            auto stk = gstks[d];
            regions++;
            Append(b, "        gs_stack_init(", stk, ");\n");
            if (IsFrameObj(d->type)) {
                auto th = FoTailHdr(d->type, gn);
                auto pre = FoPrefixSize(d->type);
                Append(b, "        GS_MEMCPY(&", gn, ", img + 8, ", pre, ");\n",
                       "        ", th, ".base = ", stk, "->top;\n",
                       "        ", th, ".len = *(gs_i64 *)img;\n",
                       "        GS_MEMCPY(", stk, "->top, img + 8 + ", pre, ", (size_t)(sz - 8 - ",
                       pre, "));\n",
                       "        ", stk, "->top += sz - 8 - ", pre, ";\n");
            } else if (d->reusable) {
                
                
                auto &p = gpools[d];
                auto esz = FixedSize(d->type->arr->sub);
                auto flsz = FlEntrySize(d);
                regions++;
                Append(b, "        int64_t cnt = *(gs_i64 *)img, ebytes = cnt * ", esz, ";\n",
                       "        ", gn, ".base = ", stk, "->top;\n",
                       "        ", gn, ".len = cnt;\n",
                       "        GS_MEMCPY(", stk, "->top, img + 8, (size_t)ebytes);\n",
                       "        ", stk, "->top += ebytes;\n",
                       "        gs_stack_init(", p.second, ");\n",
                       "        ", p.first, ".base = ", p.second, "->top;\n",
                       "        ", p.first, ".len = *(gs_i64 *)(img + 8 + ebytes);\n",
                       "        GS_MEMCPY(", p.second, "->top, img + 16 + ebytes, (size_t)(",
                       p.first, ".len * ", flsz, "));\n",
                       "        ", p.second, "->top += ", p.first, ".len * ", flsz, ";\n");
            } else {
                Append(b, "        ", gn, ".base = ", stk, "->top;\n",
                       "        ", gn, ".len = *(gs_i64 *)img;\n",
                       "        GS_MEMCPY(", stk, "->top, img + 8, (size_t)(sz - 8));\n",
                       "        ", stk, "->top += sz - 8;\n");
            }
        } else if (IsBytesT(d->type)) {
            auto stk = gstks[d];
            regions++;
            Append(b, "        gs_stack_init(", stk, ");\n",
                   "        ", gn, " = ", stk, "->top;\n",
                   "        GS_MEMCPY(", stk, "->top, img, (size_t)sz);\n",
                   "        ", stk, "->top += sz;\n");
        } else {
            Append(b, "        GS_MEMCPY(&", gn, ", img, (size_t)sz);\n");
        }
        Append(b, "    }\n");
    }
    string argstr;
    for (size_t i = 0; i < args.size(); i++) Append(argstr, i ? ", " : "", args[i]);
    Append(b, "    ", ki.cname, "(", argstr, ");\n}\n\n");
    code += b;
    thunkregions[sp] = regions;
}








inline void CodeGen::EmitLeCheck(Line ln) {
    EmitCoreTypes();
    L("if (!gs_is_le()) gs_abort(GS_E_ENDIAN, ", LocArgs(ln), ");");
}




inline void CodeGen::PayloadOf(Node *n, string &src, string &sz) {
    auto nt = n->exprtype;
    auto rt = nt->kind == TY_REF ? nt->ref->sub : nt;
    auto elem = rt->kind == TY_SLICE ? rt->sub : rt->arr->sub;
    SrcElems se;
    if (rt->kind == TY_ARRAY || nt->kind == TY_REF) {
        
        
        auto v = ArrayView(RecvLoc(n));
        auto cnt = T();
        L("int64_t ", cnt, " = ", v.len, ";");
        se.elems = v.elems;
        se.n = cnt;
    } else {
        se = GenSrcElems(n);
    }
    src = T();
    L("const uint8_t *", src, " = (const uint8_t *)(", se.elems, ");");
    sz = T();
    if (IsFix(elem)) {
        L("int64_t ", sz, " = (", se.n, ") * ", FixedSize(elem), ";");
    } else {
        auto i = T();
        L("int64_t ", sz, " = 0;");
        L("for (int64_t ", i, " = 0; ", i, " < (", se.n, "); ", i, "++) ", sz, " += ",
          SizeFn(elem), "(", src, " + ", sz, ");");
    }
}




inline void CodeGen::AppendBytes(const Loc &lv, const string &src, const string &n, Line ln) {
    auto v = ArrayView(lv);
    if (lv.t->arr->akind == A_LIMITED) {
        auto ol = T();
        L("int64_t ", ol, " = ", v.len, ";");
        L("if (", ol, " + ", n, " > ", LimitedCap(lv), ") gs_abort(GS_E_CAPACITY, ",
          LocArgs(ln), ");");
        L(CopyFn(), "(", ElemAddr(v, ol), ", ", src, ", (size_t)", n, ");");
        L(v.lenlv, " = (", LenCast(lv), ")(", ol, " + ", n, ");");
        return;
    }
    L(CopyFn(), "(", Top(lv.stk), ", ", src, ", (size_t)", n, ");");
    Bump(lv.stk, n);
    L(v.lenlv, " += ", n, ";");
}





inline CodeGen::RzDest CodeGen::OpenRzDest(TypeExpr *t, Dst d0, Line ln, const char *what) {
    EmitCoreTypes();
    RzDest rd;
    auto resz = IsResz(t);
    rd.stk = d0.s;
    rd.lenlv = d0.lenlv;
    if (d0.k != DK_STACK) {
        if (resz) {
            rd.hdr = RzTemp(t, rd.stk);
            rd.lenlv = RzLenLv(t, rd.hdr);
        } else {
            rd.hdr = BytesTemp(rd.stk);   
        }
    }
    if (rd.lenlv.empty()) {
        
        
        
        auto at = d0.t && d0.t->kind == TY_ARRAY && d0.t->arr->akind == A_VAR ? d0.t
                  : resz ? nullptr : t;
        if (!at) Fail(ln, cat(what, "() needs a resizable or variable-array destination"));
        rd.ls = LenStore(at->arr);
        rd.pref = T();
        L("uint8_t *", rd.pref, " = ", Top(rd.stk), ";");
        Bump(rd.stk, cat(PrefixBytes(rd.ls)));
    }
    rd.elems = T();
    L("uint8_t *", rd.elems, " = ", Top(rd.stk), ";");
    return rd;
}

inline void CodeGen::CloseRzDest(RzDest &rd, const string &count) {
    if (!rd.pref.empty()) EmitPrefixPatch(rd.pref, rd.ls, rd.stk, count, rd.elems);
    else L(rd.lenlv, " = ", count, ";");
}




inline vector<string> CodeGen::EmitBytesOf(Call *c, vector<Node *> &an, Line ln) {
    EmitLeCheck(ln);
    string src, sz;
    PayloadOf(an[0], src, sz);
    auto s = T();
    L(CT(c->rettypes[0]), " ", s, " = { (uint8_t *)", src, ", ", sz, " };");
    return { s };
}





inline vector<string> CodeGen::EmitToBytes(vector<Node *> &an, Dst d0, Line ln) {
    EmitLeCheck(ln);
    string src, sz;
    PayloadOf(an[0], src, sz);
    auto pfx = T(), pn = T();
    L("uint8_t ", pfx, "[10];");
    L("int64_t ", pn, " = gs_uleb_write(", pfx, ", (uint64_t)", sz, ");");
    if (an.size() > 1) {
        auto out = RecvLoc(an[1]);
        AppendBytes(out, pfx, pn, ln);
        AppendBytes(out, src, sz, ln);
        return {};
    }
    auto rd = OpenRzDest(GrowU8(), d0, ln, "to_bytes");
    L("GS_MEMCPY(", Top(rd.stk), ", ", pfx, ", (size_t)", pn, ");");
    Bump(rd.stk, pn);
    L(CopyFn(), "(", Top(rd.stk), ", ", src, ", (size_t)", sz, ");");
    Bump(rd.stk, sz);
    auto tot = T();
    L("int64_t ", tot, " = ", pn, " + ", sz, ";");
    CloseRzDest(rd, tot);
    return rd.hdr.empty() ? vector<string> {} : vector<string> { rd.hdr };
}




inline vector<string> CodeGen::EmitFromBytes(Call *c, vector<Node *> &an, Dst d0, Line ln) {
    EmitLeCheck(ln);
    auto t = c->rettypes[0];
    auto elem = t->arr->sub;
    auto sl = GenPure(an[0]);
    auto vf = VerifyFn(elem);
    
    
    
    auto pay = T(), pn = T(), ok = T(), uv = T(), kv = T();
    L("const uint8_t *", pay, " = ", sl, ".data;");
    L("int64_t ", pn, " = 0;");
    L("uint8_t ", ok, " = 0;");
    L("uint64_t ", uv, " = 0;");
    L("int64_t ", kv, " = gs_uleb_check(", sl, ".data, ", sl, ".data + ", sl, ".len, &",
      uv, ");");
    L("if (", kv, " && ", uv, " <= (uint64_t)INT64_MAX && (int64_t)", uv, " == ", sl,
      ".len - ", kv, ") {");
    ind++;
    L(pay, " += ", kv, "; ", pn, " = (int64_t)", uv, "; ", ok, " = 1;");
    ind--;
    L("}");
    string bm = "NULL";
    if (!IsFix(elem) && HasRelRefAny(elem)) {
        
        
        
        
        
        string bstk;
        bm = BytesTemp(bstk);
        auto bn = T();
        L("int64_t ", bn, " = (", pn, " + 7) / 8;");
        L("if (", bn, " > GS_BM_MAX) { ", bn, " = 0; ", ok, " = 0; }");
        L("GS_MEMZERO(", bm, ", (size_t)", bn, ");");
        Bump(bstk, bn);
    }
    auto cnt = T();
    
    
    set<TypeExpr *> seen;
    string reach;
    NamedArraysIn(elem, seen, [&](VarDef *a, bool) { reach += cat(gstks[a], "\x01"); });
    if (!reach.empty()) MarkFlush(reftops || topdst || topcap ? "*" : reach);
    L("int64_t ", cnt, " = ", ok, " ? ", vf, "(", pay, ", ", pn, ", ", bm, ") : -1;");
    L(ok, " = ", cnt, " >= 0;");
    auto rd = OpenRzDest(t, d0, ln, "from_bytes");
    L("if (", ok, ") {");
    ind++;
    L("GS_MEMCPY(", Top(rd.stk), ", ", pay, ", (size_t)", pn, ");");
    Bump(rd.stk, pn);
    ind--;
    L("}");
    CloseRzDest(rd, cat("(", ok, " ? ", cnt, " : 0)"));
    return { rd.hdr, ok };
}

}  
