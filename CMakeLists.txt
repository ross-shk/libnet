# libnet — CMake build (portable: Linux, macOS, Windows).
#
# Layout:
#   lib net        <- compiled source/net.pli [if the PL/I compiler is
#                     available], linking libsock.a (external dependency).
#   dist/net.inc   <- include/net.inc + libsock type_defs.inc + errno.inc minus %include lines
#   examples       <- trial-compile of examples/*.pli (needs pli-llvm)
#
# libsock (the socket compatibility layer) must be built and installed first.
# Point CMake at it with -DLIBSOCK_ROOT=<prefix> (auto-detected from
# /usr/local or /opt/libsock). Point CMake at the PL/I compiler with
# -DPLI_EXECUTABLE=... or PLI_LLVM.
cmake_minimum_required(VERSION 3.16)
project(libnet LANGUAGES C VERSION 0.1.0)

include(GNUInstallDirs)
include(CTest)

option(LIBNET_ENABLE_PLI "Build the compiled PL/I module when the PL/I compiler is found" ON)
option(LIBNET_BUILD_EXAMPLES "Trial-compile the PL/I examples (needs pli-llvm)" ON)

set(LIBNET_PLIFLAGS "" CACHE STRING "Extra flags forwarded to the PL/I compiler")
set(PLI_LLVM "$ENV{PLI_LLVM}" CACHE PATH "pli-llvm install prefix (bin/pli)")
set(LIBSOCK_ROOT "" CACHE PATH "libsock install prefix")

find_library(LIBSOCK_LIB
  NAMES sock
  PATHS
    "${LIBSOCK_ROOT}/lib"
    /usr/local/lib
    /opt/libsock/lib
  DOC "libsock static library (libsock.a)")
find_path(LIBSOCK_INCLUDE_DIR
  NAMES sock.inc
  PATHS
    "${LIBSOCK_ROOT}/include"
    /usr/local/include
    /opt/libsock/include
  DOC "libsock header directory")
if(NOT LIBSOCK_LIB OR NOT LIBSOCK_INCLUDE_DIR)
  message(FATAL_ERROR
    "libsock not found: LIBSOCK_LIB=${LIBSOCK_LIB} "
    "LIBSOCK_INCLUDE_DIR=${LIBSOCK_INCLUDE_DIR}. "
    "Build and install libsock first, then set -DLIBSOCK_ROOT=<prefix>.")
endif()
mark_as_advanced(LIBSOCK_LIB LIBSOCK_INCLUDE_DIR)

# ---------------------------------------------------------------- C bridge
# source/c_bridge.c now lives in libsock; the net target no longer compiles
# any C source.  c_* symbols come from libsock (libc on POSIX, c_bridge.c on
# Windows).  net links libsock as a PUBLIC dependency so consumers of libnet
# also pull in libsock.a.
add_library(net STATIC)
add_library(libnet::net ALIAS net)
set_target_properties(net PROPERTIES LINKER_LANGUAGE C)
set_target_properties(net PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  OUTPUT_NAME net)
target_compile_options(net PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
  $<$<C_COMPILER_ID:MSVC>:/W4>)
target_link_libraries(net PUBLIC ${LIBSOCK_LIB})

# ------------------------------------------------------- PL/I module (opt)
find_program(PLI_EXECUTABLE NAMES pli
  HINTS "${PLI_LLVM}/bin" /usr/local/bin /opt/pli-llvm/bin
  DOC "pli-llvm compiler driver")
mark_as_advanced(PLI_EXECUTABLE)

if(LIBNET_ENABLE_PLI AND PLI_EXECUTABLE)
  set(LIBNET_HAVE_PLI TRUE)
else()
  set(LIBNET_HAVE_PLI FALSE)
endif()

set(LIBNET_PLI_INCLUDES
  "${LIBSOCK_INCLUDE_DIR}/c_bridge.inc"
  "${LIBSOCK_INCLUDE_DIR}/sock.inc"
  "${LIBSOCK_INCLUDE_DIR}/type_defs.inc"
  "${LIBSOCK_INCLUDE_DIR}/errno.inc")

