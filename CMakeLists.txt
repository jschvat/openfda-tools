cmake_minimum_required(VERSION 3.10)
project(OpenFDADownloader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(CURL REQUIRED libcurl)

# Find ncurses
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncurses)

# Find MySQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# Find PostgreSQL
find_package(PkgConfig REQUIRED)
pkg_check_modules(PGSQL REQUIRED libpq)

# Create the main executable with all modular source files
add_executable(openfda_ndc_downloader
    src/main.cpp
    src/database.cpp
    src/database_connection.cpp
    src/database_insertions.cpp
    src/data_processor.cpp
    src/error_logger.cpp
    src/network.cpp
    src/tui.cpp
    src/interactive.cpp
    src/api_downloader.cpp
)

# Link all required libraries
target_link_libraries(openfda_ndc_downloader
    ${JSONCPP_LIBRARIES}
    ${CURL_LIBRARIES}
    ${NCURSES_LIBRARIES}
    ${MYSQL_LIBRARIES}
    ${PGSQL_LIBRARIES}
)

# Include directories
target_include_directories(openfda_ndc_downloader PRIVATE
    include/
    ${JSONCPP_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${NCURSES_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIRS}
    ${PGSQL_INCLUDE_DIRS}
)

# Compile options
target_compile_options(openfda_ndc_downloader PRIVATE 
    ${JSONCPP_CFLAGS_OTHER}
    ${CURL_CFLAGS_OTHER}
    ${NCURSES_CFLAGS_OTHER}
    ${MYSQL_CFLAGS_OTHER}
    ${PGSQL_CFLAGS_OTHER}
)

# Optional: Create a separate test executable for error logger
add_executable(test_error_logger
    tests/test_error_logger.cpp
    src/error_logger.cpp
)

target_link_libraries(test_error_logger
    ${JSONCPP_LIBRARIES}
)

target_include_directories(test_error_logger PRIVATE
    include/
    ${JSONCPP_INCLUDE_DIRS}
)

target_compile_options(test_error_logger PRIVATE ${JSONCPP_CFLAGS_OTHER})