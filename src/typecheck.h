






























































#pragma once

#include "type_queries.h"

namespace faloya {

enum IterKind { IK_RANGE, IK_COUNT, IK_ARRAY, IK_SLICE };


enum ValidPos { VT_LOCAL, VT_GLOBAL, VT_PARAM, VT_RET, VT_FIELD, VT_ELEM, VT_POINTEE };





inline bool BreaksWithValue(Node *body) {
    auto found = false;
    function<void(Node *)> walk = [&](Node *n) {
        if (found) return;
        if (auto b = Is<Break>(n); b && b->val) {
            found = true;
            return;
        }
        if (auto f = Is<ForLoop>(n)) {
            walk(f->iter);
            return;
        }
        if (Is<LoopExpr>(n) || Is<While>(n) || Is<EarlyBlock>(n) || Is<FunVal>(n)) return;
        n->Children(walk);
    };
    walk(body);
    return found;
}


inline bool IsBranchConstruct(const Node *n) {
    return Is<IfExpr>(n) || Is<MatchExpr>(n) || Is<Block>(n) || Is<EarlyBlock>(n) ||
           Is<LoopExpr>(n);
}

struct TypeCheck {
    Ast &ast;
    StableTypeGraph stabletypes;
    unordered_map<const void *, vector<TypeExpr *>> arrayelements;
    
    
    
    using TypePairCache = flat_map<pair<TypeExpr *, TypeExpr *>, bool>;
    TypePairCache cancontain, reachesthrough;
    bool library = false;   
    bool nomain = false;    

    
    

    
    
    struct ReadBack {
        Roots roots;
        VarDef *from = nullptr;
    };

    
    
    
    struct LVal : Prov {
        TypeExpr *type = nullptr;    
        VarDef *var = nullptr;       
        
        
        bool letbound = false;
        string_view letname;
        VarDef *copyof = nullptr;    
        bool fromstorage = false;    
                                     
        bool throughref = false;     
                                     
        
        
        
        bool isslot = false;
        bool fotail = false;         
        bool isvarint = false;       
        
        
        
        bool intemp = false;
        ReadBack contents;
    };

    
    
    
    
    
    
    
    
    
    struct OuterFn {
        SFunction *sf = nullptr;
        FnSpec *env = nullptr;
        int scopeserial = 0;
        int order = 0;
    };
    struct DeclSite {
        vector<VarDef *> vars;
        vector<OuterFn> fns;
    };

    
    
    
    
    
    struct BodyExits {
        vector<VarDef *> vars;
        vector<bool> assigned, maybeassigned, maybeonentry;
        bool reached = false;
    };

    
    struct Frame {
        SFunction *sf = nullptr;     
        FnSpec *spec = nullptr;      
        FnSpec *lexspec = nullptr;   
        int lexframe = -1;           
        
        
        DeclSite *decl = nullptr;
        int scopebase = 0;           
        int varbase = 0;             
        Line callline;               
        string_view expansiontrace;  
        bool isfunval = false;
        bool isdefault = false;    
        
        
        bool tailvalue = false;
        
        
        
        SFunction *defaultfn = nullptr;
        int defaultparam = -1;
        DeclSite *defaultsite = nullptr;
        
        
        const Field *defaultfield = nullptr;
        const StructLit *defaultlit = nullptr;
        TypeExpr *defaultowner = nullptr;
        
        
        int cyclecalls = 0;
        Line cyclecall;
        
        
        
        int bodyidx = 0;
        
        
        
        BodyExits *exits = nullptr;
        vector<Val> *argvals = nullptr;
        Call *call = nullptr;
    };

    enum ScopeKind { SK_PLAIN, SK_FN, SK_LOOP, SK_BLOCK, SK_EXPANSION, SK_DEFER };
    
    
    
    
    
    
    
    
    
    struct Kill {
        VarDef *container = nullptr;
        VarDef *base = nullptr;
        string path;
        size_t at = 0;
        bool operator==(const Kill &) const = default;
    };
    
    
    
    
    
    
    
    struct VarFlow {
        bool assigned = false;
        bool maybeassigned = false;
        TypeExpr *narrowed = nullptr;
        bool operator==(const VarFlow &) const = default;
    };
    struct FlowEntry {
        int index;                            
        VarDef *var;                          
        VarFlow state;
        bool operator==(const FlowEntry &) const = default;
    };
    struct FlowState {
        small_vector<FlowEntry, 16> locals;
        
        
        vector<pair<VarDef *, TypeExpr *>> globals;
        bool reachable = true;
        
        vector<Kill> kills;
        bool operator==(const FlowState &) const = default;
    };
    
    
    struct Dest {
        Roots roots;
        FnSpec *result = nullptr;
        int resultindex = 0;
        
        
        bool *used = nullptr;
        bool varbind = false;  
        
        
        TypeExpr *reached = nullptr;
        
        
        bool unknown = false;
        
        
        bool slot = false;
        
        
        bool fill = false;
        Dest() {}
        Dest(const Roots &r, bool vb = false, TypeExpr *re = nullptr)
            : roots(r), varbind(vb), reached(re), unknown(r.None()) {}
        
        Dest(VarDef *vd, bool vb = false) : varbind(vb) { roots.Set(vd, true); }
    };
    struct Scope {
        int kind = SK_PLAIN;
        int serial = 0;              
        int varbase = 0;
        int fnbase = 0;              
        Node *node = nullptr;        
        TypeExpr *breaktype = nullptr;
        Val breakvalue;             
        TypeExpr *breakexpected = nullptr;   
        Dest breakdest;             
        bool onargpath = false;     
        bool onjoinpath = false;    
        bool inreturn = false;      
        bool hasbreak = false;
        bool valuelessbreak = false;
        
        
        
        FlowState breakflow;
        bool breakflows = false;
        
        
        
        FlowState backedge;
        bool backedges = false;
    };

    vector<Frame> frames;
    vector<Scope> scopes;
    
    
    
    struct BlockPos {
        Block *block = nullptr;
        size_t idx = 0;             
        int scopeidx = 0;           
    };
    vector<BlockPos> blockpos;
    
    
    struct BlockScope {
        TypeCheck &tc;
        BlockScope(TypeCheck &_tc, Block *b) : tc(_tc) {
            tc.blockpos.push_back({ b, 0, (int)tc.scopes.size() - 1 });
        }
        ~BlockScope() { tc.blockpos.pop_back(); }
    };
    void CheckStmts(Block *b);
    size_t expansionsize = 0;
    Node *ExpansionConstant(Node *n, set<VarDecl *> &visiting,
                            const char *what = "const for source");
    void ExpandConstFor(ConstFor *cf, const function<void(ExpansionClone &)> &emit);
    Block *ExpandStatements(ConstFor *cf);
    bool ConstCondition(Node *n);
    bool ConstComparison(Binary *b);
    bool ConstQueryValue(ConstQuery *q);
    ConstIfBranch *SelectBranch(ConstIf *ci);
    Block *ExpandSelection(ConstIf *ci);
    void ExpandInitializers(vector<FieldInit> &inits);
    void ExpandArms(vector<MatchArm> &arms);
    bool IsValuelessTail(const Node *n);
    bool MentionsName(Node *n, string_view name, set<SFunction *> &seen);
    bool EscapingContinue(Node *n);
    bool UsedAfter(VarDef *v);

    
    
    
    
    
    
    
    
    
    
    
    struct PathEntry {
        Node *node;
        int idx;     
        int pos;     
        
        
        bool discovering = false;
        
        
        int frame = 0;
    };
    struct Discovering {
        TypeCheck &tc;
        Node *node;
        Discovering(TypeCheck &t, Node *n) : tc(t), node(n) { Set(true); }
        ~Discovering() { Set(false); }
        void Set(bool on) {
            if (!tc.cur.nodepath.empty() && tc.cur.nodepath.back().node == node)
                tc.cur.nodepath.back().discovering = on;
        }
    };
    struct NodeScope {
        TypeCheck &tc;
        Node *node;
        bool pushed;
        NodeScope(TypeCheck &t, Node *n) : tc(t), node(n), pushed(t.Descend(n)) {}
        ~NodeScope() { if (pushed) tc.Ascend(node); }
    };
    bool Descend(Node *n);
    void Ascend(Node *n);
    
    flat_map<Node *, Val> nodevals;
    void RecordVal(Node *n, const Val &v) {
        if (!v.None() || v.holderset) nodevals[n] = v;
    }
    
    
    
    
    
    enum HoldKind { HK_NONE, HK_VALUE, HK_VIEW, HK_ELEMS, HK_RECEIVER, HK_LOCATION,
                    HK_SEQUENCE };
    template<typename F> void ForOperands(Node *n, F f);
    int OperandIndex(Node *parent, Node *child, int next = -1);
    
    
    
    
    struct Held {
        Node *node;
        Val v;
        bool location = false;
        
        
        const char *render = nullptr;
        
        bool elems = false;
        
        
        
        ForLoop *loop = nullptr;
        bool reread = false;
        
        
        
        bool inplace = false;
    };
    template<typename F> void HoldAs(Node *n, const Val &v, HoldKind kind, Node *parent,
                                     const char *render, F f);
    template<typename F> void HoldForSequence(ForLoop *fl, const Val &v, F f);
    template<typename F> void HeldOperands(F f);
    template<typename F> void LaterOperands(F f);
    template<typename F> void AfterHead(Node *n, Node *next, F f);
    vector<VarDef *> vars;                            
    vector<pair<int, SFunction *>> localfns;          
    
    
    std::set<vector<SFunction *>> localfnsets;
    int scopeserial = 0;
    deque<DeclSite> declsites;
    
    
    map<pair<FnSpec *, SFunction *>, DeclSite *> declsiteof;
    bool reachable = true;
    
    
    
    vector<VarDef *> narrowedglobals;
    
    
    
    
    bool inreturn = false;
    
    
    struct FlagScope {
        bool &flag;
        bool saved;
        FlagScope(bool &f, bool v) : flag(f), saved(f) { f = v; }
        ~FlagScope() { flag = saved; }
    };
    
    template<typename T> struct Saved {
        T &var;
        T saved;
        Saved(T &v, T val) : var(v), saved(std::move(v)) { var = std::move(val); }
        ~Saved() { var = std::move(saved); }
    };
    Dest curdst;
    vector<Dest> callresultdsts;
    bool callforwarding = false;
    TypeExpr *callresulttype = nullptr;
    
    Node *multidestnode = nullptr;
    vector<Dest> multidests;
    bool recordingrelative = false;
    Dest ResultDest(FnSpec *spec, int index = 0) {
        Dest d;
        d.result = spec;
        d.resultindex = index;
        return d;
    }
    bool RelativeDestination(const Roots &source, Dest dest, Node *at, bool record,
                             bool forwarding = false);
    void CheckCalleeRelative(Call *call, FnSpec *spec, vector<Val> &args);
    bool ResultLayoutFits(TypeExpr *from, TypeExpr *to);
    
    
    Node *rootinit = nullptr;
    VarDef *rootowner = nullptr;
    struct RootInitScope {
        TypeCheck &tc;
        Node *init;
        VarDef *owner;
        RootInitScope(TypeCheck &t, Node *n, VarDef *v)
            : tc(t), init(t.rootinit), owner(t.rootowner) { t.rootinit = n; t.rootowner = v; }
        ~RootInitScope() { tc.rootinit = init; tc.rootowner = owner; }
    };
    
    
    
    
    
    bool constslot = false;
    struct SlotScope {
        TypeCheck &tc;
        bool saved;
        SlotScope(TypeCheck &t, bool slot) : tc(t), saved(t.constslot) { tc.constslot = slot; }
        ~SlotScope() { tc.constslot = saved; }
    };
    
    
    struct DestScope {
        TypeCheck &tc;
        Dest saved;
        DestScope(TypeCheck &t, Dest d) : tc(t), saved(t.curdst) { tc.curdst = d; }
        ~DestScope() { tc.curdst = saved; }
    };
    
    
    
    VarDef *temproot = nullptr;
    TypeExpr *fntype = nullptr;  
    TypeExpr *u8slice = nullptr; 
    TypeExpr *cu8slice = nullptr; 
    TypeExpr *nulltype = nullptr;  

    
    

    string Where(Line l) {
        if (l.fileidx < 0 || l.fileidx >= (int)ast.sources.size()) return "?";
        return cat(ast.sources[l.fileidx].first, ":", l.line);
    }

    
    
    static bool NamesGeneric(const TypeExpr *t, string_view name) {
        switch (t->kind) {
            case TY_GENERIC: return t->named->name == name;
            case TY_STRUCT:
                for (auto a : t->struc->args) if (NamesGeneric(a, name)) return true;
                return false;
            case TY_ENUM:
                for (auto a : t->enu->args) if (NamesGeneric(a, name)) return true;
                return false;
            case TY_ARRAY:   return NamesGeneric(t->arr->sub, name);
            case TY_SLICE:   return NamesGeneric(t->sub, name);
            case TY_REF:     return NamesGeneric(t->ref->sub, name);
            case TY_VARIANT: return NamesGeneric(t->var->adt, name);
            default:         return false;
        }
    }

    static constexpr int MAXCHAIN = 20;

    string_view ActiveExpansionTrace() {
        for (auto i = cur.nodepath.rbegin(); i != cur.nodepath.rend(); ++i)
            if (!i->node->expansiontrace.empty()) return i->node->expansiontrace;
        return {};
    }
    [[noreturn]] void Error(Line l, const string &msg) {
        ErrorIn(LocOf(l), msg, Chain(ActiveExpansionTrace()));
    }

    bool StdlibLocation(DiagLoc at) const { return ast.StdlibFile(at.fileidx); }

    void LibraryCallLocation(Diagnostic &d) {
        if (!StdlibLocation(d.where)) return;
        for (auto &related : d.related) {
            if (related.where.fileidx < 0 || StdlibLocation(related.where)) continue;
            auto definition = d.where;
            d.where = related.where;
            d.related.insert(d.related.begin(), { definition, "while checking this standard library expression" });
            return;
        }
    }

    
    
    
    
    [[noreturn]] void ErrorIn(DiagLoc where, const string &msg,
                              vector<Diagnostic::Related> chain) {
        for (auto &w : cur.pendingwarnings) if (!w.cast) PrintWarning(w);
        cur.pendingwarnings.clear();
        Diagnostic d;
        d.where = where;
        d.message = msg;
        d.related = std::move(chain);
        LibraryCallLocation(d);
        d.text = ast.DiagnosticText(d);
        throw CompileError { d.text, { d } };
    }

    
    
    
    
