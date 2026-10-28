#include "stride/utils/astquery.h"
#include "stride/parser/strideparser.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace strd;

namespace {
bool declMatchesNameAndAt(const std::string &objectName,
                          const std::shared_ptr<DeclarationNode> &decl,
                          bool matchAnyAtIfUnspecified = false) {
  if (!decl) {
    return false;
  }
  size_t atPos = objectName.find('@');
  if (atPos != std::string::npos) {
    std::string nameWithoutAt = objectName.substr(0, atPos);
    std::string atTag = objectName.substr(atPos + 1);
    if (decl->getName() == nameWithoutAt) {
      auto atProp = decl->getCompilerProperty("_at");
      if (atProp) {
        if (atProp->getNodeType() == AST::String) {
          auto valNode = std::static_pointer_cast<ValueNode>(atProp);
          if (valNode->getStringValue() == atTag) {
            return true;
          }
        } else if (atProp->getNodeType() == AST::Int) {
          auto valNode = std::static_pointer_cast<ValueNode>(atProp);
          if (std::to_string(valNode->getIntValue()) == atTag) {
            return true;
          }
        }
      }
    }
    return false;
  }
  if (decl->getName() == objectName) {
    auto atProp = decl->getCompilerProperty("_at");
    if (!atProp || matchAnyAtIfUnspecified) {
      return true;
    }
  }
  return false;
}
} // namespace

std::vector<StreamBlockDescriptor>
ASTQuery::getContainedStreamBlocks(std::shared_ptr<DeclarationNode> decl,
                                   const ScopeStack &scope, ASTNode tree) {
  std::vector<StreamBlockDescriptor> descriptors;
  if (!decl) {
    return descriptors;
  }

  std::set<std::string> collectedProps;

  auto collectStreamsFromValue =
      [](ASTNode propVal) -> std::vector<std::shared_ptr<StreamNode>> {
    std::vector<std::shared_ptr<StreamNode>> streams;
    if (!propVal) {
      return streams;
    }
    if (propVal->getNodeType() == AST::Stream) {
      streams.push_back(std::static_pointer_cast<StreamNode>(propVal));
    } else if (propVal->getNodeType() == AST::List) {
      for (const auto &c : propVal->getChildren()) {
        if (c && c->getNodeType() == AST::Stream) {
          streams.push_back(std::static_pointer_cast<StreamNode>(c));
        }
      }
    }
    return streams;
  };

  auto isStreamTypeRef = [](const ASTNode &node) -> bool {
    if (!node)
      return false;
    if (node->getNodeType() == AST::Entity) {
      const std::string &name =
          std::static_pointer_cast<EntityNode>(node)->getName();
      return name == "_StreamType" || name == "_Stream" || name == "stream";
    }
    if (node->getNodeType() == AST::Declaration) {
      const std::string &name =
          std::static_pointer_cast<DeclarationNode>(node)->getName();
      return name == "_StreamType" || name == "_Stream" || name == "stream";
    }
    if (node->getNodeType() == AST::String) {
      const std::string &val =
          std::static_pointer_cast<ValueNode>(node)->getStringValue();
      return val == "_StreamType" || val == "_Stream" || val == "stream";
    }
    return false;
  };

  auto checkTypeItem = [&](const ASTNode &typeItem) -> bool {
    if (!typeItem)
      return false;
    if (isStreamTypeRef(typeItem)) {
      return true;
    }
    if (typeItem->getNodeType() == AST::Declaration) {
      auto d = std::static_pointer_cast<DeclarationNode>(typeItem);
      auto isStreamBlockNode = d->getPropertyValue("isStreamBlock");
      if (isStreamBlockNode &&
          isStreamBlockNode->getNodeType() == AST::Switch) {
        if (std::static_pointer_cast<ValueNode>(isStreamBlockNode)
                ->getSwitchValue()) {
          return true;
        }
      }
      for (const char *listPropName : {"allowed", "allowedTypes", "types"}) {
        auto listProp = d->getPropertyValue(listPropName);
        if (listProp) {
          if (listProp->getNodeType() == AST::List) {
            for (const auto &c : listProp->getChildren()) {
              if (isStreamTypeRef(c))
                return true;
            }
          } else if (isStreamTypeRef(listProp)) {
            return true;
          }
        }
      }
    }
    return false;
  };

  // 1. Schema reflection
  std::vector<ASTNode> portList;
  auto typeDecl = ASTQuery::findTypeDeclaration(decl, scope, tree);
  if (typeDecl) {
    portList = ASTQuery::getPortsForTypeEntity(typeDecl, scope, tree);
  }
  if (portList.empty()) {
    portList = ASTQuery::getPortsForType(decl->getEntityType(), scope, tree);
  }

  for (const ASTNode &portNode : portList) {
    if (portNode->getNodeType() != AST::Declaration) {
      continue;
    }
    auto propDef = std::static_pointer_cast<DeclarationNode>(portNode);
    auto nameVal = propDef->getPropertyValue("name");
    if (!nameVal || nameVal->getNodeType() != AST::String) {
      continue;
    }
    std::string propName =
        std::static_pointer_cast<ValueNode>(nameVal)->getStringValue();

    if (collectedProps.count(propName)) {
      continue;
    }

    bool isStreamContainer = false;

    // Check isStreamBlock: on
    auto isStreamBlockNode = propDef->getPropertyValue("isStreamBlock");
    if (isStreamBlockNode && isStreamBlockNode->getNodeType() == AST::Switch) {
      isStreamContainer = std::static_pointer_cast<ValueNode>(isStreamBlockNode)
                              ->getSwitchValue();
    }

    // Check types / allowedTypes
    if (!isStreamContainer) {
      auto typesVal = propDef->getPropertyValue("allowedTypes");
      if (!typesVal) {
        typesVal = propDef->getPropertyValue("types");
      }
      if (typesVal) {
        if (typesVal->getNodeType() == AST::List) {
          for (const auto &typeItem : typesVal->getChildren()) {
            if (checkTypeItem(typeItem)) {
              isStreamContainer = true;
              break;
            }
          }
        } else {
          if (checkTypeItem(typesVal)) {
            isStreamContainer = true;
          }
        }
      }
    }

    if (isStreamContainer) {
      std::string executionStage;
      auto stageProp = propDef->getPropertyValue("executionStage");
      if (stageProp && stageProp->getNodeType() == AST::String) {
        executionStage =
            std::static_pointer_cast<ValueNode>(stageProp)->getStringValue();
      } else if (propName == "streams") {
        executionStage = "default";
      } else {
        executionStage = propName;
      }

      ASTNode propVal = decl->getPropertyValue(propName);
      auto streams = collectStreamsFromValue(propVal);
      if (!streams.empty()) {
        descriptors.push_back({propName, std::move(streams), executionStage});
      }
      collectedProps.insert(propName);
    }
  }

  // 2. Structural fallback:
  // If no schema was available or if properties holding streams were not
  // defined in schema, inspect decl->getProperties(). Any property whose value
  // is an AST::Stream or a ListNode of AST::Streams is automatically collected.
  if (portList.empty() || collectedProps.empty()) {
    for (const auto &prop : decl->getProperties()) {
      if (!prop)
        continue;
      std::string propName = prop->getName();
      if (propName == "blocks")
        continue;
      if (collectedProps.count(propName))
        continue;

      auto streams = collectStreamsFromValue(prop->getValue());
      if (!streams.empty()) {
        std::string executionStage =
            (propName == "streams") ? "default" : propName;
        descriptors.push_back({propName, std::move(streams), executionStage});
        collectedProps.insert(propName);
      }
    }
  }

  // 3. Nested container traversal:
  // If an entity contains child declarations that are stream containers (e.g.
  // state.transitions containing transition objects), collect those
  // hierarchically.
  for (const auto &prop : decl->getProperties()) {
    if (!prop)
      continue;
    std::string propName = prop->getName();
    if (propName == "blocks")
      continue; // child blocks are scoped separately
    if (propName == "streams")
      continue;
    ASTNode propVal = prop->getValue();
    if (!propVal)
      continue;

    if (propVal->getNodeType() == AST::Declaration ||
        propVal->getNodeType() == AST::ArrayDeclaration) {
      auto childDecl = std::static_pointer_cast<DeclarationNode>(propVal);
      auto childBlocks = getContainedStreamBlocks(childDecl, scope, tree);
      descriptors.insert(descriptors.end(), childBlocks.begin(),
                         childBlocks.end());
    } else if (propVal->getNodeType() == AST::List) {
      for (const auto &item : propVal->getChildren()) {
        if (item && (item->getNodeType() == AST::Declaration ||
                     item->getNodeType() == AST::ArrayDeclaration)) {
          auto childDecl = std::static_pointer_cast<DeclarationNode>(item);
          auto childBlocks = getContainedStreamBlocks(childDecl, scope, tree);
          descriptors.insert(descriptors.end(), childBlocks.begin(),
                             childBlocks.end());
        }
      }
    }
  }

  return descriptors;
}

std::vector<std::shared_ptr<SystemNode>>
ASTQuery::getSystemNodes(ASTNode tree) {
  //  Q_ASSERT(m_tree);
  std::vector<std::shared_ptr<SystemNode>> platformNodes;
  if (!tree) {
    return platformNodes;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Platform) {
      platformNodes.push_back(std::static_pointer_cast<SystemNode>(node));
    }
  }
  return platformNodes;
}

