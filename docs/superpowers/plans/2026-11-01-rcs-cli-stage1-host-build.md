# rcs-cli stage 1: 64-bit clean load/save and a host round-trip test

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The RCS reader, writer and colour icon store build and run on a 64-bit host and give, byte for byte, the files the Atari/ARM program writes, with no change in behaviour for m68k and ARM.

**Architecture:** RCS carries memory addresses as `long` and reads fields through casts of those addresses. On the host an "address" becomes a 32-bit offset into one reserved arena, and every dereference goes through one macro, `RP(addr)`, which is the identity on m68k and ARM. The host also gets its own record layouts (24-byte OBJECT and so on), the same as m68k, and stubs for the GUI calls the load/save code makes.

**Tech Stack:** C (gnu99), GNU make, the host `cc`, the existing m68k/ARM cross builds, QEMU for the emulator checks.

**Spec:** `docs/superpowers/specs/2026-11-01-rcs-cli-design.md`

## Global Constraints

- Output must be byte for byte what RCS writes today: the golden files of Task 1 decide.
- m68k and ARM behaviour must not change; `RP()` is the identity there and records keep the layouts `gem.h` gives them.
- `int` is not assumed to be 16 bits; fixed-width fields use `short`/`long` as the surrounding code does, or the `stdint` types in new host-only files.
- Host-only code lives in `src/host/` and `tests/host/`; it is not part of `make m68k` or `make arm`.
- C is `-std=gnu99 -fno-strict-aliasing`, 4-column-tab style of the file being edited; comments in the style of the file.
- Every task ends with a commit. Commit messages end with `(#27)` and the attribution lines of the session.

## Facts found while planning

- Code is already partly accessor-based: `GET_SPEC(long tree, short x)` and friends in `rcslib.c`. The `OB_*` macros in `rcsdefs.h:177-187` hard-code field offsets, and about 380 raw dereferences like `*(short *)(addr)` sit in `rcsdata.c`, `rcslib.c`, `rcsread.c`, `rcswrite.c`, `rcsdisk.c`, `rcsedit.c` and others.
- The arena (`head`, `buff_size`) is allocated in `rcsinit.c:264-268` with `malloc`, at most `0x10000` bytes, so the whole resource lives in 64 KB; `ini_buff()` (`rcsdata.c:300`) clears its header. `rcsinit.c` is GUI code and not in the host build, so the host driver sets up the arena itself.
- The arena of a loaded file holds the image of the file: table entries are `sizeof(long)` wide and the header has 16-bit offsets (`RSH_*` in `rcsdefs.h`). That is why the host needs the 32-bit layouts, not its native ones.
- With `third_party/gemlib/include/arm` on the include path and an empty `mint/osbind.h`, `rcsread.c`, `rcsdata.c`, `rcslib.c` and `rcsdisk.c` already pass `cc -fsyntax-only` on the host. `rcswrite.c` only needs `Dfree` and `_DISKINFO` (`rcswrite.c:56-80`).
- `write_files()` writes to `rcs_rfile` with `fopen` (`rcswrite.c:996`); `read_files()` reads `rcs_rhndl`. A test hook can call both without the GUI.

## Prerequisite (Task 0)

`rcscicon.c`/`rcscicon.h` are in PR #26, which is not merged. This branch is off `main`.

- [ ] **Step 1: Wait for the merge of #26, then rebase**

```bash
cd /Users/keli/cloud/rcs-checkout
git fetch origin && git rebase origin/main
test -f src/rcscicon.c && echo ok
```
Expected: `ok`. Tasks 1 to 5 need it (Task 1's colour icon test file, Task 4's `rcscicon.c`). If #26 is still open, stop here and ask.

## File structure

