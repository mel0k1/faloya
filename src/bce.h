




























































































































#pragma once

namespace faloya {





inline bool IndexInRangeByType(Index *ix) {
    auto at = ix->obj->exprtype;
    if (at && IsPlainRef(at)) at = at->ref->sub;
    if (!at || !IsArrayKind(at, A_FIXED) || at->arr->size <= 0) return false;
    auto n = at->arr->size;
    if (auto lit = Is<IntLit>(ix->idx)) return !lit->uns && lit->val >= 0 && lit->val < n;
    auto it = ix->idx->exprtype;
    if (!it || !IsIntT(it)) return false;
    auto [lo, hi] = IntRange(it->intstorage);
    return lo >= 0 && hi < n;
}

struct BCE {
    Ast &ast;

    
    int idxelided = 0, idxtotal = 0, slelided = 0, sltotal = 0;
    map<pair<int, int>, pair<int, int>> lineout;   

    
    
    
    
    
    
    struct Span { SFunction *sf; int file, lo, hi; };
    vector<Span> checkfreespans;
    struct KeptCheck { Line line; string why; };
    vector<KeptCheck> checkfreefails;

    void CheckfreeSpans() {
        for (auto sf : ast.functions) {
            if (!sf->checkfree || !sf->body) continue;
            auto hi = sf->line.line;
            function<void(Node *)> last = [&](Node *n) {
                if (!n) return;
                if (n->line.fileidx == sf->line.fileidx) hi = std::max(hi, n->line.line);
                n->Children(last);
            };
            last(sf->body);
            checkfreespans.push_back({ sf, sf->line.fileidx, sf->line.line, hi });
        }
    }

    BCE(Ast &_ast) : ast(_ast) {}

    
    

    enum BaseKind : uint8_t { BK_ZERO, BK_VAR, BK_LEN, BK_TMP };

    struct Base {
        uint8_t kind = BK_ZERO;
        int id = 0;
        int gen = 0;
        bool operator==(const Base &o) const {
            return kind == o.kind && id == o.id && gen == o.gen;
        }
    };

    struct Fact { Base l, r; int64_t c; };   
    struct Term { bool ok = false; Base b; int64_t off = 0; };

    
    
    
    
    
    
    struct Lin { int tmp; Term x; int64_t m; bool quot, exact; };

    
    
    
    struct Gens {
        struct Gen { int id, gen; };
        small_vector<Gen, 8> gens;

        
        size_t At(int id) const {
            auto it = std::lower_bound(gens.begin(), gens.end(), id,
                                       [](const Gen &g, int i) { return g.id < i; });
            return (size_t)(it - gens.begin());
        }
        int Get(int id) const {
            auto i = At(id);
            return i < gens.size() && gens[i].id == id ? gens[i].gen : 0;
        }
        void Set(int id, int gen) {
            auto i = At(id);
            if (i < gens.size() && gens[i].id == id) gens[i].gen = gen;
            else gens.insert(gens.begin() + i, { id, gen });
        }
        
        static Gens Max(const Gens &a, const Gens &b) {
            Gens r;
            size_t i = 0, j = 0;
            while (i < a.gens.size() || j < b.gens.size()) {
                auto ai = i < a.gens.size() ? a.gens[i].id : INT32_MAX;
                auto bj = j < b.gens.size() ? b.gens[j].id : INT32_MAX;
                if (ai < bj) r.gens.push_back(a.gens[i++]);
                else if (bj < ai) r.gens.push_back(b.gens[j++]);
                else r.gens.push_back({ ai, std::max(a.gens[i++].gen, b.gens[j++].gen) });
            }
            return r;
        }
    };
    
    
    
    struct Flow {
        vector<Fact> facts;
        Gens vgen, pgen;   
        small_vector<Lin, 4> lins;
        
        
        small_vector<int, 4> lestated;
    };
    Flow flow;
    int nextgen = 0;

    static constexpr int64_t INF = INT64_MAX;
    
    
    
    
    static constexpr int64_t LENMAX = int64_t(1) << 48;
    static constexpr int64_t OFFCAP = int64_t(1) << 32;
    static constexpr int64_t CCAP = int64_t(1) << 60;
    static constexpr size_t MAXFACTS = 200;

    
    
    
    static int64_t SatAdd(int64_t a, int64_t b) {
        if (a == INF || b == INF) return INF;
        if (b > 0 && a > INF - 1 - b) return INF;
        if (b < 0 && a < INT64_MIN - b) return -CCAP;
        auto r = a + b;
        return r < -CCAP ? -CCAP : r;
    }
    static int64_t SatSub(int64_t a, int64_t b) {
        if (b == INT64_MIN) return a >= 0 ? INF : (a + INT64_MAX) + 1;
        return SatAdd(a, -b);
    }
    static bool SmallOff(int64_t c) { return c > -OFFCAP && c < OFFCAP; }

    
    unordered_map<const VarDef *, int> varid;
    vector<VarDef *> varof;
    int VarId(VarDef *v) {
        auto it = varid.find(v);
        if (it != varid.end()) return it->second;
        auto id = (int)varof.size();
        varid[v] = id;
        varof.push_back(v);
        return id;
    }
    static Base Zero() { return Base { BK_ZERO, 0, 0 }; }
    
    
    
    
    int nexttmp = 0;
    Base TmpBase() { return Base { BK_TMP, nexttmp++, 0 }; }
    
    
    
    unordered_map<int, pair<int64_t, int64_t>> tmpival;
    Base VarBase(VarDef *v) {
        auto id = VarId(v);
        return Base { BK_VAR, id, flow.vgen.Get(id) };
    }
    Base LenBase(int pid) { return Base { BK_LEN, pid, flow.pgen.Get(pid) }; }

    void AddFactB(Base l, Base r, int64_t c) {
        if (l == r) return;
        if (c >= CCAP) return;              
        if (c < -CCAP) c = -CCAP;
        for (auto &f : flow.facts)
            if (f.l == l && f.r == r) {
                if (c < f.c) f.c = c;       
                return;
            }
        if (flow.facts.size() >= MAXFACTS) flow.facts.erase(flow.facts.begin());
        flow.facts.push_back({ l, r, c });
    }

    
    
    
    

    enum UltKind { UK_OWNED, UK_STATIC, UK_OPAQUE };

    struct Place {
        VarDef *rootv = nullptr;
        vector<int> path;
        VarDef *ultv = nullptr;
        int ultkind = UK_OPAQUE;
        bool refcrossed = false;
        bool readsref = false;  
        bool lenmut = false;    
        bool slice = false;     
        bool inelem = false;    
        
        
        int64_t cap = -1;
        TypeExpr *arr = nullptr;   
    };
    vector<Place> places;
    map<pair<VarDef *, vector<int>>, int> placeids;

    
    
    
    
    
    
    
    
    pair<int, VarDef *> UltOf(VarDef *v, bool slot = false) {
        for (auto guard = 0; guard < 16; guard++) {
            if (!v) return { UK_STATIC, nullptr };
            auto t = v->type;
            if (!t) return { UK_OPAQUE, v };
            if (!IsRefOrSlice(t) || (slot && t->kind == TY_SLICE)) return { UK_OWNED, v };
            if (!v->refrootknown || !v->ref.Exact()) return { UK_OPAQUE, nullptr };
            v = v->ref.Root();
        }
        return { UK_OPAQUE, nullptr };
    }

    static bool IsSliceRef(TypeExpr *t) {
        return t && t->kind == TY_REF && t->ref->sub->kind == TY_SLICE;
    }

    
    
    
    
    
    
    static bool ReadsStoredRef(Node *cur) {
        auto isref = [](TypeExpr *t) { return t && IsRefOrSlice(t); };
        if (isref(cur->exprtype)) return true;
        auto d = Is<Dot>(cur);
        auto ix = Is<Index>(cur);
        auto obj = d ? d->obj : ix ? ix->obj : nullptr;
        auto t = obj ? obj->exprtype : nullptr;
        if (!t) return false;
        if (t->kind == TY_REF) t = t->ref->sub;
        if (ix) {
            if (t->kind == TY_ARRAY) return isref(t->arr->sub);
            return t->kind == TY_SLICE && isref(t->sub);
        }
        auto ft = FieldTypeOf(d);
        return ft && (isref(ft) || ft->kind == TY_GENERIC);
    }

    
    
