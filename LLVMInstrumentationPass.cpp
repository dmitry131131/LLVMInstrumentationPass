//===- LLVMInstrumentationPass.cpp ---------------------------------------===//
//
// Шаблон инструментирующего (instrumentation) LLVM-пасса в виде плагина
// (динамической библиотеки .so). Сейчас пасс только печатает в консоль имя
// модуля и имена функций, на которых он запускается.
//
// ИЗ КАКИХ ЧАСТЕЙ СОСТОИТ ПАСС:
//
//  1. Заголовки и пространство имён LLVM
//       llvm/IR/Module.h, llvm/IR/Function.h  — работа с IR (модуль, функции);
//       llvm/Passes/PassBuilder.h             — менеджер пассов нового PM;
//       llvm/Plugins/PassPlugin.h             — API плагина (.so);
//       llvm/Support/raw_ostream.h            — вывод в консоль (outs()/errs()).
//
//  2. Класс пасса — struct LLVMInstrumentationPass
//       Наследуется от PassInfoMixin<T>: даёт пассу имя (имя класса),
//       возможность запускаться через новый Pass Manager и печататься
//       в -print-after / -debug-pass-manager.
//       Единственный обязательный метод:
//         PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
//       — тело пасса. Возвращает, какие анализы инвалидированы:
//         PreservedAnalyses::all()     — IR не менялся (наш случай);
//         PreservedAnalyses::none()    — IR изменён, все анализы невалидны.
//
//  3. Точка входа плагина
//       extern "C" ... llvmGetPassPluginInfo() — её ищет драйвер
//       (clang/opt) после dlopen() библиотеки; возвращает PassPluginLibraryInfo.
//
//  4. Регистрация пасса (лямбда-колбэк внутри getPassPluginInfo):
//       a) именованная регистрация registerPipelineParsingCallback —
//          позволяет вызвать пасс явно:  opt -passes='llvminstrumentation' ...
//       b) привязка к точке расширения оптимизатора
//          registerPipelineStartEPCallback — пасс автоматически выполняется
//          при компиляции через  clang -fpass-plugin=./libLLVMInstrumentationPass.so
//
//  5. (Далее, в будущем) само инструментирование: обход Function → BasicBlock
//       → Instruction и вставка вызовов ваших логгер-функций через IRBuilder.
//
// СБОРКА:
//   clang++ LLVMInstrumentationPass.cpp -fPIC -shared \
//           -I$(llvm-config --includedir) -o libLLVMInstrumentationPass.so
//
// ЗАПУСК:
//   clang -fpass-plugin=./libLLVMInstrumentationPass.so file.c -o file
//   opt -load-pass-plugin=./libLLVMInstrumentationPass.so \
//       -passes=llvminstrumentation -S file.ll -o /dev/null
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h" // в LLVM ≤21 этот заголовок лежал в llvm/Passes/
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// Часть 2: сам пасс. Один метод run() — туда пойдёт логирование инструкций.
//===----------------------------------------------------------------------===//
struct LLVMInstrumentationPass : public PassInfoMixin<LLVMInstrumentationPass> {

  // Вызывается для каждого модуля (translation unit), попавшего в пасс-пайплайн.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    outs() << "[LLVMInstrumentationPass] run on module: " << M.getName() << '\n';

    unsigned FuncCount = 0;
    for (const Function &F : M) {
      // Пропускаем только что объявленные (без тела) функции, если не хочется
      // видеть внешние символы вроде printf.
      if (F.isDeclaration())
        continue;

      ++FuncCount;
      outs() << "  [Function] " << F.getName()
             << " | args: " << F.arg_size()
             << " | BBs: " << F.size()
             << " | instrs: " << F.getInstructionCount() << '\n';
    }

    outs() << "[LLVMInstrumentationPass] instrumented functions: " << FuncCount
           << "\n\n";

    // Мы ничего не меняли в IR — просим менеджер сохранить анализы.
    return PreservedAnalyses::all();
  }

  // Статический флаг: пасс является "анализом" с побочными эффектами,
  // поэтому opt может печатать его в -print-after / -debug-pass-manager.
  static bool isRequired() { return true; }
};

} // namespace

//===----------------------------------------------------------------------===//
// Часть 4: регистрация пасса в менеджере пайплайнов.
//===----------------------------------------------------------------------===//
static PassPluginLibraryInfo getPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION,          // версия API плагинов LLVM
          "LLVMInstrumentationPass",        // имя плагина
          "0.1.0",                          // версия плагина
          [](PassBuilder &PB) {
            // (a) Именованная регистрация: -passes=llvminstrumentation
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "llvminstrumentation") {
                    MPM.addPass(LLVMInstrumentationPass{});
                    return true;
                  }
                  return false;
                });

            // (b) Автоматический запуск на старте пайплайна оптимизаций:
            //     срабатывает при компиляции через -fpass-plugin=...
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                  MPM.addPass(LLVMInstrumentationPass{});
                });
          }};
}

//===----------------------------------------------------------------------===//
// Часть 3: единственная точка входа, которую ищет clang/opt в .so.
//===----------------------------------------------------------------------===//
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getPassPluginInfo();
}
