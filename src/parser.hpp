#pragma once

#include <string>
#include <vector>

class CommandParser {
public:
    bool parse(const std::string& line, std::vector<std::string>& arguments, std::string& error);
    bool inside_comment() const { return block_comment_; }

private:
    bool block_comment_ = false;
};
