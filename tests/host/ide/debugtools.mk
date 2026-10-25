# debugtools: the leak tracker, profiler and Intuition inspector models
IDE_AREAS += debugtools
debugtools_TESTS = debugtools_leaks_test.cpp debugtools_profile_test.cpp debugtools_inspect_test.cpp
debugtools_SRCS  = src/ide/LeakModel.cpp src/ide/ProfileModel.cpp src/ide/InspectModel.cpp src/dbg/resnames.cpp
