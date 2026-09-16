# Local v1.0.2 overlay port.
# The upstream commonlibsse-ng-fork port currently pins a GitHub archive SHA512
# that no longer matches the archive GitHub serves. Fetch the exact commits with
# git instead, which keeps the source revision pinned without depending on the
# unstable generated-archive hash.

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/alandtse/CommonLibVR.git
    REF 8593e41c1f780b5b16b227f4f62e574d01e0ac3d
)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH2
    URL https://github.com/ValveSoftware/openvr.git
    REF ebdea152f8aac77e9a6db29682b81d762159df7e
)

file(GLOB OPENVR_FILES "${SOURCE_PATH2}/*")
file(COPY ${OPENVR_FILES} DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
    OPTIONS
        -DBUILD_TESTS=off
        -DSKSE_SUPPORT_XBYAK=on
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(INSTALL "${SOURCE_PATH2}/headers/openvr.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE")

file(
    INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright
)
