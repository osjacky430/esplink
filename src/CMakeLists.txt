add_library(esp_link INTERFACE)
target_include_directories(esp_link INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(esp_link INTERFACE spdlog::spdlog fmt::fmt range-v3::range-v3 project_options project_warnings)
target_compile_options(esp_link INTERFACE -B${CMAKE_LINKER})

add_executable(esp-flash esp_flash.cpp)
target_link_libraries(esp-flash PUBLIC Boost::program_options Boost::system esp_link)

install(TARGETS esp-flash)

add_executable(esp-mkbin esp_mkbin.cpp)
target_link_libraries(esp-mkbin PRIVATE Boost::program_options esp_link)

install(TARGETS esp-mkbin)

add_executable(esp-monitor esp_monitor.cpp)
target_link_libraries(esp-monitor PRIVATE Boost::program_options esp_link)
