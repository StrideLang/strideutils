#include "stride/utils/astvalidation.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include "stride/parser/arraynode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/strideparser.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <unordered_set>

using namespace strd;

ASTValidation::ASTValidation() {}

std::vector<LangError> ASTValidation::validate(ASTNode tree) {
  std::vector<LangError> errors;
  std::set<const void *> processedNodes;
  validateTypes(tree, errors, {}, tree, {}, "", &processedNodes);
  validateShadowing(tree, errors);
  return errors;
}

namespace {

bool isDeclarationNode(const ASTNode &node) {
  return node && (node->getNodeType() == AST::Declaration ||
                  node->getNodeType() == AST::ArrayDeclaration);
}

// Declaration named name in any enclosing scope frame.
std::shared_ptr<DeclarationNode> findEnclosingDeclaration(
    const std::string &name, const ScopeStack &scope, const ASTNode &self) {
  for (const auto &frame : scope) {
    for (const auto &node : frame.second) {
      if (node == self || !isDeclarationNode(node)) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getName() == name) {
        return decl;
      }
    }
  }
  return nullptr;
}

void validateShadowingInScope(ScopeStack &scope, const ASTNode &tree,
                              std::vector<LangError> &errors) {
  // Copy: recursion pushes frames, which may reallocate the stack.
  const auto nodes = scope.back().second;
  for (const auto &node : nodes) {
    if (!isDeclarationNode(node)) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    auto blocks = decl->getPropertyValue("blocks");
    if (!blocks || blocks->getNodeType() != AST::List) {
      continue;
    }
    // Inheritance is declared on the type (e.g. _Reaction), not the instance.
    auto typeDecl = ASTQuery::findTypeDeclaration(decl, scope, tree);
    if (typeDecl && ASTQuery::isCodeGenerator(typeDecl, scope, tree) &&
        ASTQuery::isDomainMember(typeDecl, scope, tree)) {
      for (const auto &block : blocks->getChildren()) {
        if (!isDeclarationNode(block)) {
          continue;
        }
        auto blockDecl = std::static_pointer_cast<DeclarationNode>(block);
        auto shadowed =
            findEnclosingDeclaration(blockDecl->getName(), scope, block);
        if (shadowed) {
          LangError error;
          error.type = LangError::ShadowedSymbol;
          error.filename = blockDecl->getFilename();
          error.lineNumber = blockDecl->getLine();
          error.errorTokens = {blockDecl->getName(), decl->getName(),
                               shadowed->getFilename(),
                               std::to_string(shadowed->getLine())};
          errors.push_back(error);
        }
      }
    }
    // Nested code generators see these blocks as their parent scope.
    scope.push_back({node, blocks->getChildren()});
    validateShadowingInScope(scope, tree, errors);
    scope.pop_back();
  }
}

} // namespace

void ASTValidation::validateShadowing(ASTNode tree,
                                      std::vector<LangError> &errors) {
  if (!tree) {
    return;
  }
  ScopeStack scope{{nullptr, tree->getChildren()}};
  validateShadowingInScope(scope, tree, errors);
}

void ASTValidation::validateTypes(ASTNode node, std::vector<LangError> &errors,
                                  ScopeStack scopeStack, ASTNode tree,
                                  std::vector<std::string> parentNamespace,
                                  std::string currentFramework,
                                  std::set<const void *> *processedNodes) {
  if (!node) {
    return;
  }
  std::set<const void *> localProcessed;
  if (!processedNodes) {
    processedNodes = &localProcessed;
  }
  if (processedNodes->count(node.get()) > 0) {
    return;
  }
  processedNodes->insert(node.get());

  if (node->getNodeType() == AST::ArrayDeclaration ||
      node->getNodeType() == AST::Declaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(node);
    validateTypesForDeclaration(decl, scopeStack, errors, tree,
                                currentFramework, processedNodes);

    return; // Children have been processed with corect scopes.
  } else if (node->getNodeType() == AST::Stream) {
    // Stream members will be processed with children below
  } else if (node->getNodeType() == AST::List) {
    // Children are checked automatically below
  } else if (node->getNodeType() == AST::Entity) {
    EntityNode *entity = static_cast<EntityNode *>(node.get());
    std::string entityName = entity->getName();
    std::vector<std::string> namespaces = entity->getNamespaceList();
    namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                      parentNamespace.end());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(entityName, scopeStack, tree,
                                        namespaces, currentFramework);
    if (!declaration) {
      declaration = ASTQuery::findTypeDeclarationByName(
          entityName, scopeStack, tree, namespaces, currentFramework);
    }
    if (!declaration) {
      LangError error;
      error.type = LangError::UndeclaredSymbol;
      error.lineNumber = node->getLine();
      error.filename = node->getFilename();
      //            error.errorTokens.push_back(entity->getName());
      //            error.errorTokens.push_back(entity->getNamespace());
      std::string entityName = "";
      if (entity->getScopeLevels()) {
        for (unsigned int i = 0; i < entity->getScopeLevels(); i++) {
          entityName += entity->getScopeAt(i);
          entityName += "::";
        }
      }
      entityName += entity->getName();
      error.errorTokens.push_back(entityName);
      errors.push_back(error);
    }
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::vector<std::string> namespaces = arrayNode->getNamespaceList();
    namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                      parentNamespace.end());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scopeStack, tree,
                                        namespaces);
    if (!declaration) {
      LangError error;
      error.type = LangError::UndeclaredSymbol;
      error.lineNumber = node->getLine();
      error.filename = node->getFilename();
      //            error.errorTokens.push_back(arrayNode->getName());
      //            error.errorTokens.push_back(arrayNode->getNamespace());
      std::string arrayName = "";
      if (arrayNode->getScopeLevels()) {
        for (unsigned int i = 0; i < arrayNode->getScopeLevels(); i++) {
          arrayName += arrayNode->getScopeAt(i);
          arrayName += "::";
        }
      }
      arrayName += arrayNode->getName();
      error.errorTokens.push_back(arrayName);
      errors.push_back(error);
    }
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(func->getName(), scopeStack, tree,
                                        func->getNamespaceList());
    if (!declaration) {
      LangError error;
      error.type = LangError::UndeclaredSymbol;
      error.lineNumber = node->getLine();
      error.filename = node->getFilename();
      //            error.errorTokens.push_back(func->getName());
      //            error.errorTokens.push_back(func->getNamespace());
      std::string funcName = "";
      if (func->getScopeLevels()) {
        for (unsigned int i = 0; i < func->getScopeLevels(); i++) {
          funcName += func->getScopeAt(i);
          funcName += "::";
        }
      }
      funcName += func->getName();
      error.errorTokens.push_back(funcName);
      errors.push_back(error);
    } else {
      for (const auto &property : func->getProperties()) {
        std::string propertyName = property->getName();
        std::vector<std::string> validPorts =
            ASTQuery::getModulePortNames(declaration, scopeStack, tree);
        // TODO "domain" port is being allowed forcefully. Should this be
        // specified in a different way?
        if (/*propertyName.at(0) != '_' && propertyName != "domain" &&*/
            std::find(validPorts.begin(), validPorts.end(), propertyName) ==
            validPorts.end()) {
          LangError error;
          error.type = LangError::InvalidPort;
          error.lineNumber = func->getLine();
          error.errorTokens.push_back(func->getName());
          error.errorTokens.push_back(propertyName);
          error.filename = func->getFilename();
          errors.push_back(error);
        }
      }
    }
  }

  for (const ASTNode &childNode : node->getChildren()) {
    //    auto frameworkNode = childNode->getCompilerProperty("framework");
    //    std::string frameworkName;
    //    if (frameworkNode) {
    //      frameworkName =
    //          static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    //    }
    validateTypes(childNode, errors, scopeStack, tree, {}, currentFramework,
                  processedNodes);
  }
}