std::vector<std::shared_ptr<ImportNode>>
ASTQuery::getImportNodes(ASTNode tree) {
  std::vector<std::shared_ptr<ImportNode>> importList;
  if (!tree) {
    return importList;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Import) {
      std::shared_ptr<ImportNode> import =
          std::static_pointer_cast<ImportNode>(node);
      // FIXME add namespace support here (e.g. import
      // Platform::Filters::Filter)
      bool imported = false;
      for (const auto &importNode : importList) {
        if ((std::static_pointer_cast<ImportNode>(importNode)->importName() ==
             import->importName()) &&
            (std::static_pointer_cast<ImportNode>(importNode)->importAlias() ==
             import->importAlias())) {
          imported = true;
          break;
        }
      }
      if (!imported) {
        importList.push_back(import);
      }
    }
  }
  return importList;
}

std::vector<std::shared_ptr<PackageNode>>
ASTQuery::getPackageNodes(ASTNode tree) {
  std::vector<std::shared_ptr<PackageNode>> packageList;
  if (!tree) {
    return packageList;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Package) {
      packageList.push_back(std::static_pointer_cast<PackageNode>(node));
    }
  }
  return packageList;
}

std::shared_ptr<DeclarationNode> ASTQuery::findDeclarationWithType(
    std::string objectName, std::string type, const ScopeStack &scopeStack,
    ASTNode tree, std::vector<std::string> namespaces, std::string framework) {
  auto decls = ASTQuery::findAllDeclarations(objectName, scopeStack, tree,
                                             namespaces, framework);
  for (const auto &decl : decls) {
    if (decl && decl->getEntityType() == type) {
      return decl;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<DeclarationNode>> ASTQuery::findAllDeclarations(
    std::string objectName, const ScopeStack &scopeStack, ASTNode tree,
    std::vector<std::string> namespaces, std::string currentFramework) {
  std::vector<std::shared_ptr<DeclarationNode>> decls;
  std::vector<std::string> scopesList = namespaces;
  std::string baseName = objectName;
  size_t pos = 0;
  while ((pos = baseName.find("::")) != std::string::npos) {
    scopesList.push_back(baseName.substr(0, pos));
    baseName = baseName.substr(pos + 2);
  }
  objectName = baseName;

  for (auto subScopeIt = scopeStack.rbegin(); subScopeIt != scopeStack.rend();
       subScopeIt++) {
    auto subScope = *subScopeIt;
    for (const ASTNode &scopeNode : subScope.second) {
      if (scopeNode->getNodeType() == AST::ArrayDeclaration ||
          scopeNode->getNodeType() == AST::Declaration) {
        std::shared_ptr<DeclarationNode> decl =
            std::static_pointer_cast<DeclarationNode>(scopeNode);
        if (declMatchesNameAndAt(objectName, decl, true)) {
          if (ASTQuery::namespaceMatch(scopesList, decl, currentFramework)) {
            if (std::find(decls.begin(), decls.end(), decl) == decls.end()) {
              decls.push_back(decl);
            }
          } else {
            auto frameworkNode = decl->getCompilerProperty("framework");
            if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
              std::string frameworkName =
                  std::static_pointer_cast<ValueNode>(frameworkNode)
                      ->getStringValue();
              if (ASTQuery::namespaceMatch(scopesList, decl, frameworkName)) {
                if (std::find(decls.begin(), decls.end(), decl) ==
                    decls.end()) {
                  decls.push_back(decl);
                }
              }
            }
          }
        }
      }
    }
  }
  if (tree) {
    for (const ASTNode &node : tree->getChildren()) {
      if (node->getNodeType() == AST::ArrayDeclaration ||
          node->getNodeType() == AST::Declaration) {
        std::shared_ptr<DeclarationNode> decl =
            std::static_pointer_cast<DeclarationNode>(node);
        if (declMatchesNameAndAt(objectName, decl, true)) {
          if (ASTQuery::namespaceMatch(scopesList, decl, currentFramework)) {
            if (std::find(decls.begin(), decls.end(), decl) == decls.end()) {
              decls.push_back(decl);
            }
          }
        }
      }
    }
  }
  return decls;
}

std::shared_ptr<DeclarationNode>
ASTQuery::findTypeDeclaration(std::shared_ptr<DeclarationNode> entityDecl,
                              ScopeStack scope, ASTNode tree,
                              std::string currentFramework) {
  if (!entityDecl) {
    return nullptr;
  }
  std::string typeName = entityDecl->getEntityType();
  return ASTQuery::findTypeDeclarationByName(
      typeName, scope, tree, entityDecl->getNamespaceList(), currentFramework);
}

std::shared_ptr<DeclarationNode> ASTQuery::findTypeDeclarationByName(
    std::string typeName, ScopeStack scope, ASTNode tree,
    std::vector<std::string> namespaces, std::string currentFramework) {
  std::vector<std::string> scopesList = namespaces;
  std::string baseTypeName = typeName;
  size_t pos = 0;
  while ((pos = baseTypeName.find("::")) != std::string::npos) {
    scopesList.push_back(baseTypeName.substr(0, pos));
    baseTypeName = baseTypeName.substr(pos + 2);
  }
  typeName = baseTypeName;

  auto getDecl = [&](ASTNode scopeMember, std::string typeName,
                     std::vector<std::string> namespaces)
      -> std::shared_ptr<DeclarationNode> {
    std::shared_ptr<DeclarationNode> declarationNode =
        std::static_pointer_cast<DeclarationNode>(scopeMember);
    if (declarationNode->getEntityType() == "type" ||
        declarationNode->getEntityType() == "platformBlock") {
      ASTNode valueNode = declarationNode->getPropertyValue("typeName");
      if (valueNode && valueNode->getNodeType() == AST::String) {
        auto value = std::static_pointer_cast<ValueNode>(valueNode);
        if (typeName == value->getStringValue()) {
          if (ASTQuery::namespaceMatch(namespaces, declarationNode,
                                       currentFramework)) {
            return declarationNode;
          }
        }
      }
    } else if (declarationNode->getEntityType() == "platformModule") {
      if (typeName == declarationNode->getName()) {
        if (ASTQuery::namespaceMatch(namespaces, declarationNode,
                                     currentFramework)) {
          return declarationNode;
        }
      }
    }
    return nullptr;
  };
  if (tree) {
    for (const ASTNode &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Declaration ||
          node->getNodeType() == AST::ArrayDeclaration) {
        auto decl = getDecl(node, typeName, namespaces);
        if (decl) {
          return decl;
        }
      }
    }
  }
  for (const auto &subScope : scope) {
    for (const auto &scopeMember : subScope.second) {
      if (scopeMember->getNodeType() == AST::Declaration ||
          scopeMember->getNodeType() == AST::ArrayDeclaration) {
        auto decl = getDecl(scopeMember, typeName, namespaces);
        if (decl) {
          return decl;
        }
      }
    }
  }
  return nullptr;
}

std::string ASTQuery::getNodeName(ASTNode node) {
  if (!node) {
    return std::string();
  }
  if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::string n = name->getName();
    auto atProp = node->getCompilerProperty("_at");
    if (atProp) {
      if (atProp->getNodeType() == AST::String) {
        n += "@" + std::static_pointer_cast<ValueNode>(atProp)->getStringValue();
      } else if (atProp->getNodeType() == AST::Int) {
        n += "@" + std::to_string(std::static_pointer_cast<ValueNode>(atProp)->getIntValue());
      }
    }
    return n;
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::string n = arrayNode->getName();
    auto atProp = node->getCompilerProperty("_at");
    if (atProp) {
      if (atProp->getNodeType() == AST::String) {
        n += "@" + std::static_pointer_cast<ValueNode>(atProp)->getStringValue();
      } else if (atProp->getNodeType() == AST::Int) {
        n += "@" + std::to_string(std::static_pointer_cast<ValueNode>(atProp)->getIntValue());
      }
    }
    return n;
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    std::string n = func->getName();
    auto atProp = node->getCompilerProperty("_at");
    if (atProp) {
      if (atProp->getNodeType() == AST::String) {
        n += "@" + std::static_pointer_cast<ValueNode>(atProp)->getStringValue();
      } else if (atProp->getNodeType() == AST::Int) {
        n += "@" + std::to_string(std::static_pointer_cast<ValueNode>(atProp)->getIntValue());
      }
    }
    return n;
  } else if (node->getNodeType() == AST::List) {
    std::string listCommonName;
    for (const auto &elem : node->getChildren()) {
      auto elemName = getNodeName(elem);
      if (listCommonName.size() == 0) {
        listCommonName = elemName;
      } else if (elemName != listCommonName) {
        return std::string();
      }
    }
    return listCommonName;
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    std::string n = decl->getName();
    auto atProp = node->getCompilerProperty("_at");
    if (atProp) {
      if (atProp->getNodeType() == AST::String) {
        n += "@" + std::static_pointer_cast<ValueNode>(atProp)->getStringValue();
      } else if (atProp->getNodeType() == AST::Int) {
        n += "@" + std::to_string(std::static_pointer_cast<ValueNode>(atProp)->getIntValue());
      }
    }
    return n;
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      return getNodeName(expr->getValue());
    } else {
      return getNodeName(expr->getLeft());
    }
  } else {
  }
  return std::string();
}

