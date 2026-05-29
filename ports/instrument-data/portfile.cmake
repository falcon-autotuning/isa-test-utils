vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-data
    REF v${VERSION}
    SHA512 c45ee8fca2e3da28b9d27f54ec27f18498302e4256cde7093709e73a5a78f01e83a9ebef68a6f80d2792277e178018e73ab01d445b6d65755f4b0db36132b17f
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
