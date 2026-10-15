"""Local ROM inspection; output is ROM-derived and must remain untracked.

Linear decode only: supply the entry M/X flags and validate branches/call
boundaries separately. Calls and PLP may change flags that this tool cannot infer.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("start", type=lambda x: int(x, 16))
p.add_argument("end", type=lambda x: int(x, 16))
p.add_argument("--m", type=int, choices=[0, 1], default=1)
p.add_argument("--x", type=int, choices=[0, 1], default=1)
p.add_argument("--rom", default=str(ROOT / "fzero.sfc"))
p.add_argument("--engine-root", type=Path, default=ROOT / "snesrecomp",
               help="Pinned engine checkout (may be outside the game worktree)")
a = p.parse_args()
sys.path.insert(0, str(a.engine_root / "recompiler"))
from snes65816 import load_rom, lorom_offset, decode_insn
rom = load_rom(a.rom)
pc, m, x = a.start, a.m, a.x
while pc < a.end:
    ins = decode_insn(rom, lorom_offset(pc >> 16, pc & 65535), pc & 65535, pc >> 16, m, x)
    if ins is None:
        raise SystemExit(f"Unknown instruction at {pc:06x}")
    print(f"{pc:06x} {ins.mnem:3} {str(ins.mode):10} {ins.operand:06x} M{m}X{x}")
    if ins.mnem in ("REP", "SEP"):
        bit = int(ins.mnem == "SEP")
        if ins.operand & 0x20: m = bit
        if ins.operand & 0x10: x = bit
    pc += ins.length
