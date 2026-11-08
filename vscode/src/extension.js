const vscode = require("vscode");

const DIAGNOSTIC = vscode.DiagnosticSeverity;
const RE = /^(.+?):(\d+): (error|warning): (.*)$/;

let output = null;
let diagnostics = null;
let checks = new Map();

function config(file) {
    return vscode.workspace.getConfiguration("faloya", file);
}

function ws(file) {
    const folder = vscode.workspace.getWorkspaceFolder(file);
    return folder ? folder.uri.fsPath : vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function resolveCompiler(file) {
    const setting = config(file).get("compilerPath");
    if (setting) return expand(setting, file);
    const root = ws(file);
    if (!root) return "faloya";
    const { existsSync } = require("fs");
    const { join } = require("path");
    for (const rel of ["faloya", join("build", "faloya"), join("build", "Release", "faloya"), join("build", "Debug", "faloya")]) {
        const candidate = join(root, rel);
        if (existsSync(candidate)) return candidate;
    }
    return "faloya";
}

function expand(text, file) {
    const folder = ws(file);
    return text.replace(/\$\{workspaceFolder\}/g, folder || "").replace(/\$\{file\}/g, file.fsPath || "");
}

function entryFile(file) {
    const setting = config(file).get("entryFile");
    if (setting) return expand(setting, file);
    return file.fsPath;
}

function runArgs(file) {
    const args = config(file).get("runArguments") || [];
    return args.length ? ["--", ...args] : [];
}

function toRange(line) {
    const l = Math.max(0, Number(line) - 1);
    return new vscode.Range(l, 0, l, 1000);
}

function parse(stderr) {
    const found = [];
    for (const line of stderr.split("\n")) {
        const m = RE.exec(line.trim());
        if (m) found.push({
            file: m[1],
            diagnostic: new vscode.Diagnostic(toRange(m[2]), m[4], m[3] === "warning" ? DIAGNOSTIC.Warning : DIAGNOSTIC.Error)
        });
    }
    return found;
}

function check(file, silent) {
    const key = file.fsPath;
    const prior = checks.get(key);
    if (prior) prior.kill();
    const child = require("child_process").spawn(resolveCompiler(file), ["check", entryFile(file), "--color", "never"], { cwd: ws(file) });
    checks.set(key, child);
    let stderr = "";
    child.stderr.on("data", (d) => { stderr += d; });
    const timer = setTimeout(() => child.kill(), config(file).get("checkTimeout"));
    child.on("close", () => {
        clearTimeout(timer);
        if (checks.get(key) === child) checks.delete(key);
        const byFile = new Map();
        for (const { file: f, diagnostic } of parse(stderr)) {
            if (!byFile.has(f)) byFile.set(f, []);
            byFile.get(f).push(diagnostic);
        }
        const scope = new Map(diagnostics.get(key) ? [[key, diagnostics.get(key)]] : []);
        for (const [f, ds] of scope) if (!byFile.has(f)) diagnostics.set(vscode.Uri.file(f), []);
        for (const [f, ds] of byFile) diagnostics.set(vscode.Uri.file(f), ds);
        if (!silent && stderr.trim()) output.appendLine(stderr.trim());
        if (!silent) vscode.commands.executeCommand("faloya.showOutput");
    });
}

function runInTerminal(file, command) {
    const term = vscode.window.createTerminal({ name: "Faloya", cwd: ws(file) });
    const args = runArgs(file).map((a) => (/[\s"']/.test(a) ? `"${a.replace(/"/g, '\\"')}"` : a)).join(" ");
    term.sendText(`${resolveCompiler(file)} ${command} ${entryFile(file)}${command === "run" || !command ? ` ${args}` : ""}`.replace(/\s+$/, ""));
    term.show(true);
}

function activate(ctx) {
    output = vscode.window.createOutputChannel("Faloya Compiler");
    diagnostics = vscode.languages.createDiagnosticCollection("faloya");
    ctx.subscriptions.push(output, diagnostics);

    const run = (file) => {
        file = file || vscode.window.activeTextEditor?.document.uri;
        if (!file) return;
        runInTerminal(file, "run");
    };
    const checkNow = (file) => {
        file = file || vscode.window.activeTextEditor?.document.uri;
        if (!file) return;
        check(file, false);
    };

    ctx.subscriptions.push(
        vscode.commands.registerCommand("faloya.run", run),
        vscode.commands.registerCommand("faloya.check", checkNow),
        vscode.commands.registerCommand("faloya.showOutput", () => output.show(true)),
        vscode.commands.registerCommand("faloya.build", (file) => { file = file || vscode.window.activeTextEditor?.document.uri; if (file) runInTerminal(file, "build"); }),
        vscode.commands.registerCommand("faloya.format", (file) => { file = file || vscode.window.activeTextEditor?.document.uri; if (file) runInTerminal(file, "format"); })
    );

    const save = (doc) => {
        if (doc.languageId !== "faloya" || !config(doc.uri).get("checkOnSave")) return;
        check(doc.uri, true);
    };
    ctx.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(save),
        vscode.workspace.onDidSaveTextDocument(save),
        vscode.workspace.onDidCloseTextDocument((doc) => {
            diagnostics.delete(doc.uri);
            const child = checks.get(doc.uri.fsPath);
            if (child) child.kill();
            checks.delete(doc.uri.fsPath);
        })
    );

    const provider = {
        provideTasks() {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== "faloya") return [];
            const kinds = ["run", "check", "build", "format"];
            return kinds.map((action) => {
                const task = new vscode.Task(
                    { type: "faloya", action },
                    vscode.TaskScope.Global,
                    `faloya: ${action}`,
                    "Faloya",
                    new vscode.ShellExecution([resolveCompiler(editor.document.uri), action, editor.document.uri.fsPath].join(" "))
                );
                task.group = action === "run" ? vscode.TaskGroup.Build : undefined;
                return task;
            });
        },
        resolveTask() { return undefined; }
    };
    ctx.subscriptions.push(vscode.tasks.registerTaskProvider("faloya", provider));
}

function deactivate() {
    for (const child of checks.values()) child.kill();
}

module.exports = { activate, deactivate };
