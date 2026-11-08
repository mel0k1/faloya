


















































#pragma once

namespace faloya {












enum DstKind { DK_DISCARD, DK_LVALUE, DK_STACK, DK_BRANCH };
struct Dst {
    DstKind k = DK_DISCARD;
    string s;
    TypeExpr *t = nullptr;
    string lenlv;
    bool pool = false;
    bool when = false;
    int exitscope = -1;
};








struct RelFacts {
    bool nonnull = false;
    bool apart = false;
};

struct CodeGen {
    Ast &ast;

    
    
    string predefs;
    string tdecls;      
    string pdata;       
    string data;        
    
    
    
    
    string gblock;
    
    
    unordered_map<FnSpec *, vector<VarDef *>> threadglobals;
    vector<VarDef *> &ThreadGlobals(FnSpec *entry);
    string protos;
    
    
    
    vector<FnSpec *> usedexterns;
    
    
    
    
    string DlSlot(FnSpec *sp) {
        auto it = std::find(usedexterns.begin(), usedexterns.end(), sp);
        assert(it != usedexterns.end() && !sp->sf->dylib.empty());
        return cat("gs_dl", it - usedexterns.begin(), "_", sp->sf->cname);
    }
    
    string DlCall(FnSpec *sp) { return cat(DlSlot(sp), "_call"); }
    
    
    
    
    DylibSpelling DlSpelling(FnSpec *sp) {
        DylibSpelling r { sp->sf->dylib };
        auto pkg = ast.filepkg[sp->sf->line.fileidx];
        for (auto p : { pkg, 0 }) {
            for (auto &d : ast.packages[p].dylibs) {
                if (d.name != r.name) continue;
                for (auto plat : { "_windows", "_macos", "_linux" }) {
                    auto &s = d.On(plat).empty() ? d.any : d.On(plat);
                    if (!s.empty()) r.On(plat) = s;
                }
            }
        }
        return r;
    }
    static bool SameDylib(const DylibSpelling &a, const DylibSpelling &b) {
        return a.name == b.name && a.onwindows == b.onwindows &&
               a.onmacos == b.onmacos && a.onlinux == b.onlinux;
    }
    
    
    
    vector<DylibSpelling> DlLibs() {
        vector<pair<Line, DylibSpelling>> firsts;
        for (auto sp : usedexterns) {
            if (sp->sf->dylib.empty()) continue;
            auto l = sp->sf->line;
            auto d = DlSpelling(sp);
            auto it = std::find_if(firsts.begin(), firsts.end(),
                                   [&](auto &f) { return SameDylib(f.second, d); });
            if (it == firsts.end()) firsts.emplace_back(l, d);
            else if (pair(l.fileidx, l.line) < pair(it->first.fileidx, it->first.line))
                it->first = l;
        }
        std::stable_sort(firsts.begin(), firsts.end(), [](auto &a, auto &b) {
            return pair(a.first.fileidx, a.first.line) < pair(b.first.fileidx, b.first.line);
        });
        vector<DylibSpelling> libs;
        for (auto &f : firsts) libs.push_back(f.second);
        return libs;
    }
    
    
    size_t dlbinds = 0;
    string code;        
    string exportprotos;
    bool usesthreads = false;
    bool library = false;
    
    
    
    
    bool norfcheck = false;

    [[noreturn]] void Fail(Line l, const string &msg) {
        auto s = cat("codegen: ", msg);
        if (l.fileidx >= 0 && l.fileidx < (int)ast.sources.size())
            s = cat(ast.sources[l.fileidx].first, ":", l.line, ": ", s);
        throw CompileError { s };
    }

    
    
    
    
    static string CStr(string_view v) {
        string s = "\"";
        auto split = v.size() > 4000;
        size_t piece = 0;
        for (size_t i = 0; i < v.size(); i++) {
            auto c = v[i];
            auto u = (uint8_t)c;
            if (c == '"' || c == '\\') { s += '\\'; s += c; }
            else if (u >= 32 && u < 127) s += c;
            else {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\%03o", u);
                s += buf;
            }
            if (split && (c == '\n' || ++piece == 4000) && i + 1 < v.size()) {
                s += "\"\n    \"";
                piece = 0;
            }
        }
        s += '"';
        return s;
    }

    
    
    
    
    
    
    
    
    map<int, string> filerefs;
    map<pair<int, int>, string> locrefs;

    string FileRef(Line l) {
        auto it = filerefs.find(l.fileidx);
        if (it == filerefs.end()) {
            auto f = l.fileidx >= 0 && l.fileidx < (int)ast.sources.size()
                         ? ast.sources[l.fileidx].first : string("?");
            for (auto &c : f) if (c == '\\') c = '/';
            auto name = Unique(cat("gs_file", filerefs.size()));
            Append(data, "static const char ", name, "[] = ", CStr(f), ";\n");
            it = filerefs.emplace(l.fileidx, name).first;
        }
        return it->second;
    }
    bool AtCaller(Line l) { return curinfo && curinfo->located && ast.StdlibFile(l.fileidx); }
    string LocFile(Line l) { return AtCaller(l) ? string("gs_at->file") : FileRef(l); }
    string LocLine(Line l) { return AtCaller(l) ? string("gs_at->line") : cat(l.line); }
    string LocArgs(Line l) { return cat(LocFile(l), ", ", LocLine(l)); }
    
    string CallerLoc(Line l) {
        if (AtCaller(l)) return "gs_at";
        auto key = make_pair(l.fileidx, l.line);
        auto it = locrefs.find(key);
        if (it == locrefs.end()) {
            auto file = FileRef(l);
            auto name = Unique(cat("gs_loc", locrefs.size()));
            Append(data, "static const gs_loc ", name, " = { ", file, ", ", l.line, " };\n");
            it = locrefs.emplace(key, name).first;
        }
        return cat("&", it->second);
    }

    
    
    
    string OvfLocArgs(IntStorage s, Line l) {
        return IsUnsigned(s) ? string() : cat(", ", LocArgs(l));
    }

    
    
    
    
    
    
    
    
    
    

    int64_t ArrSize(TypeArray *a) {
        assert(a->size >= 0 || !a->sizeexpr);
        return a->size;
    }

    bool TEq(TypeExpr *a, TypeExpr *b);
    StructInst *SI(TypeExpr *t);
    EnumInst *EIOf(TypeExpr *t);
    EnumInst *EIVar(TypeExpr *t);
    
    
    span<const FieldRun> FieldRuns(TypeExpr *t);
    
    template<typename F> bool AnyField(TypeExpr *t, F f) { return AnyFieldOf(FieldRuns(t), f); }
    template<typename F> void EachField(TypeExpr *t, F f) { EachFieldOf(FieldRuns(t), f); }
    SizeClass Cls(TypeExpr *t);

