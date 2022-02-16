find_package(Catch2 REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

add_executable(mkbin_test mkbin_test.cpp)
target_link_libraries(mkbin_test PRIVATE Catch2::Catch2WithMain)
