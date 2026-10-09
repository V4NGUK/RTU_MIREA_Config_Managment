#include "vfs.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>

#include <zlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014b50;
constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034b50;
constexpr std::size_t kMaximumArchiveSize = 512u * 1024u * 1024u;
constexpr std::size_t kMaximumFileSize = 256u * 1024u * 1024u;

bool has_range(const std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

bool read_u16(const std::vector<std::uint8_t>& bytes,
              std::size_t offset,
              std::uint16_t& value) {
    if (!has_range(bytes, offset, 2)) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(bytes[offset + 1] << 8);
    return true;
}

bool read_u32(const std::vector<std::uint8_t>& bytes,
              std::size_t offset,
              std::uint32_t& value) {
    if (!has_range(bytes, offset, 4)) return false;
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    return true;
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

std::vector<std::uint8_t> timestamp_extra(std::int64_t modified, std::int64_t accessed, bool local) {
    std::vector<std::uint8_t> extra;
    append_u16(extra, 0x5455); // Standard extended timestamps, Unix seconds.
    append_u16(extra, local ? 9 : 5);
    extra.push_back(local ? 3 : 1);
    append_u32(extra, static_cast<std::uint32_t>(modified));
    if (local) append_u32(extra, static_cast<std::uint32_t>(accessed));
    return extra;
}

std::pair<std::uint16_t, std::uint16_t> dos_timestamp(std::int64_t seconds) {
    const auto time = static_cast<std::time_t>(seconds);
    const auto* local = std::localtime(&time);
    if (!local || local->tm_year < 80 || local->tm_year > 207) return {0, 0x21};
    return {static_cast<std::uint16_t>((local->tm_hour << 11) | (local->tm_min << 5) | (local->tm_sec / 2)),
            static_cast<std::uint16_t>(((local->tm_year - 80) << 9) | ((local->tm_mon + 1) << 5) | local->tm_mday)};
}

std::int64_t unix_timestamp(std::uint16_t time, std::uint16_t date) {
    if (!date) return 0;
    std::tm parts{};
    parts.tm_year = ((date >> 9) & 127) + 80;
    parts.tm_mon = ((date >> 5) & 15) - 1;
    parts.tm_mday = date & 31;
    parts.tm_hour = time >> 11;
    parts.tm_min = (time >> 5) & 63;
    parts.tm_sec = (time & 31) * 2;
    parts.tm_isdst = -1;
    const auto result = std::mktime(&parts);
    return result < 0 ? 0 : static_cast<std::int64_t>(result);
}

bool read_timestamps(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length,
                     bool local, std::int64_t& modified, std::int64_t& accessed, std::string& error) {
    const auto end = offset + length;
    if (!has_range(bytes, offset, length)) { error = "invalid ZIP extra fields"; return false; }
    while (offset < end) {
        std::uint16_t id = 0, size = 0;
        if (end - offset < 4 || !read_u16(bytes, offset, id) || !read_u16(bytes, offset + 2, size) ||
            size > end - offset - 4) { error = "truncated ZIP extra field"; return false; }
        offset += 4;
        if (id == 0x5455 && size) {
            const auto flags = bytes[offset];
            std::size_t position = offset + 1;
            for (unsigned flag : {1u, 2u, 4u}) {
                if (!local && flag != 1) break; // Central directory carries only mtime.
                if (!(flags & flag)) continue;
                std::uint32_t value = 0;
                if (position + 4 > offset + size || !read_u32(bytes, position, value)) {
                    error = "truncated ZIP timestamp";
                    return false;
                }
                if (flag == 1) modified = value;
                if (flag == 2) accessed = value;
                position += 4;
            }
        }
        offset += size;
    }
    return true;
}

bool normalize_archive_path(std::string path,
                            std::string& normalized,
                            bool& is_directory,
                            std::string& error) {
    std::replace(path.begin(), path.end(), '\\', '/');
    is_directory = !path.empty() && path.back() == '/';

    if (path.empty() || path.find('\0') != std::string::npos || path.front() == '/' ||
        (path.size() >= 2 && path[1] == ':')) {
        error = "archive contains an absolute or empty entry path";
        return false;
    }

    normalized.clear();
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t length = (end == std::string::npos ? path.size() : end) - start;
        const std::string_view component(path.data() + start, length);

        if (component == "..") {
            error = "archive contains a path that escapes the virtual root";
            return false;
        }
        if (!component.empty() && component != ".") {
            if (!normalized.empty()) normalized.push_back('/');
            normalized.append(component.data(), component.size());
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }

    if (normalized.empty() && !is_directory) {
        error = "archive contains an invalid root entry";
        return false;
    }
    return true;
}

bool add_entry(std::map<std::string, VirtualFileSystem::Entry>& entries,
               const std::string& path,
               VirtualFileSystem::Entry value,
               std::string& error) {
    std::size_t separator = path.find('/');
    while (separator != std::string::npos) {
        const std::string parent = path.substr(0, separator);
        auto parent_entry = entries.find(parent);
        if (parent_entry != entries.end() && !parent_entry->second.is_directory) {
            error = "archive entry has a file as a parent: " + parent;
            return false;
        }
        entries.emplace(parent, VirtualFileSystem::Entry{true, {}, 0755});
        separator = path.find('/', separator + 1);
    }

    auto existing = entries.find(path);
    if (existing != entries.end()) {
        if (value.is_directory && existing->second.is_directory) {
            existing->second = std::move(value);
            return true;
        }
        error = "archive contains duplicate or conflicting entries: " + path;
        return false;
    }

    entries.emplace(path, std::move(value));
    return true;
}

std::uint32_t calculate_crc(const std::vector<std::uint8_t>& data) {
    uLong crc = crc32(0L, Z_NULL, 0);
    if (!data.empty()) {
        crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    }
    return static_cast<std::uint32_t>(crc);
}

bool inflate_raw(const std::uint8_t* input,
                 std::size_t input_size,
                 std::size_t output_size,
                 std::vector<std::uint8_t>& output,
                 std::string& error) {
    output.assign(std::max<std::size_t>(output_size, 1), 0);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input));
    stream.avail_in = static_cast<uInt>(input_size);
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = "cannot initialize ZIP decompressor";
        return false;
    }
    const int result = inflate(&stream, Z_FINISH);
    const std::size_t bytes_written = static_cast<std::size_t>(stream.total_out);
    const std::size_t bytes_read = static_cast<std::size_t>(stream.total_in);
    inflateEnd(&stream);

    if (result != Z_STREAM_END || bytes_written != output_size || bytes_read != input_size) {
        error = "ZIP entry has invalid compressed data or an unexpected size";
        return false;
    }
    output.resize(output_size);
    return true;
}

