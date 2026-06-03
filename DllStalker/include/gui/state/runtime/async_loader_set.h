#pragma once

#include "pch.h"

#include <atomic>
#include <thread>

namespace Gui::State
{
// Bundle of async worker handles + their in-progress flags. Move-assigning
// to a jthread member triggers request_stop + join on the previous worker;
// this is how each StartXLoad path cancels its predecessor.
//
// Declaration order matters: jthreads MUST be the LAST members of any
// composing class so they're destroyed first (joined) before the data
// they capture by reference goes away.
struct AsyncLoaderSet
{
    std::atomic<bool> imageLoadInProgress      = false;
    std::atomic<bool> classLoadInProgress      = false;
    std::atomic<bool> inspectorLoadInProgress  = false;
    std::atomic<bool> fieldsLoadInProgress     = false;
    std::atomic<bool> instanceSearchInProgress = false;

    // Workers. Must remain last for safe RAII teardown.
    std::jthread imageLoadThread{};
    std::jthread classLoadThread{};
    std::jthread inspectorLoadThread{};
    std::jthread fieldsLoadThread{};
    std::jthread instanceSearchThread{};
};
} // namespace Gui::State
