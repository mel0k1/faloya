
























#pragma once

namespace faloya {

enum BuiltinFlags {
    BF_MEMBER    = 1 << 0,  
    BF_PROPERTY  = 1 << 1,  
    BF_TYARGS    = 1 << 2,  
    BF_WRITE     = 1 << 3,  
    BF_REUSABLE  = 1 << 4,  
    BF_SHRINKS   = 1 << 5,  
    BF_SLICEPOOL = 1 << 6,  
    BF_OUTARG    = 1 << 7,  
};


enum BuiltinRecv {
    BR_FIXED = 1 << 0, BR_VAR = 1 << 1, BR_LIMITED = 1 << 2,
    BR_GROW = 1 << 3, BR_GROWSHRINK = 1 << 4, BR_SLICE = 1 << 5,
    BR_ANY = BR_FIXED | BR_VAR | BR_LIMITED | BR_GROW | BR_GROWSHRINK | BR_SLICE,
    BR_GROWABLE = BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    
    
    BR_INDEXABLE = BR_FIXED | BR_LIMITED | BR_GROW | BR_GROWSHRINK,
    
    
    BR_SHRINKABLE = BR_LIMITED | BR_GROWSHRINK | BR_GROW,
};



inline int RecvKindOf(TypeExpr *t) {
    if (t->kind == TY_SLICE) return BR_SLICE;
    if (t->kind != TY_ARRAY) return 0;
    switch (t->arr->akind) {
        case A_FIXED:      return BR_FIXED;
        case A_VAR:        return BR_VAR;
        case A_LIMITED:    return BR_LIMITED;
        case A_GROW:       return BR_GROW;
        case A_GROWSHRINK: return BR_GROWSHRINK;
    }
    return 0;
}


#define BUILTINS \
    F(B_PRINT,            "print",            0, 99,   "",   "",  0,              "",            0) \
    F(B_STR,              "str",              0, 99,   "",   "",  0,              "",            0) \
    F(B_FORMAT,           "format",           1, 99,   "",   "",  BR_GROWABLE,    "format into", BF_MEMBER | BF_WRITE) \
    F(B_ASSERT,           "assert",           1,  1,   "",   "",  0,              "",            0) \
    F(B_ABORT,            "abort",            1,  1,   "",   "",  0,              "",            0) \
    F(B_EXIT,             "exit",             1,  1,   "",   "",  0,              "",            0) \
    F(B_HARDWARE_THREADS, "hardware_threads", 0,  0,   "",   "i", 0,              "",            0) \
    F(B_THREAD_SPAWN,     "thread_spawn",     1, 99,   "",   "i", 0,              "",            0) \
    F(B_THREAD_SPAWN_CONTAINED, "thread_spawn_contained", 1, 99, "", "i", 0,       "",            0) \
    F(B_THREAD_WAIT,      "thread_wait",      1,  2,   "",   "",  0,              "append to",   BF_OUTARG) \
    F(B_QPUT,             "qput",             1,  1,   "",   "",  0,              "",            0) \
    F(B_QGET,             "qget",             0,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_QPOLL,            "qpoll",            0,  0,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_QLIMIT,           "qlimit",           1,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_DEFAULT,          "default",          0,  0,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_COPY,             "copy",             1,  1,   "",   "",  0,              "",            0) \
    F(B_MOVE_FROM,        "move_from",        2,  2,   "",   "",  BR_SLICE,       "",            BF_MEMBER | BF_WRITE) \
    F(B_TO_BYTES,         "to_bytes",         1,  2,   "",   "",  BR_ANY,         "append to",   BF_MEMBER | BF_OUTARG) \
    F(B_BYTES_OF,         "bytes_of",         1,  1,   "",   "",  BR_ANY,         "",            BF_MEMBER) \
    F(B_FROM_BYTES,       "from_bytes",       1,  1,   "",   "",  0,              "",            BF_TYARGS) \
    F(B_EMBED_SHADER,     "embed_shader",     1, 99,   "",   "",  0,              "",            0) \
    F(B_EMBED_FILE,       "embed_file",       1,  1,   "",   "",  0,              "",            0) \
    F(B_LEN,              "len",              1,  1,   "",   "i", BR_ANY,         "",            BF_MEMBER | BF_PROPERTY) \
    F(B_CAP,              "cap",              1,  1,   "",   "i", BR_LIMITED,     "",            BF_MEMBER | BF_PROPERTY) \
    F(B_INDEX_OF,         "index_of",         2,  2,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER) \
    F(B_RANGE_INDEX,      "range_index",      2,  3,   "",   "i", BR_INDEXABLE,   "",            BF_MEMBER) \
    F(B_CHECK_LENGTH,     "check_length",     2,  3,   "i",  "i", BR_GROW | BR_GROWSHRINK, "",     BF_MEMBER) \
    F(B_PUSH,             "push",             2,  2,   "e",  "r", BR_GROWABLE,    "push into",   BF_MEMBER | BF_WRITE) \
    F(B_APPEND,           "append",           2,  2,   "a",  "",  BR_GROWABLE,    "append to",   BF_MEMBER | BF_WRITE) \
    F(B_POP,              "pop",              1,  1,   "",   "e", BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_POP_N,            "pop_n",            2,  2,   "i",  "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_RESIZE,           "resize",           2,  3,   "",   "",  BR_SHRINKABLE,  "resize",      BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_CLEAR,            "clear",            1,  1,   "",   "",  BR_SHRINKABLE,  "",            BF_MEMBER | BF_WRITE | BF_SHRINKS) \
    F(B_ALLOC_INDEX,      "alloc_index",      2,  2,   "e",  "i", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_ALLOC_REF,        "alloc_ref",        2,  2,   "e",  "r", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_FREE,             "free",             2,  2,   "i",  "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_REUSABLE) \
    F(B_ALLOC_SLICE,      "alloc_slice",      2,  2,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL) \
    F(B_REALLOC_SLICE,    "realloc_slice",    3,  3,   "",   "s", BR_GROW,        "allocate in", BF_MEMBER | BF_WRITE | BF_SLICEPOOL) \
    F(B_FREE_SLICE,       "free_slice",       2,  2,   "",   "",  BR_GROW,        "",            BF_MEMBER | BF_WRITE | BF_SLICEPOOL)

enum BuiltinKind {
    #define F(k, n, lo, hi, a, r, rv, gr, fl) k,
    BUILTINS
    #undef F
    B_COUNT
};

struct BuiltinDef {
    BuiltinKind kind;
    const char *name;
    int minargs, maxargs;      
    const char *args;          
    const char *rets;          
    int recv;                  
    const char *grows;         
    int flags;
};

inline const BuiltinDef builtindefs[] = {
    #define F(k, n, lo, hi, a, r, rv, gr, fl) { k, n, lo, hi, a, r, rv, gr, fl },
    BUILTINS
    #undef F
};


inline const BuiltinDef *LookupBuiltin(string_view name) {
    static const auto byname = [] {
        unordered_map<string_view, const BuiltinDef *> m;
        for (auto &d : builtindefs) m.emplace(d.name, &d);
        return m;
    }();
    auto it = byname.find(name);
    return it == byname.end() ? nullptr : it->second;
}


inline const BuiltinDef &BuiltinByKind(int kind) {
    assert(kind >= 0 && kind < (int)(sizeof(builtindefs) / sizeof(builtindefs[0])) &&
           builtindefs[kind].kind == kind);
    return builtindefs[kind];
}

}  
