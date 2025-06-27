#include <iostream>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <filesystem>

#include <RpType.pb.h>
#include <RcCore.h>
#include <RcScan.h>
#include <RcSearch.h>
#include "RcStruct.h"
#include "json.hpp"

std::vector<RDate> searchDateList;
using json = nlohmann::json;

std::ofstream g_logFile;
std::mutex g_logMutex;
std::string g_logDate;

std::string makeLogFilename() {
    std::ostringstream oss;
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    oss << "./logs/search/nvr_"
        << std::put_time(&tm, "%Y%m%d") << ".log";
    return oss.str();
}

void initLogDir() { std::filesystem::create_directories("./logs/search"); }

void openLogFile() {
    std::string filename = makeLogFilename();
    g_logFile.open(filename, std::ios::app);
    if (!g_logFile.is_open()) std::cerr << "[ERROR] 로그 파일 열기 실패: " << filename << std::endl;
    g_logDate = filename.substr(filename.size() - 12, 8); // "YYYYMMDD"
}
void checkLogRotate() {
    std::string curDate = makeLogFilename().substr(22, 8);
    if (curDate != g_logDate) {
        g_logFile.close();
        openLogFile();
    }
}
void initLog() { initLogDir(); openLogFile(); }
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
void closeLog() { std::lock_guard<std::mutex> lock(g_logMutex); if (g_logFile.is_open()) g_logFile.close(); }

struct RecorderConfig {
    std::string ip;
    int port;
    std::string userId;
    std::string password;
    std::vector<int> cameraList;
};

RecorderConfig loadOrCreateConfig(const std::string& path) {
    RecorderConfig config;
    std::ifstream inFile(path);
    if (!inFile.is_open()) {
        config.ip = "127.0.0.1";
        config.port = 11001;
        config.userId = "admin";
        config.password = "admin1234";
        config.cameraList = {0};
        json j;
        j["nvr"] = {{"ip", config.ip}, {"port", config.port}, {"user_id", config.userId}, {"password", config.password}};
        j["camera_list"] = config.cameraList;
        std::ofstream outFile(path);
        if (outFile.is_open()) outFile << std::setw(4) << j << std::endl;
        return config;
    }
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
            logMsg("SearchListener:onConnected : " + std::to_string(channel) + " " + info.deviceDescription);
        }
        void onDisconnected(int channel, Rp::DisconnectReason reason) override {
            logMsg("SearchListener:onDisconnected : " + std::to_string(channel));
        }
        void onFrameLoaded(int channel, const RFrame& frame) override {
            logMsg("SearchListener:onFrameLoaded : " + std::to_string(channel));
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
    
int main() {
    initLog();
    logMsg("==== NVR Search 시작 ====");
    RecorderConfig config = loadOrCreateConfig("config.json");
    std::cout << "[INFO] NVR: " << config.ip << ":" << config.port << " (user: " << config.userId << ")\n";

    rc_core_app_startup_type1();
    rc_core_set_image_format(Rp::IMAGE_FORMAT_RGB32);
    rc_core_set_search_dual_stream_priority(Rp::DualStreamType::DUAL_STREAM_TYPE_DYNAMIC);
    rc_core_set_fen_server(config.ip.c_str(), config.port);
    rc_core_startup_fen();

    ScanListener scanListener;
    SearchListener searchListener;
    rc_scan_set_listener(&scanListener);
    rc_search_set_listener(&searchListener);

    // 스캔 및 인증
    RScanInfo scanInfo{Rp::ADDRESS_TYPE_IPV4, config.ip, config.port};
    rc_scan_request_scan(scanInfo);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    RAccountInfo account{config.userId, config.password};
    rc_scan_request_authenticate(scanInfo, account);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Search 연결
    RDeviceConnectInfo connectInfo;
    connectInfo.addressType = Rp::ADDRESS_TYPE_IPV4;
    connectInfo.address = config.ip;
    connectInfo.port = config.port;
    connectInfo.isSupportUnityPort = true;
    connectInfo.userId = config.userId;
    connectInfo.userPw = config.password;
    connectInfo.isPasswordEncrypted = false;

    int searchChannel = rc_search_connect(connectInfo);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    rc_search_set_channel_camera_list(searchChannel, config.cameraList);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // 날짜 리스트 요청
    if (!searchDateList.empty()) {
        rc_search_request_time_list(searchChannel, searchDateList.back());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    rc_search_request_time_list(searchChannel, searchDateList[searchDateList.size()-2]);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    //이벤트 영상 play
    //rc_search_request_play(searchChannel);


    // 이벤트 쿼리 예시 (수정하여 사용)
    std::vector<int> events = {100}; // 이벤트 타입
    std::vector<int> cameras = {0,2,7}; // 카메라
    RDateTime begin;
    begin.year = 2025;
    begin.month = 6;
    begin.day = 26;
    begin.hour = 0;
    begin.minute = 0;
    begin.second = 0;
    RDateTime end;
    end.year = 2025;
    end.month = 6;
    end.day = 26;
    end.hour = 14;
    end.minute = 0;
    end.second = 0;
    rc_search_request_query_event(searchChannel, begin, end, events, cameras);

    // 종료 대기
    std::string input;
    std::cout << "Type 'quit' to stop...\n";
    while (true) {
        std::getline(std::cin, input);
        if (input == "quit") break;
    }

    rc_search_disconnect(searchChannel);
    rc_core_cleanup_fen();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    logMsg("==== NVR Search 종료 ====");
    closeLog();
    return EXIT_SUCCESS;
}

