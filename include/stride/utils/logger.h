#pragma once

#include <iostream>
#include <string>

namespace strd {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};

class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(&buf) {}
private:
    NullBuffer buf;
};

class Logger {
public:
    static void setDebugOutput(std::ostream* os);
    static void setInfoOutput(std::ostream* os);
    static void setWarningOutput(std::ostream* os);
    static void setErrorOutput(std::ostream* os);

    class LogStream {
    public:
        LogStream(LogLevel level, const char* file, int line);
        ~LogStream();

        template <typename T>
        LogStream& operator<<(const T& value) {
            if (os) {
                *os << value;
            }
            return *this;
        }

        // Support for stream manipulators like std::endl
        typedef std::ostream& (*OstreamManipulator)(std::ostream&);
        LogStream& operator<<(OstreamManipulator manip) {
            if (os) {
                *os << manip;
            }
            return *this;
        }

    private:
        std::ostream* os;
        LogLevel level;
    };

    static LogStream log(LogLevel level, const char* file, int line);
    
    // Instance of a null stream for discarding output
    static NullStream nullStream;

private:
    static std::ostream* debugOs;
    static std::ostream* infoOs;
    static std::ostream* warningOs;
    static std::ostream* errorOs;
};

} // namespace strd

#define LOG_DEBUG() strd::Logger::log(strd::LogLevel::DEBUG, __FILE__, __LINE__)
#define LOG_INFO() strd::Logger::log(strd::LogLevel::INFO, __FILE__, __LINE__)
#define LOG_WARNING() strd::Logger::log(strd::LogLevel::WARNING, __FILE__, __LINE__)
#define LOG_ERROR() strd::Logger::log(strd::LogLevel::ERROR, __FILE__, __LINE__)
