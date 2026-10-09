#pragma once

#include "parser.hpp"
#include "vfs.hpp"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

class Shell {
public:
    Shell(VirtualFileSystem& vfs, std::istream& input, std::ostream& output,
          std::ostream& errors, std::string identity = "user@localhost");
    bool execute_line(const std::string& line);
    bool run(std::istream& commands, bool echo_input);
    const std::string& current_directory() const { return cwd_; }

private:
    bool execute(const std::vector<std::string>& arguments);
    void list(const std::vector<std::string>& arguments);
    void change_directory(const std::vector<std::string>& arguments);
    void text_command(const std::vector<std::string>& arguments);
    void change_mode(const std::vector<std::string>& arguments);
    void touch(const std::vector<std::string>& arguments);
    void error(const std::string& command, const std::string& message);
    std::string prompt() const;

    VirtualFileSystem& vfs_;
    std::istream& input_;
    std::ostream& output_;
    std::ostream& errors_;
    std::string identity_;
    std::string cwd_ = "/";
    std::string previous_ = "/";
    CommandParser parser_;
};
