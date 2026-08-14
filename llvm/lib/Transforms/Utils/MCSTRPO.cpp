#include "llvm/Transforms/Utils/MCSTRPO.h"

#include <algorithm>
#include <set>
#include <vector>
#include <map>
#include <string>

#include "llvm/IR/Function.h"
#include "llvm/IR/CFG.h"

using namespace llvm;

namespace {

enum class Color {
    WHITE, GRAY, BLACK
};

void PO(
  std::map<BasicBlock*, Color>& color, 
  std::vector<BasicBlock*>& po, 
  std::vector<std::pair<BasicBlock*, BasicBlock*>>& retreating,
  BasicBlock* cur) 
{
  color[cur] = Color::GRAY;

  for (auto* s : successors(cur)) {
    if (color[s] == Color::WHITE) {
      PO(color, po, retreating, s);
    } else if (color[s] == Color::GRAY) {
      retreating.push_back(std::make_pair(cur, s));
    }
  }

  po.push_back(cur);
  color[cur] = Color::BLACK;
}

std::pair<
  std::vector<BasicBlock*>, 
  std::vector<std::pair<BasicBlock*, BasicBlock*>>
> RPO(Function &F) {
  std::vector<BasicBlock*> po;
  std::vector<std::pair<BasicBlock*, BasicBlock*>> retreating;
  std::map<BasicBlock*, Color> color;
  for (auto&& bb : F) {
    color[&bb] = Color::WHITE;
  }
  PO(color, po, retreating, &F.getEntryBlock());
  // for (auto&& [bb, col] : color) {
  //   if (col == Color::WHITE) {
  //     std::vector<std::pair<BasicBlock*, BasicBlock*>> subretreating;
  //     PO(color, po, subretreating, bb);
  //     retreating.insert(
  //       retreating.begin(), 
  //       subretreating.begin(),
  //       subretreating.end());
  //   }
  // }
  std::reverse(po.begin(), po.end());
  return std::make_pair(po, retreating);
}

} // namespace

// У Константина есть: 
// [обратные ребра] - retreating edge. u -> n, rpo(u) > rpo(n) но не обязательно доминирование
// [обращенные ребра] - back edge. u -> n, n dom u
// здесь выводятся оба вида
  
PreservedAnalyses MCSTRPOPath::run(Function &F,
                        FunctionAnalysisManager &AM) {
  if (F.empty()) {
    return PreservedAnalyses::all();
  }

  std::map<BasicBlock*, std::string> bbToName;

  outs() << "Function " << F.getName() << "()\n";
  outs() << "Names of basic blocks in the order they are declared.\n" 
            "If a BB does not have a name, its name is BB[idx], where\n" 
            "idx is the sequential number of the BB, starting from 0.\n" 
            "The first BB in the list is the entry.\nList:\n\n";

  int idx = 0;
  for (auto&& bb : F) {
    std::string name;
    if (bb.hasName()) {
      name = bb.getName();
    } else {
      name = "BB";
      name += std::to_string(idx);
    }
    idx++;
    outs() << name << '\n';
    bbToName[&bb] = std::move(name);
  }

  auto [rpo, retreating] = RPO(F);
  outs() << "\nRPO:\n";
  for (auto* bb : rpo) {
    outs() << bbToName[bb] << '\n';
  }

  outs() << "\nRetreating edges:\n";
  for (auto [u, v] : retreating) {
    outs() << bbToName[u] << "->" << bbToName[v] << '\n';
  }

  auto&& DT = AM.getResult<DominatorTreeAnalysis>(F);
  outs() << "\nBack edges:\n";
  for (auto [u, v] : retreating) {
    if (DT.dominates(v, u)) {
      outs() << bbToName[u] << "->" << bbToName[v] << '\n';
    }
  }
  outs() << '\n';

  return PreservedAnalyses::all();
}