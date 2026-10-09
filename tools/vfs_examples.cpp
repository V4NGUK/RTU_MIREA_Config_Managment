#include "vfs.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using Bytes = std::vector<std::uint8_t>;
using Files = std::vector<std::pair<std::string, Bytes>>;

Bytes text(const std::string& value) { return {value.begin(), value.end()}; }

void save_example(const std::filesystem::path& path, const Files& files) {
    VirtualFileSystem vfs;
    std::string error;
    if (!vfs.initialize(files, error) || !vfs.save_zip(path, error)) throw std::runtime_error(error);
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: vfs_examples OUTPUT_DIRECTORY");
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        save_example(output / "minimal.zip", {{"README.txt", text("Minimal VFS fixture\n")}});
        save_example(output / "multiple-files.zip", {
            {"README.txt", text("Several files and directories\n")},
            {"home/alex/notes.txt", text("Notes stored in the virtual home directory.\n")},
            {"bin/greeting.txt", text("Hello from the virtual bin directory.\n")},
            {"bin/sample.bin", Bytes{0, 1, 127, 128, 255, 0, 42}},
            {"projects/demo/config.ini", text("[demo]\nmode=memory\n")}
        });
        save_example(output / "nested.zip", {
            {"home/alex/documents/semester/configuration/brief.txt", text("A deeply nested file.\n")},
            {"home/alex/projects/shell/src/main.cpp", text("int main() { return 0; }\n")}
        });
        save_example(output / "commands.zip", {
            {"lines.txt", text("one\ntwo\nthree\n")},
            {"no-newline.txt", text("one\ntwo")},
            {"utf8.txt", text(u8"Привет\nмир\r\n")},
            {"home/alex/notes.txt", text("first\nsecond\nthird\n")},
            {"space name.txt", text("with spaces\n")},
            {".hidden", text("hidden\n")},
            {"empty.txt", {}}, {"empty/", {}}
        });
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
