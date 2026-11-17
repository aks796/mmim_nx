# deko3d 0.5.0 (devkitPro, zlib licence) built from source for the fixes in PATCHES.md. Same flags as
# upstream's release libdeko3d.a. generated/ holds what dekotools writes from source/maxwell/*.def/.mme.
cmake_minimum_required(VERSION 3.16)
project(deko3d CXX)
file(GLOB DEKO3D_SOURCES CONFIGURE_DEPENDS source/*.cpp source/maxwell/*.cpp)
# deko3d: release (errors abort silently); deko3dd: every call checked and errors
# reported through the device's debug callback first (upstream's libdeko3dd).
foreach(variant deko3d deko3dd)
  add_library(${variant} STATIC ${DEKO3D_SOURCES})
  target_include_directories(${variant} PUBLIC include)
  target_include_directories(${variant} PRIVATE generated source source/maxwell)
  target_compile_definitions(${variant} PRIVATE __DK_INTERNAL__)
  target_compile_options(${variant} PRIVATE -O2 -fno-rtti -fno-exceptions -Wall -std=gnu++17)
endforeach()
target_compile_definitions(deko3d PRIVATE NDEBUG=1)
target_compile_definitions(deko3dd PRIVATE DEBUG=1)
