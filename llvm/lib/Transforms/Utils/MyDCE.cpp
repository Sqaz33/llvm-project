#include "llvm/Transforms/Utils/MyDCE.h"

#include <iostream>
#include <string>
#include <vector>


#include "llvm/IR/Function.h"

using namespace llvm;

PreservedAnalyses MyDCEPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  std::vector<Instruction*> instrs;
  bool change = true;
  while (change) {
    change = false;
    for (BasicBlock& bb: F) {
      for (Instruction& i : bb) {
        if (i.use_empty() && !i.mayHaveSideEffects() && !i.isTerminator()) {
          change = true;
          instrs.push_back(&i);
        }
      }
    }
    for (auto* i : instrs) i->eraseFromParent();
    instrs.clear();
  }

  return PreservedAnalyses::all();
}