    static TypeExpr *FieldTypeOf(Dot *d) {
        auto t = d->obj->exprtype;
        if (!t || d->fieldidx < 0) return nullptr;
        if (t->kind == TY_REF) t = t->ref->sub;
        TypeExpr *ft = nullptr;
        if (t->kind == TY_STRUCT) {
            auto inst = t->struc->inst;
            ft = inst ? inst->ftypes[d->fieldidx] : t->struc->st->fields[d->fieldidx].type;
        } else if (t->kind == TY_VARIANT) {
            auto v = t->var->variant;
            ft = v->fields[d->fieldidx].type;
            if (auto inst = t->var->adt->enu->inst)
                for (size_t vi = 0; vi < inst->en->variants.size(); vi++)
                    if (&inst->en->variants[vi] == v) ft = inst->vftypes[vi][d->fieldidx];
        }
        return ft;
    }

    
    
    
    static int64_t DeclCap(Node *n) {
        if (auto u = Is<Unary>(n); u && u->op == T_BITAND) n = u->child;
        TypeExpr *t = nullptr;
        if (auto id = Is<Ident>(n)) t = id->vdef ? id->vdef->type : nullptr;
        else if (auto d = Is<Dot>(n)) t = FieldTypeOf(d);
        if (t && t->kind == TY_REF) t = t->ref->sub;
        if (!t || t->kind != TY_ARRAY || t->arr->akind != A_LIMITED) return -1;
        return t->arr->size;
    }

    
    
    
    int PlaceFor(VarDef *root, const vector<int> &path, TypeExpr *t, bool readsref = false) {
        if (t && t->kind == TY_REF) t = t->ref->sub;
        auto slice = t && t->kind == TY_SLICE;
        auto arr = t && t->kind == TY_ARRAY && t->arr->akind != A_FIXED;
        if (!slice && !arr) return -1;
        return InternPlace(root, path, t, arr ? t : nullptr, [&](Place &P) {
            
            
            
            auto rootcross = !path.empty() && path[0] == -1 && root->type &&
                             root->type->kind == TY_REF;
            auto midcross =
                std::find(path.begin() + (rootcross ? 1 : 0), path.end(), -1) != path.end();
            P.refcrossed = midcross || (root->type && root->type->kind == TY_REF);
            P.readsref = readsref;
            if (!P.refcrossed) {
                P.ultkind = UK_OWNED;
                P.ultv = root;
            } else if (midcross) {
                P.ultkind = UK_OPAQUE;   
            } else {
                
                auto [k, u] = UltOf(root, slice);
                P.ultkind = k;
                P.ultv = u;
            }
        });
    }

    
    
    
    template<typename F>
    int InternPlace(VarDef *root, const vector<int> &path, TypeExpr *t, TypeExpr *arr, F own) {
        auto key = pair<VarDef *, vector<int>>(root, path);
        auto it = placeids.find(key);
        if (it != placeids.end()) return it->second;
        Place P;
        P.rootv = root;
        P.path = path;
        own(P);
        auto isarr = t->kind == TY_ARRAY;
        P.lenmut = isarr && t->arr->akind != A_VAR;
        P.arr = arr;
        if (isarr && t->arr->akind == A_LIMITED && t->arr->size >= 0) P.cap = t->arr->size;
        P.slice = !isarr;
        P.inelem = !isarr || (t->arr->akind != A_GROW && t->arr->akind != A_GROWSHRINK);
        auto id = (int)places.size();
        places.push_back(P);
        placeids[key] = id;
        return id;
    }

    
    
    
    int PlaceOf(Node *n, bool *failidx = nullptr, bool *crossed = nullptr,
                TypeExpr *as = nullptr) {
        vector<int> path;
        VarDef *root = nullptr;
        auto readsref = false;
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) { root = id->vdef; break; }
            if (auto d = Is<Dot>(cur); d && d->fieldidx >= 0) {
                
                
                
                if ((as && cur == n) ? IsRefOrSlice(as) : ReadsStoredRef(d)) {
                    path.push_back(-1);
                    if (crossed) *crossed = true;
                    
                    
                    
                    auto ft = cur == n ? (as ? as : FieldTypeOf(d)) : nullptr;
                    if (!ft || ft->kind != TY_SLICE) readsref = true;
                }
                path.push_back(d->fieldidx);
                if (d->obj->exprtype && d->obj->exprtype->kind == TY_REF) path.push_back(-1);
                cur = d->obj;
                continue;
            }
            if (Is<Index>(cur)) {
                if (failidx) *failidx = true;
                if (crossed && ReadsStoredRef(cur)) *crossed = true;
            }
            return -1;
        }
        if (!root) return -1;
        std::reverse(path.begin(), path.end());
        return PlaceFor(root, path, as ? as : n->exprtype, readsref);
    }

    
    
    int PlaceOfVar(VarDef *v) {
        if (!v) return -1;
        auto t = v->type;
        vector<int> path;
        while (v->componentowner) {
            path.push_back(v->componentfield);
            v = v->componentowner;
        }
        std::reverse(path.begin(), path.end());
        return PlaceFor(v, path, t);
    }

    
    
    
    
    
    
    
    
    
    int ElemPlaceOf(Node *n) {
        auto ix = Is<Index>(n);
        auto t = n->exprtype;
        if (!ix || !t || (t->kind != TY_SLICE && t->kind != TY_ARRAY)) return -1;
        if (t->kind == TY_ARRAY && t->arr->akind == A_FIXED) return -1;
        auto at = ix->obj->exprtype;
        if (at && at->kind == TY_REF) at = at->ref->sub;
        if (!at || at->kind != TY_ARRAY || at->arr->akind == A_FIXED) return -1;
        auto apid = PlaceOf(ix->obj);
        if (apid < 0) return -1;
        auto path = places[apid].path;
        path.push_back(-2);
        auto arr = t->kind == TY_ARRAY ? t : nullptr;
        return InternPlace(places[apid].rootv, path, t, arr, [&](Place &P) {
            P.ultkind = places[apid].ultkind;
            P.ultv = places[apid].ultv;
            P.refcrossed = true;
            P.readsref = places[apid].readsref;
        });
    }

    
    
    
    Term FreshLenOf(Node *init) {
        if (auto al = Is<ArrayLit>(init)) {
            if (al->capexpr) return Term { true, Zero(), 0 };
            if (al->fillval) return TermOf(al->fillcount);
            return Term { true, Zero(), (int64_t)al->elems.size() };
        }
        if (auto sl = Is<StrLit>(init))
            return Term { true, Zero(), (int64_t)sl->val.size() };
        return {};
    }

    
    

    set<VarDef *> addrof;     
    set<VarDef *> intvars;    
    set<VarDef *> wdecl;      
    set<VarDef *> wbad;       
    struct WKinds { bool inc = false, dec = false, setw = false; };
    map<VarDef *, WKinds> wkinds;
    map<VarDef *, set<int>> relpids;   

    struct Cand {
        bool ge0 = true;         
        bool wrapfree = true;    
        
        
        bool setdec = true, setinc = true;
        
        
        vector<pair<int, int64_t>> le;
        bool declseen = false;
    };
    map<VarDef *, Cand> cands;
    
    
    
    
    
    
    
    map<VarDef *, bool> gge0;
    set<VarDef *> gaddr;   

    
    set<VarDef *> ge0;
    
    
    
    bool seedglobals = false;

    bool GlobalCand(VarDef *v) {
        auto it = gge0.find(v);
        return it != gge0.end() && it->second;
    }
    bool Ge0(VarDef *v) { return ge0.count(v) || (seedglobals && GlobalCand(v)); }
    
    
    Cand *CandOf(VarDef *v) {
        auto it = cands.find(v);
        if (it != cands.end()) return &it->second;
        if (!seedglobals || !GlobalCand(v)) return nullptr;
        auto &c = cands[v];
        c.declseen = true;
        return &c;
    }
    map<VarDef *, vector<pair<int, int64_t>>> lelen;   
    
    
    set<int> rebound;
    map<VarDef *, int> mono;   

    bool Reach(VarDef *v) {
        return v && (v->isglobal || v->captured || addrof.count(v) != 0);
    }

    
    

    
    
    
    
    
    
    struct Effects {
        set<int> params, intparams;
        set<VarDef *> vars, intvars;
        bool opaque = false, intopaque = false;
        bool operator==(const Effects &o) const {
            return params == o.params && intparams == o.intparams && vars == o.vars &&
                   intvars == o.intvars && opaque == o.opaque && intopaque == o.intopaque;
        }
        void Merge(const Effects &o) {
            params.insert(o.params.begin(), o.params.end());
            intparams.insert(o.intparams.begin(), o.intparams.end());
            vars.insert(o.vars.begin(), o.vars.end());
            intvars.insert(o.intvars.begin(), o.intvars.end());
            opaque = opaque || o.opaque;
            intopaque = intopaque || o.intopaque;
        }
    };
    map<FnSpec *, Effects> effects;
    Effects *effsum = nullptr;   
    set<VarDef *> ownvars;       
    map<VarDef *, vector<int>> classparams;   
    set<VarDef *> ownclasses;    

    
    
    
    
    
    bool HandedApart(const Place &P, VarDef *v) {
        return P.ultkind == UK_OPAQUE && P.ultv && ownclasses.count(P.ultv) &&
               OwnerTarget(v).kind == TG_OWN;
    }

    
    
    
    
    
    map<FnSpec *, vector<FnSpec *>> callees;
    map<FnSpec *, int> nsites;
    set<FnSpec *> opaquesites;
    vector<FnSpec *> calleesfirst;

    
    
    
    
    struct Iface {
        vector<pair<bool, int>> bases;   
    };
    map<FnSpec *, Iface> ifaces;

    
    
    
    struct Sites {
        int seen = 0;
        bool viable = true;
        vector<int64_t> c;
    };
    map<FnSpec *, Sites> sites;

    
    
    
    
    
    

    set<VarDef *> indexarrays;       
    map<VarDef *, int> ixpid;        
    
    
    map<int, pair<int, int>> tmplen;

    void IndexPlaces() {
        ixpid.clear();
        tmplen.clear();
        for (auto a : indexarrays) ixpid[a] = PlaceOfVar(a);
    }

    
    pair<int, int> IndexBoundOf(const Base &b) {
        if (b.kind == BK_TMP) {
            auto it = tmplen.find(b.id);
            return it == tmplen.end() ? pair { -1, 0 } : it->second;
        }
        if (b.kind != BK_VAR) return { -1, 0 };
        auto t = varof[b.id]->type;
        if (!IsIndexT(t)) return { -1, 0 };
        auto it = ixpid.find(t->ix->arr);
        return { it == ixpid.end() ? -1 : it->second, t->ix->upto ? 0 : -1 };
    }

    
    
    Term IndexTypeTerm(TypeExpr *t) {
        if (mode == M_KILLS || !IsIndexT(t)) return {};
        auto it = ixpid.find(t->ix->arr);
        if (it == ixpid.end() || it->second < 0) return {};
        auto m = TmpBase();
        auto hi = t->intstorage == IS_U64 ? LENMAX
                                          : std::min(IntRange(t->intstorage).second, LENMAX);
        tmpival[m.id] = { 0, hi };
        tmplen[m.id] = { it->second, t->ix->upto ? 0 : -1 };
        return Term { true, m, 0 };
    }

    
    

    bool Query(Base l, Base r, int64_t c) {
        auto d = Dist(l, r);
        return d != INF && d <= c;
    }

    
    int64_t Dist(Base l, Base r) {
        if (l == r) return 0;
        return DistsFrom(l, span(&r, 1))[0];
    }

    
    
    
    
    small_vector<int64_t, 4> DistsFrom(Base l, span<const Base> to, bool rev = false) {
        small_vector<Base, 16> nodes = { Zero(), l };
        auto add = [&](const Base &b) {
            for (auto &x : nodes) if (x == b) return;
            nodes.push_back(b);
        };
        for (auto &b : to) add(b);
        for (auto &f : flow.facts) { add(f.l); add(f.r); }
        
        if (!ixpid.empty())
            for (size_t i = 0; i < nodes.size(); i++) {
                auto [pid, slack] = IndexBoundOf(nodes[i]);
                if (pid >= 0) add(LenBase(pid));
            }
        
        if (!lelen.empty())
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].kind != BK_VAR) continue;
                auto v = varof[nodes[i].id];
                auto it = lelen.find(v);
                if (it != lelen.end())
                    for (auto [pid, c] : it->second)
                        if (LeLenHolds(nodes[i], v, pid)) add(LenBase(pid));
            }
        auto idx = [&](const Base &b) {
            for (size_t i = 0; i < nodes.size(); i++) if (nodes[i] == b) return (int)i;
            return -1;
        };
        struct Edge { int a, b; int64_t w; };
        small_vector<Edge, 32> edges;
        for (auto &f : flow.facts) edges.push_back({ idx(f.l), idx(f.r), f.c });
        for (size_t i = 0; i < nodes.size(); i++) {
            auto &b = nodes[i];
            if (b.kind == BK_LEN) {
                edges.push_back({ 0, (int)i, 0 });         
                
                auto cap = places[b.id].cap;
                edges.push_back({ (int)i, 0, cap >= 0 && cap < LENMAX ? cap : LENMAX });
            } else if (b.kind == BK_TMP) {
                auto it = tmpival.find(b.id);
                if (it != tmpival.end()) {
                    edges.push_back({ 0, (int)i, SatSub(0, it->second.first) });
                    edges.push_back({ (int)i, 0, it->second.second });
                }
            } else if (b.kind == BK_VAR) {
                auto v = varof[b.id];
                auto t = v->type;
                if (t && t->kind == TY_INT && IntBits(t->intstorage) < 64) {
                    auto [lo, hi] = IntRange(t->intstorage);
                    edges.push_back({ 0, (int)i, -lo });
                    edges.push_back({ (int)i, 0, hi });
                }
                if (Ge0(v)) edges.push_back({ 0, (int)i, 0 });
                if (IsIndexT(t)) edges.push_back({ 0, (int)i, 0 });   
                auto it = lelen.find(v);
                if (it != lelen.end())
                    for (auto [pid, c] : it->second) {
                        if (!LeLenHolds(b, v, pid)) continue;
                        auto li = idx(LenBase(pid));
                        if (li >= 0) edges.push_back({ (int)i, li, c });
                    }
            }
        }
        
        if (!ixpid.empty())
            for (size_t i = 0; i < nodes.size(); i++) {
                auto [pid, slack] = IndexBoundOf(nodes[i]);
                if (pid < 0) continue;
                auto li = idx(LenBase(pid));
                if (li >= 0) edges.push_back({ (int)i, li, slack });
            }
        small_vector<int64_t, 16> dist;
        dist.resize(nodes.size(), INF);
        dist[idx(l)] = 0;
        for (size_t round = 0; round < nodes.size(); round++) {
            auto changed = false;
            for (auto &e : edges) {
                auto a = rev ? e.b : e.a, b = rev ? e.a : e.b;
                if (dist[a] == INF) continue;
                auto nd = SatAdd(dist[a], e.w);
                if (nd < dist[b]) { dist[b] = nd; changed = true; }
            }
            if (!changed) break;
        }
        small_vector<int64_t, 4> r;
        for (auto &b : to) r.push_back(dist[idx(b)]);
        return r;
    }

    
    
    
    
    

    struct Ival { bool ok = false; int64_t lo = 0, hi = 0; };

    
    
    
    
    
    Ival BoundsOf(const Term &t) {
        if (!t.ok) return {};
        if (t.b.kind == BK_ZERO) return Ival { true, t.off, t.off };
        auto up = Dist(t.b, Zero());   
        if (up == INF) return {};
        auto dn = Dist(Zero(), t.b);   
        if (dn == INF) return {};
        auto hi = SatAdd(up, t.off), lo = SatAdd(SatSub(0, dn), t.off);
        if (hi == INF || lo == INF || hi > CCAP || lo < -CCAP) return {};
        if (lo > hi) return {};   
        return Ival { true, lo, hi };
    }

    
    static bool MulIn(int64_t a, int64_t b, int64_t &r) {
        if (a >= CCAP || a <= -CCAP || b >= CCAP || b <= -CCAP) return false;
        if (a == 0 || b == 0) { r = 0; return true; }
        auto aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
        if (aa > CCAP / bb) return false;
        r = a * b;
        return true;
    }

    
    
    
    static TypeExpr *OpType(Binary *b) { return b->left->exprtype; }

    
    
    static bool MachineInt(TypeExpr *t) {
        return t && t->kind == TY_INT && t->intstorage != IS_U64 && t->intstorage != IS_VARINT;
    }

    
    
    
    
    
    static bool FitsType(TypeExpr *t, int64_t lo, int64_t hi) {
        if (!MachineInt(t)) return false;
        auto [tlo, thi] = IntRange(t->intstorage);
        return lo >= tlo && hi <= thi;
    }

    Term IvalTerm(TypeExpr *t, int64_t lo, int64_t hi) {
        if (!FitsType(t, lo, hi)) return {};
        auto m = TmpBase();
        tmpival[m.id] = { lo, hi };
        return Term { true, m, 0 };
    }

    
    
    
    Term MulTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = OpType(b);
        if (!MachineInt(t)) return {};
        auto lt = TermOf(b->left), rt = TermOf(b->right);
        if (!lt.ok || !rt.ok) return {};
        if (rt.b.kind == BK_ZERO && rt.off >= 1) return ScaledTerm(t, lt, rt.off);
        if (lt.b.kind == BK_ZERO && lt.off >= 1) return ScaledTerm(t, rt, lt.off);
        auto li = BoundsOf(lt), ri = BoundsOf(rt);
        if (!li.ok || !ri.ok) return {};
        int64_t c[4];
        if (!MulIn(li.lo, ri.lo, c[0]) || !MulIn(li.lo, ri.hi, c[1]) ||
            !MulIn(li.hi, ri.lo, c[2]) || !MulIn(li.hi, ri.hi, c[3]))
            return {};
        auto lo = c[0], hi = c[0];
        for (auto i = 1; i < 4; i++) {
            lo = std::min(lo, c[i]);
            hi = std::max(hi, c[i]);
        }
        return IvalTerm(t, lo, hi);
    }

    
    
    
    
    Term IvalSumTerm(Binary *b, const Term &lt, const Term &rt) {
        if (mode == M_KILLS) return {};
        auto li = BoundsOf(lt), ri = BoundsOf(rt);
        if (!li.ok || !ri.ok) return {};
        auto minus = b->op == T_MINUS;
        auto lo = minus ? li.lo - ri.hi : li.lo + ri.lo;
        auto hi = minus ? li.hi - ri.lo : li.hi + ri.hi;
        if (lo < -CCAP || hi > CCAP) return {};
        auto s = IvalTerm(OpType(b), lo, hi);
        if (!s.ok) return {};
        
        
        
        
        auto near = [&](const Term &x, int64_t plo, int64_t phi) {
            if (!SmallOff(plo) || !SmallOff(phi) || !SmallOff(phi - plo)) return;
            AddFactB(s.b, x.b, SatAdd(x.off, phi));               
            AddFactB(x.b, s.b, SatSub(0, SatAdd(x.off, plo)));    
        };
        if (minus) {
            near(lt, -ri.hi, -ri.lo);
        } else {
            near(lt, ri.lo, ri.hi);
            near(rt, li.lo, li.hi);
        }
        return s;
    }

    
    
    
    
    
    
    
    
    
    Term NarrowStepTerm(TypeExpr *t, const Term &x, int64_t d) {
        if (mode == M_KILLS || !CmpAdmissible(x)) return {};
        auto [lo, hi] = IntRange(t->intstorage);
        if (d <= lo - hi - 1 || d >= hi - lo + 1) return {};
        auto off = SatAdd(x.off, d);
        if (!SmallOff(off)) return {};
        auto s = IvalTerm(t, lo, hi);
        if (!s.ok) return {};
        auto fits = d >= 0 ? Query(x.b, Zero(), SatSub(hi, off))
                           : Query(Zero(), x.b, SatSub(off, lo));
        if (d >= 0 || fits) AddFactB(s.b, x.b, off);              
        if (d <= 0 || fits) AddFactB(x.b, s.b, SatSub(0, off));   
        return s;
    }

    
    

    static Base TmpB(int id) { return Base { BK_TMP, id, 0 }; }
    static constexpr size_t MAXLINS = 16;

    Lin *LinOf(const Base &b) {
        if (b.kind != BK_TMP) return nullptr;
        for (auto &l : flow.lins) if (l.tmp == b.id) return &l;
        return nullptr;
    }

    
    
    
    
    bool LinExact(Lin &l) {
        if (!l.exact && l.quot && l.x.off > 0 && Query(Zero(), TmpB(l.tmp), 0)) l.exact = true;
        return l.exact;
    }

    
    void AddLin(const Lin &l) {
        auto &ls = flow.lins;
        if (ls.size() >= MAXLINS) {
            size_t drop = 0;
            for (size_t i = 0; i < ls.size(); i++)
                if (!Mentioned(TmpB(ls[i].tmp))) { drop = i; break; }
            ls.erase(ls.begin() + drop);
        }
        ls.push_back(l);
    }

    
    
    
    
    
    void LinRelate(Lin l) {
        small_vector<Base, 4> to;
        small_vector<int, 4> who;
        if (LinExact(l))
            for (size_t i = 0; i < flow.lins.size(); i++) {
                auto &e = flow.lins[i];
                if (e.m == l.m && e.quot == l.quot && Mentioned(TmpB(e.tmp)) && LinExact(e)) {
                    to.push_back(e.x.b);
                    who.push_back((int)i);
                }
            }
        if (!to.empty()) {
            auto fwd = DistsFrom(l.x.b, to), bwd = DistsFrom(l.x.b, to, true);
            
            
            auto scaled = [&](int64_t d, int64_t xoff, int64_t yoff) -> int64_t {
                if (d == INF) return INF;
                auto c = SatAdd(d, SatSub(xoff, yoff));
                if (c == INF || c <= -CCAP || c >= CCAP) return INF;
                if (l.quot) return c >= 0 ? (c + l.m - 1) / l.m : -((-c) / l.m);
                int64_t r;
                return MulIn(c, l.m, r) ? r : INF;
            };
            for (size_t k = 0; k < to.size(); k++) {
                auto &e = flow.lins[who[k]];
                auto c1 = scaled(fwd[k], l.x.off, e.x.off);
                if (c1 != INF) AddFactB(TmpB(l.tmp), TmpB(e.tmp), c1);
                auto c2 = scaled(bwd[k], e.x.off, l.x.off);
                if (c2 != INF) AddFactB(TmpB(e.tmp), TmpB(l.tmp), c2);
            }
        }
        AddLin(l);
    }

    
    
    
    
    Term ScaledTerm(TypeExpr *t, const Term &x, int64_t m) {
        auto xi = BoundsOf(x);
        int64_t lo, hi;
        if (!xi.ok || !MulIn(xi.lo, m, lo) || !MulIn(xi.hi, m, hi) || !FitsType(t, lo, hi))
            return {};
        if (x.b.kind == BK_ZERO) return IvalTerm(t, lo, hi);
        if (m == 1) return x;
        for (auto &e : flow.lins) {
            int64_t d;
            if (!e.quot && e.m == m && e.x.b == x.b && MulIn(x.off - e.x.off, m, d) && SmallOff(d))
                return Term { true, TmpB(e.tmp), d };
        }
        auto r = IvalTerm(t, lo, hi);
        if (xi.lo >= 0) AddFactB(x.b, r.b, SatSub(0, x.off));   
        LinRelate(Lin { r.b.id, x, m, false, true });
        return r;
    }

    
    
    Term ShlTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = OpType(b);
        if (!MachineInt(t)) return {};
        auto kt = TermOf(b->right);
        if (!kt.ok || kt.b.kind != BK_ZERO) return {};
        auto k = kt.off & (IntBits(t->intstorage) - 1);
        if (k >= 60) return {};
        auto xt = TermOf(b->left);
        if (!xt.ok) return {};
        return ScaledTerm(t, xt, int64_t(1) << k);
    }

    
    
    
    void QuotBack(const Term &t) {
        if (!t.ok || mode == M_KILLS) return;
        auto l = LinOf(t.b);
        if (!l || !l->quot || !LinExact(*l)) return;
        auto x = l->x;
        auto m = l->m;
        auto up = Dist(t.b, Zero()), dn = Dist(Zero(), t.b);
        int64_t v;
        if (up != INF && up < CCAP && MulIn(SatAdd(up, 1), m, v))
            AddFactB(x.b, Zero(), SatSub(SatSub(v, 1), x.off));
        if (dn != INF && dn > -CCAP && MulIn(SatSub(0, dn), m, v))
            AddFactB(Zero(), x.b, SatSub(x.off, v));
    }

    
    
    
    

    Term LenTermOf(Node *obj) {
        auto t = obj->exprtype;
        if (t && t->kind == TY_REF) t = t->ref->sub;
        if (t && t->kind == TY_ARRAY && t->arr->akind == A_FIXED && t->arr->size >= 0)
            return Term { true, Zero(), t->arr->size };
        auto pid = PlaceOf(obj);
        if (pid >= 0) return Term { true, LenBase(pid), 0 };
        return Term {};
    }

    
    
    set<Node *> effectfulterms;

    Term TermOf(Node *n) {
        if (!n || effectfulterms.count(n)) return {};
        if (auto i = Is<IntLit>(n)) {
            if (i->uns || i->val >= CCAP || i->val <= -CCAP) return {};
            return Term { true, Zero(), i->val };
        }
        if (auto id = Is<Ident>(n)) {
            if (!IntBaseVar(id->vdef)) return {};
            return Term { true, VarBase(id->vdef), 0 };
        }
        if (auto d = Is<Dot>(n); d && d->member == B_LEN) return LenTermOf(d->obj);
        if (auto b = Is<Binary>(n); b && (b->op == T_PLUS || b->op == T_MINUS)) {
            auto t = OpType(b);
            if (!MachineInt(t)) return {};
            auto lt = TermOf(b->left), rt = TermOf(b->right);
            if (!lt.ok || !rt.ok) return {};
            if (b->op == T_PLUS && lt.b.kind == BK_ZERO) std::swap(lt, rt);
            
            if (rt.b.kind != BK_ZERO)
                return Derived(n, [&] { return IvalSumTerm(b, lt, rt); });
            if (t->intstorage != IS_I64)
                return Derived(n, [&] {
                    return NarrowStepTerm(t, lt, b->op == T_PLUS ? rt.off : SatSub(0, rt.off));
                });
            auto off = b->op == T_PLUS ? SatAdd(lt.off, rt.off) : SatSub(lt.off, rt.off);
            if (off == INF) return {};
            if (lt.b.kind != BK_ZERO && !SmallOff(off)) return {};
            return Term { true, lt.b, off };
        }
        if (auto b = Is<Binary>(n); b && b->op == T_MUL)
            return Derived(n, [&] { return MulTerm(b); });
        if (auto b = Is<Binary>(n); b && b->op == T_SHL)
            return Derived(n, [&] { return ShlTerm(b); });
        if (auto b = Is<Binary>(n); b && (b->op == T_MOD || b->op == T_BITAND))
            return Derived(n, [&] { return RangedOpTerm(b); });
        if (auto b = Is<Binary>(n); b && (b->op == T_DIV || b->op == T_SHR))
            return Derived(n, [&] { return QuotTerm(b); });
        if (auto ac = Is<AsCast>(n))
            return Derived(n, [&] {
                auto t = CastTerm(ac);
                return t.ok ? t : IndexTypeTerm(ac->totype);
            });
        
        
        
        if (auto ib = Is<InlineBlock>(n)) {
            auto t = BlockValueTerm(ib->body, ib->sf);
            if (t.ok || !IsIndexT(n->exprtype)) return t;
        } else if (auto bl = Is<Block>(n)) {
            auto t = BlockValueTerm(bl, nullptr);
            if (t.ok || !IsIndexT(n->exprtype)) return t;
        }
        
        
        if (Is<Index>(n) || Is<Dot>(n) || Is<Call>(n))
            return Derived(n, [&] {
                return IsIndexT(n->exprtype) ? IndexTypeTerm(n->exprtype) : TypeRangeTerm(n);
            });
        
        if (Is<IfExpr>(n) && !IsIndexT(n->exprtype)) {
            auto it = mode == M_KILLS ? derived.end() : derived.find(n);
            return it != derived.end() && it->second.second == nextgen ? it->second.first : Term {};
        }
        
        if (IsIndexT(n->exprtype)) return Derived(n, [&] { return IndexTypeTerm(n->exprtype); });
        return {};
    }

    
    
    Term TypeRangeTerm(Node *n) {
        if (mode == M_KILLS) return {};
        auto t = n->exprtype;
        if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT || IntBits(t->intstorage) >= 64)
            return {};
        auto [lo, hi] = IntRange(t->intstorage);
        return IvalTerm(t, lo, hi);
    }

    
    
    
    
    Term BlockValueTerm(Block *b, SFunction *sf) {
        
        
        if (sf) {
            auto last = !b->tail && !b->stmts.empty() ? Is<Return>(b->stmts.back()) : nullptr;
            for (auto st : b->stmts)
                if (st != last && ReturnsTo(st, sf)) return {};
            if (ReturnsTo(b->tail, sf)) return {};
            if (last) for (auto v : last->vals) if (ReturnsTo(v, sf)) return {};
        }
        
        
        for (auto st : b->stmts) if (Is<Defer>(st)) return {};
        if (b->tail) return TermOf(b->tail);
        if (b->stmts.empty()) return {};
        auto r = Is<Return>(b->stmts.back());
        if (sf && r && r->target == sf && r->vals.size() == 1) return TermOf(r->vals[0]);
        return {};
    }

    
    
    
    
    map<Node *, pair<Term, int>> derived;

    template<typename F> Term Derived(Node *n, F f) {
        if (mode == M_KILLS) return {};   
        auto it = derived.find(n);
        if (it != derived.end()) return it->second.second == nextgen ? it->second.first
                                                                    : Term {};
        auto t = f();
        derived[n] = { t, nextgen };
        return t;
    }

    
    
    
    
    Term RangedOpTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = OpType(b);
        if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT) return {};
        auto rt = TermOf(b->right);
        if (!rt.ok) return {};
        auto m = TmpBase();
        if (b->op == T_MOD) {
            AddFactB(Zero(), m, 0);   
            
            
            
            if (Query(Zero(), rt.b, rt.off)) AddFactB(m, rt.b, SatSub(rt.off, 1));
            else if (rt.b.kind == BK_ZERO && rt.off < 0)
                AddFactB(m, Zero(), SatSub(SatSub(0, rt.off), 1));
        } else if (CmpAdmissible(rt) && Query(Zero(), rt.b, rt.off)) {
            
            
            
            
            AddFactB(Zero(), m, 0);
            AddFactB(m, rt.b, rt.off);
        }
        return Term { true, m, 0 };
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    Term QuotTerm(Binary *b) {
        if (mode == M_KILLS) return {};
        auto t = OpType(b);
        if (!MachineInt(t)) return {};
        auto shift = b->op == T_SHR;
        auto xt = TermOf(b->left);
        if (!xt.ok) return {};
        auto nonneg = NoWrap(xt, t->intstorage) && Query(Zero(), xt.b, xt.off);
        auto kt = TermOf(b->right);
        if (!kt.ok || kt.b.kind != BK_ZERO) {
            if (!nonneg) return {};
            if (!shift && !(kt.ok && NoWrap(kt, t->intstorage) && Query(Zero(), kt.b, kt.off)))
                return {};
            auto q = TmpBase();
            AddFactB(Zero(), q, 0);
            AddFactB(q, xt.b, xt.off);
            return Term { true, q, 0 };
        }
        auto k = shift ? kt.off & (IntBits(t->intstorage) - 1) : kt.off;
        if (!shift && k < 1) return {};
        if (k == (shift ? 0 : 1)) return xt;
        auto iv = BoundsOf(xt);
        auto bounded = iv.ok && (shift || iv.lo >= 0);
        
        
        auto m = shift ? (k < 60 ? int64_t(1) << k : 0) : k;
        auto lin = m >= 2 && m < CCAP && xt.b.kind != BK_ZERO && (shift || nonneg);
        if (!nonneg && !bounded && !lin) return {};
        auto exact = shift ? NoWrap(xt, t->intstorage) : nonneg;
        
        
        if (lin)
            for (auto &e : flow.lins) {
                if (!e.quot || e.m != m || !(e.x.b == xt.b)) continue;
                auto d = xt.off - e.x.off;
                if (d == 0 || (exact && LinExact(e) && d % m == 0))
                    return Term { true, TmpB(e.tmp), d / m };
            }
        auto q = TmpBase();
        if (bounded) {
            tmpival[q.id] = shift ? pair<int64_t, int64_t> { iv.lo >> k, iv.hi >> k }
                                  : pair<int64_t, int64_t> { iv.lo / k, iv.hi / k };
        }
        if (nonneg) {
            AddFactB(Zero(), q, 0);
            auto below = (shift || k >= 2) && Query(Zero(), xt.b, SatSub(xt.off, 1));
            AddFactB(q, xt.b, below ? SatSub(xt.off, 1) : xt.off);
        }
        if (lin) LinRelate(Lin { q.id, xt, m, true, exact });
        return Term { true, q, 0 };
    }

    
    
    
    
    bool NonNeg(Node *n) {
        auto t = TermOf(n);
        if (t.ok && CmpAdmissible(t) && Query(Zero(), t.b, t.off)) return true;
        auto b = Is<Binary>(n);
        if (!b || b->op != T_MINUS || effectfulterms.count(b)) return false;
        if (!MachineInt(OpType(b))) return false;
        auto lt = TermOf(b->left), rt = TermOf(b->right);
        return lt.ok && rt.ok && CmpAdmissible(lt) && CmpAdmissible(rt) &&
               Query(Zero(), rt.b, rt.off) && Query(rt.b, lt.b, SatSub(lt.off, rt.off));
    }

    
    
    
    
    void JudgeUnsigned(Binary *b) {
        if (mode != M_JUDGE || effectfulterms.count(b)) return;
        auto t = OpType(b);
        if (!t || t->kind != TY_INT || t->intstorage == IS_VARINT || IsUnsigned(t->intstorage))
            return;
        if (!NonNeg(b->left)) return;
        if (b->op != T_SHR) {
            auto rt = TermOf(b->right);
            if (!rt.ok || !CmpAdmissible(rt) || !Query(Zero(), rt.b, SatSub(rt.off, 1))) return;
        }
        b->nonneg = true;
    }

    
    
    
    Term CastTerm(AsCast *ac) {
        if (mode == M_KILLS) return {};
        auto tt = ac->totype, st = ac->child->exprtype;
        if (!tt || tt->kind != TY_INT || tt->intstorage == IS_VARINT) return {};
        if (!st || st->kind != TY_INT || st->intstorage == IS_VARINT) return {};
        auto ct = TermOf(ac->child);
        if (!ct.ok) return {};
        
        
        auto [slo, shi] = IntRange(st->intstorage);
        auto [tlo, thi] = IntRange(tt->intstorage);
        if (slo < tlo && !Query(Zero(), ct.b, SatSub(ct.off, tlo))) return {};
        if (shi > thi && !Query(ct.b, Zero(), SatSub(thi, ct.off))) return {};
        return ct;
    }

    
    
    
    
    
    bool CmpAdmissible(const Term &t) {
        if (!t.ok) return false;
        if (t.b.kind == BK_ZERO || t.b.kind == BK_LEN || t.off == 0) return true;
        return NoWrap(t, IS_I64);
    }

    
    

    enum Mode { M_KILLS, M_RECORD, M_JUDGE };
    Mode mode = M_JUDGE;
    set<int> *ksum = nullptr;          
    set<int> *shsum = nullptr;         
    set<int> *rbsum = nullptr;         
    set<VarDef *> *vksum = nullptr;    
    bool anybump = false;
    
    
    set<VarDef *> *freshvars = nullptr;
    int loopdepth = 0;          

    
    
    
    struct KillsScope {
        BCE &b;
        Flow flow;
        Mode mode;
        bool anybump;
        set<int> *ksum, *shsum, *rbsum;
        set<VarDef *> *vksum;
        KillsScope(BCE &b, set<int> *ks = nullptr, set<int> *sh = nullptr,
                   set<VarDef *> *vks = nullptr, set<int> *rb = nullptr)
            : b(b), flow(b.flow), mode(b.mode), anybump(b.anybump), ksum(b.ksum),
              shsum(b.shsum), rbsum(b.rbsum), vksum(b.vksum) {
            b.ksum = ks;
            b.shsum = sh;
            b.rbsum = rb;
            b.vksum = vks;
            b.anybump = false;
            b.mode = M_KILLS;
        }
        ~KillsScope() {
            b.flow = std::move(flow);
            b.mode = mode;
            b.anybump = anybump;
            b.ksum = ksum;
            b.shsum = shsum;
            b.rbsum = rbsum;
            b.vksum = vksum;
        }
    };

    void BumpVar(VarDef *v, bool bridge = true) {
        if (!freshvars || !freshvars->count(v)) anybump = true;
        auto old = VarBase(v);
        flow.vgen.Set(VarId(v), ++nextgen);
        if (vksum) vksum->insert(v);
        if (!bridge) return;
        auto it = mono.find(v);
        if (it == mono.end()) return;
        if (it->second > 0) AddFactB(old, VarBase(v), 0);   
        else AddFactB(VarBase(v), old, 0);                  
    }

    bool Mentioned(const Base &b) {
        for (auto &f : flow.facts) if (f.l == b || f.r == b) return true;
        return false;
    }

    
    
    void BumpPlace(int pid, int dir, bool rebind = false) {   
        if (!freshvars || !freshvars->count(places[pid].rootv)) anybump = true;
        auto old = LenBase(pid);
        flow.pgen.Set(pid, ++nextgen);
        if (ksum) ksum->insert(pid);
        if (rebind && rbsum) rbsum->insert(pid);
        else if (shsum && dir <= 0) shsum->insert(pid);
        
        
        
        
        if (!dir || !Mentioned(old)) return;
        if (dir > 0) AddFactB(old, LenBase(pid), 0);
        else AddFactB(LenBase(pid), old, 0);
    }

    
    
    
    
    void ExactLenStep(int pid, int64_t delta) {
        if (mode == M_KILLS) {
            BumpPlace(pid, delta >= 0 ? 1 : -1);
            return;
        }
        auto old = LenBase(pid);
        flow.pgen.Set(pid, ++nextgen);
        auto nw = LenBase(pid);
        AddFactB(nw, old, delta);            
        AddFactB(old, nw, SatSub(0, delta)); 
    }

    
    void ExactLenIs(int pid, const Term &t) {
        if (mode == M_KILLS || !t.ok) return;
        auto lb = LenBase(pid);
        AddFactB(lb, t.b, t.off);
        AddFactB(t.b, lb, SatSub(0, t.off));
        QuotBack(t);
    }

    
    
    
    
    bool AffectedByWrite(int ppid, int recvpid, int tk, VarDef *tu, VarDef *chainroot) {
        auto &P = places[ppid];
        if (P.ultkind == UK_STATIC) return false;
        if (chainroot && P.rootv == chainroot) return true;
        if (recvpid >= 0) {
            if (ppid == recvpid) return true;
            auto &R = places[recvpid];
            if (R.ultkind == UK_OWNED) {
                if (P.ultkind == UK_OWNED && P.ultv == R.ultv)
                    return P.refcrossed || R.refcrossed;   
                return P.ultkind == UK_OPAQUE && Reach(R.ultv) && !HandedApart(P, R.ultv);
            }
            return Reach(P.rootv) || (P.ultkind == UK_OWNED ? Reach(P.ultv) : true);
        }
        if (tk == UK_OWNED) {
            if (P.rootv == tu) return true;
            if (P.ultkind == UK_OWNED && P.ultv == tu) return true;
            return P.ultkind == UK_OPAQUE && Reach(tu) && !HandedApart(P, tu);
        }
        return Reach(P.rootv) || (P.ultkind == UK_OWNED ? Reach(P.ultv) : true);
    }

    
    
    
    static bool DistinctTypes(TypeExpr *a, TypeExpr *b) {
        if (!a || !b) return false;
        if (a->kind != b->kind) return true;
        switch (a->kind) {
            case TY_INT:
                if (a->intstorage != b->intstorage || !a->ix != !b->ix) return true;
                return a->ix && (a->ix->arr != b->ix->arr || a->ix->upto != b->ix->upto);
            case TY_FLT: return a->fltstorage != b->fltstorage;
            case TY_STRUCT: return a->struc->st != b->struc->st;
            case TY_ENUM: return a->enu->en != b->enu->en;
            case TY_ARRAY:
                return a->arr->akind != b->arr->akind || DistinctTypes(a->arr->sub, b->arr->sub);
            case TY_SLICE: return DistinctTypes(a->sub, b->sub);
            case TY_REF: return DistinctTypes(a->ref->sub, b->ref->sub);
            default: return false;
        }
    }

    
    
    
    int GrowShrinkKill(Node *recv, int dir, int64_t exact = INT64_MIN, bool alloc = false) {
        if (recv) NoteStorage(ExprTarget(recv));
        auto failidx = false, crossed = false;
        auto pid = recv ? PlaceOf(recv, &failidx, &crossed) : -1;
        auto rt = recv ? recv->exprtype : nullptr;
        if (rt && rt->kind == TY_REF) rt = rt->ref->sub;
        if (rt && rt->kind != TY_ARRAY) rt = nullptr;
        
        
        
        
        if (pid < 0 && failidx && !crossed) {
            ElementLvalKill(recv);
            return -1;
        }
        for (size_t i = 0; i < places.size(); i++) {
            if (!places[i].lenmut) continue;
            if ((int)i != pid && DistinctTypes(places[i].arr, rt)) continue;
            if (!AffectedByWrite((int)i, pid, UK_OPAQUE, nullptr, nullptr)) continue;
            if ((int)i == pid && exact != INT64_MIN) ExactLenStep(pid, exact);
            else BumpPlace((int)i, dir);
        }
        
        
        
        
        if (rt && HoldsLen(rt->arr->sub) &&
            (alloc || (dir >= 0 && rt->arr->akind == A_LIMITED))) {
            auto [k, u] = ChainUlt(recv);
            ElementWriteKill(k, u);
        }
        return pid;
    }

    void StorageWriteKill(int tk, VarDef *tu, VarDef *chainroot, TypeExpr *only = nullptr) {
        NoteStorage(UltTarget(tk, tu));
        for (size_t i = 0; i < places.size(); i++)
            if (!OtherArray((int)i, only) && AffectedByWrite((int)i, -1, tk, tu, chainroot))
                BumpPlace((int)i, 0);
    }

    
    
    bool OtherArray(int i, TypeExpr *only) {
        return only && places[i].arr && DistinctTypes(places[i].arr, only);
    }

    
    
    
    
    
    void SlotWriteKill(int tk, VarDef *tu) {
        if (tk == UK_STATIC) return;   
        NoteStorage(UltTarget(tk, tu));
        for (size_t i = 0; i < places.size(); i++)
            if (places[i].slice && AffectedByWrite((int)i, -1, tk, tu, nullptr))
                BumpPlace((int)i, 0);
    }

    void RebindKill(VarDef *root) {
        NoteStorage(OwnerTarget(root));
        for (size_t i = 0; i < places.size(); i++)
            if (places[i].rootv == root) BumpPlace((int)i, 0, true);
        
        erase_if(flow.lestated, [&](int id) {
            auto it = lelen.find(varof[id]);
            if (it == lelen.end()) return false;
            for (auto [pid, c] : it->second)
                if (places[pid].rootv == root) return true;
            return false;
        });
    }

    
    
    
    void RefSlotWriteKill(int tk, VarDef *tu, VarDef *chainroot) {
        if (tk == UK_STATIC) return;   
        NoteStorage(UltTarget(tk, tu));
        for (size_t i = 0; i < places.size(); i++)
            if (places[i].readsref && AffectedByWrite((int)i, -1, tk, tu, chainroot))
                BumpPlace((int)i, 0);
    }

    
    void KillByCall() {
        StorageWriteKill(UK_OPAQUE, nullptr, nullptr);
        OpaqueIntWriteKill();
    }

    
    
    
    void KillRendering() {
        NoteStorage({ TG_OPAQUE });
        NoteInt({ TG_OPAQUE });
        for (size_t i = 0; i < places.size(); i++) BumpPlace((int)i, 0);
        for (auto v : intvars) BumpVar(v);
    }

    
    

    enum TargetKind { TG_NONE, TG_OWN, TG_VAR, TG_CLASS, TG_OPAQUE };
    struct Target { TargetKind kind = TG_OPAQUE; VarDef *v = nullptr; };

    
    
    Target OwnerTarget(VarDef *v) {
        if (!v) return { TG_OPAQUE };
        if (v->isglobal || !ownvars.count(v)) return { TG_VAR, v };
        return { TG_OWN, v };
    }

    
    
    
    Target UltTarget(int k, VarDef *u) {
        if (k == UK_STATIC) return { TG_NONE };
        if (k == UK_OWNED) return OwnerTarget(u);
        return u ? Target { TG_CLASS, u } : Target { TG_OPAQUE };
    }

    
    
    
    
    
    
    pair<int, VarDef *> ChainUlt(Node *n, bool slot = false) {
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) {
                if (!id->vdef) return { UK_OPAQUE, nullptr };
                return UltOf(id->vdef, slot && cur == n);
            }
            Node *obj = nullptr;
            if (auto d = Is<Dot>(cur)) obj = d->obj;
            else if (auto ix = Is<Index>(cur)) obj = ix->obj;
            else return { UK_OPAQUE, nullptr };
            if ((cur != n || !slot) && ReadsStoredRef(cur)) return { UK_OPAQUE, nullptr };
            cur = obj;
        }
    }

    Target ExprTarget(Node *n, bool slot = false) {
        auto [k, u] = ChainUlt(n, slot);
        return UltTarget(k, u);
    }

    
    
    
    
    Target ArgTarget(Node *a) {
        if (auto u = Is<Unary>(a); u && u->op == T_BITAND) return ExprTarget(u->child);
        if (Is<Ident>(a)) return ExprTarget(a);
        if (auto se = Is<SliceExpr>(a)) return ExprTarget(se->obj);
        if (Is<ArrayLit>(a) || Is<StrLit>(a)) return { TG_NONE };
        return { TG_OPAQUE };
    }

    
    
    void NoteClass(set<int> &into, bool &opaque, VarDef *cls) {
        auto it = classparams.find(cls);
        if (it == classparams.end()) { opaque = true; return; }
        into.insert(it->second.begin(), it->second.end());
    }

    void NoteStorage(const Target &t) {
        if (!effsum) return;
        switch (t.kind) {
            case TG_VAR:    effsum->vars.insert(t.v); break;
            case TG_CLASS:  NoteClass(effsum->params, effsum->opaque, t.v); break;
            case TG_OPAQUE: effsum->opaque = true; break;
            default:        break;
        }
    }

    void NoteInt(const Target &t) {
        if (!effsum) return;
        switch (t.kind) {
            case TG_VAR:    effsum->intvars.insert(t.v); break;
            case TG_CLASS:  NoteClass(effsum->intparams, effsum->intopaque, t.v); break;
            case TG_OPAQUE: effsum->intopaque = true; break;
            default:        break;
        }
    }

    
    

    
    
    void KillPlaceWrite(int recvpid, TypeExpr *only = nullptr) {
        for (size_t i = 0; i < places.size(); i++)
            if (!OtherArray((int)i, only) &&
                AffectedByWrite((int)i, recvpid, UK_OPAQUE, nullptr, nullptr))
                BumpPlace((int)i, 0);
    }

    
    
    
    
    
    
    
    void KillClassWrite(VarDef *cls, TypeExpr *only = nullptr) {
        auto own = ownclasses.count(cls) != 0;
        for (size_t i = 0; i < places.size(); i++) {
            if (OtherArray((int)i, only)) continue;
            auto &P = places[i];
            auto hit = own ? P.ultkind == UK_OPAQUE ||
                                 (P.ultkind == UK_OWNED && OwnerTarget(P.ultv).kind == TG_VAR)
                           : AffectedByWrite((int)i, -1, UK_OPAQUE, nullptr, nullptr);
            if (hit) BumpPlace((int)i, 0);
        }
    }

    void KillReachableInts() {
        for (auto v : intvars) if (Reach(v)) BumpVar(v);
    }

    
    void OpaqueIntWriteKill() {
        NoteInt({ TG_OPAQUE });
        KillReachableInts();
    }

    
    
    
    
    void ElementWriteKill(int tk, VarDef *tu) {
        if (tk == UK_STATIC) return;   
        NoteStorage(UltTarget(tk, tu));
        for (size_t i = 0; i < places.size(); i++) {
            auto &P = places[i];
            if (P.refcrossed && P.inelem && AffectedByWrite((int)i, -1, tk, tu, nullptr))
                BumpPlace((int)i, 0);
        }
    }

    
    
    
    void ElementLvalKill(Node *lval) {
        auto [k, u] = ChainUlt(lval, true);
        if (k == UK_OPAQUE && !u) StorageWriteKill(UK_OPAQUE, nullptr, nullptr);
        else ElementWriteKill(k, u);
    }

    
    
    
    
    
    
    static bool HoldsLen(TypeExpr *t, bool refs = false, bool lens = true, int depth = 0) {
        if (!t || depth > 32) return true;
        auto any = [&](const vector<TypeExpr *> &fts) {
            for (auto ft : fts) if (ft && HoldsLen(ft, refs, lens, depth + 1)) return true;
            return false;
        };
        switch (t->kind) {
            case TY_SLICE:
                return lens;
            case TY_REF:
                return refs;
            case TY_ARRAY:
                return (lens && t->arr->akind != A_FIXED) ||
                       HoldsLen(t->arr->sub, refs, lens, depth + 1);
            case TY_STRUCT:
                return !t->struc->inst || any(t->struc->inst->ftypes);
            case TY_ENUM: case TY_VARIANT: {
                auto inst = t->kind == TY_ENUM ? t->enu->inst : t->var->adt->enu->inst;
                if (!inst) return true;
                for (auto &fts : inst->vftypes) if (any(fts)) return true;
                return false;
            }
            case TY_GENERIC:
                return true;
            default:
                return false;   
        }
    }

    static bool HoldsRef(TypeExpr *t) { return HoldsLen(t, true, false); }

    
    
    
    pair<int, VarDef *> ArgSlot(Node *a, Node *lv) {
        if (auto id = Is<Ident>(lv)) {
            if (id->vdef) return UltOf(id->vdef, true);
        } else if (lv != a && lv->exprtype && lv->exprtype->kind == TY_SLICE) {
            return ChainUlt(lv, true);
        }
        return { UK_OPAQUE, nullptr };
    }

    
    
    
    
    
    bool KillSliceArg(Node *a, Node *lv) {
        auto t = a->exprtype;
        auto st = IsSliceRef(t) ? t->ref->sub : t;
        if (!st || st->kind != TY_SLICE) return false;
        if (Is<ArrayLit>(lv) || Is<StrLit>(lv)) return true;   
        if (st != t) {
            auto [k, u] = ArgSlot(a, lv);
            SlotWriteKill(k, u);
        }
        auto lens = HoldsLen(st->sub), refs = HoldsRef(st->sub);
        if (lens || refs) {
            auto [k, u] = ChainUlt(lv);
            if (lens) ElementWriteKill(k, u);
            if (refs) RefSlotWriteKill(k, u, nullptr);
        }
        return true;
    }

    
    
    void KillArgStorage(Node *a) {
        auto lv = a;
        if (auto u = Is<Unary>(a); u && u->op == T_BITAND) lv = u->child;
        if (!Is<SliceExpr>(lv) && KillSliceArg(a, lv)) return;
        auto t = ArgTarget(a);
        NoteStorage(t);
        
        
        
        auto at = lv->exprtype;
        if (at && at->kind == TY_REF) at = at->ref->sub;
        auto sub = at && at->kind == TY_ARRAY ? at->arr->sub : nullptr;
        auto only = sub && !HoldsLen(sub, true) ? at : nullptr;
        switch (t.kind) {
            case TG_NONE:
                return;
            case TG_OWN: case TG_VAR: {
                
                
                auto pid = Is<SliceExpr>(lv) ? -1 : PlaceOf(lv);
                if (pid >= 0) KillPlaceWrite(pid, only);
                else StorageWriteKill(UK_OWNED, t.v, t.v, only);
                return;
            }
            case TG_CLASS:
                KillClassWrite(t.v, only);
                return;
            default:
                StorageWriteKill(UK_OPAQUE, nullptr, nullptr, only);
                return;
        }
    }

    
    
    
    
    void KillArgInt(Node *a) {
        auto lv = a;
        if (auto u = Is<Unary>(a); u && u->op == T_BITAND) lv = u->child;
        if (auto id = Is<Ident>(lv); id && id->vdef) {
            auto v = id->vdef;
            if (IntBaseVar(v)) {
                NoteInt(OwnerTarget(v));
                if (mode == M_RECORD) RecordSet(v, Term {}, false);   
                BumpVar(v);
                return;
            }
            if (!v->type || v->type->kind != TY_REF) return;
            auto [k, u] = UltOf(v);
            if (k == UK_STATIC) return;
            if (k == UK_OWNED) {
                if (IntBaseVar(u)) {
                    NoteInt(OwnerTarget(u));
                    if (mode == M_RECORD) RecordSet(u, Term {}, false);
                    BumpVar(u);
                }
                return;
            }
            NoteInt(UltTarget(k, u));
            KillReachableInts();
            return;
        }
        auto t = ExprTarget(lv);
        if (t.kind == TG_OPAQUE || t.kind == TG_CLASS) {
            NoteInt(t);
            KillReachableInts();
        }
    }

    
    
    
    
    void ApplyEffects(const Effects &e, const vector<Node *> &an) {
        if (e.opaque) { KillByCall(); return; }
        for (auto j : e.params) {
            if (j >= (int)an.size()) { KillByCall(); return; }
            KillArgStorage(an[j]);
        }
        for (auto v : e.vars) StorageWriteKill(UK_OWNED, v, v);
        if (e.intopaque) {
            OpaqueIntWriteKill();
            return;
        }
        for (auto j : e.intparams) {
            if (j >= (int)an.size()) {
                OpaqueIntWriteKill();
                return;
            }
            KillArgInt(an[j]);
        }
        for (auto v : e.intvars) {
            NoteInt(OwnerTarget(v));
            BumpVar(v);
        }
    }

    
    
    
    void CallKills(Call *c) {
        Effects e;
        auto known = c->spec || !c->dispatch.empty();
        auto merge = [&](FnSpec *sp) {
            auto it = effects.find(sp);
            if (it == effects.end()) known = false;
            else e.Merge(it->second);
        };
        if (c->spec) merge(c->spec);
        for (auto d : c->dispatch) merge(d);
        if (!known) { KillByCall(); return; }
        
        
        if (!c->fvremap.empty()) {
            auto passed = [&](set<VarDef *> &vs) {
                set<VarDef *> out;
                for (auto v : vs) out.insert(c->FreeVarArg(v));
                vs = std::move(out);
            };
            passed(e.vars);
            passed(e.intvars);
        }
        ApplyEffects(e, c->ArgNodes());
    }

    
    

    static bool LenTrackable(TypeExpr *t) {
        if (t && t->kind == TY_REF) t = t->ref->sub;
        return t && (t->kind == TY_SLICE || (t->kind == TY_ARRAY && t->arr->akind != A_FIXED));
    }

    const Iface &InterfaceOf(FnSpec *sp) {
        auto it = ifaces.find(sp);
        if (it != ifaces.end()) return it->second;
        auto &f = ifaces[sp];
        f.bases.push_back({ false, -1 });
        for (size_t j = 0; j < sp->params.size(); j++) {
            auto p = sp->params[j];
            if (IntBaseVar(p)) f.bases.push_back({ false, (int)j });
            else if (LenTrackable(p->type)) f.bases.push_back({ true, (int)j });
        }
        return f;
    }

    
    
    
    
    Term BoundLenOf(Node *init) {
        if (auto u = Is<Unary>(init); u && u->op == T_BITAND) init = u->child;
        
        
        if (auto c = Is<Call>(init); c && c->builtin == B_BYTES_OF && c->args.size() == 1) {
            auto t = c->args[0]->exprtype;
            if (t && t->kind == TY_REF) t = t->ref->sub;
            if (!t || t->kind != TY_ARRAY || t->arr->akind != A_FIXED || t->arr->size < 0)
                return {};
            auto e = t->arr->sub;
            auto w = !e ? 0 : e->kind == TY_INT ? IntBits(e->intstorage) / 8
                    : e->kind == TY_FLT ? (e->fltstorage == FS_F32 ? 4 : 8) : 0;
            if (!w || t->arr->size >= CCAP / w) return {};
            return Term { true, Zero(), t->arr->size * w };
        }
        if (!Is<Ident>(init) && !Is<Dot>(init)) return {};
        
        
        
        if (auto d = Is<Dot>(init); d && init->exprtype && init->exprtype->kind == TY_SLICE) {
            auto ft = FieldTypeOf(d);
            if (ft && ft->kind == TY_ARRAY && ft->arr->akind != A_FIXED) {
                auto pid = PlaceOf(init, nullptr, nullptr, ft);
                return pid >= 0 ? Term { true, LenBase(pid), 0 } : Term {};
            }
        }
        return LenTermOf(init);
    }

    
    
    
    
    
    Term ArgLenTerm(Node *a, const Term &slen) {
        if (auto u = Is<Unary>(a); u && u->op == T_BITAND) a = u->child;
        if (Is<SliceExpr>(a)) return slen;
        if (auto t = FreshLenOf(a); t.ok) return t;
        return LenTermOf(a);
    }

    
    
    
    
    
    void RecordSite(Call *c, const vector<Node *> &an, const vector<Term> &ints,
                    const vector<Term> &slens, bool moved) {
        auto record = [&](FnSpec *sp) {
            auto &S = sites[sp];
            S.seen++;
            if (!S.viable) return;
            auto &bases = InterfaceOf(sp).bases;
            auto n = (int)bases.size();
            if (n <= 1 || an.size() < sp->params.size()) { S.viable = false; return; }
            if (S.c.empty()) S.c.assign(n * n, INT64_MIN);
            vector<Term> terms(n);
            terms[0] = Term { true, Zero(), 0 };
            for (auto k = 1; k < n; k++) {
                auto [islen, j] = bases[k];
                terms[k] = islen ? (moved ? Term {} : ArgLenTerm(an[j], slens[j])) : ints[j];
            }
            auto anyfinite = false;
            for (auto x = 0; x < n; x++) {
                vector<Base> to;
                vector<int> ys;
                if (terms[x].ok)
                    for (auto y = 0; y < n; y++)
                        if (y != x && terms[y].ok) {
                            to.push_back(terms[y].b);
                            ys.push_back(y);
                        }
                auto d = to.empty() ? small_vector<int64_t, 4> {} : DistsFrom(terms[x].b, to);
                for (auto y = 0; y < n; y++) {
                    if (y == x) continue;
                    auto &cell = S.c[x * n + y];
                    auto cv = INF;
                    auto yit = std::find(ys.begin(), ys.end(), y);
                    
                    if (yit != ys.end() && d[yit - ys.begin()] != INF)
                        cv = SatAdd(d[yit - ys.begin()], terms[x].off - terms[y].off);
                    if (cv > cell) cell = cv;
                    if (cell != INF) anyfinite = true;
                }
            }
            if (!anyfinite) S.viable = false;
        };
        if (c->spec) record(c->spec);
        for (auto d : c->dispatch) record(d);
    }

    
    
    
    void SeedEntryFacts(FnSpec *sp) {
        if (opaquesites.count(sp)) return;
        auto sit = sites.find(sp);
        auto nit = nsites.find(sp);
        if (sit == sites.end() || nit == nsites.end()) return;
        auto &S = sit->second;
        if (!S.viable || S.seen == 0 || S.seen != nit->second) return;
        auto &bases = InterfaceOf(sp).bases;
        auto n = (int)bases.size();
        vector<Base> bs(n);
        vector<bool> ok(n, true);
        bs[0] = Zero();
        for (auto k = 1; k < n; k++) {
            auto [islen, j] = bases[k];
            auto p = sp->params[j];
            if (islen) {
                auto pid = PlaceOfVar(p);
                ok[k] = pid >= 0;
                if (pid >= 0) bs[k] = LenBase(pid);
            } else {
                bs[k] = VarBase(p);
            }
        }
        for (auto x = 0; x < n; x++)
            for (auto y = 0; y < n; y++) {
                auto cv = S.c[x * n + y];
                if (x != y && ok[x] && ok[y] && cv != INF && cv != INT64_MIN)
                    AddFactB(bs[x], bs[y], cv);
            }
    }

    
    

    void RecordShift(VarDef *v, int64_t c) {
        auto cp = CandOf(v);
        if (!cp) return;
        auto &st = *cp;
        auto [lo, hi] = IntRange(v->type->intstorage);
        if (c > 0) {
            auto nowrap = Query(VarBase(v), Zero(), SatSub(hi, c));   
            st.wrapfree = st.wrapfree && nowrap;
            st.ge0 = st.ge0 && nowrap;
            for (auto pit = st.le.begin(); pit != st.le.end();)
                if (Query(VarBase(v), LenBase(pit->first), SatSub(pit->second, c))) ++pit;
                else pit = st.le.erase(pit);
        } else if (c < 0) {
            auto nowrap = Query(Zero(), VarBase(v), SatSub(c, lo));   
            st.wrapfree = st.wrapfree && nowrap;
            st.ge0 = st.ge0 && Query(Zero(), VarBase(v), c);          
            if (!nowrap) st.le.clear();
        }
    }

    void RecordSet(VarDef *v, const Term &t, bool isdecl) {
        auto cp = CandOf(v);
        if (!cp) return;
        auto &st = *cp;
        
        auto exact = NoWrap(t, v->type->intstorage);
        
        
        
        
        
        if (isdecl && !st.declseen) {
            vector<pair<int, int64_t>> le;
            for (auto [pid, c] : st.le) {
                auto d = exact ? Dist(t.b, LenBase(pid)) : INF;
                if (d == INF) continue;
                auto need = SatAdd(d, t.off);
                if (!SmallOff(need)) continue;
                le.push_back({ pid, std::max<int64_t>(need, 0) });
                if (need < 0) le.push_back({ pid, -1 });
            }
            st.le = std::move(le);
        }
        if (isdecl) st.declseen = true;
        st.ge0 = st.ge0 && exact && Query(Zero(), t.b, t.off);
        for (auto pit = st.le.begin(); pit != st.le.end();)
            if (exact && Query(t.b, LenBase(pit->first), SatSub(pit->second, t.off))) ++pit;
            else pit = st.le.erase(pit);
    }

    
    
    void RecordSetDir(VarDef *v, const Term &t) {
        auto cp = CandOf(v);
        if (!cp) return;
        auto &st = *cp;
        auto exact = t.ok && NoWrap(t, v->type->intstorage);
        st.setdec = st.setdec && exact && Query(t.b, VarBase(v), SatSub(0, t.off));   
        st.setinc = st.setinc && exact && Query(VarBase(v), t.b, t.off);              
    }

    
    void Materialize(VarDef *v) {
        if (mode != M_JUDGE) return;
        auto vb = VarBase(v);
        if (Ge0(v)) AddFactB(Zero(), vb, 0);
        auto it = lelen.find(v);
        if (it != lelen.end())
            for (auto [pid, c] : it->second)
                if (LeLenHolds(vb, v, pid)) AddFactB(vb, LenBase(pid), c);
    }

    
    
    
    
    
    
    
    bool LeLenHolds(const Base &vb, VarDef *v, int pid) {
        if (!rebound.count(pid)) return true;
        auto &st = flow.lestated;
        return vb == VarBase(v) && std::binary_search(st.begin(), st.end(), VarId(v));
    }

    
    
    
    bool NoWrap(const Term &t, IntStorage s) {
        if (!t.ok || !t.off || t.b.kind == BK_ZERO) return t.ok;
        auto [lo, hi] = IntRange(s);
        hi = std::min(hi, CCAP - 1);
        lo = std::max(lo, 1 - CCAP);
        return t.off > 0 ? Query(t.b, Zero(), SatSub(hi, t.off))
                         : Query(Zero(), t.b, SatSub(t.off, lo));
    }

    void ShiftCore(VarDef *v, int64_t c) {
        if (!SmallOff(c)) { BumpVar(v, false); return; }
        auto [lo, hi] = IntRange(v->type->intstorage);
        auto ok = c > 0 ? Query(VarBase(v), Zero(), SatSub(hi, c))
                        : Query(Zero(), VarBase(v), SatSub(c, lo));
        if (!ok) { BumpVar(v); return; }
        Materialize(v);
        
        
        
        nextgen++;
        auto vb = VarBase(v);
        for (auto &f : flow.facts) {
            auto l = f.l == vb, r = f.r == vb;
            if (l == r) continue;
            f.c = SatAdd(f.c, l ? c : -c);
        }
        for (auto &l : flow.lins)
            if (l.x.b == vb) l.x.off = SatSub(l.x.off, c);
        erase_if(flow.lins, [](const Lin &l) { return !SmallOff(l.x.off); });
    }

    void ShiftWrite(VarDef *v, int64_t c) {
        NoteInt(OwnerTarget(v));
        if (mode == M_RECORD) RecordShift(v, c);
        if (mode == M_KILLS) { BumpVar(v); return; }
        if (c == 0) return;
        ShiftCore(v, c);
    }

    
    
    void SetWrite(VarDef *v, Node *rhs, bool isdecl) {
        NoteInt(OwnerTarget(v));
        auto t = mode == M_KILLS ? Term {} : TermOf(rhs);
        if (mode == M_RECORD) {
            if (t.ok && t.b == VarBase(v)) RecordShift(v, t.off);
            else RecordSet(v, t, isdecl);
            if (!isdecl) RecordSetDir(v, t);
        }
        if (mode == M_KILLS) { BumpVar(v, !isdecl); return; }
        if (t.ok && t.b == VarBase(v)) { ShiftCore(v, t.off); return; }
        BumpVar(v, !isdecl);
        if (NoWrap(t, v->type->intstorage)) {
            auto nb = VarBase(v);
            AddFactB(nb, t.b, t.off);
            AddFactB(t.b, nb, SatSub(0, t.off));
        }
        if (mode == M_JUDGE) StateLeLen(v);
    }

    
    
    
    
    void StateLeLen(VarDef *v) {
        auto it = lelen.find(v);
        if (it == lelen.end()) return;
        auto &st = flow.lestated;
        auto id = VarId(v);
        auto at = std::lower_bound(st.begin(), st.end(), id);
        if (at != st.end() && *at == id) st.erase(at);
        auto vb = VarBase(v);
        for (auto [pid, c] : it->second)
            if (rebound.count(pid) && !Query(vb, LenBase(pid), c)) return;
        st.insert(std::lower_bound(st.begin(), st.end(), id), id);
    }

    
    
    
    map<Assign *, Binary *> compoundsums;
    Binary *CompoundSum(Assign *a) {
        auto &bin = compoundsums[a];
        if (!bin) {
            bin = ast.New<Binary>(a->line, a->op == T_PLUSEQ ? T_PLUS : T_MINUS, a->lval, a->rhs);
            bin->exprtype = a->lval->exprtype;
        }
        return bin;
    }

    void VarKillWrite(VarDef *v) {
        if (IntBaseVar(v)) NoteInt(OwnerTarget(v));
        if (mode == M_RECORD) RecordSet(v, Term {}, false);
        BumpVar(v, false);
    }

    
    
    
    static bool IntBaseVar(VarDef *v) {
        return v && (MachineInt(v->type) || IsIndexT(v->type));
    }

    
    
    

    
    
    static bool InUnit(Node *n) {
        if (auto lit = Is<IntLit>(n)) return !lit->uns && (lit->val == 0 || lit->val == 1);
        if (auto bl = Is<Block>(n)) return bl->stmts.empty() && bl->tail && InUnit(bl->tail);
        if (auto ie = Is<IfExpr>(n)) return ie->elseb && InUnit(ie->thenb) && InUnit(ie->elseb);
        return false;
    }

    
    
    static bool WritesVar(Node *n, VarDef *v) {
        if (!n) return false;
        if (auto a = Is<Assign>(n)) {
            auto id = Is<Ident>(a->lval);
            if (id && id->vdef == v) return true;
        } else if (auto inc = Is<IncDec>(n)) {
            auto id = Is<Ident>(inc->lval);
            if (id && id->vdef == v) return true;
        } else if (auto vd = Is<VarDecl>(n)) {
            for (auto d : vd->defs) if (d == v) return true;
        }
        auto found = false;
        n->Children([&](Node *ch) { found = found || WritesVar(ch, v); });
        return found;
    }

    static bool IsUnitStep(Assign *a, VarDef *v) {
        return a->op == T_PLUSEQ && !a->pointee && InUnit(a->rhs) && !WritesVar(a->rhs, v);
    }

    
    
    
    static int64_t StepsOf(Node *n, VarDef *v) {
        if (!n) return 0;
        if (auto a = Is<Assign>(n)) {
            auto id = Is<Ident>(a->lval);
            if (id && id->vdef == v) return IsUnitStep(a, v) ? 1 : INF;
        } else if (auto inc = Is<IncDec>(n)) {
            auto id = Is<Ident>(inc->lval);
            if (id && id->vdef == v) return inc->op == T_INC ? 1 : INF;
        } else if (auto vd = Is<VarDecl>(n)) {
            for (auto d : vd->defs) if (d == v) return INF;
        } else if (Is<While>(n) || Is<LoopExpr>(n) || Is<ForLoop>(n) || Is<FunVal>(n)) {
            return WritesVar(n, v) ? INF : 0;
        } else if (auto ie = Is<IfExpr>(n)) {
            return SatAdd(StepsOf(ie->cond, v),
                          std::max(StepsOf(ie->thenb, v), StepsOf(ie->elseb, v)));
        } else if (auto me = Is<MatchExpr>(n)) {
            int64_t arms = 0;
            for (auto &arm : me->arms) arms = std::max(arms, StepsOf(arm.body, v));
            return SatAdd(StepsOf(me->scrutinee, v), arms);
        }
        int64_t s = 0;
        n->Children([&](Node *ch) { s = SatAdd(s, StepsOf(ch, v)); });
        return s;
    }

    
    
    
    void StepWrite(VarDef *v) {
        NoteInt(OwnerTarget(v));
        if (mode == M_RECORD) RecordShift(v, 1);
        if (mode == M_KILLS) { BumpVar(v); return; }
        auto hi = IntRange(v->type->intstorage).second;
        auto nowrap = Query(VarBase(v), Zero(), SatSub(hi, 1));
        auto old = VarBase(v);
        BumpVar(v, false);
        if (!nowrap) return;
        auto nw = VarBase(v);
        AddFactB(old, nw, 0);
        AddFactB(nw, old, 1);
    }

    
    
    vector<VarDef *> StepCounters(Node *body) {
        vector<VarDef *> written;
        function<void(Node *)> scan = [&](Node *n) {
            if (!n) return;
            Ident *id = nullptr;
            if (auto a = Is<Assign>(n)) id = Is<Ident>(a->lval);
            else if (auto inc = Is<IncDec>(n)) id = Is<Ident>(inc->lval);
            if (id && id->vdef && std::find(written.begin(), written.end(), id->vdef) == written.end())
                written.push_back(id->vdef);
            n->Children(scan);
        };
        scan(body);
        vector<VarDef *> out;
        for (auto v : written)
            if (IntBaseVar(v) && !v->captured && !v->isglobal && !addrof.count(v) &&
                StepsOf(body, v) <= 1)
                out.push_back(v);
        return out;
    }

    
    

    enum ChainKind { CH_OK, CH_INDEX, CH_FAIL };
    struct Chain {
        ChainKind kind = CH_FAIL;
        VarDef *root = nullptr;
        bool anycross = false, rootonlycross = false;
    };

    
    
    
    
    
    Chain ChainOf(Node *n) {
        Chain ch;
        auto midcross = false;
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) {
                ch.root = id->vdef;
                if (!ch.root) return ch;
                auto rootref = ch.root->type && ch.root->type->kind == TY_REF;
                ch.anycross = midcross || rootref;
                ch.rootonlycross = rootref && !midcross;
                ch.kind = CH_OK;
                return ch;
            }
            if (auto d = Is<Dot>(cur); d && d->fieldidx >= 0) {
                if (cur != n && ReadsStoredRef(d)) midcross = true;
                cur = d->obj;
                continue;
            }
            if (Is<Index>(cur)) { ch.kind = CH_INDEX; return ch; }
            return ch;
        }
    }

    void PointeeWriteKill(Node *lval, TypeExpr *pt) {
        
        
        auto slice = pt && pt->kind == TY_SLICE;
        int tk = UK_OPAQUE;
        VarDef *tu = nullptr;
        if (auto id = Is<Ident>(lval); id && id->vdef) {
            auto [k, u] = UltOf(id->vdef, slice);
            tk = k;
            tu = u;
        }
        if (tk == UK_STATIC) return;   
        if (slice) {
            SlotWriteKill(tk, tu);
            return;
        }
        
        
        if (HoldsLen(pt, true)) StorageWriteKill(tk, tu, nullptr);
        if (pt && pt->kind == TY_INT) {
            if (tk == UK_OWNED) {
                if (tu->type && tu->type->kind == TY_INT) {
                    NoteInt(OwnerTarget(tu));
                    
                    
                    
                    if (mode == M_RECORD) RecordSet(tu, Term {}, false);
                    BumpVar(tu, false);
                }
            } else {
                NoteInt(UltTarget(tk, tu));
                for (auto v : intvars) if (Reach(v)) BumpVar(v, false);
            }
        }
    }

    
    

    void RecordLine(Line l, bool elided) {
        auto &e = lineout[{ l.fileidx, l.line }];
        if (elided) e.first++;
        else e.second++;
    }

    void JudgeIndex(Index *ix, const Term &lent) {
        if (mode == M_KILLS) return;
        auto it = TermOf(ix->idx);
        auto proven = [&] {
            return lent.ok && it.ok && Query(Zero(), it.b, it.off) &&
                   Query(it.b, lent.b, SatSub(SatSub(lent.off, it.off), 1));
        };
        
        if (probing) {
            if (!proven() && !IndexInRangeByType(ix)) probefail = true;
            return;
        }
        if (mode == M_JUDGE) {
            idxtotal++;
            auto ok = proven();
            if (ok) { ix->nobc = true; idxelided++; }
            RecordLine(ix->line, ok);
            if (!ok) KeptInCheckfree(ix->line, KeptWhy(ix->idx, ix->obj, it, lent, -1));
        }
        CheckedFacts(it, lent, -1);
    }

    
    
    void JudgeIndexCast(AsCast *ac) {
        if (mode == M_KILLS) return;
        auto t = ac->totype;
        auto pit = ixpid.find(t->ix->arr);
        auto lent = pit != ixpid.end() && pit->second >= 0
                        ? Term { true, LenBase(pit->second), 0 } : Term {};
        auto slack = t->ix->upto ? 0 : -1;
        auto xt = TermOf(ac->child);
        auto proven = [&] {
            return lent.ok && xt.ok && Query(Zero(), xt.b, xt.off) &&
                   Query(xt.b, lent.b, SatAdd(SatSub(lent.off, xt.off), slack));
        };
        if (probing) {
            if (!proven()) probefail = true;
            return;
        }
        if (mode == M_JUDGE) {
            idxtotal++;
            auto ok = proven();
            if (ok) { ac->nobc = true; idxelided++; }
            RecordLine(ac->line, ok);
            if (!ok) KeptInCheckfree(ac->line, KeptWhy(ac->child, nullptr, xt, lent, slack, t));
        }
        CheckedFacts(xt, lent, slack);
    }

    
    
    

    string ExprText(Node *n) {
        string s;
        dumpwritten = true;
        n->Dump(s, 0);
        dumpwritten = false;
        return s;
    }

    void KeptInCheckfree(Line l, const string &why) {
        
        const Span *in = nullptr;
        for (auto &sp : checkfreespans)
            if (sp.file == l.fileidx && l.line >= sp.lo && l.line <= sp.hi &&
                (!in || sp.hi - sp.lo < in->hi - in->lo))
                in = &sp;
        if (!in) return;
        auto msg = cat("checkfree fn ", in->sf->qname, " keeps a bounds check: ", why);
        for (auto &f : checkfreefails)
            if (f.line.fileidx == l.fileidx && f.line.line == l.line && f.why == msg) return;
        checkfreefails.push_back({ l, msg });
    }

    
    
    
    string KeptWhy(Node *x, Node *obj, const Term &xt, const Term &lent, int64_t slack,
                   TypeExpr *into = nullptr) {
        auto xs = ExprText(x);
        auto against = obj ? cat(ExprText(obj), ".len") : cat(into->ix->arr->name, ".len");
        if (!lent.ok)
            return cat("the length of ", obj ? ExprText(obj) : string(into->ix->arr->name),
                       " is not one the analysis tracks here");
        auto t = x->exprtype;
        
        if ((Is<Index>(x) || Is<Dot>(x) || Is<Call>(x)) && !IsIndexT(t))
            return cat(xs, " is read out of an element, a field or a call result, whose "
                       "value the analysis does not know; an index type (`u32 in a`) for "
                       "what is stored there carries it (§3.11)");
        if (!xt.ok) {
            if (Is<Ident>(x) && t && t->kind == TY_INT && t->intstorage == IS_U64)
                return cat(xs, " is a u64 variable, which the analysis does not track; an i64 "
                           "or an index type is");
            return cat("the analysis cannot relate ", xs, " to ", against);
        }
        if (!Query(Zero(), xt.b, xt.off)) return cat("nothing shows ", xs, " >= 0");
        auto rel = slack < 0 ? " < " : " <= ";
        if (IsIndexT(t) && obj)
            return cat("nothing shows ", xs, rel, against, "; ", xs, " is a ", TypeName(t),
                       ", so `assert(", ExprText(obj), ".len >= ", t->ix->arr->name,
                       ".len)` where neither changes after it would settle it");
        return cat("nothing shows ", xs, rel, against);
    }

    
    
    
    CompileError CheckfreeError() {
        CompileError e;
        for (auto &f : checkfreefails) {
            Diagnostic d;
            d.where = LocOf(f.line);
            d.message = f.why;
            d.text = ast.DiagnosticText(d);
            if (!e.msg.empty()) e.msg += "\n";
            e.msg += d.text;
            e.diags.push_back(std::move(d));
        }
        return e;
    }

    static string TypeName(TypeExpr *t) {
        string s;
        t->Dump(s);
        return s;
    }

    
    
    
    
    
    void CheckedFacts(const Term &x, const Term &lent, int64_t slack) {
        if (!x.ok) return;
        AddFactB(Zero(), x.b, x.off);
        if (lent.ok) AddFactB(x.b, lent.b, SatAdd(SatSub(lent.off, x.off), slack));
        QuotBack(x);
    }

    
    
    
    Term SliceBound(Node *bn, bool fromend, const Term &lent, Term dflt) {
        if (!bn) return dflt;
        auto t = TermOf(bn);
        if (!t.ok) return {};
        if (!fromend) return t;
        if (t.b.kind != BK_ZERO || !lent.ok) return {};
        return Term { true, lent.b, SatSub(lent.off, t.off) };
    }

    
    
    
    
    
    
    
    static Term SliceLenTerm(const Term &lot, const Term &hit) {
        if (!lot.ok || !hit.ok) return {};
        auto d = SatSub(hit.off, lot.off);
        if (!SmallOff(d)) return {};
        
        
        if (lot.b == hit.b) return d < 0 ? Term {} : Term { true, Zero(), d };
        if (lot.b.kind == BK_ZERO) return Term { true, hit.b, d };
        return {};
    }

    
    
    Term slicelen;

    void JudgeSlice(SliceExpr *se, const Term &lent, const Term &lot, const Term &hit) {
        if (probing) {
            probefail = true;
            return;
        }
        sltotal++;
        auto ok = lent.ok && lot.ok && hit.ok &&
                  Query(Zero(), lot.b, lot.off) &&
                  Query(lot.b, hit.b, SatSub(hit.off, lot.off)) &&
                  Query(hit.b, lent.b, SatSub(lent.off, hit.off));
        if (ok) { se->nobc = true; slelided++; }
        RecordLine(se->line, ok);
        if (!ok) {
            string why;
            if (!lent.ok)
                why = cat("the length of ", ExprText(se->obj),
                          " is not one the analysis tracks here");
            else if (!lot.ok || !hit.ok)
                why = "the analysis cannot relate the slice's bounds to the length";
            else if (!Query(Zero(), lot.b, lot.off))
                why = "nothing shows the lower bound >= 0";
            else if (!Query(lot.b, hit.b, SatSub(hit.off, lot.off)))
                why = "nothing shows the lower bound <= the upper bound";
            else
                why = cat("nothing shows the upper bound <= ", ExprText(se->obj), ".len");
            KeptInCheckfree(se->line, cat("slice ", ExprText(se), ": ", why));
        }
    }

    
    
    
    

    void CondFacts(Node *c, bool truth) {
        if (mode == M_KILLS) return;
        if (auto u = Is<Unary>(c); u && u->op == T_NOT) { CondFacts(u->child, !truth); return; }
        auto b = Is<Binary>(c);
        if (!b) return;
        if ((b->op == T_ANDAND && truth) || (b->op == T_OROR && !truth)) {
            CondFacts(b->left, truth);
            CondFacts(b->right, truth);
            return;
        }
        auto op = b->op;
        if (op != T_LT && op != T_GT && op != T_LTEQ && op != T_GTEQ && op != T_EQ &&
            op != T_NEQ)
            return;
        if (!MachineInt(b->left->exprtype) || !MachineInt(b->right->exprtype)) return;
        auto lt = TermOf(b->left), rt = TermOf(b->right);
        if (!lt.ok || !rt.ok || !CmpAdmissible(lt) || !CmpAdmissible(rt)) return;
        if (!truth) {
            switch (op) {
                case T_LT:   op = T_GTEQ; break;
                case T_LTEQ: op = T_GT; break;
                case T_GT:   op = T_LTEQ; break;
                case T_GTEQ: op = T_LT; break;
                case T_EQ:   op = T_NEQ; break;
                default:     op = T_EQ; break;
            }
        }
        auto le = [&](const Term &x, const Term &y, int64_t d) {   
            AddFactB(x.b, y.b, SatAdd(SatSub(y.off, x.off), d));
        };
        switch (op) {
            case T_LT:   le(lt, rt, -1); break;
            case T_LTEQ: le(lt, rt, 0); break;
            case T_GT:   le(rt, lt, -1); break;
            case T_GTEQ: le(rt, lt, 0); break;
            case T_EQ:   le(lt, rt, 0); le(rt, lt, 0); break;
            default:
                
                
                if (Query(rt.b, lt.b, SatSub(lt.off, rt.off))) le(rt, lt, -1);
                else if (Query(lt.b, rt.b, SatSub(rt.off, lt.off))) le(lt, rt, -1);
                break;
        }
    }

    
    

    bool probing = false;       
    bool probefail = false;     

    
    
    
    static bool ProbeShape(Node *n, bool &index) {
        if (Is<Index>(n)) index = true;
        else if (!Is<Binary>(n) && !Is<Unary>(n) && !Is<Ident>(n) && !Is<Dot>(n) &&
                 !Is<AsCast>(n) && !Is<IntLit>(n) && !Is<FltLit>(n) && !Is<BoolLit>(n) &&
                 !Is<NullLit>(n))
            return false;
        auto ok = true;
        n->Children([&](Node *ch) { ok = ok && ProbeShape(ch, index); });
        return ok;
    }

    
    
    
    
    
    void ProbeRight(Binary *bn) {
        auto index = false;
        if (!ProbeShape(bn->right, index) || !index) return;
        auto savedflow = flow;
        auto savedderived = derived;
        auto savedslicelen = slicelen;
        probing = true;
        probefail = false;
        Walk(bn->right);
        probing = false;
        flow = std::move(savedflow);
        derived = std::move(savedderived);
        slicelen = savedslicelen;
        bn->specidx = probefail || bn->specidx < 0 ? -1 : 1;
    }

    
    

    void NoteVar(VarDef *v) {
        if (IntBaseVar(v)) intvars.insert(v);
    }

    void MarkAddr(Node *n) {
        for (auto cur = n;;) {
            if (auto id = Is<Ident>(cur)) {
                auto v = id->vdef;
                if (!v) return;
                addrof.insert(v);
                auto [k, u] = UltOf(v);
                if (k == UK_OWNED && u && u != v) addrof.insert(u);
                return;
            }
            if (auto d = Is<Dot>(cur)) { cur = d->obj; continue; }
            if (auto ix = Is<Index>(cur)) { cur = ix->obj; continue; }
            if (auto u2 = Is<Unary>(cur)) { cur = u2->child; continue; }
            return;
        }
    }

    
    void NotePlace(Node *n) { PlaceOf(n); }

    
    
    static VarDef *PivotVar(Node *n) {
        if (auto id = Is<Ident>(n)) return id->vdef;
        if (auto b = Is<Binary>(n); b && (b->op == T_PLUS || b->op == T_MINUS)) {
            if (auto v = PivotVar(b->left)) return v;
            return PivotVar(b->right);
        }
        return nullptr;
    }

    void NoteRel(Node *obj, Node *idx) {
        if (!idx) return;
        auto v = PivotVar(idx);
        if (!v) return;
        auto pid = PlaceOf(obj);
        if (pid >= 0) relpids[v].insert(pid);
    }

    
    
    

    
    
    
    bool Walk(Node *n) { return n ? n->BceWalk(*this) : true; }

    void Mark(Node *n) { if (n) n->BceMark(*this); }

    
    
    
    void WalkLvalParts(Node *n) {
        if (Is<Ident>(n)) return;
        if (auto d = Is<Dot>(n)) { Walk(d->obj); return; }
        if (auto ix = Is<Index>(n)) { Walk(ix); return; }
        Walk(n);
    }

    
    
    
    
    
    bool HasJumps(Node *n, bool breaks, bool continues) {
        if (!n) return false;
        if ((breaks && Is<Break>(n)) || (continues && Is<Continue>(n))) return true;
        if (Is<While>(n) || Is<LoopExpr>(n) || Is<ForLoop>(n)) return false;
        if (Is<EarlyBlock>(n)) breaks = false;
        if (!breaks && !continues) return false;
        auto found = false;
        RunChildren(n, [&](Node *ch) { found = found || HasJumps(ch, breaks, continues); });
        return found;
    }

    bool HasBreaks(Node *n) { return HasJumps(n, true, false); }

    
    
    
    bool HasIterationJumps(Node *n) { return HasJumps(n, true, true); }

    
    void SummarizeInto(Node *n, set<int> &out) {
        KillsScope ks(*this, &out);
        Walk(n);
    }

    
    
    void RefIndexed(Node *n, vector<VarDef *> &out) {
        if (!n) return;
        if (auto ix = Is<Index>(n))
            if (auto id = Is<Ident>(ix->obj); id && id->vdef && id->vdef->type) {
                auto t = id->vdef->type;
                auto s = t->kind == TY_REF ? t->ref->sub : nullptr;
                if (s && s->kind == TY_ARRAY && s->arr->akind != A_FIXED &&
                    std::find(out.begin(), out.end(), id->vdef) == out.end())
                    out.push_back(id->vdef);
            }
        RunChildren(n, [&](Node *ch) { RefIndexed(ch, out); });
    }

    
    
    
    static void BoundRefs(Node *n, unordered_map<VarDef *, VarDef *> &out) {
        if (!n) return;
        if (auto vd = Is<VarDecl>(n); vd && vd->defs.size() == 1 && vd->inits.size() == 1) {
            auto d = vd->defs[0];
            auto init = vd->inits[0];
            if (auto u = Is<Unary>(init); u && u->op == T_BITAND) init = u->child;
            auto id = Is<Ident>(init);
            auto pointee = [](TypeExpr *t) { return t && t->kind == TY_REF ? t->ref->sub : t; };
            auto a = d && d->type && d->type->kind == TY_REF ? d->type->ref->sub : nullptr;
            auto b = id && id->vdef ? pointee(id->vdef->type) : nullptr;
            if (a && b && !d->isvar && !d->type->ref->optional && id->vdef != d &&
                a->kind == TY_STRUCT && b->kind == TY_STRUCT && a->struc->st == b->struc->st &&
                a->struc->inst == b->struc->inst)
                out[d] = id->vdef;
        }
        RunChildren(n, [&](Node *ch) { BoundRefs(ch, out); });
    }

    
    
    
    
    
    
    void FieldIndexed(Node *n, const unordered_map<VarDef *, VarDef *> &bound,
                      vector<FieldPath> &out) {
        if (!n) return;
        auto ix = Is<Index>(n);
        auto d = ix ? Is<Dot>(ix->obj) : nullptr;
        auto ft = d && d->fieldidx >= 0 ? FieldTypeOf(d) : nullptr;
        if (ft && ft->kind == TY_ARRAY &&
            (ft->arr->akind == A_GROW || ft->arr->akind == A_GROWSHRINK)) {
            vector<int> path;
            Node *cur = d;
            auto ok = true;
            while (auto dd = Is<Dot>(cur)) {
                auto ot = dd->obj->exprtype;
                if (dd->fieldidx < 0 || ReadsStoredRef(dd) || !ot) { ok = false; break; }
                path.push_back(dd->fieldidx);
                cur = dd->obj;
                
                if (ot->kind == TY_REF ? !Is<Ident>(cur) : ot->kind != TY_STRUCT) ok = false;
                if (!ok || ot->kind == TY_REF) break;
            }
            auto id = Is<Ident>(cur);
            if (ok && id && id->vdef) {
                auto root = id->vdef;
                for (auto guard = 0; guard < 16; guard++) {
                    auto it = bound.find(root);
                    if (it == bound.end()) break;
                    root = it->second;
                }
                std::reverse(path.begin(), path.end());
                FieldPath fp { root, path };
                if (std::find(out.begin(), out.end(), fp) == out.end()) out.push_back(fp);
            }
        }
        RunChildren(n, [&](Node *ch) { FieldIndexed(ch, bound, out); });
    }

    
    
    
    
    
    
    
    
    void LoopViewRefs(Node *body, Node *cond, vector<VarDef *> &out, vector<FieldPath> &fout) {
        out.clear();
        fout.clear();
        if (mode != M_JUDGE) return;
        vector<VarDef *> vars;
        RefIndexed(body, vars);
        RefIndexed(cond, vars);
        unordered_map<VarDef *, VarDef *> bound;
        BoundRefs(body, bound);
        BoundRefs(cond, bound);
        vector<FieldPath> fields;
        FieldIndexed(body, bound, fields);
        FieldIndexed(cond, bound, fields);
        
        
        vector<pair<VarDef *, int>> named;
        for (auto v : vars) {
            auto pid = PlaceOfVar(v);
            if (pid >= 0) named.push_back({ v, pid });
        }
        vector<pair<FieldPath *, int>> fnamed;
        for (auto &fp : fields) {
            auto rt = fp.first->type;
            if (!rt) continue;
            vector<int> path;
            if (rt->kind == TY_REF) path.push_back(-1);
            path.insert(path.end(), fp.second.begin(), fp.second.end());
            
            auto st = rt->kind == TY_REF ? rt->ref->sub : rt;
            TypeExpr *ft = nullptr;
            for (auto f : fp.second) {
                if (!st || st->kind != TY_STRUCT) { ft = nullptr; break; }
                auto inst = st->struc->inst;
                ft = inst ? inst->ftypes[f] : st->struc->st->fields[f].type;
                st = ft;
            }
            auto pid = ft ? PlaceFor(fp.first, path, ft) : -1;
            if (pid >= 0) fnamed.push_back({ &fp, pid });
        }
        if (named.empty() && fnamed.empty()) return;
        set<int> kills;
        SummarizeInto(body, kills);
        SummarizeInto(cond, kills);
        for (auto &[v, pid] : named) if (!kills.count(pid)) out.push_back(v);
        for (auto &[fp, pid] : fnamed) if (!kills.count(pid)) fout.push_back(*fp);
    }

    
    static void DeclaredIn(Node *n, set<VarDef *> &out) {
        if (!n) return;
        if (auto vd = Is<VarDecl>(n)) for (auto d : vd->defs) if (d) out.insert(d);
        if (auto fl = Is<ForLoop>(n)) {
            if (fl->vdef) out.insert(fl->vdef);
            if (fl->idxdef) out.insert(fl->idxdef);
        }
        if (auto m = Is<MatchExpr>(n))
            for (auto &arm : m->arms) if (arm.binder) out.insert(arm.binder);
        if (auto c = Is<Call>(n)) for (auto p : c->fvparams) if (p) out.insert(p);
        RunChildren(n, [&](Node *ch) { DeclaredIn(ch, out); });
    }

    
    
    
    
    
    bool HasKillEffects(Node *n) {
        set<VarDef *> fresh;
        DeclaredIn(n, fresh);
        KillsScope ks(*this);
        auto savedfresh = freshvars;
        freshvars = &fresh;
        Walk(n);
        freshvars = savedfresh;
        return anybump;
    }

    
    void StripKills(Node *n) {
        auto saved = mode;
        mode = M_KILLS;
        Walk(n);
        mode = saved;
    }

    
    
    struct Phi { Base p; Term ta, tb; };

    
    
    
    
    
    
    
    
    Flow Meet(const Flow &a, const Flow &b, span<const Phi> vals = {}) {
        Flow r;
        for (auto &f : a.facts)
            for (auto &g : b.facts)
                if (f.l == g.l && f.r == g.r) {
                    r.facts.push_back({ f.l, f.r, std::max(f.c, g.c) });
                    break;
                }
        for (auto &l : a.lins)
            for (auto &k : b.lins)
                if (l.tmp == k.tmp && l.x.b == k.x.b && l.x.off == k.x.off) {
                    r.lins.push_back(l);
                    r.lins.back().exact = l.exact && k.exact;
                    break;
                }
        r.vgen = Gens::Max(a.vgen, b.vgen);
        r.pgen = Gens::Max(a.pgen, b.pgen);
        std::set_intersection(a.lestated.begin(), a.lestated.end(), b.lestated.begin(),
                              b.lestated.end(), std::back_inserter(r.lestated));
        if (mode == M_KILLS) return r;
        auto named = [](const Flow &f, const Base &x) {
            if (x.kind == BK_ZERO) return true;
            for (auto &g : f.facts) if (g.l == x || g.r == x) return true;
            return false;
        };
        small_vector<Phi, 4> phis;
        for (auto &v : vals)
            if (named(a, v.ta.b) && named(b, v.tb.b)) phis.push_back(v);
        for (auto &g : r.vgen.gens) {
            auto ga = Base { BK_VAR, g.id, a.vgen.Get(g.id) };
            auto gb = Base { BK_VAR, g.id, b.vgen.Get(g.id) };
            if (ga.gen != gb.gen && IntBaseVar(varof[g.id]) && named(a, ga) && named(b, gb))
                phis.push_back({ Base { BK_VAR, g.id, g.gen }, { true, ga, 0 }, { true, gb, 0 } });
        }
        if (phis.empty()) return r;
        
        
        
        
        auto current = [&](const Base &x) {
            switch (x.kind) {
                case BK_ZERO: return true;
                case BK_LEN: return x.gen == r.pgen.Get(x.id);
                case BK_VAR: return x.gen == r.vgen.Get(x.id);
                default: return false;
            }
        };
        small_vector<Base, 32> to = { Zero() };
        auto add = [&](const Base &x) {
            for (auto &y : to) if (y == x) return;
            to.push_back(x);
        };
        for (auto &f : a.facts) { if (current(f.l)) add(f.l); if (current(f.r)) add(f.r); }
        for (auto &f : b.facts) { if (current(f.l)) add(f.l); if (current(f.r)) add(f.r); }
        for (auto &ph : phis) add(ph.p);
        
        
        
        auto implied = [&](const Flow &f, bool first) {
            auto scratch = f;
            std::swap(flow, scratch);
            for (auto &ph : phis) {
                auto &t = first ? ph.ta : ph.tb;
                if (t.b == ph.p && !t.off) continue;
                flow.facts.push_back({ ph.p, t.b, t.off });
                flow.facts.push_back({ t.b, ph.p, SatSub(0, t.off) });
            }
            vector<int64_t> out;
            out.reserve(phis.size() * to.size() * 2);
            for (auto &ph : phis) {
                auto up = DistsFrom(ph.p, to), dn = DistsFrom(ph.p, to, true);
                for (size_t i = 0; i < to.size(); i++) {
                    out.push_back(up[i]);
                    out.push_back(dn[i]);
                }
            }
            std::swap(flow, scratch);
            return out;
        };
        auto fa = implied(a, true), fb = implied(b, false);
        
        
        
        
        vector<Fact> extra;
        for (size_t k = 0, j = 0; k < phis.size(); k++)
            for (size_t i = 0; i < to.size(); i++, j += 2)
                for (size_t dir = 0; dir < 2; dir++) {
                    if (to[i] == phis[k].p) continue;
                    if (!SmallOff(fa[j + dir]) || !SmallOff(fb[j + dir])) continue;
                    auto c = std::max(fa[j + dir], fb[j + dir]);
                    auto l = dir ? to[i] : phis[k].p, rb = dir ? phis[k].p : to[i];
                    auto known = false;
                    for (auto &h : r.facts)
                        if (h.l == l && h.r == rb) {
                            h.c = std::min(h.c, c);
                            known = true;
                            break;
                        }
                    if (!known) extra.push_back({ l, rb, c });
                }
        auto room = MAXFACTS > r.facts.size() ? MAXFACTS - r.facts.size() : 0;
        if (extra.size() > room) extra.resize(room);
        r.facts.insert(r.facts.begin(), extra.begin(), extra.end());
        return r;
    }

    
    

    
    void ResetSpecState() {
        
        
        
        flow = Flow {};
        places.clear();
        placeids.clear();
        addrof.clear();
        intvars.clear();
        wdecl.clear();
        wbad.clear();
        wkinds.clear();
        relpids.clear();
        derived.clear();
        effectfulterms.clear();
        cands.clear();
        ge0.clear();
        lelen.clear();
        rebound.clear();
        mono.clear();
        ownvars.clear();
        classparams.clear();
        ownclasses.clear();
    }

    void NoteOwn(VarDef *v) { if (v) ownvars.insert(v); }

    
    
    void SetupSpec(FnSpec *sp) {
        ResetSpecState();
        for (size_t j = 0; j < sp->params.size(); j++) {
            auto p = sp->params[j];
            ownvars.insert(p);
            if (p->type && IsRefOrSlice(p->type) && p->ref.Root())
                classparams[p->ref.Root()].push_back((int)j);
        }
        for (auto cr : sp->classroots) if (cr) ownclasses.insert(cr);
        IndexPlaces();
        Mark(sp->body);
    }

    void RunSpec(FnSpec *sp) {
        if (!sp->body) return;   
        SetupSpec(sp);
        for (auto &[v, ok] : gge0) if (ok) ge0.insert(v);
        
        
        set<int> bumped, shrunk;
        set<VarDef *> vbumped;
        {
            KillsScope ks(*this, &bumped, &shrunk, &vbumped, &rebound);
            flow = Flow {};
            Walk(sp->body);
        }
        
        
        auto eligible = [&](VarDef *v) {
            return v && wdecl.count(v) && !wbad.count(v) && !v->captured && !v->isglobal &&
                   !v->isparam && !addrof.count(v) && IntBaseVar(v);
        };
        for (auto v : wdecl) {
            if (!eligible(v)) continue;
            auto &c = cands[v];
            auto rit = relpids.find(v);
            if (rit != relpids.end())
                for (auto pid : rit->second) {
                    
                    
                    
                    if (shrunk.count(pid) || vbumped.count(places[pid].rootv)) continue;
                    if (c.le.size() >= 8) break;
                    c.le.push_back({ pid, 0 });
                }
        }
        if (!cands.empty()) {
            mode = M_RECORD;
            flow = Flow {};
            SeedEntryFacts(sp);
            derived.clear();
            Walk(sp->body);
            for (auto &[v, c] : cands) {
                if (!c.declseen) continue;
                if (c.ge0) ge0.insert(v);
                if (!c.le.empty()) lelen[v] = c.le;
                
                
                auto &wk = wkinds[v];
                auto dec = !wk.inc && (!wk.setw || c.setdec);
                auto inc = !wk.dec && (!wk.setw || c.setinc);
                if (c.wrapfree && dec != inc) mono[v] = inc ? 1 : -1;
            }
        }
        mode = M_JUDGE;
        flow = Flow {};
        SeedEntryFacts(sp);
        derived.clear();
        Walk(sp->body);
    }

    
    

    
    
    
    void ScanCalls(Node *n, FnSpec *from, map<FnSpec *, int> &uses) {
        if (!n) return;
        if (auto c = Is<Call>(n)) {
            if (c->spec) {
                if (c->builtin < 0) {
                    nsites[c->spec]++;
                    uses[c->spec]++;
                    if (from) callees[from].push_back(c->spec);
                } else {
                    opaquesites.insert(c->spec);   
                }
            }
            for (auto &fs : c->fmtspecs) {
                opaquesites.insert(fs.second);
                uses[fs.second]++;
            }
            for (auto d : c->dispatch) {
                nsites[d]++;
                uses[d]++;
                if (from) callees[from].push_back(d);
            }
        }
        RunChildren(n, [&](Node *ch) { ScanCalls(ch, from, uses); });
    }

    void BuildCallGraph() {
        map<FnSpec *, int> uses;
        for (auto sp : ast.fnspecs)
            if (sp->live && sp->body) ScanCalls(sp->body, sp, uses);
        ast.ForEachRootTree([&](Node *n) { ScanCalls(n, nullptr, uses); });
        
        
        
        for (auto sp : ast.fnspecs)
            if (sp->live && (uses[sp] != sp->uses || sp->sf->isexport)) opaquesites.insert(sp);
        set<FnSpec *> visited;
        function<void(FnSpec *)> visit = [&](FnSpec *sp) {
            if (!visited.insert(sp).second) return;
            for (auto c : callees[sp]) visit(c);
            calleesfirst.push_back(sp);
        };
        for (auto sp : ast.fnspecs) if (sp->live) visit(sp);
    }

    
    
    Effects SummarizeSpec(FnSpec *sp) {
        SetupSpec(sp);
        Effects e;
        effsum = &e;
        flow = Flow {};
        mode = M_KILLS;
        Walk(sp->body);
        mode = M_JUDGE;
        effsum = nullptr;
        return e;
    }

    
    
    
    
    
    void ComputeEffects() {
        for (auto sp : ast.fnspecs) {
            if (!sp->live) continue;
            auto &e = effects[sp];
            if (sp->body) continue;
            for (size_t j = 0; j < sp->argtypes.size(); j++)
                if (sp->argtypes[j]->kind == TY_REF) {
                    e.params.insert((int)j);
                    e.intparams.insert((int)j);
                }
        }
        for (auto changed = true; changed;) {
            changed = false;
            for (auto sp : calleesfirst) {
                if (!sp->body) continue;
                auto e = SummarizeSpec(sp);
                auto &cur = effects[sp];
                e.Merge(cur);
                if (!(e == cur)) {
                    cur = e;
                    changed = true;
                }
            }
        }
    }

    
    
    
    void SetupRootTrees() {
        ResetSpecState();
        IndexPlaces();
        ast.ForEachRootTree([&](Node *n) { Mark(n); });
    }

    
    
    
    
    
    
    void ValidateGlobalInvariants() {
        for (auto g : ast.globals) {
            if (g->defs.size() != 1 || g->inits.size() != 1) continue;
            auto v = g->defs[0];
            if (!IntBaseVar(v)) continue;
            auto lit = Is<IntLit>(g->inits[0]);
            gge0[v] = lit && !lit->uns && lit->val >= 0;
        }
        if (gge0.empty()) return;
        
        for (auto sp : ast.fnspecs) {
            if (!sp->live || !sp->body) continue;
            SetupSpec(sp);
            for (auto v : addrof) gaddr.insert(v);
        }
        SetupRootTrees();
        for (auto v : addrof) gaddr.insert(v);
        for (auto &[v, ok] : gge0) if (gaddr.count(v)) ok = false;
        
        auto check = [&](Node *n) {
            cands.clear();
            ge0.clear();
            
            seedglobals = true;
            mode = M_RECORD;
            flow = Flow {};
            Walk(n);
            mode = M_JUDGE;
            seedglobals = false;
            auto dropped = false;
            for (auto &[v, c] : cands) {
                auto it = gge0.find(v);
                if (it != gge0.end() && it->second && !c.ge0) {
                    it->second = false;
                    dropped = true;
                }
            }
            return dropped;
        };
        auto live = [&] {
            for (auto &[v, ok] : gge0) if (ok) return true;
            return false;
        };
        for (auto dropped = true; dropped && live();) {
            dropped = false;
            SetupRootTrees();
            ast.ForEachRootTree([&](Node *n) { dropped = check(n) || dropped; });
            for (auto sp : ast.fnspecs) {
                if (!sp->live || !sp->body) continue;
                
                
                
                auto &writes = effects[sp].intvars;
                auto here = false;
                for (auto &[v, ok] : gge0) here = here || (ok && writes.count(v));
                if (!here) continue;
                SetupSpec(sp);
                dropped = check(sp->body) || dropped;
            }
        }
        ge0.clear();
        cands.clear();
    }

    void RunAll() {
        for (auto t : ast.alltypes)
            if (IsIndexT(t) && t->ix->arr) indexarrays.insert(t->ix->arr);
        CheckfreeSpans();
        BuildCallGraph();
        ComputeEffects();
        ValidateGlobalInvariants();
        
        
        for (auto it = calleesfirst.rbegin(); it != calleesfirst.rend(); ++it)
            if ((*it)->live && (*it)->body) RunSpec(*it);
        
        SetupRootTrees();
        mode = M_JUDGE;
        ast.ForEachRootTree([&](Node *n) {
            flow = Flow {};
            Walk(n);
        });
    }

    
    
    

    int VerifyAnnotations() {
        auto fails = 0;
        for (size_t fi = 0; fi < ast.sources.size(); fi++) {
            auto &src = *ast.sources[fi].second;
            auto line = 1;
            size_t pos = 0;
            while (pos <= src.size()) {
                auto eol = src.find('\n', pos);
                auto len = (eol == string::npos ? src.size() : eol) - pos;
                auto sv = string_view(src).substr(pos, len);
                auto want = 0;
                if (sv.find("bce:elide") != string_view::npos) want = 1;
                else if (sv.find("bce:keep") != string_view::npos) want = 2;
                if (want) {
                    auto it = lineout.find({ (int)fi, line });
                    auto el = it == lineout.end() ? 0 : it->second.first;
                    auto kp = it == lineout.end() ? 0 : it->second.second;
                    auto ok = want == 1 ? el > 0 && kp == 0 : kp > 0 && el == 0;
                    if (!ok) {
                        fprintf(stderr,
                                "bce-test: %s:%d: annotated %s, but %d elided / %d kept\n",
                                ast.sources[fi].first.c_str(), line,
                                want == 1 ? "bce:elide" : "bce:keep", el, kp);
                        fails++;
                    }
                }
                if (eol == string::npos) break;
                pos = eol + 1;
                line++;
            }
        }
        return fails;
    }
};






