#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "NoisiaX::noisiax" for configuration "Debug"
set_property(TARGET NoisiaX::noisiax APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(NoisiaX::noisiax PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libnoisiax.a"
  )

list(APPEND _cmake_import_check_targets NoisiaX::noisiax )
list(APPEND _cmake_import_check_files_for_NoisiaX::noisiax "${_IMPORT_PREFIX}/lib/libnoisiax.a" )

# Import target "NoisiaX::noisiax_cli" for configuration "Debug"
set_property(TARGET NoisiaX::noisiax_cli APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(NoisiaX::noisiax_cli PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/noisiax"
  )

list(APPEND _cmake_import_check_targets NoisiaX::noisiax_cli )
list(APPEND _cmake_import_check_files_for_NoisiaX::noisiax_cli "${_IMPORT_PREFIX}/bin/noisiax" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
