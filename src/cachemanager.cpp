/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#include "stride/utils/cachemanager.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace strd {
namespace {

// Fast 64-bit FNV-1a / xxHash-style mixer
constexpr uint64_t kHashOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kHashPrime = 1099511628211ULL;

inline uint64_t mix64(uint64_t h) noexcept {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  return h;
}

uint64_t combineHash(uint64_t seed, uint64_t val) noexcept {
  return mix64(seed ^ (val + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2)));
}

std::string sanitizeTag(std::string_view tag) {
  std::string out;
  out.reserve(tag.size());
  for (char c : tag) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}

} // namespace

CacheManager &CacheManager::instance() {
  static CacheManager inst;
  return inst;
}

CacheManager::CacheManager()
    : m_serializer(ASTSerializerFactory::createDefault()) {
  if (const char *noCache = std::getenv("STRIDE_NO_CACHE")) {
    std::string val(noCache);
    if (val == "1" || val == "true" || val == "TRUE" || val == "on") {
      m_enabled = false;
    }
  }
  if (const char *envDir = std::getenv("STRIDE_CACHE_DIR")) {
    if (envDir[0] != '\0') {
      m_explicitCacheDir = std::filesystem::path(envDir);
    }
  }
}

void CacheManager::setEnabled(bool enabled) noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_enabled = enabled;
}

bool CacheManager::isEnabled() const noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_enabled;
}

void CacheManager::setForceRecompile(bool force) noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_forceRecompile = force;
}

bool CacheManager::isForceRecompile() const noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_forceRecompile;
}

void CacheManager::setCacheDirectory(const std::filesystem::path &dir) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_explicitCacheDir = dir;
}

std::filesystem::path CacheManager::getCacheDirectory() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_explicitCacheDir;
}

void CacheManager::setSerializer(std::unique_ptr<IASTSerializer> serializer) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (serializer) {
    m_serializer = std::move(serializer);
  }
}

const IASTSerializer &CacheManager::getSerializer() const {
  return *m_serializer;
}

uint64_t CacheManager::hashBytes(const void *data, size_t size) noexcept {
  const auto *bytes = static_cast<const uint8_t *>(data);
  uint64_t h = kHashOffsetBasis;
  size_t i = 0;
  for (; i + 8 <= size; i += 8) {
    uint64_t word = 0;
    std::memcpy(&word, bytes + i, sizeof(uint64_t));
    h ^= mix64(word);
    h *= kHashPrime;
  }
  for (; i < size; ++i) {
    h ^= static_cast<uint64_t>(bytes[i]);
    h *= kHashPrime;
  }
  return mix64(h);
}

uint64_t CacheManager::hashString(std::string_view str) noexcept {
  return hashBytes(str.data(), str.size());
}

uint64_t CacheManager::hashFile(const std::filesystem::path &filePath) {
  std::ifstream in(filePath, std::ios::binary);
  if (!in.is_open()) {
    return 0;
  }
  uint64_t h = kHashOffsetBasis;
  char buffer[8192];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    uint64_t chunkHash =
        hashBytes(buffer, static_cast<size_t>(in.gcount()));
    h = combineHash(h, chunkHash);
  }
  return h;
}

uint64_t CacheManager::getFileMtime(const std::filesystem::path &filePath) {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(filePath, ec);
  if (ec) {
    return 0;
  }
  return static_cast<uint64_t>(ftime.time_since_epoch().count());
}

uint64_t CacheManager::getFileSize(const std::filesystem::path &filePath) {
  std::error_code ec;
  auto sz = std::filesystem::file_size(filePath, ec);
  if (ec) {
    return 0;
  }
  return static_cast<uint64_t>(sz);
}

uint64_t CacheManager::getCompilerBuildHash() noexcept {
  static const uint64_t kBuildHash =
      combineHash(hashString(__DATE__ " " __TIME__), 0x5354524443414348ULL);
  return kBuildHash;
}

