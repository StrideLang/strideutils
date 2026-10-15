/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Based on Modified BSD License code by Andres Cabrera and Joseph Tilbian.

    Author: Andres Cabrera
*/

#ifndef ASTQUERY_H
#define ASTQUERY_H

#include "stride/parser/strideparser.h"

#include <vector>

namespace strd {
typedef std::vector<std::pair<ASTNode, std::vector<ASTNode>>> ScopeStack;

class ASTQuery {
public:
  static std::vector<std::shared_ptr<SystemNode>> getSystemNodes(ASTNode tree);

  static std::vector<std::shared_ptr<ImportNode>> getImportNodes(ASTNode tree);

  static std::vector<std::shared_ptr<PackageNode>> getPackageNodes(ASTNode tree);

  // Find declarations
  static std::shared_ptr<DeclarationNode>
  findDeclarationWithType(std::string objectName, std::string type,
                          const ScopeStack &scopeStack, ASTNode tree,
                          std::vector<std::string> namespaces = {},
                          std::string framework = std::string());

  static std::shared_ptr<DeclarationNode>
  findDeclarationByName(std::string objectName, const ScopeStack &scopeStack,
                        ASTNode tree, std::vector<std::string> namespaces = {},
                        std::string framework = std::string());

  static std::vector<std::shared_ptr<DeclarationNode>>
  findAllDeclarations(std::string objectName, const ScopeStack &scopeStack,
                      ASTNode tree, std::vector<std::string> namespaces = {},
                      std::string currentFramework = std::string());

  static std::shared_ptr<DeclarationNode>
  findTypeDeclaration(std::shared_ptr<DeclarationNode> entityDecl, ScopeStack scope,
                      ASTNode tree, std::string currentFramework = "");

  static std::shared_ptr<DeclarationNode>
  findTypeDeclarationByName(std::string typeName, ScopeStack scope,
                            ASTNode tree,
                            std::vector<std::string> namespaces = {},
                            std::string currentFramework = "");

  // Get name for node. Names are found in different places for different types
  // of nodes
  static std::string getNodeName(ASTNode node);

  ///// Size queries
  /// Get the number of parallel nodes implicit in node. i.e. into how many
  /// parallel streams can the node be broken up.
  static int getNodeSize(ASTNode node, const ScopeStack &scopeStack,
                         ASTNode tree);

  static int getArraySize(std::shared_ptr<ArrayNode> arrayNode, ScopeStack scope,
                          ASTNode tree,
                          std::vector<LangError> *errors = nullptr);

  static int getEntityDeclaredSize(std::shared_ptr<DeclarationNode> entityDecl,
                                   ScopeStack scope, ASTNode tree,
                                   std::vector<LangError> *errors = nullptr);

  static std::shared_ptr<DeclarationNode> synthesizeArrayDeclarationElement(
      std::shared_ptr<DeclarationNode> arrayDeclaration, uint64_t index,
      ScopeStack scope, ASTNode tree);

  static int
  getLargestPropertySize(std::vector<std::shared_ptr<PropertyNode>> &properties,
                         ScopeStack scope, ASTNode tree,
                         std::vector<LangError> *errors = nullptr);
  // Inheritance information
  static std::vector<std::shared_ptr<DeclarationNode>>
  getInheritedTypes(std::shared_ptr<DeclarationNode> entityTypeDecl,
                    const ScopeStack &scope, ASTNode tree);

  static std::vector<ASTNode>
  getInheritedPorts(std::shared_ptr<DeclarationNode> entityDecl, ScopeStack scope,
                    ASTNode tree);

  // Type information
  static std::vector<ASTNode>
  getPortsForType(std::string typeName, ScopeStack scope, ASTNode tree,
                  std::vector<std::string> namespaces = {},
                  std::string framework = "");

  static std::vector<ASTNode>
  getPortsForTypeEntity(std::shared_ptr<DeclarationNode> entityDecl, ScopeStack scope,
                        ASTNode tree);

