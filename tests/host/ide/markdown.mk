# --- markdown --- the Markdown preview's parser (md4c) and layout
IDE_AREAS += markdown
markdown_TESTS = markdown_test.cpp
markdown_SRCS  = src/ide/Markdown.cpp
markdown_HDRS  = src/ide/Markdown.h third_party/md4c/md4c.h
markdown_LIBS  = $(OUT)/obj/md4c.o
$(OUT)/obj/md4c.o: $(ROOT)/third_party/md4c/md4c.c $(ROOT)/third_party/md4c/md4c.h
	@mkdir -p $(@D)
	$(CC) -std=gnu11 -g -O1 $(MD4C_SAN) -c $< -o $@
$(OUT)/ide_markdown: $(OUT)/obj/md4c.o
# with AddressSanitizer/UBSan: make -C tests/host markdown-asan
markdown-asan:
	@mkdir -p $(OUT)/obj
	$(CC) -std=gnu11 -g -O1 -fsanitize=address,undefined -c $(ROOT)/third_party/md4c/md4c.c -o $(OUT)/obj/md4c_asan.o
	$(CXX) $(IDE_CXXFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer ide/main.cpp $(addprefix ide/,$(markdown_TESTS)) $(addprefix $(ROOT)/,$(markdown_SRCS)) $(OUT)/obj/md4c_asan.o -o $(OUT)/ide_markdown_asan
	cd $(CURDIR) && $(OUT)/ide_markdown_asan
.PHONY: markdown-asan
