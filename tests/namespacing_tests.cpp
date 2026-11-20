// Name resolution across packages: calls are rewritten to the canonical
// package path of the declaration they name while imported declarations are
// inserted (ASTFunctions::canonicalizeQualifier). See doc/NamespacingPlan.md.

#include "gtest/gtest.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>

#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/cachemanager.h"
#include "stride/utils/stridelibrary.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

namespace {

// Calls in `node`, depth first.
std::vector<std::shared_ptr<FunctionNode>> callsIn(const ASTNode &node) {
  std::vector<std::shared_ptr<FunctionNode>> calls;
  std::function<void(const ASTNode &)> walk = [&](const ASTNode &n) {
    if (!n) {
      return;
    }
    if (n->getNodeType() == AST::Function) {
      calls.push_back(std::static_pointer_cast<FunctionNode>(n));
    }
    if (n->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(n);
      walk(stream->getLeft());
      walk(stream->getRight());
      return;
    }
    for (const auto &child : n->getChildren()) {
      walk(child);
    }
  };
  walk(node);
  return calls;
}

// Calls in the root file's own streams (not in inserted declarations).
std::vector<std::shared_ptr<FunctionNode>> rootCalls(const ASTNode &tree) {
  std::vector<std::shared_ptr<FunctionNode>> calls;
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() == AST::Stream) {
      auto found = callsIn(child);
      calls.insert(calls.end(), found.begin(), found.end());
    }
  }
  return calls;
}

ASTNode preprocessed(const char *file) {
  auto tree = AST::parseFile(file);
  if (tree) {
    ASTFunctions::preprocess(tree, nullptr, {},
                             ASTFunctions::getDefaultStrideRoot());
  }
  return tree;
}

const std::vector<std::string> kPkgInner = {"Pkg", "Inner"};
const std::vector<std::string> kPkgGains = {"Pkg", "Gains"};

} // namespace

TEST(Namespacing, AliasedCallResolvesToPackage) {
  // import Pkg::Inner as I;  In >> I::Quad() >> Out;
  auto tree = preprocessed(TESTS_SOURCE_DIR "namespacing/aliased.stride");
  ASSERT_TRUE(tree);

  auto calls = rootCalls(tree);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0]->getName(), "Quad");
  EXPECT_EQ(calls[0]->getNamespaceList(), kPkgInner);

  auto quad = ASTQuery::findDeclarationByName("Quad", {}, tree, kPkgInner);
  ASSERT_TRUE(quad) << "Pkg::Quad was not inserted";
  // Quad calls Twice unqualified: its own package comes first.
  auto inner = callsIn(quad->getPropertyValue("streams"));
  ASSERT_EQ(inner.size(), 2u);
  for (const auto &call : inner) {
    EXPECT_EQ(call->getName(), "Twice");
    EXPECT_EQ(call->getNamespaceList(), kPkgInner);
  }
  EXPECT_TRUE(ASTQuery::findDeclarationByName("Twice", {}, tree, kPkgInner))
      << "Pkg::Twice, used only inside Quad, was not inserted";
}

TEST(Namespacing, SecondUseOfAnInsertedDeclarationResolves) {
  // import Pkg::Inner as I;  In >> I::Twice() >> I::Twice() >> Out;
  // The first call inserts Pkg::Twice into the tree (and takes it out of the
  // pending objects); the second must still find it.
  auto tree = preprocessed(TESTS_SOURCE_DIR "namespacing/aliased_twice.stride");
  ASSERT_TRUE(tree);

  auto calls = rootCalls(tree);
  ASSERT_EQ(calls.size(), 2u);
  for (const auto &call : calls) {
    EXPECT_EQ(call->getNamespaceList(), kPkgInner);
  }
}

