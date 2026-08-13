#include "../aviutl.hpp"

namespace blur::aviutl {
namespace logger {
namespace {
LOG_HANDLE* handle = nullptr;
}

void Init(LOG_HANDLE* handle) { logger::handle = handle; }

void Log(const std::wstring& msg) { handle->log(handle, msg.c_str()); }

void Debug(const std::wstring& msg) { handle->verbose(handle, msg.c_str()); }

void Info(const std::wstring& msg) { handle->info(handle, msg.c_str()); }

void Warning(const std::wstring& msg) { handle->warn(handle, msg.c_str()); }

void Error(const std::wstring& msg) { handle->error(handle, msg.c_str()); }
}  // namespace logger

namespace context {
namespace {
EDIT_HANDLE* handle = nullptr;
}

void Init(EDIT_HANDLE* handle) { context::handle = handle; }

EditorState CurrentEditorState() {
    switch (handle->get_edit_state()) {
        case EDIT_HANDLE::EDIT_STATE_EDIT:
            return EditorState::kEditing;
        case EDIT_HANDLE::EDIT_STATE_PLAY:
            return EditorState::kPlaying;
        case EDIT_HANDLE::EDIT_STATE_SAVE:
            return EditorState::kExporting;
        default:
            return EditorState::kEditing;
    }
}
}  // namespace context
}  // namespace blur::aviutl
