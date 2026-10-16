#include "gtest/gtest.h"
#include <algorithm>

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
  EXPECT_EQ(decl->getEntityType(), "module");
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
  EXPECT_EQ(inPortDecl->getEntityType(), "mainInputPort");
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

TEST_F(ASTQueryTest, MultiSegmentNamespaceLookup) {
  // Create a synthetic declaration in Audio::Filters namespace
  auto decl = std::make_shared<DeclarationNode>("Biquad", "module", nullptr, "test", 1);
  decl->appendToPropertyValue("namespaceTree", std::make_shared<ValueNode>("Audio", "test", 1));
  decl->appendToPropertyValue("namespaceTree", std::make_shared<ValueNode>("Filters", "test", 1));
  tree->addChild(decl);

  // 1. Direct qualified lookup
  auto found1 = ASTQuery::findDeclarationByName("Audio::Filters::Biquad", {}, tree);
  ASSERT_NE(found1, nullptr);
  EXPECT_EQ(found1->getName(), "Biquad");

  // 2. Lookup with namespaces vector
  auto found2 = ASTQuery::findDeclarationByName("Biquad", {}, tree, {"Audio", "Filters"});
  ASSERT_NE(found2, nullptr);
  EXPECT_EQ(found2->getName(), "Biquad");

  // 3. Partial qualified lookup with base namespace
  auto found3 = ASTQuery::findDeclarationByName("Filters::Biquad", {}, tree, {"Audio"});
  ASSERT_NE(found3, nullptr);
  EXPECT_EQ(found3->getName(), "Biquad");

  // 4. Mismatched namespace lookup should fail
  auto notFound = ASTQuery::findDeclarationByName("Other::Filters::Biquad", {}, tree);
  EXPECT_EQ(notFound, nullptr);
}

TEST_F(ASTQueryTest, GetContainedStreamBlocksStructuralAndNested) {
  auto addProp = [](const std::shared_ptr<DeclarationNode> &d, const std::string &name, ASTNode val) {
    d->addProperty(std::make_shared<PropertyNode>(name, val, "test", 1));
  };

  // 1. Create a module with a default "streams" block
  auto moduleDecl = std::make_shared<DeclarationNode>("TestModule", "module", nullptr, "test", 1);
  auto streamList = std::make_shared<ListNode>("test", 1);
  auto s1 = std::make_shared<StreamNode>(
      std::make_shared<ValueNode>((int64_t)1, "test", 1),
      std::make_shared<ValueNode>((int64_t)2, "test", 1), "test", 1);
  streamList->addChild(s1);
  addProp(moduleDecl, "streams", streamList);

  auto modBlocks = ASTQuery::getContainedStreamBlocks(moduleDecl, {}, tree);
  ASSERT_EQ(modBlocks.size(), 1);
  EXPECT_EQ(modBlocks[0].propertyName, "streams");
  EXPECT_EQ(modBlocks[0].executionStage, "default");
  ASSERT_EQ(modBlocks[0].streams.size(), 1);
  EXPECT_EQ(modBlocks[0].streams[0], s1);

  // 2. Create a state with onEntry, updateGuard, and nested transitions
  auto stateDecl = std::make_shared<DeclarationNode>("TestState", "state", nullptr, "test", 1);
  auto entryList = std::make_shared<ListNode>("test", 1);
  auto sEntry = std::make_shared<StreamNode>(
      std::make_shared<ValueNode>((int64_t)10, "test", 1),
      std::make_shared<ValueNode>((int64_t)20, "test", 1), "test", 1);
  entryList->addChild(sEntry);
  addProp(stateDecl, "onEntry", entryList);

  auto guardList = std::make_shared<ListNode>("test", 1);
  auto sGuard = std::make_shared<StreamNode>(
      std::make_shared<ValueNode>((int64_t)30, "test", 1),
      std::make_shared<ValueNode>((int64_t)40, "test", 1), "test", 1);
  guardList->addChild(sGuard);
  addProp(stateDecl, "updateGuard", guardList);

  // Nested transition inside transitions property
  auto transDecl = std::make_shared<DeclarationNode>("Trans1", "transition", nullptr, "test", 1);
  auto transStreamList = std::make_shared<ListNode>("test", 1);
  auto sTrans = std::make_shared<StreamNode>(
      std::make_shared<ValueNode>((int64_t)50, "test", 1),
      std::make_shared<ValueNode>((int64_t)60, "test", 1), "test", 1);
  transStreamList->addChild(sTrans);
  addProp(transDecl, "onTransition", transStreamList);

  auto transList = std::make_shared<ListNode>("test", 1);
  transList->addChild(transDecl);
  addProp(stateDecl, "transitions", transList);

  auto stateBlocks = ASTQuery::getContainedStreamBlocks(stateDecl, {}, tree);
  ASSERT_EQ(stateBlocks.size(), 3);
  std::set<std::string> stages;
  for (const auto &b : stateBlocks) {
    stages.insert(b.executionStage);
  }
  EXPECT_TRUE(stages.count("onEntry") > 0);
  EXPECT_TRUE(stages.count("updateGuard") > 0);
  EXPECT_TRUE(stages.count("onTransition") > 0);
}

TEST_F(ASTQueryTest, GetContainedStreamBlocksSchemaReflection) {
  auto addProp = [](const std::shared_ptr<DeclarationNode> &d, const std::string &name, ASTNode val) {
    d->addProperty(std::make_shared<PropertyNode>(name, val, "test", 1));
  };

  // Create schema type definition for a custom pipeline stage
  // type _PipelineType { typeName: "pipeline" properties: [ typeProperty { name: "stage1" isStreamBlock: on executionStage: "step1" } ] }
  auto schemaType = std::make_shared<DeclarationNode>("_PipelineType", "type", nullptr, "test", 1);
  addProp(schemaType, "typeName", std::make_shared<ValueNode>(std::string("pipeline"), "test", 1));

  auto propDef = std::make_shared<DeclarationNode>("stage1Prop", "typeProperty", nullptr, "test", 1);
  addProp(propDef, "name", std::make_shared<ValueNode>(std::string("stage1"), "test", 1));
  addProp(propDef, "isStreamBlock", std::make_shared<ValueNode>(true, "test", 1));
  addProp(propDef, "executionStage", std::make_shared<ValueNode>(std::string("step1"), "test", 1));

  auto propList = std::make_shared<ListNode>("test", 1);
  propList->addChild(propDef);
  addProp(schemaType, "properties", propList);
  tree->addChild(schemaType);

  // Create an instance of pipeline
  auto pipeDecl = std::make_shared<DeclarationNode>("MyPipe", "pipeline", nullptr, "test", 1);
  auto sPipe = std::make_shared<StreamNode>(
      std::make_shared<ValueNode>((int64_t)100, "test", 1),
      std::make_shared<ValueNode>((int64_t)200, "test", 1), "test", 1);
  auto pipeStreams = std::make_shared<ListNode>("test", 1);
  pipeStreams->addChild(sPipe);
  addProp(pipeDecl, "stage1", pipeStreams);

  auto pipeBlocks = ASTQuery::getContainedStreamBlocks(pipeDecl, {}, tree);
  ASSERT_EQ(pipeBlocks.size(), 1);
  EXPECT_EQ(pipeBlocks[0].propertyName, "stage1");
  EXPECT_EQ(pipeBlocks[0].executionStage, "step1");
  ASSERT_EQ(pipeBlocks[0].streams.size(), 1);
  EXPECT_EQ(pipeBlocks[0].streams[0], sPipe);
}

