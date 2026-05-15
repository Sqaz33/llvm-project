#include "llvm/Transforms/Utils/MyDCE.h"

#include <iostream>
#include <string>
#include <vector>
#include <list>

#include "llvm/IR/Function.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

PreservedAnalyses MyDCEPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  std::list<Instruction*> instrs;
  auto* tli = &AM.getResult<TargetLibraryAnalysis>(F);

  for (BasicBlock& bb: F) {
      for (Instruction& i : bb) {
        if (i.use_empty() && isInstructionTriviallyDead(&i, tli)) {
          instrs.push_back(&i);
      }
    }
  }

  for (auto it = instrs.begin(); it != instrs.end(); it = instrs.erase(it)) {
    auto* i = *it;
    
    unsigned lim = i->getNumOperands();
    for (unsigned j = 0; j < lim; ++j) {
      auto* op = i->getOperand(j);
      i->setOperand(j, nullptr);
      if (op->use_empty() && op != i) {
        if (auto* iop = dyn_cast<Instruction>(op)) {
          if (isInstructionTriviallyDead(iop, tli)) {
            instrs.push_back(iop);
          }
        }
      }
    }

    i->eraseFromParent();
  }

  return PreservedAnalyses::all();
}