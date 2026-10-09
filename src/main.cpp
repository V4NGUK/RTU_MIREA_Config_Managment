#include "shell.hpp"

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
#include <fcntl.h>
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

bool standard_input_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}
}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Preserve file bytes and CRLF when tac/rev use the console streams.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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

    VirtualFileSystem vfs;
    std::string error;
    if (!vfs.load_zip(configuration.vfs_path, error)) {
        std::cerr << "Error: " << error << '\n';
        return 1;
    }

    Shell shell(vfs, std::cin, std::cout, std::cerr,
                get_current_username() + "@" + get_current_hostname());
    if (configuration.startup_script) {
        std::ifstream script(*configuration.startup_script);
        if (!script) {
            std::cerr << "Error: cannot open startup script: "
                      << configuration.startup_script->string() << '\n';
            return 1;
        }
        if (!shell.run(script, true)) return 0;
        if (!standard_input_is_terminal()) return 0;
    }

    shell.run(std::cin, false);
    return 0;
}
