#include "stride/utils/stridelibrary.h"
#include "stride/parser/ast.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/cachemanager.h"

#include <cassert>
#include <filesystem>

using namespace strd;

StrideLibrary::StrideLibrary() : m_majorVersion(1), m_minorVersion(0) {}

StrideLibrary::~StrideLibrary() {}

void StrideLibrary::initializeLibrary(std::string strideRootPath,
                                      std::vector<std::string> includePaths) {
  m_includePaths = includePaths;
  m_libraryTrees.clear();
  readLibrary(strideRootPath);
}

std::shared_ptr<DeclarationNode>
StrideLibrary::findTypeInLibrary(std::string typeName) {
  for (const auto &libraryTree : m_libraryTrees) {
    auto nodes = libraryTree.nodes;
    for (const ASTNode &node : nodes) {
      if (node->getNodeType() == AST::Declaration) {
        std::shared_ptr<DeclarationNode> entityDecl =
            std::static_pointer_cast<DeclarationNode>(node);
        if (entityDecl->getEntityType() != "type") {
          continue;
        }
        ASTNode value = entityDecl->getPropertyValue("typeName");
        if (value->getNodeType() == AST::String) {
          if (std::static_pointer_cast<ValueNode>(value)->getStringValue() ==
              typeName) {
            return entityDecl;
          }
        }
      }
    }
  }
  return nullptr;
}

bool StrideLibrary::isValidEntity(DeclarationNode *entityDecl) {
  std::shared_ptr<DeclarationNode> type =
      findTypeInLibrary(entityDecl->getEntityType());
  if (type) {
    if (entityDecl->getProperties().size() == 0) {
      return true; // FIXME we need to check if properties are required
    }
    for (const std::shared_ptr<PropertyNode> &property :
         entityDecl->getProperties()) {
      if (isValidProperty(property, type.get())) {
        return true;
      }
      // Now check for inherited properties
      std::vector<DeclarationNode *> parentTypes = getParentTypes(entityDecl);
      bool propertyInParent = false;
      for (DeclarationNode *parent : parentTypes) {
        propertyInParent |= isValidProperty(property, parent);
      }
      if (propertyInParent)
        return true;
    }
  }
  return false;
}

std::map<std::string, std::vector<ASTNode>> StrideLibrary::getLibraryMembers() {
  std::map<std::string, std::vector<ASTNode>> libNamespace;
  for (const auto &libraryTree : m_libraryTrees) {
    if (libraryTree.nodes.empty()) {
      continue;
    }
    if (libNamespace.find(libraryTree.importAs) == libNamespace.end()) {
      libNamespace[libraryTree.importAs] = std::vector<ASTNode>();
    }
    for (const ASTNode &node : libraryTree.nodes) {
      libNamespace[libraryTree.importAs].push_back(node);
    }
  }
  return libNamespace;
}