bool ASTValidation::isValidStringProperty(std::shared_ptr<DeclarationNode> decl,
                                          std::string propertyName,
                                          bool optional, bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::String) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidSwitchProperty(std::shared_ptr<DeclarationNode> decl,
                                          std::string propertyName,
                                          bool optional, bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::Switch) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidIntProperty(std::shared_ptr<DeclarationNode> decl,
                                       std::string propertyName, bool optional,
                                       bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::Int) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidRealProperty(std::shared_ptr<DeclarationNode> decl,
                                        std::string propertyName, bool optional,
                                        bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::Real) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidNumberProperty(std::shared_ptr<DeclarationNode> decl,
                                          std::string propertyName,
                                          bool optional, bool verbose) {
  auto isNumber = isValidIntProperty(decl, propertyName, optional, false) ||
                  isValidRealProperty(decl, propertyName, optional, false);
  if (verbose && !isNumber) {
    LOG_ERROR() << "Failed " << propertyName << std::endl;
  }
  return isNumber;
}

bool ASTValidation::isValidEntityProperty(std::shared_ptr<DeclarationNode> decl,
                                          std::string propertyName,
                                          bool optional, bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::Entity) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidNoneProperty(std::shared_ptr<DeclarationNode> decl,
                                        std::string propertyName, bool optional,
                                        bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::None) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  return true;
}

bool ASTValidation::isValidDeclarationProperty(
    std::shared_ptr<DeclarationNode> decl, std::string propertyName,
    bool optional, bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::Declaration) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  } /*else {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (objectTypes.size() == 0) {
      return true;
    }
    if (std::find(objectTypes.begin(), objectTypes.end(), decl->getEntityType())
        == objectTypes.end()) {
      if (verbose) {
        LOG_ERROR() << "Failed " << propertyName
  << std::endl;
      }
    }
    return false;
  }*/
  return true;
}

bool ASTValidation::isValidOr(
    std::function<bool(std::shared_ptr<DeclarationNode>, std::string, bool,
                       bool)>
        func1,
    std::function<bool(std::shared_ptr<DeclarationNode>, std::string, bool,
                       bool)>
        func2,
    std::shared_ptr<DeclarationNode> decl, std::string propertyName,
    bool optional, bool verbose) {
  bool valid1 = func1(decl, propertyName, true, false);
  bool valid2 = func2(decl, propertyName, true, false);
  return valid1 || valid2;
}

bool ASTValidation::isValidListProperty(
    std::shared_ptr<DeclarationNode> decl, std::string propertyName,
    std::function<bool(ASTNode)> validateElement, bool optional, bool verbose) {
  auto node = decl->getPropertyValue(propertyName);
  if (!node && optional) {
    return true;
  } else if (!node || node->getNodeType() != AST::List) {
    if (verbose) {
      LOG_ERROR() << "Failed " << propertyName << std::endl;
    }
    return false;
  }
  if (!optional) {
    for (const auto &busName : node->getChildren()) {
      if (!validateElement(busName)) {
        if (verbose) {
          LOG_ERROR() << "Failed " << propertyName << std::endl;
        }
        return false;
      }
    }
  }
  return true;
}

