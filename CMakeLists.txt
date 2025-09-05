cmake_minimum_required(VERSION 3.10)
project(OpenFDANDCDownloader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Find required libraries
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
find_path(CURL_INCLUDE_DIR curl/curl.h)
find_library(CURL_LIBRARY curl)
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
find_library(MYSQL_LIBRARY mysqlclient)

# Check if all libraries are found
if(NOT CURL_INCLUDE_DIR OR NOT CURL_LIBRARY)
    message(FATAL_ERROR "libcurl not found. Install with: sudo apt-get install libcurl4-openssl-dev")
endif()

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL client library not found. Install with: sudo apt-get install libmysqlclient-dev")
endif()

# Create the executable
add_executable(openfda_ndc_downloader openfda_ndc_downloader.cpp)

# Link libraries
target_link_libraries(openfda_ndc_downloader 
    ${CURL_LIBRARY}
    ${MYSQL_LIBRARY}
    ${JSONCPP_LIBRARIES}
    Threads::Threads
)

# Include directories
target_include_directories(openfda_ndc_downloader PRIVATE
    ${CURL_INCLUDE_DIR}
    ${MYSQL_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIRS}
)

# Compiler flags
target_compile_options(openfda_ndc_downloader PRIVATE ${JSONCPP_CFLAGS_OTHER})

# Install target
install(TARGETS openfda_ndc_downloader DESTINATION bin)