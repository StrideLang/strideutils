#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/astruntime.h"
#include "stride/utils/cachemanager.h"
#include "stride/utils/logger.h"
#include "stride/utils/stridelibrary.h"

#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace strd;

std::string ASTFunctions::getDefaultStrideRoot() {
  auto strideroot = getenv("STRIDEROOT");
  if (strideroot) {
    return std::string(strideroot);
  } else {
    std::filesystem::path abspath;
    try {
      abspath = std::filesystem::canonical(
          std::filesystem::absolute(std::filesystem::current_path().string() +
                                    "/../../../Stride/strideroot"));
      //    LOG_INFO() << abspath.string() << std::endl;
    } catch (...) {
      LOG_ERROR() << "ERROR: Can't find strideroot and STRIDEROOT not set."
                  << std::endl;
    }
    return abspath.string();
  }
}

void ASTFunctions::insertRequiredObjects(
    ASTNode tree, std::map<std::string, std::vector<ASTNode>> externalNodes,
    ScopeStack *platformScope) {
  auto children = tree->getChildren();
  std::set<const void *> processedNodes;
  for (const ASTNode &object : children) {
    insertRequiredObjectsForNode(object, externalNodes, tree, platformScope, "",
                                 &processedNodes);
  }
}

std::vector<ASTNode> ASTFunctions::loadAllInDirectory(std::string path) {
  std::vector<ASTNode> nodes;

  if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path))
    return {};

  if (CacheManager::instance().loadCachedDirectory(path, nodes)) {
    return nodes;
  }

  for (auto const &dir_entry : std::filesystem::directory_iterator{path}) {
    if (dir_entry.is_regular_file() &&
        std::filesystem::path(dir_entry).extension() == ".stride") {
      std::string stem = dir_entry.path().stem().string();
      if (stem.empty() || !std::isupper(static_cast<unsigned char>(stem[0]))) {
        continue; // Only import files starting with a capital letter
      }
      std::string fileName = dir_entry.path().string();
      auto newTree = AST::parseFile(fileName.c_str(), nullptr);
      if (newTree) {
        auto packageNodes = ASTQuery::getPackageNodes(newTree);
        std::vector<std::string> pkgSegs;
        if (!packageNodes.empty()) {
          pkgSegs = packageNodes[0]->packageSegments();
        }
        for (const auto &child : newTree->getChildren()) {
          if (child->getNodeType() == AST::Declaration ||
              child->getNodeType() == AST::ArrayDeclaration) {
            if (!pkgSegs.empty() &&
                !child->getCompilerProperty("namespaceTree")) {
              for (const auto &seg : pkgSegs) {
                child->appendToPropertyValue(
                    "namespaceTree",
                    std::make_shared<ValueNode>(seg, __FILE__, __LINE__));
              }
            }
            nodes.push_back(child);
          }
        }
      } else {
        LOG_ERROR() << "ERROR importing tree: " << dir_entry << " in " << path
                    << std::endl;
        std::vector<LangError> errors = AST::getParseErrors();
        for (LangError error : errors) {
          LOG_ERROR() << error.getErrorText();
        }
      }
    } else if (dir_entry.is_directory()) {
      std::string dirName = dir_entry.path().filename().string();
      if (!dirName.empty() &&
          std::isupper(static_cast<unsigned char>(dirName[0]))) {
        auto subNodes = loadAllInDirectory(dir_entry.path().string());
        for (const auto &subNode : subNodes) {
          if (subNode->getNodeType() == AST::Declaration ||
              subNode->getNodeType() == AST::ArrayDeclaration) {
            if (!subNode->getCompilerProperty("namespaceTree")) {
              subNode->appendToPropertyValue(
                  "namespaceTree",
                  std::make_shared<ValueNode>(dirName, __FILE__, __LINE__));
            }
            nodes.push_back(subNode);
          }
        }
      }
    }
  }

  if (!nodes.empty()) {
    CacheManager::instance().saveCachedDirectory(path, nodes);
  }

  return nodes;
}

bool ASTFunctions::preprocess(ASTNode tree, ScopeStack *platformScope,
                              std::vector<std::string> importPaths,
                              std::string strideroot) {
  bool ok = true;

  if (!tree) {
    return false;
  }

  if (tree->getCompilerProperty("preprocessed")) {
    return true;
  }

  // TODO insert external objects

  StrideLibrary library;
  { // Process Imports

    std::vector<std::shared_ptr<ImportNode>> importList;
    for (const ASTNode &node : tree->getChildren()) {
      if (node->getNodeType() == AST::Import) {
        std::shared_ptr<ImportNode> import =
            std::static_pointer_cast<ImportNode>(node);
        // TODO add namespace support here (e.g. import
        // Platform::Filters::Filter)
        bool imported = false;
        for (const auto &importNode : importList) {
          if ((std::static_pointer_cast<ImportNode>(importNode)->importName() ==
               import->importName()) &&
              (std::static_pointer_cast<ImportNode>(importNode)
                   ->importAlias() == import->importAlias())) {
            imported = true;
            break;
          }
        }
        if (!imported) {
          importList.push_back(import);
        }
      }
    }

    std::filesystem::path filePath = tree->getFilename();
    filePath.remove_filename();
    importPaths.push_back(filePath.string());

    library.initializeLibrary(strideroot, importPaths);
    for (const auto &import : importList) {
      library.loadImport(import->importName(), import->importAlias());
    }
  }

  std::map<std::string, std::vector<ASTNode>> externalNodes =
      library.getLibraryMembers();
  ASTFunctions::insertRequiredObjects(tree, externalNodes, platformScope);

  ASTFunctions::resolveInheritance(tree);
  ASTFunctions::processAnoymousDeclarations(tree);
  ASTFunctions::fillDefaultProperties(tree);
  ASTFunctions::resolveConstants(tree);

  tree->setCompilerProperty(
      "preprocessed",
      std::make_shared<ValueNode>((int64_t)1, __FILE__, __LINE__));
  return ok;
}

bool ASTFunctions::resolveInheritance(ASTNode tree) {
  bool ok = true;
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      ok &= ASTFunctions::resolveDeclarationInheritance(decl, tree);
    }
  }
  return ok;
}

