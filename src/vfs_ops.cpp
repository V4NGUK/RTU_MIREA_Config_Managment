#include "vfs.hpp"

bool VirtualFileSystem::resolve(const std::string& path, const std::string& cwd,
                                std::string& absolute, std::string& error,
                                bool allow_missing_leaf) const {
    if (path.empty() || path.find('\0') != std::string::npos) {
        error = "empty or invalid virtual path";
        return false;
    }
    std::string resolved = path.front() == '/' ? "/" : cwd;
    std::size_t start = path.front() == '/' ? 1 : 0;
    while (start < path.size()) {
        const auto separator = path.find('/', start);
        const auto end = separator == std::string::npos ? path.size() : separator;
        const auto component = path.substr(start, end - start);
        const auto* parent = entry(resolved);
        if (!parent || !parent->is_directory) {
            error = "not a directory: " + resolved;
            return false;
        }
        if (component == "..") {
            const auto last = resolved.rfind('/');
            resolved = last == 0 ? "/" : resolved.substr(0, last);
        } else if (!component.empty() && component != ".") {
            if (resolved != "/") resolved += '/';
            resolved += component;
        }
        if (!entry(resolved) && !(allow_missing_leaf && end == path.size())) {
            error = "no such file or directory: " + resolved;
            return false;
        }
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    const auto* target = entry(resolved);
    if ((!target && !allow_missing_leaf) ||
        (path.back() == '/' && (!target || !target->is_directory))) {
        error = "not a directory: " + resolved;
        return false;
    }
    absolute = resolved;
    return true;
}

const VirtualFileSystem::Entry* VirtualFileSystem::entry(const std::string& absolute) const {
    if (absolute.empty() || absolute.front() != '/') return nullptr;
    const auto found = entries_.find(absolute.substr(1));
    return found == entries_.end() ? nullptr : &found->second;
}

bool VirtualFileSystem::read_file(const std::string& absolute,
                                 std::string& data, std::string& error) const {
    const auto* found = entry(absolute);
    if (!found) { error = "no such file: " + absolute; return false; }
    if (found->is_directory) { error = "is a directory: " + absolute; return false; }
    data.assign(found->data.begin(), found->data.end());
    return true;
}
