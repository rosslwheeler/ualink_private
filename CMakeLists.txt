cmake_minimum_required(VERSION 3.20)

project(ualink_model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

add_library(ualink_model STATIC
  src/dl_flit.cpp
  src/crc.cpp
  src/dl_replay.cpp
  src/dl_pacing.cpp
  src/dl_error_injection.cpp
  src/tl_flit.cpp
)

target_include_directories(ualink_model
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

target_compile_features(ualink_model PUBLIC cxx_std_20)

# Tracy integration - always include headers, optionally enable profiling
set(TRACY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third-party/tracy)
if (EXISTS ${TRACY_ROOT}/public/tracy/Tracy.hpp)
    target_include_directories(ualink_model PUBLIC ${TRACY_ROOT}/public ${TRACY_ROOT}/public/tracy)
    if (UALINK_ENABLE_TRACY)
        add_subdirectory(${TRACY_ROOT} third-party/tracy_build)
        target_link_libraries(ualink_model PUBLIC Tracy::TracyClient)
        target_compile_definitions(ualink_model PUBLIC TRACY_ENABLE)
    endif()
else()
    message(WARNING "Tracy submodule not found at third-party/tracy. Run 'git submodule update --init --recursive'")
endif()

enable_testing()

add_executable(ualink_dl_flit_test
  tests/dl_flit_test.cpp
)

target_link_libraries(ualink_dl_flit_test PRIVATE ualink_model)

target_include_directories(ualink_dl_flit_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_dl_flit_test COMMAND ualink_dl_flit_test)

add_executable(ualink_crc_test
  tests/crc_test.cpp
)

target_link_libraries(ualink_crc_test PRIVATE ualink_model)

target_include_directories(ualink_crc_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_crc_test COMMAND ualink_crc_test)

add_executable(ualink_dl_replay_test
  tests/dl_replay_test.cpp
)

target_link_libraries(ualink_dl_replay_test PRIVATE ualink_model)

target_include_directories(ualink_dl_replay_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_dl_replay_test COMMAND ualink_dl_replay_test)

add_executable(ualink_dl_pacing_test
  tests/dl_pacing_test.cpp
)

target_link_libraries(ualink_dl_pacing_test PRIVATE ualink_model)

target_include_directories(ualink_dl_pacing_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_dl_pacing_test COMMAND ualink_dl_pacing_test)

add_executable(ualink_dl_error_injection_test
  tests/dl_error_injection_test.cpp
)

target_link_libraries(ualink_dl_error_injection_test PRIVATE ualink_model)

target_include_directories(ualink_dl_error_injection_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_dl_error_injection_test COMMAND ualink_dl_error_injection_test)

add_executable(ualink_tl_flit_test
  tests/tl_flit_test.cpp
)

target_link_libraries(ualink_tl_flit_test PRIVATE ualink_model)

target_include_directories(ualink_tl_flit_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/ross/OSS/ai/bit_fields_private/include
)

add_test(NAME ualink_tl_flit_test COMMAND ualink_tl_flit_test)
