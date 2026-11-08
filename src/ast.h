


#pragma once

namespace faloya {

struct Node;
struct Block;
struct Break;
struct FunVal;
struct TypeExpr;
struct SFunction;
struct SStruct;
struct SEnum;
struct SVariant;
struct SAlias;
struct Ast;
struct VarDef;
struct FnSpec;
struct StructInst;
struct EnumInst;
struct TypeCheck;
struct Optimizer;
struct Inliner;
struct BCE;
struct CodeGen;
struct Dst;
struct ExpansionClone;






struct Line {
    int line = 0;
    int fileidx = 0;
    int col = 0;
};


inline DiagLoc LocOf(Line l) { return { l.fileidx, l.line, l.col, 0, 0 }; }










enum TypeKind {
    TY_INT,         
    TY_FLT,         
    TY_BOOL,
    TY_STRUCT,      
    TY_ENUM,        
    TY_GENERIC,     
    TY_UNRESOLVED,  
    TY_FN,          
    TY_ARRAY,       
    TY_SLICE,       
    TY_REF,         
    TY_VARIANT,     
    TY_VOID,        
};

enum ArrayKind {
    A_FIXED,       
    A_VAR,         
    A_LIMITED,     
    A_GROW,        
    A_GROWSHRINK,  
};



enum IntStorage { IS_I8, IS_I16, IS_I32, IS_I64, IS_U8, IS_U16, IS_U32, IS_U64, IS_VARINT };

enum FltStorage { FS_F32, FS_F64 };

inline const char *IntStorageName(int s) {
    static const char *names[] = { "i8", "i16", "i32", "i64",
                                   "u8", "u16", "u32", "u64", "varint" };
    return names[s];
}

inline const char *FltStorageName(int s) {
    static const char *names[] = { "f32", "f64" };
    return names[s];
}

inline bool IsUnsigned(IntStorage s) { return s >= IS_U8 && s <= IS_U64; }
inline int IntBits(IntStorage s) {
    switch (s) {
        case IS_I8: case IS_U8:   return 8;
        case IS_I16: case IS_U16: return 16;
        case IS_I32: case IS_U32: return 32;
        default:                  return 64;
    }
}





inline pair<int64_t, int64_t> IntRange(IntStorage s) {
    switch (s) {
        case IS_I8:  return { -128, 127 };
        case IS_I16: return { -32768, 32767 };
        case IS_I32: return { INT32_MIN, INT32_MAX };
        case IS_U8:  return { 0, 255 };
        case IS_U16: return { 0, 65535 };
        case IS_U32: return { 0, (int64_t)UINT32_MAX };
        case IS_U64: return { 0, INT64_MAX };
        default:     return { INT64_MIN, INT64_MAX };
    }
}



inline bool FitsIntStorage(int64_t v, bool uns, IntStorage s) {
    if (uns) return s == IS_U64;   
    auto [lo, hi] = IntRange(s);
    return v >= lo && v <= hi;
}



inline int64_t WrapStorage(int64_t v, IntStorage s) {
    switch (s) {
        case IS_I8:  return (int8_t)v;   case IS_U8:  return (uint8_t)v;
        case IS_I16: return (int16_t)v;  case IS_U16: return (uint16_t)v;
        case IS_I32: return (int32_t)v;  case IS_U32: return (int64_t)(uint32_t)v;
        default:     return v;
    }
}


inline bool AddOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a + (uint64_t)b);
    return ((a ^ r) & (b ^ r)) < 0;
}

inline bool SubOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a - (uint64_t)b);
    return ((a ^ b) & (a ^ r)) < 0;
}

inline bool MulOv(int64_t a, int64_t b, int64_t &r) {
    r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a == 0 || b == 0) return false;
    if (a == -1) return b == INT64_MIN;
    if (b == -1) return a == INT64_MIN;
    return r / b != a;
}






inline int64_t EuclidMod(int64_t a, int64_t b) {
    if (b == -1) return 0;
    auto r = a % b;
    if (r < 0) r = (int64_t)((uint64_t)r + (b < 0 ? 0u - (uint64_t)b : (uint64_t)b));
    return r;
}








inline bool FoldIntOp(TType op, int64_t la, int64_t rb, IntStorage s, int64_t &out) {
    auto bits = IntBits(s);
    if (IsUnsigned(s)) {
        auto a = (uint64_t)la, b = (uint64_t)rb;
        auto max = bits == 64 ? UINT64_MAX : (1ull << bits) - 1;
        uint64_t r = 0;
        switch (op) {
            case T_PLUS:   r = a + b; break;
            case T_MINUS:  r = a - b; break;
            case T_MUL:    r = a * b; break;
            case T_DIV:    if (!b) return false; r = a / b; break;
            case T_MOD:    if (!b) return false; r = a % b; break;
            case T_BITAND: r = a & b; break;
            case T_BITOR:  r = a | b; break;
            case T_XOR:    r = a ^ b; break;
            case T_SHL:    r = a << (b & (bits - 1)); break;
            case T_SHR:    r = (a & max) >> (b & (bits - 1)); break;
            default:       return false;
        }
        out = WrapStorage((int64_t)r, s);
        return true;
    }
    auto a = la, b = rb;
    int64_t r = 0;
    switch (op) {
        case T_PLUS:   if (AddOv(a, b, r)) return false; break;
        case T_MINUS:  if (SubOv(a, b, r)) return false; break;
        case T_MUL:    if (MulOv(a, b, r)) return false; break;
        case T_DIV:
            if (!b || (a == INT64_MIN && b == -1)) return false;
            r = a / b;
            break;
        case T_MOD:    if (!b) return false; r = EuclidMod(a, b); break;
        case T_BITAND: r = a & b; break;
        case T_BITOR:  r = a | b; break;
        case T_XOR:    r = a ^ b; break;
        case T_SHL:
            out = WrapStorage((int64_t)((uint64_t)a << (b & (bits - 1))), s);
            return true;
        case T_SHR:    r = a >> (b & (bits - 1)); break;
        default:       return false;
    }
    if (!FitsIntStorage(r, false, s)) return false;
    out = r;
    return true;
}



struct TypeDetail { virtual ~TypeDetail() {} };

struct TypeStruct : TypeDetail {     
    SStruct *st = nullptr;
    vector<TypeExpr *> args;         
    StructInst *inst = nullptr;      
};

struct TypeEnum : TypeDetail {       
    SEnum *en = nullptr;
    vector<TypeExpr *> args;
    bool varmode = false;            
    EnumInst *inst = nullptr;        
};

struct TypeName : TypeDetail {       
    int expansionbinding = 0;       
    string_view expansionerror;     
    string_view name;                
    string_view ns;                  
                                     
                                     
    vector<TypeExpr *> args;         
    bool varmode = false;            
    
    
    
    bool opaque = false;
};

struct TypeFn : TypeDetail {         
    vector<TypeExpr *> args;         
    vector<TypeExpr *> rets;
    bool has_sig = false;            
};

struct TypeArray : TypeDetail {      
    TypeExpr *sub = nullptr;         
    ArrayKind akind = A_FIXED;
    Node *sizeexpr = nullptr;        
    int lenstorage = -1;             
    int64_t size = -1;               
};

struct TypeRef : TypeDetail {        
    TypeExpr *sub = nullptr;         
    int lenstorage = -1;             
    bool optional = false;           
    
    
    
    
    string_view poolname;
    string_view poolns;              
    VarDef *pool = nullptr;
};

struct TypeVariant : TypeDetail {    
    TypeExpr *adt = nullptr;         
    string_view name;                
    SVariant *variant = nullptr;     
};






struct TypeIndex : TypeDetail {
    string_view name;
    string_view ns;                  
    VarDef *arr = nullptr;
    bool upto = false;
};

struct TypeExpr {
    TypeKind kind;
    Line line;
    const char *srcbegin = nullptr;
    const char *srcend = nullptr;
    
    
    
    bool cq = false;
    
    
    
    bool aliasuse = false;
    
    
    TypeIndex *ix = nullptr;
    union {                          
        IntStorage intstorage;       
        FltStorage fltstorage;       
        TypeExpr *sub;               
        TypeStruct *struc;           
        TypeEnum *enu;               
        TypeName *named;             
        TypeFn *fn;                  
        TypeArray *arr;              
        TypeRef *ref;                
        TypeVariant *var;            
    };                               

    TypeExpr(TypeKind _kind, Line _line) : kind(_kind), line(_line) { named = nullptr; }
    
    
    void Dump(string &s, size_t limit = string::npos) const;
};


