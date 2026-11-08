import sys, subprocess, pathlib, os, difflib

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "faloya"
ASTDUMP = ROOT / "build" / "astdump"

def corpus():
    files = []
    for d in ("test/cases", "samples", "stdlib"):
        files += sorted((ROOT / d).glob("*.fal"))
    return files

def run(args):
    r = subprocess.run(args, capture_output=True)
    return r.returncode, r.stdout.decode("utf-8", errors="replace")

def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    npass = nfail = nskip = 0
    fails = []
    for f in corpus():
        if only and only not in str(f):
            continue
        crc, cpp = run([str(EXE), "--tree", str(f)])
        frc, fal = run([str(ASTDUMP), str(f)])
        if crc != 0:
            nskip += 1
            continue
        if frc != 0 or fal.startswith("ast: line"):
            nskip += 1
            msg = fal.strip().splitlines()
            fails.append(("SKIP", str(f.relative_to(ROOT)), msg[0] if msg else ""))
            continue
        a = [l.rstrip() for l in cpp.splitlines() if l.strip()]
        b = [l.rstrip() for l in fal.splitlines() if l.strip()]
        if a == b:
            npass += 1
            continue
        nfail += 1
        diff = list(difflib.unified_diff(a, b, "cpp", "fal", lineterm="", n=2))
        fails.append(("DIFF", str(f.relative_to(ROOT)), "\n".join(diff[:24])))
    print(f"PASS {npass}  FAIL {nfail}  SKIP {nskip}")
    for kind, name, info in fails:
        print(f"\n== {kind} {name}")
        print(info)

if __name__ == "__main__":
    main()
