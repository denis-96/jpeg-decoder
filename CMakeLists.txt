cmake_minimum_required(VERSION 3.20)

project(jpeg_decoder VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_path(FFTW3_INCLUDE_DIR fftw3.h REQUIRED)
find_library(FFTW3_LIBRARY NAMES fftw3 REQUIRED)

add_library(decoder
    decoder/bit_reader.cpp
    decoder/decoder.cpp
    decoder/fft.cpp
    decoder/huffman.cpp
    decoder/jpeg_decoder.cpp
)

add_library(jpeg_decoder::decoder ALIAS decoder)

target_compile_features(decoder PUBLIC cxx_std_20)
target_include_directories(decoder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${FFTW3_INCLUDE_DIR}
)
target_link_libraries(decoder PRIVATE ${FFTW3_LIBRARY})

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(test_baseline
        tests/test_commons.cpp
        tests/test_baseline.cpp
    )
    add_executable(test_progressive
        tests/test_commons.cpp
        tests/test_progressive.cpp
    )

    foreach(test_target IN ITEMS test_baseline test_progressive)
        target_link_libraries(${test_target}
            PRIVATE
                decoder
                Catch2::Catch2WithMain
                JPEG::JPEG
                PNG::PNG
        )
        target_include_directories(${test_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()
endif()
