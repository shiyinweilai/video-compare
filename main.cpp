#define SDL_MAIN_HANDLED
#include <algorithm>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <vector>
#include "argagg.h"
#include "side_aware_logger.h"
#include "string_utils.h"
#include "version.h"
#include "video_compare.h"


#ifdef _WIN32
#include <Windows.h>
#undef RELATIVE

void enable_windows_dpi_awareness() {
  // Try SetProcessDpiAwarenessContext (Windows 10 v1703+)
  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
  HMODULE user32 = LoadLibraryA("user32.dll");
  if (user32) {
    typedef BOOL(WINAPI * SetProcessDpiAwarenessContextProc)(HANDLE);
    SetProcessDpiAwarenessContextProc set_dpi_context =
        (SetProcessDpiAwarenessContextProc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");

    if (set_dpi_context) {
      if (set_dpi_context((HANDLE)-4)) {
        FreeLibrary(user32);
        return;
      }
    }
    FreeLibrary(user32);
  }

  // Try SetProcessDpiAwareness (Windows 8.1+)
  // PROCESS_PER_MONITOR_DPI_AWARE = 2
  HMODULE shcore = LoadLibraryA("shcore.dll");
  if (shcore) {
    typedef HRESULT(WINAPI * SetProcessDpiAwarenessProc)(int);
    SetProcessDpiAwarenessProc set_dpi_awareness =
        (SetProcessDpiAwarenessProc)GetProcAddress(shcore, "SetProcessDpiAwareness");

    if (set_dpi_awareness) {
      if (SUCCEEDED(set_dpi_awareness(2))) {
        FreeLibrary(shcore);
        return;
      }
    }
    FreeLibrary(shcore);
  }

  // Fallback to SetProcessDPIAware (Vista+)
  if (user32 = LoadLibraryA("user32.dll")) {
    typedef BOOL(WINAPI * SetProcessDPIAwareProc)();
    SetProcessDPIAwareProc set_dpi_aware =
        (SetProcessDPIAwareProc)GetProcAddress(user32, "SetProcessDPIAware");

    if (set_dpi_aware) {
      set_dpi_aware();
    }
    FreeLibrary(user32);
  }
}

// Credits to Mircea Neacsu, https://github.com/neacsum/utf8
char** get_argv(int* argc, char** argv) {
  char** uargv = nullptr;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), argc);
  if (wargv) {
    uargv = new char*[*argc];
    for (int i = 0; i < *argc; i++) {
      int nc = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
      uargv[i] = new char[nc + 1];
      WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, uargv[i], nc, 0, 0);
    }
    LocalFree(wargv);
  }
  return uargv;
}

void free_argv(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    delete argv[i];
  }
  delete argv;
}
#else
#define UNUSED(x) (void)(x)

char** get_argv(const int* argc, char** argv) {
  UNUSED(argc);
  return argv;
}

void free_argv(int argc, char** argv) {
  UNUSED(argc);
  UNUSED(argv);
}
#endif



