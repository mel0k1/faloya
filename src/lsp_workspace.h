





#pragma once

namespace faloya {


vector<string> StdlibDirs(const string &stdlibdir, const string &exe);
void ParseProgram(Ast &ast, const string &rootpath, const vector<string> &stdlibdirs,
                  const string &exedir, const SourceProvider &sources);
int RunOnCompilerStack(const function<int()> &fn);

struct Document {
    string path;        
    string text;
};





struct PublishedDiagnostic {
    string json;
    size_t keylen = 0;
    string_view Key() const { return string_view(json).substr(0, keylen); }
};

struct Root {
    string path, key;               
    bool configured = false;        
    unique_ptr<Analysis> analysis;  
    set<string> files;              
    
    
    map<string, vector<PublishedDiagnostic>> published;
    bool dirty = true;
    chrono::steady_clock::time_point due;

    
    bool Reaches(const string &k) const { return k == key || files.count(k) > 0; }
};

struct Workspace {
    Transport &rpc;
    string exe;
    vector<string> stdlibdirs;
    map<string, Document> open;     
    vector<unique_ptr<Root>> roots;
    
    
    
    set<string> clientcommands;
    
    
    
    vector<string> refreshes;
    int64_t requests = 0;
    
    static constexpr int DEBOUNCE_MS = 150;
    
    mutex reapm;
    condition_variable reapcv;
    deque<unique_ptr<Analysis>> reapq;
    bool reapstop = false;
    thread reaper;

    Workspace(Transport &r, const string &e, const string &stdlib) : rpc(r), exe(e) {
        stdlibdirs = StdlibDirs(stdlib, exe);
    }

    ~Workspace() {
        {
            lock_guard<mutex> lock(reapm);
            reapstop = true;
        }
        reapcv.notify_all();
        if (reaper.joinable()) reaper.join();
    }

    
    
    
    
    
    
    void Reap(unique_ptr<Analysis> old) {
        if (!old) return;
        lock_guard<mutex> lock(reapm);
        reapq.push_back(std::move(old));
        if (!reaper.joinable())
            reaper = thread([this] {
                unique_lock<mutex> lock(reapm);
                for (;;) {
                    reapcv.wait(lock, [&] { return reapstop || !reapq.empty(); });
                    if (reapq.empty()) return;
                    auto a = std::move(reapq.front());
                    reapq.pop_front();
                    lock.unlock();
                    a.reset();
                    lock.lock();
                }
            });
        reapcv.notify_all();
    }

    
    
    void Configure(const Json *options) {
        if (!options || options->kind != Json::OBJECT) return;
        if (auto lib = options->Get("stdlib"); lib && lib->kind == Json::STRING && !lib->str.empty())
            stdlibdirs = StdlibDirs(lib->str, exe);
        if (auto entries = options->Get("entryFiles"); entries && entries->kind == Json::ARRAY) {
            set<string> keep;
            for (auto &e : entries->arr) {
                if (e.kind != Json::STRING || e.str.empty()) continue;
                auto key = PathKey(e.str);
                keep.insert(key);
                if (auto r = FindRoot(key)) r->configured = true;
                else AddRoot(e.str, true);
            }
            for (auto &r : roots)
                if (r->configured && !keep.count(r->key)) r->configured = false;
            PruneUnreached();
        }
        for (auto &r : roots) Dirty(*r, 0);
    }

    Root *FindRoot(const string &key) {
        for (auto &r : roots) if (r->key == key) return r.get();
        return nullptr;
    }

    Root *AddRoot(const string &path, bool configured) {
        auto r = make_unique<Root>();
        r->path = CanonicalPath(path);
        r->key = PathKey(path);
        r->configured = configured;
        Dirty(*r, 0);
        roots.push_back(std::move(r));
        return roots.back().get();
    }

    
    
