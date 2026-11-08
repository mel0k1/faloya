






#pragma once

namespace faloya {


struct Place {
    int fileidx = -1;
    size_t start = 0, len = 0;
    bool Known() const { return fileidx >= 0; }
};


struct Occurrence {
    enum Kind {
        IDENT, DOT, TYPE, INIT,                     
        FN, STRUCT, ENUM, ALIAS, GLOBAL, LOCAL,     
        FIELD, VARIANT, PARAM, GENERIC, BINDER      
    };
    Kind kind = IDENT;
    size_t start = 0, len = 0;      
    string_view name;
    const Node *node = nullptr;     
                                    
    TypeExpr *type = nullptr;       
    SFunction *sf = nullptr;        
    SStruct *st = nullptr;          
    SEnum *en = nullptr;            
    SAlias *al = nullptr;           
    const Field *field = nullptr;   
    const SVariant *variant = nullptr;      
    const Param *param = nullptr;   
    const GenericParam *generic = nullptr;  
    int index = 0;                  
                                    
                                    
    bool callee = false;            
    bool isvar = false;             

    
    bool IsDeclaration() const { return kind >= FN; }
};


struct CallSpan {
    size_t start = 0, end = 0;
    const Call *call = nullptr;
};




struct NodeSpan {
    size_t start = 0, end = 0;
    const Node *node = nullptr;
};

struct FileIndex {
    int fileidx = 0;
    string uri;
    TextIndex text;
    vector<Occurrence> occs;        
    vector<CallSpan> calls;         
    vector<NodeSpan> spans;         

    
    const Occurrence *At(size_t offset) const {
        auto it = upper_bound(occs.begin(), occs.end(), offset,
                              [](size_t o, const Occurrence &x) { return o < x.start; });
        if (it == occs.begin()) return nullptr;
        --it;
        return offset < it->start + it->len ? &*it : nullptr;
    }
    
    const CallSpan *CallAt(size_t offset) const {
        const CallSpan *found = nullptr;
        for (auto &c : calls) {
            if (c.start > offset) break;
            if (offset <= c.end && (!found || c.start >= found->start)) found = &c;
        }
        return found;
    }
    
    
    vector<const NodeSpan *> SpansAt(size_t offset) const {
        vector<const NodeSpan *> found;
        for (auto &s : spans) {
            if (s.start > offset) break;
            if (offset <= s.end) found.push_back(&s);
        }
        return found;
    }
};

struct Analysis {
    unique_ptr<Ast> ast;
    bool parsed = false;            
    bool indexed = false;           
    bool built = false;             
    vector<Diagnostic> diags;       
    map<string, int> fileof;        
    vector<FileIndex> files;        
    unordered_map<const Node *, const FunVal *> funvals;  
    unordered_map<const Node *, vector<Node *>> clones;   
    
    
    unordered_set<const Node *> exemplarclones;
    
    
    
    
    
    unordered_map<const VarDef *, const VarDecl *> vardecls;
    unordered_map<const VarDef *, Place> defplaces;
    
    unordered_map<const Node *, const SVariant *> patternvariants;

    int FileOf(const string &key) const {
        auto it = fileof.find(key);
        return it == fileof.end() ? -1 : it->second;
    }

    
    
    template<typename F> const Node *Find(const Node *n, F pred) const {
        if (pred(n)) return n;
        if (auto it = clones.find(n); it != clones.end())
            for (auto c : it->second) if (pred(c)) return c;
        return nullptr;
    }

    bool FromExemplar(const Node *clone) const { return exemplarclones.count(clone) > 0; }
    
    
    bool InExemplar(const Node *n) const {
        if (auto it = clones.find(n); it != clones.end())
            for (auto c : it->second) if (FromExemplar(c)) return true;
        return false;
    }

    
    