void ASTFunctions::insertDependentTypes(
    std::shared_ptr<DeclarationNode> typeDeclaration,
    std::map<std::string, std::vector<ASTNode>> &externalNodes, ASTNode tree,
    std::set<const void *> *processedTypes) {
  if (!typeDeclaration) {
    return;
  }
  std::set<const void *> localProcessed;
  if (!processedTypes) {
    processedTypes = &localProcessed;
  }
  if (processedTypes->count(typeDeclaration.get()) > 0) {
    return;
  }
  processedTypes->insert(typeDeclaration.get());

  std::vector<std::shared_ptr<DeclarationNode>> blockList;
  //    std::shared_ptr<DeclarationNode> existingDecl =
  //    ASTQuery::findTypeDeclaration(typeDeclaration, ScopeStack(),
  //    m_tree);
  for (auto it = externalNodes.begin(); it != externalNodes.end(); it++) {
    // To avoid redundant checking here we should mark nodes that have already
    // been processed
    auto inheritedTypes = ASTQuery::getInheritedTypes(
        typeDeclaration, {{nullptr, it->second}}, tree);

    for (const auto &inheritedType : inheritedTypes) {
      const auto &children = tree->getChildren();
      if (std::find(children.begin(), children.end(), inheritedType) ==
          children.end()) {
        tree->addChild(inheritedType);
        auto findPos =
            std::find(it->second.begin(), it->second.end(), inheritedType);
        if (findPos != it->second.end()) {
          it->second.erase(findPos);
        }
      }
      insertDependentTypes(inheritedType, externalNodes, tree, processedTypes);
    }
  }
}

