#include "shell.hpp"
#include "text_commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "read host fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "expected fixture directory and output directory");
        const std::filesystem::path fixtures(argv[1]), work(argv[2]);
        std::filesystem::create_directories(work);
        const auto original = read_bytes(fixtures / "commands.zip");
        VirtualFileSystem vfs;
        std::string message, path, data;
        require(vfs.load_zip(fixtures / "commands.zip", message), message);
        require(vfs.resolve("../../../../empty", "/home/alex", path, message) && path == "/empty",
                "root boundary and parent resolution");
        require(!vfs.resolve("/lines.txt/..", "/", path, message), "file cannot be traversed");
        require(!vfs.resolve("/missing/..", "/", path, message), "missing component cannot be traversed");
        require(!vfs.resolve("/lines.txt/", "/", path, message), "trailing slash requires directory");

        std::istringstream input;
        std::ostringstream output, errors;
        Shell shell(vfs, input, output, errors, "test@host");
        const auto run = [&](const std::string& command) {
            output.str(""); errors.str("");
            require(shell.execute_line(command), "unexpected exit: " + command);
            return output.str();
        };
        require(run("ls").find(".hidden") == std::string::npos, "ls hides dotfiles");
        require(run("ls -a").find(".hidden") != std::string::npos, "ls -a shows dotfiles");
        require(run("ls -- \"space name.txt\"") == "space name.txt\n", "quoted file name");
        run("cd /home/alex");
        require(shell.current_directory() == "/home/alex", "cd absolute");
        require(run("tac notes.txt") == "third\nsecond\nfirst\n", "tac relative file");
        run("cd /lines.txt");
        require(!errors.str().empty() && shell.current_directory() == "/home/alex", "failed cd is atomic");
        run("cd ..");
        require(run("cd -") == "/home/alex\n", "cd previous directory");
        run("cd");
        require(shell.current_directory() == "/", "cd default virtual root");
        require(run("tac /no-newline.txt") == "twoone\n", "tac preserves original separators");
        require(run("tac /empty.txt").empty(), "empty file");
        require(run("rev /utf8.txt") == u8"тевирП\nрим\r\n", "rev UTF-8 and CRLF");
        require(run("rev /lines.txt /no-newline.txt") == "eno\nowt\neerht\neno\nowt", "multiple operands");
        run("rev /missing /empty");
        require(errors.str().find("no such") != std::string::npos &&
                errors.str().find("directory") != std::string::npos, "text command errors");

        run("regvar SHELL_TEST_VALUE \"C:\\folder with spaces\\a.zip\"");
        require(run("echo \"$SHELL_TEST_VALUE\"") == "C:\\folder with spaces\\a.zip\n", "environment path");
        require(run("echo '$SHELL_TEST_VALUE'") == "$SHELL_TEST_VALUE\n", "single quotes");
        require(run("echo \\$SHELL_TEST_VALUE") == "$SHELL_TEST_VALUE\n", "escaped dollar");
        require(run("echo \"// literal\" // comment") == "// literal\n", "C++ line comment");
        require(run("/* comment").empty(), "block comment start");
        require(run("end */ echo ready") == "ready\n", "block comment end");
        run("echo \"unclosed");
        require(errors.str().find("quote") != std::string::npos, "syntax error");

        std::istringstream stdin_text("red\nblue\n");
        Shell stdin_shell(vfs, stdin_text, output, errors);
        output.str(""); stdin_shell.execute_line("tac");
        require(output.str() == "blue\nred\n", "tac standard input");
        std::istringstream stdin_rev(u8"кот\n");
        Shell rev_shell(vfs, stdin_rev, output, errors);
        output.str(""); rev_shell.execute_line("rev -");
        require(output.str() == u8"ток\n", "rev explicit standard input");
        require(!reverse_characters(std::string("\xc0\xaf", 2), data, message), "reject invalid UTF-8");

        require(vfs.load_zip(fixtures / "multiple-files.zip", message), message);
        require(vfs.read_file("/bin/sample.bin", data, message), message);
        const auto binary = data;
        require(vfs.save_zip(work / "roundtrip.zip", message), message);
        require(vfs.load_zip(work / "roundtrip.zip", message), message);
        require(vfs.read_file("/bin/sample.bin", data, message) && data == binary, "binary ZIP roundtrip");
        require(read_bytes(fixtures / "commands.zip") == original, "source ZIP must stay unchanged");
        require(!std::filesystem::exists(work / "home"), "no extraction to host");
        std::cout << "Command, parser, path and ZIP checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
