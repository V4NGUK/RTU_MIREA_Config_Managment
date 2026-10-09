#include "shell.hpp"
#include "text_commands.hpp"

#include <cctype>
#include <cstdlib>
#include <iterator>
#include <utility>

Shell::Shell(VirtualFileSystem& vfs, std::istream& input, std::ostream& output,
             std::ostream& errors, std::string identity)
    : vfs_(vfs), input_(input), output_(output), errors_(errors), identity_(std::move(identity)) {}

std::string Shell::prompt() const { return identity_ + ":" + cwd_ + "$ "; }

void Shell::error(const std::string& command, const std::string& message) {
    errors_ << command + ": " + message + "\n";
}

bool Shell::execute_line(const std::string& line) {
    std::vector<std::string> arguments;
    std::string message;
    if (!parser_.parse(line, arguments, message)) { error("syntax", message); return true; }
    return execute(arguments);
}

bool Shell::run(std::istream& commands, bool echo_input) {
    std::string line;
    while (true) {
        if (!echo_input) output_ << prompt() << std::flush;
        if (!std::getline(commands, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (echo_input && !line.empty()) output_ << prompt() << line << '\n' << std::flush;
        if (!execute_line(line)) return false;
    }
    if (parser_.inside_comment()) error("syntax", "unterminated block comment");
    if (commands.bad()) error("input", "failed to read command stream");
    return true;
}

void Shell::list(const std::vector<std::string>& arguments) {
    bool all = false;
    bool options = true;
    std::vector<std::string> paths;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        if (options && arguments[i] == "--") { options = false; continue; }
        if (options && arguments[i].size() > 1 && arguments[i][0] == '-') {
            for (std::size_t j = 1; j < arguments[i].size(); ++j) {
                if (arguments[i][j] == 'a') all = true;
                else { error("ls", "unsupported option: " + arguments[i]); return; }
            }
        } else paths.push_back(arguments[i]);
    }
    if (paths.empty()) paths.push_back(".");
    for (const auto& path : paths) {
        std::string absolute, message;
        if (!vfs_.resolve(path, cwd_, absolute, message)) { error("ls", message); continue; }
        const auto* item = vfs_.entry(absolute);
        if (!item->is_directory) { output_ << path << '\n'; continue; }
        if (paths.size() > 1) output_ << path << ":\n";
        if (all) output_ << ".\n..\n";
        for (const auto& child : vfs_.list_directory(absolute)) {
            if (!all && child.first.front() == '.') continue;
            output_ << child.first << '\n';
        }
    }
}

void Shell::change_directory(const std::vector<std::string>& arguments) {
    std::size_t first = arguments.size() > 1 && arguments[1] == "--" ? 2 : 1;
    if (arguments.size() > first + 1) { error("cd", "usage: cd [path|-]"); return; }
    std::string target = arguments.size() == first ? "/" : arguments[first];
    const bool previous = target == "-";
    if (previous) target = previous_;
    else if (target == "~") target = "/";
    else if (target.compare(0, 2, "~/") == 0) target.erase(0, 1);
    std::string absolute, message;
    if (!vfs_.resolve(target, cwd_, absolute, message)) { error("cd", message); return; }
    if (!vfs_.is_directory(absolute)) { error("cd", "not a directory: " + target); return; }
    previous_ = cwd_;
    cwd_ = absolute;
    if (previous) output_ << cwd_ << '\n';
}

void Shell::text_command(const std::vector<std::string>& arguments) {
    std::vector<std::string> paths;
    bool options = true;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        if (options && arguments[i] == "--") { options = false; continue; }
        if (options && arguments[i].size() > 1 && arguments[i][0] == '-') {
            error(arguments[0], "unsupported option: " + arguments[i]);
            return;
        }
        paths.push_back(arguments[i]);
    }
    if (paths.empty()) paths.push_back("-");
    for (const auto& path : paths) {
        std::string content, message, absolute;
        if (path == "-") {
            content.assign(std::istreambuf_iterator<char>(input_), std::istreambuf_iterator<char>());
            if (input_.bad()) { error(arguments[0], "failed to read standard input"); continue; }
        } else if (!vfs_.resolve(path, cwd_, absolute, message) ||
                   !vfs_.read_file(absolute, content, message)) {
            error(arguments[0], message);
            continue;
        }
        if (arguments[0] == "tac") output_ << reverse_lines(content);
        else {
            std::string reversed;
            if (!reverse_characters(content, reversed, message)) error("rev", message + ": " + path);
            else output_ << reversed;
        }
    }
}

bool Shell::execute(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return true;
    const auto& command = arguments[0];
    if (command == "exit") {
        if (arguments.size() != 1) { error(command, "usage: exit"); return true; }
        return false;
    }
    if (command == "ls") { list(arguments); return true; }
    if (command == "cd") { change_directory(arguments); return true; }
    if (command == "tac" || command == "rev") { text_command(arguments); return true; }
    if (command == "echo") {
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            if (i > 1) output_ << ' ';
            output_ << arguments[i];
        }
        output_ << '\n';
        return true;
    }
    if (command == "regvar") {
        if (arguments.size() < 3) { error(command, "usage: regvar <name> <value>"); return true; }
        const auto& name = arguments[1];
        bool valid = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
        for (const char c : name) valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
        if (!valid) { error(command, "invalid environment variable name"); return true; }
        std::string value = arguments[2];
        for (std::size_t i = 3; i < arguments.size(); ++i) value += " " + arguments[i];
#if defined(_WIN32)
        const int status = _putenv_s(name.c_str(), value.c_str());
#else
        const int status = setenv(name.c_str(), value.c_str(), 1);
#endif
        if (status != 0) error(command, "could not set environment variable");
        return true;
    }
    if (command == "vfs-save") {
        if (arguments.size() != 2) { error(command, "usage: vfs-save <destination.zip>"); return true; }
        std::string message;
        if (!vfs_.save_zip(arguments[1], message)) error(command, message);
        else output_ << "VFS saved to " << arguments[1] << '\n';
        return true;
    }
    error(command, "unknown command");
    return true;
}
