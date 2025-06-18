#include <cstdio>
#include <vector>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "RcStruct.h"

static int64_t streamPts = 0;

void streamFramesToHLS(const std::vector<RFrame>& frames) {
    if (frames.empty()) return;

    // FFmpeg 프로세스 시작 (파이프 출력용)
    FILE* ffmpeg = popen(
        "ffmpeg -fflags +genpts -f h264 -i - "
        "-c:v h264 -f hls "
        "-hls_time 5 "
        "-hls_list_size 5 "
        "-hls_flags delete_segments "
        "-hls_segment_filename web/live_%03d.ts "
        "web/live.m3u8", "w"
    );
    if (!ffmpeg) {
        std::cerr << "Failed to open FFmpeg pipe.\n";
        return;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H.264 encoder not found\n";
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    codecCtx->width = frames[0].imageWidth;
    codecCtx->height = frames[0].imageHeight;
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx->time_base = AVRational{1, 30};
    codecCtx->framerate = AVRational{30, 1};
    codecCtx->bit_rate = 400000;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open encoder\n";
        return;
    }

    SwsContext* swsCtx = sws_getContext(
        codecCtx->width, codecCtx->height, AV_PIX_FMT_BGRA,
        codecCtx->width, codecCtx->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    AVFrame* yuvFrame = av_frame_alloc();
    yuvFrame->format = AV_PIX_FMT_YUV420P;
    yuvFrame->width = codecCtx->width;
    yuvFrame->height = codecCtx->height;
    av_frame_get_buffer(yuvFrame, 32);

    AVPacket* pkt = av_packet_alloc();
    int64_t pts = 0;

    for (const auto& frame : frames) {
        const uint8_t* srcSlice[] = { (const uint8_t*)frame.imageDataPointer };
        int srcStride[] = { 4 * frame.imageWidth };

        sws_scale(swsCtx, srcSlice, srcStride, 0, frame.imageHeight, yuvFrame->data, yuvFrame->linesize);
        yuvFrame->pts = pts++;

        if (avcodec_send_frame(codecCtx, yuvFrame) == 0) {
            while (avcodec_receive_packet(codecCtx, pkt) == 0) {
                pkt->pts = streamPts;
                pkt->dts = streamPts;
                pkt->duration = 3600; // 적절한 duration 값 (예: 25fps일 경우 3600 = 90kHz 기준)
                streamPts += pkt->duration;
        
                fwrite(pkt->data, 1, pkt->size, ffmpeg);
                fflush(ffmpeg);
                av_packet_unref(pkt);
            }
        }
    }

    avcodec_send_frame(codecCtx, nullptr);  // flush
    while (avcodec_receive_packet(codecCtx, pkt) == 0) {
        fwrite(pkt->data, 1, pkt->size, ffmpeg);
        fflush(ffmpeg);
        av_packet_unref(pkt);
    }

    // 정리
    pclose(ffmpeg);
    av_packet_free(&pkt);
    av_frame_free(&yuvFrame);
    sws_freeContext(swsCtx);
    avcodec_free_context(&codecCtx);

    std::cout << "[HLS] stream completed: web/live.m3u8\n";
}
