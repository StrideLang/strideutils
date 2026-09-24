#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

TEST(ASTQuery, StateMachineNodes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "schema/statemachine_nodes.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);
  
  ScopeStack emptyScope;
  
  // Find basic state
  auto stateBlock = ASTQuery::findDeclarationByName("MyState", emptyScope, tree);
  EXPECT_TRUE(stateBlock != nullptr);
  EXPECT_TRUE(ASTQuery::isStateNode(stateBlock, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isTransitionNode(stateBlock, emptyScope, tree));
  
  // Find basic transition
  auto transBlock = ASTQuery::findDeclarationByName("MyTransition", emptyScope, tree);
  EXPECT_TRUE(transBlock != nullptr);
  EXPECT_TRUE(ASTQuery::isTransitionNode(transBlock, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isStateNode(transBlock, emptyScope, tree));
  
  // Find extended state
  auto extState = ASTQuery::findDeclarationByName("MyExtendedState", emptyScope, tree);
  EXPECT_TRUE(extState != nullptr);
  EXPECT_TRUE(ASTQuery::isStateNode(extState, emptyScope, tree));
  
  // Find unrelated block
  auto unrelated = ASTQuery::findDeclarationByName("JustABlock", emptyScope, tree);
  EXPECT_TRUE(unrelated != nullptr);
  EXPECT_FALSE(ASTQuery::isStateNode(unrelated, emptyScope, tree));
  EXPECT_FALSE(ASTQuery::isTransitionNode(unrelated, emptyScope, tree));
}
