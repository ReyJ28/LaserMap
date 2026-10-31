# Builds the LaserMap FFGL plugin against Resolume's open-source FFGL SDK (BSD-style licence).
# The SDK is NOT vendored here; point FFGL_SDK_DIR at a checkout:
#   git clone https://github.com/resolume/ffgl <somewhere>
#   cmake -S . -B build -DLASERMAP_BUILD_FFGL=ON -DFFGL_SDK_DIR=<somewhere>
# Needs GLEW on Windows/Linux (the SDK's CMake find_package()s it; vcpkg works). macOS uses system GL.
set(FFGL_SDK_DIR "" CACHE PATH "Path to a checkout of github.com/resolume/ffgl")
if(NOT EXISTS "${FFGL_SDK_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set FFGL_SDK_DIR to a checkout of https://github.com/resolume/ffgl")
endif()

add_subdirectory("${FFGL_SDK_DIR}" ffgl-sdk EXCLUDE_FROM_ALL)

add_library(LaserMap MODULE LaserMapPlugin.h LaserMapPlugin.cpp)
target_link_libraries(LaserMap PRIVATE ffgl::sdk lasermap_core)
if(NOT APPLE)
  # The SDK headers include glew.h; link GLEW here too so its compile definitions (e.g. GLEW_STATIC) apply.
  find_package(GLEW REQUIRED)
  target_link_libraries(LaserMap PRIVATE GLEW::GLEW)
endif()
target_compile_features(LaserMap PRIVATE cxx_std_17)
set_target_properties(LaserMap PROPERTIES PREFIX "")

if(APPLE)
  # Resolume loads FFGL on macOS from a .bundle.
  set_target_properties(LaserMap PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "bundle")
endif()
