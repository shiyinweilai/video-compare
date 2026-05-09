#define SDL_MAIN_HANDLED
#include <iostream>
#include <regex>
#include <stdexcept>
#include <vector>
#include "argagg.h"
#include "side_aware_logger.h"
#include "video_compare.h"

#ifdef _WIN32
#include <Windows.h>
#undef RELATIVE

void enable_windows_dpi_awareness() {
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
        {"display-mode", {"-m", "--mode"}, "display mode: 'split' (default), 'vstack', 'hstack'", 1},
        {"window-size", {"-w", "--window-size"}, "override window size, e.g. 800x600, 1280x or x480", 1},
        {"window-fit-display", {"-W", "--window-fit-display"}, "fit window to usable display bounds", 0},
    }};

    argagg::parser_results args;
    args = argparser.parse(argc, argv_decoded);

    if (args.count() == 0) {
      std::cerr << "Usage: video-compare [OPTIONS]... FILE1 FILE2" << std::endl;
    } else {
      VideoCompareConfig config;

      if (args.pos.size() < 2) {
        throw std::logic_error{"Two FFmpeg compatible video files must be supplied"};
      }

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

      if (args["display-mode"]) {
        const std::string display_mode_arg = args["display-mode"];

        if (display_mode_arg == "split") {
          config.display_mode = Display::Mode::SPLIT;
        } else if (display_mode_arg == "vstack") {
          config.display_mode = Display::Mode::VSTACK;
        } else if (display_mode_arg == "hstack") {
          config.display_mode = Display::Mode::HSTACK;
        } else {
          throw std::logic_error{"Invalid display mode (valid: split, vstack, hstack)"};
        }
      }

      if (args["window-size"]) {
        if (config.fit_window_to_usable_bounds) {
          throw std::logic_error{"--window-size and --window-fit-display cannot be used together"};
        }

        const std::string window_size_arg = args["window-size"];
        const std::regex window_size_re("(\\d*)x(\\d*)");

        if (!std::regex_match(window_size_arg, window_size_re)) {
          throw std::logic_error{"Invalid window size format (e.g. 800x600, 1280x or x480)"};
        }

        const std::regex delimiter_re("x");
        auto const token_vec = std::vector<std::string>(
            std::sregex_token_iterator{begin(window_size_arg), end(window_size_arg), delimiter_re, -1},
            std::sregex_token_iterator{});

        config.window_size = std::make_tuple(
            !token_vec[0].empty() ? std::stoi(token_vec[0]) : -1,
            token_vec.size() == 2 ? std::stoi(token_vec[1]) : -1);
      }

      config.left.file_name = args.pos[0];

      InputVideo right_video{RIGHT, "Right"};
      right_video.file_name = args.pos[1];
      config.right_videos.push_back(right_video);

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
