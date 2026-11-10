# --- basic --- Boing BASIC: the line syntax (blocks, indentation), the lexer and its folding,
# the control/event table and forms-events.md, code-behind subs (finding, creating, renaming),
# the outline, .bbp files and the boing command lines.
IDE_AREAS += basic
BASIC_LEXLIB := $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/third_party/lexilla/lexlib/*.cxx))
basic_TESTS = basic_test.cpp basic_lexer_test.cpp
basic_SRCS  = src/sci/BasicSyntax.cpp src/sci/Languages.cpp src/ide/BasicModel.cpp src/ide/Symbols.cpp \
              src/ide/LangSymbols.cpp src/ide/GuiModel.cpp src/ide/TemplateEngine.cpp src/sci/LexBoing.cpp $(BASIC_LEXLIB)
basic_HDRS  = src/sci/BasicSyntax.h src/sci/LexBoing.h src/ide/BasicModel.h src/ide/BasicFormsEvents.h src/sci/Languages.h
basic_LIBS  = -I$(ROOT)/third_party/scintilla/include -I$(ROOT)/third_party/lexilla/include \
              -I$(ROOT)/third_party/lexilla/lexlib
# the same tests built with AddressSanitizer/UBSan: make -C tests/host basic-asan
basic-asan:
	@mkdir -p $(OUT)
	$(CXX) $(IDE_CXXFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer ide/main.cpp $(addprefix ide/,$(basic_TESTS)) $(addprefix $(ROOT)/,$(basic_SRCS)) $(basic_LIBS) -o $(OUT)/ide_basic_asan
	cd $(CURDIR) && $(OUT)/ide_basic_asan
.PHONY: basic-asan