bool CacheManager::computeDirectoryFingerprint(
    const std::filesystem::path &dirPath, uint64_t &outAggregateMtime,
    uint64_t &outAggregateSize, uint64_t &outAggregateHash) {
  std::error_code ec;
  if (!std::filesystem::exists(dirPath, ec) ||
      !std::filesystem::is_directory(dirPath, ec)) {
    return false;
  }

  std::vector<std::filesystem::path> files;
  auto collectFiles = [&](auto &self, const std::filesystem::path &currentDir) -> void {
    std::error_code iterEc;
    for (const auto &entry :
         std::filesystem::directory_iterator(currentDir, iterEc)) {
      if (entry.is_regular_file(iterEc) &&
          entry.path().extension() == ".stride") {
        std::string stem = entry.path().stem().string();
        if (!stem.empty() &&
            std::isupper(static_cast<unsigned char>(stem[0]))) {
          files.push_back(entry.path());
        }
      } else if (entry.is_directory(iterEc)) {
        std::string subDirName = entry.path().filename().string();
        if (!subDirName.empty() &&
            std::isupper(static_cast<unsigned char>(subDirName[0]))) {
          self(self, entry.path());
        }
      }
    }
  };
  collectFiles(collectFiles, dirPath);
  std::sort(files.begin(), files.end());

  uint64_t mtimeAcc = kHashOffsetBasis;
  uint64_t sizeAcc = 0;
  uint64_t hashAcc = kHashOffsetBasis;

  for (const auto &f : files) {
    std::error_code relEc;
    auto relPath = std::filesystem::relative(f, dirPath, relEc);
    std::string fname =
        relEc ? f.filename().generic_string() : relPath.generic_string();
    uint64_t fmtime = getFileMtime(f);
    uint64_t fsize = getFileSize(f);
    mtimeAcc = combineHash(mtimeAcc, hashString(fname));
    mtimeAcc = combineHash(mtimeAcc, fmtime);
    sizeAcc += fsize;
    hashAcc = combineHash(hashAcc, hashString(fname));
    hashAcc = combineHash(hashAcc, fsize);
  }

  outAggregateMtime = mtimeAcc;
  outAggregateSize = sizeAcc;
  outAggregateHash = hashAcc;
  return true;
}

std::filesystem::path CacheManager::getOsUserCacheDir() {
#ifdef _WIN32
  if (const char *localAppData = std::getenv("LOCALAPPDATA")) {
    if (localAppData[0] != '\0') {
      return std::filesystem::path(localAppData) / "stride" /
             kDefaultCacheFolderName;
    }
  }
#else
  if (const char *xdgCache = std::getenv("XDG_CACHE_HOME")) {
    if (xdgCache[0] != '\0') {
      return std::filesystem::path(xdgCache) / "stride" /
             kDefaultCacheFolderName;
    }
  }
  if (const char *home = std::getenv("HOME")) {
    if (home[0] != '\0') {
      return std::filesystem::path(home) / kDefaultCacheFolderName;
    }
  }
#endif
  std::error_code ec;
  return std::filesystem::temp_directory_path(ec) / "stride" /
         kDefaultCacheFolderName;
}

bool CacheManager::isDirectoryWritable(const std::filesystem::path &dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec)) {
    if (!std::filesystem::create_directories(dir, ec) || ec) {
      return false;
    }
  }
  auto perms = std::filesystem::status(dir, ec).permissions();
  if (ec) {
    return false;
  }
  if ((perms & (std::filesystem::perms::owner_write |
                std::filesystem::perms::group_write |
                std::filesystem::perms::others_write)) ==
      std::filesystem::perms::none) {
    return false;
  }

  static std::atomic<uint64_t> probeCounter{0};
  auto probeFile =
      dir / (".probe_" + std::to_string(probeCounter.fetch_add(1)));
  std::ofstream testOut(probeFile, std::ios::binary | std::ios::trunc);
  if (!testOut.is_open()) {
    return false;
  }
  testOut.close();
  std::filesystem::remove(probeFile, ec);
  return true;
}

