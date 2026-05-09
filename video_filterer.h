#pragma once
#include "config.h"
#include "core_types.h"
#include "demuxer.h"
#include "side_aware.h"
#include "video_decoder.h"
#include "video_filter_context.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

class VideoFilterer : public SideAware {
 public:
  VideoFilterer(const Side& side,
                const Demuxer* demuxer,
                const VideoDecoder* video_decoder,
                const VideoFilterContext* video_filter_context);
  ~VideoFilterer();

  void init();
  void free();
  void reinit();

  void close_src();

  bool send(AVFrame* decoded_frame);
  bool receive(AVFrame* filtered_frame);

  std::string filter_description() const;

  size_t src_width() const;
  size_t src_height() const;
  AVPixelFormat src_pixel_format() const;
  size_t dest_width() const;
  size_t dest_height() const;
  AVPixelFormat dest_pixel_format() const;

 private:
  int init_filters(const AVCodecContext* dec_ctx, AVRational time_base);

  const Demuxer* demuxer_;
  const VideoDecoder* video_decoder_;

  std::string filter_description_;

  int width_;
  int height_;
  AVPixelFormat pixel_format_;
  AVColorSpace color_space_;
  AVColorRange color_range_;

  AVFilterContext* buffersrc_ctx_;
  AVFilterContext* buffersink_ctx_;
  AVFilterGraph* filter_graph_;
};
