#include "gtest/gtest.h"

#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/stridelibrary.h"

using namespace strd;

TEST(Library, ImportRaw) {
  StrideLibrary library;

  // Add current path to import directories
  library.initializeLibrary("", {TESTS_SOURCE_DIR "library"});
  auto nodes = library.loadImport("Include", "Test");

  EXPECT_EQ(nodes.size(), 1);
  EXPECT_EQ(nodes[0]->getNodeType(), AST::Declaration);
  auto decl = std::static_pointer_cast<DeclarationNode>(nodes[0]);
  EXPECT_EQ(decl->getObjectType(), "module");
  EXPECT_EQ(decl->getName(), "LibTest");

  auto members = library.getLibraryMembers();

  EXPECT_EQ(members.size(), 1);
  EXPECT_TRUE(members.find("Test") != members.end());
  EXPECT_EQ(members["Test"][0], nodes[0]);
}

TEST(Library, Import) {

  auto tree = AST::parseFile(TESTS_SOURCE_DIR "library/01_import.stride");

  ASTFunctions::preprocess(tree);

  auto decl = ASTQuery::findDeclarationByName("LibTest", {}, tree);

  EXPECT_NE(decl, nullptr);
  EXPECT_EQ(decl->getNodeType(), AST::Declaration);
}

TEST(Library, PackageRootBarrelFileAndCapitalization) {
  StrideLibrary library;
  library.initializeLibrary("", {TESTS_SOURCE_DIR "library"});

  // "PackageRoot" has both PackageRoot.stride (file) and PackageRoot/ (dir with Biquad.stride, SubPkg/, ignored_lower.stride)
  auto nodes = library.loadImport("PackageRoot", "");

  // Should load RootModule (from PackageRoot.stride), BiquadModule (from PackageRoot/Biquad.stride),
  // and SubModuleInPkg (from PackageRoot/SubPkg/SubModule.stride).
  // LowerModule (from ignored_lower.stride) MUST be ignored due to lowercase stem.
  std::vector<std::string> moduleNames;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      moduleNames.push_back(decl->getName());
    }
  }

  EXPECT_TRUE(std::find(moduleNames.begin(), moduleNames.end(), "RootModule") != moduleNames.end());
  EXPECT_TRUE(std::find(moduleNames.begin(), moduleNames.end(), "BiquadModule") != moduleNames.end());
  EXPECT_TRUE(std::find(moduleNames.begin(), moduleNames.end(), "SubModuleInPkg") != moduleNames.end());
  EXPECT_TRUE(std::find(moduleNames.begin(), moduleNames.end(), "LowerModule") == moduleNames.end());
}

TEST(Library, MultiSegmentQualifiedImport) {
  StrideLibrary library;
  library.initializeLibrary("", {TESTS_SOURCE_DIR "library"});

  auto nodes = library.loadImport("PackageRoot::SubPkg", "Sub");

  ASSERT_GE(nodes.size(), 1);
  auto decl = std::static_pointer_cast<DeclarationNode>(nodes[0]);
  EXPECT_EQ(decl->getName(), "SubModuleInPkg");

  auto members = library.getLibraryMembers();
  EXPECT_TRUE(members.find("Sub") != members.end());
  EXPECT_EQ(members["Sub"][0], nodes[0]);
}
