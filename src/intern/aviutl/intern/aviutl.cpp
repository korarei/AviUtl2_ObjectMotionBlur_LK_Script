#include "../aviutl.hpp"

namespace blur::aviutl {
void Logger::Init(LOG_HANDLE* logger) { Instance().logger_ = logger; }

void Logger::Log(const std::wstring& message) {
    auto* logger = Instance().logger_;

    if (logger != nullptr) {
        logger->log(logger, message.c_str());
    }
}

void Logger::Debug(const std::wstring& message) {
    auto* logger = Instance().logger_;

    if (logger != nullptr) {
        logger->verbose(logger, message.c_str());
    }
}

void Logger::Info(const std::wstring& message) {
    auto* logger = Instance().logger_;

    if (logger != nullptr) {
        logger->info(logger, message.c_str());
    }
}

void Logger::Warning(const std::wstring& message) {
    auto* logger = Instance().logger_;

    if (logger != nullptr) {
        logger->warn(logger, message.c_str());
    }
}

void Logger::Error(const std::wstring& message) {
    auto* logger = Instance().logger_;

    if (logger != nullptr) {
        logger->error(logger, message.c_str());
    }
}

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Context::Init(EDIT_HANDLE* context) { Instance().context_ = context; }

const EDIT_HANDLE* Context::handle() { return Instance().context_; }

Context& Context::Instance() {
    static Context inst;
    return inst;
}
}  // namespace blur::aviutl
