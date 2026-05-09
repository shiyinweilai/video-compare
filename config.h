#pragma once
#include <string>
#include <vector>
#include "core_types.h"
#include "display.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

struct TimeShiftConfig {
  AVRational multiplier{1, 1};
  int64_t offset_ms{0};
};


struct InputVideo {
  Side side;
  std::string side_description;

  std::string file_name;

  std::string demuxer;
  std::string decoder;
  std::string hw_accel_spec;

  AVDictionary* demuxer_options{nullptr};   // mutated by Demuxer
  AVDictionary* decoder_options{nullptr};   // mutated by VideoDecoder
  AVDictionary* hw_accel_options{nullptr};  // mutated by VideoDecoder
};

struct VideoCompareConfig {
  bool fit_window_to_usable_bounds{false};
  bool high_dpi_allowed{false};
  bool use_10_bpc{false};
  bool fast_input_alignment{false};
  bool bilinear_texture_filtering{false};

  int display_number{0};
  std::tuple<int, int> window_size{-1, -1};

  Display::Mode display_mode{Display::Mode::SPLIT};
  Display::Loop auto_loop_mode{Display::Loop::OFF};

  size_t frame_buffer_size{50};

  TimeShiftConfig time_shift;

  InputVideo left{LEFT, "Left"};
  std::vector<InputVideo> right_videos;

};