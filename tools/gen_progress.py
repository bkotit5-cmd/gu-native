#!/usr/bin/env python3
"""gen.py as build_game.py runs it (run_translator), saying what it is doing as it goes and with less memory: the same
C, byte for byte (gen.py itself, and every file the game code's id is made from, stay as they are).

    python gen_progress.py NAME NSO BASE OUTDIR [gen.py's own options]

gen.py says how far it is only while it writes the C ("NAME: progress N/M files K"), the end of its work. Before that it
reads the module (its LZ4) and analyses it, which is most of its time and says nothing: on a PC 9 of the sdk's 11 s
and 41 of main's 50 s, on a phone minutes. The app's builder showed "Translate the game's code (10 %)" all that time,
with nothing moving (reported as stuck at 10 %). So this adds, on gen.py's stderr like its own lines:
    NAME: reading                   it starts reading the module
    NAME: analysed N/M              about every 2 s while it analyses: N pieces of code analysed, of the M function starts
                                    the module's tables give (the first round, most of the time; the rounds after it
                                    analyse what the first one found, N past M)
Memory (a phone of 4 GB has about 1.7 GB free; main's translation needed 2.1 GB, and Android swapped it out page by
page): every lifted instruction keeps its C as a string, and most of them are the same few thousand texts, so each is
kept once (sys.intern: the same strings, the same C); and Python's collector of reference cycles is off, as gen.py makes
none worth collecting (the same peak with it off, measured) and a collection walks every object the analysis keeps,
the swapped-out ones too. Measured on a PC with main: 2.1 GB -> 1.8 GB, 50 s -> 43 s, the same C (md5 of every file).
And once gen.py has written its last file, the process ends at once (os._exit) instead of freeing every object first.
"""
import gc
import os
import re
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import gen  # noqa: E402


def say(text):
    sys.stderr.write(text + '\n')
    sys.stderr.flush()


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else '?'
    module = gen.Module
    init, boundaries, scan, lift = module.__init__, module.boundaries, module.scan, module.lift
    count = {'starts': 0, 'scanned': 0, 'said': 0.0}

    def init_said(self, *a, **k):
        say(f'{name}: reading')
        init(self, *a, **k)

    def boundaries_counted(self, *a, **k):
        starts = boundaries(self, *a, **k)
        count['starts'] = len(starts)
        say(f'{name}: analysed 0/{len(starts)}')
        return starts

    def scan_counted(self, *a, **k):
        count['scanned'] += 1
        now = time.monotonic()
        if now - count['said'] >= 2:
            count['said'] = now
            say(f'{name}: analysed {count["scanned"]}/{count["starts"]}')
        return scan(self, *a, **k)

    def lift_interned(self, a):
        ins = self.cache.get(a)
        if ins is not None:
            return ins
        ins = lift(self, a)  # (now in the cache: the same object, its strings swapped for their interned copies)
        if type(ins.code) is str:
            ins.code = sys.intern(ins.code)
        if type(ins.cond) is str:
            ins.cond = sys.intern(ins.cond)
        return ins

    def print_then_end(*a, **k):
        print(*a, **k)
        if a and re.fullmatch(rf'{re.escape(name)}: wrote \d+ chunks, \d+s total', str(a[0])):
            # (gen.main()'s last line: every file is written and closed) the process ends here, as the kernel takes back
            # its memory in one go; returning would free the analysis's millions of objects one by one, reading back
            # all of them a phone had swapped out (a Moto g06: 20 s and 800,000 page faults, at the end of main)
            sys.stdout.flush()
            sys.stderr.flush()
            os._exit(0)

    module.__init__, module.boundaries, module.scan, module.lift = init_said, boundaries_counted, scan_counted, lift_interned
    gen.print = print_then_end  # (gen.py's own print: its module's name before the built-in one)
    gc.disable()
    sys.argv = [os.path.join(TOOLS, 'gen.py')] + sys.argv[1:]
    gen.main()


if __name__ == '__main__':
    main()
