import os, subprocess, sys, tempfile, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "faloya"
CC = os.environ.get("CC", "cc")

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def main():
    cases = sorted(p for p in pathlib.Path(__file__).resolve().parent.glob("*.fal")
                   if p.name != "drv.fal")
    if not cases:
        print("no cgen cases")
        return 1
    drv = pathlib.Path(__file__).resolve().parent / "drv.fal"
    r = run([str(EXE), "build", str(drv), "-o", str(ROOT / "build" / "cgendrv")])
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        print("cgendrv build failed")
        return 1
    ok = 0
    failed = []
    for case in cases:
        with tempfile.TemporaryDirectory() as td:
            cfile = os.path.join(td, "out.c")
            binfile = os.path.join(td, "out")
            g = run([str(ROOT / "build" / "cgendrv"), str(case), cfile])
            if g.returncode != 0 or "cgen:" in g.stdout:
                failed.append((case.name, "generate", g.stdout.strip() + g.stderr.strip()))
                continue
            cc = run([CC, "-w", cfile, "-o", binfile])
            if cc.returncode != 0:
                failed.append((case.name, "cc", cc.stderr.strip()[:400]))
                continue
            got = run([binfile])
            ref = run([str(EXE), "run", str(case)])
            if ref.returncode != 0:
                failed.append((case.name, "ref", ref.stderr.strip()[:200]))
                continue
            if got.stdout != ref.stdout:
                failed.append((case.name, "diff", f"--- ref\n{ref.stdout}\n--- got\n{got.stdout}"))
                continue
            ok += 1
    print(f"{ok}/{len(cases)} cgen cases passed")
    for name, stage, info in failed:
        print(f"== FAIL {name} ({stage})\n{info}")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
