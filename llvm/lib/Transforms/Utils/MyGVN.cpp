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
    auto&& dt = 
}