/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#ifndef STRIDE_UTILS_CACHEMANAGER_H
#define STRIDE_UTILS_CACHEMANAGER_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/utils/cacheenvelope.h"
#include "stride/utils/iastserializer.h"

namespace strd {

struct CacheStats {
  size_t hits{0};
  size_t misses{0};
  size_t writes{0};
  size_t invalidations{0};
};

struct CachePruneOptions {
  // Disk quota for the target cache directory (0 = no quota check)
  uint64_t maxSizeBytes{1024ULL * 1024ULL * 1024ULL}; // Default 1 GB
  // Target watermark percentage when pruning (default 80% = 800 MB on 1 GB cap)
  double pruneToFraction{0.80};
  // Maximum age for unaccessed cache files (0 = no age limit)
  std::chrono::hours maxAge{24 * 30}; // Default 30 days
  // Whether to remove cache files whose original source file no longer exists
  bool pruneOrphans{true};
  // Whether to remove directories of stale / inactive compiler build partitions
  bool pruneStaleVersions{false};
  // Explicit version partitions to retain (empty = retain current active version)
  std::vector<std::string> keepVersionPartitions{};
};

struct CachePruneStats {
  size_t filesRemoved{0};
  size_t directoriesRemoved{0};
  uint64_t bytesReclaimed{0};
};

class CacheManager {
public:
  static constexpr const char *kDefaultCacheFolderName = ".strdcache";
  static constexpr const char *kCacheExtension = ".scache";

  static CacheManager &instance();

  CacheManager();
  ~CacheManager() = default;

  void setEnabled(bool enabled) noexcept;
  // False for an experimental compiler, whatever setEnabled() was given
  bool isEnabled() const noexcept;

  void setForceRecompile(bool force) noexcept;
  bool isForceRecompile() const noexcept;

  void setCacheDirectory(const std::filesystem::path &dir);
  std::filesystem::path getCacheDirectory() const;

  void setSerializer(std::unique_ptr<IASTSerializer> serializer);
  const IASTSerializer &getSerializer() const;

  // Resolves the writable .strdcache folder for a given file or directory.
  // Priority:
  // 1. Explicit setCacheDirectory() or STRIDE_CACHE_DIR env var
  // 2. Sibling .strdcache folder if writable
  // 3. OS user cache (%LOCALAPPDATA%/stride/.strdcache or ~/.strdcache)
  // Each resolved location contains a version partition subfolder (e.g. b_<buildHash>/).
  std::filesystem::path
  resolveCacheDirForSource(const std::filesystem::path &sourcePath) const;

  std::filesystem::path
  getCacheFilePath(const std::filesystem::path &sourcePath,
                   const std::string &systemTag = "",
                   const std::string &customExt = "") const;

  // Partition name for the current active compiler version/build hash,
  // e.g. "v1.2.0_0123456789abcdef" or "b_0123456789abcdef".
  static std::string getCompilerVersionPartition();

  // File-level AST cache operations
  ASTNode loadCachedAST(
      const std::filesystem::path &sourceFile,
      const std::vector<std::filesystem::path> &dependencies = {},
      const std::string &systemTag = "");
  ASTNode loadCachedAST(const std::filesystem::path &sourceFile,
                        const std::string &systemTag) {
    return loadCachedAST(sourceFile, {}, systemTag);
  }

  bool saveCachedAST(
      const std::filesystem::path &sourceFile, const ASTNode &root,
      const std::vector<std::filesystem::path> &dependencies = {},
      const std::string &systemTag = "");
  bool saveCachedAST(const std::filesystem::path &sourceFile,
                     const ASTNode &root, const std::string &systemTag) {
    return saveCachedAST(sourceFile, root, {}, systemTag);
  }

  // Generic binary payload cache operations (for compiled object .obj and LLVM bitcode .bc)
  bool loadCachedBinary(
      const std::filesystem::path &sourceFile,
      std::vector<uint8_t> &outBytes,
      const std::vector<std::filesystem::path> &dependencies = {},
      const std::string &systemTag = "",
      const std::string &customExt = ".obj");

  bool saveCachedBinary(
      const std::filesystem::path &sourceFile,
      const void *data, size_t size,
      const std::vector<std::filesystem::path> &dependencies = {},
      const std::string &systemTag = "",
      const std::string &customExt = ".obj");

