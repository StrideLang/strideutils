#include "gtest/gtest.h"

#include "stride/parser/arraynode.h"
#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/importnode.h"
#include "stride/parser/keywordnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/packagenode.h"
#include "stride/parser/platformnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/cacheenvelope.h"
#include "stride/utils/cachemanager.h"
#include "stride/utils/iastserializer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace strd;

namespace {

class ScopedCacheDir {
public:
  ScopedCacheDir() {
    auto base =
        std::filesystem::temp_directory_path() /
        ("stride_cache_test_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    m_testDir = base;
    m_cacheDir = base / ".strdcache_custom";
    std::filesystem::create_directories(m_testDir);
    m_prevEnabled = CacheManager::instance().isEnabled();
    m_prevCacheDir = CacheManager::instance().getCacheDirectory();
    CacheManager::instance().setEnabled(true);
    CacheManager::instance().setCacheDirectory(m_cacheDir);
    CacheManager::instance().resetStats();
  }

  ~ScopedCacheDir() {
    CacheManager::instance().setCacheDirectory(m_prevCacheDir);
    CacheManager::instance().setEnabled(m_prevEnabled);
    CacheManager::instance().setSerializer(
        ASTSerializerFactory::createDefault());
    std::error_code ec;
    std::filesystem::remove_all(m_testDir, ec);
  }

  const std::filesystem::path &testDir() const { return m_testDir; }
  const std::filesystem::path &cacheDir() const { return m_cacheDir; }

private:
  std::filesystem::path m_testDir;
  std::filesystem::path m_cacheDir;
  std::filesystem::path m_prevCacheDir;
  bool m_prevEnabled{true};
};

} // namespace

TEST(ASTSerializerTests, EnvelopeHeaderIs64Bytes) {
  EXPECT_EQ(sizeof(CacheEnvelopeHeader), 64u);
  CacheEnvelopeHeader header{};
  EXPECT_TRUE(header.hasValidMagic());
  header.magic[0] = 'X';
  EXPECT_FALSE(header.hasValidMagic());
}

TEST(ASTSerializerTests, RoundTripAllNodeTypesAndCyclicCompilerProperties) {
  auto serializer = ASTSerializerFactory::createDefault();
  ASSERT_NE(serializer, nullptr);

  auto root = std::make_shared<AST>(AST::None, "test.stride", 1);
  root->addScope("RootScope");

  // Package & Import & System
  root->addChild(
      std::make_shared<PackageNode>("Games::Core", "test.stride", 2));
  root->addChild(
      std::make_shared<ImportNode>("StdLib", "test.stride", 3, "Std"));
  auto sysNode = std::make_shared<SystemNode>("JIT", 1, 2, "test.stride", 4);
  sysNode->setHwPlatforms({"x86_64", "arm64"});
  root->addChild(sysNode);

  // Declaration & ArrayDeclaration
  auto props = std::make_shared<ListNode>("test.stride", 5);
  props->addChild(std::make_shared<PropertyNode>(
      "default", std::make_shared<ValueNode>(int64_t(42), "test.stride", 6),
      "test.stride", 6));
  props->addChild(std::make_shared<PropertyNode>(
      "rate", std::make_shared<ValueNode>(48000.5, "test.stride", 7),
      "test.stride", 7));
  props->addChild(std::make_shared<PropertyNode>(
      "label",
      std::make_shared<ValueNode>(std::string(" Gain "), "test.stride", 8),
      "test.stride", 8));
  props->addChild(std::make_shared<PropertyNode>(
      "enabled", std::make_shared<ValueNode>(true, "test.stride", 9),
      "test.stride", 9));

  auto decl = std::make_shared<DeclarationNode>("MySig", "signal", props,
                                                "test.stride", 10);
  decl->addScope("Audio");
  root->addChild(decl);

  auto idxList = std::make_shared<ListNode>("test.stride", 11);
  idxList->addChild(std::make_shared<ValueNode>(int64_t(4), "test.stride", 11));
  idxList->addChild(std::make_shared<ValueNode>(int64_t(8), "test.stride", 11));
  auto arrIdx = std::make_shared<ArrayNode>("MyBundle", idxList, "test.stride", 11);
  auto arrProps = std::make_shared<ListNode>("test.stride", 11);
  auto arrDecl = std::make_shared<DeclarationNode>(arrIdx, "bundle",
                                                   arrProps, "test.stride", 12);
  root->addChild(arrDecl);

  // Stream with Function, Expression, MemberAccess, Keyword
  auto leftEnt = std::make_shared<EntityNode>("InSig", "test.stride", 13);
  auto funcPropList = std::make_shared<ListNode>("test.stride", 14);
  funcPropList->addChild(std::make_shared<PropertyNode>(
      "gain", std::make_shared<ValueNode>(0.5, "test.stride", 14),
      "test.stride", 14));
  auto funcNode = std::make_shared<FunctionNode>("Multiply", funcPropList,
                                                 "test.stride", 14);
  funcNode->setRate(100.0);

  // Test cyclic compiler properties (mainInput <-> mainOutput) and shared_ptr
  // aliasing
  leftEnt->setCompilerProperty("declaration", decl);
  funcNode->setCompilerProperty("declaration", decl);
  funcNode->setCompilerProperty("mainInput", leftEnt);
  leftEnt->setCompilerProperty("mainOutput", funcNode);

  auto stream =
      std::make_shared<StreamNode>(leftEnt, funcNode, "test.stride", 15);
  root->addChild(stream);

  auto exprAdd = std::make_shared<ExpressionNode>(
      ExpressionNode::Add,
      std::make_shared<ValueNode>(int64_t(10), "test.stride", 16),
      std::make_shared<ValueNode>(int64_t(20), "test.stride", 16),
      "test.stride", 16);
  auto exprNeg = std::make_shared<ExpressionNode>(ExpressionNode::UnaryMinus,
                                                   exprAdd, "test.stride", 17);
  root->addChild(exprNeg);

  root->addChild(std::make_shared<MemberAccessNode>("InSig", "fieldName",
                                                    "test.stride", 19));
  root->addChild(std::make_shared<KeywordNode>("on", "test.stride", 21));

  std::vector<std::byte> bytes;
  ASSERT_TRUE(serializer->serializeToBytes(root, bytes));
  EXPECT_GT(bytes.size(), 0u);

  ASTNode restored;
  ASSERT_TRUE(serializer->deserializeFromBytes(bytes, restored));
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->getNodeType(), AST::None);
  EXPECT_EQ(restored->getFilename(), "test.stride");
  ASSERT_EQ(restored->getChildren().size(), root->getChildren().size());