inline void Node::BceMark(BCE &b) { Children([&](Node *ch) { b.Mark(ch); }); }

inline void Ident::BceMark(BCE &b) { b.NoteVar(vdef); }

inline void Unary::BceMark(BCE &b) {
    if (op == T_BITAND) b.MarkAddr(child);
    Node::BceMark(b);
}

inline void Dot::BceMark(BCE &b) {
    if (member == B_LEN) b.NotePlace(obj);
    Node::BceMark(b);
}

inline void Call::BceMark(BCE &b) {
    for (auto p : fvparams) b.NoteOwn(p);
    RunChildren(this, [&](Node *ch) { b.Mark(ch); });
}

inline void Index::BceMark(BCE &b) {
    b.NotePlace(obj);
    b.NoteRel(obj, idx);
    Node::BceMark(b);
}

inline void SliceExpr::BceMark(BCE &b) {
    b.NotePlace(obj);
    b.NoteRel(obj, lo);
    b.NoteRel(obj, hi);
    Node::BceMark(b);
}

inline void MatchExpr::BceMark(BCE &b) {
    for (auto &arm : arms) {
        if (arm.pat.byref) b.MarkAddr(scrutinee);
        b.NoteVar(arm.binder);
        b.NoteOwn(arm.binder);
    }
    Node::BceMark(b);
}

