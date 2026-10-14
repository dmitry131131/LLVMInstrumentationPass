# LLVM instrumentation pass

Плагин (`.so`) с инструментирующим пассом для LLVM New PM.
Шаблон и описание структуры пасса — в комментариях в `LLVMInstrumentationPass.cpp`.
Полная версия старого кода сохранена в `LLVMInstrumentationPass.full.cpp`.

## Сборка

``` bash
clang++ LLVMInstrumentationPass.cpp -fPIC -shared \
        -I$(llvm-config --includedir) -o libLLVMInstrumentationPass.so
```

## Запуск

``` bash
# автоматически, как часть пайплайна оптимизаций:
clang -fpass-plugin=./libLLVMInstrumentationPass.so file.c -o file

# явно, по имени пасса:
opt -load-pass-plugin=./libLLVMInstrumentationPass.so \
    -passes=llvminstrumentation -S file.ll -o /dev/null
```

## Важно: версии LLVM должны совпадать

Заголовки, которыми компилируется плагин (`llvm-config --includedir`), обязаны
совпадать с версией LLVM, которая грузит его в рантайме (`clang`/`opt`).
Иначе плагин упадёт с SIGSEGV внутри STL/LLVM-кода.

Проверка:

``` bash
llvm-config --version   # версия заголовков (dev)
clang --version          # версия рантайма, который будет грузить плагин
pacman -Q llvm llvm-libs clang
```

Проверено на системе (после полного `pacman -Syu`): `llvm` = `llvm-libs` =
`clang` = 23.1.1, glibc 2.44 — всё согласовано, `clang -fpass-plugin` и
`opt -passes=llvminstrumentation` работают.
