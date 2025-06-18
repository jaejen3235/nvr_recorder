#ifndef __RC_CORE_H__
#define __RC_CORE_H__

#pragma once

#include <RpType.pb.h>

std::string rc_core_get_version();
void rc_core_app_startup_type1();
bool rc_core_startup_fen();
void rc_core_cleanup_fen();
void rc_core_set_fen_server(const std::string& address, int port);
void rc_core_set_image_format(Rp::ImageFormat imageFormat);
void rc_core_set_search_dual_stream_priority(Rp::DualStreamType type);

#endif // !__RC_CORE_H__

