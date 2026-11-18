cmake_minimum_required(VERSION 3.20)
project(InMemDB VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(memdb STATIC
    src/value.cpp
    src/document_id.cpp
    src/document.cpp
    src/query.cpp
    src/json.cpp
    src/crypto.cpp
    src/schema.cpp
    src/import.cpp
    src/import_bson.cpp
    src/import_csv.cpp
    src/import_json.cpp
    src/import_snapshot.cpp
    src/import_sqldump.cpp
    src/query_dsl.cpp
    src/regex.cpp
    src/db_graph.cpp
    src/discovery.cpp
    src/character.cpp
    src/numeric_analysis.cpp
    src/mixed_analysis.cpp
    src/complex_analysis.cpp
    src/influence.cpp
    src/cross_influence.cpp
    src/collection.cpp
    src/transaction.cpp
    src/database.cpp
    src/references.cpp
    src/message_handler.cpp
)
target_include_directories(memdb PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
add_library(memdb::memdb ALIAS memdb)
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

install(TARGETS memdb
    EXPORT InMemDBTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
install(EXPORT InMemDBTargets
    NAMESPACE memdb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InMemDB
)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/InMemDBConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/InMemDBConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InMemDB
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/InMemDBConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/InMemDBConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/InMemDBConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InMemDB
)

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

    add_executable(inmemdb_import tools/import_tool.cpp)
    target_link_libraries(inmemdb_import PRIVATE memdb)

    add_executable(inmemdb_graph tools/db_graph.cpp)
    target_link_libraries(inmemdb_graph PRIVATE memdb)

    add_executable(inmemdb_discover tools/discover_tool.cpp)
    target_link_libraries(inmemdb_discover PRIVATE memdb)

    add_executable(memdb_tests_db_graph tests/test_db_graph.cpp)
    target_link_libraries(memdb_tests_db_graph PRIVATE memdb)
    add_test(NAME memdb_tests_db_graph COMMAND memdb_tests_db_graph)

    add_executable(memdb_tests_ref_join tests/test_ref_join.cpp)
    target_link_libraries(memdb_tests_ref_join PRIVATE memdb)
    add_test(NAME memdb_tests_ref_join COMMAND memdb_tests_ref_join)

    add_executable(memdb_tests_hybrid tests/test_hybrid.cpp)
    target_link_libraries(memdb_tests_hybrid PRIVATE memdb)
    add_test(NAME memdb_tests_hybrid COMMAND memdb_tests_hybrid)

    add_executable(memdb_tests_tx_cow tests/test_tx_cow.cpp)
    target_link_libraries(memdb_tests_tx_cow PRIVATE memdb)
    add_test(NAME memdb_tests_tx_cow COMMAND memdb_tests_tx_cow)

    add_executable(memdb_tests_regex tests/test_regex.cpp)
    target_link_libraries(memdb_tests_regex PRIVATE memdb)
    target_include_directories(memdb_tests_regex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME memdb_tests_regex COMMAND memdb_tests_regex)
    add_executable(memdb_tests_error_handling tests/test_error_handling.cpp)
    target_link_libraries(memdb_tests_error_handling PRIVATE memdb)
    add_test(NAME memdb_tests_error_handling COMMAND memdb_tests_error_handling)
    add_executable(memdb_tests_message_handler tests/test_message_handler.cpp)
    target_link_libraries(memdb_tests_message_handler PRIVATE memdb)
    add_test(NAME memdb_tests_message_handler COMMAND memdb_tests_message_handler)
    add_executable(memdb_tests_json_hardening tests/test_json_hardening.cpp)
    target_link_libraries(memdb_tests_json_hardening PRIVATE memdb)
    target_include_directories(memdb_tests_json_hardening PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME memdb_tests_json_hardening COMMAND memdb_tests_json_hardening)
    add_executable(memdb_tests_hardening tests/test_hardening.cpp)
    target_link_libraries(memdb_tests_hardening PRIVATE memdb)
    add_test(NAME memdb_tests_hardening COMMAND memdb_tests_hardening)
    add_executable(memdb_tests_limits tests/test_limits.cpp)
    target_link_libraries(memdb_tests_limits PRIVATE memdb)
    add_test(NAME memdb_tests_limits COMMAND memdb_tests_limits)
    add_executable(memdb_tests_discovery6 tests/test_discovery6.cpp)
    target_link_libraries(memdb_tests_discovery6 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery6 COMMAND memdb_tests_discovery6)
    add_executable(memdb_tests_discovery7 tests/test_discovery7.cpp)
    target_link_libraries(memdb_tests_discovery7 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery7 COMMAND memdb_tests_discovery7)
    add_executable(memdb_tests_nan_ordering tests/test_nan_ordering.cpp)
    target_link_libraries(memdb_tests_nan_ordering PRIVATE memdb)
    add_test(NAME memdb_tests_nan_ordering COMMAND memdb_tests_nan_ordering)
    add_executable(memdb_tests_crypto_kdf tests/test_crypto_kdf.cpp)
    target_link_libraries(memdb_tests_crypto_kdf PRIVATE memdb)
    target_include_directories(memdb_tests_crypto_kdf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME memdb_tests_crypto_kdf COMMAND memdb_tests_crypto_kdf)
    add_executable(memdb_tests_concurrency tests/test_concurrency.cpp)
    target_link_libraries(memdb_tests_concurrency PRIVATE memdb)
    add_test(NAME memdb_tests_concurrency COMMAND memdb_tests_concurrency)

    add_executable(memdb_tests_dsl_update tests/test_dsl_update.cpp)
    target_link_libraries(memdb_tests_dsl_update PRIVATE memdb)
    add_test(NAME memdb_tests_dsl_update COMMAND memdb_tests_dsl_update)

    add_executable(memdb_tests_metrics tests/test_metrics.cpp)
    target_link_libraries(memdb_tests_metrics PRIVATE memdb)
    add_test(NAME memdb_tests_metrics COMMAND memdb_tests_metrics)

    add_executable(memdb_tests_import_csv tests/test_import_csv.cpp)
    target_link_libraries(memdb_tests_import_csv PRIVATE memdb)
    add_test(NAME memdb_tests_import_csv COMMAND memdb_tests_import_csv)

    add_executable(memdb_tests_import_formats tests/test_import_formats.cpp)
    target_link_libraries(memdb_tests_import_formats PRIVATE memdb)
    add_test(NAME memdb_tests_import_formats COMMAND memdb_tests_import_formats)

    add_executable(memdb_tests_query_dsl tests/test_query_dsl.cpp)
    target_link_libraries(memdb_tests_query_dsl PRIVATE memdb)
    add_test(NAME memdb_tests_query_dsl COMMAND memdb_tests_query_dsl)

    add_executable(memdb_tests_locale tests/test_locale.cpp)
    target_link_libraries(memdb_tests_locale PRIVATE memdb)
    add_test(NAME memdb_tests_locale COMMAND memdb_tests_locale)

    add_executable(memdb_tests_merge tests/test_merge.cpp)
    target_link_libraries(memdb_tests_merge PRIVATE memdb)
    add_test(NAME memdb_tests_merge COMMAND memdb_tests_merge)

    add_executable(memdb_tests_ttl tests/test_ttl.cpp)
    target_link_libraries(memdb_tests_ttl PRIVATE memdb)
    add_test(NAME memdb_tests_ttl COMMAND memdb_tests_ttl)

    add_executable(memdb_tests_foundations tests/test_foundations.cpp)
    target_link_libraries(memdb_tests_foundations PRIVATE memdb)
    add_test(NAME memdb_tests_foundations COMMAND memdb_tests_foundations)

    add_executable(memdb_tests_character tests/test_character.cpp)
    target_link_libraries(memdb_tests_character PRIVATE memdb)
    add_test(NAME memdb_tests_character COMMAND memdb_tests_character)

    add_executable(memdb_tests_numeric_analysis tests/test_numeric_analysis.cpp)
    target_link_libraries(memdb_tests_numeric_analysis PRIVATE memdb)
    add_test(NAME memdb_tests_numeric_analysis COMMAND memdb_tests_numeric_analysis)

    add_executable(memdb_tests_mixed_analysis tests/test_mixed_analysis.cpp)
    target_link_libraries(memdb_tests_mixed_analysis PRIVATE memdb)
    add_test(NAME memdb_tests_mixed_analysis COMMAND memdb_tests_mixed_analysis)

    add_executable(memdb_tests_complex_analysis tests/test_complex_analysis.cpp)
    target_link_libraries(memdb_tests_complex_analysis PRIVATE memdb)
    add_test(NAME memdb_tests_complex_analysis COMMAND memdb_tests_complex_analysis)

    add_executable(memdb_tests_influence tests/test_influence.cpp)
    target_link_libraries(memdb_tests_influence PRIVATE memdb)
    add_test(NAME memdb_tests_influence COMMAND memdb_tests_influence)

    add_executable(memdb_tests_cross_influence tests/test_cross_influence.cpp)
    target_link_libraries(memdb_tests_cross_influence PRIVATE memdb)
    add_test(NAME memdb_tests_cross_influence COMMAND memdb_tests_cross_influence)

    add_executable(memdb_tests_import_fk tests/test_import_fk.cpp)
    target_link_libraries(memdb_tests_import_fk PRIVATE memdb)
    add_test(NAME memdb_tests_import_fk COMMAND memdb_tests_import_fk)

    add_executable(memdb_tests_discovery tests/test_discovery.cpp)
    target_link_libraries(memdb_tests_discovery PRIVATE memdb)
    add_test(NAME memdb_tests_discovery COMMAND memdb_tests_discovery)

    add_executable(memdb_tests_discovery2 tests/test_discovery2.cpp)
    target_link_libraries(memdb_tests_discovery2 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery2 COMMAND memdb_tests_discovery2)

    add_executable(memdb_tests_discovery3 tests/test_discovery3.cpp)
    target_link_libraries(memdb_tests_discovery3 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery3 COMMAND memdb_tests_discovery3)

    add_executable(memdb_tests_discovery4 tests/test_discovery4.cpp)
    target_link_libraries(memdb_tests_discovery4 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery4 COMMAND memdb_tests_discovery4)

    add_executable(memdb_tests_discovery5 tests/test_discovery5.cpp)
    target_link_libraries(memdb_tests_discovery5 PRIVATE memdb)
    add_test(NAME memdb_tests_discovery5 COMMAND memdb_tests_discovery5)

    if(INMEMDB_WITH_SQLITE)
        add_executable(memdb_tests_import tests/test_import.cpp)
        target_link_libraries(memdb_tests_import PRIVATE memdb SQLite::SQLite3)
        target_include_directories(memdb_tests_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME memdb_tests_import COMMAND memdb_tests_import)

        # Vergleichs-Benchmark InMemDB vs SQLite (:memory:), nur mit SQLite-Dev
        add_executable(memdb_bench_sqlite bench/bench_sqlite.cpp)
        target_link_libraries(memdb_bench_sqlite PRIVATE memdb SQLite::SQLite3)
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

    install(TARGETS inmemdb_import inmemdb_graph inmemdb_discover
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
