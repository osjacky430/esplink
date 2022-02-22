add_executable(esp-flash esp_flash.cpp)
target_include_directories(esp-flash PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(esp-flash PUBLIC Boost::program_options Boost::system spdlog::spdlog project_options project_warnings
                                       range-v3::range-v3)
target_compile_options(esp-flash PUBLIC -B${CMAKE_LINKER} "-v")

install(TARGETS esp-flash)

add_library(esp_mkbin INTERFACE)
target_include_directories(esp_mkbin INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(esp_mkbin INTERFACE spdlog::spdlog fmt::fmt range-v3::range-v3 project_options project_warnings)

add_executable(esp-mkbin esp_mkbin.cpp)
target_link_libraries(esp-mkbin PUBLIC Boost::program_options esp_mkbin)

install(TARGETS esp-mkbin)
