# Rust is linked only into this board's LVGL build. Other boards keep their
# existing toolchain and never need cargo/esp installed.
find_program(DSH_CARGO cargo HINTS "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
set(DSH_RUSTUP_HOME "$ENV{RUSTUP_HOME}" CACHE PATH "Optional dedicated esp Rust toolchain home")
if(NOT DSH_RUSTUP_HOME AND EXISTS "${PROJECT_DIR}/../tools/rust/rustup")
    set(DSH_RUSTUP_HOME "${PROJECT_DIR}/../tools/rust/rustup")
endif()
# Windows host build scripts run through MSVC before cross compilation. Keep
# temporary linker files inside the build tree, where access is predictable.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/rust-tmp")
set(DSH_RUST_ENV "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/rust-animation"
    "TMP=${CMAKE_BINARY_DIR}/rust-tmp" "TEMP=${CMAKE_BINARY_DIR}/rust-tmp")
if(DSH_RUSTUP_HOME)
    list(APPEND DSH_RUST_ENV "RUSTUP_HOME=${DSH_RUSTUP_HOME}")
endif()
set(DSH_CARGO_HOME "$ENV{CARGO_HOME}" CACHE PATH "Optional cargo cache directory")
if(DSH_CARGO_HOME)
    list(APPEND DSH_RUST_ENV "CARGO_HOME=${DSH_CARGO_HOME}")
endif()
set(DSH_RUST_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(DSH_RUST_ARCHIVE "${CMAKE_BINARY_DIR}/rust-animation/xtensa-esp32s3-none-elf/release/libdsh_animation.a")
file(GLOB DSH_RUST_SOURCES CONFIGURE_DEPENDS "${DSH_RUST_DIR}/src/*.rs")
add_custom_command(OUTPUT "${DSH_RUST_ARCHIVE}"
    COMMAND ${CMAKE_COMMAND} -E env ${DSH_RUST_ENV} "${DSH_CARGO}" +esp build
        --manifest-path "${DSH_RUST_DIR}/Cargo.toml" --release
        --target xtensa-esp32s3-none-elf -Zbuild-std=core
    DEPENDS ${DSH_RUST_SOURCES} "${DSH_RUST_DIR}/Cargo.toml" "${DSH_RUST_DIR}/Cargo.lock"
    COMMENT "Building modular DSH Rust animation runtime"
    VERBATIM)
add_custom_target(dsh_rust_build DEPENDS "${DSH_RUST_ARCHIVE}")
add_library(dsh_rust_animation STATIC IMPORTED)
set_target_properties(dsh_rust_animation PROPERTIES IMPORTED_LOCATION "${DSH_RUST_ARCHIVE}")
add_dependencies(dsh_rust_animation dsh_rust_build)
target_link_libraries(${COMPONENT_LIB} PRIVATE dsh_rust_animation)
