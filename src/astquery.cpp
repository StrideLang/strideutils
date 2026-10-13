#include "stride/utils/astquery.h"
#include "stride/parser/strideparser.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace strd;

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
        std::string name = decl->getName();
        if (name == objectName) {
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
        if (decl->getName() == objectName) {
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
    return name->getName();
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    return arrayNode->getName();
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    return func->getName();
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
    return decl->getName();
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
    std::shared_ptr<DeclarationNode> decl =
        ASTQuery::findDeclarationByName(entityNode->getName(), scopeStack, tree);
    if (!decl) {
      size = -1; // Entity not declared
    } else if (decl->getNodeType() == AST::ArrayDeclaration) {
      std::vector<LangError> errors;
      size = getEntityDeclaredSize(decl, {}, tree, &errors);
    } else if (decl->getNodeType() == AST::Declaration) {
      size = 1;
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
    case AST::Expression:
      type = ASTQuery::getNodeSize(expr, scope, tree);
      if (type == "_IntLiteral" || type == "_IntType") {
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

int ASTQuery::getEntityDeclaredSize(std::shared_ptr<DeclarationNode> entityDecl,
                                    ScopeStack scope, ASTNode tree,
                                    std::vector<LangError> *errors) {
  if (entityDecl->getNodeType() == AST::Declaration) {
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
      __FILE__, __LINE__);
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
      assert(0 == 1); // Disallowed
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
    auto inheritedFromType = getPortsForTypeEntity(typeDeclaration, scope, tree);
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
        ListNode *typesPort =
            static_cast<ListNode *>(portNode->getPropertyValue("types").get());
        if (typesPort && typesPort->getNodeType() == AST::List) {
          for (const ASTNode &type : typesPort->getChildren()) {
            validTypes.push_back(type);
          }
        } else {
          LOG_ERROR() << "Expecting list for types port" << std::endl;
        }
      }
    } else {
      LOG_ERROR() << "Expecting string literal for name" << std::endl;
    }
  }
  return validTypes;
}

std::vector<ASTNode>
ASTQuery::getModuleEntities(std::shared_ptr<DeclarationNode> moduleDecl) {
  std::vector<ASTNode> blocks;

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
  if (ports && ports->getNodeType() == AST::List) {
    for (const ASTNode &port : ports->getChildren()) {
      std::shared_ptr<DeclarationNode> portBlock =
          std::static_pointer_cast<DeclarationNode>(port);
      if (portBlock->getEntityType() == "mainOutputPort") {
        return portBlock;
      }
    }
  } else if (ports && ports->getNodeType() == AST::None) {
    // If port list is None, then ignore
  } else {
    LOG_ERROR() << "ERROR! ports property must be a list or None!" << std::endl;
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

std::vector<std::string> ASTQuery::getModulePortNames(
    std::shared_ptr<DeclarationNode> blockDeclaration) {
  std::vector<std::string> portNames;
  if (blockDeclaration->getEntityType() == "module" ||
      blockDeclaration->getEntityType() == "platformModule") {
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
              portNames.push_back(
                  static_cast<ValueNode *>(nameProperty.get())->toString());
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
  if (ASTQuery::isConstant(blockDecl, scopeStack, tree)) {
    auto ports = blockDecl->getProperties();
    for (const std::shared_ptr<PropertyNode> &port : ports) {
      if (port->getName() == "value") {
        ASTNode value = port->getValue();
        if (value->getNodeType() == AST::List) {
          return ASTQuery::getMemberFromList(
              static_cast<ListNode *>(value.get()), index, errors);
        } else if (value->getNodeType() == AST::Array) {
          auto decl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(blockDecl), scopeStack, tree,
              blockDecl->getNamespaceList());
          auto indexList = std::static_pointer_cast<ArrayNode>(value);
          if (indexList->getChildren().size() == 1) {
            auto previousErrors = errors->size();
            auto indexVal = ASTFunctions::evaluateConstInteger(
                indexList->getChildren()[0], scopeStack, tree, errors);
            if (previousErrors == errors->size()) {
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
        auto typeNode = blockDecl->getPropertyValue("type");
        if (!typeNode) {
          return false;
        }
      }
    }
  }

  // Also check ports, which might lack types in generic modules
  auto portsList = typeDecl->getPropertyValue("ports");
  if (portsList && portsList->getNodeType() == AST::List) {
    for (const auto &portNode : portsList->getChildren()) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(portNode);
      if (portDecl) {
        auto typeNode = portDecl->getPropertyValue("type");
        if (!typeNode) {
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
      if (std::static_pointer_cast<EntityNode>(mainInputEntityNode)->getName() ==
          entityDecl->getName()) {
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
  for (auto subScopeIt = scopeStack.rbegin(); subScopeIt != scopeStack.rend();
       subScopeIt++) {
    auto subScope = *subScopeIt;
    for (const ASTNode &scopeNode : subScope.second) {
      if (scopeNode->getNodeType() == AST::ArrayDeclaration ||
          scopeNode->getNodeType() == AST::Declaration) {
        std::shared_ptr<DeclarationNode> decl =
            std::static_pointer_cast<DeclarationNode>(scopeNode);
        std::string name = decl->getName();
        if (name == objectName) {
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
        if (decl->getName() == objectName) {
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
  if (framework.size() > 0) {
    // If not found try to find in root namespace
    return findDeclarationByName(objectName, scopeStack, tree, namespaces);
  } else {
    return nullptr;
  }
}
