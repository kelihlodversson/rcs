# RCS, the Resource Construction Set, on gemlib and libcmini
#
#   make m68k      build/m68k/RCS.PRG and its resource files (Atari TOS, EmuTOS, pTOS on m68k)
#   make arm       build/arm/RCS.PRG and its resource files (pTOS on ARM)
#   make libs      only gemlib and libcmini, for both targets
#   make rsc-test  the tests of the resource library (src/rsc) on this machine
#   make clean
#
# Needs GNU make, python3 and rsync, and the m68k-atari-mintelf- toolchain on
# PATH for m68k, arm-none-eabi- for ARM, plus `pip3 install kconfiglib` for
# libcmini's configuration (docs/toolchains.md).  On macOS use gmake.

GEMLIB   = third_party/gemlib
LIBCMINI = third_party/libcmini

RCS_SRC  = $(wildcard src/rcs*.c)
PRG_SRC  = src/atari/prostart.c $(RCS_SRC)
PRG_NAME = RCS.PRG

# the sources read and write their records through casts of addresses
STD      = -std=gnu99 -fno-strict-aliasing
# the sources are 1985 code, warnings and all: not -Wextra or -Werror yet
WARN     = -Wall
# __GEMLIB_OLDNAMES: the old AES names (NORMAL, SELECTED, ...) the sources use
INCLUDES = -Isrc -Isrc/atari -I$(GEMLIB) -I$(LIBCMINI)/include -D__GEMLIB_OLDNAMES

.PHONY: all m68k arm libs clean
all: m68k

# RCS loads RCS.RSC (RCSLOW.RSC on a low resolution screen) from the folder it
# was started in, so they go next to the program.  The .DFN files are the
# definition files of those two resource files: not needed to run the
# program, but RCS needs them when one of the resource files is edited.
RES_FILES = RCS.RSC RCSLOW.RSC RCS.DFN RCSLOW.DFN

