cmake_minimum_required(VERSION 3.20)
project(arrowlane_c_for_asm LANGUAGES C Fortran)

# C23 and Fortran 2023, on purpose. The header uses [[nodiscard]], constexpr,
# nullptr, fixed underlying enum types, digit separators and bool as a keyword, and
# the Fortran slice uses do concurrent and logical(c_bool). Both need GCC 14.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_Fortran_STANDARD 2023)
set(CMAKE_Fortran_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "C23 needs GCC 14 or later, found ${CMAKE_C_COMPILER_VERSION}")
endif()
if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU" AND CMAKE_Fortran_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "Fortran 2023 needs GCC 14 or later, found ${CMAKE_Fortran_COMPILER_VERSION}")
endif()

option(ALN_ASM_CRC32 "Use the hand written x86-64 checksum kernel" ON)

set(ALN_CORE_SOURCES
  src/crc32.c
  src/base64.c
  src/frame.c
  src/json_fixture.c
  src/fixture_alloc.c
  src/format.c
  src/fixture.f90
)

add_library(aln_core STATIC ${ALN_CORE_SOURCES})
target_include_directories(aln_core PUBLIC include)
target_compile_options(aln_core PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>)

set(ALN_HAVE_ASM 0)
if(ALN_ASM_CRC32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT MSVC)
  enable_language(ASM)
  target_sources(aln_core PRIVATE src/crc32_x86.S)
  set(ALN_HAVE_ASM 1)
endif()
target_compile_definitions(aln_core PRIVATE ALN_HAVE_ASM_CRC32=${ALN_HAVE_ASM})

add_executable(arrowlane-c-for-asm src/main.c)
target_link_libraries(arrowlane-c-for-asm PRIVATE aln_core m)
target_compile_definitions(arrowlane-c-for-asm PRIVATE
  ALN_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/golden3.schema")

enable_testing()
add_executable(aln_test tests/aln_test.c)
target_link_libraries(aln_test PRIVATE aln_core m)
target_compile_options(aln_test PRIVATE -Wall -Wextra)
target_compile_definitions(aln_test PRIVATE
  ALN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_executable(float_test tests/float_test.c)
target_link_libraries(float_test PRIVATE aln_core m)
target_compile_options(float_test PRIVATE -Wall -Wextra)
target_compile_definitions(float_test PRIVATE
  ALN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME aln_test COMMAND aln_test)
add_test(NAME float_test COMMAND float_test)

add_executable(fuzz_frame tests/fuzz_frame.c)
target_link_libraries(fuzz_frame PRIVATE aln_core m)
target_compile_options(fuzz_frame PRIVATE -Wall -Wextra)
add_test(NAME fuzz_frame COMMAND fuzz_frame)
add_test(NAME cli_bench COMMAND arrowlane-c-for-asm bench --rows 1000 --repeats 3)
add_test(NAME cli_describe COMMAND arrowlane-c-for-asm describe)
set_tests_properties(cli_describe PROPERTIES
  PASS_REGULAR_EXPRESSION "\"name\":\"arrowlane-c-for-asm\"")
