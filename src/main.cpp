
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <lmcons.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {
struct Configuration {
    std::filesystem::path vfs_path;
    std::optional<std::filesystem::path> startup_script;
};

std::string get_current_username() {
#if defined(_WIN32)
    char username[UNLEN + 1]{};
    DWORD length = UNLEN + 1;
    if (GetUserNameA(username, &length)) return username;
#else
    char username[LOGIN_NAME_MAX]{};
    if (getlogin_r(username, sizeof(username)) == 0) return username;
#endif
    const char* env_user = std::getenv("USER");
    if (!env_user) env_user = std::getenv("USERNAME");
    return env_user ? env_user : "user";
}

std::string get_current_hostname() {
#if defined(_WIN32)
    char hostname[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(hostname, &length)) return hostname;
#else
    char hostname[HOST_NAME_MAX + 1]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) return hostname;
#endif
    return "localhost";
}

std::string get_prompt() {
    return get_current_username() + "@" + get_current_hostname() + ":~$ ";
}

std::string expand_env_variables(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != '$') {
            result.push_back(input[index++]);
            continue;
        }

        std::size_t end = index + 1;
        std::string name;
        if (end < input.size() && input[end] == '{') {
            const std::size_t closing = input.find('}', end + 1);
            if (closing == std::string::npos) {
                result.push_back(input[index++]);
                continue;
            }
            name = input.substr(end + 1, closing - end - 1);
            end = closing + 1;
        } else {
            const std::size_t name_start = end;
            while (end < input.size() &&
                   (std::isalnum(static_cast<unsigned char>(input[end])) ||
                    input[end] == '_')) {
                ++end;
            }
            name = input.substr(name_start, end - name_start);
        }

        if (name.empty()) {
            result.push_back(input[index++]);
            continue;
        }
        if (const char* value = std::getenv(name.c_str())) result += value;
        index = end;
    }
    return result;
}

std::string strip_comment(const std::string& line) {
    char quote = '\0';
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == '\\' && quote != '\'') {
            escaped = true;
            continue;
        }
        if ((character == '\'' || character == '"') &&
            (quote == '\0' || quote == character)) {
            quote = quote == '\0' ? character : '\0';
            continue;
        }
        if ((character == '#' || (character == '/' && index + 1 < line.size() &&
                                 line[index + 1] == '/')) && quote == '\0' &&
            (index == 0 || std::isspace(static_cast<unsigned char>(line[index - 1])))) {
            return line.substr(0, index);
        }
    }
    return line;
}

bool parse_arguments(const std::string& line,
                     std::vector<std::string>& arguments,
                     std::string& error) {
    const std::string expanded = expand_env_variables(line);
    std::string current;
    char quote = '\0';
    bool escaped = false;
    bool token_started = false;

    for (const char character : expanded) {
        if (escaped) {
            current.push_back(character);
            escaped = false;
            token_started = true;
            continue;
        }
        if (character == '\\' && quote != '\'') {
            escaped = true;
            token_started = true;
            continue;
        }
        if (character == '\'' || character == '"') {
            if (quote == '\0') {
                quote = character;
                token_started = true;
            } else if (quote == character) {
                quote = '\0';
            } else {
                current.push_back(character);
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) && quote == '\0') {
            if (token_started) {
                arguments.push_back(current);
                current.clear();
                token_started = false;
            }
            continue;
        }
        current.push_back(character);
        token_started = true;
    }

    if (escaped) current.push_back('\\');
    if (quote != '\0') {
        error = "syntax error: unmatched quote";
        return false;
    }
    if (token_started) arguments.push_back(current);
    return true;
}

bool valid_environment_name(const std::string& name) {
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        return false;
    }
    for (const char character : name) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_')) {
            return false;
        }
    }
    return true;
}

