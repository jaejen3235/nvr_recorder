#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <unordered_map>

#include <RpType.pb.h>
#include <RcCore.h>
#include <RcScan.h>
#include <RcWatch.h>
#include <RcSearch.h>
#include "RcStruct.h"
#include <sys/stat.h>    // mkdir, chmod 등 POSIX 파일 제어 함수
#include <sys/types.h>   // mode_t 정의
#include <jpeglib.h>
#include "json.hpp"  // json.hpp 필요

// NVR 접속 정보
const std::string deviceAddress = "14.33.239.166";
const int devicePort = 8020;
const std::string userId = "admin";
const std::string userPw = "1q2w3e4r.";

// 전역 종료 플래그
std::atomic<bool> g_running(true);

using json = nlohmann::json;
// 설정 구조체
struct RecorderConfig {
    std::string ip;
    int port;
    std::string userId;
    std::string password;
    std::vector<int> cameraList;
};
// {
//     "nvr": {
//       "ip": "192.168.0.15",
//       "port": 11001,
//       "user_id": "admin",
//       "password": "dainlab12!@"
//     },
//     "camera_list": [0, 2, 7]
// }

// 전역 설정 및 인덱스
RecorderConfig config;
std::map<int, int> cameraFileIndices; // cameraId → 순환 인덱스

RecorderConfig loadOrCreateConfig(const std::string& path) {
    RecorderConfig config;

    std::ifstream inFile(path);
    if (!inFile.is_open()) {
        std::cerr << "[WARN] config.json not found. Creating default config.\n";

        // 기본값 설정
        config.ip = "127.0.0.1";
        config.port = 11001;
        config.userId = "admin";
        config.password = "admin1234";
        config.cameraList = {0};

        // JSON으로 저장
        json j;
        j["nvr"] = {
            {"ip", config.ip},
            {"port", config.port},
            {"user_id", config.userId},
            {"password", config.password}
        };
        j["camera_list"] = config.cameraList;

        std::ofstream outFile(path);
        if (outFile.is_open()) {
            outFile << std::setw(4) << j << std::endl;
            std::cout << "[INFO] config.json created with default values.\n";
        } else {
            std::cerr << "[ERROR] Failed to write config.json\n";
        }

        return config;
    }

    // 파일이 있을 경우 기존대로 로딩
    try {
        json j;
        inFile >> j;

        if (j.contains("nvr")) {
            config.ip       = j["nvr"].value("ip", config.ip);
            config.port     = j["nvr"].value("port", config.port);
            config.userId   = j["nvr"].value("user_id", config.userId);
            config.password = j["nvr"].value("password", config.password);
        }

        config.cameraList = j.value("camera_list", config.cameraList);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to parse config.json: " << e.what() << "\n";
    }

    return config;
}


// 프레임 저장 함수 (선언만, 실제 구현은 별도)
//void saveRgb32FrameAsJPEG(const RFrame& frame, int channelId, std::string prefix) {}

std::unordered_map<int, std::vector<RFrame>> cameraFrameMap;
std::mutex frameMutex;
const int FRAME_THRESHOLD = 100;  // 프레임 수 임계값 예시

struct FrameBundle {
    std::vector<RFrame> frames;
    std::chrono::steady_clock::time_point startTime;
};
std::map<int, FrameBundle> cameraBuffers;
std::mutex bufferMutex;

static std::map<int, std::chrono::steady_clock::time_point> lastFrameTime;

class WatchListener : public OnRcWatchListener {
    public:
        std::map<int, FILE*> ffmpegPipes;
        int width = 1920;
        int height = 1080;
        std::string pixelFormat = "bgr0";
    
        void onConnected(int channel, const RDeviceInformation& info) override {
            std::cout << "[Watch] Connected: " << channel << " [" << info.deviceDescription << "]" << std::endl;
        }
    
        void onDisconnected(int channel, Rp::DisconnectReason reason) override {
            closeAllPipes();
            std::cout << "[Watch] Disconnected: " << channel << ", reason: " << reason << std::endl;
        }
    
        void openPipeIfNeeded(int camera) {
            if (ffmpegPipes.count(camera)) return;
    
            std::string dir = "web/cam" + std::to_string(camera);
            mkdir(dir.c_str(), 0755);
    
            std::ostringstream cmd;
            cmd << "ffmpeg "
                << "-f image2pipe "
                << "-use_wallclock_as_timestamps 1 "
                << "-i - "
                << "-c:v libx264 -preset ultrafast "
                << "-f hls "
                << "-hls_time 4 "
                << "-hls_list_size 10 "
                << "-hls_flags delete_segments+omit_endlist "
                << "-hls_segment_filename " << dir << "/live_%d.ts "
                << "-hls_base_url /web/cam" << camera << "/ "
                << dir << "/live.m3u8";

    
            std::cout << "[DEBUG] FFmpeg Command: " << cmd.str() << std::endl;
    
            FILE* pipe = popen(cmd.str().c_str(), "w");
            if (!pipe) {
                std::cerr << "[ERROR] Failed to start ffmpeg for camera " << camera << "\n";
                return;
            }
            ffmpegPipes[camera] = pipe;
        }
    
