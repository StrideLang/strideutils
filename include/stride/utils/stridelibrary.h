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

#ifndef STRIDELIBRARY_HPP
#define STRIDELIBRARY_HPP

#include <map>
#include <vector>

#include "stride/parser/declarationnode.h"

namespace strd {
struct LibraryTree {
  std::string importName;
  std::string importAs;
  std::vector<ASTNode> nodes;
  std::vector<std::string> namespaces;
};

class StrideLibrary {
public:
  StrideLibrary();
  ~StrideLibrary();

  void initializeLibrary(std::string strideRootPath,
                         std::vector<std::string> includePaths = {});

  std::shared_ptr<DeclarationNode> findTypeInLibrary(std::string typeName);

  bool isValidEntity(DeclarationNode *entity);

  std::map<std::string, std::vector<ASTNode>> getLibraryMembers();

  std::vector<ASTNode> loadImport(std::string importName, std::string importAs);

private:
  bool isValidProperty(std::shared_ptr<PropertyNode> property,
                       DeclarationNode *type);
  std::vector<DeclarationNode *> getParentTypes(DeclarationNode *type);

  void readLibrary(std::string rootDir);

  std::vector<LibraryTree>
      m_libraryTrees; // List of root and imported library trees

  std::string m_libraryPath;

  int m_majorVersion;
  int m_minorVersion;
  std::vector<std::string> m_includePaths;
};
} // namespace strd

#endif // STRIDELIBRARY_HPP
