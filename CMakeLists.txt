# libnet — CMake build (portable: Linux, macOS, Windows).
#
# Layout:
#   lib net        <- source/c_bridge.c [+ compiled source/net.pli if plic
#                     is available]  (static lib, installed with net.inc)
#   dist/net.inc   <- include/{net,type_defs,errno}.inc minus %include lines
#   cb_test        <- C bridge regression test (CTest)
#   examples       <- trial-compile of examples/*.pli (needs plic)
#
# The PL/I module is optional: pli-llvm's `plic` is usually absent on
# Windows, so when it is not found the C bridge + its test still build
# and run. Point CMake at plic with -DPLIC_EXECUTABLE=... or PLI_LLVM.
cmake_minimum_required(VERSION 3.16)
project(libnet LANGUAGES C VERSION 0.1.0)

include(GNUInstallDirs)
include(CTest)

option(LIBNET_ENABLE_PLI "Build the compiled PL/I module when plic is found" ON)
option(LIBNET_BUILD_EXAMPLES "Trial-compile the PL/I examples (needs plic)" ON)

set(LIBNET_PLIFLAGS "" CACHE STRING "Extra flags forwarded to plic")
set(PLI_LLVM "$ENV{PLI_LLVM}" CACHE PATH "pli-llvm install prefix (bin/plic)")

# ---------------------------------------------------------------- C bridge
add_library(net STATIC source/c_bridge.c)
add_library(libnet::net ALIAS net)
set_target_properties(net PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  OUTPUT_NAME net)
target_compile_options(net PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
  $<$<C_COMPILER_ID:MSVC>:/W4>)
if(WIN32)
  # Winsock2 (WSAStartup/WSAPoll/getaddrinfo/...). PUBLIC so consumers of
  # the static lib link it too.
  target_link_libraries(net PUBLIC ws2_32)
  target_compile_definitions(net PRIVATE _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN)
endif()

# ------------------------------------------------------- PL/I module (opt)
find_program(PLIC_EXECUTABLE NAMES plic
  HINTS "${PLI_LLVM}/bin" /usr/local/bin /opt/pli-llvm/bin
  DOC "pli-llvm compiler driver")
mark_as_advanced(PLIC_EXECUTABLE)

if(LIBNET_ENABLE_PLI AND PLIC_EXECUTABLE)
  set(LIBNET_HAVE_PLI TRUE)
else()
  set(LIBNET_HAVE_PLI FALSE)
endif()

