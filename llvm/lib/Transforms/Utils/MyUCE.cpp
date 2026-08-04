#include "llvm/Transforms/Utils/MyUCE.h"

#include <map>
#include <list>

#include "llvm/IR/Function.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/CFG.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "myuce"
STATISTIC(UCEEliminated, "Number of instns removed");

PreservedAnalyses MyUCEPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {

  if (F.empty()) {
    return PreservedAnalyses::all();
  }

  SmallPtrSet<BasicBlock*, 32> reachable;
  SmallVector<BasicBlock*, 32> wl;

  wl.push_back(&F.getEntryBlock());

  while (!wl.empty()) {
    auto* bb = wl.back();
    wl.pop_back();
    reachable.insert(bb);

    for (auto* suc : successors(bb)) {
        if (!reachable.contains(suc)) {
            wl.push_back(suc);
        }
    }
  }

  SmallVector<BasicBlock*, 32> deadBBs;
  for (auto&& bb : F) {
    if (!reachable.contains(&bb)) {
        deadBBs.push_back(&bb);
    } 
  }

  UCEEliminated += deadBBs.size();


  if (deadBBs.empty()) {
    return PreservedAnalyses::all();
  }

  DeleteDeadBlocks(deadBBs);

  return PreservedAnalyses::none();
}