if(LIBNET_HAVE_PLI)
  # Compile the net PL/I module; the object joins the static lib.  sock.o now
  # lives in libsock - net links it via ${LIBSOCK_LIB}.
  set(LIBNET_PLI_OBJ_NET "${CMAKE_CURRENT_BINARY_DIR}/net${CMAKE_C_OUTPUT_EXTENSION}")
  if(LIBNET_PLIFLAGS)
    set(_pli_flags ${LIBNET_PLIFLAGS})
  endif()
  add_custom_command(
    OUTPUT "${LIBNET_PLI_OBJ_NET}"
    COMMAND "${PLI_EXECUTABLE}" ${_pli_flags}
      -c "${CMAKE_CURRENT_SOURCE_DIR}/source/net.pli"
      -I "${LIBSOCK_INCLUDE_DIR}"
      -o "${LIBNET_PLI_OBJ_NET}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/net.pli" ${LIBNET_PLI_INCLUDES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "PL/I compiler: compiling source/net.pli"
    VERBATIM)
  target_sources(net PRIVATE "${LIBNET_PLI_OBJ_NET}")
  message(STATUS "libnet: PL/I compiler found (${PLI_EXECUTABLE}), PL/I module enabled")
else()
  message(STATUS "libnet: PL/I compiler not found, building C bridge only (PL/I module skipped)")
endif()

# ------------------------------------------------------------ dist/net.inc
set(LIBNET_DIST_INC "${CMAKE_CURRENT_BINARY_DIR}/dist/net.inc")
add_custom_command(
  OUTPUT "${LIBNET_DIST_INC}"
  COMMAND "${CMAKE_COMMAND}"
    "-DINC_FILES=${CMAKE_CURRENT_SOURCE_DIR}/include/net.inc$<SEMICOLON>${LIBSOCK_INCLUDE_DIR}/type_defs.inc$<SEMICOLON>${LIBSOCK_INCLUDE_DIR}/errno.inc"
    "-DOUT_FILE=${LIBNET_DIST_INC}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenDistInc.cmake"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/net.inc"
    "${LIBSOCK_INCLUDE_DIR}/type_defs.inc"
    "${LIBSOCK_INCLUDE_DIR}/errno.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenDistInc.cmake"
  COMMENT "Generating dist/net.inc"
  VERBATIM)
add_custom_target(libnet_dist_inc ALL DEPENDS "${LIBNET_DIST_INC}")

# ------------------------------------------------------------------ tests
# cb_test removed: C bridge regression test no longer applies since
# c_bridge.c is Windows-only and c_* resolves to libc on POSIX.

# ------------------------------------------------------ examples (PL/I only)
if(LIBNET_HAVE_PLI AND LIBNET_BUILD_EXAMPLES)
  set(LIBNET_EXAMPLES
    echo_server client resolve fetch fetch_dyn http_client multi_echo_server
    test_read_all test_resolve simple_usage sock_usage)
  set(_example_objs)
  foreach(ex IN LISTS LIBNET_EXAMPLES)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/examples/${ex}.pli")
    if(EXISTS "${_src}")
      set(_obj "${CMAKE_CURRENT_BINARY_DIR}/examples/${ex}${CMAKE_C_OUTPUT_EXTENSION}")
      add_custom_command(
        OUTPUT "${_obj}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/examples"
        COMMAND "${PLI_EXECUTABLE}" ${_pli_flags}
          -c "${_src}"
          -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
          -I "${LIBSOCK_INCLUDE_DIR}"
          -o "${_obj}"
        DEPENDS "${_src}" "${LIBNET_DIST_INC}" "${LIBSOCK_INCLUDE_DIR}/sock.inc"
        COMMENT "PL/I compiler: trial-compiling examples/${ex}.pli"
        VERBATIM)
      list(APPEND _example_objs "${_obj}")
    endif()
  endforeach()
  add_custom_target(libnet-examples DEPENDS ${_example_objs})
endif()

# Helper: compile+link one PL/I program against the built lib.
function(libnet_add_pli_program name src)
  if(NOT LIBNET_HAVE_PLI)
    # Still create the target so `cmake --build build --target <name>` does not
    # fail with MSB1009 ("project file does not exist") on generators like Visual
    # Studio that emit a .vcxproj per target. The target prints a helpful error.
    add_custom_target(${name}
      COMMAND ${CMAKE_COMMAND} -E echo
        "libnet: cannot build '${name}' — PL/I compiler was not found during configuration."
      COMMAND ${CMAKE_COMMAND} -E echo
        "Set -DPLI_EXECUTABLE=<path> or install pli-llvm, then reconfigure."
      COMMAND ${CMAKE_COMMAND} -E false
      COMMENT "PL/I compiler not available — ${name} cannot be built"
      VERBATIM)
    message(WARNING "libnet_add_pli_program(${name}): PL/I compiler unavailable, target created but will fail at build time")
    return()
  endif()
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/${name}${CMAKE_EXECUTABLE_SUFFIX}")
  # On macOS pli-llvm's lld needs -undefined dynamic_lookup so the C bridge's
  # socket symbols (resolved by dyld at runtime from the host's libSystem)
  # do not cause a hard link error. PLATFORM_ID checks avoid passing this
  # flag on Windows (lld-link /MSVC) or Linux.
  if(APPLE)
    set(_pli_extra -Wl,-undefined -Wl,dynamic_lookup)
  else()
    set(_pli_extra)
  endif()
  if(WIN32)
    # pli-llvm's lld-link does not understand -L/-l on Windows; pass the full
    # path to the static libs and use /DEFAULTLIB for Winsock.
    add_custom_target(${name}
      COMMAND "${PLI_EXECUTABLE}" ${_pli_flags}
        "${src}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
        -I "${LIBSOCK_INCLUDE_DIR}"
        -o "${_out}"
        "$<TARGET_FILE:net>"
        "${LIBSOCK_LIB}"
        -Wl,/DEFAULTLIB:ws2_32
      DEPENDS net "${LIBNET_DIST_INC}" "${src}" ${LIBNET_PLI_INCLUDES}
      COMMENT "PL/I compiler: linking ${name}"
      VERBATIM)
  else()
    add_custom_target(${name}
      COMMAND "${PLI_EXECUTABLE}" ${_pli_flags}
        "${src}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
        -I "${LIBSOCK_INCLUDE_DIR}"
        -o "${_out}"
        -L "$<TARGET_FILE_DIR:net>" -lnet
        "${LIBSOCK_LIB}"
        ${_pli_extra}
      DEPENDS net "${LIBNET_DIST_INC}" "${src}" ${LIBNET_PLI_INCLUDES}
      COMMENT "PL/I compiler: linking ${name}"
      VERBATIM)
  endif()
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