void ASTFunctions::insertRequiredObjectsForNode(
    ASTNode node, std::map<std::string, std::vector<ASTNode>> &objects,
    ASTNode tree, ScopeStack *platformScope, std::string currentFramework,
    std::set<const void *> *processedNodes) {
  if (!node) {
    return;
  }
  std::set<const void *> localProcessed;
  if (!processedNodes) {
    processedNodes = &localProcessed;
  }
  if (processedNodes->count(node.get()) > 0) {
    return;
  }
  processedNodes->insert(node.get());

  std::vector<std::shared_ptr<DeclarationNode>> blockList;
  if (node->getNodeType() == AST::List) {
    for (const ASTNode &child : node->getChildren()) {
      insertRequiredObjectsForNode(child, objects, tree, platformScope,
                                   currentFramework, processedNodes);
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    insertRequiredObjectsForNode(stream->getLeft(), objects, tree,
                                 platformScope, currentFramework,
                                 processedNodes);
    insertRequiredObjectsForNode(stream->getRight(), objects, tree,
                                 platformScope, currentFramework,
                                 processedNodes);
  } else if (node->getNodeType() == AST::Expression) {
    ExpressionNode *expr = static_cast<ExpressionNode *>(node.get());
    if (expr->isUnary()) {
      insertRequiredObjectsForNode(expr->getValue(), objects, tree,
                                   platformScope, currentFramework,
                                   processedNodes);
    } else {
      insertRequiredObjectsForNode(expr->getLeft(), objects, tree,
                                   platformScope, currentFramework,
                                   processedNodes);
      insertRequiredObjectsForNode(expr->getRight(), objects, tree,
                                   platformScope, currentFramework,
                                   processedNodes);
    }
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    ScopeStack localScope;
    ScopeStack *effectiveScope = platformScope ? platformScope : &localScope;
    for (auto it = objects.begin(); it != objects.end(); it++) {
      // for now, insert all declarations from all frameworks.
      // FIXME check outer domain and framework to only import needed modules
      effectiveScope->push_back({nullptr, it->second});

      std::vector<std::shared_ptr<DeclarationNode>> alldecls =
          ASTQuery::findAllDeclarations(func->getName(), *effectiveScope,
                                        nullptr, func->getNamespaceList(),
                                        currentFramework);
      effectiveScope->pop_back();

      for (const auto &decl : alldecls) {
        if (std::find(blockList.begin(), blockList.end(), decl) ==
            blockList.end()) {
          // TODO we should filter here to only bring in the ones that are
          // needed according to the required types.
          blockList.push_back(decl);
        }
      }
    }
    // Look for declarations of blocks present in function properties
    for (const auto &property : func->getProperties()) {
      insertRequiredObjectsForNode(property->getValue(), objects, tree,
                                   platformScope, currentFramework,
                                   processedNodes);
    }
    for (const std::shared_ptr<DeclarationNode> &usedDecl : blockList) {
      // Add declarations to tree if not there
      auto usedBlockFramework = usedDecl->getCompilerProperty("framework");
      std::string fw;

      if (usedBlockFramework &&
          usedBlockFramework->getNodeType() == AST::String) {
        fw = std::static_pointer_cast<ValueNode>(usedBlockFramework)
                 ->getStringValue();
      }

      // Declarations don't have a namespace, but they can be imported into one
      // This is written into the namespaceTree property
      auto namespaceTreeNode = usedDecl->getCompilerProperty("namespaceTree");
      std::vector<std::string> namespaceTree;
      if (namespaceTreeNode) {
        for (const auto &node : namespaceTreeNode->getChildren()) {
          namespaceTree.push_back(
              std::static_pointer_cast<ValueNode>(node)->getStringValue());
        }
      }
      if (namespaceTree.size() > 0 && namespaceTree.at(0) == fw) {
        namespaceTree.erase(namespaceTree.begin());
      }

      if (auto decl = ASTQuery::findDeclarationByName(
              usedDecl->getName(), ScopeStack(), tree, namespaceTree, fw)) {
        auto atProp = decl->getCompilerProperty("_at");
        auto usedDeclAtProp = usedDecl->getCompilerProperty("_at");
        if (atProp && usedDeclAtProp) {
          if (atProp->getNodeType() == AST::String &&
              usedDeclAtProp->getNodeType() == AST::String) {
            if (std::static_pointer_cast<ValueNode>(atProp)->getStringValue() !=
                std::static_pointer_cast<ValueNode>(usedDeclAtProp)
                    ->getStringValue()) {
              tree->addChild(usedDecl);
            }
            // Don't add if _at matches
          } else {
            tree->addChild(usedDecl);
          }
        } else {
          tree->addChild(usedDecl);
        }
      } else {
        tree->addChild(usedDecl);
      }
      insertRequiredObjectsForNode(usedDecl, objects, tree, platformScope, fw,
                                   processedNodes);
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> declaration =
        std::static_pointer_cast<DeclarationNode>(node);

    for (const auto &property : declaration->getProperties()) {
      insertRequiredObjectsForNode(property->getValue(), objects, tree,
                                   platformScope, currentFramework,
                                   processedNodes);
    }
    //    vector<ASTNode> namespaceObjects;
    if (declaration->getScopeLevels() > 0) {
      //      namespaceObjects = objects[declaration->getScopeAt(
      //          0)]; // TODO need to implement for namespaces that are more
      //          than one
      // level deep...
    }
    //    auto typeDecl = CodeValidator::findTypeDeclarationByName(
    //        declaration->getEntityType(), {}, tree);
    //    if (!typeDecl) {

    auto frameworkNode = node->getCompilerProperty("framework");
    std::string framework;
    if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
      framework =
          std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    }
    for (auto it = objects.begin(); it != objects.end(); it++) {
      auto existingTypeDecl = ASTQuery::findTypeDeclarationByName(
          declaration->getEntityType(), {}, tree, {it->first}, framework);
      //      for (auto objectTree : it->second) {
      // FIXME get namespaces for declaration, not objects to insert
      {
        bool allocated = false;
        if (!platformScope) {
          allocated = true;
          platformScope = new ScopeStack();
        }
        platformScope->push_back({nullptr, it->second});
        auto typeDecl = ASTQuery::findTypeDeclarationByName(
            declaration->getEntityType(), *platformScope, nullptr, {},
            framework);
        if (!typeDecl) { // try root namespace
          typeDecl = ASTQuery::findTypeDeclarationByName(
              declaration->getEntityType(), *platformScope, nullptr, {});
        }
        platformScope->pop_back();
        if (typeDecl && !existingTypeDecl) {
          tree->addChild(typeDecl);
          // FIXME instead of removing we must make sure that objects are not
          // inserted in tree if already there
          //        auto position =
          //            std::find(it->second.begin(), it->second.end(),
          //            typeDecl);
          //        if (position != it->second.end()) {
          //          it->second.erase(position);
          //        }
          insertRequiredObjectsForNode(typeDecl, objects, tree, platformScope,
                                       "", processedNodes);
          insertDependentTypes(typeDecl, objects, tree);
        }
        if (allocated) {
          delete platformScope;
          platformScope = nullptr;
        }
      }
      //      }
    }
    if (declaration /*&& !ASTQuery::findDeclaration(
                  QString::fromStdString(name->getName()), {},
                  tree, name->getNamespaceList())*/) {
      //                declaration->setRootScope(it->first);
      if (std::find(blockList.begin(), blockList.end(), declaration) ==
          blockList.end()) {
        blockList.push_back(declaration);
        // Insert needed objects for things in module properties
        for (std::shared_ptr<PropertyNode> property :
             declaration->getProperties()) {
          if (property->getName() == "constraints") {
            continue;
            // Constraints play by their own rules.
          }
          insertRequiredObjectsForNode(property->getValue(), objects, tree,
                                       platformScope, framework,
                                       processedNodes);
        }
        // Process index for array declarations
        if (node->getNodeType() == AST::ArrayDeclaration) {
          insertRequiredObjectsForNode(declaration->getArrayIndex()->index(),
                                       objects, tree, platformScope, framework,
                                       processedNodes);
        }
      }
    }
    //  }
    //    }
  } else if (node->getNodeType() == AST::Entity) {
    //    QList<std::shared_ptr<DeclarationNode>> blockList;
    EntityNode *name = static_cast<EntityNode *>(node.get());

    auto declaration = ASTQuery::findDeclarationByName(
        name->getName(), {}, tree, name->getNamespaceList(), currentFramework);

    if (!declaration) {
      for (auto it = objects.begin(); it != objects.end(); it++) {
        auto newDeclarations = ASTQuery::findAllDeclarations(
            name->getName(), {{nullptr, it->second}}, nullptr,
            name->getNamespaceList(), currentFramework);

        for (const auto &newDecl : newDeclarations) {
          if (std::find(blockList.begin(), blockList.end(), newDecl) ==
              blockList.end()) {
            blockList.push_back(newDecl);
            auto existingPos =
                std::find(it->second.begin(), it->second.end(), newDecl);
            if (existingPos != it->second.end()) {
              it->second.erase(existingPos);
            }
          }
        }
      }
      for (const auto &usedEntity : blockList) {
        auto frameworkNode = usedEntity->getCompilerProperty("framework");
        std::string framework;
        if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
          framework = std::static_pointer_cast<ValueNode>(frameworkNode)
                          ->getStringValue();
        }
        auto namespaceList = usedEntity->getNamespaceList();
        //        if (namespaceList.size() > 0 && namespaceList[0] == framework)
        //        {
        //          namespaceList.erase(namespaceList.begin());
        //        }
        auto existingDeclaration = ASTQuery::findDeclarationByName(
            usedEntity->getName(), {}, tree, namespaceList, framework);
        if (!existingDeclaration) {
          tree->addChild(usedEntity);
        }
        insertRequiredObjectsForNode(usedEntity, objects, tree, platformScope,
                                     "", processedNodes);
      }
    }
  } else if (node->getNodeType() == AST::Array) {
    std::vector<std::shared_ptr<DeclarationNode>> blockList;
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    auto declaration = ASTQuery::findDeclarationByName(
        arrayNode->getName(), {}, tree, arrayNode->getNamespaceList(),
        currentFramework);
    if (!declaration) {
      for (auto it = objects.begin(); it != objects.end(); it++) {
        auto newDeclarations = ASTQuery::findAllDeclarations(
            arrayNode->getName(), {{nullptr, it->second}}, nullptr,
            arrayNode->getNamespaceList(), currentFramework);
        for (const auto &newDecl : newDeclarations) {
          if (std::find(blockList.begin(), blockList.end(), newDecl) ==
              blockList.end()) {
            blockList.push_back(newDecl);
            auto existingPos =
                std::find(it->second.begin(), it->second.end(), newDecl);
            if (existingPos != it->second.end()) {
              it->second.erase(existingPos);
            }
          }
        }
      }
      for (const auto &usedEntity : blockList) {
        auto frameworkNode = usedEntity->getCompilerProperty("framework");
        std::string framework;
        if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
          framework = std::static_pointer_cast<ValueNode>(frameworkNode)
                          ->getStringValue();
        }
        auto existingDeclaration = ASTQuery::findDeclarationByName(
            usedEntity->getName(), {}, tree, usedEntity->getNamespaceList(),
            framework);
        if (!existingDeclaration) {
          tree->addChild(usedEntity);
        }
        insertRequiredObjectsForNode(usedEntity, objects, tree, platformScope,
                                     "", processedNodes);
      }
    }
  } else if (node->getNodeType() == AST::Property) {
    std::shared_ptr<PropertyNode> prop =
        std::static_pointer_cast<PropertyNode>(node);
    insertRequiredObjectsForNode(prop->getValue(), objects, tree, platformScope,
                                 currentFramework, processedNodes);
  }
}

void ASTFunctions::fillDefaultProperties(ASTNode tree) {
  std::vector<ASTNode> nodes = tree->getChildren();
  for (const auto &node : nodes) {
    ASTFunctions::fillDefaultPropertiesForNode(node, tree->getChildren());
  }
}