- Create `src/rcsaddr.h`: `RP()`, `raddr`, the allocator names.
- Create `src/host/rcshost.h`: host record layouts, stubs for `<gem.h>` and `<mint/osbind.h>`.
- Create `src/host/hostmem.c`: arena, `rcs_alloc`/`rcs_free_block`.
- Create `src/host/hoststubs.c`: stubs for the GUI and OS calls that the shared files reference.
- Create `tests/host/roundtrip.c`: driver, loads `argv[1]` and writes `argv[2]`.
- Create `tests/roundtrip/run.sh`: runs the driver on every input and compares with `tests/golden/`.
- Create `tests/emulator/qd.py`, `tests/emulator/launch.c`: the QEMU driver and launcher used to make the golden files (moved from the scratchpad).
- Create `tests/golden/*.rsc`: inputs and the files the Atari program writes.
- Modify `Makefile`: `host-test` target.
- Modify (mechanically, Tasks 3 to 5): `src/rcsinit.c` (arena `malloc`), `src/rcsdata.c`, `src/rcslib.c`, `src/rcsread.c`, `src/rcscicon.c`, `src/rcswrite.c`, `src/rcsdisk.c`, `src/rcsdefs.h`, `src/rcsfiles.c` (test hook only).

---

### Task 1: Golden files from the Atari program

**Files:**
- Modify: `src/rcsfiles.c:179-196` (hook, `RCS_ROUNDTRIP` only)
- Create: `tests/emulator/qd.py`, `tests/emulator/launch.c`, `tests/golden/{rcs,rcslow,cicon}.rsc`, `tests/golden/{rcs,rcslow,cicon}.out.rsc`, `tests/roundtrip/gen-golden.sh`

**Interfaces:**
- Produces: `tests/golden/NAME.rsc` (input) and `tests/golden/NAME.out.rsc` (what the Atari/ARM program writes after loading it). Tasks 2 to 6 compare against these.

- [ ] **Step 1: Add the hook**

In `opn_rsc()`, after `read_files()` succeeds and before `new_state(FILE_STATE)`:

```c
#ifdef RCS_ROUNDTRIP
		/* test builds: write what was loaded, to compare with the golden file */
		strcpy(rcs_rfile, "C:\\OUT.RSC");
		write_files();
#endif
```

- [ ] **Step 2: Move the emulator tooling into the repo**

Copy `qd.py` and `rcsrun/launch.c`, `launch2.c` from the session scratchpad
(`/private/tmp/claude-501/-Users-keli-cloud-pTOS/bc6b77ae-3713-432f-915b-6b6ddbf94562/scratchpad/`) into `tests/emulator/`. Change `S=` and `R=` in `qd.py` so they are relative to the script and the repo root; it needs the pTOS virt-m68k and virt-arm images in `PTOS_M68K_ELF` and `PTOS_ARM_ELF` (environment variables, no default). Document the variables at the top of the file.

- [ ] **Step 3: Write the generator script**

`tests/roundtrip/gen-golden.sh`: for each input (`src/atari/rcs.rsc`, `src/atari/rcslow.rsc`, the colour icon file) build `RCS.PRG` for m68k with `make m68k RCS_TEST_CFLAGS=-DRCS_ROUNDTRIP`, run `QD_RSC=<input> python3 tests/emulator/qd.py m68k build/m68k/RCS.PRG out w60`, and copy `C:\OUT.RSC` from the vvfat directory (`qdir-m68k/OUT.RSC`) to `tests/golden/NAME.out.rsc`. Add `RCS_TEST_CFLAGS` to the compile lines in `Makefile` (`$(RCS_TEST_CFLAGS)` after `$(WARN)`).

- [ ] **Step 4: Generate and check the golden files**

```bash
cp src/atari/rcs.rsc tests/golden/rcs.rsc; cp src/atari/rcslow.rsc tests/golden/rcslow.rsc
cp /private/tmp/claude-501/-Users-keli-cloud-pTOS/bc6b77ae-3713-432f-915b-6b6ddbf94562/scratchpad/cicon.rsc tests/golden/cicon.rsc   # generator file from pTOS, root next patched; 1,076 bytes
sh tests/roundtrip/gen-golden.sh
ls -l tests/golden
```
Expected: three `.out.rsc` files, each non-empty. Run once more for ARM (`arm` instead of `m68k`) and `cmp` the two results; the files must be identical.

