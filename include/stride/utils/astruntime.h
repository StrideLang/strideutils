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

#ifndef ASTRUNTIME_H
#define ASTRUNTIME_H

#include "stride/parser/functionnode.h"
#include "stride/parser/valuenode.h"

namespace strd {
struct StrideRuntimeStatus {
  bool ok;
  LangError err;
};

class ASTRuntime {
public:
  ASTRuntime();

  // Operators
  static std::shared_ptr<ValueNode> multiply(std::shared_ptr<ValueNode> left,
                                             std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode> divide(std::shared_ptr<ValueNode> left,
                                           std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode> add(std::shared_ptr<ValueNode> left,
                                        std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode> subtract(std::shared_ptr<ValueNode> left,
                                             std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode>
  unaryMinus(std::shared_ptr<ValueNode> value);
  static std::shared_ptr<ValueNode>
  logicalAnd(std::shared_ptr<ValueNode> left, std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode> logicalOr(std::shared_ptr<ValueNode> left,
                                              std::shared_ptr<ValueNode> right);
  static std::shared_ptr<ValueNode> logicalNot(std::shared_ptr<ValueNode> left);

  static void resolveFunction(std::shared_ptr<FunctionNode> constraintFunction,
                              std::vector<ASTNode> input,
                              std::vector<ASTNode> output,
                              StrideRuntimeStatus &status);

private:
  // Node functions
  // TODO we should have type specific versions of these for speed, so we do the
  // check on parsing but the runtime does not need to check types
  // This will also do away with the need for status
  static void nodesAreEqual(ASTNode &node1, ASTNode &node2, ASTNode &output,
                            StrideRuntimeStatus &status);

  static void nodesAreNotEqual(ASTNode &node1, ASTNode &node2, ASTNode &output,
                               StrideRuntimeStatus &status);

  static void nodesIsGreater(ASTNode &node1, ASTNode &node2, ASTNode &output,
                             StrideRuntimeStatus &status);

  static void nodesIsNotGreater(ASTNode &node1, ASTNode &node2, ASTNode &output,
                                StrideRuntimeStatus &status);

  static void nodesIsLesser(ASTNode &node1, ASTNode &node2, ASTNode &output,
                            StrideRuntimeStatus &status);

  static void nodesIsNotLesser(ASTNode &node1, ASTNode &node2, ASTNode &output,
                               StrideRuntimeStatus &status);

  static void nodesOr(ASTNode &node1, ASTNode &node2, ASTNode &output,
                      StrideRuntimeStatus &status);

  static void nodesAnd(ASTNode &node1, ASTNode &node2, ASTNode &output,
                       StrideRuntimeStatus &status);

  static void nodeNot(ASTNode &node1, ASTNode &output,
                      StrideRuntimeStatus &status);

  static void nodeIsNone(ASTNode &node1, ASTNode &output,
                         StrideRuntimeStatus &status);
};
} // namespace strd

#endif // ASTRUNTIME_H
