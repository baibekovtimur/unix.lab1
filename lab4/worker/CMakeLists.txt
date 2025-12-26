add_executable(worker main.cpp)

find_library(RDKAFKA_CPP_LIB rdkafka++)
find_library(RDKAFKA_LIB rdkafka)

if (NOT RDKAFKA_CPP_LIB OR NOT RDKAFKA_LIB)
  message(FATAL_ERROR "librdkafka++ not found. Install librdkafka-dev.")
endif()

target_link_libraries(worker PRIVATE ${RDKAFKA_CPP_LIB} ${RDKAFKA_LIB} pthread)
target_include_directories(worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})