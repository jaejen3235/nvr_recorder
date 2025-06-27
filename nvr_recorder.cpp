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
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
// const std::string deviceAddress = "14.33.239.166";
// const int devicePort = 8020;
// const std::string userId = "admin";
// const std::string userPw = "1q2w3e4r.";
//http://14.33.239.166:8020/web/cam0/live.m3u8

// User Alarm Tester
std::atomic<bool> g_serverRunning{true};
std::thread g_tcpServerThread;
int g_serverSocket = -1;
std::vector<int> g_clientSockets;
std::mutex g_clientSocketsMutex;

//related to search
std::vector<RDate> searchDateList;


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

#include <iostream>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <filesystem>

std::ofstream g_logFile;
std::mutex g_logMutex;
std::string g_logDate;
// 로그 파일명 생성 (./logs/NVR_YYYYMMDD.log)
std::string makeLogFilename() {
    std::ostringstream oss;
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    oss << "./logs/nvr_"
        << std::put_time(&tm, "%Y%m%d") << ".log";
    return oss.str();
}

void initLogDir() {
    std::filesystem::create_directories("./logs");
}

void openLogFile() {
    std::string filename = makeLogFilename();
    g_logFile.open(filename, std::ios::app);
    if (!g_logFile.is_open()) {
        std::cerr << "[ERROR] 로그 파일 열기 실패: " << filename << std::endl;
    }
    g_logDate = filename.substr(filename.size() - 12, 8); // "YYYYMMDD"
}

void checkLogRotate() {
    std::string curDate = makeLogFilename().substr(22, 8); // "YYYYMMDD"
    if (curDate != g_logDate) {
        g_logFile.close();
        openLogFile();
    }
}

void initLog() {
    initLogDir();
    openLogFile();
}

void logMsg(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    checkLogRotate();

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    if (g_logFile.is_open()) {
        g_logFile << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S] ") << msg << std::endl;
        g_logFile.flush();
    }
    std::cout << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S] ") << msg << std::endl;
}

void closeLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.close();
    }
}

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
        std::map<int, FILE*> hlsPipes;   // HLS용 ffmpeg 파이프
        std::map<int, FILE*> rtspPipes;  // RTSP용 ffmpeg 파이프

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
    
        // hls 스트리밍 전용
        // void openPipeIfNeeded(int camera) {
        //     if (hlsPipes.count(camera)) return;
    
        //     std::string dir = "web/cam" + std::to_string(camera);
        //     mkdir(dir.c_str(), 0755);
    
        //     std::ostringstream cmd;
        //     cmd << "ffmpeg "
        //         << "-loglevel error " //-loglevel quiet (로그 X)
        //         << "-f image2pipe "
        //         << "-use_wallclock_as_timestamps 1 "
        //         << "-i - "
        //         << "-c:v libx264 -preset ultrafast "
        //         << "-f hls "
        //         << "-hls_time 4 "
        //         << "-hls_list_size 10 "
        //         << "-hls_flags delete_segments+omit_endlist "
        //         << "-hls_segment_filename " << dir << "/live_%d.ts "
        //         << "-hls_base_url /web/cam" << camera << "/ "
        //         << dir << "/live.m3u8";

    
        //     std::cout << "[DEBUG] FFmpeg Command: " << cmd.str() << std::endl;
    
        //     FILE* pipe = popen(cmd.str().c_str(), "w");
        //     if (!pipe) {
        //         std::cerr << "[ERROR] Failed to start ffmpeg for camera " << camera << "\n";
        //         return;
        //     }
        //     hlsPipes[camera] = pipe;
        // }
        // void closeAllPipes() {
        //     for (auto& [cam, pipe] : hlsPipes) {
        //         if (pipe) pclose(pipe);
        //     }
        //     hlsPipes.clear();
        // }

        // hls & rtsp
        void openPipesIfNeeded(int camera) {
            // HLS 파이프 오픈
            if (!hlsPipes.count(camera)) {
                std::string dir = "web/cam" + std::to_string(camera);
                mkdir(dir.c_str(), 0755);
    
                std::ostringstream cmd;
                cmd << "ffmpeg "
                    << "-loglevel error "
                    << "-f image2pipe -use_wallclock_as_timestamps 1 -i - "
                    << "-c:v libx264 -preset ultrafast "
                    << "-f hls "
                    << "-hls_time 4 "
                    << "-hls_list_size 10 "
                    << "-hls_flags delete_segments+omit_endlist "
                    << "-hls_segment_filename " << dir << "/live_%d.ts "
                    << "-hls_base_url /web/cam" << camera << "/ "
                    << dir << "/live.m3u8";
                hlsPipes[camera] = popen(cmd.str().c_str(), "w");
            }
            // RTSP 파이프 오픈
            if (!rtspPipes.count(camera)) {
                std::ostringstream cmd;
                cmd << "ffmpeg "
                    << "-loglevel error "
                    << "-f image2pipe -use_wallclock_as_timestamps 1 -i - "
                    << "-c:v libx264 -preset ultrafast "
                    //<< "-f rtsp rtsp://127.0.0.1:8554/live" << camera;
                    << "-f rtsp rtsp://mediamtx:8554/live" << camera;
                logMsg("[DEBUG] " + cmd.str());
                rtspPipes[camera] = popen(cmd.str().c_str(), "w");
            }
        }
        void closeAllPipes() {
            for (auto& [cam, pipe] : hlsPipes) if (pipe) pclose(pipe);
            for (auto& [cam, pipe] : rtspPipes) if (pipe) pclose(pipe);
            hlsPipes.clear();
            rtspPipes.clear();
        }

        // hls 스트리밍 전용
        // void onFrameLoaded(int channel, const RFrame& frame) override {
        //     if (frame.imageFormat != Rp::IMAGE_FORMAT_RGB32 || frame.imageDataLength <= 0)
        //         return;

        //     // Camera resolutions on the NVR
        //     // std::cout << "[Frame] CAM " << frame.camera
        //     // << " - Converted: " << frame.imageWidth << "x" << frame.imageHeight
        //     // << ", Original: " << frame.originalWidth << "x" << frame.originalHeight
        //     // << std::endl;

        //     openPipeIfNeeded(frame.camera);
        //     FILE* pipe = ffmpegPipes[frame.camera];
        //     if (!pipe) return;
    
        //     // Compress frame to JPEG and write to pipe
        //     std::vector<unsigned char> jpegData;
        //     if (saveRgb32FrameAsJPEGToMemory(frame, jpegData)) {
        //         fwrite(jpegData.data(), 1, jpegData.size(), pipe);
        //         fflush(pipe);
        //     }
        // }
    
        void onFrameLoaded(int channel, const RFrame& frame) override {
            if (frame.imageFormat != Rp::IMAGE_FORMAT_RGB32 || frame.imageDataLength <= 0)
                return;
            openPipesIfNeeded(frame.camera);
            // JPEG 변환(예전 코드 그대로)
            std::vector<unsigned char> jpegData;
            if (!saveRgb32FrameAsJPEGToMemory(frame, jpegData)) return;
            // 두 파이프에 동시에 write
            FILE* hlsPipe = hlsPipes[frame.camera];
            FILE* rtspPipe = rtspPipes[frame.camera];
            if (hlsPipe) { fwrite(jpegData.data(), 1, jpegData.size(), hlsPipe); fflush(hlsPipe); }
            if (rtspPipe) { fwrite(jpegData.data(), 1, jpegData.size(), rtspPipe); fflush(rtspPipe); }
        }

        void onStatusLoaded(int, const RWatchStatus& status) override {
            logMsg("WatchListener:onStatusLoaded");
            // 00. Check event type
            bool eventDetected = true;
            // 01. Process the event
            if (eventDetected) {

                for(RCameraStatus cStatus : status.cameraStatusList){
                    auto now = std::chrono::system_clock::now();
                    std::cout << "[onStatusLoaded] CAM " << cStatus.camera
                    << " - Description: " << cStatus.description << std::endl;
                }
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
        //std::cout << "[Search] Connected: " << channel << " [" << info.deviceDescription << "]" << std::endl;
        logMsg("SearchListener:onConnected : " + std::to_string(channel) + " " + info.deviceDescription);
    }
    void onDisconnected(int channel, Rp::DisconnectReason reason) override {
        //std::cout << "[Search] Disconnected: " << channel << ", reason: " << reason << std::endl;
        logMsg("SearchListener:onDisconnected : " + std::to_string(channel));
    }
    void onFrameLoaded(int channel, const RFrame& frame) override {
        //std::cout << "[Search] Frame Loaded: channel=" << channel << ", sec=" << frame.dateTime.second << std::endl;
        logMsg("SearchListener:onFrameLoaded : " + std::to_string(channel));
        if (frame.imageFormat == Rp::IMAGE_FORMAT_RGB32) {
            //saveRgb32FrameAsJPEG(frame, channel, "SEARCH");
        }
    }
    void onCameraStatusLoaded(const RCameraStatus&) override {
        logMsg("SearchListener:onCameraStatusLoaded");
    }
    void onNoFrameLoaded(int, const RDateTime) override {
        logMsg("SearchListener:onNoFrameLoaded");
    }
    void onReceiveDateList(int, const std::vector<RDate>& dateList) override {
        logMsg("SearchListener:onReceiveDateList");
        int i=0;
        searchDateList = dateList;
        for(const auto& rdate : searchDateList){
            std::string msg = "searchDateList " + std::to_string(i++) + " : " + std::to_string(rdate.year) + "/" + std::to_string(rdate.month) + "/" + std::to_string(rdate.day);
            logMsg(msg);
        }
    }
    void onReceiveTimeList(int, const std::vector<int>& timeList) override {
        logMsg("SearchListener:onReceiveTimeList");
        int i=0;
        for(const auto& rtime : timeList){
            std::string msg = "timeList " + std::to_string(i++) + " : " + std::to_string(rtime) + "H";
            logMsg(msg);
        }
    }
    void onReceiveEventList(int, const std::vector<REventInfo>& eventList) override {
        logMsg("SearchListener:onReceiveEventList size : " + std::to_string(eventList.size()));
        for (const auto& evt : eventList) {
            std::string msg = "[Event] index=" + std::to_string(evt.index)
                + ", deviceChannel=" + std::to_string(evt.deviceChannel)
                + ", type=" + std::to_string(evt.type)
                + ", dateTime=" 
                    + std::to_string(evt.dateTime.year) + "-"
                    + std::to_string(evt.dateTime.month) + "-"
                    + std::to_string(evt.dateTime.day) + " "
                    + std::to_string(evt.dateTime.hour) + ":"
                    + std::to_string(evt.dateTime.minute) + ":"
                    + std::to_string(evt.dateTime.second)
                + ", label=" + evt.label
                + ", associatedChannels=[";
            
            for (size_t i = 0; i < evt.associatedChannels.size(); ++i) {
                msg += std::to_string(evt.associatedChannels[i]);
                if (i + 1 < evt.associatedChannels.size()) msg += ",";
            }
            msg += "]";
            msg += ", additionalData=" + evt.additionalData;
            msg += ", recognitionTime=" + evt.recognitionTime;
            logMsg(msg);
        }
    }
    void onReceiveSupportedEvents(int, const std::vector<int>& vint) override {
        logMsg("SearchListener:onReceiveSupportedEvents");
        for(const auto& vi : vint){
            logMsg(std::to_string(vi));
        }
    }
    void onReceiveSegmentSpotList(int, const std::vector<RSegmentSpot>&) override {
        logMsg("SearchListener:onReceiveSegmentSpotList");
    }
};