int ASTQuery::getNodeSize(ASTNode node, const ScopeStack &scopeStack,
                          ASTNode tree) {
  int size = 1;
  if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    std::vector<LangError> errors;
    size = getArraySize(arrayNode, scopeStack, tree, &errors);
    if (errors.size() > 0) {
      return -1;
    }
    assert(errors.size() == 0);
    return size;
  } else if (node->getNodeType() == AST::Expression) {
    ExpressionNode *expr = static_cast<ExpressionNode *>(node.get());
    if (expr->isUnary()) {
      return getNodeSize(expr->getValue(), scopeStack, tree);
    } else {
      int leftSize = getNodeSize(expr->getLeft(), scopeStack, tree);
      int rightSize = getNodeSize(expr->getLeft(), scopeStack, tree);
      if (leftSize == rightSize) {
        return leftSize;
      } else {
        return -1;
      }
    }
  } else if (node->getNodeType() == AST::Entity) {
    EntityNode *entityNode = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> decl = ASTQuery::findDeclarationByName(
        entityNode->getName(), scopeStack, tree);
    if (!decl) {
      size = -1; // Entity not declared
    } else if (decl->getNodeType() == AST::ArrayDeclaration) {
      std::vector<LangError> errors;
      size = getEntityDeclaredSize(decl, {}, tree, &errors);
    } else if (decl->getNodeType() == AST::Declaration) {
      if (isGenerator(decl)) {
        std::vector<LangError> errors;
        size = getEntityDeclaredSize(decl, {}, tree, &errors);
      } else {
        size = 1;
      }
    } else {
      assert(0 == 1);
    }
  } else if (node->getNodeType() == AST::Function) {
    std::vector<std::shared_ptr<PropertyNode>> properties =
        static_cast<FunctionNode *>(node.get())->getProperties();
    std::vector<LangError> errors;
    size = getLargestPropertySize(properties, {}, tree, &errors);
  } else if (node->getNodeType() == AST::List) {
    size = node->getChildren().size();
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *st = static_cast<StreamNode *>(node.get());
    size = getNodeSize(st->getLeft(), scopeStack, tree);
  }

  return size;
}

int ASTQuery::getArraySize(std::shared_ptr<ArrayNode> arrayNode,
                           ScopeStack scope, ASTNode tree,
                           std::vector<LangError> *errors) {
  std::shared_ptr<ListNode> indexList = arrayNode->index();
  int size = 0;
  std::vector<ASTNode> listExprs = indexList->getChildren();
  std::string type;

  for (const ASTNode &expr : listExprs) {
    switch (expr->getNodeType()) {
    case AST::Int:
      size += 1; // FIXME looks wrong
      break;
    case AST::Range:
      size += ASTFunctions::evaluateConstInteger(
                  static_cast<RangeNode *>(expr.get())->endIndex(), scope, tree,
                  errors) -
              ASTFunctions::evaluateConstInteger(
                  static_cast<RangeNode *>(expr.get())->startIndex(), scope,
                  tree, errors) +
              1;
      break;
    case AST::GeneratorRange:
      size += ASTFunctions::evaluateConstInteger(
                  static_cast<GeneratorRangeNode *>(expr.get())->endIndex(),
                  scope, tree, errors) -
              ASTFunctions::evaluateConstInteger(
                  static_cast<GeneratorRangeNode *>(expr.get())->startIndex(),
                  scope, tree, errors) +
              1;
      break;
    case AST::Expression:
      type = ASTQuery::getNodeSize(expr, scope, tree);
      if (type == "_IntLiteral" || type == DataTypes::Int) {
        size += 1;
      } else {
        LOG_ERROR() << "Error, expression does not resolve to int" << std::endl;
      }
      break;
    case AST::Entity:
      size += 1;
      break;

    case AST::MemberAccess:
      size = -2;
      return size;
    default:
      break;
    }
  }

  return size;
}

bool ASTQuery::isGenerator(std::shared_ptr<DeclarationNode> decl) {
  if (!decl)
    return false;
  auto isGenProp = decl->getCompilerProperty("_isGenerator");
  return isGenProp && isGenProp->getNodeType() == AST::Switch &&
         std::static_pointer_cast<ValueNode>(isGenProp)->getSwitchValue();
}

int ASTQuery::getEntityDeclaredSize(std::shared_ptr<DeclarationNode> entityDecl,
                                    ScopeStack scope, ASTNode tree,
                                    std::vector<LangError> *errors) {
  if (entityDecl->getNodeType() == AST::Declaration) {
    if (isGenerator(entityDecl)) {
      auto rangeProp = entityDecl->getCompilerProperty("_generatorRange");
      if (rangeProp) {
        if (rangeProp->getNodeType() == AST::GeneratorRange) {
          auto genNode =
              std::static_pointer_cast<GeneratorRangeNode>(rangeProp);
          ASTNode start = genNode->startIndex();
          ASTNode end = genNode->endIndex();
          ASTNode step = genNode->stepIndex();
          int startIndex =
              ASTFunctions::evaluateConstInteger(start, scope, tree, errors);
          int endIndex =
              ASTFunctions::evaluateConstInteger(end, scope, tree, errors);
          int stepVal = 1;
          if (step) {
            stepVal =
                ASTFunctions::evaluateConstInteger(step, scope, tree, errors);
          }
          if (stepVal > 0 && endIndex >= startIndex) {
            return (endIndex - startIndex) / stepVal + 1;
          }
          return 0;
        } else if (rangeProp->getNodeType() == AST::Range) {
          auto rNode = std::static_pointer_cast<RangeNode>(rangeProp);
          ASTNode start = rNode->startIndex();
          ASTNode end = rNode->endIndex();
          int startIndex =
              ASTFunctions::evaluateConstInteger(start, scope, tree, errors);
          int endIndex =
              ASTFunctions::evaluateConstInteger(end, scope, tree, errors);
          if (endIndex >= startIndex) {
            return endIndex - startIndex + 1;
          }
          return 0;
        } else if (rangeProp->getNodeType() == AST::List) {
          return static_cast<int>(rangeProp->getChildren().size());
        } else if (rangeProp->getNodeType() == AST::Entity) {
          auto name =
              std::static_pointer_cast<EntityNode>(rangeProp)->getName();
          auto resolvedDecl = ASTQuery::findDeclarationByName(
              name, scope, tree, entityDecl->getNamespaceList());
          if (resolvedDecl) {
            return getEntityDeclaredSize(resolvedDecl, scope, tree, errors);
          }
        }
      }
      return -1;
    }
    if (entityDecl->getEntityType() == "buffer") {
      auto sizeNode = std::static_pointer_cast<DeclarationNode>(entityDecl)
                          ->getPropertyValue("size");
      if (sizeNode) {
        auto size =
            ASTFunctions::evaluateConstInteger(sizeNode, scope, tree, errors);
        return size;
      }
      return -1;
    } else {
      return 1;
    }
  }
  int size = -1;
  assert(entityDecl->getNodeType() == AST::ArrayDeclaration);
  ArrayNode *arrayNode =
      static_cast<ArrayNode *>(entityDecl->getArrayIndex().get());
  if (arrayNode->getNodeType() == AST::Array) {
    size = 0;
    auto indexList = arrayNode->index();
    std::vector<ASTNode> indexExps = indexList->getChildren();
    for (const ASTNode &exp : indexExps) {
      if (exp->getNodeType() == AST::Range) {
        RangeNode *range = static_cast<RangeNode *>(exp.get());
        ASTNode start = range->startIndex();
        int startIndex, endIndex;
        startIndex =
            ASTFunctions::evaluateConstInteger(start, scope, tree, errors);
        ASTNode end = range->endIndex();
        endIndex = ASTFunctions::evaluateConstInteger(end, scope, tree, errors);
        if (endIndex >= startIndex) {
          size += endIndex - startIndex + 1;
        }

      } else if (exp->getNodeType() == AST::GeneratorRange) {
        GeneratorRangeNode *genRange =
            static_cast<GeneratorRangeNode *>(exp.get());
        ASTNode start = genRange->startIndex();
        ASTNode end = genRange->endIndex();
        ASTNode step = genRange->stepIndex();
        int startIndex =
            ASTFunctions::evaluateConstInteger(start, scope, tree, errors);
        int endIndex =
            ASTFunctions::evaluateConstInteger(end, scope, tree, errors);
        int stepVal = 1;
        if (step) {
          stepVal =
              ASTFunctions::evaluateConstInteger(step, scope, tree, errors);
        }
        if (stepVal > 0 && endIndex >= startIndex) {
          size += (endIndex - startIndex) / stepVal + 1;
        }

      } else if (exp->getNodeType() == AST::MemberAccess) {
        return -2; // FIXME calculate actual size from external connection
      } else {
        std::vector<LangError> internalErrors;
        size += ASTFunctions::evaluateConstInteger(exp, scope, tree,
                                                   &internalErrors);
        if (internalErrors.size() > 0) {
          if (errors) {
            LangError error;
            error.type = LangError::InvalidIndexType;
            error.lineNumber = exp->getLine();
            error.errorTokens.push_back(arrayNode->getName());
            errors->push_back(error);
          }
        }
      }
    }
  }
  return size;
}

std::shared_ptr<DeclarationNode> ASTQuery::synthesizeArrayDeclarationElement(
    std::shared_ptr<DeclarationNode> arrayDeclaration, uint64_t index,
    ScopeStack scope, ASTNode tree) {
  if (arrayDeclaration->getNodeType() != AST::ArrayDeclaration) {
    return nullptr;
  }

  std::shared_ptr<DeclarationNode> newDecl = std::make_shared<DeclarationNode>(
      arrayDeclaration->getName(), arrayDeclaration->getEntityType(), nullptr,
      arrayDeclaration->getFilename().c_str(), arrayDeclaration->getLine(),
      arrayDeclaration->getColumn());
  arrayDeclaration->copyLocationTo(newDecl);
  for (const auto &prop : arrayDeclaration->getProperties()) {
    auto value = prop->getValue();
    // TODO support more complex scenarios, e.g. entities that reference lists,
    // array ranges, etc.
    if (value->getNodeType() == AST::List) {
      if (value->getChildren().size() > index) {
        newDecl->setPropertyValue(prop->getName(),
                                  value->getChildren()[index]->deepCopy());
      } else {
        newDecl->setPropertyValue(prop->getName(), value->deepCopy());
      }
    } else {
      newDecl->setPropertyValue(prop->getName(), prop->getValue()->deepCopy());
    }
  }

  return newDecl;
}