    TypeExpr *TypeOf(const Node *n) const {
        auto c = Find(n, [](const Node *x) { return x->exprtype != nullptr; });
        return c ? c->exprtype : nullptr;
    }
    vector<TypeExpr *> TypesOf(const Node *n, size_t most) const {
        vector<TypeExpr *> types;
        set<string> seen;
        auto add = [&](const Node *x) {
            if (!x->exprtype || types.size() >= most) return;
            string s;
            x->exprtype->Dump(s);
            if (seen.insert(s).second) types.push_back(x->exprtype);
        };
        add(n);
        if (auto it = clones.find(n); it != clones.end()) for (auto c : it->second) add(c);
        return types;
    }

    
    
    const VarDecl *VarDeclOf(const VarDef *vd) const {
        auto it = vardecls.find(vd);
        return it == vardecls.end() ? nullptr : it->second;
    }

    
    
    int64_t OffsetOfView(int fileidx, string_view name) const {
        auto &src = *ast->sources[(size_t)fileidx].second;
        if (name.data() < src.data() || name.data() + name.size() > src.data() + src.size()) return -1;
        return name.data() - src.data();
    }
    Place PlaceOfView(int fileidx, string_view name) const {
        if (fileidx < 0 || fileidx >= (int)files.size() || name.empty()) return {};
        auto off = OffsetOfView(fileidx, name);
        return off < 0 ? Place {} : Place { fileidx, (size_t)off, name.size() };
    }

    
    
    void IndexFiles() {
        auto &a = *ast;
        files.resize(a.sources.size());
        for (size_t i = 0; i < a.sources.size(); i++) {
            auto &f = files[i];
            f.fileidx = (int)i;
            f.uri = UriOfPath(CanonicalPath(a.sources[i].first));
            f.text.Index(*a.sources[i].second);
            fileof[PathKey(a.sources[i].first)] = (int)i;
        }
    }

    
    
    
    
    
    
