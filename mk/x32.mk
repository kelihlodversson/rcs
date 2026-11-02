# Native pTOS ILP32 programs using the shared CRT and AES library.
# PTOS points at a pTOS build with CONF_WITH_LIBAES enabled.
PTOS ?= ../pTOS
X32_CC ?= gcc
X32_OBJCOPY ?= objcopy
X32_CFLAGS = $(STD) $(WARN) $(RCS_TEST_CFLAGS) -Os -mx32 -ffreestanding \
    -fPIC -fvisibility=hidden -fno-stack-protector -fno-builtin \
    -Isrc/x32 -Isrc -I$(GEMLIB) -I$(LIBCMINI)/include -D__GEMLIB_OLDNAMES -MMD -MP
X32_OBJ = $(patsubst %.c,build/x32/%.o,$(PRG_SRC))
X32_LIBCMINI = build/x32/lib/libcmini.a
X32_VDI_NAMES = rc_copy rc_grect_to_array v_bar v_clsvwk v_ellarc v_ellipse \
    v_ellpie v_fillarea v_get_pixel v_opnvwk v_pline vq_extnd vqt_attributes \
    vr_recfl vr_trnfm vro_cpyfm vrt_cpyfm vs_clip vsf_color vsf_interior \
    vsf_style vsf_perimeter vsl_color vsl_ends vsl_type vsl_udsty vsl_width \
    vst_color vst_height vst_effects vsm_height vswr_mode vdi_dummy
X32_VDI_OBJ = $(addprefix build/x32/gemlib/,$(addsuffix .o,$(X32_VDI_NAMES)))
X32_OBJ += build/x32/src/x32/vdi.o
build/x32/src/x32/vdi.o: X32_CFLAGS += -I$(PTOS)/include -I$(PTOS)/obj

# Use GEMlib's portable C parameter marshalling, calling the native VDI
# entry through the layout adapter below instead of its m68k trap binding.
build/x32/gemlib/%.o: $(GEMLIB)/%.c $(GEMLIB_H)
	@mkdir -p $(dir $@)
	$(X32_CC) $(X32_CFLAGS) -D__NO_INLINE__ -c $< -o $@

build/x32/lib/libptos-vdi.a: $(PTOS)/obj/x32/libptos-vdi.a
	@mkdir -p $(dir $@)
	$(X32_OBJCOPY) --redefine-sym vdi=rcs_native_vdi $< $@

.PHONY: x32
x32: build/x32/$(PRG_NAME) $(addprefix build/x32/,$(RES_FILES)) \
    $(patsubst %.c,build/x32/%.o,$(RSC_LIB_SRC))

build/x32/%.o: %.c $(GEMLIB_H)
	@mkdir -p $(dir $@)
	$(X32_CC) $(X32_CFLAGS) -c $< -o $@

$(X32_LIBCMINI): $(LIBCMINI_REV)
	rm -rf build/x32/libcmini
	mkdir -p build/x32/libcmini $(dir $@)
	$(LIBCMINI_COPY) build/x32/libcmini/
	$(MAKE) -C build/x32/libcmini ptos_x86_64_defconfig
	$(MAKE) -C build/x32/libcmini CROSSPREFIX= CC='$(X32_CC)' \
	    ARCH_CFLAGS='-fPIC -fvisibility=hidden -fno-stack-protector' dirs libs
	cp build/x32/libcmini/build/libcmini.a $@

build/x32/$(PRG_NAME): $(X32_OBJ) $(X32_VDI_OBJ) $(X32_LIBCMINI) build/x32/lib/libptos-vdi.a \
    $(PTOS)/obj/ldso/dyncrt.o $(PTOS)/obj/ldso/libptos.so $(PTOS)/obj/libaes/libaes.so
	$(X32_CC) -mx32 -nostdlib -no-pie \
	    -Wl,--hash-style=sysv,-z,now,-z,noexecstack,-e,_start \
	    -Wl,--dynamic-linker,/GEMSYS/LIB/X32/LD.SO,-Ttext-segment=0x400000 \
	    $(PTOS)/obj/ldso/dyncrt.o $(X32_OBJ) $(X32_VDI_OBJ) \
	    -L$(PTOS)/obj/libaes -laes -L$(PTOS)/obj/ldso -lptos \
	    build/x32/lib/libptos-vdi.a $(X32_LIBCMINI) -o $@

-include $(X32_OBJ:.o=.d) $(X32_VDI_OBJ:.o=.d)