bool set_environment_variable(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

void print_help(const char* executable) {
    std::cout << "Usage: " << executable
              << " --vfs <archive.zip> [--script <startup-file>]\n"
              << "Options:\n"
              << "  --vfs, --vfs-path <path>       ZIP archive used as the virtual filesystem\n"
              << "  --script, --startup-script <path>\n"
              << "                                 Commands to run before interactive input\n"
              << "  --help                         Show this help\n";
}

bool parse_configuration(int argc,
                         char** argv,
                         Configuration& configuration,
                         bool& show_help) {
    show_help = false;
    bool has_vfs_path = false;

    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            show_help = true;
            return true;
        }

        const bool is_vfs_option = option == "--vfs" || option == "--vfs-path";
        const bool is_script_option = option == "--script" || option == "--startup-script";
        if (!is_vfs_option && !is_script_option) {
            std::cerr << "Error: unknown command-line option '" << option << "'\n";
            return false;
        }
        if (index + 1 >= argc) {
            std::cerr << "Error: option '" << option << "' requires a path\n";
            return false;
        }
        const std::filesystem::path value(argv[++index]);
        if (is_vfs_option) {
            if (has_vfs_path) {
                std::cerr << "Error: VFS path was specified more than once\n";
                return false;
            }
            configuration.vfs_path = value;
            has_vfs_path = true;
        } else {
            if (configuration.startup_script.has_value()) {
                std::cerr << "Error: startup script was specified more than once\n";
                return false;
            }
            configuration.startup_script = value;
        }
    }

    if (!has_vfs_path) {
        std::cerr << "Error: a ZIP VFS archive is required; use --vfs <path>\n";
        return false;
    }
    return true;
}

bool execute_command(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return true;
    const std::string& command = arguments.front();

    if (command == "exit") return false;

    if (command == "ls" || command == "cd") {
        std::cout << "[STUB] Executing command: " << command << '\n';
        std::cout << "Arguments (" << arguments.size() - 1 << "): ";
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            std::cout << '"' << arguments[index] << '"';
            if (index + 1 < arguments.size()) std::cout << ' ';
        }
        std::cout << '\n';
        return true;
    }

    if (command == "echo") {
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            if (index > 1) std::cout << ' ';
            std::cout << arguments[index];
        }
        std::cout << '\n';
        return true;
    }

    if (command == "regvar") {
        if (arguments.size() < 3) {
            std::cerr << "Error: usage: regvar <name> <value>\n";
            return true;
        }
        if (!valid_environment_name(arguments[1])) {
            std::cerr << "Error: invalid environment variable name '" << arguments[1]
                      << "'\n";
            return true;
        }
        std::string value = arguments[2];
        for (std::size_t index = 3; index < arguments.size(); ++index) {
            value += ' ';
            value += arguments[index];
        }
        if (!set_environment_variable(arguments[1], value)) {
            std::cerr << "Error: could not set environment variable '" << arguments[1]
                      << "'\n";
        }
        return true;
    }

    std::cerr << "Error: Unknown command '" << command << "'\n";
    return true;
}

bool execute_line(const std::string& original_line,
                  bool echo_input) {
    std::string line = strip_comment(original_line);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
    }
    const auto first_non_space = line.find_first_not_of(" \t\r\n");
    if (first_non_space == std::string::npos) return true;
    line.erase(0, first_non_space);

    if (echo_input) {
        std::cout << get_prompt() << line << '\n';
        std::cout.flush();
    }

    std::vector<std::string> arguments;
    std::string error;
    if (!parse_arguments(line, arguments, error)) {
        std::cerr << "Error: " << error << '\n';
        return true;
    }
    return execute_command(arguments);
}

bool run_stream(std::istream& input,
                bool echo_input,
                bool show_prompt) {
    std::string line;
    while (true) {
        if (show_prompt && !echo_input) {
            std::cout << get_prompt();
            std::cout.flush();
        }
        if (!std::getline(input, line)) return true;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!execute_line(line, echo_input)) return false;
    }
}

bool standard_input_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}
}  // namespace

int main(int argc, char** argv) {
    Configuration configuration;
    bool show_help = false;
    if (!parse_configuration(argc, argv, configuration, show_help)) {
        print_help(argv[0]);
        return 2;
    }
    if (show_help) {
        print_help(argv[0]);
        return 0;
    }

    std::cout << "Configuration:\n"
              << "  VFS archive: " << configuration.vfs_path.string() << '\n'
              << "  Startup script: ";
    if (configuration.startup_script) {
        std::cout << configuration.startup_script->string() << '\n';
    } else {
        std::cout << "(not specified)\n";
    }

    if (configuration.startup_script) {
        std::ifstream script(*configuration.startup_script);
        if (!script) {
            std::cerr << "Error: cannot open startup script: "
                      << configuration.startup_script->string() << '\n';
            return 1;
        }
        if (!run_stream(script, true, false)) return 0;
        if (!standard_input_is_terminal()) return 0;
    }

    run_stream(std::cin, false, true);
    return 0;
}