    void RemoveRoot(Root *root) {
        auto it = find_if(roots.begin(), roots.end(), [&](const unique_ptr<Root> &r) { return r.get() == root; });
        if (it == roots.end()) return;
        auto gone = std::move(*it);
        roots.erase(it);
        Reap(std::move(gone->analysis));
        for (auto &[uri, diags] : gone->published) PublishDiagnostics(uri);
    }

    bool Reached(const string &key) const {
        for (auto &r : roots) if (r->Reaches(key)) return true;
        return false;
    }

    
    
    void RootOpenFiles() {
        for (auto &[key, doc] : open) if (!Reached(key)) AddRoot(doc.path, false);
    }

    
    
    
    
    void PruneUnreached() {
        for (size_t i = 0; i < roots.size();) {
            auto &r = *roots[i];
            auto reached = false;
            for (auto &o : roots) if (o.get() != &r && o->Reaches(r.key)) reached = true;
            if (!r.configured && (reached || !open.count(r.key))) RemoveRoot(&r);
            else i++;
        }
        RootOpenFiles();
    }

    void Dirty(Root &r, int delayms) {
        r.dirty = true;
        r.due = chrono::steady_clock::now() + chrono::milliseconds(delayms);
    }

    
    
    void Touch(const string &key) {
        for (auto &r : roots) if (r->Reaches(key)) Dirty(*r, DEBOUNCE_MS);
        RootOpenFiles();
    }

    void Open(const string &uri, const string &text) {
        auto path = PathOfUri(uri);
        auto key = PathKey(path);
        open[key] = Document { CanonicalPath(path), text };
        Touch(key);
    }

    
    
    static string Splice(const string &text, const Json &range, const string &replacement) {
        TextIndex t;
        t.Index(text);
        auto pos = [&](const Json *p) {
            return t.OffsetOfPosition(JsonInt(p ? p->Get("line") : nullptr), JsonInt(p ? p->Get("character") : nullptr));
        };
        auto from = range.kind == Json::OBJECT ? pos(range.Get("start")) : 0;
        auto to = range.kind == Json::OBJECT ? pos(range.Get("end")) : text.size();
        from = min(from, text.size());
        to = min(max(to, from), text.size());
        auto out = text;
        return out.replace(from, to - from, replacement);
    }

    
    
    
    
    
    void ChangeEdits(const string &uri, const Json &changes) {
        if (changes.kind != Json::ARRAY || changes.arr.empty()) return;
        auto key = PathKey(PathOfUri(uri));
        auto it = open.find(key);
        if (it == open.end()) return;
        for (auto &c : changes.arr) {
            if (c.kind != Json::OBJECT) continue;
            if (auto r = c.Get("range"); r && r->kind == Json::OBJECT)
                it->second.text = Splice(it->second.text, *r, JsonStr(c.Get("text")));
            else
                it->second.text = JsonStr(c.Get("text"));
        }
        Touch(key);
    }