inline void ForLoop::BceMark(BCE &b) {
    if ((iterkind == IK_ARRAY || iterkind == IK_SLICE) && !vdef->copybind) b.MarkAddr(iter);
    b.NoteVar(vdef);
    b.NoteVar(idxdef);
    b.NoteOwn(vdef);
    b.NoteOwn(idxdef);
    Node::BceMark(b);
}

inline void VarDecl::BceMark(BCE &b) {
    for (auto d : defs) {
        b.NoteVar(d);
        b.NoteOwn(d);
    }
    if (defs.size() == 1 && inits.size() == 1 && defs[0]) b.wdecl.insert(defs[0]);
    else for (auto d : defs) if (d) b.wbad.insert(d);
    Node::BceMark(b);
}

inline void Assign::BceMark(BCE &b) {
    if (auto id = Is<Ident>(lval); id && id->vdef && !pointee) {
        auto v = id->vdef;
        auto lit = Is<IntLit>(rhs);
        if (op == T_ASSIGN) {
            b.wkinds[v].setw = true;
        } else if ((op == T_PLUSEQ || op == T_MINUSEQ) && lit && !lit->uns &&
                   BCE::SmallOff(lit->val)) {
            
            
            auto c = op == T_PLUSEQ ? lit->val : -lit->val;
            (c >= 0 ? b.wkinds[v].inc : b.wkinds[v].dec) = true;
        } else if (BCE::IsUnitStep(this, v)) {
            b.wkinds[v].inc = true;
        } else if ((op == T_PLUSEQ || op == T_MINUSEQ) && !BCE::WritesVar(rhs, v)) {
            b.wkinds[v].setw = true;   
        } else {
            b.wbad.insert(v);
        }
    }
    Node::BceMark(b);
}

