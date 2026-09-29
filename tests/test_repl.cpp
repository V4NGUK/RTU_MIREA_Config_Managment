#include <iostream>
#include <cassert>
#include <string>
#include <cstdlib>

std::string expand_env_variables(const std::string& input) {
    std::string result = input;
    size_t pos = 0;
    while ((pos = result.find('$', pos)) != std::string::npos) {
        size_t end_pos = pos + 1;
        while (end_pos < result.length() &&
              (isalnum(result[end_pos]) || result[end_pos] == '_')) {
            end_pos++;
              }
        std::string var_name = result.substr(pos + 1, end_pos - pos - 1);
        if (!var_name.empty()) {
            const char* var_val = std::getenv(var_name.c_str());
            std::string replacement = var_val ? std::string(var_val) : "";
            result.replace(pos, end_pos - pos, replacement);
            pos += replacement.length();
        } else {
            pos++;
        }
    }
    return result;
}

void test_env_expansion() {
#if defined(_WIN32)
    _putenv("TEST_VAR=hello");
#else
    setenv("TEST_VAR", "hello", 1);
#endif
    std::string input = "echo $TEST_VAR world";
    std::string expanded = expand_env_variables(input);
    assert(expanded == "echo hello world");
    std::cout << "[PASSED] Test Environment Variable Expansion\n";
}

int main() {
    test_env_expansion();
    std::cout << "All Stage 1 tests passed successfully!\n";
    return 0;
}