    bool IsFix(TypeExpr *t)  { return Cls(t) == SC_FIXED; }
    bool IsResz(TypeExpr *t) { return Cls(t) == SC_RESIZABLE; }
    bool IsRoot(TypeExpr *t) { return Cls(t) == SC_ROOT; }
    bool IsBytesT(TypeExpr *t) { auto c = Cls(t); return c == SC_VARIABLE || c == SC_RESIZABLE; }
    
    
    static constexpr int64_t NATIVE_VALUE_LIMIT = 4096;
    bool IsLargeFixed(TypeExpr *t) { return IsFix(t) && FixedSize(t) > NATIVE_VALUE_LIMIT; }
    bool NeedsStack(TypeExpr *t) {
        return IsBytesT(t) || IsLargeFixed(t) ||
               (IsRoot(t) && AnyField(t, [&](TypeExpr *ft) { return NeedsStack(ft); }));
    }
    
    
    void FixedLocal(TypeExpr *t, string &name, const string &init = "", bool forlocal = false);
    
    
    
    bool IsStaticLimited(TypeExpr *t) {
        return t->kind == TY_ARRAY && t->arr->akind == A_LIMITED && IsFix(t);
    }
    
    
    
    
    
    bool IsFrameObj(TypeExpr *t) { return t->kind == TY_STRUCT && SI(t)->frameobj; }
    
    
    string RzCT(TypeExpr *t) { return IsFrameObj(t) ? CT(t) : "gs_rhdr"; }
    string FoTailHdr(TypeExpr *t, const string &obj);
    TypeExpr *FoTailArr(TypeExpr *t);
    string FoPrefixSize(TypeExpr *t);
    
    
    int64_t FoBytesPrefix(TypeExpr *t);
    bool IsFatRef(TypeExpr *t);
    bool HoldsFatRef(TypeExpr *t);
    bool HoldsFatRefIn(TypeExpr *t, set<const void *> &open);
    bool IsVoidT(TypeExpr *t) { return !t || t->kind == TY_VOID; }

    IntStorage LenStore(TypeArray *a);

    static int64_t IntSize(IntStorage s) { return IntBits(s) / 8; }

    static const char *IntCT(IntStorage s, bool unaligned = true);
    static const char *IntSfx(IntStorage s);
    static const char *RelCT(IntStorage s, bool uns = false);
    static const char *RelCT(TypeExpr *rt);

    IntStorage TagStore(SEnum *en) { return en->variants.size() <= 256 ? IS_U8 : IS_U16; }
    int64_t TagSize(SEnum *en) { return IntSize(TagStore(en)); }

    
    
    
    struct Layout {
        vector<int64_t> offs;
        int64_t size = 0;
    };
    map<pair<const void *, int>, Layout> layouts;   

    int64_t PadAlign(TypeExpr *t);
    Layout LayoutFields(const vector<Field> &fields, const vector<TypeExpr *> &ftypes);
    const Layout &StructLayout(StructInst *si);
    const Layout &VariantLayout(EnumInst *ei, int vi);
    int64_t FixedSize(TypeExpr *t);

    
    
    

    set<string> used;

    string Sanitize(string_view name);
    string Sanitize(string_view ns, string_view name);
    string Unique(string base);

    
    
    
    

    string Mangle(TypeExpr *t);
    string RawMangle(TypeExpr *t);
    bool SameMangle(TypeExpr *a, TypeExpr *b);
    map<VarDef *, int> ixmangles;   
    
    
    unordered_map<string, vector<pair<TypeExpr *, string>>> mangleowners;
    unordered_set<string> mangles;

    unordered_map<string, string> ctypes;   
    bool corebuiltins = false;

    void EmitCoreTypes();
    string CT(TypeExpr *t, bool unaligned = true);
    static bool StructLike(TypeExpr *t);
    string NameCT(TypeExpr *t);
    set<string> cdefined;    
    set<string> slicects;    

    void EmitCFields(string &d, const vector<Field> &fields, const vector<TypeExpr *> &ftypes);

    
    set<EnumInst *> tagenums;
    unordered_map<EnumInst *, string> tagprefix;
    unordered_set<string> tagbases;   

    void EnsureTagEnum(EnumInst *ei);
    string TagConst(EnumInst *ei, int vi);

    
    map<pair<EnumInst *, int>, TypeExpr *> varianttypes;

    TypeExpr *VariantType(TypeExpr *enumtype, int vi);

    
    
    

    set<string> sizefns, eqfns;

    string SizeX(TypeExpr *t, const string &ptr);
    string SizeFn(TypeExpr *t);
    void EmitSizeWalk(string &b, TypeExpr *t, const string &q);
    void EmitSizeElems(string &b, TypeExpr *elem, const string &q);
    int64_t MinBytes(TypeExpr *t, bool zero = false);

    
    
    
    
    

    set<string> verifyfns;
    int vtmpn = 0;

    string VTmp() { return cat("v", vtmpn++); }
    bool HasRelRefAny(TypeExpr *t);
    bool NeedsVerifyWalk(TypeExpr *t);
    string VerifyFn(TypeExpr *elem);
    void VNeed(string &b, const string &q, const string &bytes);
    void EmitVerifyWalk(string &b, TypeExpr *t, TypeExpr *elem, const string &q, bool links);
    void EmitVerifyFields(string &b, const vector<Field> &fields,
                          const vector<TypeExpr *> &ftypes, const Layout *lo, int64_t total,
                          TypeExpr *elem, const string &q, bool links);
    void EmitVerifyLink(string &b, TypeExpr *rt, TypeExpr *elem, const string &q,
                        const string &off);

    
    
    
    
    

    bool HasFieldDefaults(TypeExpr *t);
    bool ZeroDefaultValue(Node *n, TypeExpr *t);
    void EmitDefaultInto(const string &lv, TypeExpr *t);

    
    
    
    

    bool BitwiseEq(TypeExpr *t);
    bool ScalarEq(TypeExpr *t);
    int64_t EqScalars(TypeExpr *t, int64_t cap);
    string EqX(TypeExpr *t, const string &a, const string &b);
    string EqFn(TypeExpr *t);
    void EmitEqFixed(string &bo, TypeExpr *t);
    void EmitEqBytes(string &bo, TypeExpr *t);
    void EmitEqWalk(string &bo, TypeExpr *t, const string &pa, const string &pb, int depth);

    
    
    
    
    
    
    
    
    
    

