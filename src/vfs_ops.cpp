#include "vfs.hpp"
#include "permissions.hpp"

#include <chrono>

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
        if (!can_access(resolved, 1, error)) return false;
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
    if (!can_access(absolute, 4, error)) return false;
    data.assign(found->data.begin(), found->data.end());
    return true;
}

bool VirtualFileSystem::can_access(const std::string& absolute,
                                   unsigned rights, std::string& error) const {
    const auto* found = entry(absolute);
    if (!found) { error = "no such file or directory: " + absolute; return false; }
    if (((found->mode >> 6) & rights) != rights) {
        error = "permission denied: " + absolute;
        return false;
    }
    return true;
}

bool VirtualFileSystem::touch(const std::string& path, const std::string& cwd, bool no_create,
                              bool access_time, bool modification_time, std::string& error) {
    std::string absolute;
    if (!resolve(path, cwd, absolute, error, true)) return false;
    auto found = entries_.find(absolute.substr(1));
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (found == entries_.end()) {
        if (no_create) return true;
        const auto slash = absolute.rfind('/');
        const std::string parent = slash == 0 ? "/" : absolute.substr(0, slash);
        if (!can_access(parent, 3, error)) return false;
        entries_.emplace(absolute.substr(1), Entry{false, {}, 0644, now, now});
    } else {
        if (access_time) found->second.accessed = now;
        if (modification_time) found->second.modified = now;
    }
    return true;
}

bool VirtualFileSystem::chmod(const std::string& path, const std::string& cwd,
                              const std::string& mode, bool recursive, std::string& error) {
    std::string absolute;
    if (!resolve(path, cwd, absolute, error)) return false;
    const std::string key = absolute.substr(1);
    auto target = entries_.find(key);
    std::vector<std::pair<Entry*, std::uint16_t>> changes;
    const std::string prefix = key.empty() ? "" : key + "/";
    for (auto& item : entries_) {
        if (item.first != key && !(recursive && target->second.is_directory &&
                                  item.first.compare(0, prefix.size(), prefix) == 0)) continue;
        if (recursive && item.second.is_directory &&
            !can_access("/" + item.first, 5, error)) return false;
        std::uint16_t updated = 0;
        if (!parse_mode(mode, item.second.mode, item.second.is_directory, updated, error)) return false;
        changes.emplace_back(&item.second, updated);
    }
    for (auto& change : changes) change.first->mode = change.second;
    return true;
}