    void Close(const string &uri) {
        auto key = PathKey(PathOfUri(uri));
        open.erase(key);
        if (auto r = FindRoot(key); r && !r->configured) {
            RemoveRoot(r);
            RootOpenFiles();
        } else {
            Touch(key);   
        }
    }

    
    optional<chrono::steady_clock::time_point> NextDue() const {
        optional<chrono::steady_clock::time_point> due;
        for (auto &r : roots)
            if (r->dirty && (!due || r->due < *due)) due = r->due;
        return due;
    }

    
    Root *FirstDue() {
        Root *next = nullptr;
        for (auto &r : roots)
            if (r->dirty && (!next || r->due < next->due)) next = r.get();
        return next;
    }

    
    bool AnalyzeDue() {
        auto next = FirstDue();
        if (!next || next->due > chrono::steady_clock::now()) return false;
        Analyze(*next);
        return true;
    }

    
    
    
    
    
    void Settle() {
        while (auto next = FirstDue()) Analyze(*next, false);
    }

    
    const string *TextOf(const string &path) {
        auto it = open.find(PathKey(path));
        return it == open.end() ? nullptr : &it->second.text;
    }

    
    
    
    
    
    void Analyze(Root &r, bool cancellable = true) {
        auto an = make_unique<Analysis>();
        an->ast = make_unique<Ast>();
        auto &ast = *an->ast;
        ast.cancel = cancellable ? &rpc.cancel : nullptr;
        ast.exemplars = true;
        ast.sink = [&](const Diagnostic &d) { an->diags.push_back(d); };
        SourceProvider sources = [&](const string &path) { return TextOf(path); };
        try {
            RunOnCompilerStack([&]() -> int {
                
                
                
                ParseProgram(ast, r.path, stdlibdirs, DirOf(exe), sources);
                ResolveTypeNames(ast);
                an->parsed = true;
                
                
                
                
                
                
                an->IndexSource();
                TypeCheckProgram(ast, false, true);
                an->Build();
                if (!ast.errors.empty()) return 0;
                
                
                Optimizer opt(ast, 2);
                BCE bce(ast);
                bce.RunAll();
                if (!bce.checkfreefails.empty()) throw bce.CheckfreeError();
                return 0;
            });
        } catch (CompileError &e) {
            if (e.diags.empty()) {
                Diagnostic d;
                d.message = d.text = e.msg;
                an->diags.push_back(std::move(d));
            } else {
                an->diags.insert(an->diags.end(), e.diags.begin(), e.diags.end());
            }
        } catch (Cancelled &) {
            return;
        }
        r.dirty = false;
        r.files.clear();
        for (auto &[path, text] : ast.sources) r.files.insert(PathKey(path));
        if (!an->indexed) an->IndexFiles();
        Publish(r, *an);
        if (an->parsed || !r.analysis) {
            if (!an->built) an->Build();
            Reap(std::move(r.analysis));
            r.analysis = std::move(an);
            
            
            
            for (auto &method : refreshes) Request(method);
        }
        PruneUnreached();
    }

    
    void Request(const string &method) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Member("id", ++requests);
        j.Member("method", method);
        j.EndObject();
        rpc.Send(j.s);
    }

    
    
    bool Locate(const string &key, Analysis *&an, int &fileidx) {
        for (auto configured : { true, false })
            for (auto &r : roots) {
                if (r->configured != configured || !r->analysis) continue;
                auto f = r->analysis->FileOf(key);
                if (f < 0) continue;
                an = r->analysis.get();
                fileidx = f;
                return true;
            }
        return false;
    }

    

    
    
    static void WriteRangeOf(JsonOut &j, const DiagLoc &w, const TextIndex &t) {
        auto line = max(0, w.line - 1);
        auto ltext = t.LineText(line);
        size_t sc = 0, ec = 0;
        auto eline = line;
        if (w.col > 0) {
            sc = min((size_t)(w.col - 1), ltext.size());
            if (w.endline > 0) {
                eline = max(0, w.endline - 1);
                ec = min((size_t)max(0, w.endcol - 1), t.LineText(eline).size());
            } else {
                auto len = t.NameLengthAt(t.linestarts[(size_t)min(line, t.Lines() - 1)] + sc);
                ec = min(sc + max<size_t>(len, 1), ltext.size());
                if (ec < sc) ec = sc;
            }
        } else {
            auto first = ltext.find_first_not_of(" \t");
            sc = first == string_view::npos ? 0 : first;
            ec = ltext.size();
        }
        j.Key("range");
        j.BeginObject();
        j.Key("start");
        j.BeginObject();
        j.Member("line", (int64_t)line);
        j.Member("character", (int64_t)Utf16Col(ltext, sc));
        j.EndObject();
        j.Key("end");
        j.BeginObject();
        j.Member("line", (int64_t)eline);
        j.Member("character", (int64_t)Utf16Col(t.LineText(eline), ec));
        j.EndObject();
        j.EndObject();
    }

