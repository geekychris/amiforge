# Each area: a runner name, its test sources (in tests/host/ide) and the src/ files it needs.
# Code under test must build on the host: keep OS calls out of it (pass data in, get data out).
IDE_AREAS += example
example_TESTS = example_test.cpp
example_SRCS  =