std::filesystem::path CacheManager::resolveCacheDirForSource(
    const std::filesystem::path &sourcePath) const {
  std::filesystem::path explicitDir;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    explicitDir = m_explicitCacheDir;
  }
  if (!explicitDir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(explicitDir, ec);
    return explicitDir;
  }

  std::error_code ec;
  std::filesystem::path absSource = std::filesystem::absolute(sourcePath, ec);
  if (ec) {
    absSource = sourcePath;
  }

  std::filesystem::path baseDir =
      std::filesystem::is_directory(absSource, ec) ? absSource
                                                   : absSource.parent_path();
  if (baseDir.empty()) {
    baseDir = std::filesystem::current_path(ec);
  }

  std::filesystem::path siblingCache = baseDir / kDefaultCacheFolderName;
  if (isDirectoryWritable(siblingCache)) {
    return siblingCache;
  }

  uint64_t dirHash = hashString(baseDir.generic_string());
  std::ostringstream hexHash;
  hexHash << std::hex << dirHash;
  std::filesystem::path fallbackDir = getOsUserCacheDir() / hexHash.str();
  std::filesystem::create_directories(fallbackDir, ec);
  return fallbackDir;
}

std::filesystem::path
CacheManager::getCacheFilePath(const std::filesystem::path &sourcePath,
                               const std::string &systemTag) const {
  std::filesystem::path cacheDir = resolveCacheDirForSource(sourcePath);
  std::error_code ec;
  bool isDir = std::filesystem::is_directory(sourcePath, ec);

  std::string baseName;
  if (isDir) {
    std::string stem = sourcePath.filename().string();
    if (stem.empty()) {
      stem = sourcePath.parent_path().filename().string();
    }
    if (stem.empty()) {
      stem = "dir";
    }
    baseName = "_dir_" + sanitizeTag(stem);
  } else {
    baseName = sourcePath.filename().string();
  }

  if (!systemTag.empty()) {
    baseName += "." + sanitizeTag(systemTag);
  }
  baseName += kCacheExtension;
  return cacheDir / baseName;
}

ASTNode CacheManager::loadCachedAST(
    const std::filesystem::path &sourceFile,
    const std::vector<std::filesystem::path> &dependencies,
    const std::string &systemTag) {
  if (!isEnabled() || isForceRecompile()) {
    return nullptr;
  }

  std::error_code ec;
  if (!std::filesystem::exists(sourceFile, ec)) {
    return nullptr;
  }

  std::filesystem::path cacheFile = getCacheFilePath(sourceFile, systemTag);
  if (!std::filesystem::exists(cacheFile, ec)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.misses++;
    return nullptr;
  }

  std::ifstream in(cacheFile, std::ios::binary);
  if (!in.is_open()) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.misses++;
    return nullptr;
  }

  CacheEnvelopeHeader header{};
  in.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!in.good() || !header.hasValidMagic()) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.invalidations++;
    m_stats.misses++;
    return nullptr;
  }

  const uint64_t expectedCompilerHash = getCompilerBuildHash();
  const uint64_t expectedSystemHash = hashString(systemTag);
  if (header.compilerBuildHash != expectedCompilerHash ||
      header.systemConfigHash != expectedSystemHash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.invalidations++;
    m_stats.misses++;
    return nullptr;
  }

  const uint64_t curMtime = getFileMtime(sourceFile);
  const uint64_t curSize = getFileSize(sourceFile);

  // Two-tier freshness check:
  // Fast path: if mtime & size match (and no extra dependencies), skip hashing
  bool fresh = false;
  if (dependencies.empty() && header.sourceMtime == curMtime &&
      header.sourceSize == curSize && curSize > 0) {
    fresh = true;
  } else if (header.sourceSize == curSize) {
    // Safe fallback path: check content hash + dependencies
    uint64_t contentHash = hashFile(sourceFile);
    for (const auto &dep : dependencies) {
      contentHash = combineHash(contentHash, getFileMtime(dep));
      contentHash = combineHash(contentHash, getFileSize(dep));
    }
    if (contentHash != 0 && contentHash == header.sourceContentHash) {
      fresh = true;
    }
  }

  if (!fresh) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.invalidations++;
    m_stats.misses++;
    return nullptr;
  }

  std::vector<std::byte> payload(static_cast<size_t>(header.payloadSize));
  if (header.payloadSize > 0) {
    in.read(reinterpret_cast<char *>(payload.data()),
            static_cast<std::streamsize>(header.payloadSize));
    if (static_cast<uint64_t>(in.gcount()) != header.payloadSize) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.invalidations++;
      m_stats.misses++;
      return nullptr;
    }
  }

  std::unique_ptr<IASTSerializer> deserializer;
  if (m_serializer &&
      m_serializer->getFormatFourCC() == header.serializerFourCC &&
      m_serializer->getFormatVersion() == header.serializerVersion) {
    ASTNode result;
    if (m_serializer->deserializeFromBytes(payload, result) && result) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.hits++;
      return result;
    }
  } else {
    deserializer = ASTSerializerFactory::createByFourCC(header.serializerFourCC);
    if (deserializer &&
        deserializer->getFormatVersion() == header.serializerVersion) {
      ASTNode result;
      if (deserializer->deserializeFromBytes(payload, result) && result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.hits++;
        return result;
      }
    }
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats.invalidations++;
  m_stats.misses++;
  return nullptr;
}