  // Check Package, Import, System
  auto restPkg =
      std::static_pointer_cast<PackageNode>(restored->getChildren()[0]);
  EXPECT_EQ(restPkg->packageName(), "Games::Core");

  auto restImp =
      std::static_pointer_cast<ImportNode>(restored->getChildren()[1]);
  EXPECT_EQ(restImp->importName(), "StdLib");
  EXPECT_EQ(restImp->importAlias(), "Std");

  auto restSys =
      std::static_pointer_cast<SystemNode>(restored->getChildren()[2]);
  EXPECT_EQ(restSys->platformName(), "JIT");
  EXPECT_EQ(restSys->majorVersion(), 1);
  EXPECT_EQ(restSys->minorVersion(), 2);
  ASSERT_EQ(restSys->hwPlatforms().size(), 2u);
  EXPECT_EQ(restSys->hwPlatforms()[0], "x86_64");
  EXPECT_EQ(restSys->hwPlatforms()[1], "arm64");

  // Check Declaration & ArrayDeclaration
  auto restDecl =
      std::static_pointer_cast<DeclarationNode>(restored->getChildren()[3]);
  EXPECT_EQ(restDecl->getName(), "MySig");
  EXPECT_EQ(restDecl->getEntityType(), "signal");
  ASSERT_NE(restDecl->getPropertyValue("default"), nullptr);
  EXPECT_EQ(
      std::static_pointer_cast<ValueNode>(restDecl->getPropertyValue("default"))
          ->getIntValue(),
      42);

  auto restArrDecl =
      std::static_pointer_cast<DeclarationNode>(restored->getChildren()[4]);
  EXPECT_EQ(restArrDecl->getNodeType(), AST::ArrayDeclaration);
  ASSERT_NE(restArrDecl->getArrayIndex(), nullptr);
  EXPECT_EQ(restArrDecl->getArrayIndex()->getIndices(),
            (std::vector<size_t>{4, 8}));