bool build_zip(const std::map<std::string, VirtualFileSystem::Entry>& entries,
               std::vector<std::uint8_t>& bytes,
               std::string& error) {
    struct CentralEntry {
        std::string name;
        bool is_directory;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t local_offset;
        std::uint16_t mode;
        std::int64_t modified;
        std::int64_t accessed;
    };

    std::vector<CentralEntry> central_entries;
    for (const auto& item : entries) {
        if (central_entries.size() >= std::numeric_limits<std::uint16_t>::max() - 1u) {
            error = "cannot save more than 65534 ZIP entries";
            return false;
        }

        std::string name = item.first.empty() ? "." : item.first;
        if (item.second.is_directory) name.push_back('/');
        const auto& data = item.second.data;
        if (name.size() > std::numeric_limits<std::uint16_t>::max() ||
            data.size() > std::numeric_limits<std::uint32_t>::max() ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "VFS is too large for a standard ZIP archive";
            return false;
        }

        CentralEntry entry{name,
                           item.second.is_directory,
                           calculate_crc(data),
                           static_cast<std::uint32_t>(data.size()),
                           static_cast<std::uint32_t>(bytes.size()),
                           item.second.mode, item.second.modified, item.second.accessed};
        const auto extra = timestamp_extra(entry.modified, entry.accessed, true);
        const auto stamp = dos_timestamp(entry.modified);

        append_u32(bytes, kLocalFileHeaderSignature);
        append_u16(bytes, 20);       // version needed
        append_u16(bytes, 0x0800);   // UTF-8 file names
        append_u16(bytes, 0);        // stored, no compression
        append_u16(bytes, stamp.first);
        append_u16(bytes, stamp.second);
        append_u32(bytes, entry.crc);
        append_u32(bytes, entry.size);
        append_u32(bytes, entry.size);
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, static_cast<std::uint16_t>(extra.size()));
        bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
        bytes.insert(bytes.end(), extra.begin(), extra.end());
        bytes.insert(bytes.end(), data.begin(), data.end());
        central_entries.push_back(std::move(entry));
    }

    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "VFS is too large for a standard ZIP archive";
        return false;
    }
    const std::uint32_t central_offset = static_cast<std::uint32_t>(bytes.size());

    for (const auto& entry : central_entries) {
        const auto extra = timestamp_extra(entry.modified, entry.accessed, false);
        const auto stamp = dos_timestamp(entry.modified);
        append_u32(bytes, kCentralDirectorySignature);
        append_u16(bytes, 0x0314);   // created by Unix, ZIP 2.0
        append_u16(bytes, 20);
        append_u16(bytes, 0x0800);
        append_u16(bytes, 0);
        append_u16(bytes, stamp.first);
        append_u16(bytes, stamp.second);
        append_u32(bytes, entry.crc);
        append_u32(bytes, entry.size);
        append_u32(bytes, entry.size);
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, static_cast<std::uint16_t>(extra.size()));
        append_u16(bytes, 0);        // comment length
        append_u16(bytes, 0);        // disk number
        append_u16(bytes, 0);        // internal attributes
        const auto unix_mode = static_cast<std::uint32_t>(entry.mode | (entry.is_directory ? 0040000 : 0100000));
        append_u32(bytes, (unix_mode << 16) | (entry.is_directory ? 0x10u : 0u));
        append_u32(bytes, entry.local_offset);
        bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
        bytes.insert(bytes.end(), extra.begin(), extra.end());
    }

    if (bytes.size() - central_offset > std::numeric_limits<std::uint32_t>::max()) {
        error = "VFS central directory is too large for a standard ZIP archive";
        return false;
    }
    const auto central_size = static_cast<std::uint32_t>(bytes.size() - central_offset);
    append_u32(bytes, kEndOfCentralDirectorySignature);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, static_cast<std::uint16_t>(central_entries.size()));
    append_u16(bytes, static_cast<std::uint16_t>(central_entries.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0);  // archive comment length
    return true;
}

