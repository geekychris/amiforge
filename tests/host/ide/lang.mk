# --- lang --- Python and the other languages: language detection and indentation rules, the
# outline / references for each language, Python's checker and runner scripts (run with the
# host's python3), the traceback parser, finding the interpreter, the key table, Python
# explanations, and the ARexx / AmigaDOS / Python lexers (Lexilla's lexlib on a test document).
IDE_AREAS += lang
LANG_LEXLIB := $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/third_party/lexilla/lexlib/*.cxx))
lang_TESTS = lang_test.cpp lang_lexer_test.cpp
lang_SRCS  = src/sci/Languages.cpp src/ide/LangSymbols.cpp src/ide/Symbols.cpp src/ide/LangTools.cpp \
             src/ide/KeyBindings.cpp src/ide/Snippets.cpp src/ide/Explain.cpp src/sci/LexAmiga.cpp \
             third_party/lexilla/lexers/LexPython.cxx $(LANG_LEXLIB)
lang_HDRS  = src/sci/Languages.h src/ide/LangTools.h src/ide/KeyBindings.h src/sci/LexAmiga.h
# the lexers need Scintilla's and Lexilla's headers (only this area: their names clash with ours)
lang_LIBS  = -I$(ROOT)/third_party/scintilla/include -I$(ROOT)/third_party/lexilla/include \
             -I$(ROOT)/third_party/lexilla/lexlib
