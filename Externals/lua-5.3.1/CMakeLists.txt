# Lua 5.3.1 as an embeddable static library for Dolphin.
#
# Only the interpreter/runtime sources are built here. The two standalone
# front-ends (src/lua.c and src/luac.c) each define their own main() and are
# intentionally excluded so this can be linked into Dolphin.

add_library(lua STATIC
  src/lapi.c
  src/lauxlib.c
  src/lbaselib.c
  src/lbitlib.c
  src/lcode.c
  src/lcorolib.c
  src/lctype.c
  src/ldblib.c
  src/ldebug.c
  src/ldo.c
  src/ldump.c
  src/lfunc.c
  src/lgc.c
  src/linit.c
  src/liolib.c
  src/llex.c
  src/lmathlib.c
  src/lmem.c
  src/loadlib.c
  src/lobject.c
  src/lopcodes.c
  src/loslib.c
  src/lparser.c
  src/lstate.c
  src/lstring.c
  src/lstrlib.c
  src/ltable.c
  src/ltablib.c
  src/ltm.c
  src/lundump.c
  src/lutf8lib.c
  src/lvm.c
  src/lzio.c
)

target_include_directories(lua
PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Enable platform-appropriate features (dynamic library loading via dlopen,
# correct os.* behaviour). Harmless when building purely as an embedded lib.
if(UNIX AND NOT APPLE)
  target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
  target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS})
elseif(APPLE)
  target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
  target_link_libraries(lua PRIVATE ${CMAKE_DL_LIBS})
endif()

dolphin_disable_warnings(lua)