  // Check Stream, cyclic compiler properties, and shared_ptr identity
  auto restStream =
      std::static_pointer_cast<StreamNode>(restored->getChildren()[5]);
  auto restLeft = restStream->getLeft();
  auto restFunc =
      std::static_pointer_cast<FunctionNode>(restStream->getRight());
  EXPECT_DOUBLE_EQ(restFunc->getRate(), 100.0);

  // Check MemberAccess
  auto restMem =
      std::static_pointer_cast<MemberAccessNode>(restored->getChildren()[7]);
  EXPECT_EQ(restMem->getEntity(), "InSig");
  EXPECT_EQ(restMem->getPropertyName(), "fieldName");

  // Verify shared_ptr aliasing is preserved!
  EXPECT_EQ(restLeft->getCompilerProperty("declaration").get(), restDecl.get());
  EXPECT_EQ(restFunc->getCompilerProperty("declaration").get(), restDecl.get());

  // Verify cyclic compiler properties are preserved!
  EXPECT_EQ(restFunc->getCompilerProperty("mainInput").get(), restLeft.get());
  EXPECT_EQ(restLeft->getCompilerProperty("mainOutput").get(), restFunc.get());
}

TEST(CacheManagerTests, HitMissAndInvalidationOnFileChange) {
  ScopedCacheDir scoped;
  auto strideFile = scoped.testDir() / "Sample.stride";

  {
    std::ofstream out(strideFile);
    out << "signal X { default: 10 }\n";
  }

  // 1. Initial load should miss
  EXPECT_EQ(CacheManager::instance().loadCachedAST(strideFile), nullptr);
  EXPECT_EQ(CacheManager::instance().getStats().misses, 1u);
  EXPECT_EQ(CacheManager::instance().getStats().hits, 0u);

  // 2. Parse and save to cache
  auto parsed = AST::parseFile(strideFile.string().c_str());
  ASSERT_NE(parsed, nullptr);
  EXPECT_TRUE(CacheManager::instance().saveCachedAST(strideFile, parsed));
  EXPECT_EQ(CacheManager::instance().getStats().writes, 1u);

  // 3. Subsequent load should hit cache
  auto cached = CacheManager::instance().loadCachedAST(strideFile);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(CacheManager::instance().getStats().hits, 1u);
  EXPECT_EQ(AST::toText(cached), AST::toText(parsed));

  // 4. Modify the file content on disk -> should invalidate and miss
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream out(strideFile, std::ios::trunc);
    out << "signal X { default: 999 }\n";
  }

  auto afterEdit = CacheManager::instance().loadCachedAST(strideFile);
  EXPECT_EQ(afterEdit, nullptr);
  EXPECT_GE(CacheManager::instance().getStats().invalidations, 1u);
}

TEST(CacheManagerTests, DependencyModificationInvalidatesCache) {
  ScopedCacheDir scoped;
  auto mainFile = scoped.testDir() / "Main.stride";
  auto depFile = scoped.testDir() / "Dep.stride";

  {
    std::ofstream out(mainFile);
    out << "signal MainSig { default: 1 }\n";
  }
  {
    std::ofstream out(depFile);
    out << "constant DepVal { value: 5 }\n";
  }

  auto parsed = AST::parseFile(mainFile.string().c_str());
  ASSERT_NE(parsed, nullptr);
  ASSERT_TRUE(CacheManager::instance().saveCachedAST(
      mainFile, parsed, {depFile}, "with_dep"));

  // Hit when neither file changed
  ASSERT_NE(CacheManager::instance().loadCachedAST(
                mainFile, {depFile}, "with_dep"),
            nullptr);

  // Modify dependency file only -> Main cache must invalidate!
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream out(depFile, std::ios::trunc);
    out << "constant DepVal { value: 9999 }\n";
  }

  EXPECT_EQ(CacheManager::instance().loadCachedAST(
                mainFile, {depFile}, "with_dep"),
            nullptr);
  EXPECT_GE(CacheManager::instance().getStats().invalidations, 1u);
}

