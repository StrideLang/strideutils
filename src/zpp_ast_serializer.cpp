/*
    Stride is licensed under the terms of the 3-clause BSD license.
*/

#include "stride/utils/iastserializer.h"
#include "stride/utils/third_party/zpp_bits.h"

#include "stride/parser/arraynode.h"
#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/importnode.h"
#include "stride/parser/keywordnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/packagenode.h"
#include "stride/parser/platformnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/scopenode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace strd {
namespace {

struct SerializedNodeRecord {
  uint8_t token{0};
  uint8_t subType{0};
  int32_t line{-1};
  int32_t column{-1};
  int64_t intVal{0};
  double realVal{0.0};
  std::string filename;
  std::string relativeFilename;
  std::string absoluteFilename;
  std::string name;
  std::string secondaryStr;
  std::vector<std::string> scope;
  std::vector<std::string> extraStrings;
  std::vector<uint32_t> childrenIds;
  uint32_t compilerPropertiesId{0};
  uint32_t domainId{0};
};

struct SerializedASTGraph {
  std::vector<SerializedNodeRecord> nodes;
  std::vector<uint32_t> rootIds;
};

class GraphBuilder {
public:
  SerializedASTGraph build(const std::vector<ASTNode> &roots) {
    SerializedASTGraph graph;
    graph.rootIds.reserve(roots.size());
    for (const auto &root : roots) {
      graph.rootIds.push_back(visit(root, graph.nodes));
    }
    return graph;
  }

private:
  std::unordered_map<const AST *, uint32_t> m_ptrToId;

