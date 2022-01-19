add_executable(esputil esputil.cpp)
target_include_directories(esputil PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(esputil PUBLIC Boost::program_options Boost::system spdlog::spdlog project_options project_warnings)
target_compile_options(esputil PUBLIC -B${CMAKE_LINKER})

install(TARGETS esputil)
