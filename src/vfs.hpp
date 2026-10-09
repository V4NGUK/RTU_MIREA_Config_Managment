#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

// A ZIP-backed virtual filesystem. Files stay in memory as byte vectors; the
// archive is never extracted to the host filesystem.
class VirtualFileSystem {
public:
    struct Entry {
        bool is_directory = false;
        std::vector<std::uint8_t> data;
        std::uint16_t mode = 0644;
        std::int64_t modified = 0;
        std::int64_t accessed = 0;
    };

    bool load_zip(const std::filesystem::path& archive_path, std::string& error);
    bool save_zip(const std::filesystem::path& archive_path, std::string& error) const;
    // In-memory fixture construction; the emulator itself loads its VFS from ZIP.
    bool initialize(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
                    std::string& error);

    bool resolve(const std::string& path, const std::string& cwd, std::string& absolute,
                 std::string& error, bool allow_missing_leaf = false) const;
    const Entry* entry(const std::string& absolute) const;
    bool read_file(const std::string& absolute, std::string& data, std::string& error) const;
    bool can_access(const std::string& absolute, unsigned rights, std::string& error) const;
    bool touch(const std::string& path, const std::string& cwd, bool no_create,
               bool access_time, bool modification_time, std::string& error);
    bool chmod(const std::string& path, const std::string& cwd, const std::string& mode,
               bool recursive, std::string& error);

    bool contains(const std::string& virtual_path) const;
    bool is_directory(const std::string& virtual_path) const;
    std::vector<std::pair<std::string, bool>> list_directory(
        const std::string& virtual_path) const;

private:
    // Paths are relative to the virtual root, use '/' separators, and are
    // stored without a trailing slash. The empty path represents the root.
    std::map<std::string, Entry> entries_;
};