void tcp_server_thread_func(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        logMsg(std::string("[TCP] socket() failed: ") + strerror(errno));
        return;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logMsg(std::string("[TCP] bind() failed: ") + strerror(errno));
        close(server_fd);
        return;
    }
    if (listen(server_fd, 5) < 0) {
        logMsg(std::string("[TCP] listen() failed: ") + strerror(errno));
        close(server_fd);
        return;
    }
    logMsg("[TCP] Server started on port " + std::to_string(port));
    g_serverSocket = server_fd;

    fd_set readfds;
    char buffer[1024];
    while (g_serverRunning.load()) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;

        // 클라이언트 소켓 추가
        {
            std::lock_guard<std::mutex> lock(g_clientSocketsMutex);
            for (int client_fd : g_clientSockets) {
                FD_SET(client_fd, &readfds);
                if (client_fd > max_fd) max_fd = client_fd;
            }
        }

        timeval tv { 1, 0 }; // 1초 타임아웃
        int activity = select(max_fd + 1, &readfds, nullptr, nullptr, &tv);

        if (activity < 0 && errno != EINTR)
            continue;

        // 새 연결 처리
        if (FD_ISSET(server_fd, &readfds)) {
            sockaddr_in cli_addr {};
            socklen_t cli_len = sizeof(cli_addr);
            int client_fd = accept(server_fd, (sockaddr*)&cli_addr, &cli_len);
            if (client_fd >= 0) {
                char ip[32];
                inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));
                logMsg(std::string("[TCP] Client connected: ") + ip + ":" + std::to_string(ntohs(cli_addr.sin_port)));
                std::lock_guard<std::mutex> lock(g_clientSocketsMutex);
                g_clientSockets.push_back(client_fd);
            }
        }

        // 메시지 수신
        std::vector<int> disconnected;
        {
            std::lock_guard<std::mutex> lock(g_clientSocketsMutex);
            for (auto it = g_clientSockets.begin(); it != g_clientSockets.end(); ++it) {
                int client_fd = *it;
                if (FD_ISSET(client_fd, &readfds)) {
                    ssize_t len = recv(client_fd, buffer, sizeof(buffer)-1, 0);
                    if (len > 0) {
                        buffer[len] = 0;
                        logMsg(std::string("[TCP] Message: ") + buffer);
                    } else {
                        logMsg("[TCP] Client disconnected (fd=" + std::to_string(client_fd) + ")");
                        close(client_fd);
                        disconnected.push_back(client_fd);
                    }
                }
            }
            // 연결 끊긴 소켓 제거
            for (int fd : disconnected) {
                g_clientSockets.erase(std::remove(g_clientSockets.begin(), g_clientSockets.end(), fd), g_clientSockets.end());
            }
        }
    }
    // 모든 소켓 종료
    {
        std::lock_guard<std::mutex> lock(g_clientSocketsMutex);
        for (int client_fd : g_clientSockets)
            close(client_fd);
        g_clientSockets.clear();
    }
    close(server_fd);
    g_serverSocket = -1;
    logMsg("[TCP] Server stopped.");
}

