/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Based on BSD code by Andres Cabrera and Joseph Tilbian.

    Author: Andres Cabrera
*/

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

    // Option to print full path or just filename (defaults to false / filename only)
    static void setPrintFullPath(bool printFull);

    static void enableDebug();
    static void disableDebug();
    static bool isDebugEnabled();

    // Reset line continuation state for a stream, or for all streams if os is nullptr
    static void resetLineState(std::ostream* os = nullptr);

    // Buffering facility
    static void startBuffering();
    static void stopBuffering();
    static bool isBuffering();
    static void clearBuffer();
    static void flushBuffer(std::ostream* target = nullptr);
    static std::string getBufferedOutput();

    class LogStream {
    public:
        LogStream(LogLevel level, const char* file, int line);
        ~LogStream();

        LogStream(LogStream&& other) noexcept;
        LogStream& operator=(LogStream&& other) noexcept;
        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;

        template <typename T>
        LogStream& operator<<(const T& value) {
            if (os) {
                buffer << value;
            }
            return *this;
        }

        // Support for stream manipulators like std::endl
        typedef std::ostream& (*OstreamManipulator)(std::ostream&);
        LogStream& operator<<(OstreamManipulator manip) {
            if (os) {
                buffer << manip;
            }
            return *this;
        }

        typedef std::ios_base& (*IosManipulator)(std::ios_base&);
        LogStream& operator<<(IosManipulator manip) {
            if (os) {
                buffer << manip;
            }
            return *this;
        }

    private:
        std::ostream* os;
        LogLevel level;
        std::string metadata;
        std::ostringstream buffer;
    };

    static LogStream log(LogLevel level, const char* file, int line);
    
    // Instance of a null stream for discarding output
    static NullStream nullStream;

private:
    struct BufferEntry {
        LogLevel level;
        std::ostream* originalOs;
        std::string metadata;
        std::string message;
    };

    static std::ostream* debugOs;
    static std::ostream* infoOs;
    static std::ostream* warningOs;
    static std::ostream* errorOs;
    static bool printFullPath;

    static bool bufferingEnabled;
    static std::vector<BufferEntry> logBuffer;
};

} // namespace strd

#define LOG_DEBUG() strd::Logger::log(strd::LogLevel::DEBUG, __FILE__, __LINE__)
#define LOG_INFO() strd::Logger::log(strd::LogLevel::INFO, __FILE__, __LINE__)
#define LOG_WARNING() strd::Logger::log(strd::LogLevel::WARNING, __FILE__, __LINE__)
#define LOG_WARN() LOG_WARNING()
#define LOG_ERROR() strd::Logger::log(strd::LogLevel::ERROR, __FILE__, __LINE__)