int ASTQuery::getLargestPropertySize(
    std::vector<std::shared_ptr<PropertyNode>> &properties, ScopeStack scope,
    ASTNode tree, std::vector<LangError> *errors) {
  int maxSize = 1;
  for (const auto &property : properties) {
    ASTNode value = property->getValue();
    if (value->getNodeType() == AST::Entity) {
      EntityNode *name = static_cast<EntityNode *>(value.get());
      std::shared_ptr<DeclarationNode> entityDecl =
          ASTQuery::findDeclarationByName(name->getName(), scope, tree);
      if (entityDecl) {
        if (entityDecl->getNodeType() == AST::Declaration) {
          if (maxSize < 1) {
            maxSize = 1;
          }
        } else if (entityDecl->getNodeType() == AST::ArrayDeclaration) {
          ASTNode index = entityDecl->getArrayIndex()->index();
          int newSize =
              ASTFunctions::evaluateConstInteger(index, {}, tree, errors);
          if (newSize > maxSize) {
            maxSize = newSize;
          }
        }
      }
    } else if (value->getNodeType() == AST::List) {
      ListNode *list = static_cast<ListNode *>(value.get());
      int newSize = list->getChildren().size();
      if (newSize > maxSize) {
        maxSize = newSize;
      }

    } else if (value->getNodeType() == AST::Int ||
               value->getNodeType() == AST::Real ||
               value->getNodeType() == AST::String) {
      if (maxSize < 1) {
        maxSize = 1;
      }
    }
  }
  return maxSize;
}

std::vector<std::shared_ptr<DeclarationNode>>
ASTQuery::getInheritedTypes(std::shared_ptr<DeclarationNode> entityTypeDecl,
                            const ScopeStack &scope, ASTNode tree) {
  std::vector<std::shared_ptr<DeclarationNode>> inheritedTypes;
  if (!entityTypeDecl) {
    return {};
  }
  ASTNode inherits = entityTypeDecl->getPropertyValue("inherits");
  if (inherits) {
    if (inherits->getNodeType() == AST::List) {
      for (const ASTNode &inheritsFromName : inherits->getChildren()) {
        if (inheritsFromName->getNodeType() == AST::String) {
          assert(0 == 1); // Disallowed
        } else if (inheritsFromName->getNodeType() == AST::Entity) {
          auto inheritsEntity =
              std::static_pointer_cast<EntityNode>(inheritsFromName);
          std::shared_ptr<DeclarationNode> inheritedDeclaration =
              ASTQuery::findDeclarationByName(inheritsEntity->getName(), scope,
                                              tree);
          if (inheritedDeclaration) {
            inheritedTypes.push_back(inheritedDeclaration);
            auto parentTypes =
                ASTQuery::getInheritedTypes(inheritedDeclaration, scope, tree);
            inheritedTypes.insert(inheritedTypes.end(), parentTypes.begin(),
                                  parentTypes.end());
          }
        }
      }
    } else if (inherits->getNodeType() == AST::String) {
      // String inherits is for framework inheritance, not type inheritance.
    } else if (inherits->getNodeType() == AST::Entity) {
      auto inheritsEntity = std::static_pointer_cast<EntityNode>(inherits);
      std::shared_ptr<DeclarationNode> inheritedDeclaration =
          ASTQuery::findDeclarationByName(inheritsEntity->getName(), scope,
                                          tree);
      if (inheritedDeclaration) {
        inheritedTypes.push_back(inheritedDeclaration);
        auto parentTypes =
            ASTQuery::getInheritedTypes(inheritedDeclaration, scope, tree);
        inheritedTypes.insert(inheritedTypes.end(), parentTypes.begin(),
                              parentTypes.end());
      }
    } else if (inherits->getNodeType() == AST::None) {
      // No inherits property, do nothing
    } else {
      LOG_INFO() << "Unexpected type for inherits property" << std::endl;
    }
  }
  return inheritedTypes;
}

std::vector<ASTNode>
ASTQuery::getInheritedPorts(std::shared_ptr<DeclarationNode> entityDecl,
                            ScopeStack scope, ASTNode tree) {
  std::vector<ASTNode> inheritedProperties;
  auto inheritedTypes = ASTQuery::getInheritedTypes(entityDecl, scope, tree);
  //  std::vector<std::string> inheritedName;
  for (const auto &typeDeclaration : inheritedTypes) {
    auto inheritedFromType =
        getPortsForTypeEntity(typeDeclaration, scope, tree);
    for (const ASTNode &property : inheritedFromType) {
      if (std::find(inheritedProperties.begin(), inheritedProperties.end(),
                    property) == inheritedProperties.end()) {
        inheritedProperties.push_back(property);
      }
      //      if (property->getNodeType() == AST::Declaration) {
      //        inheritedName.push_back(
      //            std::static_pointer_cast<DeclarationNode>(property)->getName());
      //      }
    }
  }
  return inheritedProperties;
}

std::vector<ASTNode>
ASTQuery::getPortsForType(std::string typeName, ScopeStack scope, ASTNode tree,
                          std::vector<std::string> namespaces,
                          std::string framework) {
  std::vector<ASTNode> portList;

  std::shared_ptr<DeclarationNode> typeDecl =
      ASTQuery::findTypeDeclarationByName(typeName, scope, tree, namespaces,
                                          framework);

  if (typeDecl) {
    if (typeDecl->getEntityType() == "type" ||
        typeDecl->getEntityType() == "platformModule" ||
        typeDecl->getEntityType() == "platformBlock") {
      ValueNode *name = static_cast<ValueNode *>(
          typeDecl->getPropertyValue("typeName").get());
      if (name) {
        assert(name->getNodeType() == AST::String);
        assert(name->getStringValue() == typeName);
        auto newPortList =
            ASTQuery::getPortsForTypeEntity(typeDecl, scope, tree);
        portList.insert(portList.end(), newPortList.begin(), newPortList.end());
      } else {
        LOG_ERROR()
            << "ASTQuery::getModulePortsForType type missing typeName port."
            << std::endl;
      }
    }

    //        QVector<ASTNode > inheritedProperties =
    //        getInheritedPorts(typeDecl, scope, tree); portList <<
    //        inheritedProperties;
  }

  return portList;
}

std::vector<ASTNode>
ASTQuery::getPortsForTypeEntity(std::shared_ptr<DeclarationNode> entityDecl,
                                ScopeStack scope, ASTNode tree) {
  ASTNode portsValue = entityDecl->getPropertyValue("properties");
  std::vector<ASTNode> outList;
  if (portsValue && portsValue->getNodeType() != AST::None) {
    assert(portsValue->getNodeType() == AST::List);
    ListNode *portList = static_cast<ListNode *>(portsValue.get());
    for (const ASTNode &port : portList->getChildren()) {
      outList.push_back(port);
    }
  }

  auto inherited = ASTQuery::getInheritedPorts(entityDecl, scope, tree);
  outList.insert(outList.end(), inherited.begin(), inherited.end());
  return outList;
}

std::vector<ASTNode>
ASTQuery::getValidTypesForPort(std::shared_ptr<DeclarationNode> typeDeclaration,
                               std::string portName, ScopeStack scope,
                               ASTNode tree) {
  std::vector<ASTNode> validTypes;
  auto portList = ASTQuery::getPortsForTypeEntity(typeDeclaration, scope, tree);
  for (const ASTNode &node : portList) {
    DeclarationNode *portNode = static_cast<DeclarationNode *>(node.get());
    ValueNode *name =
        static_cast<ValueNode *>(portNode->getPropertyValue("name").get());
    if (name && name->getNodeType() == AST::String) {
      assert(name->getNodeType() == AST::String);
      if (name->getStringValue() == portName) {
        ASTNode typesPort = portNode->getPropertyValue("allowedTypes");
        if (!typesPort || typesPort->getNodeType() == AST::None) {
          typesPort = portNode->getPropertyValue("types");
        }
        if (typesPort) {
          if (typesPort->getNodeType() == AST::List) {
            for (const ASTNode &type : typesPort->getChildren()) {
              validTypes.push_back(type);
            }
          } else {
            validTypes.push_back(typesPort);
          }
        } else {
          LOG_ERROR() << "Expecting allowedTypes or types for port " << portName
                      << std::endl;
        }
      }
    } else {
      LOG_ERROR() << "Expecting string literal for name" << std::endl;
    }
  }
  return validTypes;
}

std::shared_ptr<DeclarationNode>
ASTQuery::findTypeClass(std::string name, ASTNode tree,
                        std::string currentFramework) {
  if (!tree) {
    return nullptr;
  }
  // Framework type classes overlay the dummy one in the library, which has no
  // framework.
  std::shared_ptr<DeclarationNode> frameworkClass;
  std::shared_ptr<DeclarationNode> libraryClass;
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "typeClass" || decl->getName() != name) {
      continue;
    }
    std::string framework;
    auto frameworkNode = decl->getCompilerProperty("framework");
    if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
      framework =
          std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    }
    if (framework.empty()) {
      if (!libraryClass) {
        libraryClass = decl;
      }
    } else if (framework == currentFramework) {
      return decl;
    } else if (!frameworkClass) {
      frameworkClass = decl;
    }
  }
  return frameworkClass ? frameworkClass : libraryClass;
}

