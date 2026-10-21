/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#ifndef STRIDE_UTILS_CACHEENVELOPE_H
#define STRIDE_UTILS_CACHEENVELOPE_H

#include <cstdint>
#include <cstring>

namespace strd {

constexpr uint32_t kSerializerVersion = 2;

#pragma pack(push, 1)
struct CacheEnvelopeHeader {
  char magic[4]{'S', 'T', 'R', 'D'};
  uint32_t envelopeVersion{1};
  uint32_t serializerFourCC{0};
  uint32_t serializerVersion{kSerializerVersion};

  uint64_t compilerBuildHash{0};
  uint64_t systemConfigHash{0};
  uint64_t sourceMtime{0};
  uint64_t sourceSize{0};
  uint64_t sourceContentHash{0};
  uint64_t payloadSize{0};

  bool hasValidMagic() const noexcept {
    return magic[0] == 'S' && magic[1] == 'T' && magic[2] == 'R' &&
           magic[3] == 'D' && envelopeVersion == 1;
  }
};
#pragma pack(pop)

static_assert(sizeof(CacheEnvelopeHeader) == 64,
              "CacheEnvelopeHeader must be exactly 64 bytes");

} // namespace strd

#endif // STRIDE_UTILS_CACHEENVELOPE_H