inline bool IsRefOrSlice(const TypeExpr *t) { return t->kind == TY_REF || t->kind == TY_SLICE; }
inline bool IsOptional(const TypeExpr *t) { return t->kind == TY_REF && t->ref->optional; }
inline bool IsOpaque(const TypeExpr *t) { return t && t->kind == TY_GENERIC && t->named->opaque; }

inline bool IsPlainRef(const TypeExpr *t) {
    return t->kind == TY_REF && !t->ref->optional && t->ref->lenstorage < 0;
}

inline bool IsRelRef(const TypeExpr *t) { return t->kind == TY_REF && t->ref->lenstorage >= 0; }

inline bool IsSelfRelRef(const TypeExpr *t) { return IsRelRef(t) && !t->ref->pool; }
inline bool IsArrayKind(const TypeExpr *t, ArrayKind k) {
    return t->kind == TY_ARRAY && t->arr->akind == k;
}




inline bool CarriesPool(const TypeExpr *t) {
    return IsPlainRef(t) && IsArrayKind(t->ref->sub, A_GROW);
}

inline bool IsIntT(const TypeExpr *t) { return t->kind == TY_INT && t->intstorage != IS_VARINT; }
inline bool IsVarintT(const TypeExpr *t) {
    return t && t->kind == TY_INT && t->intstorage == IS_VARINT;
}


inline bool IsU8(const TypeExpr *t) {
    return t->kind == TY_INT && t->intstorage == IS_U8 && !t->ix;
}

inline bool IsIndexT(const TypeExpr *t) { return t && t->kind == TY_INT && t->ix; }
inline bool IsF32(const TypeExpr *t) { return t && t->kind == TY_FLT && t->fltstorage == FS_F32; }

inline TypeExpr *AdtOf(TypeExpr *t) {
    if (t->kind == TY_VARIANT) return t->var->adt->kind == TY_ENUM ? t->var->adt : nullptr;
    return t->kind == TY_ENUM ? t : nullptr;
}


inline bool IsPrimTypeToken(TType t) { return t >= T_TBOOL && t <= T_TF64; }

struct GenericParam {
    string_view name;
    Line line;
    TypeExpr *bound = nullptr;  
};

struct Param {
    string_view name;
    Line line;                  
    TypeExpr *type = nullptr;   
    Node *defaultval = nullptr; 
    bool isvar = false;
};

struct Field {
    string_view name;
    Line line;                  
    string_view doc;            
    TypeExpr *type = nullptr;
    Node *defaultval = nullptr;
    bool isconst = false;       
    bool ispad = false;         
    int64_t padsize = -1;
};

struct FieldInit {
    string_view name;           
    Node *val = nullptr;
    bool fromdefault = false;   
    int expansionbinding = 0;
    TypeExpr *accessorowner = nullptr;
};

enum PatKind { P_WILDCARD, P_LIST };




struct PatItem {
    Node *lo = nullptr;
    Node *hi = nullptr;
    
    
    TypeExpr *variantowner = nullptr;
};

struct Pattern {
    PatKind kind = P_WILDCARD;
    vector<PatItem> items;      
    string_view binder;         
    bool byref = false;         
};


struct ArmRange {
    int64_t lo = 0, hi = 0;
};

struct MatchArm {
    Pattern pat;
    Node *body = nullptr;
    
    
    
    Node *expansion = nullptr;
    bool generated = false;         
    
    vector<SVariant *> variants;    
    VarDef *binder = nullptr;       
    vector<ArmRange> ranges;        
};




struct FnValBind {
    const FunVal *fv = nullptr;
    SFunction *named = nullptr;
    FnSpec *env = nullptr;
    
    
    const vector<SFunction *> *set = nullptr;
    bool operator==(const FnValBind &o) const {
        return fv == o.fv && named == o.named && env == o.env && set == o.set;
    }
};




enum { RU_SLOTS = 1, RU_SLICES = 2 };





struct RootAlt {
    VarDef *root = nullptr;      
    
    
    
    bool exact = false;
    
    
    
    
    VarDef *from = nullptr;
    
    
    
    
    
    
    
    bool slotread = false;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    bool classread = false;
    
    
    
    
    bool projected = false;
    bool operator==(const RootAlt &) const = default;
};









struct Roots {
    small_vector<RootAlt, 1> alts;
    
    
    
    
    
    
    bool unknown = false;

    
    
    
    
    enum class Compare { All, Cycle, GlobalReach };
    bool Same(const Roots &o, Compare how = Compare::All) const {
        if (alts.size() != o.alts.size() || (how != Compare::GlobalReach && unknown != o.unknown))
            return false;
        auto key = [how](RootAlt a) {
            if (how == Compare::Cycle) a.from = nullptr;
            if (how == Compare::GlobalReach) a.slotread = a.classread = a.projected = false;
            return a;
        };
        for (auto &a : alts)
            if (none_of(o.alts.begin(), o.alts.end(), [&](const RootAlt &b) {
                    return key(a) == key(b);
                }))
                return false;
        return true;
    }
    bool operator==(const Roots &o) const { return Same(o); }

    bool None() const { return alts.empty(); }
    
    
    
    
    bool OnlyStatic() const {
        for (auto &a : alts) if (a.root || !a.exact) return false;
        return true;
    }
    bool Unknown() const { return alts.empty() && unknown; }
    void Clear() {
        alts.clear();
        unknown = false;
    }
    void SetUnknown() {
        alts.clear();
        unknown = true;
    }
    void Set(VarDef *r, bool exact, VarDef *from = nullptr, bool slotread = false) {
        alts.clear();
        unknown = false;
        alts.push_back({ r, exact, from, slotread });
    }
    
    void TakeAlts(const Roots &o) {
        alts = o.alts;
        unknown = o.unknown;
    }
    
    
    
    bool Add(const RootAlt &a) {
        for (auto &b : alts) {
            if (b.root != a.root) continue;
            auto before = b;
            b.exact = b.exact && a.exact;
            b.slotread = b.slotread && a.slotread;
            b.classread = b.classread && a.classread;
            b.projected = b.projected && a.projected;
            if (b.from != a.from) b.from = nullptr;
            return b != before;
        }
        alts.push_back(a);
        return true;
    }
    bool Add(const Roots &o) {
        auto changed = !unknown && o.unknown;
        unknown = unknown || o.unknown;
        for (auto &a : o.alts) changed = Add(a) || changed;
        return changed;
    }
    
    
    void Weaken() {
        for (auto &a : alts) {
            a.exact = false;
            a.classread = false;
            a.projected = false;
        }
    }
    
    
    
    void ClearReads() {
        for (auto &a : alts) {
            a.slotread = false;
            a.classread = false;
        }
    }
    
    bool Exact() const { return alts.size() == 1 && alts[0].exact; }
    
    
    
    VarDef *Root() const;
    
    VarDef *From() const;
    
    bool AllSlotRead() const {
        if (alts.empty()) return false;
        for (auto &a : alts) if (!a.slotread) return false;
        return true;
    }
    bool Has(const VarDef *r) const {
        for (auto &a : alts) if (a.root == r) return true;
        return false;
    }
    template<typename F> bool Any(F f) const {
        for (auto &a : alts) if (f(a)) return true;
        return false;
    }
    template<typename F> bool All(F f) const {
        for (auto &a : alts) if (!f(a)) return false;
        return true;
    }
};





struct Prov : Roots {
    bool writable = false;       
    
    
    
    
    bool byteview = false;
    
    
    
    
    bool freshview = false;
    int reusable = 0;            
    
    
    
    
    
    
    
    TypeExpr *reached = nullptr;
    bool operator==(const Prov &) const = default;
    void SetProv(const Prov &p) { *this = p; }
    const Roots &AsRoots() const { return *this; }
};





enum ConstKind { CK_NONE, CK_INT, CK_FLT };
struct Val : Prov {
    TypeExpr *type = nullptr;
    ConstKind ck = CK_NONE;
    int64_t ival = 0;
    bool uns = false;            
    double fval = 0;
    bool strlit = false;         
    bool emptyarr = false;       
    bool isnull = false;         
    
    
    
    bool unsized = false;
    VarDef *unsizedparam = nullptr;
    
    
    
    
    
    bool litfloat = false;
    
    
    
    
    bool litint = false;
    
    
    
    
    bool litelems = false;
    int64_t litlo = 0, lithi = 0;
    
    
    
    
    
    
    bool flexint = false;
    
    
    bool notconst = false;
    bool lvalue = false;         
    
    
    
    
    bool isvarint = false;
    
    
    bool pointee = false;
    
    
    
    
    
    
    
    
    Prov slot;
    bool hasslot = false;
    
    
    
    
    Prov held;
    bool hasheld = false;
    
    
    
    
    
    
    
