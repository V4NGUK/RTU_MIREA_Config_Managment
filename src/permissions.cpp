#include "permissions.hpp"

#include <cctype>

bool parse_mode(const std::string& specification, std::uint16_t current, bool directory,
                std::uint16_t& result, std::string& error) {
    const auto invalid = [&] { error = "invalid permission mode: " + specification; return false; };
    if (specification.empty()) return invalid();
    if (std::isdigit(static_cast<unsigned char>(specification.front()))) {
        if (specification.size() > 4) return invalid();
        unsigned value = 0;
        for (char c : specification) {
            if (c < '0' || c > '7') return invalid();
            value = value * 8 + static_cast<unsigned>(c - '0');
        }
        result = static_cast<std::uint16_t>(value);
        return true;
    }

    unsigned mode = current;
    std::size_t i = 0;
    while (i < specification.size()) {
        unsigned classes = 0;
        while (i < specification.size()) {
            const char c = specification[i];
            if (c == 'u') classes |= 4;
            else if (c == 'g') classes |= 2;
            else if (c == 'o') classes |= 1;
            else if (c == 'a') classes |= 7;
            else break;
            ++i;
        }
        if (!classes) classes = 7; // No host umask: all entries belong to one virtual owner.
        bool had_operator = false;
        while (i < specification.size() && specification[i] != ',') {
            const char operation = specification[i++];
            if (operation != '+' && operation != '-' && operation != '=') return invalid();
            had_operator = true;
            unsigned permissions = 0, special = 0;
            while (i < specification.size()) {
                const char c = specification[i];
                if (c == ',' || c == '+' || c == '-' || c == '=') break;
                if (c == 'r') permissions |= 4;
                else if (c == 'w') permissions |= 2;
                else if (c == 'x') permissions |= 1;
                else if (c == 'X') { if (directory || (mode & 0111)) permissions |= 1; }
                else if (c == 'u') permissions |= (mode >> 6) & 7;
                else if (c == 'g') permissions |= (mode >> 3) & 7;
                else if (c == 'o') permissions |= mode & 7;
                else if (c == 's') {
                    if (classes & 4) special |= 04000;
                    if (classes & 2) special |= 02000;
                } else if (c == 't') { if (classes & 1) special |= 01000; }
                else return invalid();
                ++i;
            }
            unsigned mask = 0, bits = special;
            if (classes & 4) { mask |= 04700; bits |= permissions << 6; }
            if (classes & 2) { mask |= 02070; bits |= permissions << 3; }
            if (classes & 1) { mask |= 01007; bits |= permissions; }
            if (operation == '=') mode = (mode & ~mask) | bits;
            else if (operation == '+') mode |= bits;
            else mode &= ~bits;
        }
        if (!had_operator) return invalid();
        if (i < specification.size() && ++i == specification.size()) return invalid();
    }
    result = static_cast<std::uint16_t>(mode);
    return true;
}

std::string format_mode(std::uint16_t mode, bool directory) {
    std::string result = directory ? "drwxrwxrwx" : "-rwxrwxrwx";
    for (unsigned bit = 0; bit < 9; ++bit) if (!(mode & (1u << (8 - bit)))) result[bit + 1] = '-';
    if (mode & 04000) result[3] = mode & 0100 ? 's' : 'S';
    if (mode & 02000) result[6] = mode & 0010 ? 's' : 'S';
    if (mode & 01000) result[9] = mode & 0001 ? 't' : 'T';
    return result;
}
