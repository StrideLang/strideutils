#include "gtest/gtest.h"

#include "stride/parser/ast.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"

using namespace strd;

TEST(ASTFunctions, EvaluateConstInteger) {
    auto valueNode = std::make_shared<ValueNode>((int64_t)42, "test", 1);
    std::vector<LangError> errors;
    
    int64_t result = ASTFunctions::evaluateConstInteger(valueNode, {}, nullptr, &errors);
    
    EXPECT_EQ(result, 42);
    EXPECT_EQ(errors.size(), 0);
}

TEST(ASTFunctions, EvaluateConstReal) {
    auto valueNode = std::make_shared<ValueNode>((double)3.14159, "test", 1);
    std::vector<LangError> errors;
    
    double result = ASTFunctions::evaluateConstReal(valueNode, {}, nullptr, &errors);
    
    EXPECT_DOUBLE_EQ(result, 3.14159);
    EXPECT_EQ(errors.size(), 0);
}

TEST(ASTFunctions, EvaluateConstString) {
    auto valueNode = std::make_shared<ValueNode>(std::string("hello"), "test", 1);
    std::vector<LangError> errors;
    
    std::string result = ASTFunctions::evaluateConstString(valueNode, {}, nullptr, "", &errors);
    
    EXPECT_EQ(result, "hello");
    EXPECT_EQ(errors.size(), 0);
}

TEST(ASTFunctions, EvaluateTypeMismatches) {
    auto stringNode = std::make_shared<ValueNode>(std::string("not_an_int"), "test", 1);
    std::vector<LangError> errors;
    
    // Evaluating a string as an integer should yield an error
    ASTFunctions::evaluateConstInteger(stringNode, {}, nullptr, &errors);
    EXPECT_GT(errors.size(), 0);
}
