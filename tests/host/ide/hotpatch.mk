# Edit and Continue (src/dbg/hotpatch.c): the linker, data-layout checks, branch encoding
# and debug-info merge, against objects and programs built by the OS4 cross-compiler.
IDE_AREAS += hotpatch
hotpatch_TESTS = hotpatch_test.cpp
hotpatch_SRCS  = build/host/hp/libhp.a
HP_OUT  = $(OUT)/hp
HP_LIBSRC = hotpatch.c util.c elf.c stabs.c info.c
HP_OBJS = $(patsubst %.c,$(HP_OUT)/obj/%.o,$(HP_LIBSRC)) $(HP_OUT)/obj/demangle.o
$(HP_OUT)/obj/%.o: $(ROOT)/src/dbg/%.c $(ROOT)/src/dbg/dbg_int.h $(ROOT)/src/dbg/hotpatch.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(HP_OUT)/obj/demangle.o: $(ROOT)/src/dbg/demangle.cpp
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -g -I$(ROOT)/src -c $< -o $@
$(HP_OUT)/libhp.a: $(HP_OBJS)
	rm -f $@; ar rcs $@ $^
$(HP_OUT)/.fixtures: $(wildcard ide/hotpatch/*)
	@mkdir -p $(HP_OUT)
	docker run --rm -v $(abspath $(ROOT)):/src -w /src $(IMAGE) sh tests/host/ide/hotpatch/build.sh build/host/hp
$(OUT)/ide_hotpatch: $(HP_OUT)/.fixtures
