# LLVM instrumentation pass

Плагин (`.so`) с инструментирующим пассом для LLVM.
Пасс привязан к `registerOptimizerLastEPCallback`, т.е. выполняется
в конце пайплайна оптимизаций — уже на оптимизированном IR.

## Сборка

``` bash
clang++ LLVMInstrumentationPass.cpp -fPIC -shared \
        -I$(llvm-config --includedir) -o libLLVMInstrumentationPass.so
```

## Запуск
``` bash
clang -fpass-plugin=./libLLVMInstrumentationPass.so file.c runtime.c -o file
./file                       # печатает лог в stdout
```


## Частые паттерны в приложении

### Паттерн из 1 инструкции
![1 инструкция](./patterns_histogram_1.png)

### Паттерн из 2 инструкций
![2 инструкция](./patterns_histogram_2.png)

### Паттерн из 3 инструкций
![3 инструкция](./patterns_histogram_3.png)

### Паттерн из 4 инструкций
![4 инструкция](./patterns_histogram_4.png)

### Паттерн из 5 инструкций
![5 инструкция](./patterns_histogram_5.png)