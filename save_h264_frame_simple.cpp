#include <iostream>
#include <vector>
#include <string>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "RcStruct.h"  // RFrame 정의 필요

void saveFramesAsH264(int cameraId, const std::vector<RFrame>& frames, const std::string& prefix) {
    if (frames.empty()) return;

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
        std::cerr << "Failed to open H.264 encoder\n";
        avcodec_free_context(&codecCtx);
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
    std::string fileName = prefix + ".h264";
    FILE* outFile = fopen(fileName.c_str(), "wb");
    if (!outFile) {
        std::cerr << "Failed to open output file: " << fileName << "\n";
        return;
    }

    int64_t pts = 0;
    for (const auto& frame : frames) {
        const uint8_t* srcSlice[] = { (const uint8_t*)frame.imageDataPointer };
        int srcStride[] = { 4 * frame.imageWidth };  // RGB32 = BGRA (4 bytes/pixel)

        sws_scale(swsCtx, srcSlice, srcStride, 0, frame.imageHeight, yuvFrame->data, yuvFrame->linesize);

        yuvFrame->pts = pts++;

        if (avcodec_send_frame(codecCtx, yuvFrame) == 0) {
            while (avcodec_receive_packet(codecCtx, pkt) == 0) {
                fwrite(pkt->data, 1, pkt->size, outFile);
                av_packet_unref(pkt);
            }
        }
    }

    // Flush encoder
    avcodec_send_frame(codecCtx, nullptr);
    while (avcodec_receive_packet(codecCtx, pkt) == 0) {
        fwrite(pkt->data, 1, pkt->size, outFile);
        av_packet_unref(pkt);
    }

    fclose(outFile);
    av_packet_free(&pkt);
    av_frame_free(&yuvFrame);
    sws_freeContext(swsCtx);
    avcodec_free_context(&codecCtx);

    std::cout << "Saved H.264 video: " << fileName << std::endl;
}