bool write_archive_atomically(const std::filesystem::path& destination,
                              const std::vector<std::uint8_t>& bytes,
                              std::string& error) {
    if (destination.empty() || destination.filename().empty()) {
        error = "destination path must name a file";
        return false;
    }

    std::filesystem::path staging;
    bool reserved = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        staging = destination;
        staging += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                   "-" + std::to_string(attempt);
        std::error_code create_error;
        if (std::filesystem::create_directory(staging, create_error)) { reserved = true; break; }
        if (create_error && create_error != std::errc::file_exists) {
            error = "cannot create output file: " + create_error.message();
            return false;
        }
    }
    if (!reserved) { error = "cannot reserve temporary output directory"; return false; }
    const auto temporary = staging / "archive.zip";
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(staging, ignored);
    };
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot open temporary output file: " + temporary.string();
            cleanup();
            return false;
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        output.flush();
        output.close();
        if (!output) {
            error = "failed while writing ZIP archive: " + temporary.string();
            cleanup();
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        cleanup();
        error = "cannot replace destination ZIP archive (Windows error " +
                std::to_string(code) + "): " + destination.string();
        return false;
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, destination, rename_error);
    if (rename_error) {
        cleanup();
        error = "cannot replace destination ZIP archive: " + rename_error.message();
        return false;
    }