    bool storagebranches = false;
    Node *implicitcopy = nullptr;
    vector<Node *> refcopies;
    
    
    
    
    
    
    
    
    bool joinslice = false;
    bool joinhasslice = false;
    Node *joinat = nullptr;
    TypeExpr *joina = nullptr;
    TypeExpr *joinb = nullptr;
    
    
    
    
    bool nonneg = false;
    
    
    
    VarDef *nonnegfrom = nullptr;
    
    
    
    
    VarDef *constfrom = nullptr;
    
    
    
    
    Roots contents;
    bool holderset = false;
    
    
    
    VarDef *holderfrom = nullptr;
    FnValBind fnv;               
};







enum NodeKind : uint8_t {
    NK_IntLit,
    NK_FltLit,
    NK_BoolLit,
    NK_StrLit,
    NK_Ident,
    NK_ArrayLit,
    NK_StructLit,
    NK_Unary,
    NK_Binary,
    NK_Dot,
    NK_Call,
    NK_Index,
    NK_SliceExpr,
    NK_AsCast,
    NK_NullLit,
    NK_SelfRef,
    NK_RangeExpr,
    NK_Block,
    NK_IfExpr,
    NK_MatchExpr,
    NK_EarlyBlock,
    NK_While,
    NK_LoopExpr,
    NK_ForLoop,
    NK_ConstFor,
    NK_ConstIf,
    NK_ConstQuery,
    NK_Return,
    NK_Break,
    NK_Continue,
    NK_Defer,
    NK_FunVal,
    NK_VarDecl,
    NK_Assign,
    NK_IncDec,
    NK_InlineBlock,
    NK_FnDecl,
    NK_StructDecl,
    NK_EnumDecl,
    NK_AliasDecl,
};

struct Node {
    Line line;                      
                                    
                                    
    Line end;                       
                                    
    TypeExpr *exprtype = nullptr;   
    
    
    
    bool nftemp = false;
    NodeKind nkind;
    
    
    const Node *origin = nullptr;
    
    string_view expansiontrace;
    int expansionbinding = 0;   
    Node(Line _line, NodeKind _nkind) : line(_line), nkind(_nkind) {}
    virtual ~Node() {}
    virtual void Dump(string &s, int ind) const = 0;
    
    
    
    
    Node *Clone(Ast &ast) const;
    virtual Node *Clone1(Ast &ast) const = 0;
    const Node *Origin() const { return origin ? origin : this; }
    
    
    virtual void Children(const function<void(Node *)> &f) const = 0;
    
    
    
    virtual Val Check(TypeCheck &tc, TypeExpr *expected) = 0;
    
    
    
    
    
    virtual Node *Cp1(Inliner &inl) const = 0;
    virtual Node *Opt(Optimizer &opt) = 0;
    
    
    
    
    
    
    virtual bool BceWalk(BCE &bce);
    virtual void BceMark(BCE &bce);
    
    
    
    
    virtual string CgX(CodeGen &cg) = 0;
    virtual void CgAny(CodeGen &cg, const Dst &d) = 0;
    virtual void CgStmt(CodeGen &cg) = 0;
};





inline DiagLoc LocOf(const Node *n) {
    auto l = LocOf(n->line);
    function<void(Node *)> leftmost = [&](Node *c) {
        if (!c || c->line.fileidx != n->line.fileidx || c->line.line != n->line.line) return;
        if (c->line.col > 0 && (l.col == 0 || c->line.col < l.col)) l.col = c->line.col;
        c->Children(leftmost);
    };
    n->Children(leftmost);
    if (n->end.line > 0 && n->end.fileidx == n->line.fileidx) {
        l.endline = n->end.line;
        l.endcol = n->end.col;
    }
    return l;
}


template<typename T> const T *Is(const Node *n) {
    return n && n->nkind == T::NKind ? static_cast<const T *>(n) : nullptr;
}
template<typename T> T *Is(Node *n) {
    return n && n->nkind == T::NKind ? static_cast<T *>(n) : nullptr;
}

#define BCE_WALK bool BceWalk(BCE &bce) override;
#define BCE_MARK void BceMark(BCE &bce) override;

#define NODE(name) struct name : Node { \
    static constexpr NodeKind NKind = NK_##name; \
    void Dump(string &s, int ind) const override; \
    Node *Clone1(Ast &ast) const override; \
    void Children(const function<void(Node *)> &f) const override; \
    Val Check(TypeCheck &tc, TypeExpr *expected) override; \
    Node *Cp1(Inliner &inl) const override; \
    Node *Opt(Optimizer &opt) override; \
    string CgX(CodeGen &cg) override; \
    void CgAny(CodeGen &cg, const Dst &d) override; \
    void CgStmt(CodeGen &cg) override;
#define NODE_END };

NODE(IntLit)
    TypeExpr *fixedtype = nullptr; 
    int64_t val;       
    string_view text;  
    bool uns;          
    IntLit(Line l, int64_t _val, string_view _text = {}, bool _uns = false)
        : Node(l, NKind), val(_val), text(_text), uns(_uns) {}
NODE_END

NODE(FltLit)
    TypeExpr *fixedtype = nullptr;
    double val;
    string_view text;  
    FltLit(Line l, double _val, string_view _text = {}) : Node(l, NKind), val(_val), text(_text) {}
NODE_END

NODE(BoolLit)
    bool val;
    BoolLit(Line l, bool _val) : Node(l, NKind), val(_val) {}
NODE_END

NODE(StrLit)
    string val;
    
    
    bool multiline = false;
    StrLit(Line l, string _val, bool _multiline = false)
        : Node(l, NKind), val(std::move(_val)), multiline(_multiline) {}
NODE_END

NODE(Ident)
    string_view expansionerror;
    BCE_MARK
    string_view name;               
                                    
                                    
    string_view ns;                 
                                    
    
    VarDef *vdef = nullptr;         
    SFunction *fnref = nullptr;     
    Ident(Line l, string_view _name, string_view _ns = {}) : Node(l, NKind), name(_name), ns(_ns) {}
NODE_END

NODE(ArrayLit)
    vector<Node *> elems;
    Node *fillval = nullptr;    
    Node *fillcount = nullptr;
    Node *capexpr = nullptr;    
    ArrayLit(Line l) : Node(l, NKind) {}
NODE_END

NODE(StructLit)
    TypeExpr *type;             
    vector<FieldInit> inits;
    bool defaultall = false;    
                                
    
    
    
    bool implicit = false;
    
    StructInst *sinst = nullptr;    
    EnumInst *einst = nullptr;      
    SVariant *variant = nullptr;    
    vector<int> fieldindices;       
    vector<int> sourcefieldindices; 
    StructLit(Line l, TypeExpr *_type) : Node(l, NKind), type(_type) {}
    
    
    
    
    Node *InitFor(int fieldidx) const {
        for (size_t k = 0; k < fieldindices.size(); k++)
            if (fieldindices[k] == fieldidx) return inits[k].val;
        return nullptr;
    }
NODE_END

NODE(Unary)
    BCE_MARK
    TType op;                   
    Node *child;
    bool synth = false;         
    bool litfloat = false;      
    bool flexint = false;       
    Unary(Line l, TType _op, Node *_child) : Node(l, NKind), op(_op), child(_child) {}
NODE_END

NODE(Binary)
    BCE_WALK
    TType op;
    Node *left, *right;
    
    vector<VarDef *> rightkills;
    
    
    
    
    
    int8_t specidx = 0;
    bool litfloat = false;      
    bool flexint = false;       
    
    
    
    bool nonneg = false;
    Binary(Line l, TType _op, Node *_l, Node *_r) : Node(l, NKind), op(_op), left(_l), right(_r) {}
NODE_END

NODE(Dot)
    TypeExpr *accessorowner = nullptr; 
    BCE_MARK
    Node *obj;
    string_view name;
    string_view ns;                 
                                    
    
    
    TypeExpr *enumtype = nullptr;
    
    
    int fieldidx = -1;
    int member = -1;                
    SVariant *variantconst = nullptr;
    EnumInst *einst = nullptr;
    
    
    
    bool incomplete = false;
    Dot(Line l, Node *_obj, string_view _name, string_view _ns = {})
        : Node(l, NKind), obj(_obj), name(_name), ns(_ns) {}
    
    
    
    bool IsField() const { return !variantconst && member < 0; }
NODE_END

