#include "gtest/gtest.h"
#include <algorithm>

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
  EXPECT_EQ(decl->getEntityType(), "module");
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

TEST(Library, PackageDspImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  // Load package Dsp as Dsp
  auto nodes = library.loadImport("Dsp", "Dsp");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  // Verify Level, Pan, and submodules were loaded
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Level") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Pan") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "BiQuad") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Oscillator") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "C4") != names.end());
}

TEST(Library, PackageIOImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  // Load IO::Serial as Serial
  auto serialNodes = library.loadImport("IO::Serial", "Serial");
  EXPECT_FALSE(serialNodes.empty());

  std::vector<std::string> serialNames;
  for (const auto &node : serialNodes) {
    if (node->getNodeType() == AST::Declaration) {
      serialNames.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }
  EXPECT_TRUE(std::find(serialNames.begin(), serialNames.end(), "Print") != serialNames.end());
  EXPECT_TRUE(std::find(serialNames.begin(), serialNames.end(), "Out") != serialNames.end());
  EXPECT_TRUE(std::find(serialNames.begin(), serialNames.end(), "In") != serialNames.end());

  // Load IO::Osc as Osc
  auto oscNodes = library.loadImport("IO::Osc", "Osc");
  EXPECT_FALSE(oscNodes.empty());

  std::vector<std::string> oscNames;
  for (const auto &node : oscNodes) {
    if (node->getNodeType() == AST::Declaration) {
      oscNames.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }
  EXPECT_TRUE(std::find(oscNames.begin(), oscNames.end(), "Out") != oscNames.end());
  EXPECT_TRUE(std::find(oscNames.begin(), oscNames.end(), "In") != oscNames.end());
}

TEST(Library, PackageMathConstants) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  // 1. Verify root prelude does NOT contain deprecated _Pi, _TwoPi, _E
  auto members = library.getLibraryMembers();
  ASSERT_TRUE(members.find("") != members.end());
  const auto &rootNodes = members[""];

  std::vector<std::string> rootNames;
  for (const auto &node : rootNodes) {
    if (node->getNodeType() == AST::Declaration) {
      rootNames.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "_Pi") == rootNames.end());
  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "_TwoPi") == rootNames.end());
  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "_E") == rootNames.end());

  // 2. Verify Pi, TwoPi, and E are registered under package Math
  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "Pi") != rootNames.end());
  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "TwoPi") != rootNames.end());
  EXPECT_TRUE(std::find(rootNames.begin(), rootNames.end(), "E") != rootNames.end());

  for (const auto &node : rootNodes) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getName() == "Pi" || decl->getName() == "TwoPi" || decl->getName() == "E") {
        auto nsProp = decl->getCompilerProperty("namespaceTree");
        ASSERT_TRUE(nsProp != nullptr);
        ASSERT_FALSE(nsProp->getChildren().empty());
        auto val = std::static_pointer_cast<ValueNode>(nsProp->getChildren()[0]);
        EXPECT_EQ(val->getStringValue(), "Math");
      }
    }
  }

  // 3. Load Math::Constants explicitly as MathConstants
  auto constNodes = library.loadImport("Math::Constants", "MathConstants");
  EXPECT_FALSE(constNodes.empty());

  std::vector<std::string> constNames;
  for (const auto &node : constNodes) {
    if (node->getNodeType() == AST::Declaration) {
      constNames.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }
  EXPECT_TRUE(std::find(constNames.begin(), constNames.end(), "Pi") != constNames.end());
  EXPECT_TRUE(std::find(constNames.begin(), constNames.end(), "TwoPi") != constNames.end());
  EXPECT_TRUE(std::find(constNames.begin(), constNames.end(), "E") != constNames.end());

  // 4. Load package Math directly
  auto mathNodes = library.loadImport("Math", "Math");
  EXPECT_FALSE(mathNodes.empty());

  std::vector<std::string> mathNames;
  for (const auto &node : mathNodes) {
    if (node->getNodeType() == AST::Declaration) {
      mathNames.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }
  EXPECT_TRUE(std::find(mathNames.begin(), mathNames.end(), "Pi") != mathNames.end());
  EXPECT_TRUE(std::find(mathNames.begin(), mathNames.end(), "TwoPi") != mathNames.end());
  EXPECT_TRUE(std::find(mathNames.begin(), mathNames.end(), "E") != mathNames.end());
}

TEST(Library, PackageControlImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Control", "Control");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Pid") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Hysteresis") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "SlewRateLimiter") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Clarke") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Park") != names.end());
}

TEST(Library, PackageSensorsImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Sensors", "Sensors");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Debounce") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Deadband") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "LinearCalibration") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "ComplementaryFilter") != names.end());
}

TEST(Library, PackageEstimationImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Estimation", "Estimation");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Filter") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Detector") != names.end());
}

TEST(Library, PackageDspExtendedImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Dsp", "Dsp");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Detector") != names.end());       // Goertzel
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Line") != names.end());           // FractionalDelay
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Follower") != names.end());       // EnvelopeFollower
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Device") != names.end());         // Compressor
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Fast") != names.end());           // Limiter
  EXPECT_TRUE(std::find(names.begin(), names.end(), "SoftClip") != names.end());       // Saturate
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Tanh") != names.end());           // Saturate
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Fir7") != names.end());           // Halfband
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Decimator2") != names.end());     // Cic
}

TEST(Library, PackageWindowImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Window", "Window");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Hann") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Hamming") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Blackman") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "FlatTop") != names.end());
}

TEST(Library, PackageLinAlgImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("LinAlg", "LinAlg");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "Dot2D") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Cross3D") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Norm3D") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "Transform2D") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "MatMul2x2") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "RotateVector3D") != names.end());
}

TEST(Library, PackageTestImport) {
  StrideLibrary library;
  library.initializeLibrary(STRIDEROOT, {});

  auto nodes = library.loadImport("Test", "Test");
  EXPECT_FALSE(nodes.empty());

  std::vector<std::string> names;
  for (const auto &node : nodes) {
    if (node->getNodeType() == AST::Declaration) {
      names.push_back(std::static_pointer_cast<DeclarationNode>(node)->getName());
    }
  }

  EXPECT_TRUE(std::find(names.begin(), names.end(), "AssertInRange") != names.end());
}


