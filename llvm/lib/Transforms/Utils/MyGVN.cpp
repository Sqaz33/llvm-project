#include "llvm/Transforms/Utils/MyGVN.h"

#include <set>
#include <map>
#include <utility>
#include <vector>
#include <algorithm>

#include "llvm/IR/Function.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/CFG.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "mygvn"
STATISTIC(GVNInstRemove, "Number of instns removed");
STATISTIC(GVNRedundancyRemove, "Number of redundancy removed");

namespace {

bool isGVNCandidate(const Instruction& i) {
  if (!i.isSafeToRemove()) {
    return false;
  }

  if (i.mayReadOrWriteMemory()) {
    return false;
  }

  return isa<BinaryOperator>(i) ||
         isa<CmpInst>(i) ||
         isa<CastInst>(i) ||
         isa<SelectInst>(i) ||
         isa<GetElementPtrInst>(i) ||
         isa<PHINode>(i);
}

} // namespace

namespace analise {

bool sameClass(
  Value* a, 
  Value* b, 
  const std::map<Value*, std::set<Value*>*>& valToP)
{
  if (a == b) {
    return true;
  }
  
  auto aIt = valToP.find(a);
  auto bIt = valToP.find(b);
  
  return aIt != valToP.end() && 
         bIt != valToP.end() && 
         aIt->second == bIt->second;
}

bool match( 
  const std::map<Value*, std::set<Value*>*>& valToP,
  const Instruction* i1, 
  const Instruction* i2)
{
  if (i1->getNumOperands() != i2->getNumOperands()) {
      return false;
  } 

  int opNum = i1->getNumOperands();
  bool unorderedOperands = i1->isCommutative() || isa<PHINode>(i1);

  if (!unorderedOperands) {
    for (int i = 0; i < opNum; ++i) {
      if (!sameClass(i1->getOperand(i), i2->getOperand(i), valToP)) {
        return false;
      }
    }
  } else {
    // TODO: протестить
    auto* phi1 = dyn_cast<PHINode>(i1);
    auto* phi2 = dyn_cast<PHINode>(i2);
    std::vector<bool> hasSameClassIn2(opNum, false);
    for (int i = 0; i < opNum; ++i) {
      bool findSameClass = false;
      for (int j = 0; j < opNum; ++j) {
        if (hasSameClassIn2[j]) {
          continue;
        }
        bool isSameClass = sameClass(
          i1->getOperand(i), i2->getOperand(j), valToP);
        bool isSameInBB = !phi1 || 
          phi1->getIncomingBlock(i) == phi2->getIncomingBlock(j);
        hasSameClassIn2[j] = isSameClass && isSameInBB;
        findSameClass = isSameClass && isSameInBB;
        if (findSameClass) {
          break;
        }
      }
      if (!findSameClass) {
        return false;
      }
    }
  }

  return true;
}

std::pair<std::set<Value*>, std::set<Value*>> 
split(
    const std::set<Value*>& partition, 
    const std::map<Value*, std::set<Value*>*>& valToP) 
{
  if (partition.size() <= 1) {
    return std::pair<std::set<Value*>, std::set<Value*>>(partition, {});
  }

  auto* I = *partition.begin();
  std::set<Value*> packetI, packetNonI;
  packetI.insert(I);

  auto* IInst = dyn_cast<Instruction>(I);
  assert(IInst && "????");
  for (auto&& J : partition) {
    auto* JInst = dyn_cast<Instruction>(J);
    assert(JInst && "????");
    if (match(valToP, IInst, JInst)) {
      packetI.insert(J);
    } else {
      packetNonI.insert(J);
    }
  }

  return std::make_pair(packetI, packetNonI);
}

std::list<std::set<Value*>> partition(Function &F) {
  std::list<std::set<Value*>> partitions;
  std::map<Value*, std::set<Value*>*> valToP;

  // начальные пакеты
  // пакеты по типу операции
  for (auto&& bb : F) {
    for (auto&& i : bb) {
      if (!isGVNCandidate(i)) {
        continue;
      }

      std::set<Value*>* curP = nullptr;
      for (auto&& p : partitions) {
        if (auto inst = dyn_cast<Instruction>(*p.begin())) {
          auto* instPhi = dyn_cast<PHINode>(inst);
          auto* iPhi = dyn_cast<PHINode>(&i);
          if (inst->isSameOperationAs(&i) && 
            (!iPhi || instPhi->getParent() == iPhi->getParent())) 
          {
            curP = &p;
            break;
          }
        } 
      }

      if (curP != nullptr) {
        curP->insert(&i);
        valToP[&i] = curP;
      } else {
        partitions.emplace_front();
        partitions.front().insert(&i);
        valToP[&i] = &partitions.front();
      }
    }
  }

  // пакеты по аргументам
  for (auto&& arg : F.args()) {
    partitions.emplace_front();
    partitions.front().insert(dyn_cast<Value>(&arg));
    valToP[&arg] = &partitions.front();
  }

  bool change = true;
  while (change) {
    change = false;
    for (auto it = partitions.begin(); it != partitions.end();) {
      auto [packetI, packetNonI] = split(*it, valToP);
      if (!packetNonI.empty()) {
        change = true;
        // обновить отображение инструкции на пакет
        auto&& refreshValToP = [&](auto&& p) { 
          partitions.push_front(std::move(p));
          for (auto* i : partitions.front()) {
            valToP[i] = &partitions.front();
          }
        };
        refreshValToP(packetI);
        refreshValToP(packetNonI);
        it = partitions.erase(it);
      } else {
        ++it;
      }
    }
  } 

  return partitions;
} 

} // namespace analise

// устранение полной избыточности
PreservedAnalyses MyGVNPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  auto partitions = analise::partition(F);

  auto&& DT = AM.getResult<DominatorTreeAnalysis>(F);

  bool change = false;
  std::vector<Instruction*> toErase;

  for (auto&& p : partitions) {
    if (p.size() <= 1) {
      continue;
    }

    std::map<Value*, bool> valOpt;
    for (auto* i : p) {
      valOpt[i] = false;
    }

    std::vector pVector(p.begin(), p.end());
    for (int i = 0; i < pVector.size() - 1; ++i) {
      for (int j = i + 1; j < pVector.size(); ++j) {

        auto&& opt = [&] (int x, int y) {
          if (valOpt[pVector[x]] || valOpt[pVector[y]]) {
            return;
          } 
          auto* xInst = dyn_cast<Instruction>(pVector[x]);
          auto* yInst = dyn_cast<Instruction>(pVector[y]);
          assert(xInst && yInst && "????");
          if (DT.dominates(xInst, yInst)) {
            yInst->replaceAllUsesWith(xInst);
            ++GVNRedundancyRemove;
            toErase.push_back(yInst);
            valOpt[yInst] = true;
            change = true;
          }
        };

        opt(i, j);
        opt(j, i);
      }
    }
  }

  for (auto* i : toErase) {
    i->eraseFromParent();
    ++GVNInstRemove;
  }

  if (!change) {
    return PreservedAnalyses::all();
  }

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}