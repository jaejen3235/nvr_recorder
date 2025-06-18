#ifndef __RC_STRUCT_H__
#define __RC_STRUCT_H__

#pragma once

#include <RpType.pb.h>

struct RScanInfo {
   Rp::AddressType addressType;
   std::string address;
   int port;
};

struct RAccountInfo {
   std::string userId;
   std::string userPw;
};

struct RDeviceInformation {
   std::string deviceDescription;

   bool isSupportUnityPort;
   int portWatch;
   int portSearch;
   int portAdmin;
   int portAudio;

   Rp::DeviceType deviceType;
   int cameraCount;
   std::string productName;
   std::string macAddress;

   bool isSupportSearch;
   bool isSupportDualTrackRecording;
   bool isSupportPush;
};

struct RDeviceConnectInfo {
   Rp::AddressType addressType;

   std::string address;
   int port;
   bool isSupportUnityPort; // If a device support a unity port, true.

   std::string userId;
   std::string userPw;

   bool isPasswordEncrypted; // If true, userPw value has to be hashed by SHA256 from a plain password. (To use this function, RDeviceInformation.isSupportUnityPort true required.)
};

struct RDate {
   int year; 
   int month;
   int day;
};

struct RDateTime {
   int year;
   int month;
   int day;
   int hour;
   int minute;
   int second;
};

struct RFrame {
   // Copy a image data from sdk to an application by using this pointer.
   int64_t imageDataPointer;
   int64_t imageDataLength;

   Rp::ImageFormat imageFormat;
   
   int imageWidth; // Converted image size, use this value to copy a iamge.
   int imageHeight; // Converted image size, use this value to copy a iamge.
   
   int originalWidth; // Original size, just for reference.
   int originalHeight; // Original size, just for reference.

   std::string title; // camera title
   int camera; // camera index
   RDateTime dateTime; // image time
   int32_t tick;
   
   int segmentId; // Only for search.
   int streamId; // Only for search.
};

struct RAlarmDevice {
   int alarmId;
   std::string name;
};

struct RStreamInfo {
   std::string title;
   
   bool isStreamOn;

   int width;
   int height;
   float ips;
};

struct RCameraStatus {
   int channel;
   int camera;
   std::string description;

   Rp::CameraState state; // Used for watch
    
   bool isAvailableCameraAudioIn;
   bool isAvailableCameraAudioOut;

   std::vector<RStreamInfo> streamInfo;

   bool isAvailablePTZMove; 
   bool isAvailablePTZZoom; 
   bool isAvailablePTZFocus;
   bool isAvailablePTZIris;
   bool isAvailablePTZPresetMove;
   bool isAvailablePTZFocusOnePush;
   bool isAvailablePTZLensReset;
};

struct RWatchStatus {
   std::string deviceDescription;

   std::vector<RCameraStatus> cameraStatusList;
   std::vector<RAlarmDevice> alarmOutDevices;

   bool isAvailableAlarmOut;
   bool isAvailableAlarmOutBeep;
   bool isAvailableDeviceAudioIn;
   bool isAvailableDeviceAudioOut;
};

struct RPtzPreset {
   int index;
   std::string name;
};

struct RAudioFormat {
   int sampling;
   int channel;
   int segment;
   int bitrate;
   int bitPerSample;
   int outputSize;
};

struct RSegmentSpot {
    int index;
    int segmentId;
    RDateTime datetime;
};

struct REventInfo {
    int index;
    int deviceChannel;
    
    int type; // Rp::REvent
    RDateTime dateTime;
    std::string label;
    
    std::vector<int> associatedChannels;
    std::string additionalData;
    std::string recognitionTime; // Anpr, Fac
};

#endif // !__RC_STRUCT_H__

