#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "memdb::memdb" for configuration "Release"
set_property(TARGET memdb::memdb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(memdb::memdb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libmemdb.a"
  )

list(APPEND _cmake_import_check_targets memdb::memdb )
list(APPEND _cmake_import_check_files_for_memdb::memdb "${_IMPORT_PREFIX}/lib/libmemdb.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
