#include "llvm/Transforms/Utils/MyADCE.h"

#include <map>
#include <set>

#include "llvm/IR/Function.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/CFG.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "myadce"
STATISTIC(ADCEEliminated, "Number of instns removed");


namespace {

enum class InstType {
  ALIVE, ALIVE_AND_VISITED, UNVISITED
};

void dfs(Instruction* inst, std::map<Instruction*, InstType>& instTypes) {
  instTypes[inst] = InstType::ALIVE_AND_VISITED;
  for (auto&& op : inst->operands()) {
    if (auto* iop = dyn_cast<Instruction>(op.get())) {
      if (InstType::UNVISITED == instTypes[iop]) {
        dfs(iop, instTypes);
      }
    }
  }
}

} // namespace

PreservedAnalyses MyADCEPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  std::map<Instruction*, InstType> instTypes;

  LLVM_DEBUG(dbgs() << "Function: " << F.getName() << "\n");
  for (auto&& bb : F) {
    for (auto&& i : bb) {
      instTypes[&i] = 
        (i.mayHaveSideEffects() || i.isTerminator() || i.isEHPad()) ? 
          InstType::ALIVE : 
          InstType::UNVISITED;
    }
  }

  for (auto&& [i, type] : instTypes) {
    if (InstType::ALIVE == type) {
      dfs(i, instTypes);
    }
  }

  for (auto&& [i, type] : instTypes) {
    if (type == InstType::UNVISITED)
      LLVM_DEBUG(dbgs() << "DEAD: " << *i << '\n');
    else
      LLVM_DEBUG(dbgs() << "LIVE: " << *i << '\n');
  }

  for (auto&& [i, type] : instTypes) {
    if (type == InstType::UNVISITED) {
      i->dropAllReferences();
    }
  }

  bool removedInst = false;

  // remove dead instrs
  for (auto&& [i, type] : instTypes) {
    if (type == InstType::UNVISITED) {
      removedInst = true;
      i->eraseFromParent();
      ++ADCEEliminated;
    }
  }

  if (removedInst) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }

  return PreservedAnalyses::all();
}
