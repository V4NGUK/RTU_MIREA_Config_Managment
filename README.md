# Эмулятор UNIX-оболочки

Этапы 1–2 варианта 8. C++17, CMake 3.16+.

## Сборка и запуск

```sh
cmake -S . -B build
cmake --build build
./build/emulator --vfs vfs/minimal.zip --script scripts/stage2/minimal.emu
```

Параметры: `--vfs` (`--vfs-path`) и `--script` (`--startup-script`). На этапе 2 путь VFS выводится в конфигурации; загрузка архива относится к этапу 3.

Стартовый скрипт поддерживает комментарии `//`, показывает ввод и вывод. Команды: заглушки `ls`, `cd`, а также `echo`, `regvar`, `exit`. Переменные окружения раскрываются как `$NAME` и `${NAME}`.

Примеры: `bash scripts/stage2/run_minimal.sh`, `run_multiple_files.sh`, `run_nested.sh` из того же каталога.