TimeShiftConfig parse_time_shift(const std::string& time_shift_arg) {
  TimeShiftConfig config;

  // Check if it's a simple number or time format, treat it as an offset
  try {
    const double offset = parse_timestamps_to_seconds(time_shift_arg);
    config.offset_ms = static_cast<int64_t>(offset * 1000.0);
    return config;
  } catch (const std::logic_error& e) {
    // If parsing as time format fails, continue with multiplier parsing
  }

  // Parse new format: [xmultiplier][+/-offset]
  std::string remaining = time_shift_arg;

  // Parse multiplier if present
  if (remaining[0] == 'x') {
    remaining = remaining.substr(1);  // Remove 'x'

    // Find the end of the multiplier (either end of string or start of offset)
    size_t multiplier_end = remaining.find_first_of("+-");
    if (multiplier_end == std::string::npos) {
      multiplier_end = remaining.length();
    }

    const std::string multiplier_str = remaining.substr(0, multiplier_end);
    remaining = remaining.substr(multiplier_end);

    // Check if it's a rational fraction (e.g., "25/24")
    const std::regex number_re("^([0-9]+([.][0-9]*)?|[.][0-9]+)$");

    size_t slash_pos = multiplier_str.find('/');
    if (slash_pos != std::string::npos) {
      std::string numerator_str = multiplier_str.substr(0, slash_pos);
      std::string denominator_str = multiplier_str.substr(slash_pos + 1);

      if (!std::regex_match(numerator_str, number_re) || !std::regex_match(denominator_str, number_re)) {
        throw std::logic_error{"Cannot parse time shift multiplier; numerator and denominator must be valid postive numbers"};
      }

      const double numerator = parse_strict_double(numerator_str);
      const double denominator = parse_strict_double(denominator_str);

      if (denominator == 0) {
        throw std::logic_error{"Cannot parse time shift multiplier; denominator cannot be zero"};
      }

      av_reduce(&config.multiplier.num, &config.multiplier.den, std::round(numerator * 10000), std::round(denominator * 10000), 1000000);
    } else {
      if (!std::regex_match(multiplier_str, number_re)) {
        throw std::logic_error{"Cannot parse time shift multiplier; must be a valid positive number"};
      }

      config.multiplier = av_d2q(parse_strict_double(multiplier_str), 1000000);
    }

    // Prevent division by zero in inverse multiplier
    if (config.multiplier.num == 0) {
      throw std::runtime_error("Multiplier cannot be zero");
    }
  }

  // Parse offset if present
  if (!remaining.empty()) {
    try {
      double offset = parse_timestamps_to_seconds(remaining);
      config.offset_ms = static_cast<int64_t>(offset * 1000.0);
    } catch (const std::logic_error& e) {
      throw std::logic_error{"Cannot parse time shift offset: " + std::string(e.what())};
    }
  }

  return config;
}

const std::string get_nth_token_or_empty(const std::string& options_string, const char delimiter, const size_t n) {
  auto tokens = string_split(options_string, delimiter);

  return tokens.size() > n ? tokens[n] : "";
}

AVDictionary* upsert_avdict_options(AVDictionary* dict, const std::string& options_string) {
  auto options = string_split(options_string, ',');

  for (auto option : options) {
    auto key_value_pair = string_split(option, '=');

    if (key_value_pair.size() != 2) {
      throw std::logic_error{"key=value expected for option"};
    }

    av_dict_set(&dict, key_value_pair[0].c_str(), key_value_pair[1].c_str(), 0);
  }

  return dict;
}

AVDictionary* create_default_demuxer_options() {
  AVDictionary* demuxer_options = nullptr;
  av_dict_set(&demuxer_options, "analyzeduration", "100000000", 0);
  av_dict_set(&demuxer_options, "probesize", "100000000", 0);

  return demuxer_options;
}

static const std::string PLACEHOLDER("__");
static const std::regex PLACEHOLDER_REGEX(PLACEHOLDER);

static inline bool contains_placeholder(const std::string& str) {
  return str.find(PLACEHOLDER) != std::string::npos;
}

std::string safe_replace_placeholder(const std::string& template_str, const std::string& replacement, const std::string& type) {
  if (contains_placeholder(template_str) && contains_placeholder(replacement)) {
    throw std::logic_error{"Unable to replace placeholder in " + type + ": replacement contains an unresolved placeholder."};
  }

  return replacement.empty() ? template_str : std::regex_replace(template_str, PLACEHOLDER_REGEX, replacement, std::regex_constants::format_first_only);
}

void resolve_mutual_placeholders(std::string& left, std::string& right, const std::string& type, bool only_replace_full_placeholder = false) {
  if ((contains_placeholder(left) && right.empty()) || (left.empty() && contains_placeholder(right))) {
    throw std::logic_error{"Cannot resolve placeholder in " + type + ": the other is empty and cannot be substituted."};
  }

  if (only_replace_full_placeholder) {
    if ((left == PLACEHOLDER) && (right == PLACEHOLDER)) {
      throw std::logic_error{"Cannot resolve placeholder in " + type + ": the other is also a placeholder."};
    } else if (left == PLACEHOLDER) {
      left = right;
    } else if (right == PLACEHOLDER) {
      right = left;
    }
  } else {
    if (contains_placeholder(left)) {
      left = safe_replace_placeholder(left, right, type);
    } else if (contains_placeholder(right)) {
      right = safe_replace_placeholder(right, left, type);
    }
  }
}