    static PublishedDiagnostic RenderDiagnostic(const Diagnostic &d, const Analysis &an, int fileidx) {
        auto &files = an.files;
        JsonOut j;
        j.BeginObject();
        DiagLoc where = d.where;
        if (where.fileidx != fileidx) where = DiagLoc { fileidx, 1, 0, 0, 0 };
        WriteRangeOf(j, where, files[(size_t)fileidx].text);
        j.Member("severity", (int64_t)(d.warning ? 2 : 1));
        j.Member("source", "faloya");
        
        auto message = d.message;
        for (auto &r : d.related)
            if (r.where.fileidx < 0 || r.where.fileidx >= (int)files.size() || r.where.line <= 0)
                Append(message, "\n", r.message);
        j.Member("message", message);
        PublishedDiagnostic p;
        p.keylen = j.s.size();
        auto any = false;
        for (auto &r : d.related) {
            if (r.where.fileidx < 0 || r.where.fileidx >= (int)files.size() || r.where.line <= 0) continue;
            if (!any) {
                j.Key("relatedInformation");
                j.BeginArray();
                any = true;
            }
            j.BeginObject();
            j.Key("location");
            j.BeginObject();
            j.Member("uri", files[(size_t)r.where.fileidx].uri);
            WriteRangeOf(j, r.where, files[(size_t)r.where.fileidx].text);
            j.EndObject();
            j.Member("message", r.message);
            j.EndObject();
        }
        if (any) j.EndArray();
        j.EndObject();
        p.json = std::move(j.s);
        return p;
    }

    
    
    
    void PublishDiagnostics(const string &uri) {
        JsonOut j;
        j.BeginObject();
        j.Member("jsonrpc", "2.0");
        j.Member("method", "textDocument/publishDiagnostics");
        j.Key("params");
        j.BeginObject();
        j.Member("uri", uri);
        j.Key("diagnostics");
        j.BeginArray();
        set<string_view> seen;
        for (auto &r : roots)
            if (auto it = r->published.find(uri); it != r->published.end())
                for (auto &d : it->second)
                    if (seen.insert(d.Key()).second) j.Raw(d.json);
        j.EndArray();
        j.EndObject();
        j.EndObject();
        rpc.Send(j.s);
    }

    
    
    
    
    
    
    void Publish(Root &r, const Analysis &an) {
        auto &ast = *an.ast;
        map<int, vector<const Diagnostic *>> perfile;
        vector<const Diagnostic *> nowhere;
        for (auto &d : an.diags) {
            if (d.where.fileidx >= 0 && d.where.fileidx < (int)ast.sources.size() && d.where.line > 0)
                perfile[d.where.fileidx].push_back(&d);
            else nowhere.push_back(&d);
        }
        map<string, vector<PublishedDiagnostic>> published;
        for (size_t i = 0; i < ast.sources.size(); i++) {
            auto &diags = published[an.files[i].uri];
            for (auto d : perfile[(int)i]) diags.push_back(RenderDiagnostic(*d, an, (int)i));
            if (i == 0) for (auto d : nowhere) diags.push_back(RenderDiagnostic(*d, an, 0));
        }
        if (ast.sources.empty() && !nowhere.empty()) {
            
            auto &diags = published[UriOfPath(r.path)];
            for (auto d : nowhere) {
                JsonOut j;
                j.BeginObject();
                j.Key("range");
                j.Raw("{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}}");
                j.Member("severity", (int64_t)(d->warning ? 2 : 1));
                j.Member("source", "faloya");
                j.Member("message", d->message);
                j.EndObject();
                diags.push_back({ j.s, j.s.size() });
            }
        }
        swap(r.published, published);
        for (auto &[uri, diags] : r.published) PublishDiagnostics(uri);
        for (auto &[uri, diags] : published) if (!r.published.count(uri)) PublishDiagnostics(uri);
    }
};

}  