TEST(CacheManagerTests, DirectoryCachingInLoadAllInDirectory) {
  ScopedCacheDir scoped;
  auto pkgDir = scoped.testDir() / "MyPkg";
  std::filesystem::create_directories(pkgDir);

  auto modA = pkgDir / "ModA.stride";
  auto modB = pkgDir / "ModB.stride";
  {
    std::ofstream outA(modA);
    outA << "signal A { default: 1 }\n";
    std::ofstream outB(modB);
    outB << "signal B { default: 2 }\n";
  }

  CacheManager::instance().resetStats();

  // First call: misses directory cache and writes bundle
  auto nodes1 = ASTFunctions::loadAllInDirectory(pkgDir.string());
  EXPECT_EQ(nodes1.size(), 2u);
  EXPECT_EQ(CacheManager::instance().getStats().misses, 1u);
  EXPECT_EQ(CacheManager::instance().getStats().writes, 1u);

  // Second call: hits directory cache in one shot
  auto nodes2 = ASTFunctions::loadAllInDirectory(pkgDir.string());
  EXPECT_EQ(nodes2.size(), 2u);
  EXPECT_EQ(CacheManager::instance().getStats().hits, 1u);

  // Modify one file in the directory -> directory cache invalidates
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream outB(modB, std::ios::trunc);
    outB << "signal B { default: 200 }\n"
         << "signal C { default: 300 }\n";
  }

  auto nodes3 = ASTFunctions::loadAllInDirectory(pkgDir.string());
  EXPECT_EQ(nodes3.size(), 3u);
  EXPECT_GE(CacheManager::instance().getStats().invalidations, 1u);
}

TEST(CacheManagerTests, BinaryPayloadCachingObjAndBitcode) {
  ScopedCacheDir scoped;
  auto sourceFile = scoped.testDir() / "Game.stride";
  {
    std::ofstream out(sourceFile);
    out << "signal Score { default: 100 }\n";
  }

  // Simulated compiled object bytes
  std::vector<uint8_t> mockObjBytes = {0x7F, 'E', 'L', 'F', 0x01, 0x02, 0x03, 0x04};
  // Simulated LLVM bitcode bytes
  std::vector<uint8_t> mockBcBytes = {'B', 'C', 0xC0, 0xDE, 0x10, 0x20, 0x30, 0x40};

  std::string objTag = "jit_obj_x86_64_O2";
  std::string bcTag = "ir_bc_O2";

  // 1. Initial loads should miss
  std::vector<uint8_t> loadedObj;
  EXPECT_FALSE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedObj, {}, objTag, ".obj"));
  EXPECT_TRUE(loadedObj.empty());

  std::vector<uint8_t> loadedBc;
  EXPECT_FALSE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedBc, {}, bcTag, ".bc"));
  EXPECT_TRUE(loadedBc.empty());

  // 2. Save both binary caches
  EXPECT_TRUE(CacheManager::instance().saveCachedBinary(
      sourceFile, mockObjBytes.data(), mockObjBytes.size(), {}, objTag, ".obj"));
  EXPECT_TRUE(CacheManager::instance().saveCachedBinary(
      sourceFile, mockBcBytes.data(), mockBcBytes.size(), {}, bcTag, ".bc"));

  // 3. Subsequent loads should hit
  EXPECT_TRUE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedObj, {}, objTag, ".obj"));
  EXPECT_EQ(loadedObj, mockObjBytes);

  EXPECT_TRUE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedBc, {}, bcTag, ".bc"));
  EXPECT_EQ(loadedBc, mockBcBytes);

  // 4. Modifying source invalidates binary caches
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  {
    std::ofstream out(sourceFile, std::ios::trunc);
    out << "signal Score { default: 200 }\n";
  }

  EXPECT_FALSE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedObj, {}, objTag, ".obj"));
  EXPECT_FALSE(CacheManager::instance().loadCachedBinary(
      sourceFile, loadedBc, {}, bcTag, ".bc"));
}

