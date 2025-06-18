cmake_minimum_required(VERSION 3.14)
project(nvr_recorder)

set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 20)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc -stdlib=libc++")
set(Boost_USE_STATIC_LIBS OFF)

find_package(Boost 1.74 REQUIRED system thread filesystem container atomic)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/ffmpeg-6.1.2/include
)

link_directories(
    ${CMAKE_SOURCE_DIR}/lib
    /usr/local/ffmpeg-6.1.2/lib
)

add_executable(nvr_recorder
    nvr_recorder.cpp
    streamFramesToHLS.cpp
)

target_link_libraries(nvr_recorder
    R2Framework
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_CONTAINER_LIBRARY}
    avformat
    avcodec
    avutil
    swscale
    swresample
    opus
    ssl
    crypto
    iconv
    pthread
    dl
    x264
    jpeg
    stdc++
)
