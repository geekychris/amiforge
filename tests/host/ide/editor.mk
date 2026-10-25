# Editor features: themes, snippets, outline / references / rename.
IDE_AREAS += editor
editor_TESTS = editor_theme_test.cpp editor_snippets_test.cpp editor_symbols_test.cpp
editor_SRCS  = src/sci/Theme.cpp src/ide/Snippets.cpp src/ide/Symbols.cpp
