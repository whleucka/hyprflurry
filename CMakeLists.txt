cmake_minimum_required(VERSION 3.20)
project(hyprflurry VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
  wayland-client wayland-egl egl glesv2 hyprlang>=0.6.0)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTO_OUT ${CMAKE_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PROTO_OUT})
set(PROTO_SOURCES)

# generate(<xml> <basename> <want-header>)
function(wl_protocol xml base header)
  add_custom_command(
    OUTPUT ${PROTO_OUT}/${base}-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${PROTO_OUT}/${base}-protocol.c
    DEPENDS ${xml})
  set(out ${PROTO_OUT}/${base}-protocol.c)
  if(header)
    add_custom_command(
      OUTPUT ${PROTO_OUT}/${base}-client-protocol.h
      COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${PROTO_OUT}/${base}-client-protocol.h
      DEPENDS ${xml})
    list(APPEND out ${PROTO_OUT}/${base}-client-protocol.h)
  endif()
  set(PROTO_SOURCES ${PROTO_SOURCES} ${out} PARENT_SCOPE)
endfunction()

wl_protocol(${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1 ON)
# layer-shell references xdg_popup, so its interface must be linked in
wl_protocol(${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell OFF)

add_executable(hyprflurry
  src/main.cpp src/App.cpp src/Config.cpp src/Renderer.cpp src/Snow.cpp
  src/Json.cpp src/HyprIPC.cpp
  ${PROTO_SOURCES})
target_include_directories(hyprflurry PRIVATE ${PROTO_OUT})
target_link_libraries(hyprflurry PRIVATE PkgConfig::deps)
target_compile_options(hyprflurry PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)

install(TARGETS hyprflurry)
install(FILES hyprflurry.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/hyprflurry)