    [[noreturn]] void IncompleteDot(Dot *d) {
        auto isenum = false;
        if (auto id = Is<Ident>(d->obj); id && id->expansionerror.empty()) {
            const FnValBind *fb;
            isenum = !LookupVar(id->name, id->ns) && !LookupTypeParam(id->name, fb) && !fb &&
                     ast.LookupEnum(id->name, id->ns);
        }
        if (!isenum) {
            auto ov = CheckV(d->obj, nullptr);
            d->obj->exprtype = ov.type;
        }
        throw Abandoned {};
    }

    
    
    vector<Diagnostic::Related> Chain(string_view trace) {
        vector<Diagnostic::Related> chain;
        AppendTrace(chain, trace);
        InstantiationChain(chain);
        return chain;
    }

    
    
    static void AppendTrace(vector<Diagnostic::Related> &chain, string_view trace) {
        while (!trace.empty()) {
            if (trace.substr(0, 3) == "\n  ") trace.remove_prefix(3);
            auto next = trace.find("\n  ");
            chain.push_back({ {}, string(trace.substr(0, next)) });
            if (next == string_view::npos) break;
            trace.remove_prefix(next);
        }
    }

    
    
    void InstantiationChain(vector<Diagnostic::Related> &out) {
        
        auto chain = 0, nth = 0;
        string_view lasttrace;
        for (auto &f : frames) chain += f.sf && !f.isfunval;
        for (auto i = (int)frames.size() - 1; i > 0; i--) {
            auto &f = frames[i];
            if (!f.expansiontrace.empty() && f.expansiontrace != lasttrace) {
                AppendTrace(out, f.expansiontrace);
                lasttrace = f.expansiontrace;
            }
            if (f.defaultfn) {
                out.push_back({ LocOf(f.callline),
                                cat("in the default of parameter ",
                                    f.defaultfn->params[f.defaultparam].name, " of ",
                                    f.defaultfn->qname, ", for the call at ", Where(f.callline)) });
                continue;
            }
            if (f.defaultfield) {
                out.push_back({ LocOf(f.defaultlit->line),
                                cat("in the default of field ", f.defaultfield->name, " of ",
                                    LitTypeStr(f.defaultlit, f.defaultowner),
                                    ", for the construction at ", Where(f.defaultlit->line)) });
                continue;
            }
            if (!f.sf || f.isfunval) continue;
            nth++;
            if (chain > MAXCHAIN && nth > MAXCHAIN / 2 && nth <= chain - MAXCHAIN / 2) {
                if (nth == MAXCHAIN / 2 + 1)
                    out.push_back({ {}, cat("... ", chain - MAXCHAIN, " more instantiations") });
                continue;
            }
            string s = f.sf->isthread ? "in thread_fn " : "in fn ";
            if (!f.spec) Append(s, f.sf->qname, "()");
            else DumpInstance(s, f.sf, f.spec->argtypes, f.spec->litparams, f.spec->bindings,
                              f.spec->fnvals);
            Append(s, " instantiated from ", Where(f.callline));
            out.push_back({ LocOf(f.callline), std::move(s) });
        }
    }

    
    
    
    
    void DumpInstance(string &s, SFunction *sf, const vector<TypeExpr *> &argtypes,
                      const vector<int> &litparams,
                      const vector<pair<string_view, TypeExpr *>> &bindings,
                      const vector<pair<string_view, FnValBind>> &fnvals) {
        s += sf->qname;
        auto listed = false;
        auto item = [&](string_view n) {
            Append(s, listed ? ", " : "<", n, " = ");
            listed = true;
        };
        for (auto &g : sf->generics) {
            for (auto &[n, fb] : fnvals) {
                if (n != g.name) continue;
                item(n);
                auto fv = fb.fv;
                if (fb.named) s += fb.named->qname;
                else if (fv) Append(s, "{block at ", Where(fv->line), ":", fv->line.col, "}");
                else s += "?";
            }
            auto named = false;
            for (auto &p : sf->params) named |= p.type && NamesGeneric(p.type, g.name);
            if (named) continue;
            for (auto &[n, t] : bindings) {
                if (n != g.name) continue;
                item(n);
                DumpShort(s, t);
            }
        }
        if (listed) s += ">";
        s += "(";
        for (size_t j = 0; j < argtypes.size(); j++) {
            if (j) s += ", ";
            for (auto li : litparams) if (li == (int)j) s += "literal ";
            DumpShort(s, argtypes[j]);
        }
        s += ")";
    }

    
    void DumpShort(string &s, const TypeExpr *t) {
        auto limit = s.size() + 200;
        t->Dump(s, limit);
        if (s.size() > limit) {
            s.resize(limit);
            s += "...";
        }
    }

    [[noreturn]] void Error(const Node *n, const string &msg) {
        ErrorIn(LocOf(n), msg,
                Chain(n->expansiontrace.empty() ? ActiveExpansionTrace() : n->expansiontrace));
    }

    
    
    
    
    struct Warning {
        Diagnostic diag;
        const Node *at = nullptr;
        const AsCast *cast = nullptr;
        bool redundant = false;
        const AsCast *dependson = nullptr;
    };
    
    
    bool quiet = false;
    void Warn(const Node *n, const string &msg) {
        if (quiet) return;
        Warning w;
        w.diag.where = LocOf(n);
        w.diag.message = msg;
        if (StdlibLocation(w.diag.where)) {
            w.diag.related = Chain(n->expansiontrace);
            LibraryCallLocation(w.diag);
        }
        w.at = n->Origin();
        if (WarningsHeld()) {
            cur.pendingwarnings.push_back(std::move(w));
            return;
        }
        PrintWarning(w);
    }
    
    
    
    
    
    bool WarningsHeld() { return !cur.looppasses.empty() || cur.joinprobes > 0; }
    void FlushWarnings() {
        for (auto &w : cur.pendingwarnings) {
            if (w.cast) castverdicts.push_back(std::move(w));
            else PrintWarning(w);
        }
        cur.pendingwarnings.clear();
    }
    
    
    
    
    
    
    
    
    
    set<pair<const Node *, string>> warned;
    void PrintWarning(const Warning &w) {
        if (warned.insert({ w.at, w.diag.message }).second) ast.Warn(w.diag);
    }

    string TypeStr(const TypeExpr *t) {
        
        
        if (IsUntypedEmptyArray(t)) return "[]";
        string s;
        t->Dump(s);
        return s;
    }

    
    
    string LitTypeStr(const StructLit *sl, const TypeExpr *selft) {
        auto s = TypeStr(selft);
        if (selft->kind == TY_ENUM) Append(s, ".", sl->variant->name);
        return s;
    }

    
    
    
    

    
    
    
    
    struct ConstUse {
        Node *at;
        const char *what;
        VarDef *named = nullptr;
    };

    bool ConstIntValue(Node *n, Val &v, bool &literal, set<VarDecl *> &visiting,
                       ConstUse *use = nullptr);
    bool ConstExtent(Ident *id, bool cap, int64_t &extent, set<VarDecl *> &visiting,
                     ConstUse *use);

    bool ConstInt(Node *n, int64_t &v, ConstUse *use = nullptr) {
        Val value;
        bool literal;
        set<VarDecl *> visiting;
        if (!ConstIntValue(n, value, literal, visiting, use)) return false;
        v = value.ival;   
        return true;
    }

    int64_t ConstIntOrError(Node *n, const char *context) {
        int64_t v;
        ConstUse use { n, context };
        if (!ConstInt(n, v, &use))
            Error(n, cat("constant integer expression expected for ", context));
        return v;
    }

    
    int64_t ArraySize(TypeArray *a) {
        if (a->size < 0 && a->sizeexpr) {
            auto v = ConstIntOrError(a->sizeexpr, "array size");
            if (v < 0) Error(a->sizeexpr, "array size cannot be negative");
            a->size = v;
        }
        return a->size;
    }

    
    
    
    
    const char *ZeroSizeKind(TypeExpr *t) {
        if (t->kind == TY_VARIANT && EmptyLayout(t->var->variant->fields))
            return "a variant type with no fields";
        if (t->kind == TY_ARRAY && (t->arr->akind == A_FIXED || t->arr->akind == A_LIMITED) &&
            ArraySize(t->arr) == 0)
            return "a zero-length array";
        return nullptr;
    }
    void NoZeroSizeElement(TypeExpr *t, Line l) {
        if (auto zs = ZeroSizeKind(t))
            Error(l, cat("an element cannot be ", zs, ": ", TypeStr(t), " (§3.4)"));
    }

    
    
    void ConstName(Ident *id, const char *what);
    void ConstNames(Node *n, const char *what);
    void ConstNamesIn(TypeExpr *t);
    void SignatureNames(const vector<Param> &params, const vector<TypeExpr *> &rets,
                        const vector<GenericParam> &generics);
    void TypeParamSizes(TypeExpr *t, const vector<GenericParam> &generics);
    int64_t FillCount(Node *n);

    
    
    template<typename F> void ConstExprNames(Node *n, const char *what, F f) {
        if (auto id = Is<Ident>(n)) {
            f(id, what);
        } else if (auto u = Is<Unary>(n)) {
            ConstExprNames(u->child, what, f);
        } else if (auto b = Is<Binary>(n)) {
            ConstExprNames(b->left, what, f);
            ConstExprNames(b->right, what, f);
        } else if (auto d = Is<Dot>(n)) {
            if (auto obj = Is<Ident>(d->obj)) f(obj, what);
        }
    }

    
    
    template<typename F> void SizeNames(TypeExpr *t, F f) {
        if (t->aliasuse) return;
        switch (t->kind) {
            case TY_ARRAY:
                if (t->arr->sizeexpr)
                    ConstExprNames(t->arr->sizeexpr,
                                   t->arr->akind == A_LIMITED ? "array capacity" : "array size", f);
                SizeNames(t->arr->sub, f);
                return;
            case TY_SLICE: SizeNames(t->sub, f); return;
            case TY_REF: SizeNames(t->ref->sub, f); return;
            case TY_STRUCT: for (auto a : t->struc->args) SizeNames(a, f); return;
            case TY_ENUM: for (auto a : t->enu->args) SizeNames(a, f); return;
            case TY_VARIANT: SizeNames(t->var->adt, f); return;
            default: return;
        }
    }

    
    

    bool TypeEq(TypeExpr *a, TypeExpr *b) {
        if (a == b) return true;
        if (a->kind != b->kind || a->cq != b->cq) return false;
        switch (a->kind) {
            case TY_INT:  return a->intstorage == b->intstorage && SameIndex(a, b);
            case TY_FLT:  return a->fltstorage == b->fltstorage;
            case TY_BOOL: case TY_VOID: case TY_FN: return true;
            case TY_STRUCT: {
                if (a->struc->st != b->struc->st) return false;
                return TypeArgsEq(a->struc->args, b->struc->args);
            }
            case TY_ENUM: {
                if (a->enu->en != b->enu->en || a->enu->varmode != b->enu->varmode) return false;
                return TypeArgsEq(a->enu->args, b->enu->args);
            }
            case TY_ARRAY: {
                auto &x = *a->arr, &y = *b->arr;
                if (x.akind != y.akind || !TypeEq(x.sub, y.sub)) return false;
                switch (x.akind) {
                    case A_FIXED:   return ArraySize(a->arr) == ArraySize(b->arr);
                    case A_VAR: {
                        auto ls = [](int s) { return s < 0 ? IS_U32 : (IntStorage)s; };
                        return ls(x.lenstorage) == ls(y.lenstorage);
                    }
                    case A_LIMITED: {
                        auto xs = x.sizeexpr ? ArraySize(a->arr) : -1;
                        auto ys = y.sizeexpr ? ArraySize(b->arr) : -1;
                        return xs == ys;
                    }
                    default: return true;
                }
            }
            case TY_SLICE: return TypeEq(a->sub, b->sub);
            case TY_REF:
                
                
                return TypeEq(a->ref->sub, b->ref->sub) && a->ref->optional == b->ref->optional &&
                       a->ref->lenstorage == b->ref->lenstorage && a->ref->pool == b->ref->pool;
            case TY_VARIANT:
                return a->var->variant == b->var->variant && TypeEq(a->var->adt, b->var->adt);
            case TY_GENERIC:
                if (a->named->opaque || b->named->opaque) return a->named == b->named;
                return a->named->name == b->named->name;
            default: assert(false); return false;
        }
    }

    
    
    
    
    bool TopConstEq(TypeExpr *a, TypeExpr *b) {
        if (a->cq == b->cq) return TypeEq(a, b);
        TypeExpr ta = *a, tb = *b;
        ta.cq = tb.cq = false;
        return TypeEq(&ta, &tb);
    }

    bool TypeArgsEq(vector<TypeExpr *> &a, vector<TypeExpr *> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) if (!TypeEq(a[i], b[i])) return false;
        return true;
    }

    
    
    bool SameAdt(TypeExpr *a, TypeExpr *b) {
        return a->enu->en == b->enu->en && TypeArgsEq(a->enu->args, b->enu->args);
    }

    
    
    bool BindingsEq(vector<pair<string_view, TypeExpr *>> &a,
                    vector<pair<string_view, TypeExpr *>> &b) {
        if (a.size() != b.size()) return false;
        for (auto &[n, t] : a) {
            auto same = false;
            for (auto &[m, u] : b)
                if (m == n) { same = TypeEq(t, u); break; }
            if (!same) return false;
        }
        return true;
    }

    
    
    
    

