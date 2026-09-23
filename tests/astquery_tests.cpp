#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

class ASTQueryTest : public ::testing::Test {
protected:
  void SetUp() override {
    tree = AST::parseFile(TESTS_SOURCE_DIR "query/basic.stride");
    ASSERT_TRUE(tree != nullptr);
    ASTFunctions::preprocess(tree);
  }

  ASTNode tree;
};

TEST_F(ASTQueryTest, FindDeclarationByName) {
  auto decl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(decl, nullptr);
  EXPECT_EQ(decl->getNodeType(), AST::Declaration);
  EXPECT_EQ(decl->getName(), "QueryTest");
  EXPECT_EQ(decl->getObjectType(), "module");
}

TEST_F(ASTQueryTest, FindAllDeclarations) {
  auto queryTestDecl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(queryTestDecl, nullptr);
  ScopeStack scope = {
      {queryTestDecl,
       queryTestDecl->getPropertyValue("blocks")->getChildren()}};

  auto decls = ASTQuery::findAllDeclarations("MyConstant", scope, tree);
  ASSERT_EQ(decls.size(), 1);
  EXPECT_EQ(decls[0]->getName(), "MyConstant");
}

TEST_F(ASTQueryTest, GetModulePorts) {
  auto queryTestDecl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(queryTestDecl, nullptr);

  auto portNames = ASTQuery::getModulePortNames(queryTestDecl);
  ASSERT_EQ(portNames.size(), 2);
  EXPECT_TRUE(std::find(portNames.begin(), portNames.end(), "input") !=
              portNames.end());
  EXPECT_TRUE(std::find(portNames.begin(), portNames.end(), "output") !=
              portNames.end());

  auto inPortDecl = ASTQuery::getModulePort(queryTestDecl, "InPort");
  ASSERT_NE(inPortDecl, nullptr);
  EXPECT_EQ(inPortDecl->getObjectType(), "mainInputPort");
}

TEST_F(ASTQueryTest, FindTypeDeclarationByName) {
  // Tests behavior when type is not found (since we removed MyType)
  auto queryTestDecl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(queryTestDecl, nullptr);
  ScopeStack scope = {{queryTestDecl, {}}};

  auto typeDecl =
      ASTQuery::findTypeDeclarationByName("NonExistentType", scope, tree);
  EXPECT_EQ(typeDecl, nullptr);
}

TEST_F(ASTQueryTest, GetInheritedTypes) {
  // We can just test that an empty result is returned for a standard constant
  auto queryTestDecl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(queryTestDecl, nullptr);
  ScopeStack scope = {
      {queryTestDecl,
       queryTestDecl->getPropertyValue("blocks")->getChildren()}};

  auto constantDecl =
      ASTQuery::findDeclarationByName("MyConstant", scope, tree);
  ASSERT_NE(constantDecl, nullptr);

  auto inherited = ASTQuery::getInheritedTypes(constantDecl, scope, tree);
  EXPECT_EQ(inherited.size(), 0);
}

TEST_F(ASTQueryTest, IsConstant) {
  auto queryTestDecl = ASTQuery::findDeclarationByName("QueryTest", {}, tree);
  ASSERT_NE(queryTestDecl, nullptr);
  ScopeStack scope = {
      {queryTestDecl,
       queryTestDecl->getPropertyValue("blocks")->getChildren()}};

  auto constantDecl =
      ASTQuery::findDeclarationByName("MyConstant", scope, tree);
  ASSERT_NE(constantDecl, nullptr);

  EXPECT_TRUE(ASTQuery::isConstant(constantDecl, scope, tree));

  auto signalDecl = ASTQuery::findDeclarationByName("InBlock", scope, tree);
  ASSERT_NE(signalDecl, nullptr);

  EXPECT_FALSE(ASTQuery::isConstant(signalDecl, scope, tree));
}