void ASTFunctions::fillDefaultPropertiesForNode(
    ASTNode node, std::vector<ASTNode> scopeNodes) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> destBlock =
        std::static_pointer_cast<DeclarationNode>(node);
    std::vector<std::shared_ptr<PropertyNode>> blockProperties =
        destBlock->getProperties();
    auto frameworkNode = node->getCompilerProperty("framework");
    std::string frameworkName;
    if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
      frameworkName =
          std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    }
    auto typeProperties = ASTQuery::getPortsForType(
        destBlock->getEntityType(), {{nullptr, scopeNodes}}, nullptr,
        destBlock->getNamespaceList(), frameworkName);
    if (typeProperties.size() == 0) {
      LOG_ERROR()
          << "ERROR: fillDefaultPropertiesForNode() No type definition for "
          << destBlock->getEntityType() << std::endl;
      return;
    }
    for (const auto &property : blockProperties) {
      fillDefaultPropertiesForNode(property->getValue(), scopeNodes);
    }

    for (const ASTNode &propertyListMember : typeProperties) {
      assert(propertyListMember->getNodeType() == AST::Declaration);
      DeclarationNode *portDescription =
          static_cast<DeclarationNode *>(propertyListMember.get());
      ASTNode propName = portDescription->getPropertyValue("name");
      assert(propName->getNodeType() == AST::String);
      std::string propertyName =
          static_cast<ValueNode *>(propName.get())->getStringValue();
      bool propertySet = false;
      for (const auto &blockProperty : blockProperties) {
        if (blockProperty->getName() == propertyName) {
          propertySet = true;
          break;
        }
      }
      if (!propertySet) {
        ASTNode defaultValueNode = portDescription->getPropertyValue("default");
        if (defaultValueNode) {
          std::shared_ptr<PropertyNode> newProperty =
              std::make_shared<PropertyNode>(
                  propertyName, defaultValueNode->deepCopy(),
                  portDescription->getFilename().data(),
                  portDescription->getLine());
          destBlock->addProperty(newProperty);
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    std::shared_ptr<FunctionNode> destFunc =
        std::static_pointer_cast<FunctionNode>(node);
    std::vector<std::shared_ptr<PropertyNode>> blockProperties =
        destFunc->getProperties();
    std::shared_ptr<DeclarationNode> functionModule =
        ASTQuery::findDeclarationByName(destFunc->getName(),
                                        {{nullptr, scopeNodes}}, nullptr);
    if (functionModule) {
      if (functionModule->getEntityType() == "module" ||
          functionModule->getEntityType() == "reaction" ||
          functionModule->getEntityType() == "loop") {
        // TODO use isCodeGenerator
        // if (ASTQuery::isCodeGenerator(functionModule, {{nullptr,
        // scopeNodes}}, tree)) {
        if (!functionModule->getPropertyValue("ports")) {
          LOG_ERROR()
              << "ERROR: fillDefaultProperties() No ports definition for "
              << destFunc->getName() << std::endl;
          return;
        }
        std::vector<ASTNode> typeProperties =
            functionModule->getPropertyValue("ports")->getChildren();
        for (const auto &property : blockProperties) {
          fillDefaultPropertiesForNode(property->getValue(), scopeNodes);
        }

        for (const ASTNode &propertyListMember : typeProperties) {
          assert(propertyListMember->getNodeType() == AST::Declaration);
          DeclarationNode *propertyDecl =
              static_cast<DeclarationNode *>(propertyListMember.get());

          if (propertyDecl->getEntityType().substr(0, 8) == "property") {
            auto propertyNameValue = propertyDecl->getPropertyValue("name");
            if (propertyNameValue &&
                propertyNameValue->getNodeType() == AST::String) {
              bool propertySet = false;
              std::string propertyName =
                  std::static_pointer_cast<ValueNode>(propertyNameValue)
                      ->getStringValue();
              for (const auto &blockProperty : blockProperties) {
                if (blockProperty->getName() == propertyName) {
                  propertySet = true;
                  break;
                }
              }
              if (!propertySet) {
                ASTNode defaultValueNode =
                    propertyDecl->getPropertyValue("default");
                if (defaultValueNode) {
                  std::shared_ptr<PropertyNode> newProperty =
                      std::make_shared<PropertyNode>(
                          propertyName, defaultValueNode,
                          propertyDecl->getFilename().data(),
                          propertyDecl->getLine());
                  destFunc->addProperty(newProperty);
                }
              }
            }
          }
        }

      } else if (functionModule->getEntityType() == "platformModule") {
        // Is there anything to do here for platform modules?
      }
    }
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const ASTNode &listElement : node->getChildren()) {
      ASTFunctions::fillDefaultPropertiesForNode(listElement, scopeNodes);
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    for (const ASTNode &streamElement : stream->getChildren()) {
      ASTFunctions::fillDefaultPropertiesForNode(streamElement, scopeNodes);
    }
  }
}

void ASTFunctions::processAnoymousDeclarations(ASTNode tree) {
  auto newDecls = ASTFunctions::processAnonDeclsForScope(tree->getChildren());

  for (const auto &node : newDecls) {
    tree->addChild(node);
  }
}

void ASTFunctions::resolveConstants(ASTNode tree) {
  for (const auto &node : tree->getChildren()) {
    resolveConstantsInNode(node, {}, tree);
  }
}

std::shared_ptr<ListNode>
resolveStreamArray(std::shared_ptr<StreamNode> stream,
                   std::shared_ptr<DeclarationNode> decl, int64_t i) {
  auto newStreamList = std::make_shared<ListNode>(__FILE__, __LINE__);
  StreamNodeIterator it(stream);
  StreamNodeBuilder builder;
  auto node = it.next();
  while (node) {
    if (node->getNodeType() == AST::MemberAccess) {
      auto pp = std::static_pointer_cast<MemberAccessNode>(node);
      if (pp->getEntity() == "") {
        auto pv = decl->getPropertyValue(pp->getPropertyName());
        if (pv) {
          if (pv->getNodeType() == AST::List) {
            auto listElems = pv->getChildren();
            builder.addNode(listElems[i]);
          } else {
            builder.addNode(pv);
          }
        } else {
          LOG_ERROR() << "Port property not existent for expansion."
                      << std::endl;
        }
      } else {
        LOG_ERROR() << "Substition in streams for member accesses "
                       "in array declarations only supported for "
                       "internal ports."
                    << std::endl;
        builder.addNode(node);
      }
    } else {
      builder.addNode(node);
    }
    node = it.next();
  }
  newStreamList->addChild(builder.build());

  return newStreamList;
}

void ASTFunctions::resolveConstantsInNode(ASTNode node, ScopeStack scope,
                                          ASTNode tree) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Stream) {
    std::shared_ptr<StreamNode> stream =
        std::static_pointer_cast<StreamNode>(node);
    resolveConstantsInNode(stream->getLeft(), scope, tree);
    if (stream->getLeft()->getNodeType() == AST::Expression) {
      std::shared_ptr<ExpressionNode> expr =
          std::static_pointer_cast<ExpressionNode>(stream->getLeft());
      std::shared_ptr<ValueNode> newValue =
          reduceConstExpression(expr, scope, tree);
      if (newValue) {
        stream->setLeft(newValue);
      }
    } else if (stream->getLeft()->getNodeType() == AST::Entity) {
      std::shared_ptr<ValueNode> newValue =
          ASTFunctions::resolveConstant(stream->getLeft(), scope, tree);
      if (newValue) {
        stream->setLeft(newValue);
      }
    } else if (stream->getLeft()->getNodeType() == AST::MemberAccess) {
      std::shared_ptr<MemberAccessNode> propertyNode =
          std::static_pointer_cast<MemberAccessNode>(stream->getLeft());
      std::shared_ptr<DeclarationNode> entityDecl =
          ASTQuery::findDeclarationByName(propertyNode->getEntity(), scope,
                                          tree);
      if (entityDecl) {
        ASTNode property =
            entityDecl->getPropertyValue(propertyNode->getPropertyName());
        if (property) { // First replace if pointing to a name
          if (property->getNodeType() == AST::Entity ||
              property->getNodeType() == AST::Array) {
            stream->setLeft(property);
          }
          std::shared_ptr<ValueNode> newValue =
              ASTFunctions::resolveConstant(stream->getLeft(), scope, tree);
          if (newValue) {
            stream->setLeft(newValue);
          }
        }
      }
    }
    resolveConstantsInNode(stream->getRight(), scope, tree);
  } else if (node->getNodeType() == AST::Function) {
    std::shared_ptr<FunctionNode> func =
        std::static_pointer_cast<FunctionNode>(node);
    std::vector<std::shared_ptr<PropertyNode>> properties =
        func->getProperties();
    for (auto property : properties) {
      std::shared_ptr<ValueNode> newValue =
          ASTFunctions::resolveConstant(property->getValue(), scope, tree);
      if (newValue) {
        property->replaceValue(newValue);
      }
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(node);
    std::vector<std::shared_ptr<PropertyNode>> properties =
        decl->getProperties();
    std::shared_ptr<ListNode> internalBlocks =
        std::static_pointer_cast<ListNode>(decl->getPropertyValue("blocks"));
    // TODO namespaces
    if (ASTQuery::isCodeGenerator(
            ASTQuery::findDeclarationByName(decl->getName(), scope, tree),
            scope, tree) &&
        internalBlocks) {
      if (internalBlocks->getNodeType() == AST::List) {
        auto blocks = internalBlocks->getChildren();
        scope.push_back({node, blocks});
      }
    }
    // FIXME This is a hack to protect constants that are context dependent.
    // Should the language mark this?
    //    if (decl->getEntityType() != "type") {

    auto frameworkNode = node->getCompilerProperty("framework");
    std::string frameworkName;
    if (frameworkNode) {
      frameworkName =
          std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
    }
    //    if (frameworkName == "") {
    //      // Now try to see if domain belongs to a framework

    //      auto domainId =
    //          CodeAnalysis::getNodeDomainName(node, ScopeStack(), tree);

    //      frameworkName = CodeAnalysis::getFrameworkForDomain(domainId,
    //      tree);
    //    }

    for (std::shared_ptr<PropertyNode> property : properties) {
      // FIXME verify that decl inherits from _CodeGenerator
      if (property->getName() == "streams" &&
          decl->getNodeType() == AST::ArrayDeclaration) {
        size_t arraySize = 0;
        auto arrayNode = decl->getArrayIndex();
        std::vector<ASTNode> listExprs =
            decl->getArrayIndex()->getChildren()[0]->getChildren();
        for (const ASTNode &expr : listExprs) {
          if (expr->getNodeType() == AST::Int) {
            arraySize =
                std::static_pointer_cast<ValueNode>(expr)->getIntValue();
          }
        }
        auto newStreams = std::make_shared<ListNode>(__FILE__, __LINE__);
        auto value = property->getValue();
        if (value->getNodeType() == AST::List) {
          for (const auto &streamNode : value->getChildren()) {
            if (streamNode->getNodeType() == AST::Stream) {
              auto stream = std::static_pointer_cast<StreamNode>(streamNode);

              for (int i = 0; i < arraySize; i++) {
                auto newStreamList = resolveStreamArray(stream, decl, i);
                newStreams->addChild(newStreamList);
              }
            } else if (value->getNodeType() == AST::List) {

            } else {
              LOG_ERROR()
                  << "unexpected node type when processing stream node: "
                  << AST::toText(value) << std::endl;
              newStreams->addChild(value);
            }
          }
          property->replaceValue(newStreams);
        } else if (value->getNodeType() == AST::Stream) {

          //   if ()
          // for (int i = 0; i < arraySize; i++) {
          //       builder.addNode();
          // }
        } else {
          LOG_ERROR() << "unexpected node type when processing stream node: "
                      << AST::toText(value) << std::endl;
          newStreams->addChild(value);
        }

      } else {
        resolveConstantsInNode(property->getValue(), scope, tree);
        std::shared_ptr<ValueNode> newValue =
            resolveConstant(property->getValue(), scope, tree, frameworkName);
        if (!newValue) {
          // try to resolve on global namespace
          newValue =
              ASTFunctions::resolveConstant(property->getValue(), scope, tree);
        }
        if (newValue) {
          property->replaceValue(newValue);
        }
      }
    }

    if (decl->getNodeType() == AST::ArrayDeclaration) {
      std::shared_ptr<ArrayNode> arrayNode = decl->getArrayIndex();
      resolveConstantsInNode(arrayNode->index(), scope, tree);
    }
  } else if (node->getNodeType() == AST::Expression) {
    std::shared_ptr<ExpressionNode> expr =
        std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      resolveConstantsInNode(expr->getValue(), scope, tree);
      if (expr->getValue()->getNodeType() == AST::Expression) {
        std::shared_ptr<ExpressionNode> exprValue =
            std::static_pointer_cast<ExpressionNode>(expr->getValue());
        std::shared_ptr<ValueNode> newValue =
            reduceConstExpression(exprValue, scope, tree);
        if (newValue) {
          exprValue->replaceValue(newValue);
        }
      }
    } else {
      resolveConstantsInNode(expr->getLeft(), scope, tree);
      resolveConstantsInNode(expr->getRight(), scope, tree);
      if (expr->getLeft()->getNodeType() == AST::Expression) {
        std::shared_ptr<ExpressionNode> exprValue =
            std::static_pointer_cast<ExpressionNode>(expr->getLeft());
        std::shared_ptr<ValueNode> newValue =
            reduceConstExpression(exprValue, scope, tree);
        if (newValue) {
          expr->replaceLeft(newValue);
        }
      }
      if (expr->getRight()->getNodeType() == AST::Expression) {
        std::shared_ptr<ExpressionNode> exprValue =
            std::static_pointer_cast<ExpressionNode>(expr->getRight());
        std::shared_ptr<ValueNode> newValue =
            reduceConstExpression(exprValue, scope, tree);
        if (newValue) {
          expr->replaceRight(newValue);
        }
      }
    }
  } else if (node->getNodeType() == AST::List) {
    std::map<ASTNode, ASTNode> replaceMap;
    for (const ASTNode &element : node->getChildren()) {
      resolveConstantsInNode(element, scope, tree);
      std::shared_ptr<ValueNode> newValue;
      if (element->getNodeType() == AST::Expression) {
        newValue = reduceConstExpression(
            std::static_pointer_cast<ExpressionNode>(element), scope, tree);

      } else {
        newValue = ASTFunctions::resolveConstant(element, scope, tree);
      }
      if (newValue) {
        replaceMap[element] = newValue;
      }
    }
    std::shared_ptr<ListNode> list = std::static_pointer_cast<ListNode>(node);
    for (auto &values : replaceMap) {
      list->replaceMember(values.second, values.first);
    }

  } else if (node->getNodeType() == AST::Array) {
    std::shared_ptr<ArrayNode> arrayNode =
        std::static_pointer_cast<ArrayNode>(node);
    std::shared_ptr<ListNode> index = arrayNode->index();
    resolveConstantsInNode(index, scope, tree);
  }
}

