/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#ifndef STRIDE_UTILS_IASTSERIALIZER_H
#define STRIDE_UTILS_IASTSERIALIZER_H

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

#include "stride/parser/ast.h"

namespace strd {

constexpr uint32_t makeFourCC(char a, char b, char c, char d) noexcept {
  return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

class IASTSerializer {
public:
  virtual ~IASTSerializer() = default;

  virtual uint32_t getFormatFourCC() const noexcept = 0;
  virtual std::string_view getFormatName() const noexcept = 0;
  virtual uint32_t getFormatVersion() const noexcept = 0;

  virtual bool serialize(const ASTNode &root, std::ostream &out) const = 0;
  virtual bool deserialize(std::istream &in, ASTNode &outRoot) const = 0;

  virtual bool serializeList(const std::vector<ASTNode> &nodes,
                             std::ostream &out) const = 0;
  virtual bool deserializeList(std::istream &in,
                               std::vector<ASTNode> &outNodes) const = 0;

  virtual bool serializeToBytes(const ASTNode &root,
                                std::vector<std::byte> &outBytes) const = 0;
  virtual bool deserializeFromBytes(const std::vector<std::byte> &bytes,
                                    ASTNode &outRoot) const = 0;

  virtual bool serializeListToBytes(const std::vector<ASTNode> &nodes,
                                    std::vector<std::byte> &outBytes) const = 0;
  virtual bool deserializeListFromBytes(const std::vector<std::byte> &bytes,
                                        std::vector<ASTNode> &outNodes) const = 0;
};

class ASTSerializerFactory {
public:
  static constexpr uint32_t kZppBitsFourCC = makeFourCC('Z', 'P', 'P', '1');

  static std::unique_ptr<IASTSerializer> createDefault();
  static std::unique_ptr<IASTSerializer> createByFourCC(uint32_t fourCC);
};

} // namespace strd

#endif // STRIDE_UTILS_IASTSERIALIZER_H
