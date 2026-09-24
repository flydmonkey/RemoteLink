#include "remote_gateway/h264_encoder.hpp"

#include <libyuv/convert.h>
#include <wels/codec_api.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace remote_gateway {

H264Encoder::H264Encoder(std::uint32_t width, std::uint32_t height,
                         std::uint32_t fps, std::uint32_t bitrate)
    : width_(width), height_(height), fps_(fps),
      i420_(static_cast<std::size_t>(width) * height * 3 / 2) {
    if (width == 0 || height == 0 || fps == 0 || (width & 1U) || (height & 1U)) {
        throw std::invalid_argument("H.264 dimensions and frame rate must be positive; dimensions must be even");
    }

    if (WelsCreateSVCEncoder(&encoder_) != 0 || encoder_ == nullptr) {
        throw std::runtime_error("unable to create OpenH264 encoder");
    }

    SEncParamExt params {};
    if (encoder_->GetDefaultParams(&params) != cmResultSuccess) {
        throw std::runtime_error("unable to obtain OpenH264 defaults");
    }

    params.iUsageType = SCREEN_CONTENT_REAL_TIME;
    params.iPicWidth = static_cast<int>(width);
    params.iPicHeight = static_cast<int>(height);
    params.iTargetBitrate = static_cast<int>(bitrate);
    params.iRCMode = RC_BITRATE_MODE;
    params.fMaxFrameRate = static_cast<float>(fps);
    params.iTemporalLayerNum = 1;
    params.iSpatialLayerNum = 1;
    const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
    const auto encoder_threads = std::min(4U, hardware_threads);
    params.iMultipleThreadIdc = static_cast<int>(encoder_threads);
    params.bEnableFrameSkip = false;
    params.uiIntraPeriod = fps * 2;
    params.sSpatialLayers[0].iVideoWidth = static_cast<int>(width);
    params.sSpatialLayers[0].iVideoHeight = static_cast<int>(height);
    params.sSpatialLayers[0].fFrameRate = static_cast<float>(fps);
    params.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(bitrate);
    params.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(bitrate);
    params.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
    params.sSpatialLayers[0].sSliceArgument.uiSliceNum = encoder_threads;

    if (encoder_->InitializeExt(&params) != cmResultSuccess) {
        WelsDestroySVCEncoder(encoder_);
        encoder_ = nullptr;
        throw std::runtime_error("unable to initialize OpenH264 encoder");
    }

    int format = videoFormatI420;
    encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &format);
}

H264Encoder::~H264Encoder() {
    if (encoder_ != nullptr) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
    }
}

void H264Encoder::request_key_frame() {
    key_frame_requested_ = true;
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
    if (frame.width != width_ || frame.height != height_ ||
        frame.pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) {
        throw std::invalid_argument("frame does not match encoder dimensions");
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

}  // namespace remote_gateway
