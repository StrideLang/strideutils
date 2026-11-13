// Guards against declaration lookups that drop a node's namespace qualifier.
//
// `findDeclarationByName(ASTQuery::getNodeName(node), scope, tree)` (or
// `node->getName()`) looks up
// `Pkg::Module` as plain `Module`, which never matches a declaration imported
// into a namespace. Use ASTQuery::findDeclarationForNode(node, ...) instead,
// or pass a namespace list explicitly (`{}` for a deliberately unqualified
// lookup). See doc/NamespacingPlan.md, Phase 1.

#include "gtest/gtest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Index of the ')' matching the '(' just before `i`, or npos.
size_t matchingParen(const std::string &s, size_t i) {
  int depth = 1;
  for (; i < s.size(); i++) {
    char c = s[i];
    if (c == '"') {
      for (i++; i < s.size() && s[i] != '"'; i++) {
        if (s[i] == '\\') {
          i++;
        }
      }
    } else if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if (c == ')' || c == ']' || c == '}') {
      if (--depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

std::vector<std::string> topLevelArgs(const std::string &args) {
  std::vector<std::string> out;
  std::string current;
  int depth = 0;
  for (char c : args) {
    if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if (c == ')' || c == ']' || c == '}') {
      depth--;
    }
    if (c == ',' && depth == 0) {
      out.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  out.push_back(current);
  return out;
}

std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// "file:line: call" for every lookup keyed on getNodeName(...) that passes no
// namespace list.
std::vector<std::string> bareNameLookups(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string src = buffer.str();

  std::vector<std::string> found;
  for (const std::string fn :
       {"ASTQuery::findDeclarationByName(", "ASTQuery::findAllDeclarations("}) {
    size_t pos = 0;
    while ((pos = src.find(fn, pos)) != std::string::npos) {
      size_t argsBegin = pos + fn.size();
      size_t argsEnd = matchingParen(src, argsBegin);
      if (argsEnd == std::string::npos) {
        break;
      }
      auto args = topLevelArgs(src.substr(argsBegin, argsEnd - argsBegin));
      const std::string name = trim(args[0]);
      const std::string getName = "getName()";
      bool fromNode =
          name.rfind("ASTQuery::getNodeName(", 0) == 0 ||
          (name.size() > getName.size() &&
           name.compare(name.size() - getName.size(), getName.size(),
                        getName) == 0);
      if (args.size() < 4 && fromNode) {
        size_t line = 1 + std::count(src.begin(), src.begin() + pos, '\n');
        found.push_back(file.filename().string() + ":" + std::to_string(line) +
                        ": " + fn + trim(args[0]) + ", ...)");
      }
      pos = argsEnd;
    }
  }
  return found;
}

} // namespace

TEST(LookupLint, NoLookupDropsTheNamespaceQualifier) {
  // TESTS_SOURCE_DIR is strideutils/tests/data/; the other compiler repos are
  // siblings of strideutils (libgame/external/ in boardgame, the Stride root).
  const std::filesystem::path data = TESTS_SOURCE_DIR;
  const std::filesystem::path repos = data / ".." / ".." / "..";
  std::vector<std::filesystem::path> dirs;
  for (const char *repo : {"strideutils", "stridegen", "stridejit"}) {
    auto dir = repos / repo / "src";
    if (std::filesystem::is_directory(dir)) {
      dirs.push_back(dir);
    }
  }
  ASSERT_FALSE(dirs.empty()) << "no compiler sources found under " << repos;

  std::vector<std::string> offenders;
  for (const auto &dir : dirs) {
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() == ".cpp") {
        auto found = bareNameLookups(entry.path());
        offenders.insert(offenders.end(), found.begin(), found.end());
      }
    }
  }
  std::string report;
  for (const auto &o : offenders) {
    report += "\n  " + o;
  }
  EXPECT_TRUE(offenders.empty())
      << "Use ASTQuery::findDeclarationForNode / findAllDeclarationsForNode, "
         "or pass the namespace list explicitly:"
      << report;
}