TEST(Namespacing, UnqualifiedPackageConstantResolves) {
  // Amplify (package Pkg) uses `Input * Gain >> Output`. Gain is the package
  // constant (3.0), not the root file's Gain (5.0); Input and Output are the
  // module's own signals. Preprocessing substitutes a resolved constant's
  // value, so the stream shows which Gain was found.
  auto tree = preprocessed(TESTS_SOURCE_DIR "namespacing/constant.stride");
  ASSERT_TRUE(tree);

  auto amplify = ASTQuery::findDeclarationByName("Amplify", {}, tree, kPkgGains);
  ASSERT_TRUE(amplify);
  EXPECT_TRUE(ASTQuery::findDeclarationByName("Gain", {}, tree, kPkgGains))
      << "Pkg::Gain was not inserted";

  auto streams = amplify->getPropertyValue("streams")->toText();
  EXPECT_NE(streams.find("Input * 3.0"), std::string::npos) << streams;
  EXPECT_EQ(streams.find("5.0"), std::string::npos)
      << "the root Gain leaked into the package module: " << streams;
  EXPECT_NE(streams.find(">> Output"), std::string::npos) << streams;
}

TEST(Namespacing, ResolutionSurvivesTheCache) {
  // Cached trees must carry the properties resolution depends on
  // (_sourcePath, _package, _imports, namespaceTree), and a cached directory
  // must not have its source paths prefixed twice. Run cold, then warm.
  auto dir = std::filesystem::temp_directory_path() /
             ("stride_ns_cache_" +
              std::to_string(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()));
  auto &cache = CacheManager::instance();
  CacheManager::TestAccess::setExperimentalCompilerOverride(0);
  const bool wasEnabled = cache.isEnabled();
  const bool wasForce = cache.isForceRecompile();
  const auto previousDir = cache.getCacheDirectory();
  cache.setEnabled(true);
  cache.setForceRecompile(false);
  cache.setCacheDirectory(dir);
  cache.resetStats();

  for (const char *pass : {"cold", "warm"}) {
    SCOPED_TRACE(pass);
    auto aliased = preprocessed(TESTS_SOURCE_DIR "namespacing/aliased.stride");
    ASSERT_TRUE(aliased);
    auto calls = rootCalls(aliased);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0]->getNamespaceList(), kPkgInner);
    EXPECT_TRUE(ASTQuery::findDeclarationByName("Twice", {}, aliased, kPkgInner));

    auto constant = preprocessed(TESTS_SOURCE_DIR "namespacing/constant.stride");
    ASSERT_TRUE(constant);
    auto amplify = ASTQuery::findDeclarationByName("Amplify", {}, constant, kPkgGains);
    ASSERT_TRUE(amplify);
    auto streams = amplify->getPropertyValue("streams")->toText();
    EXPECT_NE(streams.find("Input * 3.0"), std::string::npos) << streams;

    // A directory import: source paths below it, prefixed exactly once.
    StrideLibrary library;
    library.initializeLibrary("", {TESTS_SOURCE_DIR "namespacing"});
    std::map<std::string, std::string> sources;
    for (const auto &node : library.loadImport("Pkg", "")) {
      if (auto path = node->getCompilerProperty("_sourcePath")) {
        sources[std::static_pointer_cast<DeclarationNode>(node)->getName()] =
            std::static_pointer_cast<ValueNode>(path)->getStringValue();
      }
    }
    EXPECT_EQ(sources["Gain"], "Pkg::Gains");
    EXPECT_EQ(sources["Twice"], "Pkg::Inner");
  }
  EXPECT_GT(cache.getStats().hits, 0u) << "the warm pass did not use the cache";

  cache.setCacheDirectory(previousDir);
  cache.setForceRecompile(wasForce);
  cache.setEnabled(wasEnabled);
  CacheManager::TestAccess::setExperimentalCompilerOverride(-1);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(Namespacing, FullImportPathResolves) {
  // import Pkg::Inner;  In >> Pkg::Inner::Twice() >> Out;
  // Inner.stride's package is Pkg, so the full import path maps to it.
  auto tree = preprocessed(TESTS_SOURCE_DIR "namespacing/fullpath.stride");
  ASSERT_TRUE(tree);

  auto calls = rootCalls(tree);
  ASSERT_EQ(calls.size(), 1u);
  EXPECT_EQ(calls[0]->getNamespaceList(), kPkgInner);
  EXPECT_TRUE(ASTQuery::findDeclarationByName("Twice", {}, tree, kPkgInner));
}
