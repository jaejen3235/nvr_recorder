#ifndef __RC_SEARCH_H__
#define __RC_SEARCH_H__

#pragma once

#include "RcStruct.h"

#include <vector>

class OnRcSearchListener {
public:
   virtual void onConnected(int channel, const RDeviceInformation& info) = 0;
   virtual void onDisconnected(int channel, Rp::DisconnectReason reason) = 0;
   virtual void onCameraStatusLoaded(const RCameraStatus& cameraStatus) = 0;
   virtual void onFrameLoaded(int channel, const RFrame& frame) = 0;
   virtual void onNoFrameLoaded(int channel, const RDateTime dateTime) = 0;
   virtual void onReceiveDateList(int channel, const std::vector<RDate>& dateList) = 0;
   virtual void onReceiveTimeList(int channel, const std::vector<int>& hours) = 0;
   virtual void onReceiveEventList(int channel, const std::vector<REventInfo>& eventList) = 0;
   virtual void onReceiveSupportedEvents(int channel, const std::vector<int>& events) = 0;
   virtual void onReceiveSegmentSpotList(int channel, const std::vector<RSegmentSpot>& spotList) = 0;
};
   
void rc_search_set_listener(OnRcSearchListener* listener);

int rc_search_connect(const RDeviceConnectInfo& connectInfo);
void rc_search_disconnect(int channel);
void rc_search_set_channel_camera_list(int channel, const std::vector<int>& cameraList); 
void rc_search_request_time_list(int channel, const RDate& date);
void rc_search_request_time_list_cancel(int channel);
void rc_search_request_play(int channel);
void rc_search_request_stop(int channel);
void rc_search_request_move_to_last(int channel);
void rc_search_request_move_to_first(int channel);
void rc_search_request_move_to_previous(int channel);
void rc_search_request_move_to_next(int channel);
void rc_search_request_move_to_datetime(int channel, const RDateTime& dateTime, bool isLoadAdjacentFrame, bool isConsiderSegment);
void rc_search_request_move_to_event(int channel, int eventIndex);
void rc_search_request_move_to_segment_spot(int channel, int segmentSpotIndex);
int rc_search_request_rewind_step(int channel);
int rc_search_request_fastforward_step(int channel);
void rc_search_request_query_event(int channel, const RDateTime begin, const RDateTime end, const std::vector<int>& events, const std::vector<int>& cameras);
void rc_search_request_query_event_next(int channel);
void rc_search_request_query_event_stop(int channel);

#endif // !__RC_SEARCH_H__