    TypeExpr *LookupBinding(string_view name) {
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent)
            for (auto &[n, t] : sp->bindings) if (n == name) return t;
        return nullptr;
    }

    
    
    
    TypeExpr *LookupTypeParam(string_view name, const FnValBind *&fn) {
        fn = nullptr;
        for (auto sp = frames.back().lexspec; sp; sp = sp->lexparent) {
            for (auto &[n, fv] : sp->fnvals) if (n == name) { fn = &fv; return nullptr; }
            for (auto &[n, t] : sp->bindings) if (n == name) return t;
        }
        return nullptr;
    }

    
    
    
    TypeExpr *Subst(TypeExpr *t) {
        auto n = SubstRaw(t);
        return t->cq && !n->cq ? ast.ConstOf(n) : n;
    }

    TypeExpr *SubstRaw(TypeExpr *t) {
        switch (t->kind) {
            case TY_GENERIC: {
                
                
                
                if (t->named->opaque) return t;
                if (!t->named->expansionerror.empty()) Error(t->line, string(t->named->expansionerror));
                auto b = LookupBindingOuter(t->named->name);
                if (!b) return t;  
                if (!t->named->args.empty())
                    Error(t->line, cat("generic parameter ", t->named->name,
                                       " takes no type arguments"));
                if (t->named->varmode) {
                    if (b->kind != TY_ENUM)
                        Error(t->line, cat("variable mode (..) requires an ADT type, not ",
                                           TypeStr(b)));
                    if (b->enu->varmode) return b;
                    return ast.EnumOf(b->enu->en, b->enu->args, true, t->line);
                }
                return b;
            }
            case TY_STRUCT: {
                auto args = SubstArgs(t->struc->args);
                if (!args) return t;
                return ast.StructOf(t->struc->st, std::move(*args), t->line);
            }
            case TY_ENUM: {
                auto args = SubstArgs(t->enu->args);
                if (!args) return t;
                return ast.EnumOf(t->enu->en, std::move(*args), t->enu->varmode, t->line);
            }
            case TY_ARRAY: {
                auto sub = Subst(t->arr->sub);
                if (sub == t->arr->sub) return t;
                auto n = ast.NewType(TY_ARRAY, t->line);
                n->arr = ast.NewDetail<TypeArray>();
                *n->arr = *t->arr;
                n->arr->sub = sub;
                return n;
            }
            case TY_SLICE: {
                auto sub = Subst(t->sub);
                if (sub == t->sub) return t;
                auto n = ast.SliceOf(sub, t->line);
                n->cq = t->cq;
                return n;
            }
            case TY_REF: {
                auto sub = Subst(t->ref->sub);
                if (sub == t->ref->sub) return t;
                
                
                
                
                if (sub->kind == TY_REF && t->ref->lenstorage < 0) {
                    if (!t->ref->optional) return LoadType(sub);
                    if (sub->ref->optional) return sub;
                    auto n = ast.NewType(TY_REF, t->line);
                    n->ref = ast.NewDetail<TypeRef>();
                    *n->ref = *sub->ref;
                    n->ref->optional = true;
                    n->cq = sub->cq;
                    return n;
                }
                auto n = ast.NewType(TY_REF, t->line);
                n->ref = ast.NewDetail<TypeRef>();
                *n->ref = *t->ref;
                n->ref->sub = sub;
                n->cq = t->cq;
                return n;
            }
            case TY_VARIANT: {
                auto adt = Subst(t->var->adt);
                if (adt == t->var->adt) return t;
                if (adt->kind != TY_ENUM)
                    Error(t->line, cat("variant type of non-ADT type ", TypeStr(adt)));
                auto name = t->var->name;
                auto found = adt->enu->en->FindVariant(name);
                if (!found) {
                    if (adt->enu->en->broken) throw Abandoned {};
                    Error(t->line, cat("enum ", adt->enu->en->name, " has no variant named ", name));
                }
                return ast.VariantOf(adt, name, found, t->line);
            }
            case TY_FN: {
                auto args = SubstArgs(t->fn->args), rets = SubstArgs(t->fn->rets);
                if (!args && !rets) return t;
                auto n = ast.NewType(TY_FN, t->line);
                n->fn = ast.NewDetail<TypeFn>();
                *n->fn = *t->fn;
                if (args) n->fn->args = std::move(*args);
                if (rets) n->fn->rets = std::move(*rets);
                return n;
            }
            default: return t;
        }
    }

    
    unique_ptr<vector<TypeExpr *>> SubstArgs(vector<TypeExpr *> &args) {
        auto changed = false;
        vector<TypeExpr *> out;
        out.reserve(args.size());
        for (auto a : args) {
            auto s = Subst(a);
            changed |= s != a;
            out.push_back(s);
        }
        if (!changed) return nullptr;
        return make_unique<vector<TypeExpr *>>(std::move(out));
    }

    
    
    
    

    vector<pair<string_view, TypeExpr *>> *extrabindings = nullptr;
    bool bindonly = false;
    
    
    
    const vector<GenericParam> *ownexclude = nullptr;

    TypeExpr *LookupBindingOuter(string_view name);
    void BindGenerics(vector<GenericParam> &generics, vector<TypeExpr *> &args, string_view what,
                      string_view name, Line l, vector<pair<string_view, TypeExpr *>> &out);

    
    
    template<typename F> void WithBindings(vector<pair<string_view, TypeExpr *>> &b, F f) {
        struct Restore {
            TypeCheck &tc;
            vector<pair<string_view, TypeExpr *>> *saveb;
            bool saveo;
            ~Restore() {
                tc.extrabindings = saveb;
                tc.bindonly = saveo;
            }
        } restore { *this, extrabindings, bindonly };
        extrabindings = &b;
        bindonly = true;
        f();
    }

    StructInst *GetStructInst(TypeExpr *t);
    EnumInst *GetEnumInst(TypeExpr *t);
    void BuildVariant(EnumInst *inst, size_t vi);
    void LimitNestedInsts(TypeExpr *t);
    
    
    span<const FieldRun> FieldRuns(TypeExpr *t);
    
    
    template<typename F> bool AnyField(TypeExpr *t, F f) { return AnyFieldOf(FieldRuns(t), f); }
    template<typename F> void EachField(TypeExpr *t, F f) { EachFieldOf(FieldRuns(t), f); }
    
    
    
    
    
    
    struct DefaultScope {
        TypeCheck &tc;
        DefaultScope(TypeCheck &t, FnSpec *env, Line callline);
        ~DefaultScope() { tc.frames.pop_back(); }
    };
    
    
    
    
    
    
    
    
    
    
    template<typename F> void EachDefaultInPlace(F f) {
        for (auto i = (int)frames.size() - 1; i > 0;) {
            auto &fr = frames[i];
            if (fr.isdefault) {
                f(fr);
                i--;
            } else if (fr.lexframe >= 0) i = min(fr.lexframe, i - 1);
            else break;
        }
    }
    Val CheckDefaultInit(Node *&n, TypeExpr *ft, TypeExpr *owner, const StructLit *sl,
                         const Field &field);
    Call *DefaultCall(TypeExpr *t, Line line);
    Node *DefaultValue(TypeExpr *t, Line line);
    SizeClass ClassOf(TypeExpr *t);
    bool IsFlat(TypeExpr *t);
    bool Reaches(TypeExpr *t, const function<bool(TypeExpr *)> &pred);
    bool HoldsIndexT(TypeExpr *t);
    SFunction *TextFn();
    void CheckfreeCallee(Node *at, SFunction *callee);
    bool HoldsPlainRef(TypeExpr *t);
    bool HasDefault(TypeExpr *t, string &why);

    
    
    
    bool ImageSafe(TypeExpr *t, string &why);
    bool VerifiableElem(TypeExpr *t, TypeExpr *elem, string &why);
    bool HoldsRelRef(TypeExpr *t);

    
    
    bool UninitOK(TypeExpr *t) { return ClassOf(t) == SC_FIXED; }

    
    
    
    
    
    
    
    

    TypeExpr *PendingArray(Line l);
    bool IsPendingArray(TypeExpr *t);
    static bool IsUntypedEmptyArray(const TypeExpr *t);
    void NoUntypedEmptyArray(const Val &v, Node *at, string_view use);
    TypeExpr *PendingElemFrom(const Val &av, Node *at);
    TypeExpr *PendingElemFromSeq(const Val &av, Node *at);
    void CompletePending(TypeExpr *arrt, TypeExpr *elem, Line l);
    
    
    map<TypeExpr *, VarDef *> pendingpools;
    void RequireComplete(TypeExpr *t, Line l);
    void ValidateType(TypeExpr *t, Line l, int pos);

    
    
    
    
    
    int typesbuilding = 0;
    
    
    
    vector<TypeExpr *> typechain;
    struct LaterPointee { TypeExpr *t; Line l; vector<TypeExpr *> chain; };
    vector<LaterPointee> laterpointees;
    void BeginTypeBuild() { typesbuilding++; }
    void EndTypeBuild();
    void ValidatePointee(TypeExpr *t, Line l);

    
    
    

    void PushScope(int kind, Node *node = nullptr);
    void PopScope();

    int CurDepth() { return (int)scopes.size(); }
    static int Depth(const VarDef *v) { return RootDepth(v); }
    
    static Roots RootsOf(VarDef *v) {
        Roots r;
        r.Set(v, true);
        return r;
    }

    
    
    
    
    
    
    VarDef *TempRoot() {
        auto t = ast.NewVarDef();
        t->name = "<temporary>";
        t->istemp = true;
        t->depth = CurDepth() + 1;
        return t;
    }
    static bool IsTemp(VarDef *v) { return v && v->istemp; }

    
    
    
    
    VarDef *RefRootOf(VarDef *vd) { return vd->refrootknown ? vd->ref.Root() : temproot; }
    
    
    
    
    Roots RefRootsOf(VarDef *vd) {
        if (BoundAnywhere(vd) && CurRealFrame().spec) return GlobalVarRead(vd);
        if (vd->refrootknown) return vd->ref;
        Roots r;
        if (UnboundIsBottom()) r.SetUnknown();
        else r.Set(temproot, false);
        return r;
    }

    bool ContainsGrowShrink(TypeExpr *t);
    TypeExpr *ResizableArrayIn(TypeExpr *t);
    bool IsGrowShrinkRoot(VarDef *r);
    bool GrowShrinkCanHold(VarDef *r, TypeExpr *of);
    bool BoundReachesGrowShrink(VarDef *r, TypeExpr *of, bool byteview);
    bool IntoGrowShrink(const Prov &v, VarDef *root, TypeExpr *t, bool holder);
    VarDef *GrowShrinkTaint(const Prov &p, TypeExpr *t, bool *reach = nullptr);
    VarDef *StoredIntoGrowShrink(const Val &v, const Roots &roots, TypeExpr *t, bool holder,
                                 bool *reach);
    string NeverStoredError(VarDef *root, bool may, bool reach);
    string ReachedThroughStr(VarDef *root);
    bool CycleStorable(VarDef *r);
    bool CycleStorable(const Roots &r);
    bool MayBeViewed(VarDef *r);
    bool Viewable(TypeExpr *t);
    void GrowShrinkElems(TypeExpr *t, vector<TypeExpr *> &out);
    bool GrowShrinkContains(TypeExpr *t, TypeExpr *of);
    bool RefMayPointInto(VarDef *v, VarDef *root, bool growonly = false,
                         TypeExpr *bound = nullptr);
    bool ClassReadMayPointInto(VarDef *cls, VarDef *root, TypeExpr *bound,
                               bool inplace = false);
    
    
    
    
    
    
    bool pathslive = false;
    struct PathsScope {
        TypeCheck &tc;
        bool saved;
        PathsScope(TypeCheck &t, bool on) : tc(t), saved(t.pathslive) { tc.pathslive = saved || on; }
        ~PathsScope() { tc.pathslive = saved; }
    };
    
    
    
    VarDef *shrunkclass = nullptr;
    TypeExpr *shrunkclasstype = nullptr;
    
    
    
    
    TypeExpr *ShrunkType(VarDef *root, TypeExpr *bound) {
        if (bound) return bound;
        if (root->type) return LoadType(root->type);
        return root == shrunkclass ? shrunkclasstype : nullptr;
    }
    
    
    bool rebuilding = false;
    bool ExposedLater(VarDef *v);
    bool CanHoldViewInto(TypeExpr *t, TypeExpr *arr);
    bool RefMayRetarget(VarDef *v, VarDef *root);
    bool SlotReadable(TypeExpr *t);
    bool HeldRefsMayPointInto(VarDef *v, const Prov &p, TypeExpr *t, VarDef *root,
                              TypeExpr *bound, bool growonly);
    Prov SlotView(const Prov &p, TypeExpr *slice);
    void BindProv(VarDef *vd, const Prov &p);
    void BindRefProvenance(VarDef *vd, const Val &v);
    bool BoundAnywhere(VarDef *vd);
    Prov GlobalVarRead(VarDef *vd);
    Prov RefProvOf(VarDef *vd);
    VarDef *ResetLocal(VarDef *previous);
    VarDef *RootComponent(VarDef *owner, TypeExpr *t, int field);
    
    
    bool NeedsCycleStack(TypeExpr *t) {
        return ClassOf(t) == SC_ROOT
                   ? AnyField(t, [&](TypeExpr *ft) { return ClassOf(ft) != SC_FIXED; })
                   : ClassOf(t) != SC_FIXED;
    }
    Roots ProjectRoots(const Roots &parents, TypeExpr *t, int field);
    bool DifferentComponents(VarDef *a, bool within_a, VarDef *b, bool within_b) {
        return within_a && within_b && a && b && a->componentbase && b->componentbase &&
               a->componentfield != b->componentfield && TypeEq(a->componentbase, b->componentbase);
    }
    bool ProjectedFrom(VarDef *root, VarDef *base) {
        for (auto r = root; r; r = r->componentowner) if (r == base) return true;
        return false;
    }
    int ClassParam(FnSpec *spec, VarDef *root, bool &view);
    Roots ProjectClass(VarDef *root, VarDef *base, const Roots &args);
    Roots MapClass(FnSpec *spec, VarDef *root, const vector<Val> &args);
    VarDef *NewVar(string_view name, TypeExpr *type, Line l, bool isvar,
                   VarDef *previous = nullptr);
    VarDef *LookupVar(string_view name, string_view ns, Node *use = nullptr);
    const char *ScopeNameKind(string_view name);

    
    
    
    
    
    
    template <typename F> bool ForOuterVars(int fi, F f) {
        for (;;) {
            auto &fr = frames[fi];
            if (fr.decl) {
                for (auto v : fr.decl->vars) if (f(v)) return true;
                return false;
            }
            auto p = fr.lexframe;
            if (p < 0) return false;
            for (auto i = frames[p + 1].varbase - 1; i >= frames[p].varbase; i--)
                if (f(vars[i])) return true;
            fi = p;
        }
    }

    
    
    
    template <typename F> bool ForOuterFns(int fi, F f) {
        for (;;) {
            auto &fr = frames[fi];
            if (fr.decl) {
                for (auto &o : fr.decl->fns) if (f(o)) return true;
                return false;
            }
            auto p = fr.lexframe;
            if (p < 0) return false;
            for (auto i = (int)localfns.size() - 1; i >= 0; i--) {
                auto [si, sf] = localfns[i];
                if (si >= frames[p].scopebase && si < frames[p + 1].scopebase &&
                    f(OuterFn { sf, frames[p].lexspec, scopes[si].serial, i }))
                    return true;
            }
            fi = p;
        }
    }

    bool InScope(VarDef *v, int idx) { return idx < (int)vars.size() && vars[idx] == v; }
    void DeclareLocalFn(FnDecl *fd);
    int FrameOfSpec(FnSpec *sp);
    int LexFrame(FnSpec *env);
    int LexFrame(FnSpec *env, SFunction *sf);
    int LexFrame(const FnValBind &fb);
    int FrameOfScope(int s);
    bool NamesFrame(int fi, int target);
    FnSpec *NamedSpec(FnSpec *env);
    bool LookupLocalFn(string_view name);

    FlowState SaveFlow();
    void RestoreFlow(const FlowState &f);
    
    
    
    
    small_vector<int, 8> NamedFrames(int fi, FnSpec *spec);
    
    template<typename F> void EachNamedVar(int fi, FnSpec *spec, F f) {
        for (auto k : NamedFrames(fi, spec)) {
            auto end = k + 1 < (int)frames.size() ? frames[k + 1].varbase : (int)vars.size();
            for (auto i = frames[k].varbase; i < end; i++) f(i);
        }
    }
    void MergeFlow(const FlowState &a, const FlowState &b);
    void NarrowCond(Node *cond, bool sense);

    void KillNarrow(VarDef *vd) { vd->narrowed = nullptr; }
    
    void NoteNarrowed(VarDef *vd) {
        if (!vd->isglobal) return;
        auto it = std::lower_bound(narrowedglobals.begin(), narrowedglobals.end(), vd,
                                   [](VarDef *a, VarDef *b) { return a->globalindex < b->globalindex; });
        if (it == narrowedglobals.end() || *it != vd) narrowedglobals.insert(it, vd);
    }
    
    
    
    void SetNarrowed(VarDef *vd, TypeExpr *t) {
        NoteNarrowed(vd);
        vd->narrowed = t;
    }
    
    
    
    
    FlowState JoinFlow(const FlowState &a, const FlowState &b);
    bool SameFlow(const FlowState &a, const FlowState &b);

    void ApplyCalleeRebinds(FnSpec *spec);
    
    
    
    
    
    static EnvRead EnvReadOf(VarDef *vd);
    static bool EnvIs(const VarDef *vd, const EnvRead &r);
    void NoteEnvRead(VarDef *vd);
    void NoteCalleeEnvReads(FnSpec *callee);
    bool EnvUnchanged(const FnSpec *spec);
    void ShareCycleEnvReads(FnSpec *head);
    void RecordEnvExits(FnSpec *spec);
    void ReplayEnvExits(FnSpec *spec);
    
    
    
    
    
    
    void NoteExit(int tf, const OuterExit *added = nullptr);
    void ReplayOuterExits(FnSpec *spec);

    
    
    
    
    
    
    
    
    
    
    
    
    
    struct LoopPass {
        int scopeidx = 0;          
        size_t firstbase = 0;      
        size_t eventbase = 0;      
        bool settled = false;
        bool changed = false;      
        bool sawunbound = false;   
        bool deferred = false;     
    };
    bool InDiscovery() {
        for (auto &lp : cur.looppasses) if (!lp.settled) return true;
        return false;
    }
    
    bool UnboundIsBottom() {
        if (!InDiscovery()) return false;
        cur.looppasses.back().sawunbound = true;
        return true;
    }
    
    
    bool VerdictDeferred() {
        if (!InDiscovery()) return false;
        for (auto &lp : cur.looppasses) if (!lp.settled) lp.deferred = true;
        return true;
    }
    
    
    
    
    
    
    
    
    void NoteFact(const VarDef *vd, int floor = 0) {
        auto mark = [&](BodyState &b) {
            for (auto &lp : b.looppasses)
                if (Depth(vd) <= lp.scopeidx && lp.scopeidx >= floor) lp.changed = true;
        };
        mark(cur);
        for (auto b : outerbodies) mark(*b);
        for (auto &r : cyclerounds)
            if (Depth(vd) <= r.scopebase && r.scopebase >= floor) r.changed = true;
    }
    
    
    
    
    
    int ActivationFloor(VarDef *root) {
        if (!IsClassRoot(root)) return 0;
        while (root->componentowner) root = root->componentowner;
        for (auto fi = (int)frames.size() - 1; fi >= 0; fi--) {
            auto &f = frames[fi];
            if (f.isfunval || !f.spec) continue;
            auto &crs = f.spec->classroots;
            if (find(crs.begin(), crs.end(), root) != crs.end()) return f.scopebase;
        }
        return 0;
    }
    
    
    
    
    
    struct CycleRound {
        int scopebase = 0;
        bool changed = false;
    };
    vector<CycleRound> cyclerounds;
    
    
    
    
    size_t LiveEventBase(const VarDef *holder) {
        for (auto i = cur.looppasses.size(); i-- > 0;)
            if (Depth(holder) > cur.looppasses[i].scopeidx) return cur.looppasses[i].eventbase;
        return 0;
    }
    
    
    bool CarriedEvent(size_t i) {
        for (auto &lp : cur.looppasses) if (lp.firstbase <= i && i < lp.eventbase) return true;
        return false;
    }

    
    

    TypeExpr *NarrowedRef(TypeExpr *t, Line l);
    TypeExpr *GrowU8Array(Line l);

    TypeExpr *LoadType(TypeExpr *t);
    bool LoadedTypeEq(TypeExpr *t, TypeExpr *of);
    TypeExpr *ValueType(TypeExpr *t);
    TypeExpr *DeclType(TypeExpr *t, TypeExpr *s);
    static bool ImplicitInt(IntStorage from, IntStorage to);
    Val CheckIntAny(Node *n);
    bool HasRelRefT(TypeExpr *t, bool inpool = false);
    bool BuiltWhereReceived(Node *n, size_t result);
    void NoRelRefCopy(Node *n, TypeExpr *t, size_t result = 0, const VarDecl *bind = nullptr,
                      const Val *src = nullptr);
    void NoRelRefTempRead(Node *base, TypeExpr *t, TypeExpr *member);
    
    
    Node *retvalnode = nullptr;
    struct RetValScope {
        TypeCheck &tc;
        Node *saved;
        RetValScope(TypeCheck &t, Node *n) : tc(t), saved(t.retvalnode) { tc.retvalnode = n; }
        ~RetValScope() { tc.retvalnode = saved; }
    };
    
    
    
    struct NamedResultUse {
        FnSpec *spec;
        Node *at;
        VarDef *var;
    };
    vector<NamedResultUse> namedresultuses;
    bool AllowNamedResult(Ident *id, TypeExpr *t);
    void CheckNamedResultUses(FnSpec *spec);
    VarDef *NamedResultOf(FnSpec *spec);
    void SelfRelPointees(TypeExpr *t, vector<TypeExpr *> &out);
    bool NamedResultKeepsLinks(TypeExpr *t);

    
    
    
    

    void ResolvePools();
    void ResolveIndexTypes();
    VarDef *NamedArrayComponent(VarDef *global, string_view name, Line line);
    void ValidateNamedComponents();
    
    
    map<pair<VarDef *, string_view>, VarDef *> namedcomponents;
    void ValidateIndexType(TypeExpr *t);
    
    set<VarDef *> indexarrays;

    
    
    
    set<VarDef *> poolglobals;
    set<VarDef *> poolowners;   

    VarDef *PoolOf(VarDef *r);
    void ValidatePool(TypeExpr *t);

    
    
    
    
    
    
    
    
    
    
    
    
    

    template<typename F> bool StableTypeQuery(TypePairCache &cache, TypeExpr *t, TypeExpr *of,
                                              F walk);
    bool CanContain(TypeExpr *t, TypeExpr *of);
    bool CanContainWalk(TypeExpr *t, TypeExpr *of);
    bool MayHoldValueOf(TypeExpr *t, TypeExpr *of);
    bool MaySameType(TypeExpr *a, TypeExpr *b);
    void ReachedThroughRefs(TypeExpr *t, vector<TypeExpr *> &out);
    bool ReachesThroughRefs(TypeExpr *t, TypeExpr *of);
    void BoundReach(VarDef *r, vector<TypeExpr *> &out);
    TypeExpr *PointeeOf(TypeExpr *t);
    void LexicalVars(int fi, vector<bool> *seen, const function<void(VarDef *)> &f);
    void VisibleVars(const function<void(VarDef *)> &f);
    void ShrinkScanVars(const function<void(VarDef *)> &f);
    bool StaticCanContain(TypeExpr *of);
    
    
    Roots RootCandidates(TypeExpr *of, int d, bool globalsonly, bool writable,
                         bool slots = false);

    bool TempContents(const Val &v, ReadBack &contents);
    
    
    
    Roots ReadBackRoot(TypeExpr *rt, const Roots &container, bool byteview = false,
                       const ReadBack *contents = nullptr, bool slotread = false,
                       bool inplace = false);
    bool ContentsReadBack(VarDef *h, TypeExpr *of, Roots &out);
    string ReadBackWhy(const Roots &r);
    bool HoldsByValue(TypeExpr *t, TypeExpr *of, vector<TypeExpr *> &open);
    int ElemArrayPlaces(TypeExpr *t, TypeExpr *of);
    bool OneArrayOf(VarDef *r, TypeExpr *elem);
    void ArrayElemsReached(TypeExpr *t, vector<TypeExpr *> &out, vector<TypeExpr *> &open);
    void AppendArrayElemsReached(TypeExpr *t, vector<TypeExpr *> &out);
    bool RootedAtReceiver(const Val &rv, const Val &av, TypeExpr *elem);
    string LibraryParamArg(VarDef *root);
    void CheckRootedAtReceiver(Call *c, const char *op, const Val &rv, const Val &av, TypeExpr *elem,
                               const char *what, const char *sec);

    
    

    LVal CheckLValue(Node *n);
    LVal LValueBase(Node *n, bool cmpview = false);
    void NoTemporaryLiteral(Node *n, TypeExpr *t);
    void DerefLValue(LVal &lv, Node *at);
    void RequireAssigned(VarDef *vd, Node *at, string_view use = {});
    void RequireIndexArray(TypeExpr *t, Node *at, string_view what);
    void SliceProvenance(LVal &lv, Node *at);
    void ReadBackLVal(LVal &lv);
    bool SlotLoadWritable(TypeExpr *t, bool path);
    Val ContainerRead(LVal lv);
    void CheckAccessorOwner(Node *at, TypeExpr *owner, TypeExpr *t, StructLit *lit = nullptr);
    void ResolveMemberLValue(LVal &lv, Dot *d);
    
    
    
    
    
    Roots ContentsOf(const Val &v) {
        if (v.holderset) return v.contents;
        if (v.type && !IsRefOrSlice(v.type) && !HoldsPlainRef(v.type)) return Roots {};
        return !v.type || IsRefOrSlice(v.type) ? v.AsRoots() : Bounds(v);
    }
    
    
    
    
    
    
    static Roots Bounds(const Roots &at) {
        Roots r = at;
        r.Weaken();
        for (auto &a : r.alts) a.from = nullptr;
        return r;
    }
    
    
    
    static VarDef *HolderSource(const Roots &at) {
        return at.Exact() && !IsTemp(at.alts[0].root) ? at.alts[0].root : nullptr;
    }
    
    
    
    
    static Roots HeldAt(const Roots &at, bool keepfrom = false) {
        auto src = HolderSource(at);
        if (src && src->heldholder) return src->heldholder->contents;
        if (src && src->type && !src->isglobal && !IsRefOrSlice(src->type))
            return src->contents;
        if (!keepfrom) return Bounds(at);
        Roots r = at;
        r.Weaken();
        return r;
    }
    
    
    
    
    
    void MarkClassCopy(Val &v) {
        if (!IsClassRoot(v.holderfrom)) return;
        for (auto &a : v.contents.alts)
            if (a.root == v.holderfrom) a.classread = true;
    }
    
    
    
    
    
    
    
    
    
    
    void AddContents(VarDef *x, const RootAlt &a, bool classread, const VarDef *src) {
        auto held = a.classread || (classread && src == a.root);
        if (x->contents.Add({ a.root, a.exact, a.from, a.slotread, held }))
            NoteFact(x, ActivationFloor(a.root));
    }
    
    
    
    
    
    
    bool AddBinding(VarDef *vd, const Roots &rv) {
        auto changed = rv.unknown && !vd->ref.unknown;
        vd->ref.unknown = vd->ref.unknown || rv.unknown;
        auto floor = changed ? 0 : (int)scopes.size();
        for (auto &a : rv.alts) {
            if (!vd->ref.Add(a)) continue;
            changed = true;
            floor = min(floor, ActivationFloor(a.root));
        }
        if (changed) NoteFact(vd, floor);
        return changed;
    }
    
    
    
    
    
    
    static VarDef *StoreSource(VarDef *src) {
        if (!src || IsTemp(src)) return nullptr;
        if (src->type && IsRefOrSlice(src->type) && !src->isglobal) return nullptr;
        return src;
    }
    bool ShrinkMayFree(VarDef *root, TypeExpr *bound, bool growonly, TypeExpr *of,
                       bool byteview);
    bool ShrinkMayMove(VarDef *root, TypeExpr *bound, TypeExpr *t);
    void CheckHeldShrinks(Node *at, const string &op, VarDef *root,
                          const string &what, bool growonly, TypeExpr *bound = nullptr);

    
    

    
    
    
    Val CheckV(Node *n, TypeExpr *expected) {
        NodeScope ns(*this, n);
        auto cyclecalls = frames.back().cyclecalls;
        auto v = n->Check(*this, expected);
        if (v.type == fntype && !Is<Ident>(n) && !Is<FunVal>(n))
            Error(n, "a function value must be a function name or block literal (§7.6); "
                     "evaluate runtime expressions separately");
        NoteTemp(n, v, cyclecalls);
        RecordVal(n, v);
        
        
        if (!ast.unknowntypes.empty() && v.type && MentionsUnknown(v.type)) throw Abandoned {};
        return v;
    }

    
    
    bool MentionsUnknown(const TypeExpr *t) {
        auto any = [&](const vector<TypeExpr *> &ts) {
            for (auto a : ts) if (a && MentionsUnknown(a)) return true;
            return false;
        };
        switch (t->kind) {
            case TY_STRUCT: return t->struc->st->unknown || any(t->struc->args);
            case TY_ENUM: return any(t->enu->args);
            case TY_ARRAY: return MentionsUnknown(t->arr->sub);
            case TY_SLICE: return MentionsUnknown(t->sub);
            case TY_REF: return MentionsUnknown(t->ref->sub);
            case TY_VARIANT: return MentionsUnknown(t->var->adt);
            case TY_FN: return any(t->fn->args) || any(t->fn->rets);
            default: return false;
        }
    }

    
    
    
    static bool LacksByError(const TypeExpr *t) {
        switch (t->kind) {
            case TY_STRUCT: return t->struc->st->broken;
            case TY_ENUM: return t->enu->en->broken;
            case TY_VARIANT: return t->var->adt->kind == TY_ENUM && t->var->adt->enu->en->broken;
            default: return false;
        }
    }

    Val DecayRef(const Val &v);
    
    TypeExpr *DecayedType(const Val &v) {
        return IsPlainRef(v.type) ? LoadType(v.type->ref->sub) : v.type;
    }
    bool KeepsRef(const Val &v, TypeExpr *dt);
    Node *AutoRef(Node *n, Val &v, bool writes = true);
    void SlotRoots(Val &v);
    void NoteWritableRef(VarDef *d, Node *at);
    VarDef *RelyOnNonneg(const Val &v, Node *at);
    void RelyOnConstant(ConstUse &use, VarDef *d);
    void RelyOnNamed(const Val &v, Node *at);
    bool TypeFixesExtent(VarDef *d, bool cap);
    void RelyOnExtent(ConstUse &use, VarDef *d, bool cap);
    void CheckVarExtent(VarDef *d, Node *at);
    TypeExpr *StorageType(const Val &v);
    bool BindsRef(const Val &v, TypeExpr *dt);
    bool IsNonFixedLValue(const Val &v);
    bool IsNonFixedRef(const Val &v);
    bool Referenceable(Node *n, const Val &v);
    bool FieldsInFrame(Dot *d);
    [[noreturn]] void NoResizableRef(Node *at);
    bool UserRefOf(Node *n);
    bool IsOwnLocal(Node *n);
    bool ImplicitCopy(const Val &v, Node *n, TypeExpr *dt);
    [[noreturn]] void ImplicitCopyError(Node *n);
    void RequireCopyable(const Val &v, Node *n, TypeExpr *dt) {
        if (ClassOf(dt) == SC_ROOT && (v.lvalue || IsPlainRef(v.type)))
            Error(n, "root owners cannot be copied; bind a reference instead");
        if (ImplicitCopy(v, n, dt)) ImplicitCopyError(n);
    }
    void CheckBranchCopy(const Val &v, Node *n, TypeExpr *dt, Val &out);
    void RefCopyWarning(Node *n);
    
    
    void RefCopyWarnings(const Val &v) {
        for (auto n : v.refcopies) RefCopyWarning(n);
    }
    
    
    
    
    
    
    Node *argpath = nullptr;
    struct PathScope {
        TypeCheck &tc;
        Node *saved;
        PathScope(TypeCheck &t, Node *n) : tc(t), saved(t.argpath) { tc.argpath = n; }
        ~PathScope() { tc.argpath = saved; }
    };
    
    
    
    
    
    Node *joinpath = nullptr;
    struct JoinPathScope {
        TypeCheck &tc;
        Node *saved;
        JoinPathScope(TypeCheck &t, Node *n) : tc(t), saved(t.joinpath) { tc.joinpath = n; }
        ~JoinPathScope() { tc.joinpath = saved; }
    };
    
    
    static bool CopiesBranch(TypeExpr *dest) { return !dest || dest->kind == TY_VOID; }
    void BindBranchesByRef(vector<Node *> &argnodes, vector<Val> &argvals,
                           const vector<TypeExpr *> &paramtypes, SFunction *sf, int skip = -1,
                           size_t end = SIZE_MAX);
    void BindBranchByRef(Node *&n, Val &v, TypeExpr *pt, TypeExpr *declared);
    Val CheckInferredResult(Node *&n, FnSpec *tspec);
    
    
    
    
    
    void UnrefForValueParam(Node *&a, TypeExpr *pt) {
        if (pt->kind != TY_SLICE && (pt->kind == TY_REF || ClassOf(pt) != SC_FIXED)) return;
        if (auto u = Is<Unary>(a); u && u->synth) a = u->child;
    }
    Val CheckValue(Node *&n, TypeExpr *expected, bool callsite = false, bool branchcopy = false,
                   bool inferred = false);

    Val CheckArg(Node *&n, TypeExpr *expected) {
        SlotScope ss(*this, false);
        return CheckValue(n, expected, true);
    }

    Val CheckValueAt(Node *&n, TypeExpr *expected, Dest d, bool callsite = false);
    Val Operand(Node *n);

    string fitfail;  

    void MustFit(Val &v, Node *n, TypeExpr *dt);
    void CheckArrayCount(Node *n, TypeExpr *t, int64_t count) {
        if (!t || t->kind != TY_ARRAY) return;
        auto a = t->arr;
        auto ls = a->akind == A_VAR ? (a->lenstorage < 0 ? IS_U32 : (IntStorage)a->lenstorage)
                  : a->akind == A_LIMITED && !a->sizeexpr ? IS_U32 : IS_U64;
        if (ls != IS_VARINT && IntBits(ls) < 64 &&
            (uint64_t)count >= (1ull << IntBits(ls)))
            Error(n, cat("array length ", count, " exceeds storage range of ", TypeStr(t)));
    }
    bool FitsAt(Val &v, TypeExpr *dt);
    static string ConstStr(const Val &v);
    Val CheckCond(Node *n);
    TypeExpr *UnifyBranch(TypeExpr *a, TypeExpr *b, Node *at, bool wantvalue);
    Val VoidVal();
    
    
    
    
    
    Val VoidIfBottom(Val v) {
        if (!v.type) v.type = ast.voidtype;
        return v;
    }
    Val CheckRefOf(Unary *x);
    void FoldInt(TType op, Val &l, Val &r, Val &out, Node *at);
    TypeExpr *UnifyNumeric(Node *at, TType op, Val &lv, Val &rv, TypeExpr *lt, TypeExpr *rt,
                           bool cmp = false);
    bool unifytrial = false;   
    void RetypeOperand(Node *&n, Val &v, TypeExpr *ct);
    void RetypeOperands(Node *&left, Node *&right, Val &lv, Val &rv, TypeExpr *ct);
    void RetypeVal(Val &v, TypeExpr *ct);
    Val CheckRefIdentity(Binary *b);
    Val CheckLogical(Binary *b);
    Val CheckBinaryResult(Binary *b, Val &lv, Val &rv);
    Val NumericBinary(Binary *b, Val lv, Val rv, TypeExpr *&ct, bool trial);
    void DecidedComparison(Binary *b, const Val &lv, const Val &rv, TypeExpr *lt, TypeExpr *rt);
    Val NumericUnary(Unary *u, const Val &v, bool trial);
    
    
    static bool LitFloat(const Val &v) {
        return v.type && v.type->kind == TY_FLT && (v.ck == CK_FLT || v.unsized || v.litfloat);
    }
    
    
    
    static bool IntConsts(const Val &v, int64_t &lo, int64_t &hi) {
        if (v.ck == CK_INT && !v.uns) { lo = hi = v.ival; return true; }
        if (v.litint) { lo = v.litlo; hi = v.lithi; return true; }
        return false;
    }
    static bool ConstsFit(const Val &v, IntStorage s) {
        int64_t lo, hi;
        return IntConsts(v, lo, hi) && FitsIntStorage(lo, false, s) && FitsIntStorage(hi, false, s);
    }
    
    
    static bool FlexConsts(const Val &v, int64_t &lo, int64_t &hi) {
        if (v.flexint) { lo = v.litlo; hi = v.lithi; return true; }
        return IntConsts(v, lo, hi);
    }
    static bool FlexFits(const Val &v, IntStorage s) {
        return FitsIntStorage(v.litlo, false, s) && FitsIntStorage(v.lithi, false, s);
    }
    string ConstsNoFit(const Val &v, TypeExpr *t) {
        if (v.notconst)
            return cat("~", v.litlo, " does not fit ", TypeStr(t), ": ", v.litlo, " does not");
        if (v.flexint && v.litlo == v.lithi)
            return cat("constant ", v.litlo, " does not fit ", TypeStr(t));
        if (v.flexint)
            return cat("the constants ", v.litlo, " to ", v.lithi, " it is computed from do not "
                       "all fit ", TypeStr(t));
        if (!v.litint) return cat("constant ", ConstStr(v), " does not fit ", TypeStr(t));
        return cat("the branches' constants ", v.litlo, " to ", v.lithi, " do not all fit ",
                   TypeStr(t));
    }
    static void IntToFloat(Val &v, TypeExpr *ft);
    void ToFloat(Node *&n, TypeExpr *from, TypeExpr *ft);
    void RetypeFlex(Node *&n, TypeExpr *t);
    void RetypeFlexInt(Node *n, TypeExpr *t);
    void RetypeBranch(Node *&n, TypeExpr *t);
    void RetypeBranches(Node *x, TypeExpr *t);

    
    
    
    
    
    struct CastAlt {
        AsCast *cast = nullptr;
        Val operand;          
        Val raw;              
        Val alt;              
        
        
        bool pending = false;
        
        
        
        TypeExpr *settle = nullptr;
        TypeExpr *reach = nullptr;   
    };
    unordered_map<Node *, CastAlt> castalts;
    
    
    
    
    
    unordered_set<Node *> casttyped;
    
    
    unordered_set<Node *> builtinfallback;
    vector<Warning> castverdicts;   
    void ForgetCastAlt(Node *n) {
        castalts.erase(n);
        casttyped.erase(n);
    }
    bool InGenericCode();
    bool Converts(const Val &v, TypeExpr *t);
    
    
    static bool SameIndex(const TypeExpr *a, const TypeExpr *b) {
        if (!a->ix || !b->ix) return !a->ix && !b->ix;
        return a->ix->arr == b->ix->arr && a->ix->upto == b->ix->upto;
    }
    static bool SameNum(TypeExpr *a, TypeExpr *b) {
        if (!a || !b || a->kind != b->kind) return false;
        if (a->kind == TY_INT) return a->intstorage == b->intstorage && SameIndex(a, b);
        return a->kind == TY_FLT && a->fltstorage == b->fltstorage;
    }
    bool SameNumVal(const Val &a, const Val &b);
    bool SameReach(const CastAlt &a, TypeExpr *at, bool typed);
    bool Reaches(const CastAlt &a, TypeExpr *at, bool typed);
    void NoteCast(AsCast *x, const Val &raw, const Val &cv, const Val &v);
    void FollowCast(Node *n, const CastAlt &a, const Val &tv, const Val &v, TypeExpr *at);
    void JudgeBinaryCasts(Binary *b, Node *l, Node *r, const Val &lv, const Val &rv, const Val &v,
                          TypeExpr *ct);
    void JudgeUnaryCast(Unary *u, Node *c, const Val &r);
    void JudgeCastAt(Node *n, TypeExpr *dt, const Val &v);
    string CastReason(const CastAlt &a, TypeExpr *reach, bool bycast = false);
    void CastVerdict(const CastAlt &a, const string &reason, const AsCast *dependson = nullptr);
    void ReportRedundantCasts();
    bool ElementwiseOK(TypeExpr *t);
    TypeExpr *ElementwiseScalarType(TypeExpr *t);
    TypeExpr *UnsignedLeaf(TypeExpr *t);
    
    Val CheckVariantConst(Dot *d, SEnum *en, TypeExpr *written = nullptr);
    Val MergeVals(const Val &a, bool areach, const Val &b, bool breach, Node *at, bool wantvalue);
    Val JoinBranches(const Val &a, bool areach, const Val &b, bool breach, Node *at,
                     bool wantvalue, bool onjoin);
    template<typename F> Val CheckJoin(Node *x, TypeExpr *expected, F check);
    
    
    
    void NoArrayJoin(const Val &v) {
        if (v.joinslice && !v.joinhasslice)
            Error(v.joinat, cat("branches have mismatched types: ", TypeStr(v.joina), " vs ",
                                TypeStr(v.joinb)));
    }
    
    
    
    
    static bool ViewedWhole(Node *arg, const Val &v, TypeExpr *pt) {
        return pt->kind == TY_SLICE &&
               (v.emptyarr ||
                ((v.type->kind == TY_ARRAY || v.joinslice) && IsBranchConstruct(arg)));
    }
    void CheckBranchRoot(const Val &v, int depth, Node *at, const char *construct);
    Val TempCopy(Val v);
    Node *WholeSlice(Node *n);
    Val CheckIf(IfExpr *x, TypeExpr *expected, bool wantvalue);
    Val CheckBlockVal(Block *b, TypeExpr *expected, bool wantvalue);
    Val CheckMatch(MatchExpr *m, TypeExpr *expected, bool wantvalue);
    Val CheckEarlyBlock(EarlyBlock *x, TypeExpr *expected, bool wantvalue);
    void CheckLoopBody(Block *body);
    Scope CheckLoopPasses(Node *x, FlowState &head, const function<void()> &pass);
    Val CheckLoop(LoopExpr *x, TypeExpr *expected, bool wantvalue);
    void CheckWhile(While *x);
    void CheckFor(ForLoop *x);
    int RealFrameIndex();
    Frame &CurRealFrame() { return frames[RealFrameIndex()]; }
    int FindBreakScope(bool forcontinue);
    void CheckBreak(Break *b);
    [[noreturn]] void JumpLeavesDefer(Node *at, const char *what);
    
    
    void NoteBreak(int si);
    void JoinBreakFlow(const Scope &sc, bool ownexit);
    void CheckContinue(Node *n);

    
    
    

    
    vector<Val> lastcallrets;

    struct MatchInfo {
        
        
        static constexpr int INTTOFLOAT = 3;
        SFunction *sf = nullptr;
        int tier = 0;
        vector<pair<string_view, TypeExpr *>> bindings;
        vector<pair<string_view, FnValBind>> fnvals;
        vector<TypeExpr *> paramtypes;  
        vector<int> litparams;          
        FnSpec *env = nullptr;          
        
        
        size_t nwritten = 0;
    };

    
    
    
    
    struct LitCheck {
        FnSpec *spec = nullptr;
        int param = 0;
        Val lit;
        Node *at = nullptr;
    };
    vector<LitCheck> litchecks;
    bool litrecord = true;   
    void RecordLitAdapt(const Val &v, TypeExpr *t, Line at);
    void NoteLitArgs(FnSpec *spec, vector<Val> &argvals, Node *at);
    void VerifyLiterals();

    
    
    
    unordered_set<Node *> contextualfloats;
    void ContextualFloatCall(Call *c, vector<SFunction *> &cands, FnSpec *env,
                             vector<Node *> &argnodes, vector<Val> &argvals,
                             MatchInfo &best, TypeExpr *expected, string_view name);
    Val CheckCall(Call *c, TypeExpr *expected);
    Val CheckNamedCall(Call *c, Ident *id, TypeExpr *expected);
    vector<SFunction *> LookupLocalFns(string_view name, FnSpec *&env);
    FnValBind LocalFnValue(const vector<SFunction *> &fns, FnSpec *env);
    Val CheckUfcsCall(Call *c, Dot *d, TypeExpr *expected);
    Val ResolveCall(Call *c, vector<SFunction *> &cands, FnSpec *env, string_view name, Val *preval,
                    Node *&prenode, bool entryreach, bool *nomatch = nullptr,
                    TypeExpr *expected = nullptr);
    void MatchCandidates(Call *c, vector<SFunction *> &cands, FnSpec *env, vector<Val> &argvals,
                         vector<MatchInfo> &tied, vector<MatchInfo> &converting, string *failures,
                         string_view name);
    bool SameMatch(MatchInfo &a, MatchInfo &b);
    void JudgeCallCasts(vector<SFunction *> &cands, MatchInfo &best, vector<Node *> &argnodes,
                        vector<Val> &argvals, const function<bool(vector<Val> &)> &resolves);
    void RecheckArgs(Call *c, vector<SFunction *> &cands, MatchInfo &mi, FnSpec *denv,
                     vector<Node *> &argnodes, int skip, const function<void()> &atskip,
                     bool entryreach);
    
    
    
    
    
    unordered_set<Node *> phase2waits;
    Node *nowait = nullptr;
    bool Phase2Waits(Call *c) {
        auto &p = cur.nodepath;
        return c != nowait && p.size() > 1 && p.back().node == c && p[p.size() - 2].discovering;
    }
    void CheckWaitingArg(Call *c, Node *a, bool entryreach);
    void CalleesReturn(Call *c, const vector<FnSpec *> &specs, bool argsreach);
    
    
    
    bool TryMatch(SFunction *sf, Call *c, vector<Val> &argvals, MatchInfo &mi, string &why,
                  bool defaults = false);
    FnSpec *ParamDefaultEnv(const MatchInfo &mi);
    void AddParamDefaults(Call *c, MatchInfo &best, vector<Node *> &argnodes,
                          vector<Val> &argvals, bool receiver, FnSpec *env);
    template<typename F> void InParamDefault(Call *c, const MatchInfo &mi, FnSpec *env,
                                             size_t param, F f);
    void DefaultScopeName(string_view name, Node *at, bool fnonly);
    void RefArg(Node *&a, Val &v);
    TypeExpr *NaturalType(const Val &av);
    TypeExpr *UnifyArg(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b, int &tier);
    TypeExpr *UnifyArgRaw(TypeExpr *pt, Val &av, vector<pair<string_view, TypeExpr *>> &b,
                          int &tier);
    static TypeExpr *ArrayLeaf(TypeExpr *t) {
        while (t->kind == TY_ARRAY) t = t->arr->sub;
        return t;
    }
    TypeExpr *LitElemsAt(const Val &av, TypeExpr *dt, bool &tofloat, string *why = nullptr);
    bool HasGenerics(TypeExpr *t);
    bool HasOpaque(TypeExpr *t);
    bool BindTypes(TypeExpr *pt, TypeExpr *at, vector<pair<string_view, TypeExpr *>> &b);
    TypeExpr *SubstOwn(TypeExpr *pt, vector<pair<string_view, TypeExpr *>> &b);
    Val TryDispatch(Call *c, vector<SFunction *> &cands, FnSpec *env, vector<Node *> &argnodes,
                    vector<Val> &argvals, string_view name, bool entryreach);

    
    
    

    
    
    
    
    
    static constexpr int MAXNESTEDSPECS = 16;
    FnSpec *GetOrCreateSpec(MatchInfo &mi, vector<Val> &argvals, Node *callnode);
    void LoadSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes);
    void RefSliceArgs(vector<Val> &argvals, const vector<TypeExpr *> &ptypes, Line at);
    void NoteHeld(Val &av, TypeExpr *slice);
    bool HasView(Val &av, TypeExpr *pt, int reach, SFunction *sf);
    
    
    static VarDef *ViewClassOf(const FnSpec *spec, size_t p) {
        if (p >= spec->views.size() || spec->views[p].cls <= 0 ||
            spec->views[p].cls >= (int)spec->classroots.size())
            return nullptr;
        return spec->classroots[spec->views[p].cls];
    }
    void ValidateCycle(FnSpec *spec, Node *callnode);
    static FnSpec *CycleHead(FnSpec *s);
    void JoinCycle(FnSpec *spec, Node *callnode);
    void NoInferredRefResult(FnSpec *spec, Line at);
    
    
    
    
    
    
    
    string_view FrameFnName(int fi);
    static VarDef *UltimateRoot(VarDef *v);
    void ValidatePoolArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode);
    
    
    
    
    
    
    
    
    
    
    struct ThreadedClass {
        bool broken = false;
        string why;                  
        
        
        vector<VarDef *> heirs;
        
        
        
        
        
        bool bounded = false;
    };
    unordered_map<VarDef *, ThreadedClass> threadedclasses;
    size_t threadbounds = 0;   
    void ValidateThreadArgs(FnSpec *spec, vector<Val> &argvals, Node *callnode);
    bool PointsOutside(VarDef *pr, const Roots &v, vector<VarDef *> &through);
    void BoundThread(VarDef *cls, const string &why);
    void NoteThreadedClass(VarDef *cls);
    bool ThreadedChain(VarDef *r);
    bool ThreadStorable(VarDef *r);
    void Unthread(VarDef *cls, const string &why);
    string CycleStoreError(VarDef *root, const string &more = {});
    template<typename F> void NeedOnPath(FnSpec *spec, FnSpec *t, Node *at, int n, F step);
    void ValidateNeeds(FnSpec *spec, Node *callnode);
    void AddNeed(FnSpec *s, FnSpec *t);
    
    
    
    static const FnRecord *RecordOf(const FnSpec *spec) {
        return spec->inprogress ? spec->prev.get() : &spec->record;
    }
    void CheckSpecBodyOnce(FnSpec *spec, vector<Val> *argvals, Line callline,
                           Call *call = nullptr);
    bool SameRecord(const FnRecord &a, const FnRecord &b, const FnSpec *spec);
    bool ExternValueOk(TypeExpr *t, string &why);
    bool ExternParamOk(TypeExpr *t, string &why);
    bool DylibValueOk(SFunction *sf, TypeExpr *t, string &why);
    void CheckExternSpec(FnSpec *spec);
    void CheckExport(SFunction *sf);
    int ClassDepth(VarDef *r);
    vector<VarDef *> NarrowedOptionals(const MatchInfo &mi);
    int EnvReach(const MatchInfo &mi);
    void CheckSpecBody(FnSpec *spec, vector<Val> *argvals, Line callline, Call *call = nullptr);
    void RecordReturn(FnSpec *tspec, vector<Val> &vals, Node *at);
    void NamedResultCopyWarning(FnSpec *spec);
    Val RetAltVal(FnSpec *spec, const RootAlt &alt, vector<Val> &argvals, TypeExpr *t, Node *at);
    int HeldParam(FnSpec *spec, const Val &v);
    Val CallResult(Call *c, FnSpec *spec, vector<Val> &argvals);
    void ReturnThroughCalls(Val &v, int tf, Return *r);
    void CheckReturn(Return *r);
    FnSpec *EnsureThreadSpec(SFunction *sf, Line l);

    
    
    

    
    struct LitDeep {
        Roots roots;
        bool byteview = false;
    };
    bool InitOrderStoreSafe(Node *n, TypeExpr *dest);
    bool InitOrderSafe(Node *n, set<FnSpec *> &visiting, FnSpec *callee = nullptr);
    LitDeep CheckInits(StructLit *sl, vector<Field> &fields, vector<TypeExpr *> &ftypes,
                       string_view what, TypeExpr *selft);
    void CheckSelfInit(Node *n, TypeExpr *ft, TypeExpr *selft);

    
    

    void CheckStmt(Node *n);
    void CheckStmtExpr(Node *n);

    
    
    
    
    int deferbodies = 0;
    unordered_map<Defer *, vector<VarDef *>> defermentions;
    void CheckDefer(Defer *d);
    bool InDeferBody();
    void NoteDeferMention(VarDef *v);
    void CheckDefers();
    void CheckVarDecl(VarDecl *vd, bool global);
    void CheckBindingRoot(VarDef *d, const Val &v, Node *at);
    void CheckCycleInit(VarDef *d, int fi, int calls);
    void AssignableClassCheck(TypeExpr *t, Node *at);
    void CheckAssign(Assign *a);
    Val CheckAssignedValue(Assign *a, TypeExpr *target, TypeExpr *arr, const Roots &built,
                           Dest dest);
    void CheckRebind(Assign *a, LVal &lv);
    bool InGenericBody();
    bool PointeeWritable(LVal &lv, Node *at);
    void PointeeAssign(Assign *a, LVal &lv, const LVal &at);
    
    
    bool CheckRefRebindRoot(Node *at, VarDef *vd, const Val &rv, const string &via = {});
    
    
    
    bool StoreIntoSlot(Node *at, VarDef *r, bool bound, TypeExpr *slice, const Val &v,
                       const string &via);
    bool RebindSliceVar(Node *at, VarDef *sv, const Val &v, const string &via);
    
    
    static VarDef *SliceVarOf(VarDef *r) {
        return r && r->type && r->type->kind == TY_SLICE ? r : nullptr;
    }
    void CompoundAssign(Assign *a, TypeExpr *st, bool writable);
    void NoLetAssign(Node *at, const LVal &lv);
    void NoLetReassign(Node *at, const VarDef *vd, const char *verb, const char *done);
    void NoCopyWrite(Node *at, const LVal &lv);
    bool WholeWritable(const LVal &lv);
    void CheckIncDec(IncDec *x);
    [[noreturn]] void NoIndexUpdate(Node *at, const char *op, TypeExpr *st);

    
    

    Val CheckBuiltin(Call *c, const BuiltinDef &d, vector<Node *> &args, Val *precv);
    void CheckByteOut(Call *c, Node *out, const string &form);
    void CheckPrintable(Call *c, const char *what, vector<Node *> &args, size_t i,
                        const Val *out = nullptr);
    
    
    
    struct RenderSeen {
        vector<TypeExpr *> path, recurring;
    };
    TypeExpr *OverloadedPart(Call *c, TypeExpr *t, vector<TypeExpr *> &seen);
    void CheckRenderable(Call *c, const char *what, TypeExpr *t, Node *at, RenderSeen &seen,
                         Val value, const Val &out);
    FnSpec *UserFormat(Call *c, TypeExpr *t, const Val &value, const Val &out, Node *arg);
    FnSpec *UserFormatIn(Call *c, TypeExpr *t, string_view ns, const Val &value, const Val &out,
                         Node *arg);
    StrLit *ConstStrLit(Node *n);
    const string *EmbedShader(Call *c, vector<Node *> &args);
    const string *EmbedFile(Call *c, vector<Node *> &args);
    void CheckGrowShrink(Node *at, const char *op, Node *recv, const Val &rv);
    void GrowOnlyAssignKnown(Assign *a, const Roots &built, bool whole);
    
    
    static bool ShrinkRootsKnown(const Roots &roots) {
        return !roots.Any([](const RootAlt &a) { return !a.root || IsTemp(a.root); });
    }
    
    
    
    
    
    void GrowOnlyShrinkAt(Node *c, const string &op, VarDef *vd, const string &what,
                          TypeExpr *bound = nullptr, TypeExpr *arr = nullptr,
                          bool viaclass = false);
    
    
    
    
    bool KillPath(Node *n, VarDef *&base, string &path);
    void NoteKill(Node *lval, const Roots &loc, TypeExpr *target, const Val &v, size_t at);
    bool KilledFor(VarDef *c, VarDef *base, TypeExpr *t, TypeExpr *arr, size_t &since);
    TypeExpr *KillPartType(const Kill &k);
    bool CallMayWrite(FnSpec *spec, const vector<Val> &argvals, const Kill &k);
    void NoteShrinkOrder(VarDef *root, TypeExpr *bound);
    
    
    
    
    
    
    
    struct CallOrder {
        size_t base = SIZE_MAX;
        const vector<int> *origins = nullptr;
        size_t before = 0;
        const vector<int> *classorigins = nullptr;
        size_t classbase = 0;
    } callorder;
    
    
    
    struct Replay {
        int origin = -1;
        vector<int> *classorigins = nullptr;
        size_t classbase = 0;
    } replay;
    bool StoredLater(size_t i) {
        if (i < callorder.base || !callorder.origins) return false;
        auto k = i - callorder.base;
        if (k >= callorder.origins->size()) return false;
        auto o = (*callorder.origins)[k];
        return o >= 0 && (size_t)o >= callorder.before;
    }
    void DropKills(VarDef *container) {
        erase_if(cur.kills, [&](const Kill &k) { return k.container == container; });
    }

    
    
    
    vector<StoreEvent> storeevents;
    Node *fitnode = nullptr;         
    
    
    
    void NoteLitElem(LitDeep &deep, Node *at, const Val &v, TypeExpr *t);
    void HolderFromLit(Val &v, const LitDeep &deep);
    void AddStoreEvent(const StoreEvent &e);
    void NoteSlotStore(const StoreEvent &e);
    
    
    
    static bool MayPointWording(const Roots &roots, VarDef *gs) {
        return !(roots.alts.size() == 1 && roots.alts[0].root == gs);
    }
    
    
    
    void RecordStore(VarDef *container, const Roots &roots, bool byteview, TypeExpr *pointee,
                     VarDef *src = nullptr, TypeExpr *reached = nullptr, bool bound = false,
                     bool slot = false, bool sliceref = false);
    
    
    
    
    
    bool HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype, size_t from,
                            Line *where, size_t *hit = nullptr, TypeExpr **via = nullptr,
                            VarDef **incoming = nullptr);
    bool HolderMayPointInto(VarDef *holder, VarDef *arr, TypeExpr *arrtype, size_t from,
                            Line *where, set<VarDef *> &seen, size_t *hit,
                            TypeExpr **via = nullptr, VarDef **incoming = nullptr);
    TypeExpr *StoredSlotMayPointInto(VarDef *holder, TypeExpr *pointee, bool sliceref, VarDef *r,
                                     bool exact, VarDef *arr, TypeExpr *arrtype, Line *where,
                                     set<VarDef *> &seen);
    
    
    void ApplyCalleeStores(FnSpec *spec, vector<Val> &argvals, Node *at,
                           vector<int> *origins = nullptr);
    void NoteHolderBinding(VarDef *d, const Val &v, Node *at);

    
    
    
    
    
    
    
    vector<StoreEvent> madestores;
    struct ClassUse {
        Roots roots;               
        VarDef *src = nullptr;     
        bool byteview = false;
    };
    map<VarDef *, vector<ClassUse>> classuses;
    void NoteClassUses(FnSpec *spec, const vector<Val> &argvals);
    void NoteClassUse(VarDef *cr, const ClassUse &u);
    void NoteGlobalBinding(VarDef *gd, const Roots &roots, bool byteview, TypeExpr *pointee,
                           Line at);
    
    
    
    struct GlobalShrink {
        VarDef *arr = nullptr;
        TypeExpr *bound = nullptr;
        Line at;
        string prefix;
        vector<Diagnostic::Related> chain;
    };
    vector<GlobalShrink> globalshrinks;
    void NoteGlobalShrink(Node *at, const string &prefix, VarDef *vd, TypeExpr *bound);
    [[noreturn]] void ThreadIndexError(Node *at, TypeExpr *t);
    void CheckGlobalShrinks();
    
    
    
    struct GlobalReach {
        TypeCheck &tc;
        VarDef *arr;
        TypeExpr *arrtype;
        const map<VarDef *, vector<size_t>> &bycontainer;
        set<VarDef *> held;
        set<pair<VarDef *, bool>> rooted;
        bool Fits(TypeExpr *pointee, bool byteview);
        bool Root(VarDef *r, bool exact, VarDef *from, TypeExpr *pointee, bool byteview);
        bool Class(VarDef *cr, bool exact, TypeExpr *pointee, bool byteview);
        bool Holds(VarDef *x, Line *where = nullptr);
        bool Event(const StoreEvent &e);
        bool Covered(VarDef *src, VarDef *root, bool exact);
    };
    bool GrowOnlyTail(TypeExpr *t);
    bool RefsBesideTail(TypeExpr *t);
    bool IsGrowOnlyRootVar(VarDef *r);
    void RefPointees(TypeExpr *t, vector<TypeExpr *> &out);
    void RefSlots(TypeExpr *t, vector<pair<TypeExpr *, bool>> &out);
    VarDef *HolderRootOf(const Val &v);
    Roots ClassArgRoots(TypeExpr *pt, const Val &v);

    string ExprStr(Node *n) {
        string s;
        dumpwritten = true;
        n->Dump(s, 0);
        dumpwritten = false;
        return s;
    }

    void CheckShrinkHolders(Node *at, const string &op, VarDef *root, const string &what,
                            TypeExpr *bound = nullptr);
    
    
    
    
    template <typename P, typename X> void NoteRootEvent(VarDef *root, P param, X external);
    void NoteShrink(VarDef *root, TypeExpr *bound = nullptr,
                    ShrinkBalance balance = SB_UNBALANCED);
    void ShrinkGrowShrink(Node *at, const string &op, VarDef *root, const string &what,
                          TypeExpr *bound = nullptr, ShrinkBalance balance = SB_UNBALANCED);
    bool ResizesToMark(Node *recv, Node *len);
    bool SamePath(Node *a, Node *b);
    
    
    
    
    
    
    struct ShrinkTarget {
        VarDef *root = nullptr;
        bool bound = false;
        bool viaclass = false;
    };
    vector<ShrinkTarget> ShrinkTargets(const Roots &roots, TypeExpr *arr, bool slots = false,
                                       bool classbound = false);
    string TargetStr(const ShrinkTarget &t);
    void ShrinkThrough(Node *at, const string &verb, const string &recv, const Roots &roots,
                       TypeExpr *arr, ShrinkBalance balance = SB_UNBALANCED);
    
    
    
    void ApplyCalleeShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name,
                            const vector<Node *> &argnodes);
    
    
    
    
    
    bool IsClassRoot(VarDef *v) {
        return v && !v->type && !v->isglobal && !IsTemp(v);
    }
    
    
    
    
    
    bool WithinDepth(VarDef *arr, VarDef *r) {
        if (IsClassRoot(r) && arr->ownerspec && arr->ownerspec == frames.back().spec)
            return false;
        return Depth(arr) <= Depth(r);
    }
    
    
    
    bool OuterLocal(VarDef *v) {
        auto d = Depth(v) - 1;
        if (d < 0 || d >= (int)scopes.size()) return false;
        auto vf = FrameOfScope(d);
        return frames[vf].spec == v->ownerspec && NamesFrame((int)frames.size() - 1, vf);
    }
    bool CallersJudge(VarDef *r, VarDef *root);
    void NoteLiveViews(Node *at, const string &prefix, VarDef *root, const string &what,
                       bool growonly, TypeExpr *bound);
    
    
    
    struct LiveView {
        Prov p;
        TypeExpr *pointee = nullptr;
        bool reached = false;    
        bool contents = false;   
        bool inplace = false;    
    };
    
    
    using TempHolds = vector<pair<VarDef *, Roots>>;
    void RecordedViews(VarDef *h, size_t from, TypeExpr *ht, vector<LiveView> &out,
                       set<VarDef *> &seen);
    void StoredViews(VarDef *r, bool exact, TypeExpr *pointee, bool sliceref, bool byteview,
                     TypeExpr *ht, vector<LiveView> &out, set<VarDef *> &seen);
    
    
    
    void HeldViews(const Prov &p, TypeExpr *held, bool isvar, vector<LiveView> &out,
                   const TempHolds *temps = nullptr, VarDef *base = nullptr,
                   TypeExpr *arrtype = nullptr);
    template<typename F> void EachHolderRoot(VarDef *holder, size_t from, F f);
    bool NoteLiveShrink(LiveShrink ls, FnSpec *current);
    void ApplyCalleeLiveShrinks(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    
    
    
    
    
    
    
    struct GrowEvent {
        Node *at = nullptr;
        VarDef *root = nullptr;
        bool exact = false;   
        string what;
        bool shrink = false;
        
        
        TypeExpr *arr = nullptr;
    };

    
    
    
    
    
    
    struct BodyState {
        
        vector<PathEntry> nodepath;
        
        
        
        
        Node *renderarg = nullptr;
        Node *renderwhere = nullptr;
        const char *rendering = nullptr;
        
        
        
        
        
        
        
        
        vector<Val> renderwalks;
        
        
        vector<LoopPass> looppasses;
        
        
        
        vector<Warning> pendingwarnings;
        int joinprobes = 0;
        
        
        
        vector<GrowEvent> growlog;
        
        
        vector<Kill> kills;
    };
    BodyState cur;
    
    
    vector<BodyState *> outerbodies;
    BodyState &StateOf(int fi) {
        auto b = frames[fi].bodyidx;
        return b < (int)outerbodies.size() ? *outerbodies[b] : cur;
    }
    
    
    
    
    void NoteTemp(Node *n, const Val &v, int cyclecalls);
    Node *FindTemp(Node *n, bool self = true);
    void CycleCallTemps(int fi, Node *callnode);
    void CycleCallResult(FnSpec *spec, Node *callnode);
    bool BuiltInPlace(int fi, int k, const vector<TypeExpr *> &rets);
    
    
    struct BodyScope {
        TypeCheck &tc;
        BodyState saved;
        BodyScope(TypeCheck &t) : tc(t) {
            std::swap(tc.cur, saved);
            tc.outerbodies.push_back(&saved);
        }
        ~BodyScope() {
            tc.outerbodies.pop_back();
            std::swap(tc.cur, saved);
        }
    };
    
    void NoteGrow(Node *at, const Roots &roots, const string &what, TypeExpr *t);
    void NoteShrinkEvent(Node *at, VarDef *root, TypeExpr *bound, const string &what,
                         TypeExpr *t);
    void CheckGrowsSince(size_t base, const Roots &built, const string &what, TypeExpr *t);
    
    
    
    TypeExpr *SoleResizableIn(TypeExpr *t);
    void ApplyCalleeGrows(Node *at, FnSpec *spec, vector<Val> &argvals, string_view name);
    
    
    
    
    
    
    
    bool BuiltInPlace(TypeExpr *elem);
    
    
    TypeExpr *AppendedRun(TypeExpr *elem, ArrayLit *al);
    
    
    void AppendedCopies(Node *an, const Val &av, TypeExpr *elem, const Val &rv);
    
    
    
    enum Alias { AL_NO, AL_YES, AL_DEFER };
    Alias MayAliasRoots(VarDef *a, bool aexact, VarDef *b, bool bexact);
    Alias MayAliasRoots(VarDef *a, bool aexact, VarDef *b, bool bexact, FnSpec *current);
    
    
    struct GrowConflict {
        Node *at = nullptr;
        FnSpec *spec = nullptr;
        VarDef *grown = nullptr;
        VarDef *built = nullptr;
        string msg;
    };
    vector<GrowConflict> growconflicts;
    
    
    
    
    void CheckBuiltUses(Node *rhs, Node *lval, TypeExpr *target, const Roots &built,
                        TypeExpr *arr);
    Alias ReachesBuilt(VarDef *v, VarDef *built, bool exact, Node *lval, TypeExpr *target,
                       TypeExpr *arr, VarDef *&root);
    bool MayHoldBuilt(TypeExpr *y, Node *lval, TypeExpr *target);
    template<typename F, typename G> void EachUse(Node *n, F named, G called);
    bool FieldsApart(Node *use, Node *lval, TypeExpr *target);
    bool NamedOutside(FnSpec *spec, vector<VarDef *> &out);
    
    
    map<FnSpec *, vector<VarDef *>> namedoutside;
    
    void ElemArg(Node *&n, TypeExpr *elem, Val &rv, bool fill = false);

    
    
    

    Val CheckFunValCall(Call *c, const FnValBind &fb, TypeExpr *expected);
    TypeExpr *SubstEnv(TypeExpr *t, FnSpec *env);

    
    

    
    
    
    
    
    
    
    
    
    
    struct Checkpoint {
        TypeCheck &tc;
        size_t frames, scopes, vars, localfns, blockpos, outerbodies, cyclerounds, typechain,
               laterpointees, fnspecs, structinsts, enuminsts;
        int typesbuilding, deferbodies;
        BodyState cur;
        struct GlobalFlow { VarDef *v; bool assigned, maybeassigned; TypeExpr *narrowed; };
        vector<GlobalFlow> globals;
        Checkpoint(TypeCheck &t)
            : tc(t), frames(t.frames.size()), scopes(t.scopes.size()), vars(t.vars.size()),
              localfns(t.localfns.size()), blockpos(t.blockpos.size()),
              outerbodies(t.outerbodies.size()), cyclerounds(t.cyclerounds.size()),
              typechain(t.typechain.size()), laterpointees(t.laterpointees.size()),
              fnspecs(t.ast.fnspecs.size()), structinsts(t.ast.structinsts.size()),
              enuminsts(t.ast.enuminsts.size()), typesbuilding(t.typesbuilding),
              deferbodies(t.deferbodies), cur(t.cur) {
            for (auto g : tc.ast.globals)
                for (auto v : g->defs) globals.push_back({ v, v->assigned, v->maybeassigned, v->narrowed });
        }
        void Restore() {
            tc.frames.resize(frames);
            tc.scopes.resize(scopes);
            tc.vars.resize(vars);
            tc.localfns.resize(localfns);
            tc.blockpos.resize(blockpos);
            tc.outerbodies.resize(outerbodies);
            tc.cyclerounds.resize(cyclerounds);
            tc.typechain.resize(typechain);
            tc.laterpointees.resize(laterpointees);
            tc.typesbuilding = typesbuilding;
            tc.deferbodies = deferbodies;
            tc.cur = cur;
            tc.reachable = true;
            tc.fitnode = nullptr;
            tc.lastcallrets.clear();
            for (auto i = fnspecs; i < tc.ast.fnspecs.size(); i++) {
                auto sp = tc.ast.fnspecs[i];
                if (!sp->inprogress) continue;
                sp->failed = true;
                sp->inprogress = false;
                sp->stale = false;
            }
            for (auto i = structinsts; i < tc.ast.structinsts.size(); i++)
                if (!tc.ast.structinsts[i]->validated) tc.ast.structinsts[i]->failed = true;
            for (auto i = enuminsts; i < tc.ast.enuminsts.size(); i++)
                if (!tc.ast.enuminsts[i]->validated) tc.ast.enuminsts[i]->failed = true;
            for (auto &g : globals) {
                g.v->assigned = g.assigned;
                g.v->maybeassigned = g.maybeassigned;
                g.v->narrowed = g.narrowed;
            }
        }
    };

    
    
    
    
    
    
    template<typename F> bool CheckRoot(F f, bool report = true) {
        Checkpoint cp(*this);
        try {
            f();
            return true;
        } catch (CompileError &e) {
            if (report) ReportError(e);
        } catch (Abandoned &) {
        }
        cp.Restore();
        return false;
    }

    
    
    
    
    
    
    
    set<string> itemerrors;
    template<typename F> void CheckItem(F f) {
        try {
            f();
        } catch (CompileError &e) {
            if (itemerrors.insert(e.msg).second) ReportError(e);
        } catch (Abandoned &) {
        }
    }

    void ReportError(const CompileError &e) {
        if (e.diags.empty()) {
            Diagnostic d;
            d.message = d.text = e.msg;
            ast.Report(d);
        }
        for (auto &d : e.diags) ast.Report(d);
    }

    
    
    
    set<VarDef *> failedglobals;

    TypeCheck(Ast &_ast, bool _library = false, bool _nomain = false) : ast(_ast), library(_library), nomain(_nomain) {
        
        
        
        nodevals.reserve(ast.allnodes.size());
        temproot = ast.NewVarDef();
        temproot->name = "<temporary>";
        temproot->istemp = true;
        temproot->depth = INT32_MAX;
        fntype = ast.NewType(TY_FN, Line {});
        fntype->fn = ast.NewDetail<TypeFn>();
        u8slice = ast.SliceOf(ast.inttypes[IS_U8], Line {});
        cu8slice = ast.SliceOf(ast.inttypes[IS_U8], Line {});
        cu8slice->cq = true;
        nulltype = ast.RefTo(ast.voidtype, Line {}, true);
        Frame f;
        frames.push_back(f);
        
        
        int globalindex = 0;
        for (auto g : ast.globals) {
            for (auto name : g->names) {
                auto vd = ast.NewVarDef();
                vd->name = name;
                vd->line = g->line;
                vd->isvar = g->isvar;
                vd->isglobal = true;
                vd->globalindex = globalindex++;
                vd->reusable = g->reusable;
                g->defs.push_back(vd);
            }
        }
        ResolvePools();
        ResolveIndexTypes();
        
        
        for (auto st : ast.structs)
            if (st->generics.empty()) GetStructInst(ast.StructOf(st, {}, st->line));
        for (auto en : ast.enums)
            if (en->generics.empty()) GetEnumInst(ast.EnumOf(en, {}, true, en->line));
        
        
        for (auto sf : ast.functions) SignatureNames(sf->params, sf->rets, sf->generics);
        for (auto st : ast.structs)
            for (auto &fld : st->fields)
                if (fld.type) TypeParamSizes(fld.type, st->generics);
        for (auto en : ast.enums)
            for (auto &v : en->variants)
                for (auto &fld : v.fields)
                    if (fld.type) TypeParamSizes(fld.type, en->generics);
        CheckRoots();
    }

    
    
    
    
    
    
    
    void CheckRoots() {
        for (auto g : ast.globals) {
            if (CheckRoot([&] { CheckVarDecl(g, true); })) {
                for (auto d : g->defs) d->assigned = d->maybeassigned = true;
            } else {
                for (auto d : g->defs) failedglobals.insert(d);
            }
        }
        ValidateNamedComponents();
        
        
        for (auto t : ast.alltypes) {
            if (t->kind == TY_REF && t->ref->pool && !HasGenerics(t->ref->sub)) ValidatePool(t);
            if (IsIndexT(t)) ValidateIndexType(t);
        }
        auto mainsf = ast.MainFunction();
        if (!mainsf && !library && !(nomain && !ast.DeclaresMain()))
            throw CompileError { "program needs exactly one global fn main()" };
        if (mainsf && !mainsf->brokensig) {
            CheckRoot([&] {
                if (!mainsf->params.empty() || mainsf->has_rets || !mainsf->generics.empty() ||
                    mainsf->isthread)
                    Error(mainsf->line, "fn main() takes no parameters and returns nothing");
                auto mainspec = ast.NewFnSpec();
                mainspec->sf = mainsf;
                mainsf->specs.push_back(mainspec);
                CheckSpecBody(mainspec, nullptr, mainsf->line);
            });
        }
        
        
        for (size_t i = 0; i < ast.functions.size(); i++) {
            auto sf = ast.functions[i];
            if (sf->isthread) CheckRoot([&] { EnsureThreadSpec(sf, sf->line); });
        }
        
        
        for (auto sf : ast.functions)
            if (sf->isthread && !sf->specs.empty() && !sf->specs[0]->failed)
                CheckRoot([&] { CheckThreadGlobals(sf->specs[0]); });
        
        
        
        
        
        
        
        
        
        for (size_t i = 0; i < ast.functions.size(); i++) {
            auto sf = ast.functions[i];
            CheckRoot([&] { CheckUnreached(sf); });
        }
        auto exportcount = 0;
        for (auto sf : ast.functions) {
            if (!sf->isexport) continue;
            exportcount++;
            CheckRoot([&] { CheckExport(sf); });
        }
        if (library && !exportcount)
            throw CompileError { "--header requires at least one export fn" };
        CheckRoot([&] { CheckGlobalShrinks(); });
        CheckRoot([&] { SettleParamRootExactness(); });
        CheckRoot([&] { ResolveGrowConflicts(); });
        CheckRoot([&] { VerifyLiterals(); });
        CheckRoot([&] { ReportRedundantCasts(); });
        CheckRoot([&] { CheckDefers(); });
        if (ast.exemplars) CheckExemplars();
    }

    
    
    
    void ResolveGrowConflicts() {
        for (auto &gc : growconflicts) {
            auto distinct = [&](VarDef *cr) {
                auto found = false;
                for (size_t i = 0; gc.spec && i < gc.spec->params.size() &&
                                   i < gc.spec->roots.size(); i++) {
                    if (!ProjectedFrom(cr, gc.spec->params[i]->ref.Root())) continue;
                    if (!gc.spec->roots[i].concrete || !gc.spec->roots[i].exact) return false;
                    found = true;
                }
                return found;
            };
            if (!distinct(gc.grown) || !distinct(gc.built))
                CheckItem([&] { Error(gc.at, gc.msg); });
        }
    }

    
    
    
    
    
    
    void SettleParamRootExactness() {
        for (auto spec : ast.fnspecs) {
            for (size_t i = 0; i < spec->params.size() && i < spec->argtypes.size(); i++) {
                auto t = spec->argtypes[i];
                if (!IsRefOrSlice(t)) continue;
                if (!spec->roots[i].exact) spec->params[i]->ref.Weaken();
            }
        }
        
        
        
        
        
        for (auto changed = true; changed;) {
            changed = false;
            for (auto spec : ast.fnspecs)
                for (auto &ra : spec->roots)
                    for (auto [p, j] : ra.via)
                        if (ra.concrete && (j >= (int)p->roots.size() || !p->roots[j].concrete ||
                                            !p->roots[j].exact)) {
                            ra.concrete = false;
                            changed = true;
                        }
        }
    }

    void CheckUnreached(SFunction *sf) {
        if (!sf->specs.empty() || sf->isthread || sf->isnested || sf->brokensig) return;
        if (sf->IsGeneric()) return;
        set<string_view> names = { sf->name };
        auto foreign = false;
        if (sf->body) ScanForeignFrom(sf->body, names, foreign);
        if (foreign) return;
        auto spec = ast.NewFnSpec();
        spec->sf = sf;
        for (auto &p : sf->params) spec->argtypes.push_back(Subst(p.type));
        CheckStandalone(spec);
    }

    
    
    
    
    
    
    
    
    
    
    int exemplarbase = -1;    
    void CheckExemplars() {
        vector<SFunction *> todo;
        for (auto sf : ast.functions) {
            if (!sf->specs.empty() || sf->isthread || sf->isnested || sf->isextern || !sf->body)
                continue;
            if (sf->IsGeneric()) todo.push_back(sf);
        }
        if (todo.empty()) return;
        exemplarbase = (int)ast.fnspecs.size();
        FlagScope q(quiet, true);
        for (auto sf : todo) {
            auto from = ast.fnspecs.size();
            CheckRoot([&] { CheckExemplar(sf); }, false);
            for (auto i = from; i < ast.fnspecs.size(); i++) ast.fnspecs[i]->exemplar = sf;
        }
        for (auto sf : ast.functions)
            erase_if(sf->specs, [](FnSpec *sp) { return sp->exemplar != nullptr; });
        ast.exemplarspecs.assign(ast.fnspecs.begin() + exemplarbase, ast.fnspecs.end());
        ast.fnspecs.resize((size_t)exemplarbase);
    }

    
    
    
    
    
    TypeExpr *OpaqueType(string_view name) {
        auto t = ast.NewType(TY_GENERIC, Line {});
        t->named = ast.NewDetail<TypeName>();
        t->named->name = name;
        t->named->opaque = true;
        return t;
    }
    
    
    TypeExpr *opaquemember = nullptr;
    TypeExpr *OpaqueMember() {
        if (!opaquemember) opaquemember = OpaqueType("?");
        return opaquemember;
    }

    void CheckExemplar(SFunction *sf) {
        auto spec = ast.NewFnSpec();
        spec->sf = sf;
        for (auto &g : sf->generics) spec->bindings.push_back({ g.name, OpaqueType(g.name) });
        WithBindings(spec->bindings, [&] {
            for (auto &p : sf->params)
                spec->argtypes.push_back(p.type ? Subst(p.type)
                                                : OpaqueType(ast.Intern(cat("type of ", p.name))));
        });
        CheckStandalone(spec);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    void CheckStandalone(FnSpec *spec) {
        auto sf = spec->sf;
        vector<Val> args(sf->params.size());
        auto classes = 0;
        
        
        auto poolfor = [&](TypeExpr *t) -> VarDef * {
            if (t->kind != TY_REF || t->ref->lenstorage >= 0) return nullptr;
            VarDef *found = nullptr;
            for (auto g : poolglobals) {
                if (!g->type || !IsArrayKind(g->type, A_GROW) ||
                    !TypeEq(g->type->arr->sub, t->ref->sub))
                    continue;
                if (found) return nullptr;
                found = g;
            }
            for (auto g : poolowners) {
                if (!g->type || !TypeEq(g->type, t->ref->sub)) continue;
                if (found) return nullptr;
                found = g;
            }
            return found;
        };
        map<VarDef *, int> poolclasses;
        for (size_t i = 0; i < sf->params.size(); i++) {
            auto &p = sf->params[i];
            auto t = spec->argtypes[i];
            ValidateType(t, sf->line, VT_PARAM);
            RootArg ra;
            RootArg noview;
            noview.cls = -1;
            auto holder = !IsRefOrSlice(t) && HoldsPlainRef(t);
            if (auto pool = poolfor(t)) {
                auto &cls = poolclasses[pool];
                if (!cls) cls = ++classes;
                ra.cls = cls;
                ra.concrete = true;
                ra.writable = true;
                ra.pool = pool;
                vector<TypeExpr *> elems, open;
                ArrayElemsReached(t, elems, open);
                for (auto e : elems)
                    if (OneArrayOf(pool, e)) ra.onearray.push_back(e);
                args[i].Set(pool, true);
                spec->roots.push_back(ra);
                spec->views.push_back(noview);
                continue;
            }
            if (IsRefOrSlice(t) || holder) {
                ra.cls = ++classes;
                ra.concrete = true;
                auto g = ast.NewVarDef();
                g->name = p.name;
                g->isglobal = true;
                args[i].Set(g, true);
            }
            if (IsRefOrSlice(t)) {
                ra.writable = true;
                if (CarriesPool(t) && ClassOf(t->ref->sub->arr->sub) == SC_FIXED)
                    ra.reusable = RU_SLOTS | RU_SLICES;
                
                if (t->kind == TY_REF) {
                    vector<TypeExpr *> elems, open;
                    ArrayElemsReached(t, elems, open);
                    for (auto e : elems)
                        if (ElemArrayPlaces(LoadType(t->ref->sub), e) == 1) ra.onearray.push_back(e);
                }
            } else if (holder) {
                
                
                ra.heldexact = !sf->isrec;
            }
            spec->roots.push_back(ra);
            spec->views.push_back(noview);
        }
        sf->specs.push_back(spec);
        CheckSpecBody(spec, &args, sf->line);
    }

    
    
    
    bool StaticViewsOnly(VarDef *g) {
        if (g->isvar) return false;
        if (Reaches(g->type, [](TypeExpr *t) { return IsRelRef(t); }))
            return false;
        auto statics = [](const Roots &r) {
            return !r.unknown && r.All([](const RootAlt &a) { return !a.root && a.exact; });
        };
        auto k = g->type->kind;
        return k == TY_REF || k == TY_SLICE ? statics(g->ref) : statics(g->contents);
    }

    
    
    
    void CheckThreadGlobals(FnSpec *entry) {
        set<FnSpec *> seen;
        set<TypeExpr *> namedtypes;
        function<void(FnSpec *)> rec = [&](FnSpec *sp) {
            if (!sp || !sp->body || !seen.insert(sp).second) return;
            function<void(Node *)> walk = [&](Node *n) {
                if (!n) return;
                NodeNamedArrays(n, namedtypes, [&](VarDef *a, bool) {
                    if (auto owner = a->componentowner)
                        Error(n, cat("thread programs cannot copy root global ", owner->name,
                                     " (named by a type reached from thread_fn ", entry->sf->name,
                                     "); construct worker-local state instead"));
                });
                if (auto id = Is<Ident>(n)) {
                    auto g = id->vdef;
                    if (g && g->isglobal && g->type && ClassOf(g->type) == SC_ROOT)
                        Error(n, cat("thread programs cannot copy root global ", id->name,
                                     " (reached from thread_fn ", entry->sf->name,
                                     "); construct worker-local state instead"));
                    if (g && g->isglobal && g->type && !IsFlat(g->type) && !StaticViewsOnly(g))
                        Error(n, cat("thread programs may access only flat globals, and `let` "
                                     "globals whose references lead only to static data "
                                     "(§11.2): ", id->name, " holds references",
                                     g->isvar ? " and is a var" : "", " (reached from thread_fn ",
                                     entry->sf->name, ")"));
                }
                if (auto c = Is<Call>(n)) {
                    
                    
                    
                    
                    if (c->spec && c->spec->sf->isextern) {
                        auto fileidx = c->spec->sf->line.fileidx;
                        if (ast.packages[ast.filepkg[fileidx]].main_thread_only)
                            Error(n, cat(PathStem(ast.sources[fileidx].first),
                                         " runs on the main thread only: ", c->spec->sf->qname,
                                         " is reached from thread_fn ", entry->sf->name));
                    }
                    rec(c->spec);
                    for (auto d : c->dispatch) rec(d);
                    for (auto &fs : c->fmtspecs) rec(fs.second);
                }
                RunChildren(n, walk);
            };
            walk(sp->body);
        };
        rec(entry);
    }

    
    void ScanForeignFrom(Node *n, set<string_view> &names, bool &foreign) {
        if (!n || foreign) return;
        if (auto r = Is<Return>(n)) {
            if (!r->from.empty() && !names.count(r->from)) foreign = true;
        } else if (auto fd = Is<FnDecl>(n)) {
            
            
            names.insert(fd->sf->name);
            if (fd->sf->body) ScanForeignFrom(fd->sf->body, names, foreign);
            return;
        }
        n->Children([&](Node *c) { ScanForeignFrom(c, names, foreign); });
    }
};





inline void TypeCheckProgram(Ast &ast, bool library = false, bool nomain = false) {
    TypeCheck tc(ast, library, nomain);
}

}  
