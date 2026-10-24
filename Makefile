# AmiForge — native IDE for AmigaOS 4.1 PPC.
# Cross-compiled with the adtools GCC 11 toolchain (tools/build.sh runs this in Docker).
#
#   make            the IDE (build/AmiForge) and the test programs
#   make DEBUG=1    with stabs debug info and no optimisation
#   make host-tests run the host-side unit tests (stabs, PPC decoder, expressions) — needs a host cc

CC      = ppc-amigaos-gcc
CXX     = ppc-amigaos-g++
AR      = ppc-amigaos-ar
STRIP   = ppc-amigaos-strip

ifeq ($(DEBUG),1)
OPT     = -O0 -gstabs
else
OPT     = -O2
endif

CPU      = -mcpu=440fp
DEFS     = -D__USE_OLD_TIMEVAL__ -DSCI_LEXER
INCS     = -Ithird_party/scintilla/include -Ithird_party/scintilla/src \
           -Ithird_party/lexilla/include -Ithird_party/lexilla/lexlib -Isrc
BASE     = -mcrt=newlib $(CPU) $(OPT) $(DEFS) $(INCS)
CFLAGS   = $(BASE) -std=gnu11 -Wall
CXXFLAGS = $(BASE) -std=c++17 -Wall
# third-party code is built without our warnings
TPFLAGS  = $(BASE) -std=c++17
LDFLAGS  = -mcrt=newlib -athread=native
LIBS     = -lauto

B = build
O = $(B)/obj

# --- Scintilla + Lexilla (unmodified upstream) + the OS4 platform layer ---
SCI_SRC   = $(wildcard third_party/scintilla/src/*.cxx)
LEXLIB    = $(wildcard third_party/lexilla/lexlib/*.cxx)
LEXERS    = $(addprefix third_party/lexilla/lexers/,LexCPP.cxx LexAsm.cxx LexMake.cxx \
            LexProps.cxx LexDiff.cxx LexA68k.cxx LexNull.cxx)
SCI_OS4   = $(wildcard src/sci/*.cpp)
SCI_OBJS  = $(patsubst %,$(O)/%.o,$(SCI_SRC) $(LEXLIB) $(LEXERS)) $(patsubst %,$(O)/%.o,$(SCI_OS4))

# --- debugger core ---
DBG_SRC   = $(wildcard src/dbg/*.c) $(wildcard src/dbg/*.cpp)
DBG_OBJS  = $(patsubst %,$(O)/%.o,$(DBG_SRC))

# --- the IDE ---
IDE_SRC   = $(wildcard src/ide/*.cpp) $(wildcard src/ide/*.c)
IDE_OBJS  = $(patsubst %,$(O)/%.o,$(IDE_SRC))

all: $(B)/AmiForge tests

$(B)/libscintilla.a: $(SCI_OBJS)
	@mkdir -p $(@D); rm -f $@; $(AR) rcs $@ $^

$(B)/libafdbg.a: $(DBG_OBJS)
	@mkdir -p $(@D); rm -f $@; $(AR) rcs $@ $^

$(B)/AmiForge: $(IDE_OBJS) $(B)/libscintilla.a $(B)/libafdbg.a
	$(CXX) $(LDFLAGS) -o $@.debug $(IDE_OBJS) -L$(B) -lscintilla -lafdbg $(LIBS)
	$(STRIP) -o $@ $@.debug

# test programs that run on OS4
TEST_SRC  = $(wildcard tests/os4/*.c) $(wildcard tests/os4/*.cpp)
TESTS     = $(patsubst tests/os4/%,$(B)/tests/%,$(basename $(TEST_SRC)))
# debuggee programs: always -O0 -gstabs+, never stripped. Assembler files named
# tests/targets/<prog>_*.S are linked into <prog> (see the asmtest rule).
TARGET_SRC = $(wildcard tests/targets/*.c) $(wildcard tests/targets/*.cpp)
TARGETS   = $(patsubst tests/targets/%,$(B)/targets/%,$(basename $(TARGET_SRC)))

tests: $(TESTS) $(TARGETS)

# the editor test needs only Scintilla
$(B)/tests/scitest: tests/os4/scitest.cpp $(B)/libscintilla.a
	@mkdir -p $(@D); $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -L$(B) -lscintilla $(LIBS)

$(B)/tests/%: tests/os4/%.cpp $(B)/libscintilla.a $(B)/libafdbg.a
	@mkdir -p $(@D); $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -L$(B) -lscintilla -lafdbg $(LIBS)

$(B)/tests/%: tests/os4/%.c $(B)/libafdbg.a
	@mkdir -p $(@D); $(CXX) $(CFLAGS) $(LDFLAGS) -x c $< -x none -o $@ -L$(B) -lafdbg $(LIBS)

TFLAGS = -mcrt=newlib -mcpu=440fp -O0 -gstabs+ -D__USE_INLINE__ -Wall
$(B)/targets/%: tests/targets/%.c
	@mkdir -p $(@D); $(CC) $(TFLAGS) -o $@ $< -lauto
$(B)/targets/%: tests/targets/%.cpp
	@mkdir -p $(@D); $(CXX) $(TFLAGS) -athread=native -o $@ $< -lauto
$(B)/targets/asmtest: tests/targets/asmtest.c $(patsubst tests/%.S,$(O)/tests/%.S.o,$(wildcard tests/targets/asmtest_*.S))
	@mkdir -p $(@D); $(CC) $(TFLAGS) -o $@ $^ -lauto
$(O)/tests/targets/%.S.o: tests/targets/%.S
	@mkdir -p $(@D); $(CC) $(TFLAGS) -c $< -o $@

# --- compile rules ---
$(O)/third_party/%.cxx.o: third_party/%.cxx
	@mkdir -p $(@D); $(CXX) $(TPFLAGS) -c $< -o $@
$(O)/%.cpp.o: %.cpp
	@mkdir -p $(@D); $(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(O)/%.c.o: %.c
	@mkdir -p $(@D); $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# --- host-side unit tests (run on the Mac/Linux host, not in Docker) ---
HOSTCC ?= cc
host-tests:
	$(MAKE) -C tests/host

clean:
	rm -rf $(B)

-include $(shell find $(O) -name '*.d' 2>/dev/null)

.PHONY: all tests clean host-tests