// Parse an FFmpeg parameter spec string (format: "name[:options]" or "name:device:options" for hwaccel)
// Returns the main value and sets options in the provided AVDictionary
// For hwaccel with join_tokens_0_and_1=true, joins tokens 0 and 1 as the main value
std::string parse_ffmpeg_param_spec(const std::string& spec, const std::string& template_spec, AVDictionary*& options, const std::string& type_name, int options_token_idx, bool use_default_demuxer_opts, bool join_tokens_0_and_1) {
  std::string result = safe_replace_placeholder(spec, template_spec, type_name);
  AVDictionary* base_dict = options;
  if (!base_dict && use_default_demuxer_opts) {
    base_dict = create_default_demuxer_options();
  }
  options = upsert_avdict_options(base_dict, get_nth_token_or_empty(result, ':', options_token_idx));
  if (join_tokens_0_and_1) {
    return string_join({get_nth_token_or_empty(result, ':', 0), get_nth_token_or_empty(result, ':', 1)}, ":");
  } else {
    return get_nth_token_or_empty(result, ':', 0);
  }
}

// Parse right video specification with :: separator
// Format: filename[::key=value[::key=value...]]
struct RightVideoSpec {
  std::string file_name;
  std::map<std::string, std::string> params;
};

RightVideoSpec parse_right_video_spec(const std::string& spec) {
  RightVideoSpec result;
  size_t pos = spec.find("::");

  if (pos == std::string::npos) {
    result.file_name = spec;
    return result;
  }

  result.file_name = spec.substr(0, pos);
  size_t start = pos + 2;

  while (start < spec.length()) {
    size_t next_sep = spec.find("::", start);
    std::string part = (next_sep == std::string::npos) ? spec.substr(start) : spec.substr(start, next_sep - start);
    start = (next_sep == std::string::npos) ? spec.length() : next_sep + 2;

    size_t eq_pos = part.find('=');
    if (eq_pos != std::string::npos) {
      result.params[part.substr(0, eq_pos)] = part.substr(eq_pos + 1);
    } else if (!part.empty()) {
      result.params[part] = "";
    }
  }

  return result;
}

void apply_right_video_spec(InputVideo& video, const RightVideoSpec& spec, const InputVideo& template_video) {
  auto get_param = [&](const std::string& key) -> const std::string* {
    auto it = spec.params.find(key);
    return (it != spec.params.end()) ? &it->second : nullptr;
  };

  if (const std::string* val = get_param("decoder")) {
    video.decoder = parse_ffmpeg_param_spec(*val, template_video.decoder, video.decoder_options, "decoder", 1, false, false);
  }
  if (const std::string* val = get_param("demuxer")) {
    video.demuxer = parse_ffmpeg_param_spec(*val, template_video.demuxer, video.demuxer_options, "demuxer", 1, true, false);
  }
  if (const std::string* val = get_param("hwaccel")) {
    video.hw_accel_spec = parse_ffmpeg_param_spec(*val, template_video.hw_accel_spec, video.hw_accel_options, "hardware acceleration", 2, false, true);
  }

}