bool CacheManager::saveCachedAST(
    const std::filesystem::path &sourceFile, const ASTNode &root,
    const std::vector<std::filesystem::path> &dependencies,
    const std::string &systemTag) {
  if (!isEnabled() || !root) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::exists(sourceFile, ec)) {
    return false;
  }

  std::vector<std::byte> payload;
  if (!m_serializer->serializeToBytes(root, payload)) {
    return false;
  }

  uint64_t contentHash = hashFile(sourceFile);
  for (const auto &dep : dependencies) {
    contentHash = combineHash(contentHash, getFileMtime(dep));
    contentHash = combineHash(contentHash, getFileSize(dep));
  }

  CacheEnvelopeHeader header{};
  header.serializerFourCC = m_serializer->getFormatFourCC();
  header.serializerVersion = m_serializer->getFormatVersion();
  header.compilerBuildHash = getCompilerBuildHash();
  header.systemConfigHash = hashString(systemTag);
  header.sourceMtime = getFileMtime(sourceFile);
  header.sourceSize = getFileSize(sourceFile);
  header.sourceContentHash = contentHash;
  header.payloadSize = static_cast<uint64_t>(payload.size());

  std::filesystem::path cacheFile = getCacheFilePath(sourceFile, systemTag);
  std::filesystem::create_directories(cacheFile.parent_path(), ec);

  static std::atomic<uint64_t> tmpSeq{0};
  std::filesystem::path tmpFile =
      cacheFile.string() + ".tmp." + std::to_string(tmpSeq.fetch_add(1));

  {
    std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    if (!payload.empty()) {
      out.write(reinterpret_cast<const char *>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
    }
    if (!out.good()) {
      out.close();
      std::filesystem::remove(tmpFile, ec);
      return false;
    }
  }

  std::filesystem::rename(tmpFile, cacheFile, ec);
  if (ec) {
    ec.clear();
    std::filesystem::copy_file(
        tmpFile, cacheFile,
        std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmpFile, ec);
    if (ec) {
      return false;
    }
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats.writes++;
  return true;
}

bool CacheManager::loadCachedDirectory(const std::filesystem::path &dirPath,
                                       std::vector<ASTNode> &outNodes,
                                       const std::string &systemTag) {
  if (!isEnabled() || isForceRecompile()) {
    return false;
  }

  uint64_t curMtime = 0;
  uint64_t curSize = 0;
  uint64_t curHash = 0;
  if (!computeDirectoryFingerprint(dirPath, curMtime, curSize, curHash)) {
    return false;
  }

  std::filesystem::path cacheFile = getCacheFilePath(dirPath, systemTag);
  std::error_code ec;
  if (!std::filesystem::exists(cacheFile, ec)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.misses++;
    return false;
  }

  std::ifstream in(cacheFile, std::ios::binary);
  if (!in.is_open()) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.misses++;
    return false;
  }

  CacheEnvelopeHeader header{};
  in.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!in.good() || !header.hasValidMagic()) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.invalidations++;
    m_stats.misses++;
    return false;
  }

  if (header.compilerBuildHash != getCompilerBuildHash() ||
      header.systemConfigHash != hashString(systemTag) ||
      header.sourceMtime != curMtime || header.sourceSize != curSize ||
      header.sourceContentHash != curHash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.invalidations++;
    m_stats.misses++;
    return false;
  }

  std::vector<std::byte> payload(static_cast<size_t>(header.payloadSize));
  if (header.payloadSize > 0) {
    in.read(reinterpret_cast<char *>(payload.data()),
            static_cast<std::streamsize>(header.payloadSize));
    if (static_cast<uint64_t>(in.gcount()) != header.payloadSize) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.invalidations++;
      m_stats.misses++;
      return false;
    }
  }

  if (m_serializer &&
      m_serializer->getFormatFourCC() == header.serializerFourCC &&
      m_serializer->getFormatVersion() == header.serializerVersion) {
    if (m_serializer->deserializeListFromBytes(payload, outNodes)) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.hits++;
      return true;
    }
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats.invalidations++;
  m_stats.misses++;
  return false;
}