- [ ] **Step 5: Revert the hook out of normal builds and commit**

The hook is under `#ifdef`, so `make m68k` and `make arm` without `RCS_TEST_CFLAGS` are unchanged; check with `make m68k && make arm`.

```bash
git add Makefile src/rcsfiles.c tests
git commit -m "Add golden files and the emulator driver for round-trip tests (#27)"
```

---

### Task 2: Host shim and a driver that fails to link

**Files:**
- Create: `src/rcsaddr.h`, `src/host/rcshost.h`, `src/host/hostmem.c`, `src/host/hoststubs.c`, `tests/host/roundtrip.c`, `tests/roundtrip/run.sh`
- Modify: `Makefile`

**Interfaces:**
- Produces:
  - `typedef unsigned long raddr;` and `#define RP(a)`: host `((void *)(rcs_base + (raddr)(a)))`, other targets `((void *)(a))`.
  - `raddr rcs_alloc(unsigned long bytes)` (zeroed, aligned to 8; 0 on failure) and `void rcs_release(raddr)` (no-op on the host).
  - `host/rcshost.h` provides `OBJECT`, `TEDINFO`, `ICONBLK`, `BITBLK`, `GRECT`, `MFDB` with the m68k sizes (24, 28, 34, 14 bytes) as packed structs of 16-bit and 32-bit fields. Assert them with `_Static_assert(sizeof(OBJECT) == 24, "OBJECT")` and so on.

- [ ] **Step 1: Write `src/rcsaddr.h`**

```c
/* RCSADDR.H	addresses of the data of a resource file.
 * RCS keeps them as longs.  On m68k and ARM they are addresses; on the host
 * they are offsets into one arena, so that they still fit 32 bits. */
#ifndef RCSADDR_H
#define RCSADDR_H

typedef unsigned long raddr;

#ifdef RCS_HOST
extern unsigned char *rcs_base;
#define RP(a)	((void *)(rcs_base + (raddr)(a)))
#else
#define RP(a)	((void *)(a))
#endif

extern raddr rcs_alloc(unsigned long bytes);
extern void rcs_release(raddr a);

#endif
```

On m68k/ARM, `rcs_alloc` is `malloc` wrapped in `src/rcsdata.c` (Task 3, Step 1) so that no new file enters those builds.

- [ ] **Step 2: Write `src/host/hostmem.c`**

```c
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "rcsaddr.h"

#define ARENA_SIZE	(1UL << 30)

unsigned char *rcs_base;
static unsigned long used = 8;		/* offset 0 is never handed out */

static void init(void)
{
    void *p = mmap(0, ARENA_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        abort();
    rcs_base = p;
}

raddr rcs_alloc(unsigned long bytes)
{
    raddr a;

    if (!rcs_base)
        init();
    bytes = (bytes + 7) & ~7UL;
    if (bytes > ARENA_SIZE - used)
        return 0;
    a = used;
    used += bytes;
    return a;			/* the pages are zeroed by the system */
}

void rcs_release(raddr a)
{
    (void)a;
}
```

- [ ] **Step 3: Write `src/host/rcshost.h` and `src/host/hoststubs.c`**

`rcshost.h` replaces `<gem.h>` and `<mint/osbind.h>` for `-DRCS_HOST` builds: the record structs above (fields as in the AES headers, `ob_spec` as `unsigned long`, `te_ptext`, `te_ptmplt`, `te_pvalid`, `ib_pmask`, `ib_pdata`, `ib_ptext`, `bi_pdata` as `unsigned long`), `GRECT`, `MFDB`, `_DISKINFO`, the object type, flag and state constants of `gem.h` that the shared files use, `typedef int bool` through `<stdbool.h>`. Put `-Isrc/host` before `-Ithird_party/gemlib` on the host command line and make `#include <gem.h>` find a `gem.h` in `src/host/` that includes `rcshost.h`; add an empty `src/host/mint/osbind.h`.

