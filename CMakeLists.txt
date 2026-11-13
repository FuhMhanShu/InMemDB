cmake_minimum_required(VERSION 3.20)
project(InMemDB VERSION 0.13 LANGUAGES CXX)

add_library(memdb STATIC
    src/value.cpp
    src/document_id.cpp
    src/document.cpp
    src/query.cpp
    src/json.cpp
    src/crypto.cpp
    src/schema.cpp
    src/import.cpp
    src/collection.cpp
    src/transaction.cpp
    src/database.cpp
    src/references.cpp
)
target_include_directories(memdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(memdb PUBLIC cxx_std_17)
set_target_properties(memdb PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
if(NOT MSVC)
    target_compile_options(memdb PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(INMEMDB_BUILD_DOCS "Doxygen-API-Dokumentation bauen (NFA-08)" OFF)
if(INMEMDB_BUILD_DOCS)
    find_package(Doxygen REQUIRED) # dot/graphviz optional; Doku ohne Diagramme (HAVE_DOT=NO)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generiere API-Dokumentation (Doxygen) nach ${CMAKE_CURRENT_BINARY_DIR}/docs/html"
        VERBATIM)
endif()

option(INMEMDB_WITH_SQLITE "SQLite-Import (SqliteImportSource + Tool + Tests) aktivieren" OFF)
if(INMEMDB_WITH_SQLITE)
    find_package(SQLite3 REQUIRED)
    target_sources(memdb PRIVATE src/import_sqlite.cpp)
    target_link_libraries(memdb PRIVATE SQLite::SQLite3)
    target_compile_definitions(memdb PUBLIC INMEMDB_HAS_SQLITE)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()
    add_executable(memdb_tests tests/main.cpp)
    target_link_libraries(memdb_tests PRIVATE memdb)
    target_include_directories(memdb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(NOT MSVC)
        target_compile_options(memdb_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME memdb_tests COMMAND memdb_tests)

    add_executable(memdb_bench bench/bench.cpp)
    target_link_libraries(memdb_bench PRIVATE memdb)

    if(INMEMDB_WITH_SQLITE)
        add_executable(sqlite_import tools/sqlite_import.cpp)
        target_link_libraries(sqlite_import PRIVATE memdb)
        add_executable(memdb_tests_import tests/test_import.cpp)
        target_link_libraries(memdb_tests_import PRIVATE memdb SQLite::SQLite3)
        target_include_directories(memdb_tests_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME memdb_tests_import COMMAND memdb_tests_import)
    endif()

    file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
    foreach(example_src ${EXAMPLE_SOURCES})
        get_filename_component(example_name ${example_src} NAME_WE)
        add_executable(${example_name} ${example_src})
        target_link_libraries(${example_name} PRIVATE memdb)
        if(NOT MSVC)
            target_compile_options(${example_name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME ${example_name} COMMAND ${example_name})
    endforeach()
endif()