#endif
    cleanup();
    return true;
}

bool normalize_virtual_path(std::string path, std::string& normalized) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    while (!path.empty() && path.back() == '/') path.pop_back();

    normalized.clear();
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t length = (end == std::string::npos ? path.size() : end) - start;
        const std::string_view component(path.data() + start, length);
        if (component == "..") return false;
        if (!component.empty() && component != ".") {
            if (!normalized.empty()) normalized.push_back('/');
            normalized.append(component.data(), component.size());
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
}  // namespace

bool VirtualFileSystem::load_zip(const std::filesystem::path& archive_path,
                                 std::string& error) {
    std::ifstream input(archive_path, std::ios::binary);
    if (!input) {
        error = "cannot open VFS ZIP archive: " + archive_path.string();
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size < 22 || static_cast<std::uint64_t>(file_size) > kMaximumArchiveSize) {
        error = "VFS archive is empty, truncated, or larger than 512 MiB";
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!input) {
        error = "cannot read VFS ZIP archive: " + archive_path.string();
        return false;
    }

    const std::size_t earliest_eocd = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    std::size_t eocd_offset = std::string::npos;
    for (std::size_t offset = bytes.size() - 22;; --offset) {
        std::uint32_t signature = 0;
        if (read_u32(bytes, offset, signature) &&
            signature == kEndOfCentralDirectorySignature) {
            std::uint16_t comment_length = 0;
            if (read_u16(bytes, offset + 20, comment_length) &&
                offset + 22u + comment_length == bytes.size()) {
                eocd_offset = offset;
                break;
            }
        }
        if (offset == earliest_eocd) break;
    }
    if (eocd_offset == std::string::npos) {
        error = "VFS input is not a valid ZIP archive";
        return false;
    }

    std::uint16_t disk_number = 0, central_disk = 0;
    std::uint16_t disk_entries = 0, entry_count = 0;
    std::uint32_t central_size = 0, central_offset = 0;
    if (!read_u16(bytes, eocd_offset + 4, disk_number) ||
        !read_u16(bytes, eocd_offset + 6, central_disk) ||
        !read_u16(bytes, eocd_offset + 8, disk_entries) ||
        !read_u16(bytes, eocd_offset + 10, entry_count) ||
        !read_u32(bytes, eocd_offset + 12, central_size) ||
        !read_u32(bytes, eocd_offset + 16, central_offset)) {
        error = "VFS ZIP end record is truncated";
        return false;
    }
    if (disk_number != 0 || central_disk != 0 || disk_entries != entry_count ||
        entry_count == std::numeric_limits<std::uint16_t>::max() ||
        central_size == std::numeric_limits<std::uint32_t>::max() ||
        central_offset == std::numeric_limits<std::uint32_t>::max()) {
        error = "multi-disk and ZIP64 archives are not supported";
        return false;
    }
    if (!has_range(bytes, central_offset, central_size) ||
        static_cast<std::size_t>(central_offset) + central_size > eocd_offset) {
        error = "VFS ZIP central directory is outside the archive";
        return false;
    }

    std::map<std::string, Entry> loaded_entries;
    loaded_entries.emplace("", Entry{true, {}, 0755});
    std::size_t cursor = central_offset;
    std::size_t total_uncompressed = 0;

    for (std::uint32_t index = 0; index < entry_count; ++index) {
        std::uint32_t signature = 0;
        if (!read_u32(bytes, cursor, signature) ||
            signature != kCentralDirectorySignature || !has_range(bytes, cursor, 46)) {
            error = "VFS ZIP central directory contains an invalid entry";
            return false;
        }

        std::uint16_t flags = 0, method = 0;
        std::uint16_t creator = 0, dos_time = 0, dos_date = 0;
        std::uint32_t attributes = 0;
        std::uint32_t expected_crc = 0, compressed_size = 0;
        std::uint32_t uncompressed_size = 0, local_offset = 0;
        std::uint16_t name_length = 0, extra_length = 0, comment_length = 0;
        if (!read_u16(bytes, cursor + 4, creator) ||
            !read_u16(bytes, cursor + 12, dos_time) || !read_u16(bytes, cursor + 14, dos_date) ||
            !read_u32(bytes, cursor + 38, attributes) ||
            !read_u16(bytes, cursor + 8, flags) ||
            !read_u16(bytes, cursor + 10, method) ||
            !read_u32(bytes, cursor + 16, expected_crc) ||
            !read_u32(bytes, cursor + 20, compressed_size) ||
            !read_u32(bytes, cursor + 24, uncompressed_size) ||
            !read_u16(bytes, cursor + 28, name_length) ||
            !read_u16(bytes, cursor + 30, extra_length) ||
            !read_u16(bytes, cursor + 32, comment_length) ||
            !read_u32(bytes, cursor + 42, local_offset)) {
            error = "VFS ZIP central directory contains a truncated entry";
            return false;
        }
        const std::size_t record_size = 46u + name_length + extra_length + comment_length;
        if (!has_range(bytes, cursor, record_size)) {
            error = "VFS ZIP central directory entry exceeds the archive bounds";
            return false;
        }
        if (compressed_size == std::numeric_limits<std::uint32_t>::max() ||
            uncompressed_size == std::numeric_limits<std::uint32_t>::max()) {
            error = "ZIP64 entries are not supported";
            return false;
        }
        if ((flags & 1u) != 0) {
            error = "encrypted ZIP entries are not supported";
            return false;
        }
        if (method != 0 && method != 8) {
            error = "ZIP entry uses an unsupported compression method";
            return false;
        }

        std::string raw_name(reinterpret_cast<const char*>(bytes.data() + cursor + 46),
                             name_length);
        std::string path;
        bool is_directory = false;
        if (!normalize_archive_path(raw_name, path, is_directory, error)) return false;
        const auto unix_mode = static_cast<std::uint16_t>(attributes >> 16);
        const bool unix_attributes = (creator >> 8) == 3 && unix_mode != 0;
        const auto type = unix_attributes ? unix_mode & 0170000 : 0;
        if (type != 0 && type != 0040000 && type != 0100000) {
            error = "ZIP links and special files are not supported: " + path;
            return false;
        }
        is_directory = is_directory || type == 0040000 || (attributes & 0x10);
        const auto mode = static_cast<std::uint16_t>(unix_attributes ? unix_mode & 07777 :
                                                    (is_directory ? 0755 : 0644));

        if (uncompressed_size > kMaximumFileSize ||
            total_uncompressed > kMaximumArchiveSize - uncompressed_size) {
            error = "VFS expands beyond the 512 MiB in-memory limit";
            return false;
        }
        total_uncompressed += uncompressed_size;

        std::uint32_t local_signature = 0;
        std::uint16_t local_name_length = 0, local_extra_length = 0;
        if (!read_u32(bytes, local_offset, local_signature) ||
            local_signature != kLocalFileHeaderSignature ||
            !read_u16(bytes, static_cast<std::size_t>(local_offset) + 26,
                      local_name_length) ||
            !read_u16(bytes, static_cast<std::size_t>(local_offset) + 28,
                      local_extra_length)) {
            error = "VFS ZIP local file header is invalid";
            return false;
        }
        const std::size_t data_offset = static_cast<std::size_t>(local_offset) + 30u +
                                        local_name_length + local_extra_length;
        if (!has_range(bytes, data_offset, compressed_size)) {
            error = "VFS ZIP compressed data exceeds the archive bounds";
            return false;
        }

        std::vector<std::uint8_t> data;
        if (method == 0) {
            if (compressed_size != uncompressed_size) {
                error = "stored ZIP entry has inconsistent compressed and original sizes";
                return false;
            }
            data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + compressed_size));
        } else if (!inflate_raw(bytes.data() + data_offset,
                                compressed_size,
                                uncompressed_size,
                                data,
                                error)) {
            return false;
        }

        if (calculate_crc(data) != expected_crc) {
            error = "VFS ZIP entry failed its CRC check: " + path;
            return false;
        }
        if (is_directory && !data.empty()) {
            error = "VFS ZIP directory entry contains file data: " + path;
            return false;
        }
        auto modified = unix_timestamp(dos_time, dos_date);
        auto accessed = modified;
        if (!read_timestamps(bytes, static_cast<std::size_t>(local_offset) + 30 + local_name_length,
                             local_extra_length, true, modified, accessed, error) ||
            !read_timestamps(bytes, cursor + 46 + name_length, extra_length,
                             false, modified, accessed, error)) return false;
        if (!add_entry(loaded_entries, path, Entry{is_directory, std::move(data), mode, modified, accessed}, error)) {
            return false;
        }
        cursor += record_size;
    }

    if (cursor != static_cast<std::size_t>(central_offset) + central_size) {
        error = "VFS ZIP central directory size does not match its entries";
        return false;
    }

    entries_ = std::move(loaded_entries);
    return true;
}

