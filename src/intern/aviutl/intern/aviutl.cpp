#include "../aviutl.hpp"

#include <intern/string.hpp>

namespace blur::aviutl {
namespace logger {
namespace {
LOG_HANDLE* handle = nullptr;
}

void Init(LOG_HANDLE* handle) { logger::handle = handle; }

void Log(const std::wstring& msg) { handle->log(handle, msg.c_str()); }

void Log(std::string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->log(handle, wstr->c_str());
    } else {
        handle->log(handle, L"Unable to log message: failed to convert string to wide string");
    }
}

void Log(std::u8string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->log(handle, wstr->c_str());
    } else {
        handle->log(handle, L"Unable to log message: failed to convert UTF-8 to wide string");
    }
}

void Debug(const std::wstring& msg) { handle->verbose(handle, msg.c_str()); }

void Debug(std::string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->verbose(handle, wstr->c_str());
    } else {
        handle->verbose(handle, L"Unable to log message: failed to convert string to wide string");
    }
}

void Debug(std::u8string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->verbose(handle, wstr->c_str());
    } else {
        handle->verbose(handle, L"Unable to log message: failed to convert UTF-8 to wide string");
    }
}

void Info(const std::wstring& msg) { handle->info(handle, msg.c_str()); }

void Info(std::string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->info(handle, wstr->c_str());
    } else {
        handle->info(handle, L"Unable to log message: failed to convert string to wide string");
    }
}

void Info(std::u8string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->info(handle, wstr->c_str());
    } else {
        handle->info(handle, L"Unable to log message: failed to convert UTF-8 to wide string");
    }
}

void Warning(const std::wstring& msg) { handle->warn(handle, msg.c_str()); }

void Warning(std::string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->warn(handle, wstr->c_str());
    } else {
        handle->warn(handle, L"Unable to log message: failed to convert string to wide string");
    }
}

void Warning(std::u8string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->warn(handle, wstr->c_str());
    } else {
        handle->warn(handle, L"Unable to log message: failed to convert UTF-8 to wide string");
    }
}

void Error(const std::wstring& msg) { handle->error(handle, msg.c_str()); }

void Error(std::string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->error(handle, wstr->c_str());
    } else {
        handle->error(handle, L"Unable to log message: failed to convert string to wide string");
    }
}

void Error(std::u8string_view msg) {
    if (const auto wstr = string::ToWString(msg); wstr.has_value()) {
        handle->error(handle, wstr->c_str());
    } else {
        handle->error(handle, L"Unable to log message: failed to convert UTF-8 to wide string");
    }
}
}  // namespace logger

namespace context {
namespace {
EDIT_HANDLE* handle = nullptr;
}

void Init(EDIT_HANDLE* handle) { context::handle = handle; }

EditorState GetEditorState() {
    switch (handle->get_edit_state()) {
        case EDIT_HANDLE::EDIT_STATE_EDIT:
            return EditorState::kIdle;
        case EDIT_HANDLE::EDIT_STATE_PLAY:
            return EditorState::kPlaying;
        case EDIT_HANDLE::EDIT_STATE_SAVE:
            return EditorState::kExporting;
        default:
            return EditorState::kIdle;
    }
}
}  // namespace context
}  // namespace blur::aviutl
