















#pragma once
#include "expansion_bind.h"

namespace faloya {

inline void ResolveTypeNames(Ast &ast) {
    BindExpansionNames(ast);
    
    
    
    set<const void *> reported;
    vector<Diagnostic> errors;
    auto ErrorAt = [&](const TypeExpr *t, const void *detail, const string &msg,
                       string_view written = {}) {
        if (!reported.insert(detail).second) return;
        Diagnostic d;
        d.where = LocOf(t->line);
        if (!written.empty() && t->line.col > 0) {
            d.where.endline = t->line.line;
            d.where.endcol = t->line.col + (int)written.size();
        }
        d.message = msg;
        errors.push_back(std::move(d));
    };
    
    
    map<const void *, SStruct *> unknowns;
    auto MakeUnknown = [&](TypeExpr *t, const void *detail, string_view name) {
        auto &st = unknowns[detail];
        if (!st) {
            st = new SStruct();
            st->name = st->qname = ast.Intern(string(name));
            st->line = st->end = t->line;
            st->broken = st->unknown = true;
            ast.unknowntypes.push_back(st);
        }
        auto d = ast.NewDetail<TypeStruct>();
        d->st = st;
        t->kind = TY_STRUCT;
        t->struc = d;
    };

    
    
    
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto nm = t->named;
        if (auto s = ast.LookupStruct(nm->name, nm->ns)) {
            if (nm->varmode)
                ErrorAt(t, nm, cat("struct ", nm->name,
                                   " has no variable mode (.. applies to enums)"));
            auto d = ast.NewDetail<TypeStruct>();
            d->st = s;
            d->args = nm->args;
            t->kind = TY_STRUCT;
            t->struc = d;
        } else if (auto e = ast.LookupEnum(nm->name, nm->ns)) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = e;
            d->args = nm->args;
            d->varmode = nm->varmode;
            t->kind = TY_ENUM;
            t->enu = d;
        } else if (!ast.LookupAlias(nm->name, nm->ns)) {
            if (SplitName(nm->name, nm->ns).qualified) {
                ErrorAt(t, nm, cat("unknown type: ", nm->name), nm->name);
                MakeUnknown(t, nm, nm->name);
            } else {
                t->kind = TY_GENERIC;  
            }
        }
        
    }

    
    
    
    
    
    function<void(TypeExpr *, const vector<GenericParam> *)> unknownin =
        [&](TypeExpr *t, const vector<GenericParam> *generics) {
            if (!t) return;
            auto each = [&](const vector<TypeExpr *> &ts) {
                for (auto child : ts) unknownin(child, generics);
            };
            switch (t->kind) {
                case TY_GENERIC: {
                    auto nm = t->named;
                    each(nm->args);
                    if (nm->expansionbinding) break;
                    if (generics)
                        for (auto &g : *generics)
                            if (g.name == nm->name) return;
                    ErrorAt(t, nm, cat("unknown type: ", nm->name), nm->name);
                    MakeUnknown(t, nm, nm->name);
                    break;
                }
                case TY_ARRAY: unknownin(t->arr->sub, generics); break;
                case TY_REF: unknownin(t->ref->sub, generics); break;
                case TY_SLICE: unknownin(t->sub, generics); break;
                case TY_VARIANT: unknownin(t->var->adt, generics); break;
                case TY_STRUCT: each(t->struc->args); break;
                case TY_ENUM: each(t->enu->args); break;
                case TY_UNRESOLVED: each(t->named->args); break;
                case TY_FN: each(t->fn->args); each(t->fn->rets); break;
                default: break;
            }
        };
    for (auto st : ast.structs)
        for (auto &f : st->fields) unknownin(f.type, &st->generics);
    for (auto en : ast.enums)
        for (auto &v : en->variants)
            for (auto &f : v.fields) unknownin(f.type, &en->generics);
    for (auto a : ast.aliases) unknownin(a->type, nullptr);

    
    
    
    
    
    
    unordered_map<SAlias *, int> aliasstate;  
    function<void(TypeExpr *)> checkaliases = [&](TypeExpr *t) {
        auto each = [&](const vector<TypeExpr *> &ts) {
            for (auto child : ts) checkaliases(child);
        };
        switch (t->kind) {
            case TY_UNRESOLVED: {
                auto a = ast.LookupAlias(t->named->name, t->named->ns);
                if (aliasstate[a] == 1) {
                    
                    
                    ErrorAt(t, t->named, cat("type alias cycle involving: ", t->named->name));
                    MakeUnknown(t, t->named, t->named->name);
                    break;
                }
                if (aliasstate[a] == 2) break;
                aliasstate[a] = 1;
                checkaliases(a->type);
                aliasstate[a] = 2;
                break;
            }
            case TY_ARRAY: checkaliases(t->arr->sub); break;
            case TY_REF: checkaliases(t->ref->sub); break;
            case TY_SLICE: checkaliases(t->sub); break;
            case TY_VARIANT: checkaliases(t->var->adt); break;
            case TY_STRUCT: each(t->struc->args); break;
            case TY_ENUM: each(t->enu->args); break;
            case TY_FN: each(t->fn->args); each(t->fn->rets); break;
            default: break;
        }
    };
    for (auto a : ast.aliases) checkaliases(a->type);

    
    
    
    
    for (auto t : ast.alltypes) {
        if (t->kind != TY_UNRESOLVED) continue;
        auto use = t->named;
        
        if (!use->args.empty())
            ErrorAt(t, use, cat("type alias ", use->name, " takes no type arguments"));
        auto target = t;
        for (auto depth = 0; target->kind == TY_UNRESOLVED; depth++) {
            assert(depth <= (int)ast.aliases.size());   
            target = ast.LookupAlias(target->named->name, target->named->ns)->type;
        }
        auto varmode = use->varmode;
        if (varmode && target->kind == TY_ENUM && target->enu->varmode) {
            ErrorAt(t, use, "type is already variable-mode");
            varmode = false;
        } else if (varmode && target->kind != TY_ENUM) {
            
            
            
            if (!(target->kind == TY_STRUCT && target->struc->st->unknown))
                ErrorAt(t, use, "variable mode (..) requires an ADT type");
            varmode = false;
        }
        if (varmode) {
            auto d = ast.NewDetail<TypeEnum>();
            d->en = target->enu->en;
            d->args = target->enu->args;
            d->varmode = true;
            t->kind = TY_ENUM;
            t->enu = d;
        } else {
            auto line = t->line;
            auto cq = t->cq;
            *t = *target;
            t->line = line;
            t->cq = t->cq || cq;   
        }
        t->aliasuse = true;
    }

    
    for (auto t : ast.alltypes) {
        if (t->kind != TY_VARIANT) continue;
        auto adt = t->var->adt;
        if (adt->kind == TY_STRUCT && adt->struc->st->unknown) {
            MakeUnknown(t, t->var, cat(adt->struc->st->name, ".", t->var->name));
            continue;
        }
        if (adt->kind != TY_ENUM) continue;  
        auto name = t->var->name;
        auto en = adt->enu->en;
        auto found = en->FindVariant(name);
        if (!found) {
            
            if (!en->broken)
                ErrorAt(t, t->var, cat("enum ", en->name, " has no variant named ", name));
            MakeUnknown(t, t->var, cat(en->qname, ".", name));
            continue;
        }
        t->var->variant = found;  
    }

    std::stable_sort(errors.begin(), errors.end(), [](const Diagnostic &a, const Diagnostic &b) {
        return tie(a.where.fileidx, a.where.line, a.where.col) <
               tie(b.where.fileidx, b.where.line, b.where.col);
    });
    for (auto &d : errors) ast.Report(std::move(d));
}

}  
