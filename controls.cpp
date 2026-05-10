#include "controls.h"

static const std::vector<std::pair<std::string, std::string>> controls{{"H", "Toggle on-screen help text for controls"},
                                                                       {"V", "Toggle video info overlay"},
                                                                       {"Space", "Toggle play/pause"},
                                                                       {"Escape", "Quit"},
                                                                       {"Left arrow", "Seek 1 second backward"},
                                                                       {"Page down", "Seek 600 seconds backward"},
                                                                       {"Right arrow", "Seek 1 second forward"},
                                                                       {"R", "Re-center and reset zoom to 100% (x1)"},
                                                                       {"Right click", "Toggle hide/show HUD"},
                                                                       {"0", "Toggle video/subtraction mode"},
                                                                       {"U", "Toggle luminance-only subtraction mode"}};

static const std::vector<std::string> instructions{
    "Move the mouse horizontally to adjust the movable slider position.",
    "Use the mouse wheel to zoom in/out on the pixel under the cursor. Pan the view by moving the mouse while holding down the right button."};

const std::vector<std::pair<std::string, std::string>> get_controls() {
  return controls;
}

const std::vector<std::string> get_instructions() {
  return instructions;
}
