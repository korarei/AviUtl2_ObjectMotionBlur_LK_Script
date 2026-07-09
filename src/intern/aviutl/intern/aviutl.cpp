#include "../aviutl.hpp"

namespace blur::aviutl {
void Logger::Init(LOG_HANDLE* logger) { Instance().logger_ = logger; }

void Logger::Log(const std::wstring& message) {
    auto* logger = Instance().logger_;

    logger->log(logger, message.c_str());
}

void Logger::Debug(const std::wstring& message) {
    auto* logger = Instance().logger_;

    logger->verbose(logger, message.c_str());
}

void Logger::Info(const std::wstring& message) {
    auto* logger = Instance().logger_;

    logger->info(logger, message.c_str());
}

void Logger::Warning(const std::wstring& message) {
    auto* logger = Instance().logger_;

    logger->warn(logger, message.c_str());
}

void Logger::Error(const std::wstring& message) {
    auto* logger = Instance().logger_;

    logger->error(logger, message.c_str());
}

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Context::Init(EDIT_HANDLE* context) { Instance().context_ = context; }

Context::SessionState Context::CurrentSessionState() {
    switch (Instance().context_->get_edit_state()) {
        case EDIT_HANDLE::EDIT_STATE_EDIT:
            return SessionState::kEditing;
        case EDIT_HANDLE::EDIT_STATE_PLAY:
            return SessionState::kPlaying;
        case EDIT_HANDLE::EDIT_STATE_SAVE:
            return SessionState::kRendering;
        default:
            return SessionState::kEditing;
    }
}

Context& Context::Instance() {
    static Context inst;
    return inst;
}
}  // namespace blur::aviutl
