cmake_minimum_required(VERSION 3.21)

project(
  NoisiaX
  VERSION 0.0.1
  DESCRIPTION "A modern C++ library project"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(noisiax)
add_library(NoisiaX::noisiax ALIAS noisiax)

target_sources(noisiax
  PRIVATE
    src/noisiax.cpp
)

target_include_directories(noisiax
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(noisiax PUBLIC cxx_std_20)

set_target_properties(noisiax PROPERTIES
  OUTPUT_NAME noisiax
  EXPORT_NAME noisiax
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
  TARGETS noisiax
  EXPORT NoisiaXTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NoisiaXConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  cmake/NoisiaXConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/NoisiaXConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NoisiaX"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NoisiaXConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NoisiaXConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NoisiaX"
)

install(
  EXPORT NoisiaXTargets
  FILE NoisiaXTargets.cmake
  NAMESPACE NoisiaX::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NoisiaX"
)

if(BUILD_TESTING)
  add_executable(noisiax_tests
    tests/test_noisiax.cpp
  )

  target_link_libraries(noisiax_tests PRIVATE NoisiaX::noisiax)
  target_compile_features(noisiax_tests PRIVATE cxx_std_20)

  add_test(NAME noisiax_tests COMMAND noisiax_tests)
endif()