inline void IncDec::BceMark(BCE &b) {
    if (auto id = Is<Ident>(lval); id && id->vdef &&
        (!id->vdef->type || id->vdef->type->kind != TY_REF))
        (op == T_INC ? b.wkinds[id->vdef].inc : b.wkinds[id->vdef].dec) = true;
    Node::BceMark(b);
}






inline bool Node::BceWalk(BCE &b) {
    Children([&](Node *ch) { b.Walk(ch); });
    return true;
}



inline bool FunVal::BceWalk(BCE &) { return true; }

inline bool Binary::BceWalk(BCE &b) {
    if ((op == T_ANDAND || op == T_OROR) && b.mode != BCE::M_KILLS) {
        
        
        b.Walk(left);
        auto after = b.flow;
        if (b.mode == BCE::M_JUDGE && !b.probing) b.ProbeRight(this);
        
        
        
        if (!b.probing && !b.HasKillEffects(left)) b.CondFacts(left, op == T_ANDAND);
        b.Walk(right);
        b.flow = b.Meet(after, b.flow);
        return true;
    }
    b.Walk(left);
    auto gen = b.nextgen;
    b.Walk(right);
    if (b.mode != BCE::M_KILLS && b.nextgen != gen)
        b.effectfulterms.insert(this);
    if (op == T_DIV || op == T_MOD || op == T_SHR) b.JudgeUnsigned(this);
    return true;
}

