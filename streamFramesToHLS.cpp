#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include "RcStruct.h"

void streamFramesToHLS(int camId, int w, int h, const std::vector<RFrame>& frames) {
    namespace fs = std::filesystem;

    // HLS 저장 디렉토리: ./web/cam{camId}
    std::string dir = "web/cam" + std::to_string(camId);
    fs::create_directories(dir);  // 디렉토리 없으면 생성

    // FFmpeg 명령어 구성
    std::ostringstream command;
    command << "ffmpeg -f rawvideo -pixel_format bgr0 -video_size "
            << w << "x" << h << " "
            << "-framerate 25 -i - "
            << "-c:v libx264 -preset ultrafast "
            << "-f hls "
            << "-hls_time 5 "
            << "-hls_list_size 5 "
            << "-hls_flags delete_segments "
            << "-hls_segment_filename " << dir << "/live_%d.ts "            // 이 줄을 base_url보다 뒤로!
            << "-hls_base_url /web/cam" << camId << "/ "                    // 반드시 이 줄을 마지막 output 직전에!
            << dir << "/live.m3u8";

    // FFmpeg 명령어 구성 후
    std::cout << "[Debug] FFmpeg Command: " << command.str() << std::endl;

    // FFmpeg 프로세스 실행
    FILE* pipe = popen(command.str().c_str(), "w");
    if (!pipe) {
        std::cerr << "[Error] Failed to open FFmpeg pipe for camera " << camId << "\n";
        return;
    }

    // 프레임 데이터를 FFmpeg 입력으로 전송
    for (const auto& frame : frames) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(frame.imageDataPointer);
        fwrite(data, 1, frame.imageDataLength, pipe);
    }

    fflush(pipe); //닫 기
    pclose(pipe);

    std::cout << "[Info] HLS stream written for camera " << camId << " (" << frames.size() << " frames)\n";
}
