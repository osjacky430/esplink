list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

add_executable(test_elf_parse test_elf_parse.cpp)
target_link_libraries(test_elf_parse PRIVATE Catch2::Catch2WithMain esp_link)
catch_discover_tests(test_elf_parse WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/elf)

include(FindPython3)
add_test(NAME [[  run mkbin for test setup]]
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_mkbin.py --mkbin-dir ${CMAKE_BINARY_DIR}/src --arg-dir
                 ${CMAKE_CURRENT_SOURCE_DIR}/elf/run_flags.json --elf-dir ${CMAKE_CURRENT_SOURCE_DIR}/elf/)
add_executable(test_mkbin test_mkbin.cpp)
target_link_libraries(test_mkbin PRIVATE Catch2::Catch2WithMain esp_link)
add_test(NAME [[  mkbin generate valid esp32 image file]] COMMAND test_mkbin WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties([[  run mkbin for test setup]] PROPERTIES FIXTURES_SETUP mkbin)
set_tests_properties([[  mkbin generate valid esp32 image file]] PROPERTIES FIXTURES_REQUIRED mkbin)

add_executable(test_flash test_flash.cpp)
target_link_libraries(test_flash PRIVATE Catch2::Catch2WithMain Boost::system esp_link)
# catch_discover_tests(test_flash)
