/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#ifndef STRIDE_UTILS_CACHEMANAGER_H
#define STRIDE_UTILS_CACHEMANAGER_H

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

class CacheManager {
public:
  static constexpr const char *kDefaultCacheFolderName = ".strdcache";
  static constexpr const char *kCacheExtension = ".scache";

  static CacheManager &instance();

  CacheManager();
  ~CacheManager() = default;

  void setEnabled(bool enabled) noexcept;
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
  std::filesystem::path
  resolveCacheDirForSource(const std::filesystem::path &sourcePath) const;

  std::filesystem::path
  getCacheFilePath(const std::filesystem::path &sourcePath,
                   const std::string &systemTag = "") const;

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

  // Fingerprinting helpers
  static uint64_t hashBytes(const void *data, size_t size) noexcept;
  static uint64_t hashString(std::string_view str) noexcept;
  static uint64_t hashFile(const std::filesystem::path &filePath);
  static uint64_t getFileMtime(const std::filesystem::path &filePath);
  static uint64_t getFileSize(const std::filesystem::path &filePath);
  static uint64_t getCompilerBuildHash() noexcept;

  // Compute composite fingerprint of all .stride files in a directory
  static bool computeDirectoryFingerprint(const std::filesystem::path &dirPath,
                                          uint64_t &outAggregateMtime,
                                          uint64_t &outAggregateSize,
                                          uint64_t &outAggregateHash);

private:
  static std::filesystem::path getOsUserCacheDir();
  static bool isDirectoryWritable(const std::filesystem::path &dir);

  mutable std::mutex m_mutex;
  bool m_enabled{true};
  bool m_forceRecompile{false};
  std::filesystem::path m_explicitCacheDir;
  std::unique_ptr<IASTSerializer> m_serializer;
  mutable CacheStats m_stats;
};

} // namespace strd

#endif // STRIDE_UTILS_CACHEMANAGER_H