bool VirtualFileSystem::save_zip(const std::filesystem::path& archive_path,
                                 std::string& error) const {
    std::vector<std::uint8_t> bytes;
    if (!build_zip(entries_, bytes, error)) return false;
    return write_archive_atomically(archive_path, bytes, error);
}

bool VirtualFileSystem::initialize(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files, std::string& error) {
    std::map<std::string, Entry> loaded;
    loaded.emplace("", Entry{true, {}, 0755});
    for (const auto& file : files) {
        std::string path;
        bool directory = false;
        if (!normalize_archive_path(file.first, path, directory, error)) return false;
        Entry value{directory, file.second, static_cast<std::uint16_t>(directory ? 0755 : 0644)};
        if (!add_entry(loaded, path, std::move(value), error)) return false;
    }
    entries_ = std::move(loaded);
    return true;
}

bool VirtualFileSystem::contains(const std::string& virtual_path) const {
    std::string normalized;
    if (!normalize_virtual_path(virtual_path, normalized)) return false;
    return entries_.find(normalized) != entries_.end();
}

bool VirtualFileSystem::is_directory(const std::string& virtual_path) const {
    std::string normalized;
    if (!normalize_virtual_path(virtual_path, normalized)) return false;
    const auto entry = entries_.find(normalized);
    return entry != entries_.end() && entry->second.is_directory;
}

std::vector<std::pair<std::string, bool>> VirtualFileSystem::list_directory(
    const std::string& virtual_path) const {
    std::vector<std::pair<std::string, bool>> result;
    std::string normalized;
    if (!normalize_virtual_path(virtual_path, normalized)) return result;
    const auto directory = entries_.find(normalized);
    if (directory == entries_.end() || !directory->second.is_directory) return result;

    const std::string prefix = normalized.empty() ? "" : normalized + "/";
    for (auto iterator = entries_.lower_bound(prefix);
         iterator != entries_.end() && iterator->first.compare(0, prefix.size(), prefix) == 0;
         ++iterator) {
        const std::string_view remainder(iterator->first.data() + prefix.size(),
                                         iterator->first.size() - prefix.size());
        if (remainder.empty() || remainder.find('/') != std::string_view::npos) continue;
        result.emplace_back(std::string(remainder), iterator->second.is_directory);
    }
    return result;
}