std::vector<std::string>
ASTQuery::getDataTypeClassNames(std::shared_ptr<DeclarationNode> decl,
                                ASTNode tree) {
  std::vector<std::string> names;
  auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, tree);
  if (!typeDecl) {
    return names;
  }
  for (const auto &allowedType :
       ASTQuery::getValidTypesForPort(typeDecl, "type", {}, tree)) {
    if (allowedType->getNodeType() == AST::Entity) {
      auto name = ASTQuery::getNodeName(allowedType);
      if (name != "_NoneLiteral") {
        names.push_back(name);
      }
    }
  }
  return names;
}

bool ASTQuery::isRated(std::shared_ptr<DeclarationNode> decl, ASTNode tree) {
  auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, tree);
  if (!typeDecl) {
    return decl->getPropertyValue("rate") != nullptr;
  }
  if (typeDecl->getName() == "_Rated") {
    return true;
  }
  for (const auto &inherited :
       ASTQuery::getInheritedTypes(typeDecl, {}, tree)) {
    if (inherited->getName() == "_Rated") {
      return true;
    }
  }
  return false;
}

bool ASTQuery::isTypeClassMember(std::shared_ptr<DeclarationNode> typeClass,
                                 std::string typeName) {
  auto typesNode = typeClass->getPropertyValue("types");
  if (!typesNode) {
    return false;
  }
  for (const auto &member : typesNode->getChildren()) {
    if (ASTQuery::getNodeName(member) == typeName) {
      return true;
    }
  }
  return false;
}

std::vector<ASTNode>
ASTQuery::getModuleEntities(std::shared_ptr<DeclarationNode> moduleDecl) {
  std::vector<ASTNode> blocks;
  if (!moduleDecl)
    return blocks;

  ASTNode blocksNode = moduleDecl->getPropertyValue("blocks");
  if (blocksNode) {
    if (blocksNode->getNodeType() == AST::Declaration ||
        blocksNode->getNodeType() == AST::ArrayDeclaration) {
      blocks.push_back(blocksNode);
    } else if (blocksNode->getNodeType() == AST::List) {
      for (const ASTNode &node : blocksNode->getChildren()) {
        if (node->getNodeType() == AST::Declaration ||
            node->getNodeType() == AST::ArrayDeclaration) {
          blocks.push_back(node);
        }
      }
    }
  }

  auto genVarProp = moduleDecl->getCompilerProperty("_generatorVar");
  if (genVarProp && genVarProp->getNodeType() == AST::String) {
    std::string genVarName =
        std::static_pointer_cast<ValueNode>(genVarProp)->getStringValue();
    std::string filename = moduleDecl->getFilename();
    int line = moduleDecl->getLine();
    int column = moduleDecl->getColumn();
    auto genDecl = std::make_shared<DeclarationNode>(
        genVarName, "signal", nullptr, filename.c_str(), line, column);
    moduleDecl->copyLocationTo(genDecl);
    auto entityType = std::make_shared<EntityNode>(DataTypes::Int, filename.c_str(), line, column);
    moduleDecl->copyLocationTo(entityType);
    auto propNode = std::make_shared<PropertyNode>("type", entityType, filename.c_str(), line, column);
    moduleDecl->copyLocationTo(propNode);
    genDecl->addProperty(propNode);
    blocks.push_back(genDecl);
  }

  return blocks;
}

std::vector<ASTNode>
ASTQuery::getStateMachineEntities(std::shared_ptr<DeclarationNode> stateDecl) {
  std::vector<ASTNode> blocks;

  auto addPropBlocks = [&](const std::string &propName) {
    ASTNode propNode = stateDecl->getPropertyValue(propName);
    if (propNode) {
      if (propNode->getNodeType() == AST::Declaration ||
          propNode->getNodeType() == AST::ArrayDeclaration) {
        blocks.push_back(propNode);
      } else if (propNode->getNodeType() == AST::List) {
        for (const ASTNode &node : propNode->getChildren()) {
          if (node->getNodeType() == AST::Declaration ||
              node->getNodeType() == AST::ArrayDeclaration) {
            blocks.push_back(node);
          }
        }
      }
    }
  };

  addPropBlocks("blocks");
  addPropBlocks("states");
  addPropBlocks("transitions");

  return blocks;
}

std::shared_ptr<DeclarationNode>
ASTQuery::getModuleMainOutputPort(std::shared_ptr<DeclarationNode> moduleDecl) {
  ListNode *ports =
      static_cast<ListNode *>(moduleDecl->getPropertyValue("ports").get());
  if (ports) {
    if (ports->getNodeType() == AST::List) {
      for (const ASTNode &port : ports->getChildren()) {
        std::shared_ptr<DeclarationNode> portBlock =
            std::static_pointer_cast<DeclarationNode>(port);
        if (portBlock->getEntityType() == "mainOutputPort") {
          return portBlock;
        }
      }
    } else if (ports->getNodeType() == AST::None) {
      // If port list is None, then ignore
    } else {
      LOG_ERROR() << "ERROR! ports property must be a list or None!"
                  << std::endl;
    }
  }
  return nullptr;
}

