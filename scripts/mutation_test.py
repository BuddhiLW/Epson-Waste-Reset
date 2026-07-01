#!/usr/bin/env python3
"""Mutation testing for the EWR pure protocol core.

Applies small semantic mutations to the safety-critical byte builders / parser
(include/ewr/proto.h, src/protocol.cpp) and verifies that the golden + property
suite KILLS each one (fails to compile or fails an assertion). A SURVIVING
mutant means the tests have a blind spot in exactly the code that writes printer
EEPROM.

Usage:  python3 scripts/mutation_test.py
Requires: build/ already configured (cmake -B build). Restores every file and a
clean build on exit, even on error.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
TARGETS = ["golden_test", "property_test"]

# (relative path, exact source substring, replacement, human label)
MUTATIONS = [
    ("include/ewr/proto.h", "WRITE_CMD = 0x42", "WRITE_CMD = 0x43", "write cmd 0x42 -> 0x43"),
    ("include/ewr/proto.h", "D4_CREDIT_NONE = 0x00", "D4_CREDIT_NONE = 0x01", "d4 credit 0x00 -> 0x01 (overflow guard)"),
    ("include/ewr/proto.h", "FRAME = 0x7C", "FRAME = 0x7D", "frame marker 0x7C -> 0x7D"),
    ("include/ewr/proto.h", "0x00, 0x00, 0x00, 0x1B, 0x01", "0x00, 0x00, 0x00, 0x1C, 0x01", "EJL init 0x1B -> 0x1C"),
    ("src/protocol.cpp", "((c >> 1) & 0x7F)", "((c >> 2) & 0x7F)", "rotr(cmd) shift 1 -> 2"),
    ("src/protocol.cpp", "pushLe16(inner, static_cast<uint16_t>(cell.addr))", "pushBe16(inner, static_cast<uint16_t>(cell.addr))", "address endianness LE -> BE"),
    ("src/protocol.cpp", "pushBe16(d4, static_cast<uint16_t>(epson.size() + proto::D4_HEADER_LEN))", "pushLe16(d4, static_cast<uint16_t>(epson.size() + proto::D4_HEADER_LEN))", "d4 length endianness BE -> LE"),
    ("src/protocol.cpp", "(i < model.reset_values.size()) ? model.reset_values[i] : 0x00", "0x00", "reset value forced to 0x00"),
    ("src/protocol.cpp", "pkt.size() >= 27 && pkt[0] == 0x1B", "pkt.size() > 27 && pkt[0] == 0x1B", "usbpcap strip >=27 -> >27"),
    ("src/protocol.cpp", "pkt.begin() + 27", "pkt.begin() + 26", "usbpcap strip 27 -> 26 bytes"),
]


def build():
    return subprocess.run(["cmake", "--build", BUILD, "--target", *TARGETS],
                          capture_output=True, text=True)


def suite_kills():
    """(killed, how): killed if the mutant fails to build or fails any test."""
    b = build()
    if b.returncode != 0:
        return True, "did-not-compile"
    for t in TARGETS:
        r = subprocess.run([os.path.join(BUILD, t)], capture_output=True, text=True)
        if r.returncode != 0:
            return True, f"{t} failed"
    return False, "SURVIVED"


def main():
    print("[*] Baseline: build + suite must be green before mutating...")
    if build().returncode != 0:
        print("[-] Baseline build failed. Configure/build first: cmake -B build && cmake --build build")
        return 2
    for t in TARGETS:
        if subprocess.run([os.path.join(BUILD, t)], capture_output=True, text=True).returncode != 0:
            print(f"[-] Baseline test '{t}' fails; fix before mutation testing.")
            return 2
    print(f"[+] Baseline green. Running {len(MUTATIONS)} mutants...\n")

    results = []
    try:
        for rel, old, new, label in MUTATIONS:
            path = os.path.join(ROOT, rel)
            src = open(path, encoding="utf-8").read()
            if old not in src:
                results.append((label, None, "pattern-not-found"))
                print(f"  ???  {label:44s} (pattern not found in {rel})")
                continue
            try:
                open(path, "w", encoding="utf-8").write(src.replace(old, new, 1))
                killed, how = suite_kills()
            finally:
                open(path, "w", encoding="utf-8").write(src)
            results.append((label, killed, how))
            print(f"  {'KILLED  ' if killed else 'SURVIVED'} {label:44s} ({how})")
    finally:
        print("\n[*] Restoring clean build...")
        build()

    killed = [r for r in results if r[1] is True]
    survived = [r for r in results if r[1] is False]
    notfound = [r for r in results if r[1] is None]

    print(f"\n==== Mutation score: {len(killed)}/{len(MUTATIONS)} killed ====")
    if notfound:
        print("[!] patterns not found (source drifted): " + ", ".join(l for l, _, _ in notfound))
    if survived:
        print("[-] SURVIVING MUTANTS (test gap in EEPROM-writing code):")
        for l, _, _ in survived:
            print("      - " + l)
        return 1
    if notfound:
        return 3
    print("[+] All mutants killed — the test net catches every seeded defect.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