  static std::vector<ASTNode>
  getValidTypesForPort(std::shared_ptr<DeclarationNode> typeDeclaration,
                       std::string portName, ScopeStack scope, ASTNode tree);

  // Module Query
  static std::vector<ASTNode>
  getModuleEntities(std::shared_ptr<DeclarationNode> moduleDecl);

  static std::vector<ASTNode>
  getStateMachineEntities(std::shared_ptr<DeclarationNode> stateDecl);

  static std::shared_ptr<DeclarationNode>
  getModuleMainOutputPort(std::shared_ptr<DeclarationNode> moduleDecl);

  /// Returns mainInputPort declaration
  static std::shared_ptr<DeclarationNode>
  getModuleMainInputPort(std::shared_ptr<DeclarationNode> moduleDecl);

  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyOutputPorts(
      std::shared_ptr<DeclarationNode> moduleDecl);

  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyInputPorts(std::shared_ptr<DeclarationNode> moduleDecl);

  static std::shared_ptr<DeclarationNode>
  getModulePort(std::shared_ptr<DeclarationNode> moduleDecl, std::string name);

  static std::vector<std::string>
  getModulePortNames(std::shared_ptr<DeclarationNode> entityDeclaration);

  // Array Declarations
  static ASTNode
  getMemberFromArrayConst(std::shared_ptr<DeclarationNode> arrayDecl,
                          int index, ASTNode tree, ScopeStack scopeStack,
                          std::vector<LangError> *errors);

  // Const Declarations
  static ASTNode getValueFromConstEntity(DeclarationNode *entityDecl);

  // List nodes
  static ASTNode getMemberFromList(ListNode *node, int index,
                                   std::vector<LangError> *errors);

  // Property Nodes
  static std::shared_ptr<PropertyNode>
  findPropertyByName(std::vector<std::shared_ptr<PropertyNode>> properties,
                     std::string propertyName);

  static bool isCodeGenerator(std::shared_ptr<DeclarationNode> typeDecl,
                              const ScopeStack &scope, ASTNode tree);
  static bool isConstant(std::shared_ptr<DeclarationNode> typeDecl,
                         const ScopeStack &scope, ASTNode tree);
  static bool isDomainMember(std::shared_ptr<DeclarationNode> typeDecl,
                             const ScopeStack &scope, ASTNode tree);
  static bool isCallable(std::shared_ptr<DeclarationNode> typeDecl,
                         const ScopeStack &scope, ASTNode tree);
  static bool isStronglyTyped(std::shared_ptr<DeclarationNode> typeDecl,
                              const ScopeStack &scope, ASTNode tree);
  static bool isInputPort(std::shared_ptr<DeclarationNode> entityDecl,
                          std::shared_ptr<DeclarationNode> funcDecl,
                          const ScopeStack &scope, ASTNode tree);
  static bool isOutputPort(std::shared_ptr<DeclarationNode> entityDecl,
                           std::shared_ptr<DeclarationNode> funcDecl,
                           const ScopeStack &scope, ASTNode tree);
  static bool isPortEntity(std::shared_ptr<DeclarationNode> entityDecl,
                           std::shared_ptr<DeclarationNode> funcDecl,
                           const ScopeStack &scope, ASTNode tree);
  static bool isStatelessGenerator(std::shared_ptr<DeclarationNode> typeDecl,
                                   const ScopeStack &scope, ASTNode tree);
  static bool isStateNode(std::shared_ptr<DeclarationNode> typeDecl,
                          const ScopeStack &scope, ASTNode tree);
  static bool isTransitionNode(std::shared_ptr<DeclarationNode> typeDecl,
                               const ScopeStack &scope, ASTNode tree);
  static bool isDomainDefinition(std::shared_ptr<DeclarationNode> typeDecl,
                                 const ScopeStack &scope, ASTNode tree);

private:
  static bool namespaceMatch(std::vector<std::string> scopeList,
                             std::shared_ptr<DeclarationNode> decl,
                             std::string currentFramework = "");
};
} // namespace strd

#endif // ASTQUERY_H
