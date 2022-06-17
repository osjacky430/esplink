macro (run_conan)
  if (NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/conan.cmake")
    message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/0.18.1/conan.cmake" "${CMAKE_BINARY_DIR}/conan.cmake"
         TLS_VERIFY ON)
  endif ()

  include(${CMAKE_CURRENT_BINARY_DIR}/conan.cmake)

  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT ANDROID)
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)

    set(B2_OPTIONS "b2:toolset=clang") # pretty f-up thing tbh, needing to specify this kind of nonsense
  endif ()

  conan_cmake_configure(REQUIRES spdlog/1.10.0 fmt/8.1.1 boost/1.79.0 range-v3/0.11.0 GENERATORS cmake_find_package)
  conan_cmake_autodetect(settings BUILD_TYPE ${CMAKE_BUILD_TYPE})

  # Forcing compiler and its version to make sure conan builds project dependencies with same compiler
  # Due to https://github.com/conan-io/conan-center-index/pull/3999, we need to pass "boost:without_fiber=True"
  # in order to build successfully
  conan_cmake_install(PATH_OR_REFERENCE . BUILD missing OPTIONS "boost:without_fiber=True" ${B2_OPTIONS} SETTINGS ${settings}
                      ENV "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}")
endmacro ()
