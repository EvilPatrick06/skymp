# Compiles the .psc files in DIRECTORY with the Creation Kit's Papyrus
# compiler, into the build folder (${CMAKE_CURRENT_BINARY_DIR}/pex). The
# compiled .pex files committed in PREBUILT_DIR are used as they are when the
# compiler is not there. PEX_DIR_VARIABLE names a variable that is set to the
# folder the .pex files are in either way, for whatever copies or reads them.
#
# The compiler used to write into the source tree, over the committed .pex
# files, and a .pex carries the time, the user and the machine it was compiled
# on: every build that compiled left five tracked files changed, so a built
# fork always read as having uncommitted changes (Thornswood #1220).
function(add_papyrus_library_ck)
    set(options)
    set(oneValueArgs NAME DIRECTORY COMPILER_EXECUTABLE_PATH PREBUILT_DIR PEX_DIR_VARIABLE)
    set(multiValueArgs)
    cmake_parse_arguments(A
      "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN}
    )

    file(GLOB src ${A_DIRECTORY}/*.psc)

    if(NOT A_PREBUILT_DIR)
      message(FATAL_ERROR "PREBUILT_DIR was not specified")
    endif()
    if(EXISTS "${A_COMPILER_EXECUTABLE_PATH}")
      set(pex_dir "${CMAKE_CURRENT_BINARY_DIR}/pex")
      # Copy src to out and for each element change extension from .psc to .pex
      set(out)
      foreach(file ${src})
        get_filename_component(file "${file}" NAME_WE)
        set(file "${pex_dir}/${file}.pex")
        list(APPEND out ${file})
      endforeach()

      add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${pex_dir}
        COMMAND "${A_COMPILER_EXECUTABLE_PATH}" ${A_DIRECTORY} -flags=${CMAKE_SOURCE_DIR}/cmake/TESV_Papyrus_Flags.flg -output=${pex_dir} -import=${A_DIRECTORY} -all
        DEPENDS ${src}
      )
      add_custom_target(${A_NAME} ALL
        DEPENDS ${out}
        SOURCES ${src}
      )
    else()
      set(pex_dir "${A_PREBUILT_DIR}")
      # dummy target for post build events
      add_custom_target(${A_NAME} ALL
        COMMAND ${CMAKE_COMMAND} -E sleep 0
      )
      if(NOT A_COMPILER_EXECUTABLE_PATH MATCHES "OFF.*")
        message(STATUS "Skipping optional ${A_NAME} target (requires Creation Kit)")
      endif()
    endif()
    if(A_PEX_DIR_VARIABLE)
      set(${A_PEX_DIR_VARIABLE} "${pex_dir}" PARENT_SCOPE)
    endif()
endfunction()