ASTNode
ASTFunctions::resolveMemberAccessForDecl(std::shared_ptr<DeclarationNode> decl,
                                         ScopeStack scopeStack, ASTNode tree) {
  ASTNode out;
  for (const auto &prop : decl->getProperties()) {
    auto propValue = prop->getValue();
    // TODO verify that decl inherits from _CodeGeneration
    if (prop->getName() == "streams") {
      if (propValue->getNodeType() == AST::List) {
        // for()
      }
    }
  }
  return out;
}

std::shared_ptr<ValueNode>
ASTFunctions::resolveConstant(ASTNode value, ScopeStack scope, ASTNode tree,
                              std::string framework) {
  std::shared_ptr<ValueNode> newValue = nullptr;
  if (value->getNodeType() == AST::Expression) {
    std::shared_ptr<ExpressionNode> expr =
        std::static_pointer_cast<ExpressionNode>(value);
    newValue = reduceConstExpression(expr, scope, tree);
    if (newValue) {
      newValue->setCompilerProperty("resolvedFrom", expr);
    }
    return newValue;
  } else if (value->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(value.get());
    std::shared_ptr<DeclarationNode> decl = ASTQuery::findDeclarationByName(
        name->getName(), scope, tree, name->getNamespaceList(), framework);
    if (decl && decl->getNodeType() == AST::Declaration &&
        ASTQuery::isConstant(decl, scope, tree)) { // Size == 1
      //            string namespaceValue = name->getScopeAt(0);
      ASTNode declarationNamespace = decl->getPropertyValue("namespace");
      //            if (namespaceValue.size() == 0 || namespaceValue)
      ASTNode blockValue = decl->getPropertyValue("value");
      if (blockValue->getNodeType() == AST::Int ||
          blockValue->getNodeType() == AST::Real ||
          blockValue->getNodeType() == AST::String) {
        blockValue = blockValue->deepCopy();
        blockValue->setCompilerProperty("resolvedFrom", decl);
        return std::static_pointer_cast<ValueNode>(blockValue);
      }
      newValue = ASTFunctions::resolveConstant(decl->getPropertyValue("value"),
                                               scope, tree, framework);
      return newValue;
    }
  } else if (value->getNodeType() == AST::Array) {
    // What does this mean??
  } else if (value->getNodeType() == AST::MemberAccess) {
    MemberAccessNode *propertyNode =
        static_cast<MemberAccessNode *>(value.get());
    std::shared_ptr<DeclarationNode> entityDecl =
        ASTQuery::findDeclarationByName(propertyNode->getEntity(), scope, tree);
    if (entityDecl) {
      ASTNode propertyValue =
          entityDecl->getPropertyValue(propertyNode->getPropertyName());
      if (propertyValue) {
        if (propertyValue->getNodeType() == AST::Int ||
            propertyValue->getNodeType() == AST::Real ||
            propertyValue->getNodeType() == AST::String) {
          propertyValue = propertyValue->deepCopy();
          propertyValue->setCompilerProperty("resolvedFrom", entityDecl);
          return std::static_pointer_cast<ValueNode>(propertyValue);
        }
      }
    }
  }
  return nullptr;
}