  // Directory-level AST bundle cache operations (for loadAllInDirectory)
  bool loadCachedDirectory(const std::filesystem::path &dirPath,
                           std::vector<ASTNode> &outNodes,
                           const std::string &systemTag = "");
  bool saveCachedDirectory(const std::filesystem::path &dirPath,
                           const std::vector<ASTNode> &nodes,
                           const std::string &systemTag = "");

  // Maintenance & metrics
  void clearCache(const std::filesystem::path &sourceOrCacheDir = {});
  CacheStats getStats() const noexcept;
  void resetStats() noexcept;

  // Pruning & quota management
  CachePruneStats pruneCache(
      const CachePruneOptions &options = {},
      const std::filesystem::path &targetCacheDir = {});
  uint64_t getCacheDiskUsage(const std::filesystem::path &targetCacheDir = {}) const;

  void setAutoPruneEnabled(bool enabled) noexcept;
  bool isAutoPruneEnabled() const noexcept;

  void setAutoPruneThreshold(size_t writes) noexcept;
  size_t getAutoPruneThreshold() const noexcept;

  // Fingerprinting helpers
  static uint64_t hashBytes(const void *data, size_t size) noexcept;
  static uint64_t hashString(std::string_view str) noexcept;
  static uint64_t hashFile(const std::filesystem::path &filePath);
  static uint64_t getFileMtime(const std::filesystem::path &filePath);
  static uint64_t getFileSize(const std::filesystem::path &filePath);
  // The commits of the compiler's sources, e.g. "strideutils:9beca65
  // stridejit:5d26dd5"
  static const char *getCompilerCommitHash() noexcept;
  // Whether the compiler was built from sources with uncommitted changes.
  // An experimental compiler never reads or writes the cache: its output can
  // differ from that of the commits that identify it.
  static bool isExperimentalCompiler() noexcept;
  static uint64_t getCompilerBuildHash() noexcept;

  // Testing-only access hooks. Not intended for production use.
  struct TestAccess {
    static void setCompilerBuildHashOverride(uint64_t hashOverride) noexcept;
    static void clearCompilerBuildHashOverride() noexcept;
    static void setExperimentalCompilerOverride(int experimental) noexcept;
  };

  [[deprecated("Use CacheManager::TestAccess::setExperimentalCompilerOverride instead")]]
  static void setExperimentalCompilerOverride(int experimental) noexcept {
    TestAccess::setExperimentalCompilerOverride(experimental);
  }
  [[deprecated("Use CacheManager::TestAccess::setCompilerBuildHashOverride instead")]]
  static void setCompilerBuildHashOverride(uint64_t hashOverride) noexcept {
    TestAccess::setCompilerBuildHashOverride(hashOverride);
  }
  [[deprecated("Use CacheManager::TestAccess::clearCompilerBuildHashOverride instead")]]
  static void clearCompilerBuildHashOverride() noexcept {
    TestAccess::clearCompilerBuildHashOverride();
  }

  // What a cached entry's key takes from one of its dependencies: a file's
  // time and size, or for a directory (the folders a program imports
  // libraries from) the fingerprint of its library files
  static uint64_t dependencyHash(const std::filesystem::path &dependency);

  // Compute composite fingerprint of all .stride files in a directory
  static bool computeDirectoryFingerprint(const std::filesystem::path &dirPath,
                                          uint64_t &outAggregateMtime,
                                          uint64_t &outAggregateSize,
                                          uint64_t &outAggregateHash);

private:
  static std::filesystem::path getOsUserCacheDir();
  static bool isDirectoryWritable(const std::filesystem::path &dir);
  void maybeAutoPrune(const std::filesystem::path &cacheDir);

  mutable std::mutex m_mutex;
  bool m_enabled{true};
  bool m_forceRecompile{false};
  bool m_autoPruneEnabled{false};
  size_t m_autoPruneThreshold{50};
  mutable std::atomic<size_t> m_writesSincePrune{0};
  std::filesystem::path m_explicitCacheDir;
  std::unique_ptr<IASTSerializer> m_serializer;
  mutable CacheStats m_stats;
};

} // namespace strd

#endif // STRIDE_UTILS_CACHEMANAGER_H
