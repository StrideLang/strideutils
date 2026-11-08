#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/astvalidation.h"

using namespace strd;

TEST(Schema, ObjectTypes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/05_objectTypes.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 2);
}

TEST(Schema, IntLiterals) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/01_intliterals.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 10);
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 66);
  EXPECT_EQ(errors[1].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[1].lineNumber, 67);
  EXPECT_EQ(errors[2].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[2].lineNumber, 68);
  EXPECT_EQ(errors[3].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[3].lineNumber, 72);
  EXPECT_EQ(errors[4].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[4].lineNumber, 73);
  EXPECT_EQ(errors[5].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[5].lineNumber, 74);
  EXPECT_EQ(errors[6].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[6].lineNumber, 78);
  EXPECT_EQ(errors[7].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[7].lineNumber, 79);
  EXPECT_EQ(errors[8].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[8].lineNumber, 80);
  EXPECT_EQ(errors[9].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[9].lineNumber, 81);
}

TEST(Schema, RealLiterals) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/02_realliterals.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 6);
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 56);
  EXPECT_EQ(errors[1].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[1].lineNumber, 57);
  EXPECT_EQ(errors[2].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[2].lineNumber, 58);
  EXPECT_EQ(errors[3].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[3].lineNumber, 62);
  EXPECT_EQ(errors[4].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[4].lineNumber, 63);
  EXPECT_EQ(errors[5].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[5].lineNumber, 64);
}

TEST(Schema, StringLiterals) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/03_stringliterals.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 4);
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 37);
  EXPECT_EQ(errors[1].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[1].lineNumber, 38);
  EXPECT_EQ(errors[2].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[2].lineNumber, 42);
  EXPECT_EQ(errors[3].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[3].lineNumber, 43);
}

TEST(Schema, ConstrainedList) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/04_constrainedList.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 4);
  EXPECT_EQ(errors[0].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[0].lineNumber, 63);
  EXPECT_EQ(errors[1].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[1].lineNumber, 67);
  EXPECT_EQ(errors[2].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[2].lineNumber, 71);
  EXPECT_EQ(errors[3].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[3].lineNumber, 76);
}

TEST(Schema, StrictValidation) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/06_strict_validation.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 13);
  // Fail1: scalarProp rejects list
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 107);
  // Fail2: scalarProp rejects unindexed array bundle
  EXPECT_EQ(errors[1].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[1].lineNumber, 112);
  // Fail3: scalarProp rejects array slice
  EXPECT_EQ(errors[2].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[2].lineNumber, 117);
  // Fail4: listProp rejects scalar literal
  EXPECT_EQ(errors[3].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[3].lineNumber, 122);
  // Fail5: listProp rejects single-index array element
  EXPECT_EQ(errors[4].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[4].lineNumber, 127);
  // Fail6: listProp rejects scalar entity
  EXPECT_EQ(errors[5].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[5].lineNumber, 132);
  // Fail7: listProp rejects slice violating maxItems (8 > 6)
  EXPECT_EQ(errors[6].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[6].lineNumber, 137);
  // Fail8: listProp rejects slice violating minItems (1 < 2)
  EXPECT_EQ(errors[7].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[7].lineNumber, 142);
  // Fail9: anyProp rejects list
  EXPECT_EQ(errors[8].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[8].lineNumber, 147);
  // Fail10: anyProp rejects array bundle
  EXPECT_EQ(errors[9].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[9].lineNumber, 152);
  // Fail11: anyListProp rejects scalar
  EXPECT_EQ(errors[10].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[10].lineNumber, 157);
  // Fail12: entityUnionProp rejects unlisted scalar literal
  EXPECT_EQ(errors[11].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[11].lineNumber, 162);
  // Fail13: entityUnionProp rejects collection/list
  EXPECT_EQ(errors[12].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[12].lineNumber, 167);
}

TEST(Schema, BlockValidation) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/07_block_validation.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 4);
  // Fail1: blockProp rejects list
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 57);
  // Fail2: blockProp rejects array slice
  EXPECT_EQ(errors[1].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[1].lineNumber, 62);
  // Fail3: blockProp rejects integer literal
  EXPECT_EQ(errors[2].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[2].lineNumber, 67);
  // Fail4: blockProp rejects module declaration
  EXPECT_EQ(errors[3].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[3].lineNumber, 72);
}

TEST(Schema, ArrayDefaultValidation) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "schema/08_array_default_validation.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 3);
  // Fail1: element type mismatch in array default list
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 44);
  // Fail2: array default list size mismatch (2 items != 4 declared)
  EXPECT_EQ(errors[1].type, LangError::ArraySizeMismatch);
  EXPECT_EQ(errors[1].lineNumber, 49);
  // Fail3: scalar switch rejects list default
  EXPECT_EQ(errors[2].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[2].lineNumber, 54);
}

