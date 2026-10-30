function(download_skyrim_data DEST_DIR)
    if(NOT EXISTS "${DEST_DIR}")
        file(MAKE_DIRECTORY "${DEST_DIR}")
    endif()

    set(URL_BASE "https://gitlab.com/pospelov/se-data/-/raw/main")
    set(FILES
        "Skyrim.esm"
        "Update.esm"
        "Dawnguard.esm"
        "HearthFires.esm"
        "Dragonborn.esm"
    )

    # THORNSWOOD PATCH (Thornswood #3728). Each file is held to its sha256, so
    # a file changed or cut short at URL_BASE stops the configure instead of
    # being tested against. The values are what se-data serves on 26 Oct 2026
    # (GitLab's X-Gitlab-Content-Sha256 for main, which a download of
    # HearthFires.esm matched), and Thornswood's THIRD-PARTY.md lists them.
    set(SHA256_Skyrim.esm      "9ec6b3818bbd002487bdc4104add07f814cc067e70c79ea2d164108a6c960f04")
    set(SHA256_Update.esm      "8502f3975a84fc4fd62c9781e867df5bb661972887c93f017db0857b20dc90f2")
    set(SHA256_Dawnguard.esm   "501bbcf0c61c8f28396a13a2ca877f904803ef9c0002b329edde7fb741119023")
    set(SHA256_HearthFires.esm "30d319a8e439dbd1c5dec03860a60aaec9142642f2c9168419b424c897f592d1")
    set(SHA256_Dragonborn.esm  "4f2fe839956db8807e07b1a0feb805130487899ce337e60ad43f15ed9f8cb988")

    foreach(FILE_NAME ${FILES})
        set(FILE_PATH "${DEST_DIR}/${FILE_NAME}")
        set(EXPECTED "${SHA256_${FILE_NAME}}")
        if(NOT EXISTS "${FILE_PATH}")
            message(STATUS "Downloading ${FILE_NAME} to ${DEST_DIR}...")
            file(DOWNLOAD "${URL_BASE}/${FILE_NAME}" "${FILE_PATH}"
                EXPECTED_HASH SHA256=${EXPECTED}
                STATUS status
                LOG log
            )
            list(GET status 0 status_code)
            list(GET status 1 status_string)

            if(NOT status_code EQUAL 0)
                message(FATAL_ERROR "error: downloading '${FILE_NAME}' failed
  status_code: ${status_code}
  status_string: ${status_string}
  log: ${log}
")
            endif()
        else()
            # A copy left by an earlier configure is held to the same sha256.
            file(SHA256 "${FILE_PATH}" HAVE)
            if(NOT HAVE STREQUAL EXPECTED)
                message(FATAL_ERROR "error: ${FILE_PATH} is sha256 ${HAVE}, not the ${EXPECTED} this build tests against. Delete it and configure again to download it.")
            endif()
            message(STATUS "${FILE_NAME} already exists in ${DEST_DIR}")
        endif()
    endforeach()
endfunction()
