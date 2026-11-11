# FM-9 : emulateur materiel de la FM-1 (Rust : fm1hw/ + third_party/fm1-emu, licence MIT), lie en bibliotheque statique.
# Fournit la cible "fm1hw" a lier au plugin. macOS : binaire universel (arm64 + x86_64, lipo).
find_program(CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
set(FM1HW_DIR "${CMAKE_CURRENT_LIST_DIR}/../fm1hw")
set(FM1HW_TARGET_DIR "${CMAKE_BINARY_DIR}/fm1hw-target")
file(GLOB_RECURSE FM1HW_SOURCES CONFIGURE_DEPENDS "${FM1HW_DIR}/src/*.rs" "${FM1HW_DIR}/Cargo.toml"
     "${CMAKE_CURRENT_LIST_DIR}/../third_party/fm1-emu/crates/*.rs" "${CMAKE_CURRENT_LIST_DIR}/../third_party/fm1-emu/crates/*.toml")

if(APPLE)
  set(FM1HW_LIB "${FM1HW_TARGET_DIR}/universal/libfm1hw.a")
  set(_libs "")
  set(_cmds "")
  foreach(_t aarch64-apple-darwin x86_64-apple-darwin)
    list(APPEND _libs "${FM1HW_TARGET_DIR}/${_t}/release/libfm1hw.a")
    list(APPEND _cmds COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=10.13
         ${CARGO} build --release --lib --target ${_t} --manifest-path "${FM1HW_DIR}/Cargo.toml" --target-dir "${FM1HW_TARGET_DIR}")
  endforeach()
  add_custom_command(OUTPUT "${FM1HW_LIB}"
    ${_cmds}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FM1HW_TARGET_DIR}/universal"
    COMMAND lipo -create ${_libs} -output "${FM1HW_LIB}"
    DEPENDS ${FM1HW_SOURCES} COMMENT "fm1hw (Rust, universel)" VERBATIM)
elseif(WIN32)
  set(FM1HW_LIB "${FM1HW_TARGET_DIR}/release/fm1hw.lib")
  add_custom_command(OUTPUT "${FM1HW_LIB}"
    COMMAND ${CARGO} build --release --lib --manifest-path "${FM1HW_DIR}/Cargo.toml" --target-dir "${FM1HW_TARGET_DIR}"
    DEPENDS ${FM1HW_SOURCES} COMMENT "fm1hw (Rust)" VERBATIM)
else()
  set(FM1HW_LIB "${FM1HW_TARGET_DIR}/release/libfm1hw.a")
  add_custom_command(OUTPUT "${FM1HW_LIB}"
    COMMAND ${CARGO} build --release --lib --manifest-path "${FM1HW_DIR}/Cargo.toml" --target-dir "${FM1HW_TARGET_DIR}"
    DEPENDS ${FM1HW_SOURCES} COMMENT "fm1hw (Rust)" VERBATIM)
endif()

add_custom_target(fm1hw_build DEPENDS "${FM1HW_LIB}")
add_library(fm1hw STATIC IMPORTED GLOBAL)
set_target_properties(fm1hw PROPERTIES IMPORTED_LOCATION "${FM1HW_LIB}")
add_dependencies(fm1hw fm1hw_build)
if(WIN32)
  set_property(TARGET fm1hw PROPERTY INTERFACE_LINK_LIBRARIES ws2_32 userenv bcrypt ntdll advapi32 kernel32)
elseif(NOT APPLE)
  set_property(TARGET fm1hw PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
endif()
