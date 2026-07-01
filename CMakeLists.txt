cmake_minimum_required(VERSION 3.15)
project(EWR VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIB_SOURCES src/parser.cpp src/generator.cpp)

if(WIN32)
    list(APPEND LIB_SOURCES src/usb_windows.cpp)
else()
    list(APPEND LIB_SOURCES src/usb_linux.cpp)
endif()

add_library(ewr_core STATIC ${LIB_SOURCES})

target_include_directories(ewr_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>
)

if(WIN32)
    target_link_libraries(ewr_core PRIVATE setupapi urlmon)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    pkg_check_modules(LIBCURL REQUIRED libcurl)

    target_include_directories(ewr_core PRIVATE ${LIBUSB_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS})
    target_link_libraries(ewr_core PRIVATE ${LIBUSB_LIBRARIES} ${LIBCURL_LIBRARIES})
endif()

add_executable(ewr cli/main.cpp)
target_link_libraries(ewr PRIVATE ewr_core)
set_target_properties(ewr PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# ── Tests: golden-byte safety net (P0) ───────────────────────────────────────
# ewr_core is a STATIC lib, so the test pulls only the object files it references
# (generator.o + parser.o). generator.o references the OTA download symbols, so
# the test links the same platform HTTP lib; USB objects are not referenced.
enable_testing()
add_executable(golden_test tests/golden_test.cpp)
target_link_libraries(golden_test PRIVATE ewr_core)
if(WIN32)
    target_link_libraries(golden_test PRIVATE urlmon)
else()
    target_link_libraries(golden_test PRIVATE ${LIBUSB_LIBRARIES} ${LIBCURL_LIBRARIES})
endif()
add_test(NAME golden_test COMMAND golden_test)