int main(int argc, char** argv) {
#ifdef _WIN32
  enable_windows_dpi_awareness();
#endif

  char** argv_decoded = get_argv(&argc, argv);
  int exit_code = 0;

#if (LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 6, 102))
  av_register_all();
  avcodec_register_all();
#endif

  try {
    argagg::parser argparser{{
        {"high-dpi", {"-d", "--high-dpi"}, "allow high DPI mode for e.g. displaying UHD content on Retina displays", 0},
         {"no-high-dpi", {"--no-high-dpi"}, "disable high DPI mode (default on non-macOS systems)", 0},
         {"10-bpc", {"-b", "--10-bpc"}, "use 10 bits per color component instead of 8", 0},
         {"fast-alignment", {"-F", "--fast-alignment"}, "toggle fast bilinear scaling for aligning input source resolutions, replacing high-quality bicubic and chroma-accurate interpolation", 0},
         {"bilinear-texture", {"-I", "--bilinear-texture"}, "toggle bilinear video texture interpolation, replacing nearest-neighbor filtering", 0},
         {"display-number", {"-n", "--display-number"}, "open main window on specific display (e.g. 0, 1 or 2), default is 0", 1},
         {"display-mode", {"-m", "--mode"}, "display mode (layout), 'split' for split screen (default), 'vstack' for vertical stack, 'hstack' for horizontal stack", 1},
         {"window-size", {"-w", "--window-size"}, "override window size, specified as [width]x[height] (e.g. 800x600, 1280x or x480)", 1},
         {"window-fit-display", {"-W", "--window-fit-display"}, "calculate the window size to fit within the usable display bounds while maintaining the video aspect ratio", 0},
         {"auto-loop-mode", {"-a", "--auto-loop-mode"}, "auto-loop playback when buffer fills, 'off' for continuous streaming (default), 'on' for forward-only mode, 'pp' for ping-pong mode", 1},
         {"frame-buffer-size", {"-f", "--frame-buffer-size"}, "frame buffer size (e.g. 10, 70 or 150), default is 50", 1},
         {"time-shift", {"-t", "--time-shift"}, "shift the time stamps of the right video by a user-specified time offset, optionally with a multiplier (e.g. 0.150, -0.1, x1.04+0.1, x25.025/24-1:30.5)", 1},
         {"demuxer", {"--demuxer"}, "left FFmpeg video demuxer name for both sides, specified as [type?][:options?] (e.g. 'rawvideo:pixel_format=rgb24,video_size=320x240,framerate=10')", 1},
         {"left-demuxer", {"--left-demuxer"}, "left FFmpeg video demuxer name, specified as [type?][:options?]", 1},
         {"right-demuxer", {"--right-demuxer"}, "right FFmpeg video demuxer name, specified as [type?][:options?]", 1},
         {"decoder", {"--decoder"}, "FFmpeg video decoder name for both sides, specified as [type?][:options?] (e.g. ':strict=unofficial', ':strict=-2' or 'vvc:strict=experimental')", 1},
         {"left-decoder", {"--left-decoder"}, "left FFmpeg video decoder name, specified as [type?][:options?] (e.g. ':strict=-2,trust_dec_pts=1' or 'h264:trust_dec_pts=1')", 1},
         {"right-decoder", {"--right-decoder"}, "right FFmpeg video decoder name, specified as [type?][:options?]", 1},
         {"hwaccel", {"--hwaccel"}, "FFmpeg video hardware acceleration for both sides, specified as [type][:device?[:options?]] (e.g. 'videotoolbox' or 'vaapi:/dev/dri/renderD128')", 1},
         {"left-hwaccel", {"--left-hwaccel"}, "left FFmpeg video hardware acceleration, specified as [type][:device?[:options?]] (e.g. 'cuda', 'cuda:1' or 'vulkan')", 1},
         {"right-hwaccel", {"--right-hwaccel"}, "right FFmpeg video hardware acceleration, specified as [type][:device?[:options?]]", 1}}};

    argagg::parser_results args;
    args = argparser.parse(argc, argv_decoded);

    if (args.count() == 0) {
      std::cerr << "Usage: video-compare [OPTIONS]... FILE1 FILE2 [FILE3] [FILE4] ..." << std::endl;
    } else {
      VideoCompareConfig config;

      if (args.pos.size() < 2) {
        throw std::logic_error{"Two or more FFmpeg compatible video files must be supplied (left and at least one right)"};
      }

      // Create a temporary right video for parsing command-line options
      // This will be used as a template for all right videos
      InputVideo right_template{RIGHT, "Right"};

      config.fit_window_to_usable_bounds = args["window-fit-display"];
      
      if (args["high-dpi"]) {
        config.high_dpi_allowed = true;
      } else if (args["no-high-dpi"]) {
        config.high_dpi_allowed = false;
      } else {
#if defined(__APPLE__) || defined(_WIN32)
        config.high_dpi_allowed = true;
#else
        config.high_dpi_allowed = false;
#endif
      }

      config.use_10_bpc = args["10-bpc"];
      config.fast_input_alignment = args["fast-alignment"];
      config.bilinear_texture_filtering = args["bilinear-texture"];

      if (args["display-number"]) {
        const std::string display_number_arg = args["display-number"];
        const std::regex display_number_re("(\\d*)");

        if (!std::regex_match(display_number_arg, display_number_re)) {
          throw std::logic_error{"Cannot parse display number argument (required format: [number], e.g. 0, 1 or 2)"};
        }

        config.display_number = std::stoi(display_number_arg);
      }
      if (args["display-mode"]) {
        const std::string display_mode_arg = args["display-mode"];

        if (display_mode_arg == "split") {
          config.display_mode = Display::Mode::SPLIT;
        } else if (display_mode_arg == "vstack") {
          config.display_mode = Display::Mode::VSTACK;
        } else if (display_mode_arg == "hstack") {
          config.display_mode = Display::Mode::HSTACK;
        } else {
          throw std::logic_error{"Cannot parse display mode argument (valid options: split, vstack, hstack)"};
        }
      }
      if (args["window-size"]) {
        if (config.fit_window_to_usable_bounds) {
          throw std::logic_error{"Options --window-size and --window-fit-display cannot be used together"};
        }

        const std::string window_size_arg = args["window-size"];
        const std::regex window_size_re("(\\d*)x(\\d*)");

        if (!std::regex_match(window_size_arg, window_size_re)) {
          throw std::logic_error{"Cannot parse window size argument (required format: [width]x[height], e.g. 800x600, 1280x or x480)"};
        }

        const std::regex delimiter_re("x");

        auto const token_vec = std::vector<std::string>(std::sregex_token_iterator{begin(window_size_arg), end(window_size_arg), delimiter_re, -1}, std::sregex_token_iterator{});

        config.window_size = std::make_tuple(!token_vec[0].empty() ? std::stoi(token_vec[0]) : -1, token_vec.size() == 2 ? std::stoi(token_vec[1]) : -1);
      }

      if (args["auto-loop-mode"]) {
        const std::string auto_loop_mode_arg = args["auto-loop-mode"];

        if (auto_loop_mode_arg == "off") {
          config.auto_loop_mode = Display::Loop::OFF;
        } else if (auto_loop_mode_arg == "on") {
          config.auto_loop_mode = Display::Loop::FORWARDONLY;
        } else if (auto_loop_mode_arg == "pp") {
          config.auto_loop_mode = Display::Loop::PINGPONG;
        } else {
          throw std::logic_error{"Cannot parse auto loop mode argument (valid options: off, on, pp)"};
        }
      }
      if (args["frame-buffer-size"]) {
        const std::string frame_buffer_size_arg = args["frame-buffer-size"];
        const std::regex frame_buffer_size_re("(\\d*)");

        if (!std::regex_match(frame_buffer_size_arg, frame_buffer_size_re)) {
          throw std::logic_error{"Cannot parse frame buffer size (required format: [number], e.g. 10, 70 or 150)"};
        }

        config.frame_buffer_size = std::stoi(frame_buffer_size_arg);

        if (config.frame_buffer_size < 1) {
          throw std::logic_error{"Frame buffer size must be at least 1"};
        }
      }
      if (args["time-shift"]) {
        const std::string time_shift_arg = args["time-shift"];

        try {
          const TimeShiftConfig time_shift_config = parse_time_shift(time_shift_arg);
          config.time_shift = time_shift_config;

          const double multiplier_value = av_q2d(time_shift_config.multiplier);
          std::cout << string_sprintf("Timeshift config: multiplier=%d/%d (x%.6f), offset=%ld ms", time_shift_config.multiplier.num, time_shift_config.multiplier.den, multiplier_value, time_shift_config.offset_ms) << std::endl;
        } catch (const std::logic_error& e) {
          throw std::logic_error{"Cannot parse time shift argument: " + std::string(e.what())};
        }
      }
      // demuxer
      config.left.demuxer_options = create_default_demuxer_options();
      right_template.demuxer_options = create_default_demuxer_options();

      if (args["demuxer"]) {
        config.left.demuxer = static_cast<const std::string&>(args["demuxer"]);
        right_template.demuxer = static_cast<const std::string&>(args["demuxer"]);
      }
      if (args["left-demuxer"]) {
        config.left.demuxer = safe_replace_placeholder(static_cast<const std::string&>(args["left-demuxer"]), config.left.demuxer, "demuxer");
      }
      if (args["right-demuxer"]) {
        right_template.demuxer = safe_replace_placeholder(static_cast<const std::string&>(args["right-demuxer"]), right_template.demuxer, "demuxer");
      }
      resolve_mutual_placeholders(config.left.demuxer, right_template.demuxer, "demuxer");

      config.left.demuxer = parse_ffmpeg_param_spec(config.left.demuxer, "", config.left.demuxer_options, "demuxer", 1, false, false);
      right_template.demuxer = parse_ffmpeg_param_spec(right_template.demuxer, "", right_template.demuxer_options, "demuxer", 1, false, false);

      // decder
      if (args["decoder"]) {
        config.left.decoder = static_cast<const std::string&>(args["decoder"]);
        right_template.decoder = static_cast<const std::string&>(args["decoder"]);
      }
      if (args["left-decoder"]) {
        config.left.decoder = safe_replace_placeholder(static_cast<const std::string&>(args["left-decoder"]), config.left.decoder, "decoder");
      }
      if (args["right-decoder"]) {
        right_template.decoder = safe_replace_placeholder(static_cast<const std::string&>(args["right-decoder"]), right_template.decoder, "decoder");
      }
      resolve_mutual_placeholders(config.left.decoder, right_template.decoder, "decoder");

      config.left.decoder = parse_ffmpeg_param_spec(config.left.decoder, "", config.left.decoder_options, "decoder", 1, false, false);
      right_template.decoder = parse_ffmpeg_param_spec(right_template.decoder, "", right_template.decoder_options, "decoder", 1, false, false);

      // HW acceleration
      if (args["hwaccel"]) {
        config.left.hw_accel_spec = static_cast<const std::string&>(args["hwaccel"]);
        right_template.hw_accel_spec = static_cast<const std::string&>(args["hwaccel"]);
      }
      if (args["left-hwaccel"]) {
        config.left.hw_accel_spec = safe_replace_placeholder(static_cast<const std::string&>(args["left-hwaccel"]), config.left.hw_accel_spec, "hardware acceleration");
      }
      if (args["right-hwaccel"]) {
        right_template.hw_accel_spec = safe_replace_placeholder(static_cast<const std::string&>(args["right-hwaccel"]), right_template.hw_accel_spec, "hardware acceleration");
      }
      resolve_mutual_placeholders(config.left.hw_accel_spec, right_template.hw_accel_spec, "hardware acceleration");

      config.left.hw_accel_spec = parse_ffmpeg_param_spec(config.left.hw_accel_spec, "", config.left.hw_accel_options, "hardware acceleration", 2, false, true);
      right_template.hw_accel_spec = parse_ffmpeg_param_spec(right_template.hw_accel_spec, "", right_template.hw_accel_options, "hardware acceleration", 2, false, true);

      config.left.file_name = args.pos[0];

      // Parse multiple right videos
      // right_template already has all the parsed options
      right_template.file_name = args.pos[1];

      // Resolve placeholders for first right video
      resolve_mutual_placeholders(config.left.file_name, right_template.file_name, "video file", true);

      // Create right videos from all remaining file arguments
      for (size_t i = 1; i < args.pos.size(); ++i) {
        RightVideoSpec spec = parse_right_video_spec(args.pos[i]);

        InputVideo right_video = right_template;
        right_video.file_name = spec.file_name;
        right_video.side = Side::Right(static_cast<size_t>(i - 1));
        right_video.side_description = i == 1 ? "Right" : "Right" + std::to_string(i);
        right_video.demuxer_options = nullptr;
        av_dict_copy(&right_video.demuxer_options, right_template.demuxer_options, 0);
        right_video.decoder_options = nullptr;
        av_dict_copy(&right_video.decoder_options, right_template.decoder_options, 0);
        right_video.hw_accel_options = nullptr;
        av_dict_copy(&right_video.hw_accel_options, right_template.hw_accel_options, 0);

        apply_right_video_spec(right_video, spec, right_template);

        // Resolve placeholders for this right video
        std::string left_file = config.left.file_name;
        resolve_mutual_placeholders(left_file, right_video.file_name, "video file", true);

        config.right_videos.push_back(right_video);
      }


      av_log_set_callback(sa_av_log_callback);

      VideoCompare compare{config};
      compare();
    }
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    exit_code = -1;
  }

  free_argv(argc, argv_decoded);

  return exit_code;
}
