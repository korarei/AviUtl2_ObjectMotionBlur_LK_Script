#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

#include <cache2.h>
#include <logger2.h>
#include <plugin2.h>

namespace blur::aviutl {
namespace logger {
void Init(LOG_HANDLE* log);

void Log(const std::wstring& msg);
void Log(std::string_view msg);
void Log(std::u8string_view msg);

void Debug(const std::wstring& msg);
void Debug(std::string_view msg);
void Debug(std::u8string_view msg);

void Info(const std::wstring& msg);
void Info(std::string_view msg);
void Info(std::u8string_view msg);

void Warning(const std::wstring& msg);
void Warning(std::string_view msg);
void Warning(std::u8string_view msg);

void Error(const std::wstring& msg);
void Error(std::string_view msg);
void Error(std::u8string_view msg);
}  // namespace logger

namespace context {
enum class EditorState : uint8_t {
    kIdle,
    kPlaying,
    kExporting,
};

void Init(EDIT_HANDLE* ctx);

[[nodiscard]] EditorState GetEditorState();
}  // namespace context
}  // namespace blur::aviutl
