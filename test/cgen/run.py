import os, subprocess, sys, tempfile, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EXE = ROOT / "build" / "faloya"
CC = os.environ.get("CC", "cc")

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def translate(drv, out_c, inputs):
    return run([drv, out_c] + [str(i) for i in inputs])

def compile_and_run(cfile, binfile):
    r = run([CC, "-w", cfile, "-o", binfile])
    if r.returncode != 0:
        return None, r.stderr.strip()[:500]
    return r, None

def check_case(drv, case, td):
    cfile = os.path.join(td, "out.c")
    binfile = os.path.join(td, "out")
    g = translate(drv, cfile, [case])
    if g.returncode != 0 or "cgen:" in g.stdout or not os.path.exists(cfile):
        return "generate", g.stdout.strip() + g.stderr.strip()
    r, err = compile_and_run(cfile, binfile)
    if err is not None:
        return "cc", err
    got = run([binfile])
    ref = run([str(EXE), "run", str(case)])
    if ref.returncode != 0:
        return "ref", ref.stderr.strip()[:200]
    if got.stdout != ref.stdout:
        return "diff", f"--- ref\n{ref.stdout}\n--- got\n{got.stdout}"
    return None, ""

def main():
    cases = sorted(p for p in pathlib.Path(__file__).resolve().parent.glob("*.fal")
                   if p.name != "drv.fal")
    if not cases:
        print("no cgen cases")
        return 1
    stdlib = ROOT / "stdlib"
    drv_src = pathlib.Path(__file__).resolve().parent / "drv.fal"
    r = run([str(EXE), "build", str(drv_src), "-o", str(ROOT / "build" / "cgendrv")])
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        print("cgendrv build failed")
        return 1
    drv = str(ROOT / "build" / "cgendrv")
    ok = 0
    failed = []
    for case in cases:
        with tempfile.TemporaryDirectory() as td:
            stage, info = check_case(drv, case, td)
            if stage is None:
                ok += 1
            else:
                failed.append((case.name, stage, info))
    print(f"{ok}/{len(cases)} cgen cases passed")
    for name, stage, info in failed:
        print(f"== FAIL {name} ({stage})\n{info}")

    boot_c = ROOT / "build" / "cgen_boot.c"
    b = translate(drv, boot_c, [stdlib / "ast.fal", stdlib / "cgen.fal", drv_src])
    if b.returncode != 0 or "cgen:" in b.stdout or not boot_c.exists():
        print("== FAIL bootstrap (translate)\n" + b.stdout.strip() + b.stderr.strip())
        return 1
    boot_bin = ROOT / "build" / "bootdrv"
    r, err = compile_and_run(boot_c, boot_bin)
    if err is not None:
        print("== FAIL bootstrap (cc)\n" + err)
        return 1
    bok = 0
    for case in cases:
        with tempfile.TemporaryDirectory() as td:
            stage, info = check_case(str(boot_bin), case, td)
            if stage is None:
                bok += 1
            else:
                failed.append(("boot:" + case.name, stage, info))
    print(f"{bok}/{len(cases)} boot cases passed")
    boot2_c = ROOT / "build" / "cgen_boot2.c"
    b2 = translate(str(boot_bin), boot2_c, [stdlib / "ast.fal", stdlib / "cgen.fal", drv_src])
    if b2.returncode != 0 or "cgen:" in b2.stdout or not boot2_c.exists():
        print("== FAIL self (translate)\n" + b2.stdout.strip() + b2.stderr.strip())
        return 1
    if boot2_c.read_bytes() != boot_c.read_bytes():
        print("== FAIL self: boot2.c differs from boot.c")
        return 1
    print("self-translation is a fixed point")
    for name, stage, info in failed:
        print(f"== FAIL {name} ({stage})\n{info}")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