inline bool Index::BceWalk(BCE &b) {
    b.Walk(obj);
    auto lent = b.mode == BCE::M_KILLS ? BCE::Term {} : b.LenTermOf(obj);
    b.Walk(idx);
    
    
    
    
    
    if (lent.b.kind == BCE::BK_LEN && !(lent.b == b.LenBase(lent.b.id))) lent = BCE::Term {};
    b.JudgeIndex(this, lent);
    return true;
}

inline bool AsCast::BceWalk(BCE &b) {
    b.Walk(child);
    if (totype && totype->ix) b.JudgeIndexCast(this);
    return true;
}

inline bool SliceExpr::BceWalk(BCE &b) {
    auto kills = b.mode == BCE::M_KILLS;
    b.Walk(obj);
    
    
    
    auto lent = kills ? BCE::Term {} : b.LenTermOf(obj);
    b.slicelen = BCE::Term {};
    b.Walk(lo);
    auto lot = kills ? BCE::Term {}
                     : b.SliceBound(lo, lo_from_end, lent,
                                    BCE::Term { true, BCE::Zero(), 0 });
    auto gen = b.nextgen;
    b.Walk(hi);
    auto hit = kills ? BCE::Term {} : b.SliceBound(hi, hi_from_end, lent, lent);
    
    
    if (b.nextgen != gen) lot = BCE::Term {};
    if (!kills) b.slicelen = BCE::SliceLenTerm(lot, hit);
    if (b.mode == BCE::M_JUDGE) b.JudgeSlice(this, lent, lot, hit);
    if (!kills) {
        
        b.CheckedFacts(lot, BCE::Term {}, 0);
        b.CheckedFacts(hit, lent, 0);
        if (lot.ok && hit.ok) b.AddFactB(lot.b, hit.b, BCE::SatSub(hit.off, lot.off));
    }
    return true;
}

