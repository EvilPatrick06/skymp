function(apply_default_settings)
  cmake_parse_arguments(A "" "" "TARGETS" ${ARGN})
  foreach(arg TARGETS)
    if ("${A_${arg}}" STREQUAL "")
      message(FATAL_ERROR "Missing ${arg} argument")
    endif()
  endforeach()

  foreach(target ${A_TARGETS})
    set_target_properties(${target} PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      # "not all execution paths return value" must be error
      target_compile_options(${target} PRIVATE -Werror=return-type)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      # THORNSWOOD (Thornswood #5191). Through ccache: one file per cl.exe
      # (UseMultiToolTask runs them side by side, so no /MP) and the debug
      # information in each object (/Z7), which ccache can keep. Otherwise
      # /MP and a program database (/Zi), as upstream builds.
      # cmake/thornswood_compiler_cache.cmake says why.
      if(SKYMP_COMPILER_CACHE_ON)
        set(skymp_debug_and_mp /Z7)
      else()
        set(skymp_debug_and_mp /MP /Zi)
      endif()
      target_compile_options(${target} PRIVATE
        /WX /we4715 # "not all execution paths return value" must be error
        /wd4551 # disable non critical frida warning
        /wd5104 # disable non critical winsdk warning
        /wd5105 # TODO: investigate and fix
        ${skymp_debug_and_mp}
        /utf-8	# Set Source and Executable character sets to UTF-8

        /permissive-	# Standards conformance

        /Zc:alignedNew	# C++17 over-aligned allocation
        /Zc:__cplusplus	# Enable updated __cplusplus macro
        /Zc:externConstexpr	# Enable extern constexpr variables
        /Zc:preprocessor	# Enable preprocessor conformance mode
        #/Zc:throwingNew # Assume operator new throws on failure

        "$<$<CONFIG:DEBUG>:>"
        "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob2>"
      )

      target_link_options(${target} PRIVATE
        #/WX	# Treat Linker Warnings as Errors

        "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
        "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
      )
    endif()
  endforeach()
endfunction()
