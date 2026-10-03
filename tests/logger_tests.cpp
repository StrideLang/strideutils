#include "stride/utils/logger.h"
#include "gtest/gtest.h"
#include <regex>
#include <sstream>

using namespace strd;

class LoggerTest : public ::testing::Test {
protected:
  void SetUp() override {
    Logger::setInfoOutput(&infoStream);
    Logger::setErrorOutput(&errorStream);
    Logger::resetLineState();
  }

  void TearDown() override {
    Logger::setInfoOutput(&std::cout);
    Logger::setErrorOutput(&std::cerr);
    Logger::resetLineState();
  }

  std::stringstream infoStream;
  std::stringstream errorStream;
};

TEST_F(LoggerTest, NormalLogWithNewline) {
  LOG_INFO() << "First message\n";
  LOG_INFO() << "Second message\n";

  std::string out = infoStream.str();

  // Each log should have its own metadata prefix
  std::regex pattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] First message\n\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Second message\n)");
  EXPECT_TRUE(std::regex_match(out, pattern)) << "Output was: " << out;
}

TEST_F(LoggerTest, ContinuationWithoutNewline) {
  LOG_INFO() << "Processing... ";
  LOG_INFO() << "Done!\n";

  std::string out = infoStream.str();

  // "Done!" should be printed directly after "Processing... " with NO metadata
  // prefix between them
  std::regex pattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Processing\.\.\. Done!\n)");
  EXPECT_TRUE(std::regex_match(out, pattern)) << "Output was: " << out;

  // A subsequent log after newline should have metadata again
  LOG_INFO() << "Next operation\n";
  std::string out2 = infoStream.str();
  std::regex pattern2(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Processing\.\.\. Done!\n\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Next operation\n)");
  EXPECT_TRUE(std::regex_match(out2, pattern2)) << "Output was: " << out2;
}

TEST_F(LoggerTest, MultipleContinuationsOnSameLine) {
  LOG_INFO() << "Numbers: ";
  LOG_INFO() << 1 << " ";
  LOG_INFO() << 2 << " ";
  LOG_INFO() << 3 << "\n";

  std::string out = infoStream.str();
  std::regex pattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Numbers: 1 2 3\n)");
  EXPECT_TRUE(std::regex_match(out, pattern)) << "Output was: " << out;
}

TEST_F(LoggerTest, ContinuationWithStdEndl) {
  LOG_INFO() << "Status: ";
  LOG_INFO() << "OK" << std::endl;
  LOG_INFO() << "Finished\n";

  std::string out = infoStream.str();
  std::regex pattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Status: OK\n\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Finished\n)");
  EXPECT_TRUE(std::regex_match(out, pattern)) << "Output was: " << out;
}

TEST_F(LoggerTest, IndependentStreamsDoNotInterfere) {
  LOG_INFO() << "Info start: ";
  LOG_ERROR() << "Error message\n";
  LOG_INFO() << "info end\n";

  std::string infoOut = infoStream.str();
  std::string errorOut = errorStream.str();

  std::regex infoPattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Info start: info end\n)");
  EXPECT_TRUE(std::regex_match(infoOut, infoPattern))
      << "Info output was: " << infoOut;

  std::regex errorPattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] E \[.*\] Error message\n)");
  EXPECT_TRUE(std::regex_match(errorOut, errorPattern))
      << "Error output was: " << errorOut;
}

TEST_F(LoggerTest, ResetLineStateForcesMetadata) {
  LOG_INFO() << "Unfinished line... ";
  Logger::resetLineState(&infoStream);
  LOG_INFO() << "Forced new metadata\n";

  std::string out = infoStream.str();
  std::regex pattern(
      R"(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Unfinished line\.\.\. \[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] I \[.*\] Forced new metadata\n)");
  EXPECT_TRUE(std::regex_match(out, pattern)) << "Output was: " << out;
}