inline bool Call::BceWalk(BCE &b) {
    Node *recv = nullptr;
    
    
    
    
    
    
    auto site = b.mode == BCE::M_JUDGE && builtin < 0 && !fvbody && (spec || !dispatch.empty());
    vector<BCE::Term> ints, slens;
    auto gen0 = b.nextgen;
    auto walkarg = [&](Node *a) {
        b.Walk(a);
        if (!site) return;
        auto t = a->exprtype;
        auto isint = BCE::MachineInt(t);
        auto it = isint ? b.TermOf(a) : BCE::Term {};
        ints.push_back(isint && b.NoWrap(it, t->intstorage) ? it : BCE::Term {});
        slens.push_back(Is<SliceExpr>(a) ? b.slicelen : BCE::Term {});
    };
    if (auto d = Is<Dot>(callee)) {
        recv = d->obj;
        walkarg(recv);
        if (!fmtspecs.empty() && builtin != B_FORMAT) b.KillRendering();
    }
    
    
    
    
    
    
    
    auto pinrecv = b.mode != BCE::M_KILLS &&
                   (builtin == B_PUSH || builtin == B_APPEND || builtin == B_RESIZE ||
                    builtin == B_POP_N);
    auto recvpid = -1, recvgen = 0;
    auto pin = [&](Node *rn) {
        recvpid = b.PlaceOf(rn);
        if (recvpid >= 0) recvgen = b.LenBase(recvpid).gen;
    };
    if (pinrecv && recv) pin(recv);
    
    
    vector<decltype(b.nextgen)> argsgen;
    for (auto a : args) {
        walkarg(a);
        if (!fmtspecs.empty()) b.KillRendering();
        argsgen.push_back(b.nextgen);
        if (pinrecv && !recv && argsgen.size() == 1) pin(a);
    }
    auto recvmoved = recvpid >= 0 && b.LenBase(recvpid).gen != recvgen;
    auto moved = site && b.nextgen != gen0;
    if (moved) {
        for (auto &t : ints) t = BCE::Term {};
        for (auto &t : slens) t = BCE::Term {};
    }
    if (builtin >= 0) {
        if (defaultinit) {
            
            
            if (builtin != B_DEFAULT || rettypes[0]->kind == TY_ARRAY) {
                if (b.mode != BCE::M_KILLS) {
                    auto saved = std::move(b.flow);
                    b.flow = BCE::Flow {};
                    b.loopdepth++;
                    b.Walk(defaultinit);
                    b.loopdepth--;
                    b.flow = std::move(saved);
                }
                b.StripKills(defaultinit);
            } else b.Walk(defaultinit);
        }
        auto rn = recv ? recv : (args.empty() ? nullptr : args[0]);
        
        auto arg0 = recv ? (args.empty() ? nullptr : args[0])
                         : (args.size() > 1 ? args[1] : nullptr);
        switch (builtin) {
            case B_MOVE_FROM:
                
                
                if (b.HasKillEffects(arg0))
                    b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, nullptr);
                else b.KillArgStorage(rn);
                break;
            case B_PUSH:
                if (recvmoved) b.GrowShrinkKill(rn, 0);
                else b.GrowShrinkKill(rn, 1, 1);
                break;
            case B_APPEND: {
                auto st = arg0 ? b.LenTermOf(arg0) : BCE::Term {};
                if (recvmoved) b.GrowShrinkKill(rn, 0);
                else b.GrowShrinkKill(rn, 1, st.ok && st.b.kind == BCE::BK_ZERO ? st.off
                                                                               : INT64_MIN);
                break;
            }
            case B_ALLOC_INDEX: case B_ALLOC_REF: case B_ALLOC_SLICE: case B_REALLOC_SLICE:
                b.GrowShrinkKill(rn, 1, INT64_MIN, true);
                break;
            case B_FORMAT:
                b.GrowShrinkKill(rn, 1);
                break;
            case B_TO_BYTES:
                
                
                
                if (arg0) b.GrowShrinkKill(arg0, 1);
                break;
            case B_THREAD_WAIT:
                
                if (arg0) b.GrowShrinkKill(arg0, 1);
                break;
            case B_POP: {
                
                
                
                
                auto lt = b.mode == BCE::M_KILLS || !rn ? BCE::Term {} : b.LenTermOf(rn);
                if (b.mode == BCE::M_JUDGE && lt.ok &&
                    b.Query(BCE::Zero(), lt.b, BCE::SatSub(lt.off, 1)))
                    nobc = true;
                b.GrowShrinkKill(rn, -1, -1);
                if (lt.ok && lt.b.kind != BCE::BK_ZERO)
                    b.AddFactB(BCE::Zero(), lt.b, lt.off - 1);
                break;
            }
            case B_POP_N: {
                
                
                
                
                auto fresh = b.mode != BCE::M_KILLS && rn && !recvmoved;
                auto lt = fresh ? b.LenTermOf(rn) : BCE::Term {};
                auto nt = fresh ? b.TermOf(arg0) : BCE::Term {};
                auto k = nt.ok && nt.b.kind == BCE::BK_ZERO && nt.off >= 0 ? nt.off : -1;
                b.GrowShrinkKill(rn, -1, k >= 0 ? -k : INT64_MIN);
                b.CheckedFacts(nt, lt, 0);
                break;
            }
            case B_CLEAR: {
                auto pid = b.GrowShrinkKill(rn, -1);
                if (pid >= 0) b.ExactLenIs(pid, BCE::Term { true, BCE::Zero(), 0 });
                break;
            }
            case B_RESIZE: {
                
                size_t ai = recv ? 0 : 1;
                auto countmoved = ai < argsgen.size() && argsgen[ai] != b.nextgen;
                auto nt = b.mode == BCE::M_KILLS || countmoved || recvmoved ? BCE::Term {}
                                                                            : b.TermOf(arg0);
                auto pid = b.GrowShrinkKill(rn, 0);
                if (pid >= 0) b.ExactLenIs(pid, nt);
                break;
            }
            case B_ASSERT: {
                auto cond = FirstArg();
                if (b.mode != BCE::M_KILLS && !b.HasKillEffects(cond)) b.CondFacts(cond, true);
                
                if (auto bl = Is<BoolLit>(cond); bl && !bl->val) return false;
                break;
            }
            case B_ABORT: case B_EXIT:
                return false;   
            default:
                break;   
        }
        return true;
    }
    if (fvbody) {
        
        
        
        
        if (b.mode != BCE::M_KILLS) {
            auto saved = std::move(b.flow);
            b.flow = BCE::Flow {};
            b.loopdepth++;
            for (auto p : fvparams) if (p) b.BumpVar(p, false);   
            b.Walk(fvbody);
            b.loopdepth--;
            b.flow = std::move(saved);
        }
        for (auto p : fvparams) if (p) b.BumpVar(p, false);
        b.StripKills(fvbody);
        return true;
    }
    if (site) b.RecordSite(this, ArgNodes(), ints, slens, moved);
    b.CallKills(this);
    return true;
}

inline bool Block::BceWalk(BCE &b) {
    auto fell = true;
    auto defers = false;
    for (auto st : stmts) {
        fell = b.Walk(st);
        if (!fell) break;
        defers = defers || Is<Defer>(st);
    }
    if (fell) b.Walk(tail);
    
    if (fell && defers)
        for (auto st : stmts)
            if (auto d = Is<Defer>(st)) b.StripKills(d->body);
    return fell;
}

