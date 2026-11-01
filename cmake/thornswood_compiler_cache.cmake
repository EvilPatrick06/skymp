# THORNSWOOD (Thornswood #5191, the plan's part 7, "cache the engine build").
#
# WHY. On 31 Oct 2026 the fork's pull request build took 32 minutes (run
# 41254727003), and 25.6 of them were `cmake --build`: MSVC compiled every
# file of every project on a fresh runner, though the pull request changed a
# handful. server_guest_lib took 489 s and unit 522 s of that. Nothing kept a
# compiled file from one build to the next. vcpkg's libraries were already
# kept (GitHub Packages), and the masters the tests read (the Actions cache),
# but not the engine's own objects.
#
# WHAT. When SKYMP_COMPILER_CACHE names ccache.exe (the workflow passes it as
# an environment variable of the same name), every MSVC compile of this build
# goes through ccache, which hands back the object a compile with the same
# compiler, flags and preprocessed source made before, and compiles only what
# changed. Its store is kept between builds in the Actions cache
# (.github/workflows/thornswood-pr-windows.yml says who may write it).
#
# HOW, the Visual Studio generator's way (ccache's wiki, "MS Visual Studio"):
#   - ccache.exe is copied into the build folder as cl.exe, and every
#     project's CLToolPath names that folder, so MSBuild starts ccache, which
#     finds the real cl.exe on PATH and runs it when it has no object;
#   - UseMultiToolTask runs one cl.exe per file, files side by side, because
#     ccache cannot cache a compile that names several files, which is what
#     /MP does (apply_default_settings.cmake leaves /MP out while this is on);
#   - EnforceProcessCountAcrossBuilds holds the compilers of every project
#     MSBuild builds at once to one count, the processors the machine has, so
#     the fork's build can build its projects side by side (MSBuild /m, the
#     PARALLEL input of .github/actions/pr_base) without each project
#     starting that many compilers of its own;
#   - debug information goes into each object (/Z7) instead of a shared
#     program database (/Zi), which ccache cannot cache. The linker still
#     writes every binary's .pdb (/DEBUG:FULL), so Write-ClientLicences.ps1
#     reads the same thing it read before;
#   - skyrim_platform and skyrim_platform_entry, which precompile a header,
#     include it in each source instead (/FI), because ccache caches no MSVC
#     compile that uses a precompiled header (skyrim-platform/src/platform_se,
#     Thornswood #5216).
# Off, which is every build that does not set it (upstream's and a person's
# own), nothing here changes the build.
#
# This file is included before project(), with NO_POLICY_SCOPE, because
# CMP0141 (the debug information format as a property) has to be set before
# the compiler is enabled and has to outlive the include. CMAKE_BINARY_DIR is
# the build folder already here (the root CMakeLists.txt holds it to
# <repo>/build).

set(SKYMP_COMPILER_CACHE "$ENV{SKYMP_COMPILER_CACHE}" CACHE FILEPATH
  "ccache.exe to compile through (Thornswood #5191); empty compiles as usual")

set(SKYMP_COMPILER_CACHE_ON OFF)
if(NOT "${SKYMP_COMPILER_CACHE}" STREQUAL "")
  if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
    message(FATAL_ERROR "SKYMP_COMPILER_CACHE is for the Visual Studio generator on Windows; this is ${CMAKE_GENERATOR}. Leave it empty.")
  endif()
  if(NOT EXISTS "${SKYMP_COMPILER_CACHE}")
    message(FATAL_ERROR "SKYMP_COMPILER_CACHE names ${SKYMP_COMPILER_CACHE}, which does not exist. Leave it empty to compile without ccache.")
  endif()
  set(SKYMP_COMPILER_CACHE_ON ON)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
  file(COPY_FILE "${SKYMP_COMPILER_CACHE}" "${CMAKE_BINARY_DIR}/cl.exe" ONLY_IF_DIFFERENT)
  set(CMAKE_VS_GLOBALS
    "CLToolExe=cl.exe"
    "CLToolPath=${CMAKE_BINARY_DIR}"
    "UseMultiToolTask=true"
    "EnforceProcessCountAcrossBuilds=true"
  )
  message(STATUS "Compiling through ccache: ${SKYMP_COMPILER_CACHE}, as ${CMAKE_BINARY_DIR}/cl.exe")
endif()
