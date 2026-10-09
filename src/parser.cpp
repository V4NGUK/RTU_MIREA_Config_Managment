#include "parser.hpp"

#include <cctype>
#include <cstdlib>

bool CommandParser::parse(const std::string& line,
                          std::vector<std::string>& arguments,
                          std::string& error) {
    arguments.clear();
    error.clear();
    std::string token;
    char quote = 0;
    bool started = false;
    const auto emit = [&] {
        if (started) arguments.push_back(token);
        token.clear();
        started = false;
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (block_comment_) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                block_comment_ = false;
                ++i;
            }
            continue;
        }
        if (!quote && c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            emit();
            block_comment_ = true;
            ++i;
            continue;
        }
        if (!quote && !started && (c == '#' ||
            (c == '/' && i + 1 < line.size() && line[i + 1] == '/'))) break;
        if (c == '\\' && quote != '\'' && i + 1 < line.size()) {
            const char next = line[i + 1];
            if (next == '\\' || next == '$' || next == '"' || next == '\'' || next == '#' ||
                std::isspace(static_cast<unsigned char>(next))) {
                token.push_back(next);
                started = true;
                ++i;
                continue;
            }
        }
        if (c == '\'' || c == '"') {
            if (!quote) { quote = c; started = true; continue; }
            if (quote == c) { quote = 0; continue; }
        }
        if (c == '$' && quote != '\'') {
            std::size_t end = i + 1;
            std::string name;
            if (end < line.size() && line[end] == '{') {
                const auto closing = line.find('}', end + 1);
                if (closing == std::string::npos) {
                    error = "unclosed environment variable expression";
                    return false;
                }
                name = line.substr(end + 1, closing - end - 1);
                end = closing + 1;
            } else {
                const auto begin = end;
                while (end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
                name = line.substr(begin, end - begin);
            }
            if (!name.empty()) {
                if (const char* value = std::getenv(name.c_str())) {
                    for (; *value; ++value) {
                        if (!quote && std::isspace(static_cast<unsigned char>(*value))) emit();
                        else { token.push_back(*value); started = true; }
                    }
                }
                i = end - 1;
                continue;
            }
        }
        if (!quote && std::isspace(static_cast<unsigned char>(c))) { emit(); continue; }
        token.push_back(c);
        started = true;
    }
    if (quote) { error = "unmatched quote"; return false; }
    emit();
    return true;
}