# The resource library (src/rsc, #29) is compiled for both targets so that
# it keeps building there, but not linked into RCS.PRG yet.
RSC_LIB_SRC = src/rsc/rscmodel.c src/rsc/rscatari_read.c
RSC_LIB_H   = $(wildcard src/rsc/*.h)

m68k: build/m68k/$(PRG_NAME) $(addprefix build/m68k/,$(RES_FILES)) \
    $(patsubst %.c,build/m68k/%.o,$(RSC_LIB_SRC))
arm: build/arm/$(PRG_NAME) $(addprefix build/arm/,$(RES_FILES)) \
    $(patsubst %.c,build/arm/%.o,$(RSC_LIB_SRC))

$(patsubst %.c,build/m68k/%.o,$(RSC_LIB_SRC)) $(patsubst %.c,build/arm/%.o,$(RSC_LIB_SRC)): $(RSC_LIB_H)

build/%/RCS.RSC: src/atari/rcs.rsc
	@mkdir -p $(dir $@)
	cp $< $@
build/%/RCSLOW.RSC: src/atari/rcslow.rsc
	@mkdir -p $(dir $@)
	cp $< $@
build/%/RCS.DFN: src/atari/rcs.dfn
	@mkdir -p $(dir $@)
	cp $< $@
build/%/RCSLOW.DFN: src/atari/rcslow.dfn
	@mkdir -p $(dir $@)
	cp $< $@

# ---- the libraries -----------------------------------------------------

M68K_PREFIX ?= m68k-atari-mintelf-
M68K_CC      = $(M68K_PREFIX)gcc

GEMLIB_H     = $(GEMLIB)/mt_gem.h
GEMLIB_A     = $(GEMLIB)/.lib68000/libgem.a
LIBCMINI_A   = build/m68k/lib/libcmini.a
LIBCMINI_CRT = build/m68k/lib/crt0.o

# gemlib's headers are generated from mt_gem.h.in
$(GEMLIB_H):
	$(MAKE) -C $(GEMLIB) mt_gem.h

# gemlib does its own dependency checking, so ask it on every build; it leaves
# the archive alone (and so does not relink) when nothing changed.
$(GEMLIB_A): $(GEMLIB_H) FORCE
	$(MAKE) -C $(GEMLIB) CROSS_TOOL=$(patsubst %-,%,$(M68K_PREFIX)) .lib68000/libgem.a

# libcmini keeps one build/ directory and one .config for every target, so each
# target builds in its own copy of it (a parallel `make m68k arm` cannot make
# the two clash) and keeps the two files it needs from there.
LIBCMINI_COPY = rsync -a --exclude=.git --exclude=/build --exclude=/obj \
    --exclude=/.config --exclude=/.config.old $(LIBCMINI)/

# libcmini is copied, so a stamp fingerprinting the submodule's files (contents,
# not just names, so editing a modified file again counts) tells make when the
# copies are out of date.  The stamp keeps its timestamp while nothing changes.
LIBCMINI_REV = build/libcmini.rev

$(LIBCMINI_REV): FORCE
	@mkdir -p $(dir $@)
	@(cd $(LIBCMINI) && git rev-parse HEAD && \
	    git ls-files -co --exclude-standard | LC_ALL=C sort | \
	    while read -r f; do cksum "$$f" 2>/dev/null; done) | cksum > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@

FORCE:
.PHONY: FORCE

$(LIBCMINI_A): $(LIBCMINI_REV)
	rm -rf build/m68k/libcmini
	mkdir -p build/m68k/libcmini $(dir $@)
	$(LIBCMINI_COPY) build/m68k/libcmini/
	$(MAKE) -C build/m68k/libcmini mintelf_defconfig
	$(MAKE) -C build/m68k/libcmini ONLY_68K=y dirs libs startups
	cp build/m68k/libcmini/build/libcmini.a build/m68k/libcmini/build/objs/crt0.o $(dir $@)

$(LIBCMINI_CRT): $(LIBCMINI_A)

# Extra compiler flags for the round trip test hook only (tests/roundtrip/
# gen-golden.sh passes -DRCS_ROUNDTRIP on the command line).  Empty here, so an
# environment variable of this name does not reach the build.
RCS_TEST_CFLAGS =

# ---- m68k --------------------------------------------------------------

M68K_CFLAGS = $(STD) $(WARN) $(RCS_TEST_CFLAGS) -O2 -m68000 -fomit-frame-pointer $(INCLUDES)
M68K_OBJ    = $(patsubst %.c,build/m68k/%.o,$(PRG_SRC))

build/m68k/%.o: %.c $(GEMLIB_H)
	@mkdir -p $(dir $@)
	$(M68K_CC) $(M68K_CFLAGS) -c $< -o $@

build/m68k/$(PRG_NAME): $(M68K_OBJ) $(GEMLIB_A) $(LIBCMINI_A) $(LIBCMINI_CRT)
	$(M68K_CC) -m68000 -nostdlib $(LIBCMINI_CRT) $(M68K_OBJ) \
	    -L$(GEMLIB)/.lib68000 -Lbuild/m68k/lib \
	    -lcmini -lgem -lgcc -lcmini -o $@

# ---- ARM ---------------------------------------------------------------

ARM_PREFIX   ?= arm-none-eabi-
ARM_CC        = $(ARM_PREFIX)gcc
# the same CPU flags pTOS builds its own ARM programs with
ARM_ABI       = -march=armv7-a -mtune=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard \
    -mword-relocations
ARM_LIBGEM    = build/arm/lib/libgem.a
ARM_LIBCMINI  = build/arm/lib/libcmini.a
ARM_CRT       = build/arm/lib/minicrt0.o

# gemlib's own include/arm directory has the compiler.h its headers need
ARM_CFLAGS = $(STD) $(WARN) $(RCS_TEST_CFLAGS) -O2 -ffreestanding $(ARM_ABI) \
    -Isrc -I$(GEMLIB)/include/arm -I$(GEMLIB) -I$(LIBCMINI)/include -D__GEMLIB_OLDNAMES
ARM_OBJ    = $(patsubst %.c,build/arm/%.o,$(PRG_SRC))

$(ARM_LIBGEM): $(GEMLIB_H) FORCE
	$(MAKE) -C $(GEMLIB) ARCH=arm .libarm/libgem.a
	@mkdir -p $(dir $@)
	@cmp -s $(GEMLIB)/.libarm/libgem.a $@ || cp $(GEMLIB)/.libarm/libgem.a $@

$(ARM_LIBCMINI): $(LIBCMINI_REV)
	rm -rf build/arm/libcmini
	mkdir -p build/arm/libcmini $(dir $@)
	$(LIBCMINI_COPY) build/arm/libcmini/
	$(MAKE) -C build/arm/libcmini ptos_arm_defconfig
	$(MAKE) -C build/arm/libcmini dirs libs startups
	cp build/arm/libcmini/build/libcmini.a build/arm/libcmini/build/objs/minicrt0.o $(dir $@)

$(ARM_CRT): $(ARM_LIBCMINI)

build/arm/%.o: %.c $(GEMLIB_H)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

# -q keeps the relocations in the file: pTOS loads a program wherever there is
# room and applies them (doc/elfload.txt in pTOS), so it must not be stripped
build/arm/$(PRG_NAME): $(ARM_OBJ) $(ARM_LIBGEM) $(ARM_LIBCMINI) $(ARM_CRT)
	$(ARM_CC) $(ARM_ABI) -nostdlib -Wl,-q -Wl,-Ttext=0 -Wl,-e_start $(ARM_CRT) $(ARM_OBJ) \
	    -Lbuild/arm/lib -lcmini -lgem -lgcc -lcmini -o $@

libs: $(GEMLIB_A) $(LIBCMINI_A) $(ARM_LIBGEM) $(ARM_LIBCMINI)

clean:
	rm -rf build

# ---- host -----------------------------------------------------------------
#
#   make host-test   the load/save round trip on this machine: the sources
#                    compiled as 64 bit host code, without the Atari GUI
#
# The host build does not use gemlib or libcmini: src/host has its own gem.h
# and the GUI and OS calls the sources make are stubbed in hoststubs.c.

HOST_CC    ?= cc
HOST_SRC    = src/rcsread.c src/rcswrite.c src/rcsdata.c src/rcslib.c src/rcsdisk.c \
              src/rcscicon.c src/host/hostmem.c src/host/hoststubs.c tests/host/roundtrip.c
HOST_FLAGS  = -std=gnu99 -fno-strict-aliasing -Wall -DRCS_HOST -D__GEMLIB_OLDNAMES \
              -Isrc/host -Isrc -g

.PHONY: host-test
host-test: build/host/roundtrip
	sh tests/roundtrip/run.sh

build/host/roundtrip: $(HOST_SRC) $(wildcard src/*.h src/host/*.h src/host/mint/*.h)
	@mkdir -p build/host
	$(HOST_CC) $(HOST_FLAGS) -o $@ $(HOST_SRC)

# ---- the resource library on the host ---------------------------------------
#
#   make rsc-test            build the tests of tests/rsc with the library and run them
#   make rsc-test RSC_SAN=1  the same with AddressSanitizer and UBSan
#
# The library defines the AES structures itself on the host (RSC_HOST); it
# does not use src/host.  Each test program is one file of tests/rsc; the
# reader tests read the files of tests/golden.

RSC_SAN        =
RSC_TESTS      = test_rsc test_read
RSC_TEST_FLAGS = -std=gnu99 -fno-strict-aliasing -Wall -Wextra -DRSC_HOST -DRSC_TEST \
                 -Isrc/rsc -g
ifeq ($(RSC_SAN),1)
RSC_TEST_FLAGS += -fsanitize=address,undefined -fno-sanitize-recover=undefined
RSC_TEST_SUFFIX = -san
else
RSC_TEST_SUFFIX =
endif
RSC_TEST_BINS  = $(patsubst %,build/host/rsc/%$(RSC_TEST_SUFFIX),$(RSC_TESTS))

.PHONY: rsc-test
rsc-test: $(RSC_TEST_BINS)
	@set -e; for t in $(RSC_TEST_BINS); do echo "./$$t"; ./$$t; done

build/host/rsc/%$(RSC_TEST_SUFFIX): tests/rsc/%.c $(RSC_LIB_SRC) $(RSC_LIB_H)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(RSC_TEST_FLAGS) -o $@ $(RSC_LIB_SRC) $<
