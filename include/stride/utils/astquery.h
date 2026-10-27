/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

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

struct StreamBlockDescriptor {
  std::string propertyName;
  std::vector<std::shared_ptr<StreamNode>> streams;
  std::string executionStage; // e.g. "default", "onEntry", "updateGuard"
};

class ASTQuery {
public:
  // Stream discovery
  static std::vector<StreamBlockDescriptor>
  getContainedStreamBlocks(std::shared_ptr<DeclarationNode> decl,
                           const ScopeStack &scope, ASTNode tree);

  static std::vector<std::shared_ptr<SystemNode>> getSystemNodes(ASTNode tree);

  static std::vector<std::shared_ptr<ImportNode>> getImportNodes(ASTNode tree);

  static std::vector<std::shared_ptr<PackageNode>>
  getPackageNodes(ASTNode tree);

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
  findTypeDeclaration(std::shared_ptr<DeclarationNode> entityDecl,
                      ScopeStack scope, ASTNode tree,
                      std::string currentFramework = "");

  static std::shared_ptr<DeclarationNode>
  findTypeDeclarationByName(std::string typeName, ScopeStack scope,
                            ASTNode tree,
                            std::vector<std::string> namespaces = {},
                            std::string currentFramework = "");

  // Finds a typeClass declaration in the root of tree. Prefers the one from
  // currentFramework, as each framework provides its own type classes.
  static std::shared_ptr<DeclarationNode>
  findTypeClass(std::string name, ASTNode tree,
                std::string currentFramework = "");

  // Names of the typeClasses listed in the allowedTypes of the type property
  // of decl's type. Empty for declarations with a builtin data type.
  static std::vector<std::string>
  getDataTypeClassNames(std::shared_ptr<DeclarationNode> decl, ASTNode tree);

  static bool isTypeClassMember(std::shared_ptr<DeclarationNode> typeClass,
                                std::string typeName);

  // Rated declarations inherit _Rated. Others are reactive and take the rate
  // of whatever writes to them.
  static bool isRated(std::shared_ptr<DeclarationNode> decl, ASTNode tree);

  // Get name for node. Names are found in different places for different types
  // of nodes
  static std::string getNodeName(ASTNode node);

  static bool isGenerator(std::shared_ptr<DeclarationNode> decl);

  ///// Size queries
  /// Get the number of parallel nodes implicit in node. i.e. into how many
  /// parallel streams can the node be broken up.
  static int getNodeSize(ASTNode node, const ScopeStack &scopeStack,
                         ASTNode tree);