set(LIBNET_PLI_INCLUDES
  "${CMAKE_CURRENT_SOURCE_DIR}/include/c_bridge.inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/type_defs.inc"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/errno.inc")

if(LIBNET_HAVE_PLI)
  # Compile source/net.pli once; the object joins the static lib alongside
  # the C bridge object (equivalent to the old `ar rcs libnet.a c_bridge.o net.o`).
  set(LIBNET_PLI_OBJ "${CMAKE_CURRENT_BINARY_DIR}/net${CMAKE_C_OUTPUT_EXTENSION}")
  if(LIBNET_PLIFLAGS)
    set(_pli_flags ${LIBNET_PLIFLAGS})
  endif()
  add_custom_command(
    OUTPUT "${LIBNET_PLI_OBJ}"
    COMMAND "${PLIC_EXECUTABLE}" ${_pli_flags}
      -c "${CMAKE_CURRENT_SOURCE_DIR}/source/net.pli"
      -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
      -o "${LIBNET_PLI_OBJ}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/net.pli" ${LIBNET_PLI_INCLUDES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "plic: compiling source/net.pli"
    VERBATIM)
  target_sources(net PRIVATE "${LIBNET_PLI_OBJ}")
  message(STATUS "libnet: plic found (${PLIC_EXECUTABLE}), PL/I module enabled")
else()
  message(STATUS "libnet: plic not found, building C bridge only (PL/I module skipped)")
endif()

# ------------------------------------------------------------ dist/net.inc
set(LIBNET_DIST_INC "${CMAKE_CURRENT_BINARY_DIR}/dist/net.inc")
add_custom_command(
  OUTPUT "${LIBNET_DIST_INC}"
  COMMAND "${CMAKE_COMMAND}"
    "-DINC_FILES=${CMAKE_CURRENT_SOURCE_DIR}/include/net.inc$<SEMICOLON>${CMAKE_CURRENT_SOURCE_DIR}/include/type_defs.inc$<SEMICOLON>${CMAKE_CURRENT_SOURCE_DIR}/include/errno.inc"
    "-DOUT_FILE=${LIBNET_DIST_INC}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenDistInc.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/net.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/type_defs.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/errno.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenDistInc.cmake"
  COMMENT "Generating dist/net.inc"
  VERBATIM)
add_custom_target(libnet_dist_inc ALL DEPENDS "${LIBNET_DIST_INC}")

# ------------------------------------------------------------------ tests
if(BUILD_TESTING)
  add_executable(cb_test source/c_bridge.c tests/c_bridge.c)
  set_target_properties(cb_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
  target_compile_options(cb_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
    $<$<C_COMPILER_ID:MSVC>:/W4>)
  if(WIN32)
    target_link_libraries(cb_test PRIVATE ws2_32)
    target_compile_definitions(cb_test PRIVATE _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN)
  endif()
  add_test(NAME c_bridge COMMAND cb_test)
endif()

# ------------------------------------------------------ examples (pli only)
if(LIBNET_HAVE_PLI AND LIBNET_BUILD_EXAMPLES)
  set(LIBNET_EXAMPLES
    echo_server client resolve fetch fetch_dyn http_client
    test_read_all test_resolve simple_usage)
  set(_example_objs)
  foreach(ex IN LISTS LIBNET_EXAMPLES)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/examples/${ex}.pli")
    if(EXISTS "${_src}")
      set(_obj "${CMAKE_CURRENT_BINARY_DIR}/examples/${ex}${CMAKE_C_OUTPUT_EXTENSION}")
      add_custom_command(
        OUTPUT "${_obj}"
        COMMAND "${PLIC_EXECUTABLE}" ${_pli_flags}
          -c "${_src}"
          -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
          -o "${_obj}"
        DEPENDS "${_src}" "${LIBNET_DIST_INC}"
        COMMENT "plic: trial-compiling examples/${ex}.pli"
        VERBATIM)
      list(APPEND _example_objs "${_obj}")
    endif()
  endforeach()
  add_custom_target(libnet-examples DEPENDS ${_example_objs})
endif()

# Helper: compile+link one PL/I program against the built lib.
# Equivalent to: plic <src> -I include -o <out> -L <libdir> -lnet
function(libnet_add_pli_program name src)
  if(NOT LIBNET_HAVE_PLI)
    message(WARNING "libnet_add_pli_program(${name}): plic unavailable, skipping")
    return()
  endif()
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/${name}${CMAKE_EXECUTABLE_SUFFIX}")
  add_custom_target(${name}
    COMMAND "${PLIC_EXECUTABLE}" ${_pli_flags}
      "${src}"
      -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
      -o "${_out}"
      -L "$<TARGET_FILE_DIR:net>" -lnet
    DEPENDS net "${LIBNET_DIST_INC}" "${src}"
    COMMENT "plic: linking ${name}"
    VERBATIM)
endfunction()

# ----------------------------------------------------------- Quick Start target
# simple_usage.pli — the Quick Start example, buildable via CMake so the README
# instructions work out of the box.
libnet_add_pli_program(simple_usage
  "${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_usage.pli")

# ---------------------------------------------------------------- install
install(TARGETS net EXPORT libnet-targets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${LIBNET_DIST_INC}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  RENAME net.inc)
install(EXPORT libnet-targets
  NAMESPACE libnet::
  FILE libnet-targets.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libnet")

if(WIN32)
  set(LIBNET_PC_PRIVATE "-lws2_32")
endif()
configure_file(cmake/libnet.pc.in libnet.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libnet.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# --------------------------------------------------------------- uninstall
# Removes all files recorded in install_manifest.txt (produced by `cmake --install`).
configure_file(
  "cmake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  @ONLY)
add_custom_target(libnet_uninstall
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
