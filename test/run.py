#!/usr/bin/env python3
import argparse
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent


def check(exe, path, bless):
    r = subprocess.run([exe, str(path)], capture_output=True, text=True, timeout=180)
    exp = path.with_suffix(".out")
    if bless:
        exp.write_text(r.stdout)
        return path.name, True, ""
    want = exp.read_text() if exp.exists() else ""
    if r.returncode != 0:
        last = r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "ненулевой код выхода"
        return path.name, False, last
    if r.stdout != want:
        return path.name, False, "вывод не совпал с " + exp.name
    return path.name, True, ""


def check_error(exe, path):
    r = subprocess.run([exe, str(path)], capture_output=True, text=True, timeout=180)
    exp = path.with_suffix(".err")
    want = exp.read_text() if exp.exists() else ""
    if r.returncode == 0:
        return path.name, False, "ожидалась ошибка компиляции, программа прошла"
    if want not in r.stderr:
        return path.name, False, "в stderr нет: " + want.strip()
    return path.name, True, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="faloya")
    ap.add_argument("--bless", action="store_true")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    args = ap.parse_args()
    exe = str(Path(args.exe).resolve()) if "/" in args.exe else args.exe
    cases = sorted((HERE / "cases").glob("*.fal"))
    errors = sorted((HERE / "errors").glob("*.fal"))
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda p: check(exe, p, args.bless), cases))
        results += list(pool.map(lambda p: check_error(exe, p), errors))
    for name, ok, msg in results:
        print(("ok   " if ok else "FAIL ") + name + ((" — " + msg) if msg else ""))
    bad = sum(1 for _, ok, _ in results if not ok)
    print(f"{len(results) - bad}/{len(results)} прошли")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