    struct SpecInfo {
        string cname;
        vector<VarDef *> freevars;
        set<const VarDef *> globals; 
        bool needssp = false;
        bool hasrf = false;
        bool readsalltops = false;   
        bool located = false;        
        int cret = -1;               
    };
    unordered_map<FnSpec *, SpecInfo> sinfo;
    vector<FnSpec *> livespecs;

    
    
    
    
    
    struct StackCall {
        FnSpec *callee;
        int offset;
        Line line;
    };
    unordered_map<FnSpec *, vector<StackCall>> stackcalls;
    unordered_map<FnSpec *, int> stackown;
    
    
    
    unordered_map<FnSpec *, int64_t> stackneed;
    
    
    
    int globalregions = 0;
    unordered_map<FnSpec *, int> thunkregions;
    
    
    int64_t maxstacks = 0;
    void NoteStackCall(FnSpec *callee, Line ln) {
        stackcalls[curspec].push_back({ callee, stknext, ln });
    }
    void BoundStacks();
    int64_t ProgramStacks(const vector<FnSpec *> &roots);
    vector<FnSpec *> MainRoots();
    vector<pair<string, FnSpec *>> WorkerEntries();
    void CheckStackLimit(const string &what, int64_t stacks, Line ln);
    
    struct StackConfig {
        uint64_t reserve, gap, budget;
        int64_t maxstacks;
    };
    void StackReport(FILE *out, const StackConfig &cfg);

    
    unordered_map<FnSpec *, int> fromids;
    set<FnSpec *> fromemitted;

    bool IsPoolParam(FnSpec *sp, size_t i);
    
    
    static const char *FlEntrySize(const VarDef *d) {
        return d->reusable == RU_SLICES ? "GS_SPAN_NODE" : "8";
    }

    
    
    
    
    
    
    string HandedStacks(Node *n);
    string SyncReach(FnSpec *callee, const vector<string> &args, const vector<Node *> &an);
    void CollectSpecs();
    string SigParams(FnSpec *sp, bool decls, bool er = false);
    string SigRet(FnSpec *sp);
    
    
    
    vector<pair<string, string>> ParamFields(TypeExpr *t, bool pool);
    void PushArg(vector<string> &args, TypeExpr *t, bool pool, const string &v);
    
    
    
    string paramcopies;

    
    

    FnSpec *curspec = nullptr;
    SpecInfo *curinfo = nullptr;
    bool emiter = false;                 
    unordered_map<FnSpec *, string> ernames;   
    vector<FnSpec *> erqueue;
    string body;
    int ind = 1;
    int tmpn = 0;
    int stknext = 0, stkmax = 0;
    string spexpr;                       
    set<string> fnused;                  
    unordered_map<string, int> fnnextsuffix;
    unordered_map<const VarDef *, string> vnames;
    unordered_map<const VarDef *, string> vstk;    
    unordered_map<const VarDef *, pair<string, string>> vpool;  
    set<const VarDef *> fvptr;           

    
    
    
    
    
    struct NrvoDest {
        string stk;               
        string lenlv;             
        IntStorage ls = IS_U32;   
        bool prefix = false;      
        bool inlined = false;     
        bool fo = false;          
        string pref, hdr;         
    };
    unordered_map<const VarDef *, NrvoDest> nrvo;
    vector<string> fdstsaves;            

    
    
    
    unordered_map<string, int> openat;
    struct OpenAt {
        CodeGen &cg;
        string stk;
        OpenAt(CodeGen &cg, const string &stk) : cg(cg), stk(stk) { cg.openat[stk]++; }
        ~OpenAt() { cg.openat[stk]--; }
    };
    vector<string> dsttop0;              

    enum { SC_PLAIN, SC_FN, SC_LOOP, SC_BLOCK, SC_IB, SC_STMT };
    struct CScope {
        int kind;
        int stkbase;
        vector<pair<string, string>> saves;   
        
        
        
        
        vector<pair<size_t, Defer *>> defers;
        SFunction *ibsf = nullptr;            
        string brklbl, cntlbl;
        bool usedbrk = false, usedcnt = false;
        Dst dst;                              
        
        
        int open0 = 0;
        size_t topat = 0;
        int topind = 0;
        string top0;
        
        
        
        bool straight = false;
    };
    vector<CScope> cscopes;

    
    
    
    
    
    
    
    
    template<typename... Ts> void L(const Ts &...args) {
        body.append((size_t)ind * 4, ' ');
        Append(body, args...);
        body += '\n';
    }

    string T() { return cat("t", tmpn++); }
    string Lbl() { return cat("L", tmpn++); }

    string SpIdx(int k) { return cat("GS(", spexpr, " + ", k, ")"); }
    string SpTop() { return cat(spexpr, " + ", stknext); }   
    
    
    
    
    
    
    void NoStackAcrossCycleCall(FnSpec *callee, Line ln) {
        if (!curspec || !curspec->incycle || !callee->incycle || !stknext) return;
        auto cycle = [](FnSpec *s) { while (s->cyclelink) s = s->cyclelink; return s; };
        if (cycle(callee) != cycle(curspec)) return;
        Fail(ln, cat("internal: ", curspec->sf->name, " holds ", stknext, " data stack(s) "
                     "across its call into the recursive cycle through ", callee->sf->name,
                     " (§7.8)"));
    }
    
    
    
    
    
