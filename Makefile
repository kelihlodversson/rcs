# RCS, the Resource Construction Set, on gemlib and libcmini
#
#   make m68k      build/m68k/RCS.PRG and its resource files (Atari TOS, EmuTOS, pTOS on m68k)
#   make arm       build/arm/RCS.PRG and its resource files (pTOS on ARM)
#   make libs      only gemlib and libcmini, for both targets
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

STD      = -std=gnu99
# the sources are 1985 code, warnings and all: not -Wextra or -Werror yet
WARN     = -Wall
# __GEMLIB_OLDNAMES: the old AES names (NORMAL, SELECTED, ...) the sources use
INCLUDES = -Isrc -Isrc/atari -I$(GEMLIB) -I$(LIBCMINI)/include -D__GEMLIB_OLDNAMES

.PHONY: all m68k arm libs clean
all: m68k

# RCS loads RCS.RSC (RCSLOW.RSC on a low resolution screen) from the folder it
# was started in, so they go next to the program; the .DFN files are the
# definition files of those two resource files
RES_FILES = RCS.RSC RCSLOW.RSC RCS.DFN RCSLOW.DFN

m68k: build/m68k/$(PRG_NAME) $(addprefix build/m68k/,$(RES_FILES))
arm: build/arm/$(PRG_NAME) $(addprefix build/arm/,$(RES_FILES))

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

# ---- m68k --------------------------------------------------------------

M68K_CFLAGS = $(STD) $(WARN) -O2 -m68000 -fomit-frame-pointer $(INCLUDES)
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
ARM_CFLAGS = $(STD) $(WARN) -O2 -ffreestanding $(ARM_ABI) \
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
