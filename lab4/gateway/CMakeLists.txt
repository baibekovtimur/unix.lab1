add_executable(gateway main.cpp)

find_path(PISTACHE_INCLUDE_DIR pistache/http.h)
find_library(PISTACHE_LIBRARY pistache)
find_library(RDKAFKA_CPP_LIB rdkafka++)
find_library(RDKAFKA_LIB rdkafka)

if (NOT PISTACHE_INCLUDE_DIR OR NOT PISTACHE_LIBRARY)
  message(FATAL_ERROR "Pistache not found. Install libpistache-dev.")
endif()
if (NOT RDKAFKA_CPP_LIB OR NOT RDKAFKA_LIB)
  message(FATAL_ERROR "librdkafka++ not found. Install librdkafka-dev.")
endif()

target_include_directories(gateway PRIVATE ${PISTACHE_INCLUDE_DIR})
target_link_libraries(gateway PRIVATE ${PISTACHE_LIBRARY} ${RDKAFKA_CPP_LIB} ${RDKAFKA_LIB} pthread)