#!/usr/bin/env python3
"""Makes the game code for PC INSIDE MHGU.exe (the game folder screen's "Build the game code on this PC", and F1, System,
Game code): the same tools/build_game.py a player runs by hand, for the PC's library only, with Zig instead of llvm-mingw
(--zig) and a work folder that is kept (--work), so that a run cut short goes on from where it stopped. The PC's
counterpart of tools/phone_build.py, whose "@@" lines for the program, stages and resume stamp it shares.

    python pc_build.py --game FOLDER --work DIR --zig ZIG [--jobs N] [--expect-id ID] [--clean]

MHGU.exe (runtime/pc_builder.c) runs it with the Python it downloaded (python.org's embeddable package for Windows) and
Zig (PyPI's ziglang 0.13.0 for Windows), below normal priority, with all the processor's cores but one.

Its output is build_game.py's, with phone_build.py's @@ lines (@@pid, @@id, @@stage, @@progress, @@error, @@done).
Resuming is phone_build.py's too: once the translation is through, the work folder's inputs.txt keeps a hash of what it
was made from (the game's exefs/main and exefs/sdk, these tools, the Python) and of the C it wrote; a run whose inputs
give the same goes straight to compiling the files whose objects are missing. The library is the same, byte for byte, as
a run from an empty work folder makes (build_game.link_pc_zig: its time stamp is 0).

Everything this writes is made from the player's own game and stays on the PC.
"""
import argparse
import errno
import os
import shutil
import sys
import time
import traceback

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import build_game  # noqa: E402
import phone_build as pb  # noqa: E402 (the @@ lines, the stages and the resume stamp, shared with the phone's)

LIBRARY = 'mhgugame.dll'
WORK_SPACE = 3 << 30  # (the C it writes, 300 MB; its objects, and their copies in Zig's cache; the library)


def can_write(folder):
    """A file can be made in the folder (os.access only reads the read-only flag on Windows)."""
    probe = os.path.join(folder, f'.mhgu_write_test_{os.getpid()}')
    try:
        with open(probe, 'wb') as f:
            f.write(b'0')
        os.remove(probe)
        return True
    except OSError:
        return False


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--game', required=True, help='the game folder (exefs and romfs of 1.4.0)')
    ap.add_argument('--work', required=True, help='the work folder (kept between runs to go on where one stopped)')
    ap.add_argument('--zig', required=True, help='Zig (zig.exe)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument('--expect-id', default='', help='the id MHGU.exe wants: anything else stops before the long part')
    ap.add_argument('--clean', action='store_true', help='once the library is in place, delete the work folder')
    args = ap.parse_args(argv)

    t0 = time.time()
    pb.emit(f'@@pid {os.getpid()}')
    lib_id = build_game.gamelib_id(build_game.PC_ID_FLAGS)
    pb.emit(f'@@id {lib_id}')
    if args.expect_id and args.expect_id != lib_id:
        pb.emit(f'@@error tools these tools make game code with id {lib_id}, the program wants {args.expect_id}')
        return 1
    tap = pb.Stages()
    try:
        os.makedirs(args.work, exist_ok=True)
        have = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(args.work) for f in fs)
        free = shutil.disk_usage(args.work).free
        if free + have < WORK_SPACE:
            pb.emit(f'@@error space {(WORK_SPACE - have - free) >> 20} MB more free space needed')
            return 1
        exefs = pb.game_exefs(os.path.abspath(args.game))
        if not can_write(os.path.dirname(exefs) if exefs else args.game):
            pb.emit(f'@@error dest can\'t write in {args.game}')
            return 1
        pb.emit('@@stage extract')

        # the translation: kept from the last run if it was made from the same exefs and tools
        lines = [f'tools {lib_id} {pb.tools_digest()}', 'python ' + ' '.join(sys.version.split()), f'game {args.game}',
                 f'main {pb.any_digest(exefs and os.path.join(exefs, "main"))}',
                 f'sdk {pb.any_digest(exefs and os.path.join(exefs, "sdk"))}']
        inputs = pb.stamp_hash(lines)
        kept_inputs, kept_made = pb.read_stamp(args.work)
        reuse = kept_inputs == inputs and kept_made != 'none' and kept_made == pb.written(args.work)
        if reuse:
            print(f'inputs as the last run\'s ({time.time() - t0:.1f} s to check): its translation is kept, on to the compile')
        else:
            if kept_inputs:
                print('the last run\'s translation isn\'t for these inputs (or its C is gone): translating again')
            pb.forget_translation(args.work)

        def on_stage(stage):
            if stage != 'compile':
                return
            if not reuse:  # (the translation is through: what it was made from, and the C it wrote)
                pb.write_stamp(args.work, inputs, pb.written(args.work), lines)
            print(f'compiling from {time.time() - t0:.1f} s after the start')
        tap.on_stage = on_stage
        progress = pb.CompileProgress(tap, os.path.join(args.work, 'obj_pc'))
        progress.start()
        sys.stdout = tap
        cmd = [args.game, '--pc', '--force', '--zig', args.zig, '--work', args.work, '--jobs', str(args.jobs), '--no-pip']
        try:
            try:
                build_game.main(cmd)
            except SystemExit as e:
                # (Zig's cache naming an object it no longer has, as phone_build.py: emptied, once more)
                zig_cache = os.path.join(args.work, 'zig-cache')
                if e.code in (0, None) or tap.stage not in ('compile', 'link') or 'zig-cache' not in tap.error or \
                        not os.path.isdir(zig_cache):
                    raise
                print("Zig's cache names a file it no longer has: emptied, the build goes on")
                shutil.rmtree(zig_cache, ignore_errors=True)
                tap.error, tap.in_error = '', False
                build_game.main(cmd)
        finally:
            progress.stop.set()
            sys.stdout = sys.__stdout__
        game = build_game.check_game(os.path.abspath(args.game))
        placed = os.path.join(game, LIBRARY)
        tap.set('install')
        if args.clean:
            shutil.rmtree(args.work, ignore_errors=True)
        pb.emit(f'@@done {placed}')
        return 0
    except SystemExit as e:
        if e.code in (0, None):
            return 0
        message = tap.error or (str(e.code) if not isinstance(e.code, int) else f'stopped with code {e.code}')
        pb.emit(f'@@error {tap.stage} {" ".join(message.split())}')
        return 1
    except OSError as e:
        traceback.print_exc()
        stage = 'space' if e.errno == errno.ENOSPC or getattr(e, 'winerror', 0) in (112, 39) else tap.stage
        pb.emit(f'@@error {stage} {e}')
        return 1
    except MemoryError:
        pb.emit(f'@@error memory {tap.stage}')
        return 1
    except Exception as e:  # noqa: BLE001 (any other failure: the program shows it, the log has the traceback)
        traceback.print_exc()
        pb.emit(f'@@error {tap.stage} {type(e).__name__}: {" ".join(str(e).split())}')
        return 1


if __name__ == '__main__':
    t0 = time.time()
    code = main()
    print(f'({time.time() - t0:.0f} s)')
    sys.exit(code)
