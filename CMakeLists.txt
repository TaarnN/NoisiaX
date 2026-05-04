cmake_minimum_required(VERSION 3.21)

project(
  NoisiaX
  VERSION 1.0.0
  DESCRIPTION "Deterministic scenario-first simulation core"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(noisiax)
add_library(NoisiaX::noisiax ALIAS noisiax)

target_sources(noisiax
  PRIVATE
    src/noisiax.cpp
    src/engine/simulation_engine.cpp
    src/engine/agent_runtime.cpp
    src/serialization/yaml_serializer.cpp
    src/validation/scenario_validator.cpp
    src/compiler/scenario_compiler.cpp
    src/scheduler/event_scheduler.cpp
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

find_package(yaml-cpp QUIET CONFIG)
if(NOT TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
  message(STATUS "yaml-cpp package not found. Fetching yaml-cpp from source...")

  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
  set(YAML_CPP_INSTALL ON CACHE BOOL "Enable yaml-cpp install target" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
endif()

if(TARGET yaml-cpp::yaml-cpp)
  set(NOISIAX_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
  set(NOISIAX_YAML_CPP_TARGET yaml-cpp)
else()
  message(FATAL_ERROR "yaml-cpp target could not be resolved")
endif()

target_link_libraries(noisiax PRIVATE ${NOISIAX_YAML_CPP_TARGET})

add_executable(noisiax_cli
  src/noisiax_cli.cpp
)
target_link_libraries(noisiax_cli PRIVATE NoisiaX::noisiax)
target_compile_features(noisiax_cli PRIVATE cxx_std_20)
set_target_properties(noisiax_cli PROPERTIES OUTPUT_NAME noisiax)

install(
  TARGETS noisiax noisiax_cli
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
  add_test(
    NAME noisiax_cli_validate
    COMMAND noisiax_cli validate ${CMAKE_CURRENT_BINARY_DIR}/scenarios/happy_path.yaml
  )
  add_test(
    NAME noisiax_cli_compile
    COMMAND noisiax_cli compile ${CMAKE_CURRENT_BINARY_DIR}/scenarios/happy_path.yaml
  )
  add_test(
    NAME noisiax_cli_run
    COMMAND noisiax_cli run ${CMAKE_CURRENT_BINARY_DIR}/scenarios/happy_path.yaml
  )
endif()

# Scenario fixtures for testing
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/scenarios DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
