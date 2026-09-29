#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#include <lmcons.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

std::string get_current_username() {
#if defined(_WIN32)
    char username[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(username, &len)) {
        return std::string(username);
    }
#else
    char username[LOGIN_NAME_MAX];
    if (getlogin_r(username, sizeof(username)) == 0) {
        return std::string(username);
    }
#endif
    char* env_user = std::getenv("USER");
    if (!env_user) env_user = std::getenv("USERNAME");
    return env_user ? std::string(env_user) : "user";
}

std::string get_current_hostname() {
#if defined(_WIN32)
    char hostname[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(hostname, &len)) {
        return std::string(hostname);
    }
#else
    char hostname[HOST_NAME_MAX];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
#endif
    return "localhost";
}

std::string get_prompt() {
    return get_current_username() + "@" + get_current_hostname() + ":~$ ";
}

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

std::vector<std::string> parse_input(const std::string& line) {
    std::string expanded = expand_env_variables(line);
    std::stringstream ss(expanded);
    std::string token;
    std::vector<std::string> tokens;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

int main() {
    std::string input_line;
    while (true) {
        std::cout << get_prompt();
        if (!std::getline(std::cin, input_line)) {
            break;
        }

        std::vector<std::string> args = parse_input(input_line);
        if (args.empty()) {
            continue;
        }

        std::string command = args[0];

        if (command == "exit") {
            break;
        } else if (command == "ls" || command == "cd") {
            std::cout << "[STUB] Executing command: " << command << "\n";
            std::cout << "Arguments (" << args.size() - 1 << "): ";
            for (size_t i = 1; i < args.size(); ++i) {
                std::cout << "\"" << args[i] << "\" ";
            }
            std::cout << "\n";
        } else {
            std::cerr << "Error: Unknown command '" << command << "'\n";
        }
    }
    return 0;
}