std::shared_ptr<ValueNode>
ASTFunctions::reduceConstExpression(std::shared_ptr<ExpressionNode> expr,
                                    ScopeStack scope, ASTNode tree) {
  ASTNode left = nullptr, right = nullptr;
  bool isConstant;

  if (!expr->isUnary()) {
    left = expr->getLeft();
  } else {
    left = expr->getValue();
  }

  std::shared_ptr<ValueNode> newValue = resolveConstant(left, scope, tree);
  if (newValue) {
    if (expr->isUnary()) {
      expr->replaceValue(newValue);
    } else {
      expr->replaceLeft(newValue);
    }
    left = newValue;
  }
  if (!expr->isUnary()) {
    right = expr->getRight();
    newValue = resolveConstant(right, scope, tree);
    if (newValue) {
      expr->replaceRight(newValue);
      right = newValue;
    }
    isConstant =
        (left->getNodeType() == AST::Int || left->getNodeType() == AST::Real) &&
        (right->getNodeType() == AST::Int || right->getNodeType() == AST::Real);
  } else {
    isConstant =
        (left->getNodeType() == AST::Int || left->getNodeType() == AST::Real);
  }

  if (isConstant) {
    std::shared_ptr<ValueNode> result = nullptr;
    switch (expr->getExpressionType()) {
    case ExpressionNode::Multiply:
      result = ASTRuntime::multiply(std::static_pointer_cast<ValueNode>(left),
                                    std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::Divide:
      result = ASTRuntime::divide(std::static_pointer_cast<ValueNode>(left),
                                  std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::Add:
      result = ASTRuntime::add(std::static_pointer_cast<ValueNode>(left),
                               std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::Subtract:
      result = ASTRuntime::subtract(std::static_pointer_cast<ValueNode>(left),
                                    std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::And:
      result =
          ASTRuntime::logicalAnd(std::static_pointer_cast<ValueNode>(left),
                                 std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::Or:
      result =
          ASTRuntime::logicalOr(std::static_pointer_cast<ValueNode>(left),
                                std::static_pointer_cast<ValueNode>(right));
      break;
    case ExpressionNode::UnaryMinus:
      result =
          ASTRuntime::unaryMinus(std::static_pointer_cast<ValueNode>(left));
      break;
    case ExpressionNode::LogicalNot:
      result =
          ASTRuntime::logicalNot(std::static_pointer_cast<ValueNode>(left));
      break;
    default:
      assert(0 == 1); // Should never get here
      break;
    }
    if (result) {
      return result;
    }
  }
  return nullptr;
}

int64_t ASTFunctions::evaluateConstInteger(ASTNode node, ScopeStack scope,
                                           ASTNode tree,
                                           std::vector<LangError> *errors) {
  int result = 0;
  if (node->getNodeType() == AST::Int) {
    return static_cast<ValueNode *>(node.get())->getIntValue();
  } else if (node->getNodeType() == AST::Array) {
    // FIXME this seems to be evaluating the array index??
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    ListNode *indexList = arrayNode->index().get();
    if (indexList->size() == 1) {
      int index = evaluateConstInteger(indexList->getChildren().at(0), scope,
                                       tree, errors);
      std::shared_ptr<DeclarationNode> declaration =
          ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree);
      if (declaration && declaration->getNodeType() == AST::ArrayDeclaration) {
        ASTNode member = ASTQuery::getMemberFromArrayConst(declaration, index,
                                                           tree, scope, errors);
        std::vector<LangError> internalErrors;
        auto integer =
            evaluateConstInteger(member, scope, tree, &internalErrors);
        if (internalErrors.size() == 0) {
          return integer;
        }
        LangError error;
        error.type = LangError::InvalidIndexType;
        error.lineNumber = arrayNode->index()->getLine();
        error.errorTokens.push_back(arrayNode->getName());
        if (errors) {
          errors->push_back(error);
        }
        return 0;
      }
    }
    LangError error;
    error.type = LangError::InvalidType;
    error.lineNumber = arrayNode->getLine();
    error.errorTokens.push_back(arrayNode->getName());
    if (errors) {
      errors->push_back(error);
    }
  } else if (node->getNodeType() == AST::Entity) {
    std::shared_ptr<EntityNode> nameNode =
        std::static_pointer_cast<EntityNode>(node);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(nameNode->getName(), scope, tree);
    if (declaration && ASTQuery::isConstant(declaration, scope, tree)) {
      return evaluateConstInteger(declaration->getPropertyValue("value"), scope,
                                  tree, errors);
    }
    if (errors) {
      LangError error;
      error.type = LangError::InvalidType;
      error.lineNumber = node->getLine();
      error.errorTokens.push_back(nameNode->getName());
      errors->push_back(error);
    }
    return 0;
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->getExpressionType() == ExpressionNode::UnaryMinus) {
      return -evaluateConstInteger(expr->getValue(), scope, tree, errors);
    } else if (expr->getExpressionType() == ExpressionNode::Add) {
      return evaluateConstInteger(expr->getLeft(), scope, tree, errors) +
             evaluateConstInteger(expr->getRight(), scope, tree, errors);
    } else if (expr->getExpressionType() == ExpressionNode::Subtract) {
      return evaluateConstInteger(expr->getLeft(), scope, tree, errors) -
             evaluateConstInteger(expr->getRight(), scope, tree, errors);
    } else if (expr->getExpressionType() == ExpressionNode::Multiply) {
      return evaluateConstInteger(expr->getLeft(), scope, tree, errors) *
             evaluateConstInteger(expr->getRight(), scope, tree, errors);
    } else if (expr->getExpressionType() == ExpressionNode::Divide) {
      int64_t denom =
          evaluateConstInteger(expr->getRight(), scope, tree, errors);
      if (denom != 0) {
        return evaluateConstInteger(expr->getLeft(), scope, tree, errors) /
               denom;
      }
    }
    return 0;
  } else {
    LangError error;
    error.type = LangError::InvalidType;
    error.lineNumber = node->getLine();
    error.errorTokens.push_back("");
    if (errors) {
      errors->push_back(error);
    }
  }
  return result;
}

double ASTFunctions::evaluateConstReal(ASTNode node, ScopeStack scope,
                                       ASTNode tree,
                                       std::vector<LangError> *errors) {
  double result = 0;
  if (node->getNodeType() == AST::Real) {
    return static_cast<ValueNode *>(node.get())->getRealValue();
  } else if (node->getNodeType() == AST::Int) {
    return static_cast<ValueNode *>(node.get())->getIntValue();
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree);
    int index = evaluateConstInteger(arrayNode->index(), scope, tree, errors);
    if (declaration && declaration->getNodeType() == AST::ArrayDeclaration) {
      ASTNode member = ASTQuery::getMemberFromArrayConst(declaration, index,
                                                         tree, scope, errors);
      return evaluateConstReal(member, scope, tree, errors);
    }
  } else if (node->getNodeType() == AST::Entity) {
    EntityNode *blockNode = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(blockNode->getName(), scope, tree,
                                        blockNode->getNamespaceList());
    if (!declaration) {
      LangError error;
      error.type = LangError::UndeclaredSymbol;
      error.lineNumber = node->getLine();
      //            error.errorTokens.push_back(blockNode->getName());
      //            error.errorTokens.push_back(blockNode->getNamespace());
      std::string blockName = "";
      if (blockNode->getScopeLevels()) {
        for (unsigned int i = 0; i < blockNode->getScopeLevels(); i++) {
          blockName += blockNode->getScopeAt(i);
          blockName += "::";
        }
      }
      blockName += blockNode->getName();
      error.errorTokens.push_back(blockName);
      errors->push_back(error);
    }
    if (declaration && declaration->getNodeType() == AST::Declaration) {
      ASTNode value = ASTQuery::getValueFromConstEntity(declaration.get());
      if (value->getNodeType() == AST::Int ||
          value->getNodeType() == AST::Real) {
        return static_cast<ValueNode *>(value.get())->toReal();
      } else {
        // Do something?
      }
    }
  } else {
    LangError error;
    error.type = LangError::InvalidType;
    error.lineNumber = node->getLine();
    error.errorTokens.push_back("");
    errors->push_back(error);
  }
  return result;
}

std::string ASTFunctions::evaluateConstString(ASTNode node, ScopeStack scope,
                                              ASTNode tree,
                                              std::string currentFramework,
                                              std::vector<LangError> *errors) {
  std::string result;
  if (node->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(node)->getStringValue();
  } else if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree);
    int index = evaluateConstInteger(arrayNode->index(), scope, tree, errors);
    if (declaration && declaration->getNodeType() == AST::ArrayDeclaration) {
      ASTNode member = ASTQuery::getMemberFromArrayConst(declaration, index,
                                                         tree, scope, errors);
      return evaluateConstString(member, scope, tree, currentFramework, errors);
    }
  } else if (node->getNodeType() == AST::Entity) {
    auto blockNode = std::static_pointer_cast<EntityNode>(node);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(blockNode->getName(), scope, tree,
                                        blockNode->getNamespaceList(),
                                        currentFramework);
    if (!declaration) {
      LangError error;
      error.type = LangError::UndeclaredSymbol;
      error.lineNumber = node->getLine();
      //            error.errorTokens.push_back(blockNode->getName());
      //            error.errorTokens.push_back(blockNode->getNamespace());
      std::string blockName = "";
      if (blockNode->getScopeLevels()) {
        for (unsigned int i = 0; i < blockNode->getScopeLevels(); i++) {
          blockName += blockNode->getScopeAt(i);
          blockName += "::";
        }
      }
      blockName += blockNode->getName();
      error.errorTokens.push_back(blockName);
      errors->push_back(error);
    }
    if (declaration && declaration->getNodeType() == AST::Declaration) {
      ASTNode value = ASTQuery::getValueFromConstEntity(declaration.get());
      if (value) {
        if (value->getNodeType() == AST::String ||
            value->getNodeType() == AST::Int ||
            value->getNodeType() == AST::Real) {
          return std::static_pointer_cast<ValueNode>(value)->toString();
        }
      } else {
        // Do something?
      }
    }
  } else if (node->getNodeType() == AST::MemberAccess) {
    MemberAccessNode *propertyNode =
        static_cast<MemberAccessNode *>(node.get());
    std::shared_ptr<DeclarationNode> entityDecl =
        ASTQuery::findDeclarationByName(propertyNode->getEntity(), scope, tree,
                                        {}, currentFramework);
    if (entityDecl) {
      ASTNode propertyValue =
          entityDecl->getPropertyValue(propertyNode->getPropertyName());
      if (propertyValue) {
        if (propertyValue->getNodeType() == AST::String ||
            propertyValue->getNodeType() == AST::Int ||
            propertyValue->getNodeType() == AST::Real) {
          return std::static_pointer_cast<ValueNode>(propertyValue)->toString();
        }
      }
    }
  }
  if (node->getNodeType() == AST::String || node->getNodeType() == AST::Int ||
      node->getNodeType() == AST::Real) {
    return std::static_pointer_cast<ValueNode>(node)->toString();
  } else {
    LangError error;
    error.type = LangError::InvalidType;
    error.lineNumber = node->getLine();
    error.errorTokens.push_back("");
    errors->push_back(error);
  }
  return result;
}

