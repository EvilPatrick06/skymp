include(${CMAKE_SOURCE_DIR}/cmake/yarn.cmake)

file(GLOB_RECURSE sources ${CMAKE_CURRENT_LIST_DIR}/*.ts)

set(out
  ${CMAKE_BINARY_DIR}/dist/server/dist_back/skymp5-server.js
)

# The bundle is more than the .ts files: esbuild --bundle carries every package
# out of node_modules, which yarn.lock pins and package.json names, and tsc
# reads tsconfig.json. With only the .ts files as inputs, a package or lockfile
# change (an axios or ws bump) left the old bundle in place as current, and the
# server kept running the old packages (Thornswood #1236). So those three
# files are inputs beside the .ts files, and on Windows the packages are
# installed from the lockfile before the bundle is made: the configure's yarn
# install runs once, and a lockfile that moved since then would bundle the
# packages it had. The other branch already installs first (its plain yarn).
#
# On Windows yarn is yarn.cmd, a batch file, and the Visual Studio generator
# writes the commands below as lines of one batch script. A batch file run
# from a batch script without "call" never comes back, so the build-ts line
# after the install would never run and the old bundle would stay. CMake puts
# "call" in front of a command whose name ends in .cmd, so it is named so.
set(inputs
  ${sources}
  ${SKYMP5_SERVER_SOURCE_DIR}/package.json
  ${SKYMP5_SERVER_SOURCE_DIR}/yarn.lock
  ${SKYMP5_SERVER_SOURCE_DIR}/tsconfig.json
)

if(WIN32)
  add_custom_command(
    OUTPUT ${out}
    COMMAND yarn.cmd --cwd "\"${SKYMP5_SERVER_SOURCE_DIR}\"" install --frozen-lockfile --non-interactive
    COMMAND yarn.cmd --cwd "\"${SKYMP5_SERVER_SOURCE_DIR}\"" build-ts
    DEPENDS ${inputs}
  )
else()
  add_custom_command(
    OUTPUT ${out}
    COMMAND yarn
    COMMAND yarn build-ts
    WORKING_DIRECTORY ${SKYMP5_SERVER_SOURCE_DIR}
    DEPENDS ${inputs}
  )
endif()

add_custom_target(skymp5-server-ts ALL
  DEPENDS ${out}
  SOURCES ${sources}
)