NODE(Call)
    BCE_WALK
    BCE_MARK
    Node *callee;               
    vector<TypeExpr *> tyargs;  
    vector<Node *> args;
    FunVal *trailing = nullptr; 
    
    
    
    bool implicit = false;
    
    FnSpec *spec = nullptr;             
    vector<FnSpec *> dispatch;          
    int dispatcharg = -1;               
    int builtin = -1;                   
    Block *fvbody = nullptr;            
    Node *defaultinit = nullptr;        
    vector<VarDef *> fvparams;          
    vector<TypeExpr *> rettypes;        
    
    
    
    
    vector<pair<VarDef *, VarDef *>> fvremap;
    
    
    vector<pair<TypeExpr *, FnSpec *>> fmtspecs;
    vector<Call *> fmtcontexts; 
    
    
    
    
    int firstdefault = 0;
    int ndefaults = 0;
    
    
    bool poolcheck = false;
    
    
    bool nobc = false;
    TypeExpr *qtype = nullptr;  
    const string *shaderblob = nullptr;  
    Call(Line l, Node *_callee) : Node(l, NKind), callee(_callee) {}
    
    VarDef *FreeVarArg(VarDef *v) const {
        for (auto &p : fvremap) if (p.first == v) return p.second;
        return v;
    }
    
    Node *FirstArg() const {
        if (auto d = Is<Dot>(callee)) return d->obj;
        return args[0];
    }
    
    
    
    vector<Node *> ArgNodes() const {
        vector<Node *> an;
        if (auto d = Is<Dot>(callee)) an.push_back(d->obj);
        for (auto a : args) an.push_back(a);
        return an;
    }
    
    
    vector<Node *> ValueArgNodes() const {
        auto an = ArgNodes();
        while (!an.empty() && an.back()->exprtype && an.back()->exprtype->kind == TY_FN)
            an.pop_back();
        return an;
    }
    
    
    
    void SetArgNode(size_t i, Node *n) {
        if (auto d = Is<Dot>(callee)) {
            if (!i) { d->obj = n; return; }
            i--;
        }
        args[i] = n;
    }
    void SetArgNodes(const vector<Node *> &an) {
        for (size_t i = 0; i < an.size(); i++) SetArgNode(i, an[i]);
    }
    
    bool IsDefaultArg(size_t i) const {
        return (int)i >= firstdefault && (int)i < firstdefault + ndefaults;
    }
NODE_END

NODE(Index)
    BCE_WALK
    BCE_MARK
    Node *obj, *idx;
    bool nobc = false;          
    Index(Line l, Node *_obj, Node *_idx) : Node(l, NKind), obj(_obj), idx(_idx) {}
NODE_END

NODE(SliceExpr)
    BCE_WALK
    BCE_MARK
    Node *obj;
    Node *lo = nullptr, *hi = nullptr;      
    bool lo_from_end = false, hi_from_end = false;  
    bool nobc = false;          
    bool cmpview = false;       
    SliceExpr(Line l, Node *_obj) : Node(l, NKind), obj(_obj) {}
NODE_END

NODE(AsCast)
    BCE_WALK
    Node *child;
    TypeExpr *type;
    bool unchecked;             
    
    
    bool implicit = false;
    
    
    
    bool nobc = false;
    
    
    
    TypeExpr *totype = nullptr;
    AsCast(Line l, Node *_child, TypeExpr *_type, bool _unchecked)
        : Node(l, NKind), child(_child), type(_type), unchecked(_unchecked) {}
    
    
    
    const AsCast *Origin() const { return static_cast<const AsCast *>(Node::Origin()); }
NODE_END

NODE(NullLit)                   
    NullLit(Line l) : Node(l, NKind) {}
NODE_END

NODE(SelfRef)                   
    SelfRef(Line l) : Node(l, NKind) {}
NODE_END

NODE(RangeExpr)                 
    Node *lo, *hi;
    RangeExpr(Line l, Node *_lo, Node *_hi) : Node(l, NKind), lo(_lo), hi(_hi) {}
NODE_END


NODE(Block)
    BCE_WALK
    vector<Node *> stmts;
    Node *tail = nullptr;
    bool expansionboundary = false;
    
    
    
    
    int brokenat = -1;
    Block(Line l) : Node(l, NKind) {}
NODE_END

NODE(IfExpr)
    BCE_WALK
    Node *cond;
    Block *thenb;
    Node *elseb;                
    
    
    
    
    
    
    
    bool flat = false;
    IfExpr(Line l, Node *_cond, Block *_thenb, Node *_elseb)
        : Node(l, NKind), cond(_cond), thenb(_thenb), elseb(_elseb) {}
NODE_END

NODE(MatchExpr)
    BCE_WALK
    BCE_MARK
    Node *scrutinee;
    vector<MatchArm> arms;
    MatchExpr(Line l, Node *_scrutinee) : Node(l, NKind), scrutinee(_scrutinee) {}
NODE_END

NODE(EarlyBlock)                
    BCE_WALK
    Block *body;
    vector<Break *> breaks;     
    EarlyBlock(Line l, Block *_body) : Node(l, NKind), body(_body) {}
NODE_END



using FieldPath = pair<VarDef *, vector<int>>;

NODE(While)
    BCE_WALK
    
    
    
    vector<VarDef *> hoistrefs;
    
    vector<FieldPath> hoistfields;
    
    
    
    
    bool countdown = false;
    Node *cond;
    Block *body;
    While(Line l, Node *_cond, Block *_body) : Node(l, NKind), cond(_cond), body(_body) {}
NODE_END

NODE(LoopExpr)
    BCE_WALK
    vector<VarDef *> hoistrefs;   
    vector<FieldPath> hoistfields;
    Block *body;
    vector<Break *> breaks;       
    LoopExpr(Line l, Block *_body) : Node(l, NKind), body(_body) {}
NODE_END

NODE(ForLoop)
    BCE_WALK
    
    
    
    
    bool fixedlen = false;
    vector<VarDef *> hoistrefs;   
    vector<FieldPath> hoistfields;
    
    
    
    int64_t lenbound = -1;
    bool lenexact = false;
    
    
    
    
    
    
    
    
    int stripk = 0;
    vector<Binary *> lanemods;
    bool sumred = false;
    BCE_MARK
    bool byref;                 
    string_view var;
    string_view idxvar;         
    
    
    TypeExpr *vartype = nullptr, *idxtype = nullptr;
    Node *iter;                 
    Block *body;
    
    VarDef *vdef = nullptr;
    VarDef *idxdef = nullptr;
    int iterkind = 0;           
    ForLoop(Line l, bool _byref, string_view _var, string_view _idxvar, Node *_iter, Block *_body)
        : Node(l, NKind), byref(_byref), var(_var), idxvar(_idxvar), iter(_iter), body(_body) {}
NODE_END

enum ExpansionSource { ES_FIELDS, ES_TYPES, ES_CONSTANTS, ES_VARIANTS };

NODE(ConstFor)
    ExpansionSource source = ES_FIELDS;
    vector<string_view> names;
    vector<int> bindings;           
    vector<TypeExpr *> types;
    vector<Node *> constants;
    Block *body = nullptr;          
    vector<FieldInit> inits;
    bool armfragments = false;      
    vector<MatchArm> arms;
    ConstFor(Line l) : Node(l, NKind) {}
NODE_END




enum QueryKind { Q_HAS_DEFAULT, Q_SAME };

NODE(ConstQuery)
    QueryKind query;
    string_view name;               
    vector<TypeExpr *> types;       
    
    
    TypeExpr *owner = nullptr;
    string_view field;
    ConstQuery(Line l, QueryKind _query, string_view _name)
        : Node(l, NKind), query(_query), name(_name) {}
NODE_END



struct ConstIfBranch {
    Node *cond = nullptr;
    Block *body = nullptr;          
    vector<FieldInit> inits;
    vector<MatchArm> arms;
};

NODE(ConstIf)
    vector<ConstIfBranch> branches;
    bool armfragments = false;      
    ConstIf(Line l) : Node(l, NKind) {}
NODE_END

NODE(Return)
    BCE_WALK
    vector<Node *> vals;
    string_view from;           
    string_view ns;             
    SFunction *target = nullptr;  
    FnSpec *targetspec = nullptr; 
    Return(Line l) : Node(l, NKind) {}
NODE_END

NODE(Break)
    BCE_WALK
    Node *val;
    Break(Line l, Node *_val) : Node(l, NKind), val(_val) {}
NODE_END

NODE(Continue)
    BCE_WALK
    Continue(Line l) : Node(l, NKind) {}
