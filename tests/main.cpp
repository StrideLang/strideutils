
#include "gtest/gtest.h"
#include "stride/utils/cachemanager.h"

int main(int argc, char **argv) {
  // Never read from cache during tests, but always regenerate cache files.
  strd::CacheManager::instance().setEnabled(true);
  strd::CacheManager::instance().setForceRecompile(true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
