#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include <cache2.h>
#include <logger2.h>
#include <plugin2.h>

namespace blur::aviutl {
namespace logger {
void Init(LOG_HANDLE* handle);

void Log(const std::wstring& msg);
void Debug(const std::wstring& msg);
void Info(const std::wstring& msg);
void Warning(const std::wstring& msg);
void Error(const std::wstring& msg);
}  // namespace logger

namespace context {
enum class EditorState : uint8_t {
    kEditing,
    kPlaying,
    kExporting,
};

void Init(EDIT_HANDLE* handle);

[[nodiscard]] EditorState CurrentEditorState();
}  // namespace context
}  // namespace blur::aviutl
