#ifndef __RC_SCAN_H__
#define __RC_SCAN_H__

#pragma once

#include <RpType.pb.h>
#include "RcStruct.h"

class OnRcScanListener {
public:
   virtual void onDeviceScanned(bool isDeviceScanned, 
                                Rp::DisconnectReason reason, 
                                const RDeviceInformation& info) = 0;
   virtual void onDeviceAuthenticated(bool isAuthenticated, 
                                      Rp::DisconnectReason reason,
                                      const RAccountInfo account,
                                      const RDeviceInformation& info) = 0;
};

void rc_scan_set_listener(OnRcScanListener* listener);

void rc_scan_request_scan(const RScanInfo& info);
void rc_scan_cancel();
void rc_scan_request_authenticate(const RScanInfo& info, const RAccountInfo& account);
void rc_scan_cancel_authenticate();

#endif // !__RC_SCAN_H__