NODE_END







NODE(Defer)
    BCE_WALK
    Block *body;
    Defer(Line l, Block *_body) : Node(l, NKind), body(_body) {}
NODE_END



NODE(FunVal)
    BCE_WALK
    vector<Param> params;       
    bool explicit_params = false;
    Block *body;
    FunVal(Line l, Block *_body) : Node(l, NKind), body(_body) {}
NODE_END


NODE(VarDecl)
    BCE_WALK
    BCE_MARK
    bool isvar;                 
    bool isconst = false;       
    int reusable = 0;           
    bool isglobal = false;
    bool byref = false;         
    bool inline_arg = false;    
    string_view ns;             
    string_view doc;            
    vector<string_view> names;  
    
    
    bool results = false;
    TypeExpr *type = nullptr;
    vector<Node *> inits;       
    vector<VarDef *> defs;      
    VarDecl(Line l, bool _isvar) : Node(l, NKind), isvar(_isvar) {}
NODE_END

NODE(Assign)
    BCE_WALK
    BCE_MARK
    TType op;                   
    Node *lval, *rhs;
    bool pointee = false;       
    Assign(Line l, TType _op, Node *_lval, Node *_rhs) : Node(l, NKind), op(_op), lval(_lval), rhs(_rhs) {}
NODE_END

NODE(IncDec)
    BCE_WALK
    BCE_MARK
    TType op;                   
    Node *lval;
    IncDec(Line l, TType _op, Node *_lval) : Node(l, NKind), op(_op), lval(_lval) {}
NODE_END









NODE(InlineBlock)
    BCE_WALK
    SFunction *sf;
    FnSpec *spec;               
    Block *body;
    InlineBlock(Line l, SFunction *_sf, FnSpec *_spec, Block *_body)
        : Node(l, NKind), sf(_sf), spec(_spec), body(_body) {}
    void EmitBody(CodeGen &cg, const Dst &d);   
NODE_END





struct SFunction {
    string_view name;           
    string_view ns;             
    string_view qname;          
    Line line;
    Line end;                   
    string_view doc;            
    vector<GenericParam> generics;
    vector<Param> params;
    vector<TypeExpr *> rets;    
    bool has_rets = false;
    bool isrec = false;         
    bool isinline = false;      
    bool issimd = false;        
    
    
    bool checkfree = false;
    bool isthread = false;
    bool isextern = false;      
    bool isexport = false;      
    string cname;               
    
    
    string dylib;
    
    
    
    bool readsalltops = false;
    
    
    
    bool takesloc = false;
    bool isnested = false;
    SFunction *outer = nullptr;     
    Block *body = nullptr;
    
    
    
    
    bool broken = false;
    
    
    
    
    bool brokensig = false;
    vector<FnSpec *> specs;     

    
    
    bool IsGeneric() const {
        if (!generics.empty()) return true;
        for (auto &p : params) if (!p.type) return true;
        return false;
    }
};

struct SStruct {
    string_view name;
    string_view ns;             
    string_view qname;
    Line line;
    Line end;                   
    string_view doc;            
    bool isroot = false;       
    vector<GenericParam> generics;
    vector<Field> fields;
    vector<StructInst *> insts;  
    
    
    
    
    bool broken = false;
    
    
    
    
    bool unknown = false;
};

struct SVariant {
    string_view name;
    Line line;                  
    string_view doc;            
    vector<Field> fields;
    bool has_payload = false;   
};

struct SEnum {
    string_view name;
    string_view ns;             
    string_view qname;
    Line line;
    Line end;                   
    string_view doc;            
    vector<GenericParam> generics;
    vector<SVariant> variants;  
    vector<EnumInst *> insts;   
    
    
    bool broken = false;

    SVariant *FindVariant(string_view vname) {
        for (auto &v : variants) if (v.name == vname) return &v;
        return nullptr;
    }
    
    int VariantIndex(const SVariant *v) const {
        assert(v >= variants.data() && v < variants.data() + variants.size());
        return (int)(v - variants.data());
    }
};



inline int LastRealField(const vector<Field> &fields) {
    for (auto i = (int)fields.size() - 1; i >= 0; i--) if (!fields[i].ispad) return i;
    return -1;
}







inline bool EmptyLayout(const vector<Field> &fields) {
    for (auto &f : fields) if (!f.ispad || f.padsize > 0) return false;
    return true;
}



struct SAlias {
    string_view name;
    string_view ns;             
    string_view qname;
    Line line;
    Line end;                   
    string_view doc;            
    TypeExpr *type = nullptr;
};


inline string_view NominalNs(const TypeExpr *t) {
    switch (t->kind) {
        case TY_STRUCT:  return t->struc->st->ns;
        case TY_ENUM:    return t->enu->en->ns;
        case TY_VARIANT: return NominalNs(t->var->adt);
        default:         return {};
    }
}

NODE(FnDecl)
    SFunction *sf;
    FnDecl(Line l, SFunction *_sf) : Node(l, NKind), sf(_sf) {}
NODE_END

NODE(StructDecl)
    SStruct *st;
    StructDecl(Line l, SStruct *_st) : Node(l, NKind), st(_st) {}
NODE_END

NODE(EnumDecl)
    SEnum *en;
    EnumDecl(Line l, SEnum *_en) : Node(l, NKind), en(_en) {}
NODE_END

NODE(AliasDecl)
    SAlias *al;
    AliasDecl(Line l, SAlias *_al) : Node(l, NKind), al(_al) {}
NODE_END

#undef NODE
#undef NODE_END









enum SizeClass { SC_FIXED, SC_VARIABLE, SC_RESIZABLE, SC_ROOT };





struct VarDef {
    string_view name;
    TypeExpr *type = nullptr;   
    Line line;
    bool isvar = false;         
    
    
    
    bool copybind = false;
    bool isglobal = false;
    
    int globalindex = 0;
    bool isparam = false;
    
    
    
    vector<VarDef *> components;
    VarDef *componentowner = nullptr;
    int componentfield = -1;
    
    
    TypeExpr *componentbase = nullptr;
    
    
    
    bool resulttemp = false;
    
    
    
    bool istemp = false;
    int reusable = 0;           
    bool nonneg = false;        
    VarDef *nonnegfrom = nullptr;   
    
    
    
    
    
    bool constlit = false;
    bool constuns = false;      
    int64_t constval = 0;
    bool constnot = false;      
    bool constflt = false;      
    double constfval = 0;
    VarDef *constfrom = nullptr;    
    
    
    Node *markof = nullptr;
    FnSpec *ownerspec = nullptr;  
    
    
    
    int depth = 0;
    
    
    
    
    
    
    VarDef *classfrom = nullptr;
    bool poolclass = false;
    
    
    VarDef *classpool = nullptr;
    
    
    
    
    
    
    
    
    
    bool growshrink = false;
    bool gsvia = false;
    vector<TypeExpr *> gselems;
    
    
    
    
    
    vector<TypeExpr *> classreach;
    
    
    
    
    vector<TypeExpr *> onearray;
    
    
    
    
    
    VarDef *heldslice = nullptr;
    
    
    
    VarDef *heldholder = nullptr;
    
    
    
    
    Prov ref { .writable = true };
    bool refrootknown = false;
    
    
    
    
    
    
    Roots contents;
    bool contentbyteview = false; 
    
    
    bool storedinto = false;
    
    
    bool unsized = false;
    VarDef *unsizedorigin = nullptr;
    
    
    bool assigned = false;
    bool maybeassigned = false;
    TypeExpr *narrowed = nullptr;  
    bool captured = false;         
    
    
    
    
    
    
    
    
    
    Node *nonneguse = nullptr;
    Node *markuse = nullptr;
    Node *constuse = nullptr;
    const char *constwhat = nullptr;
    Node *refwrite = nullptr;
    
    
    
    
    Node *extentuse = nullptr;
    const char *extentwhat = nullptr;
    bool extentcap = false;
    
    
    
    bool slotref = false;
};


inline int RootDepth(const VarDef *v) { return v ? v->depth : 0; }

inline VarDef *Roots::Root() const {
    VarDef *r = nullptr;
    for (size_t i = 0; i < alts.size(); i++) {
        auto a = alts[i].root;
        
        if (!i || RootDepth(a) > RootDepth(r) || (a && !r)) r = a;
    }
    return r;
}

inline VarDef *Roots::From() const {
    VarDef *f = nullptr;
    for (auto &a : alts)
        if (a.from && (!f || RootDepth(a.root) > RootDepth(f))) f = a.from;
    return f;
}