`hoststubs.c`: the definitions of the functions the shared files call and that only exist in the GUI part. Find them from the linker (Step 5); each stub for a form alert prints its text to stderr, and returns the first button; each stub for drawing, mouse and window calls returns 0.

- [ ] **Step 4: Write the driver and test script**

`tests/host/roundtrip.c`:

```c
#include <stdio.h>
#include <string.h>
#include "rcs.h"
#include "rcsaddr.h"
#include "rcsdata.h"
#include "rcsdefs.h"
#include "rcsread.h"
#include "rcswrite.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: roundtrip in.rsc out.rsc\n");
        return 2;
    }
    buff_size = 0x10000L;
    head = rcs_alloc(buff_size);
    ini_buff();
    strcpy(rcs_rfile, argv[1]);
    rcs_rhndl = fopen(rcs_rfile, "rb");
    if (!rcs_rhndl || !read_files()) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    strcpy(rcs_rfile, argv[2]);
    if (!write_files()) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
```

`tests/roundtrip/run.sh`:

```sh
#!/bin/sh
# load each golden input with the host driver and compare with what the Atari program wrote
set -e
bin=build/host/roundtrip
status=0
for in in tests/golden/*.rsc; do
    case $in in *.out.rsc) continue;; esac
    out=build/host/$(basename "$in" .rsc).out.rsc
    "$bin" "$in" "$out"
    if cmp "$out" "${in%.rsc}.out.rsc"; then echo "ok   $in"; else echo "FAIL $in"; status=1; fi
done
exit $status
```

- [ ] **Step 5: Add `make host-test` and see it fail**

```make
HOST_CC   ?= cc
HOST_SRC   = src/rcsread.c src/rcswrite.c src/rcsdata.c src/rcslib.c src/rcsdisk.c \
             src/rcscicon.c src/host/hostmem.c src/host/hoststubs.c tests/host/roundtrip.c
HOST_FLAGS = -std=gnu99 -fno-strict-aliasing -Wall -DRCS_HOST -D__GEMLIB_OLDNAMES \
             -Isrc/host -Isrc -g

.PHONY: host-test
host-test: build/host/roundtrip
	sh tests/roundtrip/run.sh
build/host/roundtrip: $(HOST_SRC) $(wildcard src/*.h src/host/*.h)
	@mkdir -p build/host
	$(HOST_CC) $(HOST_FLAGS) -o $@ $(HOST_SRC)
```

Run `make host-test`. Expected: compile errors (the shared files still cast `long` to pointers), then, once those are fixed in Tasks 3 to 5, link errors listing the stubs. Add the stubs the linker names to `hoststubs.c` as they appear, until only compile errors from raw dereferences remain.

- [ ] **Step 6: Commit**

```bash
git add Makefile src tests
git commit -m "Add the host shim, arena and round-trip driver (#27)"
```

---

### Task 3: Make `rcsdata.c` and `rcslib.c` use `RP()`

**Files:**
- Modify: `src/rcsdata.c`, `src/rcslib.c`, `src/rcsdefs.h`

**Interfaces:**
- Consumes: `RP()`, `raddr`, `rcs_alloc` from Task 2.
- Produces: every dereference of an RCS address in these files goes through `RP()`; `get_mem()` hands out host offsets.

**The rule.** An RCS address is an `unsigned long`/`long` value used as `*(T *)(expr)`, `(T *)(expr)`, `(void *)(expr)`, `(char *)(expr)` in `memmove`, `strcpy`, `strlen` and similar calls. Each of these becomes `*(T *)RP(expr)`, `(T *)RP(expr)` and so on. Ordinary pointers (locals declared as `char *`, `FILE *`, arrays) are not changed. Where an address is also stored in an object field (`ob_spec`, `te_ptext`, ...), the field holds the `raddr` value; the host structs from Task 2 are 32-bit for that.

- [ ] **Step 1: `get_mem` and the arena start**

In `src/rcsinit.c:264-268` the two `head = (long)malloc(...)` calls become `head = rcs_alloc(...)` (the `buff_size` logic around them stays). On m68k/ARM `rcs_alloc` is defined in `src/rcsdata.c` as a thin wrapper that keeps `malloc`:

