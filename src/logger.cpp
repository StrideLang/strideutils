#include "stride/utils/logger.h"
#include <chrono>
#include <iomanip>

namespace strd {

// By default, INFO goes to cout, WARNING and ERROR go to cerr, DEBUG goes to nullStream
std::ostream* Logger::debugOs = &Logger::nullStream;
std::ostream* Logger::infoOs = &std::cout;
std::ostream* Logger::warningOs = &std::cerr;
std::ostream* Logger::errorOs = &std::cerr;

NullStream Logger::nullStream;

void Logger::setDebugOutput(std::ostream* os) { debugOs = os; }
void Logger::setInfoOutput(std::ostream* os) { infoOs = os; }
void Logger::setWarningOutput(std::ostream* os) { warningOs = os; }
void Logger::setErrorOutput(std::ostream* os) { errorOs = os; }

Logger::LogStream::LogStream(LogLevel level, const char* file, int line) : level(level) {
    switch (level) {
        case LogLevel::DEBUG: os = debugOs; break;
        case LogLevel::INFO: os = infoOs; break;
        case LogLevel::WARNING: os = warningOs; break;
        case LogLevel::ERROR: os = errorOs; break;
    }

    if (os) {
        // Add timestamp
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        *os << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") 
            << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] ";
            
        // Add log level
        switch (level) {
            case LogLevel::DEBUG: *os << "[DEBUG] "; break;
            case LogLevel::INFO: *os << "[INFO] "; break;
            case LogLevel::WARNING: *os << "[WARN] "; break;
            case LogLevel::ERROR: *os << "[ERROR] "; break;
        }

        // Add file and line
        *os << "[" << file << ":" << line << "] ";
    }
}

Logger::LogStream::~LogStream() {
    if (os) {
        os->flush();
    }
}

Logger::LogStream Logger::log(LogLevel level, const char* file, int line) {
    return LogStream(level, file, line);
}

} // namespace strd
