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
  static int getBundleSize(std::shared_ptr<ArrayNode> arrayNode, ScopeStack scope,
                           ASTNode tree,
                           std::vector<LangError> *errors = nullptr) {
    return getArraySize(arrayNode, scope, tree, errors);
  }

  static int getEntityDeclaredSize(std::shared_ptr<DeclarationNode> entityDecl,
                                   ScopeStack scope, ASTNode tree,
                                   std::vector<LangError> *errors = nullptr);
  static int getBlockDeclaredSize(std::shared_ptr<DeclarationNode> block,
                                  ScopeStack scope, ASTNode tree,
                                  std::vector<LangError> *errors = nullptr) {
    return getEntityDeclaredSize(block, scope, tree, errors);
  }

  static std::shared_ptr<DeclarationNode> synthesizeArrayDeclarationElement(
      std::shared_ptr<DeclarationNode> arrayDeclaration, uint64_t index,
      ScopeStack scope, ASTNode tree);
  static std::shared_ptr<DeclarationNode> synthesizeBundleDeclarationElement(
      std::shared_ptr<DeclarationNode> bundleDeclaration, uint64_t index,
      ScopeStack scope, ASTNode tree) {
    return synthesizeArrayDeclarationElement(bundleDeclaration, index, scope, tree);
  }

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
  getPortsForTypeBlock(std::shared_ptr<DeclarationNode> block, ScopeStack scope,
                       ASTNode tree) {
    return getPortsForTypeEntity(block, scope, tree);
  }

  static std::vector<ASTNode>
  getValidTypesForPort(std::shared_ptr<DeclarationNode> typeDeclaration,
                       std::string portName, ScopeStack scope, ASTNode tree);

  // Module Query
  static std::vector<ASTNode>
  getModuleEntities(std::shared_ptr<DeclarationNode> moduleDecl);
  static std::vector<ASTNode>
  getModuleBlocks(std::shared_ptr<DeclarationNode> moduleDecl) {
    return getModuleEntities(moduleDecl);
  }

  static std::vector<ASTNode>
  getStateMachineEntities(std::shared_ptr<DeclarationNode> stateDecl);
  static std::vector<ASTNode>
  getStateMachineBlocks(std::shared_ptr<DeclarationNode> stateDecl) {
    return getStateMachineEntities(stateDecl);
  }

  static std::shared_ptr<DeclarationNode>
  getModuleMainOutputPort(std::shared_ptr<DeclarationNode> moduleDecl);
  static std::shared_ptr<DeclarationNode>
  getModuleMainOutputPortBlock(std::shared_ptr<DeclarationNode> moduleDecl) {
    return getModuleMainOutputPort(moduleDecl);
  }

  /// Returns mainInputPort declaration
  static std::shared_ptr<DeclarationNode>
  getModuleMainInputPort(std::shared_ptr<DeclarationNode> moduleDecl);
  static std::shared_ptr<DeclarationNode>
  getModuleMainInputPortBlock(std::shared_ptr<DeclarationNode> moduleDecl) {
    return getModuleMainInputPort(moduleDecl);
  }

  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyOutputPorts(
      std::shared_ptr<DeclarationNode> moduleDecl);
  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyOutputPortBlocks(
      std::shared_ptr<DeclarationNode> moduleDecl) {
    return getModulePropertyOutputPorts(moduleDecl);
  }

  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyInputPorts(std::shared_ptr<DeclarationNode> moduleDecl);
  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyInputPortBlocks(std::shared_ptr<DeclarationNode> moduleDecl) {
    return getModulePropertyInputPorts(moduleDecl);
  }

  static std::shared_ptr<DeclarationNode>
  getModulePort(std::shared_ptr<DeclarationNode> moduleDecl, std::string name);

  static std::vector<std::string>
  getModulePortNames(std::shared_ptr<DeclarationNode> entityDeclaration);

  // Array / Bundle Declarations
  static ASTNode
  getMemberFromArrayConst(std::shared_ptr<DeclarationNode> arrayDecl,
                          int index, ASTNode tree, ScopeStack scopeStack,
                          std::vector<LangError> *errors);
  static ASTNode
  getMemberfromBlockBundleConst(std::shared_ptr<DeclarationNode> blockDecl,
                                int index, ASTNode tree, ScopeStack scopeStack,
                                std::vector<LangError> *errors) {
    return getMemberFromArrayConst(blockDecl, index, tree, scopeStack, errors);
  }

  // Const Declarations
  static ASTNode getValueFromConstEntity(DeclarationNode *entityDecl);
  static ASTNode getValueFromConstBlock(DeclarationNode *block) {
    return getValueFromConstEntity(block);
  }

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
  static bool isInputPortBlock(std::shared_ptr<DeclarationNode> blockDecl,
                               std::shared_ptr<DeclarationNode> funcDecl,
                               const ScopeStack &scope, ASTNode tree) {
    return isInputPort(blockDecl, funcDecl, scope, tree);
  }
  static bool isOutputPort(std::shared_ptr<DeclarationNode> entityDecl,
                           std::shared_ptr<DeclarationNode> funcDecl,
                           const ScopeStack &scope, ASTNode tree);
  static bool isOutputPortBlock(std::shared_ptr<DeclarationNode> blockDecl,
                                std::shared_ptr<DeclarationNode> funcDecl,
                                const ScopeStack &scope, ASTNode tree) {
    return isOutputPort(blockDecl, funcDecl, scope, tree);
  }
  static bool isPortEntity(std::shared_ptr<DeclarationNode> entityDecl,
                           std::shared_ptr<DeclarationNode> funcDecl,
                           const ScopeStack &scope, ASTNode tree);
  static bool isPortBlock(std::shared_ptr<DeclarationNode> blockDecl,
                          std::shared_ptr<DeclarationNode> funcDecl,
                          const ScopeStack &scope, ASTNode tree) {
    return isPortEntity(blockDecl, funcDecl, scope, tree);
  }
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