struct FieldRun {
    const vector<Field> *fields = nullptr;
    const vector<TypeExpr *> *ftypes = nullptr;
};



struct StructInst {
    SStruct *st = nullptr;
    vector<TypeExpr *> args;
    vector<TypeExpr *> ftypes;     
    FieldRun run;                  
    SizeClass sclass = SC_FIXED;
    bool flat = true;
    bool validated = false;        
    
    
    
    
    bool failed = false;
    
    
    
    bool frameobj = false;
};


struct EnumInst {
    SEnum *en = nullptr;
    vector<TypeExpr *> args;
    vector<vector<TypeExpr *>> vftypes;   
    vector<FieldRun> runs;                
    bool allfixed = true;                 
    
    
    
    bool selfrel = false;
    SizeClass varclass = SC_VARIABLE;     
    bool flat = true;
    bool validated = false;
    bool failed = false;                  
    
    
    vector<uint8_t> vbuilt;
};

inline span<const FieldRun> RunsOf(const StructInst *si) { return { &si->run, 1 }; }
inline span<const FieldRun> RunsOf(const EnumInst *ei) { return ei->runs; }
inline span<const FieldRun> RunsOf(const EnumInst *ei, int vi) { return { &ei->runs[vi], 1 }; }






template<typename F> bool AnyFieldOf(span<const FieldRun> runs, F f) {
    for (auto &run : runs)
        for (auto ft : *run.ftypes) if (ft && f(ft)) return true;
    return false;
}

template<typename F> void EachFieldOf(span<const FieldRun> runs, F f) {
    for (auto &run : runs)
        for (auto ft : *run.ftypes) if (ft) f(ft);
}






template<typename R> bool HasRelRefOf(TypeExpr *t, bool inpool, R runs) {
    switch (t->kind) {
        case TY_REF: return IsRelRef(t) && (inpool || !t->ref->pool);
        case TY_ARRAY: return HasRelRefOf(t->arr->sub, inpool, runs);
        default:
            return AnyFieldOf(runs(t), [&](TypeExpr *ft) { return HasRelRefOf(ft, inpool, runs); });
    }
}




template<typename F> void NamedArraysIn(TypeExpr *t, set<TypeExpr *> &seen, F f) {
    if (!t || !seen.insert(t).second) return;
    auto visit = [&](TypeExpr *ft) { NamedArraysIn(ft, seen, f); };
    switch (t->kind) {
        case TY_INT: if (t->ix && t->ix->arr) f(t->ix->arr, true); break;
        case TY_REF:
            if (t->ref->pool) f(t->ref->pool, false);
            visit(t->ref->sub);
            break;
        case TY_SLICE: visit(t->sub); break;
        case TY_ARRAY: visit(t->arr->sub); break;
        case TY_STRUCT:
            if (t->struc->inst) EachFieldOf(RunsOf(t->struc->inst), visit);
            break;
        case TY_ENUM:
            if (t->enu->inst) EachFieldOf(RunsOf(t->enu->inst), visit);
            break;
        case TY_VARIANT: visit(t->var->adt); break;
        default: break;
    }
}





struct RootArg {
    int cls = 0;
    int ownerparam = -1;
    int ownerfield = -1;
    bool writable = true;
    int reusable = 0;
    
    
    
    
    
    
    
    
    
    
    bool growshrink = false;
    bool gsvia = false;
    vector<TypeExpr *> gselems;
    
    
    
    
    
    vector<TypeExpr *> onearray;
    
    
    
    
    
    
    
    bool slotread = false;
    
    
    
    
    
    bool unknown = false;
    
    
    
    bool byteview = false;
    
    
    
    
    
    
    
    bool exact = true;
    
    
    
    
    
    
    
    bool heldexact = false;
    
    
    
    
    
    
    bool concrete = false;
    
    
    
    vector<pair<FnSpec *, int>> via;
    
    
    
    
    
    
    
    VarDef *pool = nullptr;
    
    
    
    int depth = 0;
    
    
    
    
    
    
    
    
    
    int depthkey = 0;
    bool operator==(const RootArg &o) const {
        return cls == o.cls && ownerparam == o.ownerparam && ownerfield == o.ownerfield &&
               writable == o.writable && reusable == o.reusable &&
               growshrink == o.growshrink && gsvia == o.gsvia && slotread == o.slotread &&
               byteview == o.byteview && pool == o.pool && heldexact == o.heldexact &&
               unknown == o.unknown;
    }
};







enum ShrinkBalance { SB_BALANCED, SB_UNBALANCED };




template<typename K> struct BoundShrink {
    K key;
    TypeExpr *type = nullptr;
    ShrinkBalance balance = SB_UNBALANCED;
};




struct StoreEvent {
    VarDef *container = nullptr;
    
    
    
    
    
    VarDef *root = nullptr;      
    VarDef *src = nullptr;
    bool exact = false;
    TypeExpr *pointee = nullptr; 
    bool byteview = false;
    Line at;
    
    
    
    
    
    TypeExpr *reached = nullptr;
    
    
    
    bool bound = false;
    
    
    bool slot = false;
    
    
    
    
    bool sliceref = false;
    
    
    
    
    
    
    
    
    
    bool classread = false;
    
    
    
    bool incoming = false;
};






struct LiveShrink {
    VarDef *shrunk = nullptr;
    bool shrunkexact = true;
    
    
    TypeExpr *bound = nullptr;
    VarDef *live = nullptr;      
    bool liveexact = true;
    bool liveprojected = false;
    TypeExpr *pointee = nullptr; 
    bool byteview = false;
    bool growonly = false;       
    string name;                 
    
    
    
    
    
    bool contents = false;
    
    
    
    
    bool inplace = false;
};





struct RetRoot {
    Roots alts;                
    bool writable = true;      
    bool byteview = false;
    bool freshview = false;    
    bool set = false;          
    
    
    
    
    int heldparam = -1;
};



struct LitAdapt {
    int param = 0;
    TypeExpr *type = nullptr;
    Line at;
};

struct LitFlow {
    int param = 0;
    FnSpec *to = nullptr;
    int toparam = 0;
};




struct EnvRead {
    VarDef *var = nullptr;
    bool assigned = false;
    bool maybeassigned = false;
    bool refrootknown = false;
    Prov ref;
    Roots contents;
    bool contentbyteview = false;
    bool slotref = false;
};



struct OuterExit {
    FnSpec *target = nullptr;
    set<VarDef *> assigned, maybeassigned;
};






struct FnRecord {
    
    
    
    struct RelativeStore {
        Roots source, destination;
        int result = -1;
    };
    vector<RelativeStore> relativestores;
    
    
    
    bool mayreturn = true;
    vector<RetRoot> retroots;      
    
    set<VarDef *> reboundoptionals;
    
    
    
    map<VarDef *, ShrinkBalance> shrinkexternals;
    
    
    map<int, ShrinkBalance> shrinkparams;
    
    
    
    
    vector<BoundShrink<VarDef *>> shrinkexternalbounds;
    vector<BoundShrink<int>> shrinkparambounds;
    
    
    
    
    vector<LiveShrink> liveshrinks;
    
    
    
    
    
    map<VarDef *, vector<TypeExpr *>> growexternals;
    map<int, vector<TypeExpr *>> growparams;
    
    
    vector<StoreEvent> classevents;
    
    
    
    
    
    
    
    struct ShrinkOrder {
        int param = -1;
        VarDef *external = nullptr;
        TypeExpr *bound = nullptr;
        size_t before = 0;
        vector<pair<int, string>> kills;
    };
    vector<ShrinkOrder> shrinkorders;
    size_t eventstart = 0;         
    size_t eventend = 0;           
    
    
    
    struct BodyEvent {
        VarDef *container, *root, *src;
        bool exact, classread;
    };
    vector<BodyEvent> bodyevents;
};




struct FnSpec {
    SFunction *sf = nullptr;
    FnSpec *lexparent = nullptr;   
    
    
    
    
    
    
    bool isfunval = false;
    vector<TypeExpr *> argtypes;   
    
    
    vector<RootArg> roots;
    
    
    
    
    
    
    vector<RootArg> views;
    
    vector<VarDef *> narrowedenv;
    
    
    
    
    
    vector<EnvRead> envreads;
    
    
    vector<EnvRead> envexits;
    
    
    
    
    
    
    vector<OuterExit> outerexits;
    vector<int> litparams;         
    vector<LitAdapt> litadapts;    
    vector<LitFlow> litflows;      
    vector<pair<string_view, FnValBind>> fnvals;  
    vector<pair<string_view, TypeExpr *>> bindings;  
    Block *body = nullptr;         
    vector<VarDef *> params;
    vector<TypeExpr *> rets;       
    bool retsknown = false;
    bool inprogress = false;
    
    
    
    
    bool failed = false;
    
    
    