    void IndexSource() {
        if (indexed) return;
        indexed = true;
        auto &a = *ast;
        if (files.size() != a.sources.size()) IndexFiles();
        auto infile = [&](int fileidx) { return fileidx >= 0 && fileidx < (int)files.size(); };
        auto add = [&](Occurrence o, int fileidx, int64_t start) {
            if (start < 0 || !infile(fileidx) || !o.len) return;
            o.start = (size_t)start;
            files[(size_t)fileidx].occs.push_back(o);
        };
        auto view = [&](Occurrence o, int fileidx, string_view name) {
            o.name = name;
            o.len = name.size();
            add(o, fileidx, infile(fileidx) ? OffsetOfView(fileidx, name) : -1);
        };
        auto at = [&](Occurrence o, Line l, string_view name) {
            if (l.line <= 0 || !infile(l.fileidx)) return;
            o.name = name;
            o.len = name.size();
            add(o, l.fileidx, files[(size_t)l.fileidx].text.OffsetOf(l.line, l.col));
        };
        auto decl = [&](Occurrence::Kind k) { Occurrence o; o.kind = k; return o; };
        
        auto generics = [&](const vector<GenericParam> &gs) {
            for (auto &g : gs) {
                auto o = decl(Occurrence::GENERIC);
                o.generic = &g;
                at(o, g.line, g.name);
            }
        };
        auto fields = [&](const vector<Field> &fs) {
            for (auto &f : fs) {
                if (f.ispad) continue;
                auto o = decl(Occurrence::FIELD);
                o.field = &f;
                o.isvar = !f.isconst;
                at(o, f.line, f.name);
            }
        };
        for (auto sf : a.functions) {
            auto o = decl(Occurrence::FN);
            o.sf = sf;
            view(o, sf->line.fileidx, sf->name);
            generics(sf->generics);
            for (auto &p : sf->params) {
                auto po = decl(Occurrence::PARAM);
                po.sf = sf;
                po.param = &p;
                po.isvar = p.isvar;
                at(po, p.line, p.name);
            }
        }
        for (auto st : a.structs) {
            auto o = decl(Occurrence::STRUCT);
            o.st = st;
            view(o, st->line.fileidx, st->name);
            generics(st->generics);
            fields(st->fields);
        }
        for (auto en : a.enums) {
            auto o = decl(Occurrence::ENUM);
            o.en = en;
            view(o, en->line.fileidx, en->name);
            generics(en->generics);
            for (auto &v : en->variants) {
                auto vo = decl(Occurrence::VARIANT);
                vo.en = en;
                vo.variant = &v;
                at(vo, v.line, v.name);
                fields(v.fields);
            }
        }
        for (auto al : a.aliases) {
            auto o = decl(Occurrence::ALIAS);
            o.al = al;
            view(o, al->line.fileidx, al->name);
        }
        for (auto g : a.globals) {
            for (size_t i = 0; i < g->names.size(); i++) {
                auto o = decl(Occurrence::GLOBAL);
                o.node = g;
                o.index = (int)i;
                o.isvar = g->isvar;
                view(o, g->line.fileidx, g->names[i]);
            }
        }
        
        
        
        
        for (auto t : a.alltypes) {
            if (t->line.line <= 0 || !infile(t->line.fileidx)) continue;
            string_view leaf;
            switch (t->kind) {
                case TY_STRUCT:  leaf = t->struc->st->name; break;
                case TY_ENUM:    leaf = t->enu->en->name; break;
                case TY_GENERIC: leaf = t->named->name; break;
                case TY_VARIANT: leaf = t->var->name; break;
                default: continue;
            }
            auto &f = files[(size_t)t->line.fileidx];
            auto start = f.text.OffsetOf(t->line.line, t->line.col);
            if (start < 0) continue;
            
            if (t->kind == TY_VARIANT && (size_t)start < f.text.text.size() && f.text.text[(size_t)start] == '.') start++;
            auto len = f.text.NameLengthAt((size_t)start);
            if (!len) continue;
            auto written = f.text.text.substr((size_t)start, len);
            auto lastleaf = written.substr(written.rfind("::") == string_view::npos ? 0 : written.rfind("::") + 2);
            if (lastleaf != leaf) continue;
            Occurrence o;
            o.kind = Occurrence::TYPE;
            o.type = t;
            o.name = written;
            o.len = len;
            add(o, t->line.fileidx, start);
        }
        
        
        
        
        function<int64_t(Node *, bool)> visit = [&](Node *n, bool callee) -> int64_t {
            if (!n) return -1;
            auto fileidx = n->line.fileidx;
            if (auto id = Is<Ident>(n)) {
                Occurrence o;
                o.kind = Occurrence::IDENT;
                o.node = n;
                o.callee = callee;
                at(o, n->line, id->name);
            } else if (auto d = Is<Dot>(n)) {
                Occurrence o;
                o.kind = Occurrence::DOT;
                o.node = n;
                o.callee = callee;
                o.name = d->name;
                o.len = d->name.size();
                if (n->end.line > 0 && n->end.fileidx == fileidx && infile(fileidx)) {
                    auto end = files[(size_t)fileidx].text.OffsetOf(n->end.line, n->end.col);
                    if (end >= (int64_t)o.len) add(o, fileidx, end - (int64_t)o.len);
                }
            } else if (auto sl = Is<StructLit>(n)) {
                for (size_t i = 0; i < sl->inits.size(); i++) {
                    if (sl->inits[i].name.empty()) continue;
                    auto o = decl(Occurrence::INIT);
                    o.node = n;
                    o.index = (int)i;
                    view(o, fileidx, sl->inits[i].name);
                }
            } else if (auto vd = Is<VarDecl>(n)) {
                if (!vd->isglobal && !vd->results)
                    for (size_t i = 0; i < vd->names.size(); i++) {
                        auto o = decl(Occurrence::LOCAL);
                        o.node = n;
                        o.index = (int)i;
                        o.isvar = vd->isvar;
                        view(o, fileidx, vd->names[i]);
                    }
            } else if (auto fl = Is<ForLoop>(n)) {
                auto o = decl(Occurrence::BINDER);
                o.node = n;
                o.isvar = fl->byref;
                view(o, fileidx, fl->var);
                if (!fl->idxvar.empty()) {
                    o.index = 1;
                    view(o, fileidx, fl->idxvar);
                }
            } else if (Is<MatchExpr>(n) || Is<ConstFor>(n) || Is<ConstIf>(n)) {
                
                
                vector<const vector<MatchArm> *> lists;
                if (auto m = Is<MatchExpr>(n)) lists.push_back(&m->arms);
                else if (auto cf = Is<ConstFor>(n)) lists.push_back(&cf->arms);
                else for (auto &br : Is<ConstIf>(n)->branches) lists.push_back(&br.arms);
                int index = 0;
                for (auto arms : lists) {
                    for (auto &arm : *arms) {
                        auto i = index++;
                        if (arm.pat.binder.empty()) continue;
                        auto o = decl(Occurrence::BINDER);
                        o.node = n;
                        o.index = i;
                        o.isvar = arm.pat.byref;
                        view(o, fileidx, arm.pat.binder);
                    }
                }
            } else if (auto fv = Is<FunVal>(n)) {
                funvals[fv->body] = fv;
                for (size_t i = 0; i < fv->params.size(); i++) {
                    auto o = decl(Occurrence::BINDER);
                    o.node = n;
                    o.index = (int)i;
                    o.isvar = fv->params[i].isvar;
                    at(o, fv->params[i].line, fv->params[i].name);
                }
            }
            int64_t start = infile(fileidx) && n->line.line > 0
                ? files[(size_t)fileidx].text.OffsetOf(n->line.line, n->line.col) : -1;
            auto child = [&](Node *ch, bool iscallee) {
                auto s = visit(ch, iscallee);
                if (s >= 0 && ch->line.fileidx == fileidx && (start < 0 || s < start)) start = s;
            };
            if (auto c = Is<Call>(n)) {
                if (start >= 0 && n->end.line > 0) {
                    auto &f = files[(size_t)fileidx];
                    auto end = f.text.OffsetOf(n->end.line, n->end.col);
                    if (end >= start) f.calls.push_back({ (size_t)start, (size_t)end, c });
                }
                child(c->callee, true);
                for (auto arg : c->args) child(arg, false);
                if (c->trailing) child(c->trailing, false);
            } else {
                n->Children([&](Node *ch) { child(ch, false); });
            }
            if (start >= 0 && n->end.line > 0 && n->end.fileidx == fileidx) {
                auto &f = files[(size_t)fileidx];
                auto end = f.text.OffsetOf(n->end.line, n->end.col);
                if (end >= start) f.spans.push_back({ (size_t)start, (size_t)end, n });
            }
            return start;
        };
        for (auto sf : a.functions) {
            if (sf->body) visit(sf->body, false);
            for (auto &p : sf->params) if (p.defaultval) visit(p.defaultval, false);
        }
        for (auto st : a.structs)
            for (auto &f : st->fields) if (f.defaultval) visit(f.defaultval, false);
        for (auto en : a.enums)
            for (auto &v : en->variants)
                for (auto &f : v.fields) if (f.defaultval) visit(f.defaultval, false);
        for (auto g : a.globals) {
            for (auto init : g->inits) visit(init, false);
        }
        
        for (auto d : a.topdecls) {
            if (d->line.line <= 0 || d->end.line <= 0 || !infile(d->line.fileidx)) continue;
            auto &f = files[(size_t)d->line.fileidx];
            auto start = f.text.OffsetOf(d->line.line, d->line.col);
            auto end = f.text.OffsetOf(d->end.line, d->end.col);
            if (start >= 0 && end >= start) f.spans.push_back({ (size_t)start, (size_t)end, d });
        }
        for (auto &f : files) {
            stable_sort(f.occs.begin(), f.occs.end(),
                        [](const Occurrence &x, const Occurrence &y) { return x.start < y.start; });
            f.occs.erase(unique(f.occs.begin(), f.occs.end(),
                                [](const Occurrence &x, const Occurrence &y) { return x.start == y.start; }),
                         f.occs.end());
            stable_sort(f.calls.begin(), f.calls.end(),
                        [](const CallSpan &x, const CallSpan &y) { return x.start < y.start; });
            stable_sort(f.spans.begin(), f.spans.end(), [](const NodeSpan &x, const NodeSpan &y) {
                return x.start != y.start ? x.start < y.start : x.end > y.end;
            });
        }
    }

    
    void Build() {
        if (built) return;
        built = true;
        IndexSource();
        auto &a = *ast;
        auto infile = [&](int fileidx) { return fileidx >= 0 && fileidx < (int)files.size(); };
        
        
        
        
        
        
        
        auto bind = [&](const VarDef *vd, Place p) { if (vd && p.Known()) defplaces[vd] = p; };
        auto exemplar = false;
        function<void(Node *)> mark = [&](Node *n) {
            if (!n) return;
            if (n->origin) {
                clones[n->origin].push_back(n);
                if (exemplar) exemplarclones.insert(n);
            }
            auto fileidx = n->Origin()->line.fileidx;
            if (auto v = Is<VarDecl>(n)) {
                for (size_t i = 0; i < v->defs.size() && i < v->names.size(); i++) {
                    if (!v->defs[i]) continue;
                    vardecls[v->defs[i]] = Is<VarDecl>(n->Origin());
                    bind(v->defs[i], PlaceOfView(fileidx, v->names[i]));
                }
            } else if (auto fl = Is<ForLoop>(n)) {
                bind(fl->vdef, PlaceOfView(fileidx, fl->var));
                bind(fl->idxdef, PlaceOfView(fileidx, fl->idxvar));
            } else if (auto m = Is<MatchExpr>(n)) {
                for (auto &arm : m->arms) {
                    bind(arm.binder, PlaceOfView(fileidx, arm.pat.binder));
                    if (arm.variants.size() != arm.pat.items.size()) continue;
                    for (size_t k = 0; k < arm.variants.size(); k++)
                        if (arm.pat.items[k].lo && arm.variants[k])
                            patternvariants[arm.pat.items[k].lo->Origin()] = arm.variants[k];
                }
            } else if (auto c = Is<Call>(n)) {
                if (c->fvbody) {
                    
                    if (auto it = funvals.find(c->fvbody->Origin()); it != funvals.end())
                        for (size_t i = 0; i < c->fvparams.size() && i < it->second->params.size(); i++) {
                            auto &p = it->second->params[i];
                            if (p.line.line <= 0 || !infile(p.line.fileidx)) continue;
                            auto off = files[(size_t)p.line.fileidx].text.OffsetOf(p.line.line, p.line.col);
                            if (off >= 0) bind(c->fvparams[i], Place { p.line.fileidx, (size_t)off, p.name.size() });
                        }
                    mark(c->fvbody);
                }
                if (c->defaultinit) mark(c->defaultinit);
            }
            n->Children(mark);
        };
        for (auto sp : a.fnspecs) if (sp->body) mark(sp->body);
        for (auto g : a.globals) {
            for (auto init : g->inits) mark(init);
            for (size_t i = 0; i < g->defs.size() && i < g->names.size(); i++) {
                if (!g->defs[i]) continue;
                vardecls[g->defs[i]] = g;
                bind(g->defs[i], PlaceOfView(g->line.fileidx, g->names[i]));
            }
        }
        
        
        
        
        exemplar = true;
        for (auto sp : a.exemplarspecs) {
            auto own = false;
            for (auto sf = sp->sf; sf && !own; sf = sf->outer) own = sf == sp->exemplar;
            if (own && sp->body) mark(sp->body);
        }
    }
};

}  