  uint32_t visit(const ASTNode &node,
                 std::vector<SerializedNodeRecord> &outNodes) {
    if (!node) {
      return 0;
    }
    auto it = m_ptrToId.find(node.get());
    if (it != m_ptrToId.end()) {
      return it->second;
    }

    uint32_t id = static_cast<uint32_t>(outNodes.size() + 1);
    m_ptrToId[node.get()] = id;
    outNodes.emplace_back();

    SerializedNodeRecord rec;
    const AST::Token tok = node->getNodeType();
    rec.token = static_cast<uint8_t>(tok);
    rec.line = node->getLine();
    rec.column = node->getColumn();
    rec.filename = node->getFilename();
    rec.relativeFilename = node->getRelativeFilename();
    rec.absoluteFilename = node->getAbsoluteFilename();
    rec.scope = node->getNamespaceList();

    const auto &children = node->getChildren();
    rec.childrenIds.reserve(children.size());
    for (const auto &child : children) {
      rec.childrenIds.push_back(visit(child, outNodes));
    }

    auto compProps = node->getCompilerPropertiesList();
    if (compProps && !compProps->getChildren().empty()) {
      rec.compilerPropertiesId = visit(compProps, outNodes);
    }

    switch (tok) {
    case AST::None: {
      if (auto valNode = dynamic_cast<ValueNode *>(node.get())) {
        rec.subType = 1;
        rec.domainId = visit(valNode->getDomain(), outNodes);
      } else {
        rec.subType = 0;
      }
      break;
    }
    case AST::Platform: {
      auto *sys = static_cast<const SystemNode *>(node.get());
      rec.name = sys->platformName();
      rec.intVal = (static_cast<int64_t>(sys->majorVersion()) << 32) |
                   static_cast<uint32_t>(sys->minorVersion());
      rec.extraStrings = sys->hwPlatforms();
      break;
    }
    case AST::Array: {
      auto *arr = static_cast<const ArrayNode *>(node.get());
      rec.name = arr->getName();
      break;
    }
    case AST::Declaration: {
      auto *decl = static_cast<const DeclarationNode *>(node.get());
      rec.name = decl->getName();
      rec.secondaryStr = decl->getEntityType();
      break;
    }
    case AST::ArrayDeclaration: {
      auto *decl = static_cast<const DeclarationNode *>(node.get());
      rec.secondaryStr = decl->getEntityType();
      break;
    }
    case AST::Property: {
      auto *prop = static_cast<const PropertyNode *>(node.get());
      rec.name = prop->getName();
      break;
    }
    case AST::Import: {
      auto *imp = static_cast<const ImportNode *>(node.get());
      rec.name = imp->importName();
      rec.secondaryStr = imp->importAlias();
      break;
    }
    case AST::Package: {
      auto *pkg = static_cast<const PackageNode *>(node.get());
      rec.name = pkg->packageName();
      break;
    }
    case AST::Scope: {
      auto *sc = static_cast<const ScopeNode *>(node.get());
      rec.name = sc->getName();
      break;
    }
    case AST::MemberAccess: {
      auto *ma = static_cast<const MemberAccessNode *>(node.get());
      rec.name = ma->getEntity();
      rec.secondaryStr = ma->getPropertyName();
      break;
    }
    case AST::Int: {
      auto *val = static_cast<ValueNode *>(node.get());
      rec.intVal = val->getIntValue();
      rec.domainId = visit(val->getDomain(), outNodes);
      break;
    }
    case AST::Real: {
      auto *val = static_cast<ValueNode *>(node.get());
      rec.realVal = val->getRealValue();
      rec.domainId = visit(val->getDomain(), outNodes);
      break;
    }
    case AST::String: {
      auto *val = static_cast<ValueNode *>(node.get());
      rec.name = val->getStringValue();
      rec.domainId = visit(val->getDomain(), outNodes);
      break;
    }
    case AST::Switch: {
      auto *val = static_cast<ValueNode *>(node.get());
      rec.subType = val->getSwitchValue() ? 1 : 0;
      rec.domainId = visit(val->getDomain(), outNodes);
      break;
    }
    case AST::Entity: {
      auto *ent = static_cast<const EntityNode *>(node.get());
      rec.name = ent->getName();
      break;
    }
    case AST::Expression: {
      auto *expr = static_cast<const ExpressionNode *>(node.get());
      rec.subType = static_cast<uint8_t>(expr->getExpressionType());
      break;
    }
    case AST::Function: {
      auto *fn = static_cast<const FunctionNode *>(node.get());
      rec.name = fn->getName();
      rec.realVal = fn->getRate();
      break;
    }
    case AST::Keyword: {
      auto *kw = static_cast<KeywordNode *>(node.get());
      rec.name = kw->keyword();
      break;
    }
    case AST::Stream:
    case AST::Range:
    case AST::List:
    default:
      break;
    }

    outNodes[id - 1] = std::move(rec);
    return id;
  }
};

bool reconstructGraph(const SerializedASTGraph &graph,
                      std::vector<ASTNode> &outRoots) {
  static const ASTNode kDummyNode = std::make_shared<AST>();
  static const auto kDummyList = std::make_shared<ListNode>("", -1);

  std::vector<ASTNode> nodes(graph.nodes.size() + 1, nullptr);

  // Pass 1: Instantiate all AST node objects
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto &rec = graph.nodes[i];
    const auto tok = static_cast<AST::Token>(rec.token);
    const char *fname = rec.filename.c_str();
    const int line = rec.line;

    ASTNode created;
    switch (tok) {
    case AST::None: {
      if (rec.subType == 1) {
        created = std::make_shared<ValueNode>(fname, line);
        created->setNamespaceList(rec.scope);
      } else {
        created = std::make_shared<AST>(AST::None, fname, line, rec.scope);
      }
      break;
    }
    case AST::Platform: {
      int major = static_cast<int>(rec.intVal >> 32);
      int minor = static_cast<int>(static_cast<int32_t>(rec.intVal & 0xFFFFFFFF));
      created = std::make_shared<SystemNode>(rec.name, major, minor, fname,
                                             line, rec.extraStrings);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Array: {
      created = std::make_shared<ArrayNode>(rec.name, kDummyList, fname, line,
                                            rec.scope);
      break;
    }
    case AST::Declaration: {
      created = std::make_shared<DeclarationNode>(
          rec.name, rec.secondaryStr, nullptr, fname, line, rec.scope);
      break;
    }
    case AST::ArrayDeclaration: {
      auto dummyArr = std::make_shared<ArrayNode>("", kDummyList, fname, line);
      created = std::make_shared<DeclarationNode>(
          dummyArr, rec.secondaryStr, nullptr, fname, line, rec.scope);
      break;
    }
    case AST::Stream: {
      created = std::make_shared<StreamNode>(kDummyNode, kDummyNode, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Property: {
      created = std::make_shared<PropertyNode>(rec.name, kDummyNode, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Range: {
      created = std::make_shared<RangeNode>(kDummyNode, kDummyNode, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::List: {
      created = std::make_shared<ListNode>(fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Import: {
      created = std::make_shared<ImportNode>(rec.name, fname, line,
                                             rec.secondaryStr);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Package: {
      created = std::make_shared<PackageNode>(rec.name, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Scope: {
      created = std::make_shared<ScopeNode>(rec.name, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::MemberAccess: {
      created = std::make_shared<MemberAccessNode>(rec.name, rec.secondaryStr,
                                                   fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Int: {
      created = std::make_shared<ValueNode>(rec.intVal, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Real: {
      created = std::make_shared<ValueNode>(rec.realVal, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::String: {
      created = std::make_shared<ValueNode>(rec.name, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Switch: {
      created = std::make_shared<ValueNode>(rec.subType != 0, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Entity: {
      created = std::make_shared<EntityNode>(rec.name, fname, line, rec.scope);
      break;
    }
    case AST::Expression: {
      auto etype = static_cast<ExpressionNode::ExpressionType>(rec.subType);
      if (etype == ExpressionNode::UnaryMinus ||
          etype == ExpressionNode::LogicalNot) {
        created = std::make_shared<ExpressionNode>(etype, kDummyNode, fname, line);
      } else {
        created = std::make_shared<ExpressionNode>(etype, kDummyNode, kDummyNode,
                                                   fname, line);
      }
      created->setNamespaceList(rec.scope);
      break;
    }
    case AST::Function: {
      auto fn = std::make_shared<FunctionNode>(rec.name, nullptr, fname, line);
      fn->setNamespaceList(rec.scope);
      fn->setRate(rec.realVal);
      created = fn;
      break;
    }
    case AST::Keyword: {
      created = std::make_shared<KeywordNode>(rec.name, fname, line);
      created->setNamespaceList(rec.scope);
      break;
    }
    default:
      return false;
    }

    if (created) {
      created->setColumn(rec.column);
      created->setFilenames(rec.relativeFilename, rec.absoluteFilename);
    }

    nodes[i + 1] = std::move(created);
  }

  // Pass 2: Wire children, compiler properties, and domains
  for (size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto &rec = graph.nodes[i];
    ASTNode &node = nodes[i + 1];
    if (!node) {
      return false;
    }

    std::vector<ASTNode> children;
    children.reserve(rec.childrenIds.size());
    for (uint32_t cid : rec.childrenIds) {
      if (cid >= nodes.size()) {
        return false;
      }
      children.push_back(nodes[cid]);
    }
    node->setChildren(children);

    if (rec.compilerPropertiesId > 0) {
      if (rec.compilerPropertiesId >= nodes.size()) {
        return false;
      }
      auto cpNode = nodes[rec.compilerPropertiesId];
      if (cpNode && cpNode->getNodeType() == AST::List) {
        node->setCompilerPropertiesList(
            std::static_pointer_cast<ListNode>(cpNode));
      }
    } else {
      node->setCompilerPropertiesList(nullptr);
    }

    if (rec.domainId > 0) {
      if (rec.domainId >= nodes.size()) {
        return false;
      }
      if (auto valNode = std::dynamic_pointer_cast<ValueNode>(node)) {
        valNode->setDomain(nodes[rec.domainId]);
      }
    }
  }

  outRoots.clear();
  outRoots.reserve(graph.rootIds.size());
  for (uint32_t rid : graph.rootIds) {
    if (rid >= nodes.size()) {
      return false;
    }
    outRoots.push_back(nodes[rid]);
  }
  return true;
}

class ZppBitsASTSerializer final : public IASTSerializer {
public:
  uint32_t getFormatFourCC() const noexcept override {
    return ASTSerializerFactory::kZppBitsFourCC;
  }

  std::string_view getFormatName() const noexcept override {
    return "zpp_bits";
  }

  uint32_t getFormatVersion() const noexcept override { return 1; }

  bool serialize(const ASTNode &root, std::ostream &out) const override {
    std::vector<std::byte> bytes;
    if (!serializeToBytes(root, bytes)) {
      return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
  }

  bool deserialize(std::istream &in, ASTNode &outRoot) const override {
    std::vector<char> buf((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> byteBuf(buf.size());
    std::memcpy(byteBuf.data(), buf.data(), buf.size());
    return deserializeFromBytes(byteBuf, outRoot);
  }

  bool serializeList(const std::vector<ASTNode> &nodes,
                     std::ostream &out) const override {
    std::vector<std::byte> bytes;
    if (!serializeListToBytes(nodes, bytes)) {
      return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
  }

  bool deserializeList(std::istream &in,
                       std::vector<ASTNode> &outNodes) const override {
    std::vector<char> buf((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> byteBuf(buf.size());
    std::memcpy(byteBuf.data(), buf.data(), buf.size());
    return deserializeListFromBytes(byteBuf, outNodes);
  }

  bool serializeToBytes(const ASTNode &root,
                        std::vector<std::byte> &outBytes) const override {
    std::vector<ASTNode> list{root};
    return serializeListToBytes(list, outBytes);
  }

  bool deserializeFromBytes(const std::vector<std::byte> &bytes,
                            ASTNode &outRoot) const override {
    std::vector<ASTNode> list;
    if (!deserializeListFromBytes(bytes, list) || list.empty()) {
      return false;
    }
    outRoot = list[0];
    return true;
  }

  bool serializeListToBytes(const std::vector<ASTNode> &nodes,
                            std::vector<std::byte> &outBytes) const override {
    GraphBuilder builder;
    SerializedASTGraph graph = builder.build(nodes);
    outBytes.clear();
    auto out = zpp::bits::out(outBytes);
    auto err = out(graph);
    return !zpp::bits::failure(static_cast<std::errc>(err));
  }

  bool deserializeListFromBytes(const std::vector<std::byte> &bytes,
                                std::vector<ASTNode> &outNodes) const override {
    if (bytes.empty()) {
      return false;
    }
    SerializedASTGraph graph;
    auto in = zpp::bits::in(bytes);
    auto err = in(graph);
    if (zpp::bits::failure(static_cast<std::errc>(err))) {
      return false;
    }
    return reconstructGraph(graph, outNodes);
  }
};

} // namespace

std::unique_ptr<IASTSerializer> ASTSerializerFactory::createDefault() {
  return std::make_unique<ZppBitsASTSerializer>();
}

std::unique_ptr<IASTSerializer>
ASTSerializerFactory::createByFourCC(uint32_t fourCC) {
  if (fourCC == kZppBitsFourCC) {
    return std::make_unique<ZppBitsASTSerializer>();
  }
  return nullptr;
}

} // namespace strd
