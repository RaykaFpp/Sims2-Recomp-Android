#pragma once

#include "core/input.h"

// scripted pad on port 0 (pad_script.cpp): SIMS2_STEP token bursts, or the
// SIMS2_AUTOSTART repeating Start / D-pad Up / A cycle. Only ever ORs bits in.
namespace padscript {

// app::GameInfo::pad helper: any guest thread, each poll. Completely inert
// when neither SIMS2_STEP nor SIMS2_AUTOSTART is set. Never clears what a
// real controller or the keyboard hook pressed.
void Apply(int port, input::Pad& pad);

}  // namespace padscript
