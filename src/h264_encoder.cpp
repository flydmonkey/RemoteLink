#include "remotelink/h264_encoder.hpp"

#include <libyuv/convert.h>
#include <wels/codec_api.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace remotelink {

H264Encoder::H264Encoder(std::uint32_t width, std::uint32_t height,
                         std::uint32_t fps, std::uint32_t bitrate)
    : fps_(fps), bitrate_(bitrate) {
    if (fps == 0) {
        throw std::invalid_argument("H.264 frame rate must be positive");
    }
    configure(width, height);
}

void H264Encoder::configure(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || (width & 1U) || (height & 1U)) {
        throw std::invalid_argument("H.264 dimensions must be positive and even");
    }
    shutdown();
    width_ = width;
    height_ = height;
    i420_.assign(static_cast<std::size_t>(width_) * height_ * 3 / 2, 0);

    if (WelsCreateSVCEncoder(&encoder_) != 0 || encoder_ == nullptr) {
        throw std::runtime_error("unable to create OpenH264 encoder");
    }

    SEncParamExt params {};
    if (encoder_->GetDefaultParams(&params) != cmResultSuccess) {
        shutdown();
        throw std::runtime_error("unable to obtain OpenH264 defaults");
    }

    params.iUsageType = SCREEN_CONTENT_REAL_TIME;
    params.iPicWidth = static_cast<int>(width);
    params.iPicHeight = static_cast<int>(height);
    params.iTargetBitrate = static_cast<int>(bitrate_);
    params.iRCMode = RC_BITRATE_MODE;
    params.fMaxFrameRate = static_cast<float>(fps_);
    params.iTemporalLayerNum = 1;
    params.iSpatialLayerNum = 1;
    const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
    const auto encoder_threads = std::min(4U, hardware_threads);
    params.iMultipleThreadIdc = static_cast<int>(encoder_threads);
    params.bEnableFrameSkip = false;
    params.uiIntraPeriod = fps_ * 2;
    params.sSpatialLayers[0].iVideoWidth = static_cast<int>(width);
    params.sSpatialLayers[0].iVideoHeight = static_cast<int>(height);
    params.sSpatialLayers[0].fFrameRate = static_cast<float>(fps_);
    params.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(bitrate_);
    params.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(bitrate_);
    params.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
    params.sSpatialLayers[0].sSliceArgument.uiSliceNum = encoder_threads;

    if (encoder_->InitializeExt(&params) != cmResultSuccess) {
        shutdown();
        throw std::runtime_error("unable to initialize OpenH264 encoder");
    }

    int format = videoFormatI420;
    encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &format);
}

H264Encoder::~H264Encoder() {
    shutdown();
}

void H264Encoder::shutdown() {
    if (encoder_ == nullptr) return;
    encoder_->Uninitialize();
    WelsDestroySVCEncoder(encoder_);
    encoder_ = nullptr;
}

void H264Encoder::request_key_frame() {
    key_frame_requested_ = true;
}

bool H264Encoder::set_bitrate(std::uint32_t bitrate) {
    std::lock_guard lock(mutex_);
    bitrate_ = bitrate;
    SBitrateInfo info{SPATIAL_LAYER_ALL, static_cast<int>(bitrate)};
    return encoder_ && encoder_->SetOption(ENCODER_OPTION_BITRATE, &info) == cmResultSuccess;
}

void H264Encoder::convert_bgra_to_i420(const Frame& frame) {
    auto* y_plane = i420_.data();
    auto* u_plane = y_plane + static_cast<std::size_t>(width_) * height_;
    auto* v_plane = u_plane + static_cast<std::size_t>(width_) * height_ / 4;

    const int result = libyuv::ARGBToI420(
        reinterpret_cast<const std::uint8_t*>(frame.pixels.data()),
        static_cast<int>(frame.stride),
        y_plane, static_cast<int>(width_),
        u_plane, static_cast<int>(width_ / 2),
        v_plane, static_cast<int>(width_ / 2),
        static_cast<int>(width_), static_cast<int>(height_));
    if (result != 0) throw std::runtime_error("BGRA to I420 conversion failed");
}

EncodedFrame H264Encoder::encode(const Frame& frame) {
    std::lock_guard lock(mutex_);
    if (frame.pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        throw std::invalid_argument("frame pixel buffer is smaller than its dimensions");
    }
    if (frame.width != width_ || frame.height != height_) {
        std::cerr << "RDP frame is " << frame.width << 'x' << frame.height
                  << "; recreating H.264 encoder\n";
        configure(frame.width, frame.height);
        key_frame_requested_ = true;
    }

    convert_bgra_to_i420(frame);
    if (key_frame_requested_.exchange(false)) {
        encoder_->ForceIntraFrame(true);
    }
    SSourcePicture picture {};
    picture.iColorFormat = videoFormatI420;
    picture.iPicWidth = static_cast<int>(width_);
    picture.iPicHeight = static_cast<int>(height_);
    picture.iStride[0] = static_cast<int>(width_);
    picture.iStride[1] = static_cast<int>(width_ / 2);
    picture.iStride[2] = static_cast<int>(width_ / 2);
    picture.pData[0] = i420_.data();
    picture.pData[1] = picture.pData[0] + static_cast<std::size_t>(width_) * height_;
    picture.pData[2] = picture.pData[1] + static_cast<std::size_t>(width_) * height_ / 4;
    picture.uiTimeStamp = frame.sequence * 1000 / fps_;

    SFrameBSInfo info {};
    if (encoder_->EncodeFrame(&picture, &info) != cmResultSuccess) {
        throw std::runtime_error("OpenH264 frame encoding failed");
    }

    EncodedFrame output;
    output.sequence = frame.sequence;
    output.key_frame = info.eFrameType == videoFrameTypeIDR;
    if (info.eFrameType == videoFrameTypeSkip) {
        return output;
    }

    for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo& layer_info = info.sLayerInfo[layer];
        int layer_size = 0;
        for (int nal = 0; nal < layer_info.iNalCount; ++nal) {
            layer_size += layer_info.pNalLengthInByte[nal];
        }
        const auto* begin = reinterpret_cast<const std::byte*>(layer_info.pBsBuf);
        output.annex_b.insert(output.annex_b.end(), begin, begin + layer_size);
    }
    return output;
}

}  // namespace remotelink
