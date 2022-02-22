find_package(Catch2 REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

add_executable(test_elf_parse test_elf_parse.cpp)
target_link_libraries(test_elf_parse PRIVATE Catch2::Catch2WithMain esp_mkbin)
catch_discover_tests(test_elf_parse WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/elf)

add_test(NAME [[  run mkbin for test setup]]
         COMMAND ${CMAKE_COMMAND} -DELF_DIR=${CMAKE_CURRENT_SOURCE_DIR}/elf -DESP_MKBIN_DIR=${CMAKE_BINARY_DIR}/src
                 -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR} -P run_mkbin.cmake)
add_executable(test_mkbin test_mkbin.cpp)
target_link_libraries(test_mkbin PRIVATE Catch2::Catch2WithMain esp_mkbin)
add_test(NAME [[  mkbin generate valid esp32 image file]] COMMAND test_mkbin WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties([[  run mkbin for test setup]] PROPERTIES FIXTURES_SETUP mkbin)
set_tests_properties([[  mkbin generate valid esp32 image file]] PROPERTIES FIXTURES_REQUIRED mkbin)