std::shared_ptr<DeclarationNode>
ASTQuery::getModuleMainInputPort(std::shared_ptr<DeclarationNode> moduleDecl) {
  ListNode *ports =
      static_cast<ListNode *>(moduleDecl->getPropertyValue("ports").get());
  if (ports) {
    if (ports->getNodeType() == AST::List) {
      for (const ASTNode &port : ports->getChildren()) {
        std::shared_ptr<DeclarationNode> portBlock =
            std::static_pointer_cast<DeclarationNode>(port);
        if (portBlock->getEntityType() == "mainInputPort") {
          return portBlock;
        }
      }
    } else if (ports->getNodeType() == AST::None) {
      // If port list is None, then ignore
    } else {
      LOG_ERROR() << "ERROR! ports property must be a list or None!"
                  << std::endl;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<DeclarationNode>>
strd::ASTQuery::getModulePropertyOutputPorts(
    std::shared_ptr<DeclarationNode> moduleDecl) {
  std::vector<std::shared_ptr<DeclarationNode>> portBlocks;
  ListNode *ports =
      static_cast<ListNode *>(moduleDecl->getPropertyValue("ports").get());
  if (ports) {
    if (ports->getNodeType() == AST::List) {
      for (const ASTNode &port : ports->getChildren()) {
        std::shared_ptr<DeclarationNode> portBlock =
            std::static_pointer_cast<DeclarationNode>(port);
        if (portBlock->getEntityType() == "propertyOutputPort" ||
            portBlock->getEntityType() == "secondaryOutputPort") {
          portBlocks.push_back(portBlock);
        }
      }
    } else if (ports->getNodeType() == AST::None) {
      // If port list is None, then ignore
    } else {
      LOG_ERROR() << "ERROR! ports property must be a list or None!"
                  << std::endl;
    }
  }
  return portBlocks;
}

std::vector<std::shared_ptr<DeclarationNode>>
strd::ASTQuery::getModulePropertyInputPorts(
    std::shared_ptr<DeclarationNode> moduleDecl) {
  std::vector<std::shared_ptr<DeclarationNode>> portBlocks;
  ListNode *ports =
      static_cast<ListNode *>(moduleDecl->getPropertyValue("ports").get());
  if (ports) {
    if (ports->getNodeType() == AST::List) {
      for (const ASTNode &port : ports->getChildren()) {
        std::shared_ptr<DeclarationNode> portBlock =
            std::static_pointer_cast<DeclarationNode>(port);
        if (portBlock->getEntityType() == "propertyInputPort" ||
            portBlock->getEntityType() == "secondaryInputPort") {
          portBlocks.push_back(portBlock);
        }
      }
    } else if (ports->getNodeType() == AST::None) {
      // If port list is None, then ignore
    } else {
      LOG_ERROR() << "ERROR! ports property must be a list or None!"
                  << std::endl;
    }
  }
  return portBlocks;
}

std::shared_ptr<DeclarationNode>
ASTQuery::getModulePort(std::shared_ptr<DeclarationNode> moduleDecl,
                        std::string name) {
  ListNode *ports =
      static_cast<ListNode *>(moduleDecl->getPropertyValue("ports").get());
  if (ports->getNodeType() == AST::List) {
    for (const ASTNode &port : ports->getChildren()) {
      std::shared_ptr<DeclarationNode> portBlock =
          std::static_pointer_cast<DeclarationNode>(port);
      if (portBlock->getName() == name) {
        if (portBlock->getEntityType() == "mainInputPort" ||
            portBlock->getEntityType() == "mainOutputPort" ||
            portBlock->getEntityType() == "propertyInputPort" ||
            portBlock->getEntityType() == "propertyOutputPort") {
          return portBlock;
        } else {
          LOG_ERROR() << "WARNING name found in getPort() but unexpected type"
                      << std::endl;
        }
      }
    }
  } else if (ports->getNodeType() == AST::None) {
    // If port list is None, then ignore
  } else {
    LOG_ERROR() << "ERROR! ports property must be a list or None!" << std::endl;
  }
  return nullptr;
}

std::vector<std::string>
ASTQuery::getModulePortNames(std::shared_ptr<DeclarationNode> blockDeclaration,
                             const ScopeStack &scope, ASTNode tree) {
  std::vector<std::string> portNames;
  if (!blockDeclaration)
    return portNames;

  auto typeDecl = ASTQuery::findTypeDeclaration(blockDeclaration, scope, tree);
  if (!typeDecl) {
    typeDecl = ASTQuery::findTypeDeclarationByName(
        blockDeclaration->getEntityType(), scope, tree);
  }
  bool isCodeGen = isCodeGenerator(typeDecl, scope, tree) ||
                   isCodeGenerator(blockDeclaration, scope, tree);

  if (isCodeGen || blockDeclaration->getEntityType() == "platformModule") {
    auto portsList = std::static_pointer_cast<ListNode>(
        blockDeclaration->getPropertyValue("ports"));
    if (portsList && portsList->getNodeType() == AST::List) {
      for (const ASTNode &portDeclaration : portsList->getChildren()) {
        if (portDeclaration->getNodeType() == AST::Declaration) {
          auto port =
              std::static_pointer_cast<DeclarationNode>(portDeclaration);
          ASTNode nameProperty = port->getPropertyValue("name");
          if (nameProperty) {
            assert(nameProperty->getNodeType() == AST::String);
            if (nameProperty->getNodeType() == AST::String) {
              portNames.push_back(static_cast<ValueNode *>(nameProperty.get())
                                      ->getStringValue());
            }
          }
        }
      }
    }
  }
  return portNames;
}

ASTNode ASTQuery::getMemberFromArrayConst(
    std::shared_ptr<DeclarationNode> blockDecl, int index, ASTNode tree,
    ScopeStack scopeStack, std::vector<LangError> *errors) {
  ASTNode out = nullptr;
  if (blockDecl) {
    auto ports = blockDecl->getProperties();
    for (const std::shared_ptr<PropertyNode> &port : ports) {
      if (port->getName() == "value" || port->getName() == "default") {
        ASTNode value = port->getValue();
        if (value && value->getNodeType() == AST::List) {
          auto listNode = static_cast<ListNode *>(value.get());
          if (index >= 0 && index < (int)listNode->getChildren().size()) {
            return listNode->getChildren()[index];
          } else if (index >= 1 &&
                     index <= (int)listNode->getChildren().size()) {
            return listNode->getChildren()[index - 1];
          }
          return nullptr;
        } else if (value && value->getNodeType() == AST::Array) {
          auto decl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(blockDecl), scopeStack, tree,
              blockDecl->getNamespaceList());
          auto indexList = std::static_pointer_cast<ArrayNode>(value);
          if (indexList->getChildren().size() == 1) {
            auto previousErrors = errors ? errors->size() : 0;
            auto indexVal = ASTFunctions::evaluateConstInteger(
                indexList->getChildren()[0], scopeStack, tree, errors);
            if (!errors || previousErrors == errors->size()) {
              return getMemberFromArrayConst(decl, indexVal, tree, scopeStack,
                                             errors);
            }
          }
        }
      }
    }
  }
  return out;
}

ASTNode ASTQuery::getValueFromConstEntity(DeclarationNode *entityDecl) {
  ASTNode out = nullptr;
  if (entityDecl->getEntityType() == "constant") {
    auto ports = entityDecl->getProperties();
    for (const std::shared_ptr<PropertyNode> &port : ports) {
      if (port->getName() == "value") {
        return port->getValue();
      }
    }
  } else {
    // Should something else be done?
  }
  return out;
}

ASTNode ASTQuery::getMemberFromList(ListNode *node, int index,
                                    std::vector<LangError> *errors) {
  if (index < 1 || index > (int)node->getChildren().size()) {
    LangError error;
    error.type = LangError::ArrayIndexOutOfBounds;
    error.lineNumber = node->getLine();
    error.errorTokens.push_back(std::to_string(index));
    errors->push_back(error);
    return nullptr;
  }
  return node->getChildren()[index - 1];
}

std::shared_ptr<PropertyNode> ASTQuery::findPropertyByName(
    std::vector<std::shared_ptr<PropertyNode>> properties,
    std::string propertyName) {
  for (auto property : properties) {
    if (property->getName() == propertyName) {
      return property;
    }
  }
  return nullptr;
}

bool ASTQuery::isCodeGenerator(std::shared_ptr<DeclarationNode> typeDecl,
                               const ScopeStack &scope, ASTNode tree) {
  if (typeDecl) {
    auto cached = typeDecl->getCompilerProperty("_cache_isCodeGenerator");
    if (cached && cached->getNodeType() == AST::Switch) {
      return std::static_pointer_cast<ValueNode>(cached)->getSwitchValue();
    }
    auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
    for (const auto &inheritsNode : inheritsNodes) {
      if (inheritsNode->getName() == "_CodeGenerator") {
        typeDecl->setCompilerProperty(
            "_cache_isCodeGenerator",
            std::make_shared<ValueNode>(true, __FILE__, __LINE__));
        return true;
      }
    }
    typeDecl->setCompilerProperty(
        "_cache_isCodeGenerator",
        std::make_shared<ValueNode>(false, __FILE__, __LINE__));
  }
  return false;
}

bool ASTQuery::isConstant(std::shared_ptr<DeclarationNode> decl,
                          const ScopeStack &scope, ASTNode tree) {
  if (decl) {
    if (decl->getEntityType() == "constant") {
      return true;
    } else if (decl->getEntityType() != "type") {
      auto typeDecl = findTypeDeclaration(decl, scope, tree);
      if (typeDecl) {
        auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
        for (const auto &inheritsNode : inheritsNodes) {
          if (inheritsNode->getName() == "_Constant" ||
              inheritsNode->getEntityType() == "constant") {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool ASTQuery::isDomainMember(std::shared_ptr<DeclarationNode> typeDecl,
                              const ScopeStack &scope, ASTNode tree) {
  if (typeDecl) {
    auto cached = typeDecl->getCompilerProperty("_cache_isDomainMember");
    if (cached && cached->getNodeType() == AST::Switch) {
      return std::static_pointer_cast<ValueNode>(cached)->getSwitchValue();
    }
    auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
    for (const auto &inheritsNode : inheritsNodes) {
      if (inheritsNode->getName() == "_DomainMember") {
        typeDecl->setCompilerProperty(
            "_cache_isDomainMember",
            std::make_shared<ValueNode>(true, __FILE__, __LINE__));
        return true;
      }
    }
    typeDecl->setCompilerProperty(
        "_cache_isDomainMember",
        std::make_shared<ValueNode>(false, __FILE__, __LINE__));
  }
  return false;
}

bool ASTQuery::isCallable(std::shared_ptr<DeclarationNode> typeDecl,
                          const ScopeStack &scope, ASTNode tree) {
  if (typeDecl) {
    auto cached = typeDecl->getCompilerProperty("_cache_isCallable");
    if (cached && cached->getNodeType() == AST::Switch) {
      return std::static_pointer_cast<ValueNode>(cached)->getSwitchValue();
    }
    auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
    for (const auto &inheritsNode : inheritsNodes) {
      if (inheritsNode->getName() == "_Callable") {
        typeDecl->setCompilerProperty(
            "_cache_isCallable",
            std::make_shared<ValueNode>(true, __FILE__, __LINE__));
        return true;
      }
    }
    typeDecl->setCompilerProperty(
        "_cache_isCallable",
        std::make_shared<ValueNode>(false, __FILE__, __LINE__));
  }
  return false;
}

bool ASTQuery::isStronglyTyped(std::shared_ptr<DeclarationNode> typeDecl,
                               const ScopeStack &scope, ASTNode tree) {
  if (!typeDecl)
    return false;

  std::string objType = typeDecl->getEntityType();
  if (objType == "platformModule" || objType == "external") {
    auto inputs = typeDecl->getPropertyValue("inputs");
    auto outputs = typeDecl->getPropertyValue("outputs");
    return inputs != nullptr && outputs != nullptr;
  }

  // For standard modules, check all entities/blocks.
  auto blocksList = typeDecl->getPropertyValue("blocks");
  if (blocksList && blocksList->getNodeType() == AST::List) {
    for (const auto &blockNode : blocksList->getChildren()) {
      auto blockDecl = std::static_pointer_cast<DeclarationNode>(blockNode);
      if (blockDecl && blockDecl->getEntityType() == "signal") {
        // Ports declare no type. Blocks typed from a port (Port.type) are
        // generic, as their type depends on the connection.
        auto typeNode = blockDecl->getPropertyValue("type");
        if (!typeNode || typeNode->getNodeType() == AST::None ||
            typeNode->getNodeType() == AST::MemberAccess) {
          return false;
        }
      }
    }
  }

  return true;
}

bool ASTQuery::isInputPort(std::shared_ptr<DeclarationNode> entityDecl,
                           std::shared_ptr<DeclarationNode> funcDecl,
                           const ScopeStack &scope, ASTNode tree) {
  if (!entityDecl || !funcDecl)
    return false;
  auto mainInputPort = ASTQuery::getModuleMainInputPort(funcDecl);
  if (mainInputPort) {
    auto mainInputEntityNode = mainInputPort->getPropertyValue("block");
    if (mainInputEntityNode &&
        mainInputEntityNode->getNodeType() == AST::Entity) {
      if (std::static_pointer_cast<EntityNode>(mainInputEntityNode)
              ->getName() == entityDecl->getName()) {
        return true;
      }
    }
  }

  auto propertyInputPorts = ASTQuery::getModulePropertyInputPorts(funcDecl);
  for (const auto &port : propertyInputPorts) {
    auto propertyInputEntityNode = port->getPropertyValue("block");
    if (propertyInputEntityNode &&
        propertyInputEntityNode->getNodeType() == AST::Entity) {
      if (std::static_pointer_cast<EntityNode>(propertyInputEntityNode)
              ->getName() == entityDecl->getName()) {
        return true;
      }
    }
  }
  return false;
}

bool ASTQuery::isOutputPort(std::shared_ptr<DeclarationNode> entityDecl,
                            std::shared_ptr<DeclarationNode> funcDecl,
                            const ScopeStack &scope, ASTNode tree) {
  if (!entityDecl || !funcDecl)
    return false;
  auto mainOutputPort = ASTQuery::getModuleMainOutputPort(funcDecl);
  if (mainOutputPort) {
    auto mainOutputEntityNode = mainOutputPort->getPropertyValue("block");
    if (mainOutputEntityNode &&
        mainOutputEntityNode->getNodeType() == AST::Entity) {
      if (std::static_pointer_cast<EntityNode>(mainOutputEntityNode)
              ->getName() == entityDecl->getName()) {
        return true;
      }
    }
  }

  auto propertyOutputPorts = ASTQuery::getModulePropertyOutputPorts(funcDecl);
  for (const auto &port : propertyOutputPorts) {
    auto propertyOutputEntityNode = port->getPropertyValue("block");
    if (propertyOutputEntityNode &&
        propertyOutputEntityNode->getNodeType() == AST::Entity) {
      if (std::static_pointer_cast<EntityNode>(propertyOutputEntityNode)
              ->getName() == entityDecl->getName()) {
        return true;
      }
    }
  }
  return false;
}

bool ASTQuery::isPortEntity(std::shared_ptr<DeclarationNode> entityDecl,
                            std::shared_ptr<DeclarationNode> funcDecl,
                            const ScopeStack &scope, ASTNode tree) {
  return isInputPort(entityDecl, funcDecl, scope, tree) ||
         isOutputPort(entityDecl, funcDecl, scope, tree);
}

bool ASTQuery::isStatelessGenerator(std::shared_ptr<DeclarationNode> typeDecl,
                                    const ScopeStack &scope, ASTNode tree) {
  if (typeDecl && typeDecl->getEntityType() == "type") {
    if (typeDecl->getName() == "_Loop" || typeDecl->getName() == "_Reaction") {
      return isCodeGenerator(typeDecl, scope, tree);
    }
    auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
    for (const auto &inheritsNode : inheritsNodes) {
      if (inheritsNode->getName() == "_Reaction" ||
          inheritsNode->getName() == "_Loop") {
        return isCodeGenerator(typeDecl, scope, tree);
      }
    }
  }
  return false;
}

bool ASTQuery::isStateNode(std::shared_ptr<DeclarationNode> typeDecl,
                           const ScopeStack &scope, ASTNode tree) {
  if (!typeDecl)
    return false;

  if (typeDecl->getEntityType() == "state" ||
      typeDecl->getName() == "_StateType") {
    return true;
  }

  std::shared_ptr<DeclarationNode> schemaDecl = typeDecl;
  if (typeDecl->getEntityType() != "type") {
    schemaDecl = ASTQuery::findTypeDeclaration(typeDecl, scope, tree);
  }

  if (schemaDecl) {
    if (schemaDecl->getName() == "_StateType")
      return true;
    auto inherited = getInheritedTypes(schemaDecl, scope, tree);
    for (const auto &parent : inherited) {
      if (parent->getName() == "_StateType") {
        return true;
      }
    }
  }
  return false;
}

bool ASTQuery::isTransitionNode(std::shared_ptr<DeclarationNode> typeDecl,
                                const ScopeStack &scope, ASTNode tree) {
  if (!typeDecl)
    return false;

  if (typeDecl->getEntityType() == "transition" ||
      typeDecl->getName() == "_TransitionType") {
    return true;
  }

  std::shared_ptr<DeclarationNode> schemaDecl = typeDecl;
  if (typeDecl->getEntityType() != "type") {
    schemaDecl = ASTQuery::findTypeDeclaration(typeDecl, scope, tree);
  }

  if (schemaDecl) {
    if (schemaDecl->getName() == "_TransitionType")
      return true;
    auto inherited = getInheritedTypes(schemaDecl, scope, tree);
    for (const auto &parent : inherited) {
      if (parent->getName() == "_TransitionType") {
        return true;
      }
    }
  }
  return false;
}

bool ASTQuery::isDomainDefinition(std::shared_ptr<DeclarationNode> typeDecl,
                                  const ScopeStack &scope, ASTNode tree) {
  if (typeDecl && typeDecl->getEntityType() == "type") {
    if (typeDecl->getName() == "_DomainDefinition") {
      return true;
    }
    auto inheritsNodes = ASTQuery::getInheritedTypes(typeDecl, scope, tree);
    for (const auto &inheritsNode : inheritsNodes) {
      if (inheritsNode->getName() == "_DomainDefinition") {
        return true;
      }
    }
  }
  return false;
}

bool ASTQuery::isModuleNode(ASTNode node, const ScopeStack &scope,
                            ASTNode tree) {
  if (!node) {
    return false;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    return decl->getEntityType() == "module";
  }
  if (tree) {
    auto decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                                scope, tree);
    if (decl) {
      return decl->getEntityType() == "module";
    }
  }
  return false;
}

bool ASTQuery::namespaceMatch(std::vector<std::string> scopeList,
                              std::shared_ptr<DeclarationNode> decl,
                              std::string currentFramework) {
  if (scopeList.size() == 1 && scopeList[0].size() == 0) {
    scopeList.clear();
  }
  auto declScopesNode = decl->getCompilerProperty("namespaceTree");

  auto frameworkNode = decl->getCompilerProperty("framework");
  std::string framework;
  if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
    framework =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  std::vector<std::string> namespaceTree;
  // Add import namespaces
  if (declScopesNode) {
    for (const auto &node : declScopesNode->getChildren()) {
      assert(node->getNodeType() == AST::String);

      namespaceTree.push_back(
          std::static_pointer_cast<ValueNode>(node)->getStringValue());
    }
  }
  // Remove namespace if calling from within the same framework
  if (framework == currentFramework && namespaceTree.size() > 0 &&
      namespaceTree.at(0) == currentFramework) {
    //    if (scopeList.size() > 0 && scopeList.at(0) != currentFramework) {
    namespaceTree.erase(namespaceTree.begin());
    //    }
  }
  if (framework.size() > 0 && framework != currentFramework) {
    if (namespaceTree.size() == 0 || framework != namespaceTree.at(0)) {
      namespaceTree.insert(namespaceTree.begin(), framework);
    }
  } else {
    //      if (scopeList.size() > 0 && scopeList[0] == framework) {
    //        scopeList.erase(scopeList.begin());
    //      }
  }
  if (namespaceTree.size() != scopeList.size()) {
    return false;
  }

  for (size_t i = 0; i < namespaceTree.size(); i++) {
    if (namespaceTree[i] != scopeList[i]) {
      return false;
    }
  }

  return true;
}

std::shared_ptr<DeclarationNode> ASTQuery::findDeclarationByName(
    std::string objectName, const ScopeStack &scopeStack, ASTNode tree,
    std::vector<std::string> namespaces, std::string framework) {
  std::vector<std::string> scopesList = namespaces;
  std::string baseName = objectName;
  size_t pos = 0;
  while ((pos = baseName.find("::")) != std::string::npos) {
    scopesList.push_back(baseName.substr(0, pos));
    baseName = baseName.substr(pos + 2);
  }
  objectName = baseName;

  auto searchScopesAndTree = [&](bool matchAnyAtIfUnspecified)
      -> std::shared_ptr<DeclarationNode> {
    for (auto subScopeIt = scopeStack.rbegin(); subScopeIt != scopeStack.rend();
         subScopeIt++) {
      auto subScope = *subScopeIt;
      for (const ASTNode &scopeNode : subScope.second) {
        if (scopeNode->getNodeType() == AST::ArrayDeclaration ||
            scopeNode->getNodeType() == AST::Declaration) {
          std::shared_ptr<DeclarationNode> decl =
              std::static_pointer_cast<DeclarationNode>(scopeNode);
          if (declMatchesNameAndAt(objectName, decl, matchAnyAtIfUnspecified)) {
            if (ASTQuery::namespaceMatch(scopesList, decl, framework)) {
              return decl;
            }
          }
        }
      }
    }
    if (tree) {
      for (const ASTNode &node : tree->getChildren()) {
        if (node->getNodeType() == AST::ArrayDeclaration ||
            node->getNodeType() == AST::Declaration) {
          std::shared_ptr<DeclarationNode> decl =
              std::static_pointer_cast<DeclarationNode>(node);
          if (declMatchesNameAndAt(objectName, decl, matchAnyAtIfUnspecified)) {
            if (ASTQuery::namespaceMatch(scopesList, decl, framework)) {
              if (framework.size() > 0) {
                auto platformNode = decl->getCompilerProperty("framework");
                if (platformNode && platformNode->getNodeType() == AST::String) {
                  auto platformString =
                      std::static_pointer_cast<ValueNode>(platformNode)
                          ->getStringValue();
                  if (platformString == framework) {
                    return decl;
                  }
                }
              } else {
                return decl;
              }
            }
          }
        }
      }
    }
    return nullptr;
  };

  auto decl = searchScopesAndTree(false);
  if (!decl && objectName.find('@') == std::string::npos) {
    decl = searchScopesAndTree(true);
  }
  if (!decl && framework.size() > 0) {
    // If not found try to find in root namespace
    return findDeclarationByName(objectName, scopeStack, tree, namespaces);
  }
  return decl;
}

// ---------------------------------------------------------------------------
// DataTypes

const std::string DataTypes::Real = "Real";
const std::string DataTypes::Int = "Int";
const std::string DataTypes::UInt = "UInt";
const std::string DataTypes::Size = "Size";
const std::string DataTypes::Switch = "Switch";
const std::string DataTypes::String = "String";

bool DataTypes::isDataTypeName(const std::string &name) {
  return !name.empty() && name[0] != '_';
}

DataTypes::Info DataTypes::builtin(const std::string &name) {
  struct Builtin {
    const char *canonical;
    Kind kind;
    int bits;
    bool isSigned;
  };
  static const std::map<std::string, Builtin> builtins = {
      {"Int8", {"Int8", Kind::Integer, 8, true}},
      {"Int16", {"Int16", Kind::Integer, 16, true}},
      {"Int32", {"Int32", Kind::Integer, 32, true}},
      {"Int64", {"Int64", Kind::Integer, 64, true}},
      {"UInt8", {"UInt8", Kind::Integer, 8, false}},
      {"UInt16", {"UInt16", Kind::Integer, 16, false}},
      {"UInt32", {"UInt32", Kind::Integer, 32, false}},
      {"UInt64", {"UInt64", Kind::Integer, 64, false}},
      {"Float32", {"Float32", Kind::Float, 32, true}},
      {"Float64", {"Float64", Kind::Float, 64, true}},
      {"Switch", {"Switch", Kind::Switch, 1, false}},
      {"String", {"String", Kind::String, 0, false}},
      {"Int", {"Int32", Kind::Integer, 32, true}},
      {"UInt", {"UInt32", Kind::Integer, 32, false}},
      {"Real", {"Float64", Kind::Float, 64, true}},
      {"Size", {"UInt64", Kind::Integer, 64, false}},
  };
  Info info;
  info.name = name;
  auto it = builtins.find(name);
  if (it != builtins.end()) {
    info.canonical = it->second.canonical;
    info.kind = it->second.kind;
    info.bits = it->second.bits;
    info.isSigned = it->second.isSigned;
  }
  return info;
}

std::shared_ptr<DeclarationNode>
DataTypes::findDeclaration(const std::string &name, ASTNode tree,
                           const std::string &currentFramework) {
  if (!tree) {
    return nullptr;
  }
  // Framework data types overlay the dummy ones in the library, which have no
  // framework.
  std::shared_ptr<DeclarationNode> frameworkType;
  std::shared_ptr<DeclarationNode> libraryType;
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if ((decl->getEntityType() != "frameworkDataType" &&
         decl->getEntityType() != "alias") ||
        decl->getName() != name) {
      continue;
    }
    std::string framework;
    auto frameworkNode = decl->getCompilerProperty("framework");
    if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
      framework =
          std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    }
    if (framework.empty()) {
      if (!libraryType) {
        libraryType = decl;
      }
    } else if (framework == currentFramework) {
      return decl;
    } else if (!frameworkType) {
      frameworkType = decl;
    }
  }
  return frameworkType ? frameworkType : libraryType;
}

DataTypes::Info DataTypes::resolve(const std::string &name, ASTNode tree,
                                   const std::string &currentFramework) {
  Info info;
  info.name = name;
  std::string current = name;
  std::set<std::string> visited;
  while (true) {
    auto decl = findDeclaration(current, tree, currentFramework);
    if (!decl) {
      auto fallback = builtin(current);
      fallback.name = name;
      return fallback;
    }
    if (!visited.insert(current).second) {
      return info; // Alias cycle. ASTValidation reports it.
    }
    if (decl->getEntityType() == "alias") {
      auto entity = decl->getPropertyValue("entity");
      if (!entity || entity->getNodeType() != AST::Entity) {
        return info; // Not an alias of a data type
      }
      current = ASTQuery::getNodeName(entity);
      continue;
    }
    auto kindNode = decl->getPropertyValue("kind");
    if (!kindNode || kindNode->getNodeType() != AST::String) {
      // Library dummies have no kind. Use the builtin description.
      auto fallback = builtin(current);
      fallback.name = name;
      return fallback;
    }
    auto kind = std::static_pointer_cast<ValueNode>(kindNode)->getStringValue();
    info.canonical = current;
    if (kind == "integer") {
      info.kind = Kind::Integer;
    } else if (kind == "float") {
      info.kind = Kind::Float;
    } else if (kind == "switch") {
      info.kind = Kind::Switch;
      info.bits = 1;
    } else if (kind == "string") {
      info.kind = Kind::String;
    }
    auto bitsNode = decl->getPropertyValue("bits");
    if (bitsNode && bitsNode->getNodeType() == AST::Int) {
      info.bits = static_cast<int>(
          std::static_pointer_cast<ValueNode>(bitsNode)->getIntValue());
    }
    // The name sets the sign: UInt8..UInt64 are unsigned
    if (info.kind == Kind::Integer) {
      info.isSigned = current.rfind("UInt", 0) != 0;
    }
    if (info.kind == Kind::Switch || info.kind == Kind::String) {
      info.isSigned = false;
    }
    return info;
  }
}

std::string DataTypes::fixedTypeName(Kind kind, int bits, bool isSigned) {
  if (kind == Kind::Float) {
    return "Float" + std::to_string(bits);
  }
  if (kind == Kind::Integer) {
    return (isSigned ? "Int" : "UInt") + std::to_string(bits);
  }
  return std::string();
}

std::string DataTypes::promote(const std::string &a, const std::string &b,
                               ASTNode tree) {
  if (a == b) {
    return a;
  }
  auto infoA = resolve(a, tree);
  auto infoB = resolve(b, tree);
  if (!infoA.isNumeric() || !infoB.isNumeric()) {
    return std::string();
  }
  if (infoA.canonical == infoB.canonical) {
    return a;
  }
  if (infoA.isFloat() != infoB.isFloat()) {
    return infoA.isFloat() ? a : b;
  }
  if (infoA.isFloat() || infoA.isSigned == infoB.isSigned) {
    if (infoA.bits == infoB.bits) {
      return a;
    }
    return infoA.bits > infoB.bits ? a : b;
  }
  // Signed and unsigned: a signed type that can hold the unsigned values
  const auto &signedInfo = infoA.isSigned ? infoA : infoB;
  const auto &unsignedInfo = infoA.isSigned ? infoB : infoA;
  if (signedInfo.bits > unsignedInfo.bits) {
    return infoA.isSigned ? a : b;
  }
  int bits = std::min(64, std::max(2 * unsignedInfo.bits, signedInfo.bits));
  return fixedTypeName(Kind::Integer, bits, true);
}

namespace {
// Integers up to this many bits convert to the float type exactly
int floatMantissaBits(int floatBits) { return floatBits == 32 ? 24 : 53; }
} // namespace

std::string DataTypes::conversionRisk(const std::string &from,
                                      const std::string &to, ASTNode tree) {
  auto infoFrom = resolve(from, tree);
  auto infoTo = resolve(to, tree);
  if (!infoFrom.isNumeric() || !infoTo.isNumeric() ||
      infoFrom.canonical == infoTo.canonical) {
    return std::string();
  }
  if (infoFrom.isFloat() && infoTo.isInteger()) {
    return "truncates the fractional part, and values outside the range of " +
           to + " overflow";
  }
  if (infoFrom.isFloat() && infoTo.isFloat()) {
    if (infoTo.bits < infoFrom.bits) {
      return "loses precision and range";
    }
    return std::string();
  }
  if (infoFrom.isInteger() && infoTo.isFloat()) {
    int valueBits = infoFrom.bits - (infoFrom.isSigned ? 1 : 0);
    if (valueBits > floatMantissaBits(infoTo.bits)) {
      return "rounds integers larger than 2^" +
             std::to_string(floatMantissaBits(infoTo.bits));
    }
    return std::string();
  }
  // Integer to integer
  if (infoFrom.isSigned && !infoTo.isSigned) {
    if (infoTo.bits < infoFrom.bits) {
      return "wraps negative values and values above the maximum of " + to;
    }
    return "wraps negative values";
  }
  if (!infoFrom.isSigned && infoTo.isSigned && infoTo.bits <= infoFrom.bits) {
    return "wraps values above the maximum of " + to + " to negative values";
  }
  if (infoTo.bits < infoFrom.bits) {
    return "wraps values outside the range of " + to;
  }
  return std::string();
}

std::string DataTypes::promotionRisk(const std::string &a,
                                     const std::string &b, ASTNode tree) {
  auto promoted = promote(a, b, tree);
  if (promoted.empty()) {
    return std::string();
  }
  for (const auto &operand : {a, b}) {
    auto risk = conversionRisk(operand, promoted, tree);
    if (!risk.empty()) {
      return "mixes " + a + " and " + b + ". It " + risk + " of the " +
             operand + " operand";
    }
  }
  return std::string();
}

bool DataTypes::implicitlyConvertible(const std::string &from,
                                      const std::string &to, ASTNode tree) {
  if (from == to) {
    return true;
  }
  auto infoFrom = resolve(from, tree);
  auto infoTo = resolve(to, tree);
  if (!infoFrom.valid() || !infoTo.valid()) {
    return false;
  }
  if (infoFrom.canonical == infoTo.canonical) {
    return true;
  }
  if (!infoFrom.isNumeric() || !infoTo.isNumeric()) {
    return false;
  }
  if (infoTo.isFloat()) {
    return infoFrom.isInteger() || infoTo.bits >= infoFrom.bits;
  }
  if (infoFrom.isFloat()) {
    return false;
  }
  if (infoFrom.isSigned && !infoTo.isSigned) {
    return false;
  }
  if (!infoFrom.isSigned && infoTo.isSigned) {
    return infoTo.bits > infoFrom.bits;
  }
  return infoTo.bits >= infoFrom.bits;
}
