#include "vector"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/IR/Instruction.h>

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
      return true;

    return false;
  }

  unsigned runOnFunction(Function &F) {
    if (F.isDeclaration() || isRuntime(F.getName()))
      return 0;

    std::vector<Instruction*> Worklist;
    for (BasicBlock &BB : F)
      for (Instruction &I : BB)
        if (!skipAsUser(I))
          Worklist.push_back(&I);

    unsigned NumCalls = 0;
    IRBuilder<> B(F.getContext());
    
    for (Instruction *I : Worklist) {
      std::vector<Instruction*> OpInsts;
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
