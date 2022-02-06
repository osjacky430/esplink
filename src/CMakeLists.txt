add_executable(esp-flash esp_flash.cpp)
target_include_directories(esp-flash PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(esp-flash PUBLIC Boost::program_options Boost::system spdlog::spdlog project_options project_warnings range-v3::range-v3)
target_compile_options(esp-flash PUBLIC -B${CMAKE_LINKER})

install(TARGETS esp-flash)