double ASTFunctions::getDefaultForTypeAsDouble(
    std::string type, std::string port, ScopeStack scope, ASTNode tree,
    std::vector<std::string> namespaces, std::vector<LangError> *errors) {
  double outValue = 0.0;
  ASTNode value = ASTFunctions::getDefaultPortValueForType(type, port, scope,
                                                           tree, namespaces);
  if (value) {
    outValue = ASTFunctions::evaluateConstReal(value, scope, tree, errors);
  }
  return outValue;
}

ASTNode
ASTFunctions::getDefaultPortValueForType(std::string type, std::string portName,
                                         ScopeStack scope, ASTNode tree,
                                         std::vector<std::string> namespaces) {
  auto ports = ASTQuery::getPortsForType(type, scope, tree, namespaces);
  if (ports.size() > 0) {
    for (const ASTNode &port : ports) {
      DeclarationNode *entityDecl = static_cast<DeclarationNode *>(port.get());
      assert(entityDecl->getNodeType() == AST::Declaration);
      assert(entityDecl->getEntityType() == "typeProperty");
      ASTNode platPortNameNode = entityDecl->getPropertyValue("name");
      ValueNode *platPortName =
          static_cast<ValueNode *>(platPortNameNode.get());
      assert(platPortName->getNodeType() == AST::String);
      if (platPortName->getStringValue() == portName) {
        ASTNode platPortDefault = entityDecl->getPropertyValue("default");
        if (platPortDefault) {
          return platPortDefault;
        }
      }
    }
  }
  return nullptr;
}