    SFunction *exemplar = nullptr;
    bool incycle = false;          
    
    
    
    
    FnSpec *cyclelink = nullptr;
    
    
    
    
    
    
    
    
    
    vector<FnSpec *> cyclemembers;
    FnRecord record;
    unique_ptr<FnRecord> prev;
    int rounds = 0;
    bool stale = false;
    vector<VarDef *> classroots;
    Line joinedat;                 
    set<FnSpec *> needs;           
    bool hasdefer = false;         
    
    
    
    
    vector<pair<Node *, vector<pair<SFunction *, FnSpec *>>>> neededges;
    
    
    
    
    
    
    
    bool storesout = false;
    
    
    
    
    bool relnamedresult = false;
    int id = 0;                    
    
    int uses = 0;                  
    bool live = false;             
    
    
    
    bool outofline = false;
    
    
    
    bool cinline = false;
};






struct Namespace {
    unordered_map<string_view, SStruct *> structmap;
    unordered_map<string_view, SEnum *> enummap;
    unordered_map<string_view, SAlias *> aliasmap;
    unordered_map<string_view, vector<SFunction *>> functionmap;
    unordered_map<string_view, VarDecl *> globalmap;

    bool TypeNameExists(string_view name) const {
        return structmap.count(name) || enummap.count(name) || aliasmap.count(name);
    }
};




struct NameRef {
    string_view leaf;
    string_view ns;
    bool qualified = false;      
};

inline NameRef SplitName(string_view name, string_view usens) {
    auto pos = name.find("::");
    if (pos == string_view::npos) return { name, usens, false };
    return { name.substr(pos + 2), name.substr(0, pos), true };
}



inline string_view GlobalLeaf(string_view name) {
    return name.substr(0, 2) == "::" ? name.substr(2) : name;
}











struct DylibSpelling {
    string name, any, onwindows, onmacos, onlinux;
    const string &On(const char *platform) const {
        return string_view(platform) == "_windows" ? onwindows
             : string_view(platform) == "_macos"   ? onmacos
                                                   : onlinux;
    }
    string &On(const char *platform) {
        return const_cast<string &>(std::as_const(*this).On(platform));
    }
};

struct Package {
    string name;
    string root;          
    bool stdlib = false;
    string cachekey;      
    int builtin = -1;     
    
    
    
    string sidecar;
    bool hassidecar = false;
    vector<string> csources, cincludes, cheaders;     
    vector<string> cdefines, cflags_cc, cflags_msvc, clibs;
    string cmake_target;
    vector<string> cmake_options;
    
    
    vector<string> exclude;
    bool main_thread_only = false;
    vector<DylibSpelling> dylibs;           
    string output;                          
    vector<int> required;                   

    bool HasNative() const { return !csources.empty() || !cmake_target.empty(); }
};

struct Ast {
    
    
    ExpansionClone *expansionclone = nullptr;
    TypeExpr *CloneExpansionType(TypeExpr *t);
    FieldInit CloneFieldInit(const FieldInit &fi);
    
    
    vector<pair<string, unique_ptr<string>>> sources;   

    
    
    
    function<void(const Diagnostic &)> sink;
    
    
    
    vector<Diagnostic> errors;

    
    
    
    
    const atomic<bool> *cancel = nullptr;
    void CheckCancel() const {
        if (cancel && cancel->load(memory_order_relaxed)) throw Cancelled {};
    }
    
    
    bool exemplars = false;

    
    
    struct ImportSite { Line line; Line end; string path; };
    vector<ImportSite> imports;

    
    
    string DiagnosticText(const Diagnostic &d) const {
        auto known = d.where.fileidx >= 0 && d.where.fileidx < (int)sources.size();
        return RenderDiagnostic(d, known ? sources[d.where.fileidx].first : string(),
                                known ? string_view(*sources[d.where.fileidx].second)
                                      : string_view());
    }

    void Warn(Diagnostic d) {
        d.warning = true;
        d.text = DiagnosticText(d);
        Emit(d);
    }

    
    
    void Report(Diagnostic d) {
        d.warning = false;
        if (d.text.empty()) d.text = DiagnosticText(d);
        errors.push_back(d);
        Emit(d);
    }

    void Emit(const Diagnostic &d) {
        if (sink) sink(d);
        else PrintDiagnosticText(d.text);
    }

    
    void Warn(DiagLoc where, string message) {
        Diagnostic d;
        d.where = where;
        d.message = std::move(message);
        Warn(std::move(d));
    }
    vector<Package> packages;
    vector<int> filepkg;                                
    bool StdlibFile(int fileidx) const {
        if (fileidx < 0 || fileidx >= (int)filepkg.size()) return false;
        auto pkg = filepkg[fileidx];
        return pkg >= 0 && pkg < (int)packages.size() && packages[pkg].stdlib;
    }

    vector<Node *> allnodes;
    vector<TypeExpr *> alltypes;
    vector<TypeDetail *> typedetails;
    vector<SFunction *> functions;
    vector<SStruct *> structs;
    vector<SEnum *> enums;
    vector<SAlias *> aliases;
    
    
    vector<SStruct *> unknowntypes;

    
    vector<VarDef *> vardefs;
    vector<StructInst *> structinsts;
    vector<EnumInst *> enuminsts;
    vector<FnSpec *> fnspecs;
    vector<FnSpec *> fvenvs;     
    
    
    vector<FnSpec *> exemplarspecs;

    vector<Node *> topdecls;                        
    vector<VarDecl *> globals;                      

    
    
    
    
    map<string, string> shaders;

    
    
    
    template<typename F> void ForEachRootTree(F f) {
        for (auto g : globals) for (auto &i : g->inits) f(i);
    }

    
    
    map<string_view, Namespace> namespaces;
    deque<string> interned;

    
    TypeExpr *inttypes[IS_VARINT + 1];
    TypeExpr *flttypes[FS_F64 + 1];
    TypeExpr *booltype;
    TypeExpr *voidtype;

    Ast() {
        for (int s = IS_I8; s <= IS_VARINT; s++) {
            inttypes[s] = NewType(TY_INT, Line {});
            inttypes[s]->intstorage = (IntStorage)s;
        }
        for (int s = FS_F32; s <= FS_F64; s++) {
            flttypes[s] = NewType(TY_FLT, Line {});
            flttypes[s]->fltstorage = (FltStorage)s;
        }
        booltype = NewType(TY_BOOL, Line {});
        voidtype = NewType(TY_VOID, Line {});
    }

    ~Ast() {
        for (auto n : allnodes) delete n;
        for (auto t : alltypes) delete t;
        for (auto d : typedetails) delete d;
        for (auto f : functions) delete f;
        for (auto s : structs) delete s;
        for (auto s : unknowntypes) delete s;
        for (auto e : enums) delete e;
        for (auto a : aliases) delete a;
        for (auto v : vardefs) delete v;
        for (auto i : structinsts) delete i;
        for (auto i : enuminsts) delete i;
        for (auto sp : fnspecs) delete sp;
        for (auto sp : fvenvs) delete sp;
        for (auto sp : exemplarspecs) delete sp;
    }

    TypeExpr *NewType(TypeKind kind, Line line) {
        auto t = new TypeExpr(kind, line);
        alltypes.push_back(t);
        return t;
    }

    
    
    struct DeclMark {
        size_t types = 0, functions = 0, structs = 0, enums = 0, aliases = 0, globals = 0,
               topdecls = 0;
    };
    DeclMark Mark() const {
        return { alltypes.size(), functions.size(), structs.size(), enums.size(), aliases.size(),
                 globals.size(), topdecls.size() };
    }

    
    
    
    
    
    
    void DropSince(const DeclMark &m) {
        for (auto i = m.types; i < alltypes.size(); i++) delete alltypes[i];
        alltypes.resize(m.types);
        for (auto i = m.functions; i < functions.size(); i++) delete functions[i];
        functions.resize(m.functions);
        for (auto i = m.structs; i < structs.size(); i++) delete structs[i];
        structs.resize(m.structs);
        for (auto i = m.enums; i < enums.size(); i++) delete enums[i];
        enums.resize(m.enums);
        for (auto i = m.aliases; i < aliases.size(); i++) delete aliases[i];
        aliases.resize(m.aliases);
        for (auto i = m.globals; i < globals.size(); i++) {
            auto g = globals[i];
            auto &gm = NS(g->ns).globalmap;
            for (auto name : g->names)
                if (auto it = gm.find(name); it != gm.end() && it->second == g) gm.erase(it);
        }
        globals.resize(m.globals);
        topdecls.resize(m.topdecls);
    }

    
    