std::vector<ASTNode> StrideLibrary::loadImport(std::string importName,
                                               std::string importAs) {
  // Normalize importName: replace "::" with "/"
  std::string normalized = importName;
  size_t pos = 0;
  while ((pos = normalized.find("::", pos)) != std::string::npos) {
    normalized.replace(pos, 2, "/");
    pos += 1;
  }

  std::vector<ASTNode> newNodes;

  if (normalized.empty()) {
    // Root library load
    if (!m_libraryPath.empty() && std::filesystem::exists(m_libraryPath) &&
        std::filesystem::is_directory(m_libraryPath)) {
      newNodes = ASTFunctions::loadAllInDirectory(m_libraryPath);
    }
    if (!newNodes.empty()) {
      m_libraryTrees.push_back(LibraryTree{importName, importAs, newNodes, {}});
    }
    return newNodes;
  }

  std::vector<std::string> searchPaths = m_includePaths;
  if (!m_libraryPath.empty()) {
    searchPaths.push_back(m_libraryPath);
  }

  auto loadOrParseFile = [](const std::filesystem::path &filePath) -> ASTNode {
    if (auto cached = CacheManager::instance().loadCachedAST(filePath)) {
      return cached;
    }
    auto parsed = AST::parseFile(filePath.string().c_str(), nullptr);
    if (parsed) {
      CacheManager::instance().saveCachedAST(filePath, parsed);
    }
    return parsed;
  };

  for (const auto &includePath : searchPaths) {
    std::filesystem::path p = includePath;
    std::filesystem::path fileCandidate = p / (normalized + ".stride");
    std::filesystem::path dirCandidate = p / normalized;

    bool fileExists = std::filesystem::exists(fileCandidate) &&
                      std::filesystem::is_regular_file(fileCandidate);
    bool dirExists = std::filesystem::exists(dirCandidate) &&
                     std::filesystem::is_directory(dirCandidate);

    if (fileExists && dirExists) {
      // Package Root Model: load barrel file + directory sub-modules
      auto fileTree = loadOrParseFile(fileCandidate);
      if (fileTree) {
        auto packageNodes = ASTQuery::getPackageNodes(fileTree);
        std::vector<std::string> pkgSegs;
        if (!packageNodes.empty()) {
          pkgSegs = packageNodes[0]->packageSegments();
        } else if (!importAs.empty()) {
          pkgSegs = {importAs};
        }
        for (const auto &child : fileTree->getChildren()) {
          if (child->getNodeType() == AST::Declaration ||
              child->getNodeType() == AST::ArrayDeclaration) {
            if (!pkgSegs.empty() &&
                !child->getCompilerProperty("namespaceTree")) {
              for (const auto &seg : pkgSegs) {
                child->appendToPropertyValue(
                    "namespaceTree",
                    std::make_shared<ValueNode>(seg, __FILE__, __LINE__));
              }
            }
            newNodes.push_back(child);
          }
        }
      }
      auto dirNodes = ASTFunctions::loadAllInDirectory(dirCandidate.string());
      for (const auto &dNode : dirNodes) {
        if (dNode->getNodeType() == AST::Declaration ||
            dNode->getNodeType() == AST::ArrayDeclaration) {
          if (!importAs.empty()) {
            dNode->appendToPropertyValue(
                "namespaceTree",
                std::make_shared<ValueNode>(importAs, __FILE__, __LINE__));
          }
          newNodes.push_back(dNode);
        }
      }
      break;
    } else if (dirExists) {
      auto dirNodes = ASTFunctions::loadAllInDirectory(dirCandidate.string());
      for (const auto &dNode : dirNodes) {
        if (dNode->getNodeType() == AST::Declaration ||
            dNode->getNodeType() == AST::ArrayDeclaration) {
          if (!importAs.empty()) {
            dNode->appendToPropertyValue(
                "namespaceTree",
                std::make_shared<ValueNode>(importAs, __FILE__, __LINE__));
          }
          newNodes.push_back(dNode);
        }
      }
      break;
    } else if (fileExists) {
      auto fileTree = loadOrParseFile(fileCandidate);
      if (fileTree) {
        auto packageNodes = ASTQuery::getPackageNodes(fileTree);
        std::vector<std::string> pkgSegs;
        if (!packageNodes.empty()) {
          pkgSegs = packageNodes[0]->packageSegments();
        } else if (!importAs.empty()) {
          pkgSegs = {importAs};
        }
        for (const auto &child : fileTree->getChildren()) {
          if (child->getNodeType() == AST::Declaration ||
              child->getNodeType() == AST::ArrayDeclaration) {
            if (!pkgSegs.empty() &&
                !child->getCompilerProperty("namespaceTree")) {
              for (const auto &seg : pkgSegs) {
                child->appendToPropertyValue(
                    "namespaceTree",
                    std::make_shared<ValueNode>(seg, __FILE__, __LINE__));
              }
            }
            newNodes.push_back(child);
          }
        }
      }
      break;
    }
  }

  if (!newNodes.empty()) {
    m_libraryTrees.push_back(LibraryTree{importName, importAs, newNodes, {}});
  }
  return newNodes;
}

bool StrideLibrary::isValidProperty(std::shared_ptr<PropertyNode> property,
                                    DeclarationNode *type) {
  assert(type->getEntityType() == "type");
  std::shared_ptr<PropertyNode> portsInType =
      ASTQuery::findPropertyByName(type->getProperties(), "properties");
  ListNode *portList = static_cast<ListNode *>(portsInType->getValue().get());
  assert(portList->getNodeType() == AST::List);
  for (const ASTNode &port : portList->getChildren()) {
    DeclarationNode *portBlock = static_cast<DeclarationNode *>(port.get());
    assert(portBlock->getNodeType() == AST::Declaration);
    assert(portBlock->getEntityType() == "typeProperty");
    std::shared_ptr<PropertyNode> portName =
        ASTQuery::findPropertyByName(portBlock->getProperties(), "name");
    std::string portNameInType =
        static_cast<ValueNode *>(portName->getValue().get())->getStringValue();
    if (property->getName() == portNameInType) {
      return true;
    }
  }
  std::shared_ptr<PropertyNode> inherits =
      ASTQuery::findPropertyByName(type->getProperties(), "inherits");
  if (inherits) {
    ValueNode *inheritedTypeName =
        static_cast<ValueNode *>(inherits->getValue().get());
    assert(inheritedTypeName->getNodeType() == AST::String);
    std::shared_ptr<DeclarationNode> inheritedType =
        findTypeInLibrary(inheritedTypeName->getStringValue());
    assert(inheritedType != nullptr);
    if (isValidProperty(property, inheritedType.get())) {
      return true;
    }
  }
  return false;
}

std::vector<DeclarationNode *>
StrideLibrary::getParentTypes(DeclarationNode *type) {
  std::shared_ptr<PropertyNode> inheritProperty =
      ASTQuery::findPropertyByName(type->getProperties(), "inherits");
  if (inheritProperty) {
    ASTNode parentType = inheritProperty->getValue();
    if (parentType->getNodeType() == AST::String) {
      std::string parentBlockName =
          static_cast<ValueNode *>(parentType.get())->getStringValue();
      std::shared_ptr<DeclarationNode> parentBlock =
          findTypeInLibrary(parentBlockName);
      return getParentTypes(parentBlock.get());
    }
  }
  return std::vector<DeclarationNode *>();
}

void StrideLibrary::readLibrary(std::string rootDir) {
  std::string basepath = "/library/" + std::to_string(m_majorVersion) + "." +
                         std::to_string(m_minorVersion);
  //    QMapIterator<QString, QStringList> it(importList);
  //    importList[""] = QStringList() << ""; // Add root namespace
  m_libraryPath = rootDir + basepath;

  loadImport("", "");
}