        void closeAllPipes() {
            for (auto& [cam, pipe] : ffmpegPipes) {
                if (pipe) pclose(pipe);
            }
            ffmpegPipes.clear();
        }
    
        void onFrameLoaded(int channel, const RFrame& frame) override {
            if (frame.imageFormat != Rp::IMAGE_FORMAT_RGB32 || frame.imageDataLength <= 0)
                return;

            // Camera resolutions on the NVR
            // std::cout << "[Frame] CAM " << frame.camera
            // << " - Converted: " << frame.imageWidth << "x" << frame.imageHeight
            // << ", Original: " << frame.originalWidth << "x" << frame.originalHeight
            // << std::endl;

            openPipeIfNeeded(frame.camera);
            FILE* pipe = ffmpegPipes[frame.camera];
            if (!pipe) return;
    
            // Compress frame to JPEG and write to pipe
            std::vector<unsigned char> jpegData;
            if (saveRgb32FrameAsJPEGToMemory(frame, jpegData)) {
                fwrite(jpegData.data(), 1, jpegData.size(), pipe);
                fflush(pipe);
            }
        }
    
        void onStatusLoaded(int, const RWatchStatus& status) override {

            // struct RCameraStatus {
            //     int channel;
            //     int camera;
            //     std::string description;
             
            //     Rp::CameraState state; // Used for watch
                 
            //     bool isAvailableCameraAudioIn;
            //     bool isAvailableCameraAudioOut;
             
            //     std::vector<RStreamInfo> streamInfo;
             
            //     bool isAvailablePTZMove; 
            //     bool isAvailablePTZZoom; 
            //     bool isAvailablePTZFocus;
            //     bool isAvailablePTZIris;
            //     bool isAvailablePTZPresetMove;
            //     bool isAvailablePTZFocusOnePush;
            //     bool isAvailablePTZLensReset;
            //  };
             
            // 00. Check event type
            bool eventDetected = true;
            // 01. Process the event
            if (eventDetected) {

                for(RCameraStatus cStatus : status.cameraStatusList){
                    auto now = std::chrono::system_clock::now();
                    std::cout << "[onStatusLoaded] CAM " << cStatus.camera
                    << " - Description: " << cStatus.description << std::endl;
                }
                //saveEventClipForCamera(cameraId, now);
            }
        }
        void onRecvPTZPresetList(int, int, const std::vector<RPtzPreset>&) override {}
        void onAudioConnected(int) override {}
        void onAudioDisconnected(int, Rp::DisconnectReason) override {}
        void onReceivingAudioOpened(int, int, const RAudioFormat&) override {}
        void onReceivingAudioClosed(int, int) override {}
        void onReceivingAudioFormatChanged(int, int, const RAudioFormat&) override {}
        void onAudioDataLoaded(int, int, const std::string&) override {}
        void onSendingAudioOpened(int, int, bool, const RAudioFormat&) override {}
        void onSendingAudioClosed(int, int) override {}
    
        // RGB32 to JPEG conversion
        bool saveRgb32FrameAsJPEGToMemory(const RFrame& frame, std::vector<unsigned char>& jpegOut) {
            jpegOut.clear();
            jpeg_compress_struct cinfo;
            jpeg_error_mgr jerr;
    
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_compress(&cinfo);
    
            unsigned char* mem = nullptr;
            unsigned long memSize = 0;
            jpeg_mem_dest(&cinfo, &mem, &memSize);
    
            cinfo.image_width = frame.imageWidth;
            cinfo.image_height = frame.imageHeight;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;
    
            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, 85, TRUE);
            jpeg_start_compress(&cinfo, TRUE);
    
            const uint8_t* src = reinterpret_cast<const uint8_t*>(frame.imageDataPointer);
            std::vector<uint8_t> rowBuf(frame.imageWidth * 3);
    
            while (cinfo.next_scanline < cinfo.image_height) {
                for (int x = 0; x < frame.imageWidth; ++x) {
                    rowBuf[x * 3 + 0] = src[cinfo.next_scanline * frame.imageWidth * 4 + x * 4 + 2]; // R
                    rowBuf[x * 3 + 1] = src[cinfo.next_scanline * frame.imageWidth * 4 + x * 4 + 1]; // G
                    rowBuf[x * 3 + 2] = src[cinfo.next_scanline * frame.imageWidth * 4 + x * 4 + 0]; // B
                }
                JSAMPROW rowPointer = rowBuf.data();
                jpeg_write_scanlines(&cinfo, &rowPointer, 1);
            }
    
            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
    
