# --- mcp --- the MCP side of the ARexx port: escaping, the paged result envelope, line edits,
# and the new commands' templates.
IDE_AREAS += mcp
mcp_TESTS = mcp_test.cpp
mcp_SRCS  = src/ide/McpCmd.cpp src/ide/RexxCmd.cpp
mcp_HDRS  = src/ide/McpCmd.h src/ide/RexxCmd.h