inline bool IfExpr::BceWalk(BCE &b) {
    b.Walk(cond);
    if (b.mode == BCE::M_KILLS) {
        b.Walk(thenb);
        b.Walk(elseb);
        return true;
    }
    auto killfree = !b.HasKillEffects(cond);
    auto base = b.flow;
    if (killfree) b.CondFacts(cond, true);
    auto fellthen = b.Walk(thenb);
    auto thenf = std::move(b.flow);
    b.flow = std::move(base);
    if (killfree) b.CondFacts(cond, false);
    auto fellelse = b.Walk(elseb);
    
    b.derived.erase(this);
    if (fellthen && !fellelse) { b.flow = std::move(thenf); return true; }
    if (!fellthen && fellelse) return true;   
    if (!fellthen && !fellelse) return false;
    
    
    
    small_vector<BCE::Phi, 1> val;
    if (elseb && BCE::MachineInt(exprtype)) {
        auto et = b.TermOf(elseb);
        std::swap(thenf, b.flow);
        auto tt = b.TermOf(thenb);
        auto admissible = b.CmpAdmissible(tt);
        std::swap(thenf, b.flow);
        if (admissible && b.CmpAdmissible(et)) {
            auto p = b.IndexTypeTerm(exprtype);
            val.push_back({ p.ok ? p.b : b.TmpBase(), tt, et });
        }
    }
    b.flow = b.Meet(thenf, b.flow, val);
    if (!val.empty()) b.derived[this] = { BCE::Term { true, val[0].p, 0 }, b.nextgen };
    return true;
}

inline bool MatchExpr::BceWalk(BCE &b) {
    b.Walk(scrutinee);
    if (b.mode == BCE::M_KILLS) {
        for (auto &arm : arms) {
            if (arm.binder) b.BumpVar(arm.binder, false);   
            b.Walk(arm.body);
        }
        return true;
    }
    auto st = b.TermOf(scrutinee);
    auto stt = scrutinee->exprtype;
    auto admissible = st.ok && b.CmpAdmissible(st) && stt && stt->kind == TY_INT &&
                      stt->intstorage != IS_U64;
    auto base = b.flow;
    BCE::Flow acc;
    auto anyfell = false;
    for (auto &arm : arms) {
        b.flow = base;
        if (admissible && arm.pat.kind != P_WILDCARD) {
            
            auto lo = arm.ranges[0].lo, hi = arm.ranges[0].hi;
            for (auto &r : arm.ranges) {
                lo = std::min(lo, r.lo);
                hi = std::max(hi, r.hi);
            }
            b.AddFactB(BCE::Zero(), st.b, BCE::SatSub(st.off, lo));
            b.AddFactB(st.b, BCE::Zero(), BCE::SatSub(hi, st.off));
        }
        if (b.Walk(arm.body)) {
            if (!anyfell) acc = std::move(b.flow);
            else acc = b.Meet(acc, b.flow);
            anyfell = true;
        }
    }
    if (!anyfell) { b.flow = std::move(base); return false; }
    b.flow = std::move(acc);
    return true;
}

inline bool EarlyBlock::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        return true;
    }
    if (!b.HasBreaks(body)) return b.Walk(body);
    auto entry = b.flow;
    b.Walk(body);
    b.flow = std::move(entry);
    b.StripKills(body);
    return true;
}

inline bool While::BceWalk(BCE &b) {
    b.loopdepth++;
    if (b.mode == BCE::M_KILLS) {
        b.Walk(cond);
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    b.LoopViewRefs(body, cond, hoistrefs, hoistfields);
    b.StripKills(cond);
    b.StripKills(body);
    
    
    
    auto condbreaks = b.HasBreaks(cond);
    auto top = condbreaks ? b.flow : BCE::Flow {};
    b.Walk(cond);
    auto exitf = b.flow;
    auto killfree = !b.HasKillEffects(cond);
    if (killfree) b.CondFacts(cond, true);
    b.Walk(body);
    if (condbreaks) {
        b.flow = std::move(top);
    } else {
        b.flow = std::move(exitf);
        
        
        
        if (b.HasBreaks(body)) b.StripKills(body);
        else if (killfree) b.CondFacts(cond, false);
    }
    b.loopdepth--;
    return true;
}

inline bool LoopExpr::BceWalk(BCE &b) {
    b.loopdepth++;
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    b.LoopViewRefs(body, nullptr, hoistrefs, hoistfields);
    b.StripKills(body);
    auto exitf = b.flow;
    b.Walk(body);
    b.flow = std::move(exitf);
    b.loopdepth--;
    return true;
}

inline bool ForLoop::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(iter);
        b.loopdepth++;
        if (vdef) b.BumpVar(vdef, false);
        if (idxdef) b.BumpVar(idxdef, false);
        b.Walk(body);
        b.loopdepth--;
        return true;
    }
    
    
    
    BCE::Term lot, hit;
    if (iterkind == IK_RANGE) {
        if (auto r = Is<RangeExpr>(iter)) {
            
            
            
            
            
            b.Walk(r->lo);
            lot = b.TermOf(r->lo);
            if (!b.CmpAdmissible(lot)) lot = BCE::Term {};
            auto gen = b.nextgen;
            b.Walk(r->hi);
            hit = b.TermOf(r->hi);
            if (!b.CmpAdmissible(hit)) hit = BCE::Term {};
            
            
            if (b.nextgen != gen && lot.b.kind != BCE::BK_ZERO) lot = BCE::Term {};
        }
    } else {
        b.Walk(iter);
        if (iterkind == IK_COUNT) {
            lot = BCE::Term { true, BCE::Zero(), 0 };
            hit = b.TermOf(iter);
            if (!b.CmpAdmissible(hit)) hit = BCE::Term {};
        }
    }
    
    
    
    
    
    
    vector<pair<int, int64_t>> pushpids;
    vector<BCE::Term> prelens;
    if ((iterkind == IK_COUNT || iterkind == IK_RANGE) && !body->tail &&
        !b.HasIterationJumps(body)) {
        
        
        vector<int> pushed;
        for (auto st : body->stmts) {
            auto pid = -1;
            if (auto pc = Is<Call>(st); pc && pc->builtin == B_PUSH) {
                auto pan = pc->ArgNodes();
                pid = pan.size() == 2 ? b.PlaceOf(pan[0]) : -1;
                if (pid >= 0 && b.HasKillEffects(pan[1])) pid = -1;
            }
            pushed.push_back(pid);
        }
        map<int, int64_t> counts;
        set<int> otherbumps;
        for (size_t i = 0; i < body->stmts.size(); i++) {
            set<int> bumps;
            b.SummarizeInto(body->stmts[i], bumps);
            if (pushed[i] >= 0) {
                counts[pushed[i]]++;
                bumps.erase(pushed[i]);
            }
            otherbumps.insert(bumps.begin(), bumps.end());
        }
        for (auto &[pid, k] : counts)
            if (!otherbumps.count(pid)) {
                pushpids.push_back({ pid, k });
                prelens.push_back(BCE::Term { true, b.LenBase(pid), 0 });
            }
    }
    
    
    
    if (b.mode == BCE::M_JUDGE && (iterkind == IK_ARRAY || iterkind == IK_SLICE)) {
        auto pid = b.PlaceOf(iter);
        if (pid < 0) pid = b.ElemPlaceOf(iter);
        if (pid >= 0) {
            set<int> kills;
            b.SummarizeInto(body, kills);
            fixedlen = !kills.count(pid);
        }
    }
    auto iv = iterkind == IK_ARRAY || iterkind == IK_SLICE ? idxdef : vdef;
    
    
    
    
    
    vector<pair<VarDef *, int64_t>> stepped;
    if (iv && BCE::IntBaseVar(iv) && !iv->captured && !b.addrof.count(iv)) {
        auto start = iterkind == IK_ARRAY || iterkind == IK_SLICE
                         ? BCE::Term { true, BCE::Zero(), 0 } : lot;
        if (start.ok && b.CmpAdmissible(start))
            for (auto v : b.StepCounters(body)) {
                auto c = BCE::SatSub(b.Dist(b.VarBase(v), start.b), start.off);
                if (BCE::SmallOff(c)) stepped.push_back({ v, c });
            }
    }
    b.loopdepth++;
    
    
    b.LoopViewRefs(body, iter, hoistrefs, hoistfields);
    b.StripKills(body);
    if (iterkind == IK_ARRAY || iterkind == IK_SLICE) {
        lot = BCE::Term { true, BCE::Zero(), 0 };
        hit = b.LenTermOf(iter);
        
        
        if (b.mode == BCE::M_JUDGE) {
            lenbound = -1;
            lenexact = false;
            auto up = hit.ok ? b.Dist(hit.b, BCE::Zero()) : BCE::INF;
            auto ub = up == BCE::INF ? BCE::INF : BCE::SatAdd(up, hit.off);
            if (ub >= 0 && ub < BCE::LENMAX) {
                lenbound = ub;
                lenexact = b.Query(BCE::Zero(), hit.b, BCE::SatSub(hit.off, ub));
            }
        }
    }
    auto exitf = b.flow;
    if (iv) {
        auto vb = b.VarBase(iv);
        if (lot.ok) b.AddFactB(lot.b, vb, BCE::SatSub(0, lot.off));
        if (hit.ok) b.AddFactB(vb, hit.b, BCE::SatSub(hit.off, 1));
        for (auto &[v, c] : stepped) b.AddFactB(b.VarBase(v), vb, c);
    }
    if ((iterkind == IK_RANGE || iterkind == IK_COUNT) && idxdef)
        b.AddFactB(BCE::Zero(), b.VarBase(idxdef), 0);
    b.Walk(body);
    b.flow = std::move(exitf);
    b.loopdepth--;
    if (!pushpids.empty()) {
        BCE::Term iters;
        if (iterkind == IK_COUNT) {
            iters = hit;
        } else if (lot.ok && hit.ok) {
            if (lot.b == hit.b)
                iters = BCE::Term { true, BCE::Zero(), BCE::SatSub(hit.off, lot.off) };
            else if (lot.b.kind == BCE::BK_ZERO)
                iters = BCE::Term { true, hit.b, BCE::SatSub(hit.off, lot.off) };
        }
        
        if (iters.ok && b.CmpAdmissible(iters) &&
            b.Query(BCE::Zero(), iters.b, iters.off)) {
            for (size_t i = 0; i < pushpids.size(); i++) {
                auto [pid, k] = pushpids[i];
                auto &pre = prelens[i];
                auto post = b.LenBase(pid);
                if (iters.b.kind == BCE::BK_ZERO) {
                    auto total = iters.off;
                    for (auto m = int64_t(1); m < k && total != BCE::INF; m++)
                        total = BCE::SatAdd(total, iters.off);
                    if (total == BCE::INF) continue;
                    b.AddFactB(post, pre.b, total);              
                    b.AddFactB(pre.b, post, BCE::SatSub(0, total));
                } else if (k == 1) {
                    b.AddFactB(iters.b, post, BCE::SatSub(0, iters.off));   
                    if (b.Query(pre.b, BCE::Zero(), 0))                     
                        b.AddFactB(post, iters.b, iters.off);               
                }
            }
        }
    }
    return true;
}

inline bool InlineBlock::BceWalk(BCE &b) {
    if (b.mode == BCE::M_KILLS) {
        b.Walk(body);
        return true;
    }
    
    
    auto &stmts = body->stmts;
    Node *last = stmts.empty() ? nullptr : stmts.back();
    auto lastret = Is<Return>(last) && ((Return *)last)->target == sf;
    auto early = false;
    for (size_t i = 0; i + 1 < stmts.size(); i++)
        early = early || ReturnsTo(stmts[i], sf);
    if (last) {
        if (lastret) {
            for (auto v : ((Return *)last)->vals)
                early = early || ReturnsTo(v, sf);
        } else {
            early = early || ReturnsTo(last, sf);
        }
    }
    if (body->tail) early = early || ReturnsTo(body->tail, sf);
    if (!early) {
        b.Walk(body);
        return true;
    }
    auto entry = b.flow;
    b.Walk(body);
    b.flow = std::move(entry);
    b.StripKills(body);
    return true;
}

inline bool Return::BceWalk(BCE &b) {
    for (auto v : vals) b.Walk(v);
    return false;
}

inline bool Break::BceWalk(BCE &b) {
    b.Walk(val);
    return false;
}

inline bool Continue::BceWalk(BCE &) { return false; }









inline bool Defer::BceWalk(BCE &b) {
    if (b.mode != BCE::M_KILLS) {
        auto saved = std::move(b.flow);
        b.flow = BCE::Flow {};
        b.loopdepth++;
        b.Walk(body);
        b.loopdepth--;
        b.flow = std::move(saved);
    }
    b.StripKills(body);
    return true;
}

inline bool VarDecl::BceWalk(BCE &b) {
    for (auto i : inits) b.Walk(i);
    if (defs.size() == 1 && inits.size() == 1 && defs[0]) {
        auto v = defs[0];
        auto lt = b.mode != BCE::M_KILLS && v->type && v->type->kind == TY_ARRAY
                      ? b.FreshLenOf(inits[0]) : BCE::Term {};
        
        
        if (b.mode != BCE::M_KILLS && v->type && v->type->kind == TY_SLICE &&
            Is<SliceExpr>(inits[0]))
            lt = b.slicelen;
        else if (b.mode != BCE::M_KILLS && v->type && IsRefOrSlice(v->type))
            lt = b.BoundLenOf(inits[0]);
        if (BCE::IntBaseVar(v)) {
            b.SetWrite(v, inits[0], true);
        } else if (b.loopdepth > 0 && v->type && IsRefOrSlice(v->type)) {
            b.RebindKill(v);   
        } else if (b.loopdepth > 0 && v->type && v->type->kind != TY_FLT &&
                   v->type->kind != TY_BOOL && v->type->kind != TY_INT) {
            b.StorageWriteKill(BCE::UK_OWNED, v, v);   
        }
        if (lt.ok) {
            auto pid = b.PlaceOfVar(v);
            if (pid >= 0) b.ExactLenIs(pid, lt);
        }
        
        if (b.mode != BCE::M_KILLS && v->type && v->type->kind == TY_SLICE) {
            auto cap = BCE::DeclCap(inits[0]);
            auto pid = cap >= 0 ? b.PlaceOfVar(v) : -1;
            if (pid >= 0) b.AddFactB(b.LenBase(pid), BCE::Zero(), cap);
        }
    } else {
        for (auto d : defs) if (d) b.VarKillWrite(d);
    }
    return true;
}

inline bool Assign::BceWalk(BCE &b) {
    b.WalkLvalParts(lval);
    auto gen = b.nextgen;
    b.Walk(rhs);
    if (op == T_DOTASSIGN) {
        auto ch = b.ChainOf(lval);
        if (Is<Ident>(lval) && ch.kind == BCE::CH_OK) {
            
            b.RebindKill(ch.root);
            
            if (b.mode != BCE::M_KILLS) {
                auto lt = b.BoundLenOf(rhs);
                auto pid = lt.ok ? b.PlaceOfVar(ch.root) : -1;
                if (pid >= 0) b.ExactLenIs(pid, lt);
            }
            return true;
        }
        
        
        
        auto [k, u] = b.ChainUlt(lval, true);
        b.RefSlotWriteKill(k, u, ch.root);
        return true;
    }
    if (pointee) {
        auto pt = lval->exprtype;
        b.PointeeWriteKill(lval, pt && pt->kind == TY_REF ? pt->ref->sub : nullptr);
        return true;
    }
    if (auto id = Is<Ident>(lval)) {
        auto v = id->vdef;
        if (!v) return true;
        if (BCE::IntBaseVar(v)) {
            if (op == T_ASSIGN) {
                b.SetWrite(v, rhs, false);
            } else if (op == T_PLUSEQ || op == T_MINUSEQ) {
                auto lit = Is<IntLit>(rhs);
                if (lit && !lit->uns && BCE::SmallOff(lit->val))
                    b.ShiftWrite(v, op == T_PLUSEQ ? lit->val : -lit->val);
                else if (BCE::IsUnitStep(this, v)) b.StepWrite(v);
                
                
                else b.SetWrite(v, b.nextgen == gen ? b.CompoundSum(this) : nullptr, false);
            } else {
                b.VarKillWrite(v);
            }
            return true;
        }
        auto t = v->type;
        auto fresh = b.mode != BCE::M_KILLS && t && t->kind == TY_ARRAY && op == T_ASSIGN
                         ? b.FreshLenOf(rhs) : BCE::Term {};
        if (b.mode != BCE::M_KILLS && t && t->kind == TY_SLICE && op == T_ASSIGN)
            fresh = Is<SliceExpr>(rhs) ? b.slicelen : b.BoundLenOf(rhs);
        
        if (t && t->kind == TY_SLICE) b.SlotWriteKill(BCE::UK_OWNED, v);
        else if (t && t->kind != TY_FLT && t->kind != TY_BOOL && t->kind != TY_INT)
            b.StorageWriteKill(BCE::UK_OWNED, v, v);
        if (fresh.ok) {
            auto pid = b.PlaceOfVar(v);
            if (pid >= 0) b.ExactLenIs(pid, fresh);
        }
        return true;
    }
    auto lt = lval->exprtype;
    
    
    
    
    auto elemwrite = [&] {
        if (BCE::HoldsLen(lt)) b.ElementLvalKill(lval);
        if (BCE::HoldsRef(lt)) {
            auto [k, u] = b.ChainUlt(lval, true);
            b.RefSlotWriteKill(k, u, nullptr);
        }
        return true;
    };
    if (Is<Index>(lval)) return elemwrite();
    if (lt && !BCE::HoldsLen(lt, true))
        return true;                    
    auto ch = b.ChainOf(lval);
    if (ch.kind == BCE::CH_INDEX) return elemwrite();
    if (ch.kind == BCE::CH_FAIL || !ch.root) {
        b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, nullptr);
        return true;
    }
    if (!ch.anycross) {
        b.StorageWriteKill(BCE::UK_OWNED, ch.root, ch.root);
    } else if (ch.rootonlycross) {
        auto [k, u] = b.UltOf(ch.root);
        if (k != BCE::UK_STATIC) b.StorageWriteKill(k, u, ch.root);
        else b.RebindKill(ch.root);
    } else {
        b.StorageWriteKill(BCE::UK_OPAQUE, nullptr, ch.root);
    }
    return true;
}

inline bool IncDec::BceWalk(BCE &b) {
    b.WalkLvalParts(lval);
    auto lt = lval->exprtype;
    if (lt && lt->kind == TY_REF) {
        b.PointeeWriteKill(lval, lt->ref->sub);
        return true;
    }
    if (auto id = Is<Ident>(lval); id && id->vdef) {
        if (BCE::IntBaseVar(id->vdef)) b.ShiftWrite(id->vdef, op == T_INC ? 1 : -1);
        else b.VarKillWrite(id->vdef);
    }
    
    return true;
}

}  