            if (mem && memSize > 0) {
                jpegOut.assign(mem, mem + memSize);
                free(mem);
                return true;
            }
            return false;
        }
    };

class ScanListener : public OnRcScanListener {
public:
    void onDeviceScanned(bool scanned, Rp::DisconnectReason reason, const RDeviceInformation& info) override {
        std::cout << "[Scan] Device scanned: " << scanned << std::endl;
    }
    void onDeviceAuthenticated(bool auth, Rp::DisconnectReason reason, const RAccountInfo account, const RDeviceInformation& info) override {
        std::cout << "[Scan] Authenticated: " << auth << std::endl;
    }
};

class SearchListener : public OnRcSearchListener {
public:
    void onConnected(int channel, const RDeviceInformation& info) override {
        std::cout << "[Search] Connected: " << channel << " [" << info.deviceDescription << "]" << std::endl;
    }
    void onDisconnected(int channel, Rp::DisconnectReason reason) override {
        std::cout << "[Search] Disconnected: " << channel << ", reason: " << reason << std::endl;
    }
    void onFrameLoaded(int channel, const RFrame& frame) override {
        std::cout << "[Search] Frame Loaded: channel=" << channel << ", sec=" << frame.dateTime.second << std::endl;
        if (frame.imageFormat == Rp::IMAGE_FORMAT_RGB32) {
            //saveRgb32FrameAsJPEG(frame, channel, "SEARCH");
        }
    }
    void onCameraStatusLoaded(const RCameraStatus&) override {}
    void onNoFrameLoaded(int, const RDateTime) override {}
    void onReceiveDateList(int, const std::vector<RDate>&) override {}
    void onReceiveTimeList(int, const std::vector<int>&) override {}
    void onReceiveEventList(int, const std::vector<REventInfo>&) override {}
    void onReceiveSupportedEvents(int, const std::vector<int>&) override {}
    void onReceiveSegmentSpotList(int, const std::vector<RSegmentSpot>&) override {}
};

int main() {
    // 설정 파일 로딩
    config = loadOrCreateConfig("config.json");

    std::cout << "[INFO] NVR: " << config.ip << ":" << config.port
              << " (user: " << config.userId << ")\n";

    for (int cam : config.cameraList) {
        std::cout << "[INFO] Starting stream for camera " << cam << std::endl;
        // openPipeIfNeeded(cam); ← 기존 FFmpeg 스트리밍 로직 연결
        // rc_watch_set_channel_camera_list 등 SDK 함수 호출
    }

    // SDK 초기화
    rc_core_app_startup_type1();
    rc_core_set_image_format(Rp::IMAGE_FORMAT_RGB32);
    rc_core_set_search_dual_stream_priority(Rp::DualStreamType::DUAL_STREAM_TYPE_DYNAMIC);
    rc_core_set_fen_server("192.168.0.15", 11001);
    rc_core_startup_fen();
    std::cout << "[Core] Version: " << rc_core_get_version() << std::endl;

    ScanListener scanListener;
    WatchListener watchListener;
    SearchListener searchListener;
    rc_scan_set_listener(&scanListener);
    rc_watch_set_listener(&watchListener);
    rc_search_set_listener(&searchListener);

    // 스캔 및 인증
    RScanInfo scanInfo{Rp::ADDRESS_TYPE_IPV4, deviceAddress, devicePort};
    rc_scan_request_scan(scanInfo);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    RAccountInfo account{userId, userPw};
    rc_scan_request_authenticate(scanInfo, account);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 연결 및 카메라 설정
    RDeviceConnectInfo connectInfo;
    connectInfo.addressType = Rp::ADDRESS_TYPE_IPV4;
    connectInfo.address = deviceAddress;
    connectInfo.port = devicePort;
    connectInfo.isSupportUnityPort = true;
    connectInfo.userId = userId;
    connectInfo.userPw = userPw;
    connectInfo.isPasswordEncrypted = false;

    int watchChannel = rc_watch_connect(connectInfo);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "[Watch] Connected channel: " << watchChannel << std::endl;

    rc_watch_set_channel_camera_list(watchChannel, config.cameraList);
    std::cout << "[Watch] Watching cameras...\n";

    // 사용자 종료 대기
    std::string input;
    std::cout << "Type 'quit' to stop...\n";
    while (true) {
        std::getline(std::cin, input);
        if (input == "quit") break;
    }

    // 종료 처리
    rc_watch_disconnect(watchChannel);
    rc_core_cleanup_fen();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    return EXIT_SUCCESS;
}
