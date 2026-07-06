#include <windows.h>

#include <logger2.h>
#include <plugin2.h>

#include "intern/aviutl/aviutl.hpp"

#include "object/object.hpp"

#include "api.h"

#ifndef VERSION
#define VERSION L"0.1.0"
#endif

#ifndef REQUIRES_AVIUTL2
#define REQUIRES_AVIUTL2 2000100u
#endif

namespace {
constinit COMMON_PLUGIN_TABLE info = {
    .name = L"MotionBlur_K Hub",
    .information = L"MotionBlur_K Hub v" VERSION L" by Korarei",
};
}  // namespace

extern "C" {
API DWORD RequiredVersion() { return REQUIRES_AVIUTL2; }

API void InitializeLogger(LOG_HANDLE* logger) { blur::aviutl::Logger::Init(logger); }

API bool InitializePlugin(DWORD version) { return version >= RequiredVersion(); }

API void UninitializePlugin() { blur::object::Deinit(); }

API COMMON_PLUGIN_TABLE* GetCommonPluginTable() { return &info; }

API void RegisterPlugin(HOST_APP_TABLE* host) {
    blur::aviutl::Context::Init(host->create_edit_handle());

    blur::object::Init(host);
}
}
