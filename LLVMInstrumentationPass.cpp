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
//          registerOptimizerLastEPCallback — пасс автоматически выполняется
//          в КОНЦЕ пайплайна оптимизаций, т.е. после всех оптимизаций
//          (при компиляции через clang -fpass-plugin=./libLLVMInstrumentationPass.so).
//          Альтернативы: registerPipelineStartEPCallback — до оптимизаций,
//          registerOptimizerEarlySimplificationEPCallback — после ранних
//          упрощений, но до основной части оптимизаций.
//
//  5. Инструментирование (см. runOnFunction): обход Function → BasicBlock
//       → Instruction и вставка перед каждой инструкцией вызовов
//       __inst_log_pair(user, operand) через IRBuilder.
//       В рантайме каждая пара печатает строку вида "add <- load":
//       слева имя операции-user, справа имя операции, породившей операнд.
//
//       КАКИЕ ИНСТРУКЦИИ ИНСТРУМЕНТИРУЮТСЯ (как User):
//         — все, кроме phi (задание), dbg-интриниксов (llvm.dbg.*),
//           EH-pad инструкций (landingpad/catchswitch/catchpad/cleanuppad —
//           они обязаны быть первыми в блоке, вставка перед ними ломает IR),
//           musttail call и стоящего сразу за ним ret (между ними нельзя
//           вставлять ничего);
//
//       КАКИЕ ОПЕРАНДЫ УЧИТЫВАЮТСЯ (как Operand):
//         — только инструкции, т.е. результаты работы других инструкций;
//           константы, аргументы, глобалы и метки ветвлений отбрасываются.
//           Инструкции без инструкционных операндов строк не дают.
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

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct LLVMInstrumentationPass : public PassInfoMixin<LLVMInstrumentationPass> {
  static constexpr const char *LoggerName = "__inst_log_pair";
  static constexpr const char *SkipPrefix = "__inst_";

  Module *M = nullptr;
  FunctionCallee LogFunc;
  DenseMap<StringRef, Constant *> OpNameStrs;

  LLVMInstrumentationPass() {}

  static bool isRuntime(StringRef Name) { return Name.starts_with(SkipPrefix); }

  Constant *getOpNameStr(StringRef OpName) {
    auto It = OpNameStrs.find(OpName);
    if (It != OpNameStrs.end())
      return It->second;

    IRBuilder<> B(M->getContext());
    GlobalVariable *GV = B.CreateGlobalString(
        OpName, (Twine(SkipPrefix) + "op_" + OpName).str(), 0, M);
    OpNameStrs[OpName] = GV;
    return GV;
  }

  static bool skipAsUser(Instruction &I) {
    if (isa<PHINode>(I) || isa<DbgInfoIntrinsic>(I))
      return true;
    if (I.isEHPad())
      return true;
    if (auto *Call = dyn_cast<CallInst>(&I); Call && Call->isMustTailCall())
      return true;
    if (auto *Ret = dyn_cast<ReturnInst>(&I))
      if (auto *Prev = Ret->getPrevNode())
        if (auto *Call = dyn_cast<CallInst>(Prev); Call && Call->isMustTailCall())
          return true;
    return false;
  }

  unsigned runOnFunction(Function &F) {
    if (F.isDeclaration() || isRuntime(F.getName()))
      return 0;

    SmallVector<Instruction *, 64> Worklist;
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (!skipAsUser(I))
          Worklist.push_back(&I);

    unsigned NumCalls = 0;
    IRBuilder<> B(F.getContext());
    for (Instruction *I : Worklist) {
      SmallVector<Instruction *, 8> OpInsts;
      for (Use &U : I->operands())
        if (auto *OpI = dyn_cast<Instruction>(U.get()))
          OpInsts.push_back(OpI);
      if (OpInsts.empty())
        continue;

      Constant *UserStr = getOpNameStr(I->getOpcodeName());
      B.SetInsertPoint(I);
      for (Instruction *OpI : OpInsts) {
        Constant *OpStr = getOpNameStr(OpI->getOpcodeName());
        B.CreateCall(LogFunc, {UserStr, OpStr});
        ++NumCalls;
      }
    }
    return NumCalls;
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    outs() << "[LLVMInstrumentationPass] run on module: " << M.getName() << '\n';
    this->M = &M;
    OpNameStrs.clear();

    LLVMContext &Ctx = M.getContext();
    Type *PtrTy = PointerType::getUnqual(Ctx);
    Type *VoidTy = Type::getVoidTy(Ctx);
    LogFunc = M.getOrInsertFunction(
        LoggerName, FunctionType::get(VoidTy, {PtrTy, PtrTy}, false));

    unsigned FuncCount = 0, TotalCalls = 0;
    for (Function &F : M) {
      if (F.isDeclaration() || isRuntime(F.getName()))
        continue;

      ++FuncCount;
      unsigned NumCalls = runOnFunction(F);
      TotalCalls += NumCalls;

      outs() << "  [Function] " << F.getName()
             << " | args: " << F.arg_size()
             << " | BBs: " << F.size()
             << " | inserted calls: " << NumCalls << '\n';

      if (verifyFunction(F, &errs()))
        outs() << "    [VERIFICATION] FAIL\n";
    }

    outs() << "[LLVMInstrumentationPass] instrumented functions: " << FuncCount
           << " | total inserted calls: " << TotalCalls << "\n\n";

    return PreservedAnalyses::none();
  }
  static bool isRequired() { return true; }
};

} // namespace

static PassPluginLibraryInfo getPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION,          
          "LLVMInstrumentationPass",       
          "0.1.0",                          
          [](PassBuilder &PB) {

            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel,
                   ThinOrFullLTOPhase) {
                  MPM.addPass(LLVMInstrumentationPass{});
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getPassPluginInfo();
}