bool CacheManager::saveCachedDirectory(const std::filesystem::path &dirPath,
                                       const std::vector<ASTNode> &nodes,
                                       const std::string &systemTag) {
  if (!isEnabled()) {
    return false;
  }

  uint64_t curMtime = 0;
  uint64_t curSize = 0;
  uint64_t curHash = 0;
  if (!computeDirectoryFingerprint(dirPath, curMtime, curSize, curHash)) {
    return false;
  }

  std::vector<std::byte> payload;
  if (!m_serializer->serializeListToBytes(nodes, payload)) {
    return false;
  }

  CacheEnvelopeHeader header{};
  header.serializerFourCC = m_serializer->getFormatFourCC();
  header.serializerVersion = m_serializer->getFormatVersion();
  header.compilerBuildHash = getCompilerBuildHash();
  header.systemConfigHash = hashString(systemTag);
  header.sourceMtime = curMtime;
  header.sourceSize = curSize;
  header.sourceContentHash = curHash;
  header.payloadSize = static_cast<uint64_t>(payload.size());

  std::filesystem::path cacheFile = getCacheFilePath(dirPath, systemTag);
  std::error_code ec;
  std::filesystem::create_directories(cacheFile.parent_path(), ec);

  static std::atomic<uint64_t> tmpSeq{0};
  std::filesystem::path tmpFile =
      cacheFile.string() + ".tmp." + std::to_string(tmpSeq.fetch_add(1));

  {
    std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return false;
    }
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    if (!payload.empty()) {
      out.write(reinterpret_cast<const char *>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
    }
    if (!out.good()) {
      out.close();
      std::filesystem::remove(tmpFile, ec);
      return false;
    }
  }

  std::filesystem::rename(tmpFile, cacheFile, ec);
  if (ec) {
    ec.clear();
    std::filesystem::copy_file(
        tmpFile, cacheFile,
        std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmpFile, ec);
    if (ec) {
      return false;
    }
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats.writes++;
  return true;
}

void CacheManager::clearCache(const std::filesystem::path &sourceOrCacheDir) {
  std::error_code ec;
  if (!sourceOrCacheDir.empty()) {
    if (sourceOrCacheDir.filename() == kDefaultCacheFolderName &&
        std::filesystem::exists(sourceOrCacheDir, ec)) {
      std::filesystem::remove_all(sourceOrCacheDir, ec);
      return;
    }
    auto resolved = resolveCacheDirForSource(sourceOrCacheDir);
    if (std::filesystem::exists(resolved, ec)) {
      std::filesystem::remove_all(resolved, ec);
    }
    return;
  }

  std::filesystem::path explicitDir;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    explicitDir = m_explicitCacheDir;
  }
  if (!explicitDir.empty() && std::filesystem::exists(explicitDir, ec)) {
    std::filesystem::remove_all(explicitDir, ec);
  }
  auto userCache = getOsUserCacheDir();
  if (std::filesystem::exists(userCache, ec)) {
    std::filesystem::remove_all(userCache, ec);
  }
}

CacheStats CacheManager::getStats() const noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stats;
}

void CacheManager::resetStats() noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_stats = CacheStats{};
}

} // namespace strd
