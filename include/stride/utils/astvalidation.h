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

#ifndef ASTVALIDATION_H
#define ASTVALIDATION_H

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"

#include <functional>
#include <set>

namespace strd {
class ASTValidation {
public:
  ASTValidation();

  static std::vector<LangError> validate(ASTNode tree);
  // Verifies that types are declared for all nodes, and that the properties and
  // property types are allowed by the type declaration
  static void validateTypes(ASTNode node, std::vector<LangError> &errors,
                            ScopeStack scopeStack = ScopeStack(),
                            ASTNode tree = nullptr,
                            std::vector<std::string> parentNamespace = {},
                            std::string currentFramework = "",
                            std::set<const void *> *processedNodes = nullptr);

  // Validate property types
  static bool isValidStringProperty(std::shared_ptr<DeclarationNode> decl,
                                    std::string propertyName,
                                    bool optional = false, bool verbose = true);
  static bool isValidSwitchProperty(std::shared_ptr<DeclarationNode> decl,
                                    std::string propertyName,
                                    bool optional = false, bool verbose = true);
  static bool isValidIntProperty(std::shared_ptr<DeclarationNode> decl,
                                 std::string propertyName,
                                 bool optional = false, bool verbose = true);
  static bool isValidRealProperty(std::shared_ptr<DeclarationNode> decl,
                                  std::string propertyName,
                                  bool optional = false, bool verbose = true);
  static bool isValidNumberProperty(std::shared_ptr<DeclarationNode> decl,
                                    std::string propertyName,
                                    bool optional = false, bool verbose = true);
  static bool isValidEntityProperty(std::shared_ptr<DeclarationNode> decl,
                                    std::string propertyName,
                                    bool optional = false, bool verbose = true);
  static bool isValidNoneProperty(std::shared_ptr<DeclarationNode> decl,
                                  std::string propertyName,
                                  bool optional = false, bool verbose = true);
  static bool isValidDeclarationProperty(std::shared_ptr<DeclarationNode> decl,
                                         std::string propertyName,
                                         bool optional = false,
                                         bool verbose = true);

  static bool isValidOr(std::function<bool(std::shared_ptr<DeclarationNode>,
                                           std::string, bool, bool)>
                            func1,
                        std::function<bool(std::shared_ptr<DeclarationNode>,
                                           std::string, bool, bool)>
                            func2,
                        std::shared_ptr<DeclarationNode> decl,
                        std::string propertyName, bool optional = false,
                        bool verbose = true);

  static bool isValidListProperty(
      std::shared_ptr<DeclarationNode> decl, std::string propertyName,
      std::function<bool(ASTNode)> validateElement =
          [](ASTNode) { return true; },
      bool optional = false, bool verbose = true);

protected:
  static void validateTypesForDeclaration(
      std::shared_ptr<DeclarationNode> decl, ScopeStack scopeStack,
      std::vector<LangError> &errors, ASTNode tree,
      std::string currentFramework,
      std::set<const void *> *processedNodes = nullptr);

  static void
  validateConstrainedInt(std::shared_ptr<DeclarationNode> constrainDecl,
                         std::shared_ptr<DeclarationNode> declaration,
                         std::string portName, ASTNode portValue,
                         std::vector<LangError> &errors);
  static void
  validateConstrainedReal(std::shared_ptr<DeclarationNode> constrainDecl,
                          std::shared_ptr<DeclarationNode> declaration,
                          std::string portName, ASTNode portValue,
                          std::vector<LangError> &errors);
  static void
  validateConstrainedString(std::shared_ptr<DeclarationNode> constrainDecl,
                            std::shared_ptr<DeclarationNode> declaration,
                            std::string portName, ASTNode portValue,
                            std::vector<LangError> &errors);
  static void validateConstrainedList(
      std::shared_ptr<DeclarationNode> constrainDecl,
      std::shared_ptr<DeclarationNode> declaration, std::string portName,
      ASTNode portValue, std::vector<LangError> &errors,
      ScopeStack scopeStack = ScopeStack(), ASTNode tree = nullptr,
      std::vector<std::string> parentNamespace = {},
      std::string currentFramework = "");
  static std::string getTypeName(ASTNode child,
                                 ScopeStack scopeStack = ScopeStack(),
                                 ASTNode tree = nullptr,
                                 std::vector<std::string> parentNamespace = {},
                                 std::string currentFramework = "");

  static std::vector<std::string> getValidTypeNames(
      std::vector<ASTNode> portTypesList, ScopeStack scopeStack = ScopeStack(),
      ASTNode tree = nullptr, std::vector<std::string> parentNamespace = {},
      std::string currentFramework = "");
};
} // namespace strd

#endif // ASTVALIDATION_H