```c
#ifndef RCS_HOST
raddr rcs_alloc(unsigned long bytes)
{
    void *p = malloc(bytes);
    if (p)
        memset(p, 0, bytes);
    return (raddr)p;
}
void rcs_release(raddr a) { free((void *)a); }
#endif
```

Include `rcsaddr.h` in both files. The host driver (Task 2) does the same as `rcsinit.c`: `buff_size = 0x10000L; head = rcs_alloc(buff_size); ini_buff();`.

- [ ] **Step 2: Replace the dereferences**

Work through `rcsdata.c` first, then `rcslib.c`. After each file:

```bash
grep -n '\*(\(unsigned \)\?\(short\|long\|char\) \*)\s*(' src/rcsdata.c | grep -v 'RP('
```
Expected: no output. Also replace the field offsets of the `OB_*` macros in `rcsdefs.h` with `offsetof` of the record layouts, which on m68k/ARM equal the numbers there today:

```c
#include <stddef.h>
#define OB_NEXT(x)	(tree + (x) * sizeof(OBJECT) + offsetof(OBJECT, ob_next))
/* ... likewise for HEAD, TAIL, TYPE, FLAGS, STATE, SPEC, X, Y, WIDTH, HEIGHT */
```

- [ ] **Step 3: Check the m68k and ARM builds are unchanged**

```bash
make m68k && make arm
md5 build/m68k/RCS.PRG build/arm/RCS.PRG
git stash -q; make clean; make m68k && make arm; md5 build/m68k/RCS.PRG build/arm/RCS.PRG; git stash pop -q
```
Expected: the two sets of checksums are equal (`RP()` is the identity, `offsetof` equals the old constants). If they differ, find the difference with `m68k-atari-mintelf-objdump -d` and fix before going on.

- [ ] **Step 4: Compile for the host**

Run `make host-test`. Expected: no compile errors from `rcsdata.c` and `rcslib.c`; others remain.

- [ ] **Step 5: Commit**

```bash
git add src
git commit -m "Reach RCS memory through RP() in rcsdata.c and rcslib.c (#27)"
```

---

### Task 4: `rcsread.c` and `rcscicon.c`

**Files:**
- Modify: `src/rcsread.c`, `src/rcscicon.c`, `src/rcscicon.h` (if it casts)

**Interfaces:**
- Consumes: `RP()`, `raddr`, `rcs_alloc`.
- Produces: the same functions as before (`read_files`, `cicon_scan`, ...), using `raddr` for the addresses they pass, as `long` today. A `long` that holds an address keeps its type (it is 64 bits on the host and holds an offset below 2^32).

- [ ] **Step 1: Apply the rule of Task 3**

Same transformation as Task 3. Three places differ:
- The `malloc` of the whole file in `read_files` (`full`, `rcsread.c`) and the `malloc`s in `rcscicon.c` become `rcs_alloc`, and the cast `(long)full` becomes `full` (an `raddr`). Freed with `rcs_release`.
- Table entries are `sizeof(long)` wide in the arena (`rcsread.c:259-268`, `482`, `526`, `725-776`); on the host they must be 4 bytes. Replace `sizeof(long)` in those lines with `RS_ENTRY`, defined in `rcsaddr.h` as `4` (`sizeof(long)` on m68k and ARM, which is 4 there), and read and write the entries as `unsigned int` on the host, `long` elsewhere:

```c
#ifdef RCS_HOST
typedef unsigned int rentry;
#else
typedef long rentry;
#endif
#define RS_ENTRY	((short)sizeof(rentry))
```
- Fields stored as `char *` that hold a number (`rcs_index[idx].val = (char *) (long) val;`, `rcsread.c:~85`) are ordinary pointers on the host: leave them.

- [ ] **Step 2: Check no behaviour change for m68k and ARM**

Same as Task 3 Step 3. Expected: identical checksums.

- [ ] **Step 3: Compile for the host**

Run `make host-test`. Expected: no compile errors from the two files.

- [ ] **Step 4: Commit**

