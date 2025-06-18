#ifndef __RC_WATCH_H__
#define __RC_WATCH_H__

#pragma once

#include "RcStruct.h"

#include <vector>

class OnRcWatchListener {
public:
   virtual void onConnected(int channel, const RDeviceInformation& info) = 0;
   virtual void onDisconnected(int channel, Rp::DisconnectReason reason) = 0;
   virtual void onFrameLoaded(int channel, const RFrame& frame) = 0;
   virtual void onStatusLoaded(int channel, const RWatchStatus& status) = 0;
   virtual void onRecvPTZPresetList(int channel, int camera, const std::vector<RPtzPreset>& presets) = 0;
   virtual void onAudioConnected(int channel) = 0;
   virtual void onAudioDisconnected(int channel, Rp::DisconnectReason reason) = 0;
   virtual void onReceivingAudioOpened(int channel, int camera, const RAudioFormat& format) = 0;
   virtual void onReceivingAudioClosed(int channel, int camera) = 0;
   virtual void onReceivingAudioFormatChanged(int channel, int camera, const RAudioFormat& format) = 0;
   virtual void onAudioDataLoaded(int channel, int camera, const std::string& data) = 0;
   virtual void onSendingAudioOpened(int channel, int camera, bool available, const RAudioFormat& format) = 0;
   virtual void onSendingAudioClosed(int channel, int camera) = 0;
};

void rc_watch_set_listener(OnRcWatchListener* listener);

int rc_watch_connect(const RDeviceConnectInfo& connectInfo);
void rc_watch_disconnect(int channel);
void rc_watch_set_channel_camera_list(int channel, const std::vector<int>& cameraList);
void rc_watch_set_ptz(int channel, int camera, Rp::PtzCommand command, Rp::PtzCommandMethod argument);
void rc_watch_request_ptz_preset_list(int channel, int camera);
void rc_watch_request_ptz_preset_move(int channel, int camera, int index);
void rc_watch_set_alarm_out(int channel, int alarmId, bool isOn);
void rc_watch_set_beep_control(int channel, bool isOn);
void rc_watch_connect_audio(int channel, int port);
void rc_watch_disconnect_audio(int channel, int camera);
void rc_watch_request_receiving_audio_open(int channel, int camera);
void rc_watch_request_receiving_audio_close(int channel, int camera);
void rc_watch_request_sending_audio_open(int channel, int camera);
void rc_watch_request_sending_audio_close(int channel, int camera);
void rc_watch_send_audio_data(int channel, int camera, std::string& data);

#endif // !__RC_WATCH_H__