void start_tcp_server(int port) {
    g_serverRunning = true;
    g_tcpServerThread = std::thread(tcp_server_thread_func, port);
}

void stop_tcp_server() {
    g_serverRunning = false;
    if (g_tcpServerThread.joinable())
        g_tcpServerThread.join();
}


int main() {
    initLog();
    logMsg("==== NVR Recorder 시작 ====");
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
    RScanInfo scanInfo{Rp::ADDRESS_TYPE_IPV4, config.ip, config.port};
    rc_scan_request_scan(scanInfo);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    RAccountInfo account{config.userId, config.password};
    rc_scan_request_authenticate(scanInfo, account);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // // 연결 및 카메라 설정
    // RDeviceConnectInfo connectInfo;
    // connectInfo.addressType = Rp::ADDRESS_TYPE_IPV4;
    // connectInfo.address = deviceAddress;
    // connectInfo.port = devicePort;
    // connectInfo.isSupportUnityPort = true;
    // connectInfo.userId = userId;
    // connectInfo.userPw = userPw;
    // connectInfo.isPasswordEncrypted = false;
    // 연결 및 카메라 설정
    RDeviceConnectInfo connectInfo;
    connectInfo.addressType = Rp::ADDRESS_TYPE_IPV4;
    connectInfo.address = config.ip;
    connectInfo.port = config.port;
    connectInfo.isSupportUnityPort = true;
    connectInfo.userId = config.userId;
    connectInfo.userPw = config.password;
    connectInfo.isPasswordEncrypted = false;

    //Search 테스트를 위해 Watch 주석처리
    int watchChannel = rc_watch_connect(connectInfo);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "[Watch] Connected channel: " << watchChannel << std::endl;

    rc_watch_set_channel_camera_list(watchChannel, config.cameraList);
    std::cout << "[Watch] Watching cameras...\n";

    //User Alarm Test (iNEX 이벤트 알람 수신용)
    start_tcp_server(8203);
    logMsg("==== UserAlarmTester 시작 ====");

    // rc_search_connect 사용할 경우 watch 동작 안됨
    // int searchChannel = rc_search_connect(connectInfo);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // rc_search_set_channel_camera_list(searchChannel, config.cameraList);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // rc_search_request_time_list(searchChannel, searchDateList[searchDateList.size()-2]);
    // std::this_thread::sleep_for(std::chrono::seconds(1));
    // 이벤트 영상 play
    //rc_search_request_play(searchChannel);

    // std::vector<int> events;
    // std::vector<int> cameras;
    // RDateTime begin;
    // begin.year = 2025;
    // begin.month = 6;
    // begin.day = 25;
    // begin.hour = 13;
    // begin.minute = 37;
    // begin.second = 0;
    // RDateTime end;
    // end.year = 2025;
    // end.month = 6;
    // end.day = 25;
    // end.hour = 14;
    // end.minute = 37;
    // end.second = 0;
    // rc_search_request_query_event(searchChannel, begin, end, events, cameras);

    // logMsg("Events : " + std::to_string(events.size()));
    // logMsg("Cameras : " + std::to_string(cameras.size()));


    // 사용자 종료 대기
    std::string input;
    std::cout << "Type 'quit' to stop...\n";
    while (true) {
        std::getline(std::cin, input);
        if (input == "quit") break;
    }

    // watch 종료 처리
    rc_watch_disconnect(watchChannel);
    // search 종료 처리
    //rc_search_disconnect(searchChannel);

    rc_core_cleanup_fen();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    logMsg("==== UserAlarmTester 종료 ====");
    stop_tcp_server();
    logMsg("==== NVR Recorder 종료 ====");
    closeLog();
    return EXIT_SUCCESS;
}
