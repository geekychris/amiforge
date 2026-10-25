# integrations: git (objects, status, diff, blame), hex editor, ARexx commands, distcc, explanations.
# The git tests make real repositories with the host's git (needs git on the PATH).
IDE_AREAS += integrations
integrations_TESTS = integrations_git_test.cpp integrations_diff_test.cpp integrations_hex_test.cpp \
                     integrations_rexx_test.cpp integrations_distcc_test.cpp integrations_explain_test.cpp
integrations_SRCS  = src/ide/GitCore.cpp src/ide/LineDiff.cpp src/ide/HexData.cpp src/ide/RexxCmd.cpp \
                     src/ide/DistccCmd.cpp src/ide/Explain.cpp
integrations_HDRS  = src/ide/GitCore.h src/ide/LineDiff.h src/ide/HexData.h src/ide/RexxCmd.h \
                     src/ide/DistccCmd.h src/ide/Explain.h
integrations_LIBS  = -lz
