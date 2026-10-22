set(FRIDA_URL "https://github.com/frida/frida/releases/download/16.0.19/frida-gum-devkit-16.0.19-windows-x86_64.exe")
set(FRIDA_FILENAME "frida-gum-devkit-16.0.19-windows-x86_64.exe")
set(FRIDA_SHA512 9058c9010b966a07a875e4b7855dab60db6182b3c827c8d723d10b09034c50a418c72793f3e53419ab2e088c343eb2fd637cdb02bb8b64a0a29f68a2ee813343)

vcpkg_download_distfile(ARCHIVE
    URLS ${FRIDA_URL}
    FILENAME ${FRIDA_FILENAME}
    SHA512 ${FRIDA_SHA512}
)

vcpkg_extract_source_archive_ex(
    OUT_SOURCE_PATH SOURCE_PATH
    ARCHIVE ${ARCHIVE}
    REF 16.0.19
    NO_REMOVE_ONE_LEVEL
)

file(INSTALL ${SOURCE_PATH}/frida-gum.h DESTINATION ${CURRENT_PACKAGES_DIR}/include)
file(INSTALL ${SOURCE_PATH}/frida-gum.lib DESTINATION ${CURRENT_PACKAGES_DIR}/lib)
file(INSTALL ${SOURCE_PATH}/frida-gum.lib DESTINATION ${CURRENT_PACKAGES_DIR}/debug/lib)

# frida-gum.lib is not gum alone: the devkit links in the libraries frida's
# releng/deps.mk pins for 16.0.19 (glib with gobject, gnulib and libcharset,
# proxy-libintl, pcre2, capstone, libffi), and llvm-ar t lists their objects
# beside gum's. Code from them ends up in whatever links this port, so the
# copyright carries each one's licence, at the commit frida built it from
# (Thornswood #3099: it once carried gum's alone).
set(FRIDA_LICENCES
    "frida-gum-COPYING|frida/frida-gum/698b356fef0ecfc3ac2818f0b387be90e93deeda/COPYING|c7ae0367bf235f52abfc79bfacafeefba02e8bb8e3b643a2d044fda57bb4bdf9df8528f0799fe7e1280bbedcc7a359186a902ca7d6fa51193a4a8ecde3afd7d1"
    "glib-LGPL-2.1-or-later.txt|frida/glib/f3af6c29d39eb7590425d1b22bae29ff6a0ebb6f/LICENSES/LGPL-2.1-or-later.txt|60f9950e59df8b35af4553e22c0c69c3e4dbe931d59159a1abb06a75a188fac6b0c8a0c8bbfab39eb372e54f6dac32696d39521cd01e03f60f0bd5c1210a45e8"
    "proxy-libintl-COPYING|frida/proxy-libintl/c03e1a74b17fa7ec467e110130775409e4828a4c/COPYING|697df393fbc529f7be805bdf7b18a064ea8f1d85241a1f50cb9a720e8cdec87319f53f62e000bd613113198d5a802e22f0ec7426bbb4562e423da8ba3ffb71e5"
    "pcre2-LICENCE|frida/pcre2/b47486922fdc3486499b310dc9cf903449700474/LICENCE|af550d2b3deda372bd6d0ea78c877bda46f2a597d2960865854e07ffb30ab4ae78c768cf11b4ead72a0d890970192442fd5edb754ee54fcf3e633e9788b3cf6b"
    "capstone-LICENSE.TXT|frida/capstone/22d317042ee4d251280d2960f5cf294433977db4/LICENSE.TXT|b535e719ea9f999e081e2368faa8214dc23b39faee60a31fd10ac5ab8df4d42e577acebb57d3dabcb69a04f1d62e4e1a93ea0e623c09841b0257a1b8ffa79c68"
    "capstone-LICENSE_LLVM.TXT|frida/capstone/22d317042ee4d251280d2960f5cf294433977db4/LICENSE_LLVM.TXT|02b81aca5835cf62cb8ae229b3f9203cc33291a9223e7b9a26b984fdf8ec955a88fb4250db5212dba9d19e6e5f6499e363ea9149ed2e8fc3565b32c11732a3a4"
    "libffi-LICENSE|frida/libffi/938b07aa8280bcc4f92f08a0fdbb4ad81024bde0/LICENSE|f9898316051199765cafa1b768d4362f91cb93bcf334bf170aef95f0e0d7100310712809bb78c112dd140ab8e7abdf3dc66ec1d81663a09081e2e0cf588b259c"
)
set(licence_files "")
file(REMOVE_RECURSE "${CURRENT_BUILDTREES_DIR}/licences")
file(MAKE_DIRECTORY "${CURRENT_BUILDTREES_DIR}/licences")
foreach(entry IN LISTS FRIDA_LICENCES)
    string(REPLACE "|" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 path)
    list(GET entry 2 sha512)
    vcpkg_download_distfile(licence
        URLS "https://raw.githubusercontent.com/${path}"
        FILENAME "frida-gum-16.0.19-${name}"
        SHA512 ${sha512}
    )
    # vcpkg_install_copyright heads each text with its file's name.
    file(COPY_FILE "${licence}" "${CURRENT_BUILDTREES_DIR}/licences/${name}")
    list(APPEND licence_files "${CURRENT_BUILDTREES_DIR}/licences/${name}")
endforeach()
vcpkg_install_copyright(
    COMMENT "frida-gum 16.0.19 (wxWindows Library Licence 3.1) and the libraries its devkit links into frida-gum.lib: glib, gobject, gnulib and libcharset (LGPL-2.1-or-later), proxy-libintl (LGPL-2.0-or-later), pcre2 (BSD-3-Clause with its binary exemption), capstone (BSD-3-Clause, with LLVM's terms for the tables it takes from LLVM), libffi (MIT)."
    FILE_LIST ${licence_files}
)
