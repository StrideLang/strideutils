#include "stride/utils/logger.h"
#include <chrono>
#include <iomanip>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace strd {

static std::mutex streamMutex;
static std::unordered_map<std::ostream *, bool> streamEndsWithNewline;

// By default, INFO goes to cout, WARNING and ERROR go to cerr, DEBUG goes to
// nullStream
std::ostream *Logger::debugOs = &Logger::nullStream;
std::ostream *Logger::infoOs = &std::cout;
std::ostream *Logger::warningOs = &std::cerr;
std::ostream *Logger::errorOs = &std::cerr;
bool Logger::printFullPath = false;

NullStream Logger::nullStream;

void Logger::setDebugOutput(std::ostream *os) {
  std::lock_guard<std::mutex> lock(streamMutex);
  debugOs = os;
  if (os) streamEndsWithNewline[os] = true;
}

void Logger::setInfoOutput(std::ostream *os) {
  std::lock_guard<std::mutex> lock(streamMutex);
  infoOs = os;
  if (os) streamEndsWithNewline[os] = true;
}

void Logger::setWarningOutput(std::ostream *os) {
  std::lock_guard<std::mutex> lock(streamMutex);
  warningOs = os;
  if (os) streamEndsWithNewline[os] = true;
}

void Logger::setErrorOutput(std::ostream *os) {
  std::lock_guard<std::mutex> lock(streamMutex);
  errorOs = os;
  if (os) streamEndsWithNewline[os] = true;
}

void Logger::setPrintFullPath(bool printFull) { printFullPath = printFull; }

void Logger::enableDebug() {
  std::lock_guard<std::mutex> lock(streamMutex);
  debugOs = &std::cout;
  streamEndsWithNewline[debugOs] = true;
}

void Logger::disableDebug() {
  std::lock_guard<std::mutex> lock(streamMutex);
  debugOs = &Logger::nullStream;
}

void Logger::resetLineState(std::ostream *os) {
  std::lock_guard<std::mutex> lock(streamMutex);
  if (os) {
    streamEndsWithNewline[os] = true;
  } else {
    streamEndsWithNewline.clear();
  }
}

Logger::LogStream::LogStream(LogLevel level, const char *file, int line)
    : level(level) {
  switch (level) {
  case LogLevel::DEBUG:
    os = debugOs;
    break;
  case LogLevel::INFO:
    os = infoOs;
    break;
  case LogLevel::WARNING:
    os = warningOs;
    break;
  case LogLevel::ERROR:
    os = errorOs;
    break;
  }

  if (os == &nullStream) {
    os = nullptr;
  }

  if (os) {
    // Format timestamp
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;

    std::ostringstream meta;
    meta << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S")
         << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] ";

    // Add log level
    switch (level) {
    case LogLevel::DEBUG:
      meta << "D ";
      break;
    case LogLevel::INFO:
      meta << "I ";
      break;
    case LogLevel::WARNING:
      meta << "W ";
      break;
    case LogLevel::ERROR:
      meta << "E ";
      break;
    }

    // Handle file path trimming if printFullPath is false
    const char *fileName = file;
    if (!printFullPath) {
      const char *slash = strrchr(file, '/');
      const char *backslash = strrchr(file, '\\');
      if (slash && (!backslash || slash > backslash)) {
        fileName = slash + 1;
      } else if (backslash) {
        fileName = backslash + 1;
      }
    }

    // Add file and line
    meta << "[" << fileName << ":" << line << "] ";
    metadata = meta.str();
  }
}

Logger::LogStream::LogStream(LogStream &&other) noexcept
    : os(other.os),
      level(other.level),
      metadata(std::move(other.metadata)),
      buffer(std::move(other.buffer)) {
  other.os = nullptr;
}

Logger::LogStream &Logger::LogStream::operator=(LogStream &&other) noexcept {
  if (this != &other) {
    os = other.os;
    level = other.level;
    metadata = std::move(other.metadata);
    buffer = std::move(other.buffer);
    other.os = nullptr;
  }
  return *this;
}

Logger::LogStream::~LogStream() {
  if (!os) {
    return;
  }

  std::string content = buffer.str();

  std::lock_guard<std::mutex> lock(streamMutex);

  auto it = streamEndsWithNewline.find(os);
  bool atStartOfLine = (it == streamEndsWithNewline.end() || it->second);

  if (atStartOfLine) {
    *os << metadata;
  }

  *os << content;
  os->flush();

  if (!content.empty()) {
    streamEndsWithNewline[os] = (content.back() == '\n');
  } else if (atStartOfLine) {
    streamEndsWithNewline[os] = false;
  }
}

Logger::LogStream Logger::log(LogLevel level, const char *file, int line) {
  return LogStream(level, file, line);
}

} // namespace strd
