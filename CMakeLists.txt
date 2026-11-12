cmake_minimum_required(VERSION 3.20)
project(InMemDB VERSION 0.2 LANGUAGES CXX)

add_library(memdb STATIC
    src/value.cpp
    src/document_id.cpp
    src/document.cpp
    src/query.cpp
    src/collection.cpp
    src/database.cpp
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

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()
    add_executable(memdb_tests tests/main.cpp)
    target_link_libraries(memdb_tests PRIVATE memdb)
    if(NOT MSVC)
        target_compile_options(memdb_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME memdb_tests COMMAND memdb_tests)
endif()