```bash
git add src
git commit -m "Reach RCS memory through RP() in rcsread.c and rcscicon.c (#27)"
```

---

### Task 5: `rcswrite.c` and `rcsdisk.c`

**Files:**
- Modify: `src/rcswrite.c`, `src/rcsdisk.c`

- [ ] **Step 1: Apply the rule**

Same transformation. In `rcswrite.c`, `get_dspace` and `dspace` (`rcswrite.c:56-80`) call `Dfree`: on the host they return a large value (add `#ifdef RCS_HOST` returning `0x7fffffffL`, `false`), so that the free-space check does not block the write.

- [ ] **Step 2: Check the cross builds and compile for the host**

As in Tasks 3 and 4: identical checksums, and `make host-test` links.

- [ ] **Step 3: Run the round-trip test**

```bash
make host-test
```
Expected: `ok` for each of `rcs.rsc`, `rcslow.rsc`, `cicon.rsc`. Any `FAIL` shows a difference: look at it with `cmp -l build/host/NAME.out.rsc tests/golden/NAME.out.rsc | head`, find the field it belongs to, and fix the layout or the conversion that is wrong. Do not change the golden files.

- [ ] **Step 4: Commit**

```bash
git add src
git commit -m "Reach RCS memory through RP() in rcswrite.c and rcsdisk.c; host round trip passes (#27)"
```

---

### Task 6: The rest of the GUI program, emulator checks and documentation

**Files:**
- Modify: `src/rcsedit.c`, `src/rcsalert.c`, `src/rcstrees.c`, `src/rcsload.c`, `src/rcsiedit.c`, `src/rcsiinit.c`, `src/rcsintf.c`, `src/rcsmenu.c`, `src/rcsobjs.c`, `src/rcsmain.c`, `src/rcsimain.c`, `src/rcsifile.c` (the files of the GUI program that dereference RCS addresses)
- Modify: `README.md`, `docs/`

The GUI files are not built for the host, but they share `rcsdata.h`/`rcslib.h` and must keep working with the accessors. Whether they need `RP()` depends on what they do with addresses: on m68k and ARM nothing changes either way, so this task is only needed to keep one rule in the source.

- [ ] **Step 1: Apply the rule where it is cheap**

Apply `RP()` to the dereferences in the files above with the same rule and check for each file that the cross builds are unchanged (checksums as in Task 3). Skip any file in which the change is not mechanical (more than the rule, a file that is only drawing code), and list the skipped files in the PR description.

- [ ] **Step 2: Emulator checks on m68k and ARM**

```bash
make m68k && make arm
python3 tests/emulator/qd.py m68k build/m68k/RCS.PRG chk-m68k w60 sload
python3 tests/emulator/qd.py arm  build/arm/RCS.PRG  chk-arm  w60 sload
```
Expected: the RCS window opens with the resource tree (check the screenshot) on both. Repeat with `QD_RSC=tests/golden/cicon.rsc`.

- [ ] **Step 3: Document**

In `README.md`, add a section "Building on the host": `make host-test` runs the round-trip test on the host; `rcs-cli` follows in stage 2. In `docs/`, add one paragraph on `RP()` and `raddr` for people who edit the sources.

- [ ] **Step 4: Run the whole check once more and commit**

```bash
make m68k && make arm && make host-test
git add -A
git commit -m "Finish the RP() conversion, add the host build to the README (#27)"
git push
gh pr ready 28
```

---

## Self-review against the spec

- Accessor layer and two implementations: Tasks 2 to 6 (`RP()`, `rcshost.h`, `hostmem.c`).
- Host-only `rcshost.h` and no `<gem.h>` on the host: Task 2.
- Safety net round trip before the refactor: Task 1 (golden files from the Atari program, before any source change).
- GUI-only files stay out of the host build: Task 2's `HOST_SRC`.
- `make host`, `rcs-cli` itself and commands: later stages. This stage ends with `make host-test`.
- `#N` aliases, path model: stage 2.
- Not covered by this stage: wildcards, icon image import and export.
