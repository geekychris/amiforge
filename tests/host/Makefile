# Host-side unit tests for the portable debugger core (ELF/stabs, PPC decoder, frames,
# expressions). Needs a host cc/c++ and Docker (objdump reference dumps of the targets).
#   make -C tests/host        (build the PPC targets first: tools/build.sh tests)
ROOT   = ../..
OUT    = $(ROOT)/build/host
CC    ?= cc
CXX   ?= c++
IMAGE ?= walkero/amigagccondocker:os4-gcc11
CFLAGS = -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I$(ROOT)/src
SRC    = $(addprefix $(ROOT)/src/dbg/,util.c elf.c stabs.c info.c ppc.c frame.c eval.c)
TARGETS = basic cplus asmtest
DUMPS  = $(foreach t,$(TARGETS),$(OUT)/$(t).G $(OUT)/$(t).d $(OUT)/$(t).syms)

all: run

OBJS   = $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(SRC)) dbg_hosttest.c) $(OUT)/obj/demangle.o
HDRS   = $(ROOT)/src/dbg/dbg_int.h $(ROOT)/src/dbg/dbg.h
vpath %.c $(ROOT)/src/dbg .
vpath %.cpp $(ROOT)/src/dbg

$(OUT)/obj/%.o: %.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/obj/%.o: %.cpp $(HDRS)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -g -I$(ROOT)/src -c $< -o $@
$(OUT)/dbg_hosttest: $(OBJS)
	$(CXX) $(OBJS) -o $@

# objdump reference output for each target (stabs, disassembly, symbol table)
$(OUT)/%.G: $(ROOT)/build/targets/%
	@mkdir -p $(OUT)
	docker run --rm -v $(abspath $(ROOT)):/src -w /src $(IMAGE) ppc-amigaos-objdump -G build/targets/$* > $@
$(OUT)/%.d: $(ROOT)/build/targets/%
	@mkdir -p $(OUT)
	docker run --rm -v $(abspath $(ROOT)):/src -w /src $(IMAGE) ppc-amigaos-objdump -d build/targets/$* > $@
$(OUT)/%.syms: $(ROOT)/build/targets/%
	@mkdir -p $(OUT)
	docker run --rm -v $(abspath $(ROOT)):/src -w /src $(IMAGE) ppc-amigaos-readelf -sW build/targets/$* > $@

$(ROOT)/build/targets/%:
	@echo "missing $@: build the targets first with tools/build.sh tests" && false

run: $(OUT)/dbg_hosttest $(DUMPS)
	$(OUT)/dbg_hosttest $(ROOT)/build/targets $(OUT) $(ROOT)/tests/targets

clean:
	rm -rf $(OUT)

.PHONY: all run clean
.PRECIOUS: $(DUMPS)