void ASTValidation::validateTypesForDeclaration(
    std::shared_ptr<DeclarationNode> decl, ScopeStack scopeStack,
    std::vector<LangError> &errors, ASTNode tree, std::string currentFramework,
    std::set<const void *> *processedNodes) {
  auto blockType = decl->getEntityType();

  auto frameworkNode = decl->getCompilerProperty("framework");
  std::string frameworkName;
  if (frameworkNode) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  std::shared_ptr<DeclarationNode> declaration =
      ASTQuery::findTypeDeclaration(decl, scopeStack, tree, frameworkName);
  if (!declaration) { // Check if node type exists
    LangError error;  // Not a valid type, then error
    error.type = LangError::UnknownType;
    error.lineNumber = decl->getLine();
    error.errorTokens.push_back(decl->getEntityType());
    error.filename = decl->getFilename();
    errors.push_back(error);
  } else {
    // Validate port names and types
    std::vector<std::shared_ptr<PropertyNode>> ports = decl->getProperties();
    std::vector<ASTNode> blocks = ASTQuery::getModuleEntities(decl);
    scopeStack.push_back({decl, blocks});
    // Set by the resolver when a block is written by a module whose output
    // type comes from its output port, so its type can't be resolved.
    auto unresolvedType = decl->getCompilerProperty("unresolvedType");
    if (unresolvedType && unresolvedType->getNodeType() == AST::String) {
      LangError error;
      error.type = LangError::UnresolvedType;
      error.lineNumber = decl->getLine();
      error.errorTokens.push_back(decl->getName());
      error.errorTokens.push_back(
          std::static_pointer_cast<ValueNode>(unresolvedType)
              ->getStringValue());
      error.filename = decl->getFilename();
      errors.push_back(error);
    }
    auto typeProperties =
        ASTQuery::getPortsForTypeEntity(declaration, scopeStack, tree);
    for (const auto &port : ports) {
      std::string portName = port->getName();
      // Read only properties (writeable: off) are set by the compiler. Only
      // defaults filled in by the compiler may be present.
      if (!port->getCompilerProperty("defaultFilled")) {
        for (const auto &typeProperty : typeProperties) {
          auto typePropertyDecl =
              std::static_pointer_cast<DeclarationNode>(typeProperty);
          auto nameNode = typePropertyDecl->getPropertyValue("name");
          auto writeableNode = typePropertyDecl->getPropertyValue("writeable");
          if (nameNode && nameNode->getNodeType() == AST::String &&
              std::static_pointer_cast<ValueNode>(nameNode)->getStringValue() ==
                  portName &&
              writeableNode && writeableNode->getNodeType() == AST::Switch &&
              !std::static_pointer_cast<ValueNode>(writeableNode)
                   ->getSwitchValue()) {
            LangError error;
            error.type = LangError::ReadOnlyPort;
            error.lineNumber = port->getLine();
            error.errorTokens.push_back(blockType);
            error.errorTokens.push_back(portName);
            error.filename = port->getFilename();
            errors.push_back(error);
          }
        }
      }
      // Check if portname is valid
      std::vector<ASTNode> portTypesList = ASTQuery::getValidTypesForPort(
          declaration, portName, scopeStack, tree);
      if (portTypesList.size() == 0) {
        LangError error;
        error.type = LangError::InvalidPort;
        error.lineNumber = port->getLine();
        error.errorTokens.push_back(blockType);
        error.errorTokens.push_back(portName);
        error.filename = port->getFilename();
        errors.push_back(error);
      } else {
        // Then check type passed to port is valid
        ASTNode portValue = port->getValue();

        if (portValue) {
          std::vector<std::string> validTypeNames = getValidTypeNames(
              portTypesList, scopeStack, tree, {}, currentFramework);
          bool allowsBlock =
              std::find(validTypeNames.begin(), validTypeNames.end(),
                        "_Block") != validTypeNames.end();

          bool portExpectsList = portExpectsListType(portTypesList);
          bool valueIsCollection =
              isCollectionNode(portValue, scopeStack, tree,
                               decl->getNamespaceList(), currentFramework);

          if (allowsBlock && portValue->getNodeType() == AST::Entity) {
            valueIsCollection = false;
          }

          bool isArrayOrGenerator =
              (decl->getNodeType() == AST::ArrayDeclaration ||
               ASTQuery::isGenerator(decl));

          bool isArrayInitList =
              (isArrayOrGenerator &&
               !portExpectsList && portValue->getNodeType() == AST::List);

          if (isArrayInitList) {
            std::string validTypesList;
            for (const auto &type : validTypeNames) {
              validTypesList += type + ",";
            }
            if (validTypesList.size() > 0) {
              validTypesList =
                  validTypesList.substr(0, validTypesList.size() - 1);
            }

            int declaredSize = ASTQuery::getEntityDeclaredSize(decl, scopeStack,
                                                               tree, nullptr);
            if (declaredSize > 0 &&
                static_cast<int>(portValue->getChildren().size()) !=
                    declaredSize) {
              LangError error;
              error.type = LangError::ArraySizeMismatch;
              error.lineNumber = portValue->getLine();
              error.filename = portValue->getFilename();
              errors.push_back(error);
            }

            for (const auto &elem : portValue->getChildren()) {
              // 1. Validate scalar constraints on element (int, real, string,
              // entity)
              for (const auto &validType : portTypesList) {
                if (validType->getNodeType() == AST::Declaration) {
                  auto constrainDecl =
                      std::static_pointer_cast<DeclarationNode>(validType);

                  if (constrainDecl->getEntityType() == "constrainedInt") {
                    validateConstrainedInt(constrainDecl, declaration, portName,
                                           elem, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedReal") {
                    validateConstrainedReal(constrainDecl, declaration,
                                            portName, elem, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedString") {
                    validateConstrainedString(constrainDecl, declaration,
                                              portName, elem, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedEntity") {
                    validateConstrainedEntity(
                        constrainDecl, declaration, portName, elem, errors,
                        scopeStack, tree, decl->getNamespaceList(),
                        currentFramework);
                  }
                }
              }

              // 2. Validate scalar type of element
              bool elemValid = false;
              if (std::find(validTypeNames.begin(), validTypeNames.end(),
                            "_Any") != validTypeNames.end()) {
                elemValid = true;
              } else {
                elemValid = matchesAllowedType(elem, validTypeNames, scopeStack,
                                               tree, {}, currentFramework);
              }

              if (!elemValid) {
                LangError error;
                error.type = LangError::InvalidPortType;
                error.lineNumber = elem->getLine();
                error.errorTokens.push_back(blockType);
                error.errorTokens.push_back(portName);
                error.errorTokens.push_back(
                    getTypeName(elem, scopeStack, tree, {}, currentFramework));
                error.errorTokens.push_back(validTypesList);
                error.filename = elem->getFilename();
                errors.push_back(error);
              }
            }
          } else {
            bool allowsCollection =
                portExpectsList ||
                (std::find(validTypeNames.begin(), validTypeNames.end(),
                           "_List") != validTypeNames.end());
            bool allowsScalar = false;
            for (const auto &tn : validTypeNames) {
              if (tn != "_List") {
                allowsScalar = true;
                break;
              }
            }

            if (valueIsCollection && !allowsCollection) {
              // Port only allows scalar, but received a collection
              LangError error;
              error.type = LangError::InvalidPortType;
              error.lineNumber = port->getLine();
              error.errorTokens.push_back(blockType);
              error.errorTokens.push_back(portName);
              error.errorTokens.push_back("_List");
              std::string validTypesList;
              for (const auto &type : validTypeNames) {
                validTypesList += type + ",";
              }
              if (validTypesList.size() > 0) {
                validTypesList =
                    validTypesList.substr(0, validTypesList.size() - 1);
              }
              error.errorTokens.push_back(validTypesList);
              error.filename = port->getFilename();
              errors.push_back(error);
            } else if (!valueIsCollection && !allowsScalar) {
              // Port only allows collection, but received a scalar
              LangError error;
              error.type = LangError::InvalidPortType;
              error.lineNumber = port->getLine();
              error.errorTokens.push_back(blockType);
              error.errorTokens.push_back(portName);
              error.errorTokens.push_back(getTypeName(
                  portValue, scopeStack, tree, {}, currentFramework));
              error.errorTokens.push_back("_List");
              error.filename = port->getFilename();
              errors.push_back(error);
            } else if (valueIsCollection) {
              // Valid collection! Validate list constraints
              for (const auto &validType : portTypesList) {
                if (validType->getNodeType() == AST::Declaration) {
                  auto constrainDecl =
                      std::static_pointer_cast<DeclarationNode>(validType);
                  if (constrainDecl->getEntityType() == "constrainedList") {
                    validateConstrainedList(
                        constrainDecl, declaration, portName, portValue, errors,
                        scopeStack, tree, {}, currentFramework);
                  }
                }
              }
            } else {
              // Both agree it's a scalar.
              // 1. Validate scalar constraints (int, real, string)
              for (const auto &validType : portTypesList) {
                if (validType->getNodeType() == AST::Declaration) {
                  auto constrainDecl =
                      std::static_pointer_cast<DeclarationNode>(validType);

                  if (constrainDecl->getEntityType() == "constrainedInt") {
                    validateConstrainedInt(constrainDecl, declaration, portName,
                                           portValue, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedReal") {
                    validateConstrainedReal(constrainDecl, declaration,
                                            portName, portValue, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedString") {
                    validateConstrainedString(constrainDecl, declaration,
                                              portName, portValue, errors);
                  }
                  if (constrainDecl->getEntityType() == "constrainedEntity") {
                    validateConstrainedEntity(
                        constrainDecl, declaration, portName, portValue, errors,
                        scopeStack, tree, decl->getNamespaceList(),
                        currentFramework);
                  }
                }
              }

              // 2. Validate scalar type names
              bool typeIsValid = false;
              std::string actualType = getTypeName(portValue, scopeStack, tree,
                                                   {}, currentFramework);
              if (actualType.empty()) {
                actualType = ASTQuery::getNodeName(portValue);
              }

              if (std::find(validTypeNames.begin(), validTypeNames.end(),
                            "_Any") != validTypeNames.end()) {
                typeIsValid = true;
              } else {
                typeIsValid =
                    matchesAllowedType(portValue, validTypeNames, scopeStack,
                                       tree, {}, currentFramework);
              }

              if (!typeIsValid) {
                LangError error;
                error.type = LangError::InvalidPortType;
                error.lineNumber = port->getLine();
                error.errorTokens.push_back(blockType);
                error.errorTokens.push_back(portName);
                error.errorTokens.push_back(actualType);
                std::string validTypesList;
                for (const auto &type : validTypeNames) {
                  validTypesList += type + ",";
                }
                if (validTypesList.size() > 0) {
                  validTypesList =
                      validTypesList.substr(0, validTypesList.size() - 1);
                }
                error.errorTokens.push_back(validTypesList);
                error.filename = port->getFilename();
                errors.push_back(error);
              }
            }
          }
        }
      }
    }
  }
  std::vector<ASTNode> subScope = ASTQuery::getModuleEntities(decl);
  if (decl->getPropertyValue("ports")) {
    for (const ASTNode &port : decl->getPropertyValue("ports")->getChildren()) {
      subScope.push_back(port);
    }
  }
  scopeStack.push_back({decl, subScope});

  frameworkNode = decl->getCompilerProperty("framework");
  if (frameworkNode) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  // For ArrayDeclarations in particular, we need to ignore the array when
  // declaring types. The inner array has no scope set, and trying to find
  // it will fail if the declaration is scoped....
  for (const auto &property : decl->getProperties()) {
    if (property->getName() != "constraints") {
      // Ignore constraints streams as they play by different rules
      std::vector<LangError> internalErrors;
      validateTypes(property->getValue(), internalErrors, scopeStack, tree,
                    decl->getNamespaceList(), frameworkName, processedNodes);
      errors.insert(errors.end(), internalErrors.begin(), internalErrors.end());
    }
  }
}

void ASTValidation::validateConstrainedInt(
    std::shared_ptr<DeclarationNode> constrainDecl,
    std::shared_ptr<DeclarationNode> declaration, std::string portName,
    ASTNode portValue, std::vector<LangError> &errors) {

  if (portValue->getNodeType() == AST::Int) {
    auto codeValue =
        std::static_pointer_cast<ValueNode>(portValue)->getIntValue();
    if (auto maximumNode = constrainDecl->getPropertyValue("maximum")) {
      if (maximumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(maximumNode)->getIntValue();
        if (codeValue > value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto minimumNode = constrainDecl->getPropertyValue("minimum")) {
      if (minimumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(minimumNode)->getIntValue();
        if (codeValue < value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto exMaximumNode =
            constrainDecl->getPropertyValue("exclusiveMaximum")) {
      if (exMaximumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(exMaximumNode)->getIntValue();
        if (codeValue >= value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto exMinimumNode =
            constrainDecl->getPropertyValue("exclusiveMinimum")) {
      if (exMinimumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(exMinimumNode)->getIntValue();
        if (codeValue <= value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto multipleOfNode = constrainDecl->getPropertyValue("multipleOf")) {
      if (multipleOfNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(multipleOfNode)->getIntValue();
        if (codeValue % value != 0) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
  } else {
    LOG_ERROR() << "Failed constraint" << std::endl;
  }
}

void ASTValidation::validateConstrainedReal(
    std::shared_ptr<DeclarationNode> constrainDecl,
    std::shared_ptr<DeclarationNode> declaration, std::string portName,
    ASTNode portValue, std::vector<LangError> &errors) {

  if (portValue->getNodeType() == AST::Int ||
      portValue->getNodeType() == AST::Real) {
    auto codeValue = std::static_pointer_cast<ValueNode>(portValue)->toReal();
    if (auto maximumNode = constrainDecl->getPropertyValue("maximum")) {
      if (maximumNode->getNodeType() == AST::Int ||
          maximumNode->getNodeType() == AST::Real) {
        auto value = std::static_pointer_cast<ValueNode>(maximumNode)->toReal();
        if (codeValue > value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto minimumNode = constrainDecl->getPropertyValue("minimum")) {
      if (minimumNode->getNodeType() == AST::Int ||
          minimumNode->getNodeType() == AST::Real) {
        auto value = std::static_pointer_cast<ValueNode>(minimumNode)->toReal();
        if (codeValue < value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto exMaximumNode =
            constrainDecl->getPropertyValue("exclusiveMaximum")) {
      if (exMaximumNode->getNodeType() == AST::Int ||
          exMaximumNode->getNodeType() == AST::Real) {
        auto value =
            std::static_pointer_cast<ValueNode>(exMaximumNode)->toReal();
        if (codeValue >= value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto exMinimumNode =
            constrainDecl->getPropertyValue("exclusiveMinimum")) {
      if (exMinimumNode->getNodeType() == AST::Int ||
          exMinimumNode->getNodeType() == AST::Real) {
        auto value =
            std::static_pointer_cast<ValueNode>(exMinimumNode)->toReal();
        if (codeValue <= value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
  }
}

void ASTValidation::validateConstrainedString(
    std::shared_ptr<DeclarationNode> constrainDecl,
    std::shared_ptr<DeclarationNode> declaration, std::string portName,
    ASTNode portValue, std::vector<LangError> &errors) {

  if (portValue->getNodeType() == AST::String) {
    auto codeValue =
        std::static_pointer_cast<ValueNode>(portValue)->getStringValue();
    if (auto maximumNode = constrainDecl->getPropertyValue("maxLength")) {
      if (maximumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(maximumNode)->getIntValue();
        if (codeValue.size() > value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
    if (auto minimumNode = constrainDecl->getPropertyValue("minLength")) {
      if (minimumNode->getNodeType() == AST::Int) {
        auto value =
            std::static_pointer_cast<ValueNode>(minimumNode)->getIntValue();
        if (codeValue.size() < value) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
  }
}

void ASTValidation::validateConstrainedList(
    std::shared_ptr<DeclarationNode> constrainDecl,
    std::shared_ptr<DeclarationNode> declaration, std::string portName,
    ASTNode portValue, std::vector<LangError> &errors, ScopeStack scopeStack,
    ASTNode tree, std::vector<std::string> parentNamespace,
    std::string currentFramework) {

  int collSize =
      getCollectionSize(portValue, scopeStack, tree, currentFramework);

  auto maximumNode = constrainDecl->getPropertyValue("maxItems");
  if (maximumNode && maximumNode->getNodeType() == AST::Int) {
    int maxVal =
        std::static_pointer_cast<ValueNode>(maximumNode)->getIntValue();
    if (collSize < 0) {
      LOG_WARNING()
          << "Warning: Cannot evaluate array/slice size at compile time "
             "for maxItems constraint validation on property '"
          << portName << "' in " << declaration->getEntityType() << std::endl;
    } else if (collSize > maxVal) {
      LangError error;
      error.lineNumber = portValue->getLine();
      error.filename = portValue->getFilename();
      error.type = LangError::ConstraintFail;
      error.errorTokens.push_back(declaration->getEntityType());
      error.errorTokens.push_back(portName);
      error.errorTokens.push_back(AST::toText(constrainDecl));
      error.errorTokens.push_back(AST::toText(portValue));
      errors.push_back(error);
    }
  }

  auto minimumNode = constrainDecl->getPropertyValue("minItems");
  if (minimumNode && minimumNode->getNodeType() == AST::Int) {
    int minVal =
        std::static_pointer_cast<ValueNode>(minimumNode)->getIntValue();
    if (collSize < 0) {
      LOG_WARNING()
          << "Warning: Cannot evaluate array/slice size at compile time "
             "for minItems constraint validation on property '"
          << portName << "' in " << declaration->getEntityType() << std::endl;
    } else if (collSize < minVal) {
      LangError error;
      error.lineNumber = portValue->getLine();
      error.filename = portValue->getFilename();
      error.type = LangError::ConstraintFail;
      error.errorTokens.push_back(declaration->getEntityType());
      error.errorTokens.push_back(portName);
      error.errorTokens.push_back(AST::toText(constrainDecl));
      error.errorTokens.push_back(AST::toText(portValue));
      errors.push_back(error);
    }
  }

  if (portValue->getNodeType() == AST::List) {
    if (auto uniqueNode = constrainDecl->getPropertyValue("uniqueItems")) {
      if (uniqueNode->getNodeType() == AST::Switch &&
          std::static_pointer_cast<ValueNode>(uniqueNode)->getSwitchValue() ==
              true) {
        bool repeated = false;
        std::unordered_set<std::string> previous;
        for (const auto &node : portValue->getChildren()) {
          auto newString = AST::toText(node, 0, 0, false);
          if (previous.find(newString) != previous.end()) {
            repeated = true;
            break;
          }
          previous.insert(newString);
        }

        if (repeated) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(portName);
          error.errorTokens.push_back(AST::toText(constrainDecl));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
  }

  auto allowedNode = constrainDecl->getPropertyValue("allowed");

  if (allowedNode && allowedNode->getNodeType() == AST::List) {
    if (allowedNode->getChildren().size() > 0) {
      auto allowedTypes = allowedNode->getChildren();
      auto validTypes = getValidTypeNames(allowedTypes, scopeStack, tree,
                                          parentNamespace, currentFramework);

      if (portValue->getNodeType() == AST::List) {
        for (const auto &node : portValue->getChildren()) {
          if (!matchesAllowedType(node, validTypes, scopeStack, tree,
                                  parentNamespace, currentFramework)) {
            LangError error;
            error.lineNumber = portValue->getLine();
            error.filename = portValue->getFilename();
            error.type = LangError::ConstraintFail;
            error.errorTokens.push_back(declaration->getEntityType());
            error.errorTokens.push_back(AST::toText(allowedNode));
            error.errorTokens.push_back(AST::toText(node));
            error.errorTokens.push_back(AST::toText(portValue));
            errors.push_back(error);
          }
        }
      } else if (portValue->getNodeType() == AST::GeneratorRange) {
        auto dummyInt = std::make_shared<ValueNode>(
            (int64_t)0, portValue->getFilename().c_str(), portValue->getLine());
        if (!matchesAllowedType(dummyInt, validTypes, scopeStack, tree,
                                parentNamespace, currentFramework)) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(AST::toText(allowedNode));
          error.errorTokens.push_back("_IntLiteral");
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      } else {
        // portValue is an array slice or bundle
        std::string name = ASTQuery::getNodeName(portValue);
        std::vector<std::string> namespaces = portValue->getNamespaceList();
        namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                          parentNamespace.end());
        auto foundDecl = ASTQuery::findDeclarationByName(
            name, scopeStack, tree, namespaces, currentFramework);
        std::shared_ptr<DeclarationNode> typeDecl = nullptr;
        if (foundDecl->getEntityType() == "type" ||
            foundDecl->getEntityType() == "platformBlock") {
          typeDecl = foundDecl;
        } else {
          typeDecl = ASTQuery::findTypeDeclaration(foundDecl, scopeStack, tree,
                                                   currentFramework);
          if (!typeDecl) {
            typeDecl = ASTQuery::findTypeDeclarationByName(
                foundDecl->getEntityType(), scopeStack, tree, parentNamespace,
                currentFramework);
          }
        }
        bool matched = false;
        if (std::find(validTypes.begin(), validTypes.end(), "_Any") !=
            validTypes.end()) {
          matched = true;
        } else if (typeDecl) {
          if (std::find(validTypes.begin(), validTypes.end(),
                        typeDecl->getName()) != validTypes.end()) {
            matched = true;
          } else {
            auto inherited =
                ASTQuery::getInheritedTypes(typeDecl, scopeStack, tree);
            for (const auto &inh : inherited) {
              if (std::find(validTypes.begin(), validTypes.end(),
                            inh->getName()) != validTypes.end()) {
                matched = true;
                break;
              }
            }
          }
        }
        if (!matched) {
          LangError error;
          error.lineNumber = portValue->getLine();
          error.filename = portValue->getFilename();
          error.type = LangError::ConstraintFail;
          error.errorTokens.push_back(declaration->getEntityType());
          error.errorTokens.push_back(AST::toText(allowedNode));
          error.errorTokens.push_back(AST::toText(portValue));
          error.errorTokens.push_back(AST::toText(portValue));
          errors.push_back(error);
        }
      }
    }
  } else if (allowedNode && allowedNode->getNodeType() != AST::None) {
    LOG_ERROR() << "ERROR expecting list or none for allowed in constrainedList"
                << std::endl;
  }
}

void ASTValidation::validateConstrainedEntity(
    std::shared_ptr<DeclarationNode> constrainDecl,
    std::shared_ptr<DeclarationNode> declaration, std::string portName,
    ASTNode portValue, std::vector<LangError> &errors, ScopeStack scopeStack,
    ASTNode tree, std::vector<std::string> parentNamespace,
    std::string currentFramework) {
  if (!constrainDecl || !portValue) {
    return;
  }
  auto allowedNode = constrainDecl->getPropertyValue("allowed");
  if (!allowedNode) {
    return;
  }
  std::vector<ASTNode> allowedTypes;
  if (allowedNode->getNodeType() == AST::List) {
    allowedTypes = allowedNode->getChildren();
  } else {
    allowedTypes.push_back(allowedNode);
  }
  for (const auto &typeNode : allowedTypes) {
    if (typeNode->getNodeType() == AST::Declaration) {
      auto subDecl = std::static_pointer_cast<DeclarationNode>(typeNode);
      if (subDecl->getEntityType() == "constrainedInt" &&
          portValue->getNodeType() == AST::Int) {
        validateConstrainedInt(subDecl, declaration, portName, portValue,
                               errors);
      } else if (subDecl->getEntityType() == "constrainedReal" &&
                 (portValue->getNodeType() == AST::Real ||
                  portValue->getNodeType() == AST::Int)) {
        validateConstrainedReal(subDecl, declaration, portName, portValue,
                                errors);
      } else if (subDecl->getEntityType() == "constrainedString" &&
                 portValue->getNodeType() == AST::String) {
        validateConstrainedString(subDecl, declaration, portName, portValue,
                                  errors);
      }
    }
  }
}

std::string ASTValidation::getTypeName(ASTNode child, ScopeStack scopeStack,
                                       ASTNode tree,
                                       std::vector<std::string> parentNamespace,
                                       std::string currentFramework) {
  if (!child) {
    return std::string();
  }
  auto resolvedFrom = child->getCompilerProperty("resolvedFrom");
  if (resolvedFrom) {
    auto resolvedTypeName = getTypeName(resolvedFrom, scopeStack, tree,
                                        parentNamespace, currentFramework);
    if (!resolvedTypeName.empty()) {
      return resolvedTypeName;
    }
  }
  if (child->getNodeType() == AST::Declaration ||
      child->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    if (decl->getEntityType() == "type" ||
        decl->getEntityType() == "platformBlock") {
      return decl->getName();
    }
    auto typeDeclaration =
        ASTQuery::findTypeDeclaration(decl, scopeStack, tree, currentFramework);
    if (!typeDeclaration) {
      typeDeclaration = ASTQuery::findTypeDeclarationByName(
          decl->getEntityType(), scopeStack, tree, parentNamespace,
          currentFramework);
    }
    if (typeDeclaration) {
      return typeDeclaration->getName();
    }
  } else if (child->getNodeType() == AST::Entity ||
             child->getNodeType() == AST::Array) {
    std::vector<std::string> namespaces = child->getNamespaceList();
    namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                      parentNamespace.end());
    auto decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(child),
                                                scopeStack, tree, namespaces,
                                                currentFramework);
    if (decl) {
      if (decl->getEntityType() == "type" ||
          decl->getEntityType() == "platformBlock") {
        return decl->getName();
      }
      auto typeDeclaration = ASTQuery::findTypeDeclaration(
          decl, scopeStack, tree, currentFramework);
      if (!typeDeclaration) {
        typeDeclaration = ASTQuery::findTypeDeclarationByName(
            decl->getEntityType(), scopeStack, tree, parentNamespace,
            currentFramework);
      }
      if (typeDeclaration) {
        return typeDeclaration->getName();
      }
    }
    return std::string();
  } else if (child->getNodeType() == AST::String) {
    return std::string("_StringLiteral");
  } else if (child->getNodeType() == AST::Int) {
    return std::string("_IntLiteral");
  } else if (child->getNodeType() == AST::Real) {
    return std::string("_RealLiteral");
  } else if (child->getNodeType() == AST::Switch) {
    return std::string("_SwitchLiteral");
  } else if (child->getNodeType() == AST::MemberAccess) {
    return std::string("_MemberAccess");
  } else if (child->getNodeType() == AST::None) {
    return std::string("_NoneLiteral");
  } else if (child->getNodeType() == AST::List) {
    return std::string("_List");
  } else if (child->getNodeType() == AST::GeneratorRange) {
    return std::string("_GeneratorRange");
  } else if (child->getNodeType() == AST::Stream) {
    return std::string("_StreamType");
  }
  return std::string();
}

std::vector<std::string> ASTValidation::getValidTypeNames(
    std::vector<ASTNode> portTypesList, ScopeStack scopeStack, ASTNode tree,
    std::vector<std::string> parentNamespace, std::string currentFramework) {
  std::vector<std::string> validTypeNames;
  for (const ASTNode &validType : portTypesList) {
    if (validType->getNodeType() == AST::String) {
      std::string typeCode =
          static_cast<ValueNode *>(validType.get())->getStringValue();
      validTypeNames.push_back(typeCode);
    } else if (validType->getNodeType() == AST::Entity) {
      auto blockNode = std::static_pointer_cast<EntityNode>(validType);
      if (blockNode->getName() == "_Any") {
        validTypeNames.push_back("_Any");
      } else if (blockNode->getName() == "_List") {
        validTypeNames.push_back("_List");
      } else if (blockNode->getName() == "_ExplicitList") {
        validTypeNames.push_back("_ExplicitList");
      } else if (blockNode->getName() == "_GeneratorRange") {
        validTypeNames.push_back("_GeneratorRange");
      } else if (blockNode->getName() == "_Block") {
        validTypeNames.push_back("_Block");
      } else if (auto typeClass = ASTQuery::findTypeClass(
                     blockNode->getName(), tree, currentFramework)) {
        // A type class allows any of its members
        if (auto typesNode = typeClass->getPropertyValue("types")) {
          for (const auto &member : typesNode->getChildren()) {
            validTypeNames.push_back(ASTQuery::getNodeName(member));
          }
        }
      } else {
        std::vector<std::string> namespaces = blockNode->getNamespaceList();
        namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                          parentNamespace.end());
        std::shared_ptr<DeclarationNode> declaration =
            ASTQuery::findDeclarationByName(blockNode->getName(), scopeStack,
                                            tree, namespaces, currentFramework);
        if (declaration) {
          std::string validTypeName = declaration->getName();
          validTypeNames.push_back(validTypeName);
        } else {
          validTypeNames.push_back(blockNode->getName());
        }
      }
    } else if (validType->getNodeType() == AST::Declaration) {
      auto constrainDecl = std::static_pointer_cast<DeclarationNode>(validType);

      if (constrainDecl->getEntityType() == "constrainedInt") {
        validTypeNames.push_back("_IntLiteral");
      }
      if (constrainDecl->getEntityType() == "constrainedReal") {
        validTypeNames.push_back("_IntLiteral");
        validTypeNames.push_back("_RealLiteral");
      }
      if (constrainDecl->getEntityType() == "constrainedString") {
        validTypeNames.push_back("_StringLiteral");
      }
      if (constrainDecl->getEntityType() == "constrainedList") {
        validTypeNames.push_back("_List");
      }
      if (constrainDecl->getEntityType() == "constrainedEntity") {
        auto allowedNode = constrainDecl->getPropertyValue("allowed");
        if (allowedNode) {
          std::vector<ASTNode> allowedList;
          if (allowedNode->getNodeType() == AST::List) {
            allowedList = allowedNode->getChildren();
          } else {
            allowedList.push_back(allowedNode);
          }
          auto subTypes = getValidTypeNames(allowedList, scopeStack, tree,
                                            parentNamespace, currentFramework);
          validTypeNames.insert(validTypeNames.end(), subTypes.begin(),
                                subTypes.end());
        }
      }
    }
  }
  return validTypeNames;
}

bool ASTValidation::isCollectionNode(
    ASTNode node, const ScopeStack &scopeStack, ASTNode tree,
    const std::vector<std::string> &parentNamespace,
    const std::string &currentFramework) {
  if (!node) {
    return false;
  }
  if (node->getNodeType() == AST::List) {
    return true;
  }
  if (node->getNodeType() == AST::GeneratorRange) {
    return true;
  }
  if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    if (arrayNode->index()) {
      for (const auto &idx : arrayNode->index()->getChildren()) {
        if (idx->getNodeType() == AST::Range ||
            idx->getNodeType() == AST::GeneratorRange) {
          return true; // Array slice, e.g. Sig[0:8] or Sig[0..8]
        }
      }
    }
    return false; // Single element access, e.g. Sig[1] -> scalar
  }
  if (node->getNodeType() == AST::Entity) {
    auto entityNode = std::static_pointer_cast<EntityNode>(node);
    std::vector<std::string> namespaces = entityNode->getNamespaceList();
    namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                      parentNamespace.end());
    auto decl = ASTQuery::findDeclarationByName(
        entityNode->getName(), scopeStack, tree, namespaces, currentFramework);
    if (decl && (decl->getNodeType() == AST::ArrayDeclaration ||
                 ASTQuery::isGenerator(decl))) {
      return true; // Full array bundle reference, e.g. Sig -> collection
    }
    return false;
  }
  if (node->getNodeType() == AST::ArrayDeclaration) {
    return true;
  }
  return false;
}

bool ASTValidation::portExpectsListType(
    const std::vector<ASTNode> &portTypesList) {
  for (const auto &validType : portTypesList) {
    if (validType->getNodeType() == AST::Declaration) {
      auto constrainDecl = std::static_pointer_cast<DeclarationNode>(validType);
      if (constrainDecl->getEntityType() == "constrainedList") {
        return true;
      }
    } else if (validType->getNodeType() == AST::Entity) {
      auto entityNode = std::static_pointer_cast<EntityNode>(validType);
      if (entityNode->getName() == "_List" ||
          entityNode->getName() == "_ExplicitList" ||
          entityNode->getName() == "_GeneratorRange") {
        return true;
      }
    }
  }
  return false;
}

int ASTValidation::getCollectionSize(ASTNode node, const ScopeStack &scopeStack,
                                     ASTNode tree,
                                     const std::string &currentFramework) {
  if (!node) {
    return -1;
  }
  if (node->getNodeType() == AST::List) {
    return static_cast<int>(node->getChildren().size());
  }
  if (node->getNodeType() == AST::GeneratorRange) {
    auto genRange = std::static_pointer_cast<GeneratorRangeNode>(node);
    if (genRange->startIndex() && genRange->endIndex() &&
        genRange->startIndex()->getNodeType() == AST::Int &&
        genRange->endIndex()->getNodeType() == AST::Int) {
      int start = std::static_pointer_cast<ValueNode>(genRange->startIndex())
                      ->getIntValue();
      int end = std::static_pointer_cast<ValueNode>(genRange->endIndex())
                    ->getIntValue();
      int step = 1;
      if (genRange->stepIndex() &&
          genRange->stepIndex()->getNodeType() == AST::Int) {
        step = std::static_pointer_cast<ValueNode>(genRange->stepIndex())
                   ->getIntValue();
      }
      if (step != 0) {
        return (std::abs(end - start) / std::abs(step)) + 1;
      }
    }
    return -1; // dynamic or unknown
  }
  if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    if (arrayNode->index()) {
      auto indices = arrayNode->index()->getChildren();
      if (!indices.empty()) {
        if (indices[0]->getNodeType() == AST::Range) {
          auto range = std::static_pointer_cast<RangeNode>(indices[0]);
          if (range->startIndex()->getNodeType() == AST::Int &&
              range->endIndex()->getNodeType() == AST::Int) {
            int start = std::static_pointer_cast<ValueNode>(range->startIndex())
                            ->getIntValue();
            int end = std::static_pointer_cast<ValueNode>(range->endIndex())
                          ->getIntValue();
            if (end >= start) {
              return (end - start) + 1; // inclusive range e.g. 0:8 -> 9 items
            }
          }
        } else if (indices[0]->getNodeType() == AST::GeneratorRange) {
          auto genRange =
              std::static_pointer_cast<GeneratorRangeNode>(indices[0]);
          if (genRange->startIndex()->getNodeType() == AST::Int &&
              genRange->endIndex()->getNodeType() == AST::Int) {
            int start =
                std::static_pointer_cast<ValueNode>(genRange->startIndex())
                    ->getIntValue();
            int end = std::static_pointer_cast<ValueNode>(genRange->endIndex())
                          ->getIntValue();
            int stepVal = 1;
            if (genRange->stepIndex() &&
                genRange->stepIndex()->getNodeType() == AST::Int) {
              stepVal =
                  std::static_pointer_cast<ValueNode>(genRange->stepIndex())
                      ->getIntValue();
            }
            if (stepVal > 0 && end >= start) {
              return (end - start) / stepVal + 1;
            }
          }
        }
      }
    }
    return -1; // dynamic or unknown
  }
  if (node->getNodeType() == AST::Entity) {
    auto entityNode = std::static_pointer_cast<EntityNode>(node);
    auto decl = ASTQuery::findDeclarationByName(
        entityNode->getName(), scopeStack, tree, entityNode->getNamespaceList(),
        currentFramework);
    if (decl && (decl->getNodeType() == AST::ArrayDeclaration ||
                 ASTQuery::isGenerator(decl))) {
      std::vector<LangError> errors;
      return ASTQuery::getEntityDeclaredSize(decl, scopeStack, tree, &errors);
    }
    return -1; // dynamic or unknown
  }
  return -1;
}

bool ASTValidation::matchesAllowedType(
    ASTNode element, const std::vector<std::string> &validTypes,
    ScopeStack scopeStack, ASTNode tree,
    std::vector<std::string> parentNamespace, std::string currentFramework) {
  if (!element) {
    return false;
  }

  auto resolvedFrom = element->getCompilerProperty("resolvedFrom");
  if (resolvedFrom) {
    if (matchesAllowedType(resolvedFrom, validTypes, scopeStack, tree,
                           parentNamespace, currentFramework)) {
      return true;
    }
  }

  // 1. Check _Any wildcard
  if (std::find(validTypes.begin(), validTypes.end(), "_Any") !=
      validTypes.end()) {
    // _Any matches any scalar element
    if (!isCollectionNode(element, scopeStack, tree, parentNamespace,
                          currentFramework)) {
      return true;
    }
  }

  // 1b. Check _Block wildcard / type: matches any block declaration entity
  // (scalar or bundle)
  if (std::find(validTypes.begin(), validTypes.end(), "_Block") !=
      validTypes.end()) {
    if (element->getNodeType() == AST::Entity) {
      std::string name = ASTQuery::getNodeName(element);
      std::vector<std::string> namespaces = element->getNamespaceList();
      namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                        parentNamespace.end());
      auto decl = ASTQuery::findDeclarationByName(name, scopeStack, tree,
                                                  namespaces, currentFramework);
      if (decl && (decl->getNodeType() == AST::Declaration ||
                   decl->getNodeType() == AST::ArrayDeclaration)) {
        std::string entityType = decl->getEntityType();
        if (entityType != "module" && entityType != "reaction" &&
            entityType != "loop" && entityType != "type" &&
            entityType != "mainInputPort" && entityType != "mainOutputPort" &&
            entityType != "propertyInputPort" &&
            entityType != "propertyOutputPort" && entityType != "port" &&
            entityType != "domainDefinition" &&
            entityType != "platformModule") {
          return true;
        }
      }
    } else if (element->getNodeType() == AST::Declaration ||
               element->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(element);
      std::string entityType = decl->getEntityType();
      if (entityType != "module" && entityType != "reaction" &&
          entityType != "loop" && entityType != "type" &&
          entityType != "mainInputPort" && entityType != "mainOutputPort" &&
          entityType != "propertyInputPort" &&
          entityType != "propertyOutputPort" && entityType != "port" &&
          entityType != "domainDefinition" && entityType != "platformModule") {
        return true;
      }
    }
  }

  // 2. Literals and built-in AST types
  if (element->getNodeType() == AST::String) {
    return std::find(validTypes.begin(), validTypes.end(), "_StringLiteral") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::Int) {
    return std::find(validTypes.begin(), validTypes.end(), "_IntLiteral") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::Real) {
    return std::find(validTypes.begin(), validTypes.end(), "_RealLiteral") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::Switch) {
    return std::find(validTypes.begin(), validTypes.end(), "_SwitchLiteral") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::None) {
    return std::find(validTypes.begin(), validTypes.end(), "_NoneLiteral") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::Stream) {
    return std::find(validTypes.begin(), validTypes.end(), "_StreamType") !=
           validTypes.end();
  }
  if (element->getNodeType() == AST::List) {
    return std::find(validTypes.begin(), validTypes.end(), "_List") !=
               validTypes.end() ||
           std::find(validTypes.begin(), validTypes.end(), "_ExplicitList") !=
               validTypes.end() ||
           std::find(validTypes.begin(), validTypes.end(), "_ListLiteral") !=
               validTypes.end();
  }
  if (element->getNodeType() == AST::GeneratorRange) {
    return std::find(validTypes.begin(), validTypes.end(), "_List") !=
               validTypes.end() ||
           std::find(validTypes.begin(), validTypes.end(), "_GeneratorRange") !=
               validTypes.end();
  }
  if (element->getNodeType() == AST::MemberAccess) {
    // TODO: Fully resolve the data type of member access expressions (e.g.
    // OutputPort.rate) by looking up the base object declaration and resolving
    // the accessed member property type. For now, treat member access as a
    // wildcard matching int and real literals (and port properties).
    if (std::find(validTypes.begin(), validTypes.end(), "_IntLiteral") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_RealLiteral") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_PortProperty") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_IntType") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_RealType") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_DomainDefinition") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_StringLiteral") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_SwitchType") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_Any") !=
            validTypes.end() ||
        std::find(validTypes.begin(), validTypes.end(), "_MemberAccess") !=
            validTypes.end()) {
      return true;
    }
  }

  // 3. Declarations (inline declaration or array declaration)
  if (element->getNodeType() == AST::Declaration ||
      element->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(element);
    std::shared_ptr<DeclarationNode> typeDecl = nullptr;
    if (decl->getEntityType() == "type" ||
        decl->getEntityType() == "platformBlock") {
      typeDecl = decl;
    } else {
      typeDecl = ASTQuery::findTypeDeclaration(decl, scopeStack, tree,
                                               currentFramework);
      if (!typeDecl) {
        typeDecl = ASTQuery::findTypeDeclarationByName(
            decl->getEntityType(), scopeStack, tree, parentNamespace,
            currentFramework);
      }
    }
    if (typeDecl) {
      if (std::find(validTypes.begin(), validTypes.end(),
                    typeDecl->getName()) != validTypes.end()) {
        return true;
      }
      auto inherited = ASTQuery::getInheritedTypes(typeDecl, scopeStack, tree);
      for (const auto &inh : inherited) {
        if (std::find(validTypes.begin(), validTypes.end(), inh->getName()) !=
            validTypes.end()) {
          return true;
        }
      }
    }
    return false;
  }

  // 4. Entities and Arrays (references to named declarations or type
  // declarations)
  if (element->getNodeType() == AST::Entity ||
      element->getNodeType() == AST::Array) {
    std::string name = ASTQuery::getNodeName(element);
    std::vector<std::string> namespaces = element->getNamespaceList();
    namespaces.insert(namespaces.begin(), parentNamespace.begin(),
                      parentNamespace.end());
    auto decl = ASTQuery::findDeclarationByName(name, scopeStack, tree,
                                                namespaces, currentFramework);
    if (decl) {
      if (std::find(validTypes.begin(), validTypes.end(), decl->getName()) !=
          validTypes.end()) {
        return true;
      }
      std::shared_ptr<DeclarationNode> typeDecl = nullptr;
      if (decl->getEntityType() == "type" ||
          decl->getEntityType() == "platformBlock") {
        typeDecl = decl;
      } else {
        typeDecl = ASTQuery::findTypeDeclaration(decl, scopeStack, tree,
                                                 currentFramework);
        if (!typeDecl) {
          typeDecl = ASTQuery::findTypeDeclarationByName(
              decl->getEntityType(), scopeStack, tree, parentNamespace,
              currentFramework);
        }
      }
      if (typeDecl) {
        if (std::find(validTypes.begin(), validTypes.end(),
                      typeDecl->getName()) != validTypes.end()) {
          return true;
        }
        auto inherited =
            ASTQuery::getInheritedTypes(typeDecl, scopeStack, tree);
        for (const auto &inh : inherited) {
          if (std::find(validTypes.begin(), validTypes.end(), inh->getName()) !=
              validTypes.end()) {
            return true;
          }
        }
      }
    } else {
      // If no declaration found, check if the entity name itself matches a
      // valid type
      if (std::find(validTypes.begin(), validTypes.end(), name) !=
          validTypes.end()) {
        return true;
      }
    }
    return false;
  }

  return false;
}
