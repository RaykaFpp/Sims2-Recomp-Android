#include "app/app.h"
#include "common/input_kbm.h"
#include "common/overlay.h"
#include "common/pad_script.h"

namespace {

// every polled pad: the scripted pad first (pad_script.cpp), then the keyboard and mouse
// (input_kbm.cpp). Both only ever add to what a real controller reported.
void PadHook(int port, input::Pad& pad) {
    padscript::Apply(port, pad);
    kbm::Apply(port, pad);
}

}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Sims 2",               // window and splash
    .short_title = L"The Sims 2",         // message boxes
    .name = L"Sims2",                     // window classes, crash dumps, handover event
    .log = "sims2",                       // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its DATA and movies folders beside it.",
    .images = {{"default.xbe", "Sims2"}}, // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = &kbm::Message,             // keyboard and mouse tracking; never takes a message
    .ui = &overlay::Draw,                 // ImGui drawing each frame: F1 settings, F2 performance
    .pad = &PadHook,                      // scripted pad, then keyboard and mouse as port 0
    .rumble = nullptr,                    // changes a rumble before it is set
    // The game creates a 640x480 back buffer (Direct3D_CreateDevice at 0x270AB0, called from
    // sub_001FB080 at 0x1FB1B8, parameters built in sub_001C33D0) but draws a 640x448 picture
    // centred in it: picture size fields at display+12/+16 stored at 0x1C350A/0x1C3594, centre
    // offsets (0.0, 16.0) = (backbuffer - picture) / 2, the scissor shift in sub_001FD6D0 at
    // 0x1FD931, the 2D screen matrix built from the picture size in sub_001FB3F0 at
    // 0x1FBBC7/0x1FBBCF, and the UI reciprocals 1/640 and 1/448 at 0x316478/0x316474
    // (docs/analysis-video-mode.md). A TV overscanned the 16 dead rows at the top and bottom
    // away; a PC window would show them as black bars, so the host presents rows 16 to 463 only.
    .picture_top = 16,                    // first presented row
    .picture_height = 448,                // presented rows
};