TEST(Schema, GeneratorListValidation) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "schema/09_generator_list_validation.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 2);
  // Fail1: element type mismatch in generator list
  EXPECT_EQ(errors[0].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[0].lineNumber, 28);
  // Fail2: generator list size mismatch (2 items != 4 declared)
  EXPECT_EQ(errors[1].type, LangError::ArraySizeMismatch);
  EXPECT_EQ(errors[1].lineNumber, 33);
}
TEST(Schema, NumericConstraintsAndGuards) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree =
      AST::parseFile(TESTS_SOURCE_DIR "schema/10_numeric_constraints.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 9);
  for (const auto &err : errors) {
    EXPECT_EQ(err.type, LangError::ConstraintFail);
  }
  // Line checks for value constraint failures:
  EXPECT_EQ(errors[0].lineNumber, 76); // realMult: 0.6
  EXPECT_EQ(errors[1].lineNumber, 77); // intAllowed: 3
  EXPECT_EQ(errors[2].lineNumber, 78); // intEnum: 25
  EXPECT_EQ(errors[3].lineNumber, 79); // realAllowed: 2.0
  EXPECT_EQ(errors[4].lineNumber, 80); // realEnum: 0.4
  // Line checks for validator guard failures:
  EXPECT_EQ(errors[5].lineNumber, 91);  // multipleOf: 0
  EXPECT_EQ(errors[6].lineNumber, 106); // maximum: 5 (< 10)
  EXPECT_EQ(errors[7].lineNumber, 120); // multipleOf: -0.5
  EXPECT_EQ(errors[8].lineNumber, 135); // exclusiveMaximum: 5.0 (<= 5.0)
}
TEST(Schema, LiteralEnumsInAllowedTypes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/11_literal_enums.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 6);
  for (const auto &err : errors) {
    EXPECT_EQ(err.type, LangError::InvalidPortType);
  }
  EXPECT_EQ(errors[0].lineNumber, 64); // strEnum: "unknown"
  EXPECT_EQ(errors[1].lineNumber, 68); // intEnum: 3
  EXPECT_EQ(errors[2].lineNumber, 72); // realEnum: 1.0
  EXPECT_EQ(errors[3].lineNumber, 76); // switchEnum: on
  EXPECT_EQ(errors[4].lineNumber, 80); // mixedEnum: "fast"
  EXPECT_EQ(errors[5].lineNumber, 84); // openEnum: "other"
}
TEST(Schema, PublicAndWriteableFlags) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/12_public_writeable.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 9);
  // Error 1: declaration write to writeable: off property
  EXPECT_EQ(errors[0].type, LangError::ReadOnlyPort);
  EXPECT_EQ(errors[0].lineNumber, 79);

  // Error 2: declaration write to writeable: off property
  EXPECT_EQ(errors[1].type, LangError::ReadOnlyPort);
  EXPECT_EQ(errors[1].lineNumber, 84);

  // Error 3: direct member access read of private property
  EXPECT_EQ(errors[2].type, LangError::PrivatePort);
  EXPECT_EQ(errors[2].lineNumber, 88);

  // Error 4: direct member access read of private property
  EXPECT_EQ(errors[3].type, LangError::PrivatePort);
  EXPECT_EQ(errors[3].lineNumber, 91);

  // Error 5: member access in expression
  EXPECT_EQ(errors[4].type, LangError::PrivatePort);
  EXPECT_EQ(errors[4].lineNumber, 94);

  // Error 6: member access in list
  EXPECT_EQ(errors[5].type, LangError::PrivatePort);
  EXPECT_EQ(errors[5].lineNumber, 97);

  // Error 7: member access as property value in function call
  EXPECT_EQ(errors[6].type, LangError::PrivatePort);
  EXPECT_EQ(errors[6].lineNumber, 100);

  // Error 8: member access in expression within function call
  EXPECT_EQ(errors[7].type, LangError::PrivatePort);
  EXPECT_EQ(errors[7].lineNumber, 103);

  // Error 9: member access in list within function call
  EXPECT_EQ(errors[8].type, LangError::PrivatePort);
  EXPECT_EQ(errors[8].lineNumber, 106);
}
TEST(Schema, AdvancedCollectionConstraints) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "schema/13_advanced_collection_constraints.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 5);
  // Error 1: containsAtLeastOneA fails when 0 items match
  EXPECT_EQ(errors[0].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[0].lineNumber, 96);

  // Error 2: containsMinTwoA fails when only 1 item matches
  EXPECT_EQ(errors[1].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[1].lineNumber, 101);

  // Error 3: containsMaxTwoA fails when 3 items match
  EXPECT_EQ(errors[2].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[2].lineNumber, 106);

  // Error 4: uniqueByName fails with duplicate "alpha"
  EXPECT_EQ(errors[3].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[3].lineNumber, 111);

  // Error 5: uniqueByIndex fails with duplicate index 1
  EXPECT_EQ(errors[4].type, LangError::ConstraintFail);
  EXPECT_EQ(errors[4].lineNumber, 116);
}

TEST(Schema, FinalType) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "schema/14_final_type.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  std::vector<LangError> errors;
  ASTValidation::validateTypes(tree, errors, {}, tree);

  EXPECT_EQ(errors.size(), 4);

  // Error 1: forbiddenChild1 inherits from final type _FinalBase
  EXPECT_EQ(errors[0].type, LangError::InheritedFinalType);
  EXPECT_EQ(errors[0].lineNumber, 32);

  // Error 2: forbiddenChild2 inherits from final type _FinalBase in list
  EXPECT_EQ(errors[1].type, LangError::InheritedFinalType);
  EXPECT_EQ(errors[1].lineNumber, 37);

  // Error 3: 'final' property is invalid on a signal declaration
  EXPECT_EQ(errors[2].type, LangError::InvalidPort);
  EXPECT_EQ(errors[2].lineNumber, 41);

  // Error 4: 'final' expects a Switch literal, given Int
  EXPECT_EQ(errors[3].type, LangError::InvalidPortType);
  EXPECT_EQ(errors[3].lineNumber, 46);
}