std::vector<ASTNode>
ASTFunctions::processAnonDeclsForScope(const std::vector<ASTNode> scopeTree) {
  std::vector<ASTNode> newDecls;
  for (const auto &node : scopeTree) {
    if (node->getNodeType() == AST::Stream) {
      auto streamDecls =
          extractStreamDeclarations(std::static_pointer_cast<StreamNode>(node));
      newDecls.insert(newDecls.end(), streamDecls.begin(), streamDecls.end());
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "reaction" ||
          decl->getEntityType() == "module" ||
          decl->getEntityType() == "loop") {
        auto streams = decl->getPropertyValue("streams");
        auto blocks = decl->getPropertyValue("blocks");
        //          Q_ASSERT(streams && blocks);
        if (streams && blocks) {

          auto streamDecls = processAnonDeclsForScope(streams->getChildren());
          for (const auto &node : streamDecls) {
            blocks->addChild(node);
          }

          auto newInternalBlocks =
              ASTFunctions::processAnonDeclsForScope(blocks->getChildren());
          for (const auto &node : newInternalBlocks) {
            blocks->addChild(node);
          }
        }
      }
    }
  }

  return newDecls;
}

std::vector<std::shared_ptr<DeclarationNode>>
ASTFunctions::extractStreamDeclarations(std::shared_ptr<StreamNode> stream) {
  std::vector<std::shared_ptr<DeclarationNode>> streamDeclarations;
  std::shared_ptr<StreamNode> currentStream = stream;
  while (currentStream) {
    if (currentStream->getLeft() &&
        currentStream->getLeft()->getNodeType() == AST::Declaration) {
      auto decl =
          std::static_pointer_cast<DeclarationNode>(currentStream->getLeft());
      auto newBlock = std::make_shared<FunctionNode>(
          decl->getName(), std::make_shared<ListNode>(__FILE__, __LINE__),
          __FILE__, __LINE__);
      streamDeclarations.push_back(decl);
      currentStream->setLeft(newBlock);
    }
    if (currentStream->getRight()) {
      if (currentStream->getRight()->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(
            currentStream->getRight());
        auto newBlock = std::make_shared<FunctionNode>(
            decl->getName(), std::make_shared<ListNode>(__FILE__, __LINE__),
            __FILE__, __LINE__);
        streamDeclarations.push_back(decl);
        currentStream->setRight(newBlock);
        break;
      } else if (currentStream->getRight()->getNodeType() == AST::Stream) {
        currentStream =
            std::static_pointer_cast<StreamNode>(currentStream->getRight());
      } else {
        break;
      }
    } else {
      break;
    }
  }
  return streamDeclarations;
}

bool ASTFunctions::resolveDeclarationInheritance(
    std::shared_ptr<DeclarationNode> decl, ASTNode tree) {
  auto inheritsNode = decl->getPropertyValue("inherits");
  if (inheritsNode && inheritsNode->getNodeType() == AST::Entity) {
    auto inheritedName =
        std::static_pointer_cast<EntityNode>(inheritsNode)->getName();
    auto inheritedDecl = ASTQuery::findDeclarationWithType(
        inheritedName, decl->getEntityType(), ScopeStack(), tree);
    if (inheritedDecl) {
      for (const auto &prop : inheritedDecl->getProperties()) {
        if (!decl->getPropertyValue(prop->getName())) {
          decl->setPropertyValue(prop->getName(), prop->getValue());
        }
      }
      return true;
    } else {
      LOG_ERROR() << "ERROR could not resolve inheritance: " << inheritedName
                  << std::endl;
      return true;
    }
  } else {
    return false;
  }
}