    bool LargeFixedOnStack(TypeExpr *t) {
        return IsLargeFixed(t) && !(curspec && curspec->incycle);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    vector<string> toporder;                  
    
    
    struct TopRegion { int stk, loop; };
    vector<TopRegion> growth;                
    vector<int> loopparent;                   
    vector<int> loopstack;                    
    
    
    struct TopCachePlan {
        vector<TopRegion> regions;
        vector<string> fnlocals;             
        string fnlens;                       
        bool IsRegion(int k, int id) const {
            for (auto r : regions) if (r.stk == k && r.loop == id) return true;
            return false;
        }
    };
    
    
    
    
    
    
    
    
    
    struct LenSlot {
        int stk;          
        string lenlv;     
        string root;      
    };
    vector<LenSlot> lenslots;
    void NoteLen(const string &stk, const string &lenlv, const string &component = {});
    static bool LenRoot(const string &lenlv, string &root);
    bool cachetops = false;             
    bool topown = false;                
    bool topglob = false;               
    bool topcap = false;                
    bool topdst = false;                
    bool reftops = false;               
    set<string> refstkexprs;            
    set<string> capstkexprs;            

    static constexpr const char *FLUSHMARK = "@@gsflush@@";
    static constexpr const char *RELOADMARK = "@@gsreload@@";
    static constexpr const char *LOOPMARK = "@@gsloop@@";
    static constexpr const char *TOPMARK = "@@gstop";

    set<string> gstkexprs;   

    bool CacheableStk(const string &stk);
    int TopIdx(const string &stk);
    string Top(const string &stk);
    string TopW(const string &stk);

    
    
    
    
    
    void MarkFlush(const string &reach = "*")  { if (markers) L(FLUSHMARK, reach); }
    void MarkReload(const string &reach = "*") { if (markers) L(RELOADMARK, reach); }

    bool markers = false;                
    
    
    vector<string> loopcons;
    static constexpr const char *BLOCKREGION = "{";
    bool InConsOf(const string &stk);
    int OpenRegion(const string &cons);
    int MarkLoopBegin();
    int MarkBlockBegin();
    void MarkLoopEnd(int id);
    int MarkConsBegin(const string &stk);
    struct ConsRegion {
        CodeGen &cg;
        int id;
        ConsRegion(CodeGen &cg, const string &stk) : cg(cg), id(cg.MarkConsBegin(stk)) {}
        ~ConsRegion() { cg.MarkLoopEnd(id); }
    };
    TopCachePlan PlanTopCaches();
    static bool LineIs(string_view s, const char *pfx);
    static string_view GotoTarget(string_view line);
    static string_view LabelHere(string_view line);
    static string_view NextLine(const string &b, size_t &i, size_t &ind0);
    string ExpandTopMarkers(const string &b, TopCachePlan &plan);
    string ExpandTopMarkers1(const string &b, TopCachePlan &plan, vector<bool> &lenok, bool &clean);
    string HoistAggregateDecls(string &b);
    vector<string> BraceFields(const string &ct);
    static vector<string> SplitBraceInit(const string &init);
    void PushSc(int kind);
    void EmitRestores(const CScope &s);
    void PopSc();
    void EmitExitRestores(int to);
    void RegisterDefer(Defer *d);
    void EmitDeferBody(Defer *d);
    string AllocStk(bool forlocal);
    void SaveBase(bool forlocal, const string &stk, const string &basevar);

    
    
    
    unordered_map<const VarDef *, string> gnames;
    unordered_map<const VarDef *, string> gstks;
    unordered_map<const VarDef *, pair<string, string>> gpools;

    
    
    
    
    
    
    vector<pair<const VarDef *, string>> poolbases;
    set<const VarDef *> poolglobals;   

    string PoolBase(const VarDef *pool);
    string PoolBaseOr(const VarDef *vd, const string &hdrbase);
    string LocalName(VarDef *vd);
    string VName(const VarDef *vd);

    
    
    
    

    
    
    
    
    struct Loc {
        string s;
        TypeExpr *t = nullptr;
        bool val = false;
        bool ispref = false;   
        
        
        
        bool viaref = false;
        string stk, lenlv, fl, flstk;
        string lenroot;         
        string hdr;            
        
        
        
        string hbase, hlen;
        
        
        const VarDef *aliasof = nullptr;
        Node *aliaspath = nullptr;
        VarDef *rootowner = nullptr; 
    };

    bool PrefVar(const VarDef *vd);

    
    
    
    
    
    
    
    
    unordered_map<const VarDef *, const VarDef *> refalias;
    
    
    unordered_map<const VarDef *, Node *> aliaspath;
    set<const VarDef *> aliasbound;          
    set<const VarDef *> capturedvars;        
    
    
    unordered_map<const VarDef *, VarDef *> knownrootparams;
    void FindKnownRootParams();
    
    
    set<const VarDef *> splitroots;
    void FindSplitRootParams();
    static string NamedRootDesc(const string &v);
    void PushRootMembers(vector<string> &args, TypeExpr *t, string v);
    void FindRefAliases(FnSpec *sp);
    const VarDef *AliasTarget(VarDef *d, Node *init, Node *&path);
    static bool HandedRef(Node *n);
    static Node *FieldChainRoot(Node *n, bool typed);

    
    
    

    bool NeedsDeref(TypeExpr *have, TypeExpr *want);
    Loc FatRefLoc(const string &x, TypeExpr *sub);
    Loc BytesLoc(const string &ptr, TypeExpr *t, const Loc &from);
    void RelParts(const Loc &lv, string &faddr, string &off);
    string RelOrigin(TypeExpr *rt, const string &faddr);
    string PointeeLv(const string &p, TypeExpr *t);
    void DerefLoc(Loc &lv);
    void PinLoc(Loc &lv);
    Loc VarLoc(VarDef *vd);
    string HdrLv(VarDef *vd);
    string VarCT(const VarDef *vd);
    string VStkOf(const VarDef *vd);
    string FieldPtr(const string &base, const vector<Field> &fields,
                    const vector<TypeExpr *> &ftypes, int fieldidx);

    
    struct ArrView {
        string elems;      
        string len;        
        string lenlv;      
        TypeExpr *elem = nullptr;
        bool typedelems = false;   
    };

    
    
    static const char *CopyFn() { return "gs_memcpy"; }

    ArrView ArrayView(const Loc &lv);
    ArrView RawArrayView(const Loc &lv);
    
    
    string ElemAddr(const ArrView &v, const string &i) {
        return v.typedelems ? cat(v.elems, " + ", i)
                            : cat("(", v.elems, ") + ", i, " * ", FixedSize(v.elem));
    }
    
    
    string LimitedCap(const Loc &lv) {
        return lv.val ? cat(ArrSize(lv.t->arr)) : cat("(int64_t)*(gs_u32 *)(", lv.s, ")");
    }

    
    
    
    
    
    
    
    unordered_map<const VarDef *, pair<string, string>> views;   
    
    
    map<pair<const VarDef *, vector<int>>, pair<string, string>> fieldviews;

    bool AddView(VarDef *vd);
    const VarDef *FieldRoot(const VarDef *vd);
    bool AddFieldView(const FieldPath &fp);
    void UseFieldView(Dot *d, Loc &lv);

    
    struct ViewScope {
        CodeGen &cg;
        vector<VarDef *> added;
        vector<pair<const VarDef *, vector<int>>> addedfields;
        ViewScope(CodeGen &_cg, const vector<VarDef *> &refs, const vector<FieldPath> &fields)
            : cg(_cg) {
            for (auto vd : refs) if (cg.AddView(vd)) added.push_back(vd);
            for (auto &fp : fields)
                if (cg.AddFieldView(fp)) addedfields.push_back({ cg.FieldRoot(fp.first), fp.second });
        }
        ~ViewScope() {
            for (auto vd : added) cg.views.erase(vd);
            for (auto &k : addedfields) cg.fieldviews.erase(k);
        }
    };

    string Snapshot(TypeExpr *t, const string &x);
    
    
    unordered_map<Node *, Loc> fillvalues;
    void FreezeFill(Node *n, TypeExpr *t, vector<Node *> &added);
    struct FillScope {
        CodeGen &cg;
        vector<Node *> added;
        FillScope(CodeGen &cg, Node *n, TypeExpr *t) : cg(cg) { cg.FreezeFill(n, t, added); }
        ~FillScope() { for (auto n : added) cg.fillvalues.erase(n); }
    };
    string GenPure(Node *n);
    string DivX(bool div, IntStorage s, const string &l, const string &r, Line line);
    string ArithX(TType op, TypeExpr *t, const string &l, const string &r, Line line,
                  bool nonneg = false, string lx = "", string rx = "");
    string IndexTypeCheck(const string &x, TypeExpr *t, Line ln);
    static string IntStr(int64_t v);
    static string FltStr(double v, bool f32);
    Loc IndexLoc(Loc lv, Node *idxnode, Line ln, bool nobc);
    Index *LeadingCheck(Node *arm);
    bool SamePath(Node *a, Node *b);
    bool SameCheck(Index *a, Index *b);
    void EmitHoistedCheck(Index *ta, Index *ea, const string &c);
    Loc GenLoc(Node *n);
    
    
    
    
    bool locnow = false;
    Loc GenLocNow(Node *n);
    bool QuietValue(Node *n);
    bool StableDest(Node *n);
    string BytesTemp(string &stk);
    string RzTemp(TypeExpr *t, string &stk);
    string RzLenLv(TypeExpr *t, const string &h);
    Loc RzTempLoc(TypeExpr *t, const string &h, const string &stk);
    Loc GenRzTmp(Node *n);
    Loc MemberLoc(Loc lv, Dot *d);
    Loc FieldLocAt(Loc lv, int fieldidx);

    
    
    
    
    

    static bool IsCtl(Node *n);
    string LoadLoc(Loc lv, TypeExpr *et, Line ln);
    string AdaptToFixed(Loc lv, TypeExpr *et, Line ln);
    void CopyIntoLimited(Loc lv, TypeExpr *et, Line ln, const string &dst, bool overlap);
    bool AdaptsToLimited(Node *n, TypeExpr *want);
    bool GenIntoLimited(Node *n, TypeExpr *want, const string &dst, bool overlap);
    string BytesAddrOf(const Loc &lv);
    string GenRefVal(Node *child, Line ln);
    string GenXD(Node *n, TypeExpr *want);
    string GenTruth(Node *n);
    bool SpeculativeForm(Binary *b);
    Binary *ChainOp(Node *n);
    bool RangeTest(Binary *b, Node *&x, Node *&y);
    InlineBlock *BranchCall(Node *n);
    bool BranchChain(Node *n);
    void GenBranch(Node *n, bool when, string lbl, int exitscope = -1);
    bool Speculatable(Node *n, bool idxok, bool truth, int &budget);
    bool SpeculatablePlace(Node *n, bool idxok, int &budget);

    
    
    string GenX(Node *n) {
        if (!substs.empty())
            if (auto s = substs.find(n); s != substs.end()) return s->second;
        if (auto it = fillvalues.find(n); it != fillvalues.end())
            return LoadLoc(it->second, it->second.t, n->line);
        auto x = n->CgX(*this);
        return Spills(n) && !IsCName(x) ? Snapshot(n->exprtype, x) : x;
    }
    
    
    
    
    
    
    
    static constexpr int SPILLDEPTH = 32;
    static bool NestsDeep(Node *n, int levels = SPILLDEPTH);
    bool Spills(Node *n);
    void GenAny(Node *n, Dst d) {
        if (fillvalues.count(n)) LeafAny(n, d);
        else n->CgAny(*this, d);
    }
    void GenStmt2(Node *n) { n->CgStmt(*this); }

    string CtlValX(Node *n);
    void LeafAny(Node *n, const Dst &d);
    string GenFixedArrayLit(ArrayLit *al);
    string GenPtr(Node *n, string *stkout = nullptr);

    
    
    
    

    unordered_map<string, string> strdata;   
    unordered_map<string, string> strval;                 

    string StrRaw(const string &v);
    string GenStrBytes(StrLit *s);
    map<string, string> blobdata;
    string BlobRaw(const string &v);

    
    

    TypeExpr *OperandT(TypeExpr *t);
    string GenVal(Node *n);
    string GenPureVal(Node *n);
    string GenOperandVal(Node *n, bool later);
    static bool IsCName(const string &x);
    bool HasStmts(Node *n);
    static bool EmitsStmts(Node *n);
    string GenEquality(TypeExpr *lt, const string &l, const string &r);
    string GenSliceEq(TypeExpr *st, const string &l, const string &r);
    string GenRangeEq(TypeExpr *elem, const string &ae, const string &an, const string &be,
                      const string &bn);
    void GenElemwiseInto(TypeExpr *t, TType op, Line line, const string &l,
                         const string &r, const string &dst,
                         bool lscalar = false, bool rscalar = false);
    void GenElemwiseNegInto(TypeExpr *t, Line line, const string &x, const string &dst);
    bool VecShape(TypeExpr *t, FltStorage &fs, int64_t &n);
    string VecType(FltStorage fs, int64_t n);
    string VecOperand(TypeExpr *t, FltStorage fs, const string &x, bool scalar);
    string VecLoad(const string &vt, int64_t n, const string &v, bool scalar);
    void GenElemwiseLeaves(TypeExpr *t, const string &dst,
                           const function<string(TypeExpr *, const string &)> &leaf);
    void ElemwiseOperands(Binary *b, string &l, string &r);
    string GenElemwise(Binary *b, const string &l, const string &r);
    string GenSlice(SliceExpr *se, const string &into = "");

    
    
    
    

    void Bump(const string &stk, const string &n) { L(TopW(stk), " += ", n, ";"); }

    void EmitValStore(const string &stk, TypeExpr *t, const string &x);
    void StoreWhole(const string &p, TypeExpr *t, const string &x);
    bool WholeStore(TypeExpr *t);
    void EmitLenCheck(IntStorage ls, const string &n);
    void EmitLenStore(const string &stk, IntStorage ls, const string &n);
    void EmitArrayCount(const string &stk, TypeExpr *at, const string &lenlv, const string &n,
                        bool check);
    void EmitVarintStore(const string &stk, const string &x);
    void EmitRelRangeCheck(TypeExpr *rt, const string &off, Line ln, bool inroot);
    RelFacts RelValue(Node *v);
    bool RelSlotApart(TypeExpr *rt, TypeExpr *holder, int64_t off);
    string RelOffset(TypeExpr *rt, const string &org, const string &rv, Line ln, RelFacts f);
    void EmitRelStoreAt(const string &fa, TypeExpr *rt, const string &rv, Line ln, bool inroot,
                        RelFacts f = {});
    void EmitRelStore(const string &stk, TypeExpr *rt, const string &rv, Line ln,
                      RelFacts f = {});
    void EmitRelSelfAt(const string &fa, TypeExpr *rt, int64_t fieldoff, Line ln,
                       bool inroot = true);
    void EmitRelSelfStore(const string &stk, TypeExpr *rt, int64_t fieldoff, Line ln);
    bool HasRelRef(TypeExpr *t);
    bool IsFlatFixed(TypeExpr *t);
    bool NeedsResultLocation(TypeExpr *t) { return IsFix(t) && t->kind != TY_REF && HasRelRef(t); }
    bool HasSelfRelRef(TypeExpr *t);
    void NoSelfRelCopy(Node *val);
    bool HasUninitSlots(TypeExpr *t);

    
    
    
    
    
    int64_t relrootmax = 0;

    void ComputeRelRootMax();
    void EmitCopyElems(const string &stk, TypeExpr *elem, const string &src, const string &n);

    
    
    
    struct SrcElems {
        string elems, n;
    };

    SrcElems GenSrcElems(Node *n);
    void GenConstruct(Node *n, const string &stk, TypeExpr *want = nullptr,
                      const string &lenlv = "");
    void ConstructFromLoc(Loc lv, TypeExpr *et, const string &stk, const string &lenlv, Line ln);
    bool CallBuildsAt(Call *c, TypeExpr *rt, TypeExpr *et, const string &lenlv);
    void ConstructCall(Call *c, TypeExpr *et, const string &stk, TypeExpr *want,
                       const string &lenlv);
    TypeExpr *AdtFrom(Node *n);
    void GenAdtAdapted(TypeExpr *from, TypeExpr *to, const Dst &d, Line ln,
                       const function<void(const Dst &)> &gen);
    Dst VariantBehindTag(TypeExpr *from, TypeExpr *to, const Dst &d);
    void GenCallAs(Call *c, TypeExpr *t, const Dst &d);
    void GenArrayFromLoc(Loc lv, TypeExpr *et, const string &stk, Line ln,
                         const string &lenlv = "");
    void EmitRzCopy(Loc lv, TypeExpr *et, const string &stk, const string &lenlv, Line ln);
    void FoBytes(TypeExpr *t, const string &obj, const string &p, bool tobytes);
    Loc FoView(const Loc &lv);
    void GenFoAsBytes(Node *n, const string &stk, TypeExpr *t, const string &lenlv);
    bool RzShape(TypeExpr *t, int64_t &prefix, TypeExpr *&elem);
    void GenVarEnumFromLoc(Loc lv, TypeExpr *et, const string &stk);
    void FixedLitAtStk(Node *n, const string &stk);
    void FixedLitAt(Node *n, const string &dst);
    void FixedLitAtLv(Node *n, const string &base, bool inroot);
    void FixedArrayLitAt(ArrayLit *al, const string &base, bool inroot);
    void StructLitAt(StructLit *sl, const string &base, bool inroot);
    void EmitValStoreTag(const string &stk, IntStorage ts, const string &x);
    
    
    static int64_t LitCount(ArrayLit *al) {
        if (!al->fillval) return (int64_t)al->elems.size();
        assert(Is<IntLit>(al->fillcount));
        return ((IntLit *)al->fillcount)->val;
    }
    void GenArrayLit(ArrayLit *al, const string &stk, const string &lenlv = "",
                     TypeExpr *as = nullptr);
    static bool HasSelfInit(StructLit *sl);
    void GenStructLit(StructLit *sl, const string &stk, const string &lenlv = "");
    void GenFrameObjLit(StructLit *sl, StructInst *si, const string &stk, const string &obj);
    void GenFieldInits(StructLit *sl, const vector<Field> &fields, const vector<TypeExpr *> &ftypes,
                       const string &stk, const string &lenlv = "", const string &selfbase = "");

    
    
    
    
    
    

    bool termjump = false;   

    void GenBlockInner(Block *b, Dst d, size_t first = 0, int exitscope = -1);
    size_t GenInlineArgs(Block *b);
    void GenStmt(Node *n);

    
    
    
    
    
    
    

    
    
    
    struct OpenedLoop { int si = -1, loopid = -1; vector<string> divisors; };
    OpenedLoop OpenLoop(Block *bodyb, Node *cond, const vector<const VarDef *> &binders,
                        const Dst &d);
    void CloseLoop(const OpenedLoop &lp);
    void GenLoopBody(const function<void()> &condexit, Block *bodyb, Dst d,
                     const string &forhead = "", Node *cond = nullptr, size_t first = 0,
                     vector<const VarDef *> binders = {});
    static bool StraightCode(Node *n);
    bool InStraightLoop();
    static bool CallsRecursive(Node *n);

    
    
    
    
    struct DivMagic { string val, magic, more; bool narrow, zeronone; };
    unordered_map<string, DivMagic> divmagic;
    
    
    set<const VarDef *> refdlocals;
    FnSpec *refdspec = nullptr;
    
    
    
    
    struct Division { TType op; TypeExpr *t; Node *divisor; bool nonneg; };
    static bool AsDivision(Node *n, Division &dv);
    bool LoopDivisible(const Division &dv);
    static bool NarrowDivision(const Division &dv);
    string DivisionKey(const Division &dv, const VarDef **root = nullptr, bool *path = nullptr);
    string FixedDivX(const Division &dv, const string &l, const function<string()> &plain,
                     Line line);
    string DivisorKey(Node *n, const VarDef **root = nullptr, bool *path = nullptr);
    string DivisorValue(Node *n, string &ok);
    const set<const VarDef *> &RefdLocals();
    vector<string> HoistDivisors(Block *loopbody, Node *cond, const vector<const VarDef *> &binders);
    void GenBreakPath(Node *val);

    
    
    
    
    
    struct NameScope {
        CodeGen &cg;
        unordered_map<const VarDef *, string> vnames, vstk;
        unordered_map<const VarDef *, pair<string, string>> vpool;
        explicit NameScope(CodeGen &_cg)
            : cg(_cg), vnames(_cg.vnames), vstk(_cg.vstk), vpool(_cg.vpool) {}
        ~NameScope() {
            cg.vnames = std::move(vnames);
            cg.vstk = std::move(vstk);
            cg.vpool = std::move(vpool);
        }
    };
    
    static constexpr int SUMBLOCK = 8;
    
    
    
    static constexpr int64_t MAXTRIPBOUND = 8;
    static constexpr int MAXTRIPBODY = 48;
    
    
    unordered_map<const Node *, string> substs;
    bool DupLocal(VarDef *v);
    bool Dupable(Node *n);
    int BlockSize(ForLoop *f);
    string AtLeastLeft(TypeExpr *ct, const string &hi, const string &ctr, int k, bool fromzero);
    void GenBodyCopy(Block *bodyb, int si, size_t first = 0);
    void GenBlocked(ForLoop *f, int k, const string &more, const string &step,
                    const function<void()> &bind, const string &cond);

    
    

    void EnterDst(int si, const Dst &d);
    void DeclareTop0(size_t at, int indent, const string &name, const string &stk);
    string ScopeTop0(int si);
    string DstTop0(size_t i);
    const NrvoDest *NrvoBoundAt(Node *val, const string &stk, const string &lenlv);
    string ExitStart(Node *val, const string &stk, const string &lenlv, int open0);
    void LandValue(const string &stk, const string &top0, const string &start, TypeExpr *t,
                   const string &lenlv);
    void GenExitValue(Node *val, int si);

    
    

    void BindLocal(VarDef *d, Node *init, bool forlocal = true);
    Dst DeclStackLocal(VarDef *d, bool forlocal);
    string GenPrefVal(Node *n);
    string Unique2(const string &base);
    void GenRelAssign(Loc lv, Node *lval, Node *rhs, Line ln);
    void GenRebind(Assign *a, Loc lv);

    
    
    

    vector<string> GenForward(Call *c, const vector<TypeExpr *> &rets, const vector<Dst> &chans);
    void GenNormalReturn(const vector<Node *> &vals);

    
    
    int64_t PrefixBytes(IntStorage ls) { return ls == IS_VARINT ? 1 : IntSize(ls); }

    void EmitPrefixPatch(const string &pref, IntStorage ls, const string &stk, const string &count,
                         const string &elems);
    void EmitNrvoFinish(const NrvoDest &nd);
    void Epilogue(const string &retv);
    void PropagateReturn(const string &rfval);
    void GenFromReturn(Return *r);

    void EnsureFromChannels(FnSpec *t);

    
    
    
    
    

    string CallVal0(Call *c, const string &r0, TypeExpr *want = nullptr);
    Loc CallResLoc(Call *c, const string &r0);
    string BytesResultBase(const Dst &dd, string &stk);
    void EmitSlidePrefix(const string &base, IntStorage ls, const string &stk, const string &lenlv);
    vector<string> EmitCall(Call *c, Dst d0, vector<Dst> *alldst = nullptr);
    void EmitCallInto(Call *c, vector<Dst> &dsts);
    vector<string> EmitExternCall(Call *c, FnSpec *sp);
    string ExternProto(FnSpec *sp);
    vector<Node *> CallArgNodes(Call *c, size_t nparams);
    void EmitArg(FnSpec *sp, size_t i, Node *node, vector<string> &args, bool later);
    void EmitFvArg(const VarDef *fv, vector<string> &args);
    vector<string> EmitSpecCall(Call *c, FnSpec *sp, Dst d0, vector<Dst> *alldst);
    bool NeedsReprefix(const Dst &d, TypeExpr *rt);
    void EmitReprefix(FnSpec *sp, const Dst &dd, const string &base, const string &cnt);
    void EmitRfCheck(FnSpec *callee);
    vector<string> EmitFvCall(Call *c, Dst d0);
    vector<string> EmitDispatch(Call *c, Dst d0, vector<Dst> *alldst);

    
    
    

    unordered_map<string, string> queues;   

    string QueueFor(TypeExpr *t);
    Loc RecvLoc(Node *n);
    string LenCast(const Loc &lv);
    vector<string> EmitBuiltin(Call *c, Dst d0);

    
    
    
    
    
    

    TypeExpr *growu8 = nullptr;
    TypeExpr *GrowU8();
    Loc TempBuilder();
    Call *FmtContext(Call *c, Node *arg);
    FnSpec *FmtSpecFor(Call *c, TypeExpr *t);
    bool SimpleText(Call *c, TypeExpr *t);
    void RenderLit(Loc &out, const string &text);
    void RenderN(Loc &out, const string &nexpr);
    void RenderLoc(Loc &out, Loc lv, TypeExpr *t, bool nested, Call *c, Line ln);
    void RenderVariant(Loc &out, Loc lv, TypeExpr *t, Call *c, Line ln);
    string RefArg(const Loc &lv, TypeExpr *sub, Line ln);
    string PrefArg(const Loc &lv, Line ln);
    void EmitUserFormat(Loc &out, Loc lv, FnSpec *sp, Line ln);

    
    
    
    
    
    struct RenderFnReq {
        TypeExpr *t = nullptr;
        Line ln;
        string sig;
    };
    unordered_map<string, string> renderfns;   
    vector<RenderFnReq> renderqueue;
    vector<TypeExpr *> rendering;   
    string RenderFn(TypeExpr *t, Line ln);
    void EmitRenderCall(Loc &out, const Loc &lv, TypeExpr *t, Line ln);
    void EmitRenderFn(RenderFnReq r);
    Loc RenderedLoc(Node *a);
    Loc RenderToTemp(Node *a, Call *c);
    string FmtCall(Node *a, const string &dst);
    void EmitFormatInto(Loc lv, Node *a, Line ln, Call *c);
    vector<string> EmitStr(Call *c, vector<Node *> &an, Dst d0, Line ln);
    void EmitLeCheck(Line ln);
    void PayloadOf(Node *n, string &src, string &sz);
    void AppendBytes(const Loc &lv, const string &src, const string &n, Line ln);
    vector<string> EmitBytesOf(Call *c, vector<Node *> &an, Line ln);
    vector<string> EmitToBytes(vector<Node *> &an, Dst d0, Line ln);
    vector<string> EmitFromBytes(Call *c, vector<Node *> &an, Dst d0, Line ln);

    
    
    
    
    struct RzDest {
        string stk, lenlv, hdr, pref, elems;
        IntStorage ls = IS_U32;
    };

    RzDest OpenRzDest(TypeExpr *t, Dst d0, Line ln, const char *what);
    void CloseRzDest(RzDest &rd, const string &count);
    string NewElemResult(const string &e, TypeExpr *elem, const Loc &lv, Line ln);
    vector<string> EmitPush(vector<Node *> &an, Line ln);
    string ElemValue(Node *n, TypeExpr *elem);
    void StoreElemAt(Node *n, TypeExpr *elem, const string &e, const string &ev, Line ln);
    bool LiteralBytes(Node *n, TypeExpr *t, vector<uint8_t> &out);
    bool UniformFillByte(Node *n, TypeExpr *t, int &byte);
    void EmitAppend(vector<Node *> &an, Line ln);
    vector<string> EmitAlloc(Call *c, vector<Node *> &an, Line ln);
    vector<string> EmitSlicePool(Call *c, vector<Node *> &an, Line ln);
    string SpanArgs(const Loc &lv);
    string PlaceRun(const Loc &lv, const string &n);
    string SliceLen(Node *n, TypeExpr *elem, Line ln, const char *error = "GS_E_SLICELEN",
                    Node *msg = nullptr);
    string FailCall(const char *error, Node *msg, Line ln);
    void EmitSliceExtend(const Loc &lv, const string &end, int64_t esz);
    void EmitDefaultElems(const ArrView &v, const string &first, const string &count, Node *init);
    bool BulkDefaultLoop(ForLoop *loop);

    
    
    unordered_map<FnSpec *, string> thunks;

    vector<string> EmitThreadSpawn(Call *c, vector<Node *> &an);
    string EnsureThreadThunk(FnSpec *sp);
    void EmitThreadThunk(FnSpec *sp, int64_t stacks);
    
    
    void EmitRzImage(Loc src, TypeExpr *t, const string &stk, Line ln);

    
    

    void DetectNrvo(FnSpec *sp);
    
    static const char *FnAttrs(FnSpec *sp) {
        return sp->outofline ? "GS_NOINLINE " : sp->cinline ? "GS_INLINE " : "";
    }
    const VarDef *NamedResult(Block *fnbody, SFunction *target, size_t nrets, size_t resultidx);
    const VarDef *OpenIbNrvo(InlineBlock *ib, const Dst &d);
    void PlanTopClasses(FnSpec *sp);
    bool RefTopsOk(FnSpec *sp, bool farreturn);
    void ResetFnState();
    string EnsureEr(FnSpec *sp);
    void EmitSpec(FnSpec *sp, bool er = false);
    
    
    static constexpr int SIMD_LEVELS = 2;
    void EmitSimdVersions(FnSpec *sp, const string &name, const string &params,
                          const string &fnbody);

    
    
    

    
    
    
    map<const VarDef *, Node *> gstatic;
    
    
    
    set<const VarDef *> gshared;

    
    
    
    
    
    static constexpr int64_t MAXSTATICELEMS = 256;

    bool StaticInitX(Node *n, TypeExpr *t, string &out);
    void EmitGlobalDecls();
    void InitGlobalValue(VarDef *d, Node *init);
    void EmitGlobalInit();
    string GlobalLenLv(VarDef *d);
    void InitGlobalStack(VarDef *d);
    void EmitProgramInit();
    void EmitExports();
    string MainCall();
    void EmitMain();

    
    

    
    
    
    string head;     
    string result;   
    string ExportHeader();

    
    
    
    CodeGen(Ast &_ast, string_view runtime_ext_text, const vector<string> &headers,
            bool _norfcheck = false, bool _library = false, int64_t _maxstacks = 0)
        : ast(_ast), library(_library), norfcheck(_norfcheck), maxstacks(_maxstacks) {
        for (auto t : ast.alltypes)
            if (t->kind == TY_REF && t->ref->pool) poolglobals.insert(t->ref->pool);
        ComputeRelRootMax();
        CollectSpecs();
        for (auto sp : livespecs)
            for (auto fv : sinfo[sp].freevars) capturedvars.insert(fv);
        FindKnownRootParams();
        FindSplitRootParams();
        
        
        if (!fromids.empty()) data += "static GS_TLS int32_t gs_rf;\n";
        EmitGlobalDecls();
        
        for (auto sp : livespecs) {
            auto ret = SigRet(sp), params = SigParams(sp, false);
            Append(protos, "static ", FnAttrs(sp), ret, " ", sinfo[sp].cname, "(", params, ");\n");
        }
        for (auto sp : livespecs) EmitSpec(sp);
        EmitGlobalInit();
        
        
        for (size_t i = 0; i < erqueue.size(); i++) EmitSpec(erqueue[i], true);
        
        for (size_t i = 0; i < renderqueue.size(); i++) EmitRenderFn(renderqueue[i]);
        EmitProgramInit();
        EmitExports();
        if (!library) EmitMain();
        if (usesthreads) predefs = "#define GS_NEED_THREADS 1\n";
        
        
        Append(data, "typedef struct {\n", gblock.empty() ? "    uint8_t gs_none;\n" : gblock,
               "} gs_globals_t;\nstatic gs_globals_t gs_globals_main;\n",
               usesthreads ? "#define GS_GL ((gs_globals_t *)gs_gl)\n"
                           : "#define GS_GL (&gs_globals_main)\n");
        
        
        
        
        
        for (auto sp : ast.fnspecs) {
            if (!sp->sf->isextern) continue;
            for (auto t : sp->argtypes) {
                CT(t);
                
                
                if (t->kind == TY_REF && !IsResz(t->ref->sub)) CT(t->ref->sub);
            }
            for (auto t : sp->rets) CT(t);
        }
        string externs;
        if (!usedexterns.empty() || !runtime_ext_text.empty()) {
            EmitCoreTypes();
            CT(ast.SliceOf(ast.inttypes[IS_U8], Line {}));
        }
        for (auto sp : usedexterns) {
            if (sp->sf->dylib.empty() &&
                runtime_ext_text.find(cat(" ", sp->sf->cname, "(")) != string_view::npos)
                continue;
            externs += ExternProto(sp);
        }
        
        
        
        
        auto dllibs = DlLibs();
        if (!dllibs.empty()) {
            auto spelling = [&](const string &s) { return s.empty() ? string("0") : CStr(s); };
            externs += "static const gs_dllib gs_dllibs[] = {\n";
            for (auto &lib : dllibs)
                Append(externs, "    { ", CStr(lib.name), ", ", spelling(lib.onwindows), ", ",
                       spelling(lib.onmacos), ", ", spelling(lib.onlinux), " },\n");
            externs += "};\nstatic const gs_dlbind gs_dlbinds[] = {\n";
            size_t n = 0;
            for (auto sp : usedexterns) {
                if (sp->sf->dylib.empty()) continue;
                assert(++n <= dlbinds && "an extern bound after gs_program_init was emitted");
                auto d = DlSpelling(sp);
                auto lib = std::find_if(dllibs.begin(), dllibs.end(),
                                        [&](auto &l) { return SameDylib(l, d); }) - dllibs.begin();
                Append(externs, "    { ", lib, ", ", CStr(sp->sf->cname), ", (void **)&",
                       DlSlot(sp), " },\n");
            }
            externs += "};\n";
        }
        string includes;
        for (auto inc : headers) {
            
            
            
            for (auto &c : inc) if (c == '\\') c = '/';
            Append(includes, "#include \"", inc, "\"\n");
        }
        Append(head, "\n/* ---- types ---- */\n#pragma pack(push, 1)\n", tdecls, pdata,
               "#pragma pack(pop)\n\n/* ---- data ---- */\n", data);
        Append(result, "\n/* ---- includes ---- */\n", includes,
               "\n/* ---- extern prototypes ---- */\n", externs,
               "\n/* ---- prototypes ---- */\n", protos, "\n/* ---- code ---- */\n", code);
    }
};

}  
