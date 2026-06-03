#pragma once

#include "pch.h"

namespace Gui::Views
{
struct CopyFeedbackState {
    char copiedMethodAddress[32] = {};
    float copiedAtSeconds = -1000.0f;
    char copiedFieldOffset[32] = {};
    float copiedFieldAtSeconds = -1000.0f;
};
} // namespace Gui::Views
