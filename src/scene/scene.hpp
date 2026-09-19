#pragma once

#include <windows.h>

#include <cstdint>  // IWYU pragma: keep

#include <plugin2.h>

namespace blur::scene {
void Register(HOST_APP_TABLE* host);
void Unregister();
}  // namespace blur::scene
