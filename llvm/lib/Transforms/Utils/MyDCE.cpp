#include "llvm/Transforms/Utils/MyDCE.h"

#include <list>

#include "llvm/IR/Function.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

#define DEBUG_TYPE "mydce"
STATISTIC(DCEEliminated, "Number of instns removed");

PreservedAnalyses MyDCEPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  bool erasedInstr = false;
  std::list<Instruction*> instrs;
  auto* tli = &AM.getResult<TargetLibraryAnalysis>(F);

  for (BasicBlock& bb: F) {
    for (Instruction& i : bb) {
      if (isInstructionTriviallyDead(&i, tli)) {
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

    erasedInstr = true;
    i->eraseFromParent();
    ++DCEEliminated;
  }

  if (!erasedInstr) {
    return PreservedAnalyses::all();
  }

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