    TypeExpr *ConstOf(TypeExpr *t) {
        if (t->cq) return t;
        auto n = NewType(t->kind, t->line);
        *n = *t;
        n->cq = true;
        return n;
    }

    
    
    
    
    TypeExpr *PlainOf(TypeExpr *t) {
        if (!t->cq || t->kind == TY_REF || t->kind == TY_SLICE) return t;
        auto n = NewType(t->kind, t->line);
        *n = *t;
        n->cq = false;
        return n;
    }

    template<typename T> T *NewDetail() {
        auto d = new T();
        typedetails.push_back(d);
        return d;
    }

    
    
    TypeExpr *NoIndex(TypeExpr *t) {
        return t && t->kind == TY_INT && t->ix ? inttypes[t->intstorage] : t;
    }
    
    TypeExpr *IndexTypeOf(IntStorage s, VarDef *arr, bool upto, Line line) {
        auto t = NewType(TY_INT, line);
        t->intstorage = s;
        t->ix = NewDetail<TypeIndex>();
        t->ix->name = arr->name;
        t->ix->arr = arr;
        t->ix->upto = upto;
        return t;
    }

    
    
    
    TypeExpr *SliceOf(TypeExpr *elem, Line line) {
        auto t = NewType(TY_SLICE, line);
        t->sub = elem;
        return t;
    }
    
    
    TypeExpr *RefTo(TypeExpr *sub, Line line, bool optional = false) {
        auto t = NewType(TY_REF, line);
        t->ref = NewDetail<TypeRef>();
        t->ref->sub = sub;
        t->ref->optional = optional;
        return t;
    }
    
    
    
    TypeExpr *ArrayOf(TypeExpr *elem, ArrayKind kind, Line line, int64_t size = -1) {
        auto t = NewType(TY_ARRAY, line);
        t->arr = NewDetail<TypeArray>();
        t->arr->sub = elem;
        t->arr->akind = kind;
        t->arr->size = size;
        return t;
    }
    TypeExpr *StructOf(SStruct *st, vector<TypeExpr *> args, Line line) {
        auto t = NewType(TY_STRUCT, line);
        t->struc = NewDetail<TypeStruct>();
        t->struc->st = st;
        t->struc->args = std::move(args);
        return t;
    }
    
    TypeExpr *EnumOf(SEnum *en, vector<TypeExpr *> args, bool varmode, Line line) {
        auto t = NewType(TY_ENUM, line);
        t->enu = NewDetail<TypeEnum>();
        t->enu->en = en;
        t->enu->args = std::move(args);
        t->enu->varmode = varmode;
        return t;
    }
    
    
    TypeExpr *VariantOf(TypeExpr *adt, string_view name, SVariant *variant, Line line) {
        auto t = NewType(TY_VARIANT, line);
        t->var = NewDetail<TypeVariant>();
        t->var->adt = adt;
        t->var->name = name;
        t->var->variant = variant;
        return t;
    }
    
    
    TypeExpr *VariantTypeOf(TypeExpr *enumtype, SVariant *v, Line line) {
        auto base = enumtype->enu->varmode
                        ? EnumOf(enumtype->enu->en, enumtype->enu->args, false, line)
                        : enumtype;
        return VariantOf(base, v->name, v, line);
    }

    template<typename T, typename... Args> T *New(Args &&...args) {
        auto n = new T(std::forward<Args>(args)...);
        allnodes.push_back(n);
        return n;
    }

    VarDef *NewVarDef()       { auto v = new VarDef();     vardefs.push_back(v);     return v; }
    StructInst *NewStructInst() { auto i = new StructInst(); structinsts.push_back(i); return i; }
    EnumInst *NewEnumInst()   { auto i = new EnumInst();   enuminsts.push_back(i);   return i; }
    FnSpec *NewFnSpec() {
        auto sp = new FnSpec();
        sp->id = (int)fnspecs.size();
        fnspecs.push_back(sp);
        return sp;
    }
    FnSpec *NewFunValEnv() {
        auto sp = new FnSpec();
        sp->isfunval = true;
        fvenvs.push_back(sp);
        return sp;
    }

    TypeExpr *PrimTypeForToken(TType t) {
        switch (t) {
            case T_TI8:     return inttypes[IS_I8];
            case T_TI16:    return inttypes[IS_I16];
            case T_TI32:    return inttypes[IS_I32];
            case T_TI64:    return inttypes[IS_I64];
            case T_TU8:     return inttypes[IS_U8];
            case T_TU16:    return inttypes[IS_U16];
            case T_TU32:    return inttypes[IS_U32];
            case T_TU64:    return inttypes[IS_U64];
            case T_TVARINT: return inttypes[IS_VARINT];
            case T_TF32:    return flttypes[FS_F32];
            case T_TF64:    return flttypes[FS_F64];
            case T_TBOOL:   return booltype;
            default:        assert(false); return inttypes[IS_I64];
        }
    }

    string_view Intern(string s) {
        interned.push_back(std::move(s));
        return interned.back();
    }

    
    
    string_view QualifiedName(string_view ns, string_view leaf) {
        return ns.empty() ? leaf : Intern(cat(ns, "::", leaf));
    }

    Namespace &NS(string_view ns) { return namespaces[ns]; }
    Namespace *FindNS(string_view ns) {
        auto it = namespaces.find(ns);
        return it == namespaces.end() ? nullptr : &it->second;
    }

    
    
    
    template<typename T>
    T *Lookup(unordered_map<string_view, T> Namespace::*map, string_view name,
              string_view usens) {
        auto ref = SplitName(name, usens);
        if (auto n = FindNS(ref.ns))
            if (auto it = (n->*map).find(ref.leaf); it != (n->*map).end()) return &it->second;
        if (ref.qualified || ref.ns.empty()) return nullptr;
        auto n = FindNS("");
        if (!n) return nullptr;
        auto it = (n->*map).find(ref.leaf);
        return it == (n->*map).end() ? nullptr : &it->second;
    }

    
    
    
    template<typename T>
    T *LookupType(unordered_map<string_view, T> Namespace::*map, string_view name,
                  string_view usens) {
        auto ref = SplitName(name, usens);
        auto n = FindNS(ref.ns);
        if ((!n || !n->TypeNameExists(ref.leaf)) && !ref.qualified && !ref.ns.empty())
            n = FindNS("");
        if (!n) return nullptr;
        auto it = (n->*map).find(ref.leaf);
        return it == (n->*map).end() ? nullptr : &it->second;
    }

    SStruct *LookupStruct(string_view name, string_view usens) {
        auto p = LookupType(&Namespace::structmap, name, usens);
        return p ? *p : nullptr;
    }
    SEnum *LookupEnum(string_view name, string_view usens) {
        auto p = LookupType(&Namespace::enummap, name, usens);
        return p ? *p : nullptr;
    }
    SAlias *LookupAlias(string_view name, string_view usens) {
        auto p = LookupType(&Namespace::aliasmap, name, usens);
        return p ? *p : nullptr;
    }
    VarDecl *LookupGlobal(string_view name, string_view usens) {
        auto p = Lookup(&Namespace::globalmap, name, usens);
        return p ? *p : nullptr;
    }
    
    
    const vector<SFunction *> &LookupFunctions(string_view name, string_view usens) {
        static const vector<SFunction *> none;
        auto p = Lookup(&Namespace::functionmap, name, usens);
        return p ? *p : none;
    }
    
    
    
    
    const vector<SFunction *> &LookupMethods(string_view name, string_view usens,
                                             const TypeExpr *recv) {
        auto tns = NominalNs(recv);
        if (auto n = tns.empty() ? nullptr : FindNS(tns))
            if (auto it = n->functionmap.find(name); it != n->functionmap.end()) return it->second;
        return LookupFunctions(name, usens);
    }

    
    
    SFunction *MainFunction() {
        auto n = FindNS("");
        if (!n) return nullptr;
        auto it = n->functionmap.find("main");
        return it != n->functionmap.end() && it->second.size() == 1 ? it->second[0] : nullptr;
    }

    
    
    
    bool DeclaresMain() {
        for (auto &[name, n] : namespaces)
            if (n.functionmap.count("main")) return true;
        return false;
    }

    void Dump(string &s) const;  
};

}  
