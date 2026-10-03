#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

TEST(ASTQuery, StateMachineNodes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "schema/statemachine_nodes.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  ScopeStack emptyScope;

  // Find basic state
  auto stateBlock =
      ASTQuery::findDeclarationByName("MyState", emptyScope, tree);
  EXPECT_TRUE(stateBlock != nullptr);
  EXPECT_TRUE(ASTQuery::isStateNode(stateBlock, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isTransitionNode(stateBlock, emptyScope, tree));

  // Find basic transition
  auto transBlock =
      ASTQuery::findDeclarationByName("MyTransition", emptyScope, tree);
  EXPECT_TRUE(transBlock != nullptr);
  EXPECT_TRUE(ASTQuery::isTransitionNode(transBlock, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isStateNode(transBlock, emptyScope, tree));

  // Find extended state
  auto extState =
      ASTQuery::findDeclarationByName("MyExtendedState", emptyScope, tree);
  EXPECT_TRUE(extState != nullptr);
  EXPECT_TRUE(ASTQuery::isStateNode(extState, emptyScope, tree));

  // Find unrelated block
  auto unrelated =
      ASTQuery::findDeclarationByName("JustABlock", emptyScope, tree);
  EXPECT_TRUE(unrelated != nullptr);
  EXPECT_FALSE(ASTQuery::isStateNode(unrelated, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isTransitionNode(unrelated, emptyScope, tree));
}

TEST(ASTQuery, StateMachineScope) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "schema/statemachine_nodes.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  ScopeStack scope;

  auto parentState =
      ASTQuery::findDeclarationByName("ParentState", scope, tree);
  EXPECT_TRUE(parentState != nullptr);

  auto nestedState =
      ASTQuery::findDeclarationByName("NestedState", scope, tree);
  EXPECT_TRUE(nestedState != nullptr);

  // Push ParentState and its state machine blocks (states, transitions)
  scope.push_back({parentState, ASTQuery::getStateMachineBlocks(parentState)});

  // Push NestedState and its state machine blocks (blocks)
  scope.push_back({nestedState, ASTQuery::getStateMachineBlocks(nestedState)});

  // Should find signal MySignal in the scope
  auto signal = ASTQuery::findDeclarationByName("MySignal", scope, tree);
  EXPECT_TRUE(signal != nullptr);
  EXPECT_EQ(signal->getObjectType(), "signal");

  // Should find transition GoToNested in the scope
  auto transition = ASTQuery::findDeclarationByName("GoToNested", scope, tree);
  EXPECT_TRUE(transition != nullptr);
  EXPECT_EQ(transition->getObjectType(), "transition");
}