  static int getArraySize(std::shared_ptr<ArrayNode> arrayNode,
                          ScopeStack scope, ASTNode tree,
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
  getInheritedPorts(std::shared_ptr<DeclarationNode> entityDecl,
                    ScopeStack scope, ASTNode tree);

  // Type information
  static std::vector<ASTNode>
  getPortsForType(std::string typeName, ScopeStack scope, ASTNode tree,
                  std::vector<std::string> namespaces = {},
                  std::string framework = "");

  static std::vector<ASTNode>
  getPortsForTypeEntity(std::shared_ptr<DeclarationNode> entityDecl,
                        ScopeStack scope, ASTNode tree);

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
  getModulePropertyOutputPorts(std::shared_ptr<DeclarationNode> moduleDecl);

  static std::vector<std::shared_ptr<DeclarationNode>>
  getModulePropertyInputPorts(std::shared_ptr<DeclarationNode> moduleDecl);

  static std::shared_ptr<DeclarationNode>
  getModulePort(std::shared_ptr<DeclarationNode> moduleDecl, std::string name);

  static std::vector<std::string>
  getModulePortNames(std::shared_ptr<DeclarationNode> entityDeclaration,
                     const ScopeStack &scope = {}, ASTNode tree = nullptr);

  // Array Declarations
  static ASTNode
  getMemberFromArrayConst(std::shared_ptr<DeclarationNode> arrayDecl, int index,
                          ASTNode tree, ScopeStack scopeStack,
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

  // Source location and scope queries
  static ASTNode findNodeAt(ASTNode tree, int line, int column);
  static ScopeStack getEnclosingScopeAt(ASTNode tree, int line, int column);

  // Property and schema queries
  static std::shared_ptr<DeclarationNode>
  findPropertyDeclaration(std::shared_ptr<DeclarationNode> entityOrTypeDecl,
                          const std::string &propertyName,
                          const ScopeStack &scope, ASTNode tree);

  static std::vector<std::shared_ptr<DeclarationNode>>
  getAllAllowedProperties(std::shared_ptr<DeclarationNode> entityOrTypeDecl,
                          const ScopeStack &scope, ASTNode tree);

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
  static bool isModuleNode(ASTNode node, const ScopeStack &scope, ASTNode tree);

private:
  static bool namespaceMatch(std::vector<std::string> scopeList,
                             std::shared_ptr<DeclarationNode> decl,
                             std::string currentFramework = "");
};

// Data types are the frameworkDataType declarations users write (Int, Real,
// UInt8, Float32, ...). Each framework declares them in its platformlib.
// Schema validation types (_NumericTypes, _IntLiteral, ...) start with '_' and
// are never data types.
namespace DataTypes {
// Names the compiler uses for literals and builtin declarations. Int, UInt,
// Real and Size are aliases whose size each framework decides.
extern const std::string Real;
extern const std::string Int;
extern const std::string UInt;
extern const std::string Size;
extern const std::string Switch;
extern const std::string String;

enum class Kind { Unknown, Integer, Float, Switch, String };

struct Info {
  std::string name;      // As written, e.g. "Int"
  std::string canonical; // After following aliases, e.g. "Int32"
  Kind kind{Kind::Unknown};
  int bits{0};
  bool isSigned{true};

  bool valid() const { return kind != Kind::Unknown; }
  bool isNumeric() const { return kind == Kind::Integer || kind == Kind::Float; }
  bool isInteger() const { return kind == Kind::Integer; }
  bool isFloat() const { return kind == Kind::Float; }
};

// A data type name is not empty and does not start with '_'.
bool isDataTypeName(const std::string &name);

// The fixed types and the default sizes of the aliases (Int is Int32, UInt is
// UInt32, Real is Float64 and Size is UInt64). Used when no frameworkDataType
// declaration is available.
Info builtin(const std::string &name);

// Finds the frameworkDataType or alias declaration in the root of tree.
// Prefers the one from currentFramework over the one in the library.
std::shared_ptr<DeclarationNode>
findDeclaration(const std::string &name, ASTNode tree,
                const std::string &currentFramework = "");

// Describes name from its declaration in tree. Aliases (alias Int { block:
// Int32 }) are followed to any depth. Falls back to builtin() for types
// without a declaration. Not valid() for an alias cycle.
Info resolve(const std::string &name, ASTNode tree = nullptr,
             const std::string &currentFramework = "");

// Integer types are named by size and sign: Int8..Int64 are signed and
// UInt8..UInt64 unsigned. Float types are Float32 and Float64. Returns the
// expected name for an integer or float, e.g. "UInt16".
std::string fixedTypeName(Kind kind, int bits, bool isSigned);

// The type of an operation that mixes a and b. Float wins over integer, then
// for floats the wider type wins. Integers of the same sign take the wider
// type. Mixing signed and unsigned gives a signed type that holds both: the
// signed type if it is wider, otherwise a signed type twice as wide as the
// unsigned one (up to Int64). Empty if a and b can't be mixed.
std::string promote(const std::string &a, const std::string &b,
                    ASTNode tree = nullptr);

// Why converting a value of type from to type to can lose data or change
// it, e.g. "truncates the fractional part". Empty when every value of from
// is kept.
std::string conversionRisk(const std::string &from, const std::string &to,
                           ASTNode tree = nullptr);

// Why converting the operands of an operation mixing a and b to their
// promoted type can lose data, e.g. UInt64 and Int8 give Int64, which can't
// hold the largest UInt64 values. Empty when it can't.
std::string promotionRisk(const std::string &a, const std::string &b,
                          ASTNode tree = nullptr);

// Whether a value of type from can be used where to is expected without
// losing range: integer to float, and integer or float widening.
bool implicitlyConvertible(const std::string &from, const std::string &to,
                           ASTNode tree = nullptr);
} // namespace DataTypes
} // namespace strd

#endif // ASTQUERY_H