TEST(CacheManagerTests, MicroBenchmarkASTParseVsLoadCache) {
  ScopedCacheDir scoped;
  auto strideFile = scoped.testDir() / "BenchmarkModule.stride";

  {
    std::ofstream out(strideFile);
    for (int i = 0; i < 50; ++i) {
      out << "signal Sig" << i << " { default: " << i << " }\n";
      out << "constant Const" << i << " { value: " << (i * 10) << " }\n";
    }
  }

  // Pre-populate cache
  auto parsedInitial = AST::parseFile(strideFile.string().c_str());
  ASSERT_NE(parsedInitial, nullptr);
  ASSERT_TRUE(CacheManager::instance().saveCachedAST(strideFile, parsedInitial));

  // 1. Measure raw AST parse time over 50 iterations
  const int iterations = 50;
  auto t0 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto parsed = AST::parseFile(strideFile.string().c_str());
    ASSERT_NE(parsed, nullptr);
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  double parseDurationUs =
      std::chrono::duration<double, std::micro>(t1 - t0).count() / iterations;

  // 2. Measure cached AST load time over 50 iterations
  auto t2 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto cached = CacheManager::instance().loadCachedAST(strideFile);
    ASSERT_NE(cached, nullptr);
  }
  auto t3 = std::chrono::high_resolution_clock::now();
  double cacheDurationUs =
      std::chrono::duration<double, std::micro>(t3 - t2).count() / iterations;

  double speedup =
      parseDurationUs / (cacheDurationUs > 0.0 ? cacheDurationUs : 0.001);

  std::cout << "\n======================================================\n";
  std::cout << " [MICRO-BENCHMARK] StrideUtils AST Cache (" << iterations
            << " iterations)\n";
  std::cout << " Raw AST::parseFile:        " << parseDurationUs
            << " us (avg)\n";
  std::cout << " Cached loadCachedAST:      " << cacheDurationUs
            << " us (avg)\n";
  std::cout << " Micro-Speedup:             " << speedup << "x faster\n";
  std::cout << "======================================================\n"
            << std::endl;
}

TEST(CacheManagerTests, CompilerCommitAndBuildHashInvalidation) {
  ScopedCacheDir scoped;
  auto strideFile = scoped.testDir() / "VersionTest.stride";

  {
    std::ofstream out(strideFile);
    out << "signal TestSig { default: 42 }\n";
  }

  // 1. Verify commit hash and build hash are populated
  const char *commitHash = CacheManager::getCompilerCommitHash();
  ASSERT_NE(commitHash, nullptr);
  EXPECT_GT(std::strlen(commitHash), 0u);

  uint64_t initialBuildHash = CacheManager::getCompilerBuildHash();
  EXPECT_NE(initialBuildHash, 0u);

  // 2. Save both AST and binary payload
  auto parsed = AST::parseFile(strideFile.string().c_str());
  ASSERT_NE(parsed, nullptr);
  EXPECT_TRUE(CacheManager::instance().saveCachedAST(strideFile, parsed));

  std::vector<uint8_t> fakeObj = {0xDE, 0xAD, 0xBE, 0xEF};
  EXPECT_TRUE(CacheManager::instance().saveCachedBinary(
      strideFile, fakeObj.data(), fakeObj.size(), {}, "", ".obj"));

  // Verify hits
  EXPECT_NE(CacheManager::instance().loadCachedAST(strideFile), nullptr);
  std::vector<uint8_t> loadedObj;
  EXPECT_TRUE(CacheManager::instance().loadCachedBinary(strideFile, loadedObj, {}, "", ".obj"));
  EXPECT_EQ(loadedObj, fakeObj);

  // 3. Simulate a compiler binary / commit hash change using hash override
  uint64_t simulatedNewCommitHash = initialBuildHash ^ 0xDEADBEEFCAFEBABEULL;
  CacheManager::setCompilerBuildHashOverride(simulatedNewCommitHash);
  EXPECT_EQ(CacheManager::getCompilerBuildHash(), simulatedNewCommitHash);

  // Both AST and binary cache must invalidate due to compiler build hash mismatch!
  EXPECT_EQ(CacheManager::instance().loadCachedAST(strideFile), nullptr);
  std::vector<uint8_t> rejectedObj;
  EXPECT_FALSE(CacheManager::instance().loadCachedBinary(strideFile, rejectedObj, {}, "", ".obj"));

  // 4. Restore original hash
  CacheManager::clearCompilerBuildHashOverride();
  EXPECT_EQ(CacheManager::getCompilerBuildHash(), initialBuildHash);

  // Both hit again
  EXPECT_NE(CacheManager::instance().loadCachedAST(strideFile), nullptr);
  std::vector<uint8_t> restoredObj;
  EXPECT_TRUE(CacheManager::instance().loadCachedBinary(strideFile, restoredObj, {}, "", ".obj"));
  EXPECT_EQ(restoredObj, fakeObj);
}

