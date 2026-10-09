#include "permissions.hpp"
#include "shell.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error(reason);
}
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot read fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write_bytes(const std::filesystem::path& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(static_cast<bool>(output), "cannot write test data");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture and output directories");
        const std::filesystem::path fixtures(argv[1]), work(argv[2]);
        std::filesystem::create_directories(work);
        std::string message, contents;
        std::uint16_t mode = 0;
        require(parse_mode("640", 0777, false, mode, message) && mode == 0640, "octal mode");
        require(parse_mode("u=rw,g=r,o=", 0777, false, mode, message) && mode == 0640, "symbolic mode");
        require(parse_mode("g=u", 0640, false, mode, message) && mode == 0660, "copy permission class");
        require(parse_mode("a+X", 0644, false, mode, message) && mode == 0644, "X on non-executable file");
        require(parse_mode("a+X", 0644, true, mode, message) && mode == 0755, "X on directory");
        require(parse_mode("u+s,g+s,o+t", 0755, false, mode, message) && mode == 07755, "special bits");
        require(parse_mode("u+r-w", 0222, false, mode, message) && mode == 0422, "chained operators");
        for (const auto& bad : {"", "999", "u?x", "g", "u+r,", "a+z"}) {
            require(!parse_mode(bad, 0644, false, mode, message), "invalid mode accepted");
        }
        require(format_mode(04700, false) == "-rws------", "special mode formatting");

        const auto original = read_bytes(fixtures / "commands.zip");
        VirtualFileSystem vfs;
        require(vfs.load_zip(fixtures / "commands.zip", message), message);
        const auto before = *vfs.entry("/lines.txt");
        require(vfs.touch("/lines.txt", "/", false, true, false, message), message);
        require(vfs.entry("/lines.txt")->modified == before.modified &&
                vfs.entry("/lines.txt")->accessed > before.accessed, "touch -a timestamps");
        const auto access = vfs.entry("/lines.txt")->accessed;
        require(vfs.touch("/lines.txt", "/", false, false, true, message), message);
        require(vfs.entry("/lines.txt")->accessed == access &&
                vfs.entry("/lines.txt")->modified > before.modified, "touch -m timestamps");
        require(vfs.entry("/lines.txt")->data == before.data, "touch must not truncate");
        require(vfs.touch("/absent", "/", true, true, true, message) && !vfs.entry("/absent"), "touch -c");
        require(vfs.touch("new.txt", "/home/alex", false, true, true, message), message);
        require(vfs.entry("/home/alex/new.txt")->data.empty(), "touch creates empty file");
        require(!vfs.touch("/missing/child", "/", false, true, true, message), "missing parent");
        require(!vfs.touch("/lines.txt/child", "/", false, true, true, message), "file as parent");
        require(!vfs.touch("/new-dir/", "/", false, true, true, message), "touch cannot create directory");

        require(vfs.chmod("/lines.txt", "/", "u-r", false, message), message);
        require(!vfs.read_file("/lines.txt", contents, message), "read permission enforced");
        require(vfs.chmod("/lines.txt", "/", "0644", false, message), message);
        require(!vfs.chmod("/lines.txt", "/", "88", false, message) &&
                vfs.entry("/lines.txt")->mode == 0644, "bad chmod must not alter mode");
        require(vfs.chmod("/home/alex", "/", "u-w", false, message), message);
        require(!vfs.touch("/home/alex/blocked", "/", false, true, true, message), "parent write permission");
        require(vfs.chmod("/home/alex", "/", "0755", false, message), message);
        require(vfs.chmod("/home", "/", "u=rwX,go=", true, message), message);
        require(vfs.entry("/home/alex")->mode == 0700 &&
                vfs.entry("/home/alex/new.txt")->mode == 0600, "recursive symbolic chmod");
        require(vfs.chmod("/home/alex", "/", "u-x", false, message), message);
        std::string absolute;
        require(!vfs.resolve("/home/alex/new.txt", "/", absolute, message), "directory traversal permission");
        require(vfs.chmod("/home/alex", "/", "0700", false, message), message);
        require(vfs.chmod("/", "/", "0700", false, message), message);

        const auto saved_file = *vfs.entry("/lines.txt");
        const auto saved_new = *vfs.entry("/home/alex/new.txt");
        const auto snapshot = work / "saved.zip";
        write_bytes(work / "saved.zip.tmp", "existing host file");
        require(vfs.save_zip(snapshot, message), message);
        require(vfs.save_zip(snapshot, message), "replace existing snapshot");
        require(read_bytes(work / "saved.zip.tmp") == "existing host file", "temporary file collision");
        require(vfs.load_zip(snapshot, message), message);
        require(vfs.entry("/")->mode == 0700, "root permissions persisted");
        const auto* restored = vfs.entry("/lines.txt");
        require(restored && restored->mode == saved_file.mode && restored->modified == saved_file.modified &&
                restored->accessed == saved_file.accessed && restored->data == saved_file.data, "metadata roundtrip");
        require(vfs.entry("/home/alex/new.txt")->mode == saved_new.mode, "new file roundtrip");

        const auto corrupt = work / "broken.zip";
        write_bytes(corrupt, "not a ZIP");
        require(!vfs.load_zip(corrupt, message) && vfs.entry("/home/alex/new.txt"), "failed load rollback");
        auto damaged = original;
        const auto contents_position = damaged.find("one\ntwo\nthree\n");
        require(contents_position != std::string::npos, "stored fixture contents missing");
        damaged[contents_position] ^= 0x7f;
        write_bytes(corrupt, damaged);
        require(!vfs.load_zip(corrupt, message), "corrupted file contents");
        auto traversal = original;
        for (auto pos = traversal.find("lines.txt"); pos != std::string::npos;
             pos = traversal.find("lines.txt", pos + 9)) traversal.replace(pos, 9, "../es.txt");
        write_bytes(corrupt, traversal);
        require(!vfs.load_zip(corrupt, message), "archive traversal path rejected");
        require(vfs.entry("/home/alex/new.txt") != nullptr, "archive error preserves complete VFS");

        std::istringstream input;
        std::ostringstream out, err;
        Shell shell(vfs, input, out, err);
        shell.execute_line("cd /home/alex");
        shell.execute_line("vfs-load \"" + corrupt.generic_string() + "\"");
        require(shell.current_directory() == "/home/alex" && !err.str().empty(), "failed reload preserves cwd");
        err.str("");
        shell.execute_line("touch -am -- \"two words.txt\" other.txt");
        shell.execute_line("chmod 600 \"two words.txt\" other.txt");
        require(vfs.entry("/home/alex/two words.txt")->mode == 0600 && err.str().empty(), "mutation operands");
        shell.execute_line("ls -al");
        require(out.str().find("-rw-------") != std::string::npos, "ls shows changed permissions");
        shell.execute_line("chmod 000 /home/alex");
        err.str(""); shell.execute_line("ls /home/alex");
        require(err.str().find("permission denied") != std::string::npos, "ls directory permission");
        err.str(""); shell.execute_line("cd /home/alex");
        require(err.str().find("permission denied") != std::string::npos, "cd directory permission");
        shell.execute_line("chmod 700 /home/alex");
        shell.execute_line("vfs-load \"" + (fixtures / "minimal.zip").generic_string() + "\"");
        require(shell.current_directory() == "/" && !vfs.entry("/home/alex/new.txt"), "successful load replaces VFS");
        out.str(""); shell.execute_line("cd -");
        require(out.str() == "/\n", "reload resets previous directory");
        for (const auto& command : {"chmod", "chmod 888 /README.txt", "touch", "touch -z /x", "vfs-load"}) {
            err.str(""); shell.execute_line(command);
            require(!err.str().empty(), "missing command error");
        }
        require(!vfs.save_zip(work / "missing" / "output.zip", message), "save failure is reported");
        require(read_bytes(fixtures / "commands.zip") == original, "host source ZIP unchanged");
        require(!std::filesystem::exists(work / "home"), "VFS commands never extract files");
        std::cout << "Permissions, touch, reload and persistence checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
