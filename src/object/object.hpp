#pragma once

#include <windows.h>

#include <plugin2.h>

namespace blur::object {
void Register(HOST_APP_TABLE* host);
void Unregister();
}  // namespace blur::object
