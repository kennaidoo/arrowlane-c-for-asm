cmake_minimum_required(VERSION 3.20)
project(arrowlane_c_for_asm LANGUAGES C Fortran)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(ALN_ASM_CRC32 "Use the hand written x86-64 checksum kernel" ON)

set(ALN_CORE_SOURCES
  src/crc32.c
  src/base64.c
  src/frame.c
  src/json_fixture.c
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
add_test(NAME aln_test COMMAND aln_test)
add_test(NAME cli_bench COMMAND arrowlane-c-for-asm bench --rows 1000 --repeats 3)
add_test(NAME cli_describe COMMAND arrowlane-c-for-asm describe)
set_tests_properties(cli_describe PROPERTIES
  PASS_REGULAR_EXPRESSION "\"name\":\"arrowlane-c-for-asm\"")
