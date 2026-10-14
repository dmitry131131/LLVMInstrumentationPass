# LLVM instrumentation pass

Плагин (`.so`) с инструментирующим пассом для LLVM New PM.
Описание структуры пасса — в комментариях в `LLVMInstrumentationPass.cpp`.
Пасс привязан к `registerOptimizerLastEPCallback`, т.е. выполняется
в конце пайплайна оптимизаций — уже на оптимизированном IR.

## Что делает

На каждую IR-инструкцию вставляет перед ней вызов `__inst_log_pair(user, op)`
(см. `runtime.c`). В рантайме каждая пара печатается строкой вида

```
add <- load
store <- alloca
ret <- add
```

где слева — имя операции-user, справа — имя операции, породившей операнд.

- **User** — все инструкции, кроме `phi` (по заданию), `llvm.dbg.*`, EH-падов
  (`landingpad`/`catchswitch`/`catchpad`/`cleanuppad` — вставка перед ними
  ломает IR) и `musttail call` + стоящего за ним `ret`.
- **Operand** — только инструкции, т.е. результаты работы других инструкций.
  Константы, аргументы, глобалы и метки ветвлений игнорируются. Инструкции
  без инструкционных операндов строк в лог не дают.

## Сборка

``` bash
clang++ LLVMInstrumentationPass.cpp -fPIC -shared \
        -I$(llvm-config --includedir) -o libLLVMInstrumentationPass.so
```

## Запуск

`runtime.c` обязателен: без него линковка упадёт на `__inst_log_pair`.

``` bash
clang -fpass-plugin=./libLLVMInstrumentationPass.so file.c runtime.c -o file
./file                       # печатает лог в stdout
```

`runtime.c` тоже проходит через пасс, но пропускается (префикс `__inst_`) —
иначе логгер залогировал бы сам себя.

Отдельно, без рантайма (только вставка вызовов в IR):

``` bash
clang -fpass-plugin=./libLLVMInstrumentationPass.so -S -emit-llvm file.c -o file.ll
```
