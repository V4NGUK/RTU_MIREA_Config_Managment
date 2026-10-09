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
    };

    bool load_zip(const std::filesystem::path& archive_path, std::string& error);
    bool save_zip(const std::filesystem::path& archive_path, std::string& error) const;
    // In-memory fixture construction; the emulator itself loads its VFS from ZIP.
    bool initialize(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
                    std::string& error);

    bool contains(const std::string& virtual_path) const;
    bool is_directory(const std::string& virtual_path) const;
    std::vector<std::pair<std::string, bool>> list_directory(
        const std::string& virtual_path) const;

private:
    // Paths are relative to the virtual root, use '/' separators, and are
    // stored without a trailing slash. The empty path represents the root.
    std::map<std::string, Entry> entries_;
};
