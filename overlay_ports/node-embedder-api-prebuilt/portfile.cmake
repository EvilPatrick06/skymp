set(NODE_EMBEDDER_API_URL "https://node-embedder-api-prebuilt-vcpkg-port-v2.b-cdn.net/archive2_1.zip")
set(NODE_EMBEDDER_API_FILENAME "node-embedder-api-prebuilt.zip")
set(NODE_EMBEDDER_API_SHA512 bea6dbf02783e3cafbe8a752f3ce245e85189d3946902aece5a5ff6373cec684a986aa65df279f1c79fc886be380edac0926fc3808930a29a013d7fefeec67da)

vcpkg_download_distfile(ARCHIVE
    URLS ${NODE_EMBEDDER_API_URL}
    FILENAME ${NODE_EMBEDDER_API_FILENAME}
    SHA512 ${NODE_EMBEDDER_API_SHA512}
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE ${ARCHIVE}
    NO_REMOVE_ONE_LEVEL
)

file(GLOB_RECURSE files ${SOURCE_PATH}/*)

foreach(file ${files})
  file(RELATIVE_PATH relative_path ${SOURCE_PATH} ${file})

  set(destination_path ${CURRENT_PACKAGES_DIR}/${relative_path})

  get_filename_component(destination_dir ${destination_path} DIRECTORY)
  file(MAKE_DIRECTORY ${destination_dir})

  message(STATUS "Copying ${file} to ${destination_path}")
  file(COPY ${file} DESTINATION ${destination_dir})
endforeach()

# The archive carries no licence. These are Node's own terms, at the tag of
# the Node it was built from (include/node/node_version.h says 23.2.0), so the
# client files that link it carry Node's text and not another port's
# (Thornswood #3099: this line once fetched frida-gum's COPYING here).
vcpkg_download_distfile(NODE_LICENSE
    URLS https://raw.githubusercontent.com/nodejs/node/a83fbdbde16b7decf5ec41277917abe267746661/LICENSE
    FILENAME node-v23.2.0-LICENSE
    SHA512 96f94d7e41f70d7a5e3ea7dca49865035d9149c6a99c48ad49cef80d33bd351b224cabcad611682daf6497fc924a38da27c5410fac6bd4269138d2d30647a5a0
)
file(INSTALL ${NODE_LICENSE} DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
