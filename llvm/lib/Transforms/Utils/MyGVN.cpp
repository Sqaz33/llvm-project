//===----------------------------------------------------------------------===//
// MyGVN — устранение полной избыточности выражений
//===----------------------------------------------------------------------===//
//
// Проход строит классы конгруэнтности для чистых SSA-выражений. Две
// инструкции попадают в один класс, если они выполняют одну операцию с
// одинаковыми типами и semantic flags, а их операнды принадлежат одним
// классам конгруэнтности. Для коммутативных операций порядок операндов не
// важен.
//
// После стабилизации разбиения проход удаляет инструкцию Y, если в том же
// классе существует инструкция X, которая доминирует Y. Все использования Y
// заменяются на X:
//
//                 entry
//                /     \
//              left   right
//
//   entry:                         entry:
//     %x = add i32 %a, %b            %x = add i32 %a, %b
//     br i1 %cond,                  br i1 %cond,
//        label %left, ...     =>       label %left, ...
//
//   left:                          left:
//     %y = add i32 %a, %b            call void @use(i32 %x)
//     call void @use(i32 %y)
//
// `%x` и `%y` вычисляют одно выражение, а `entry` доминирует `left`, поэтому
// на любом пути к `%y` значение `%x` уже вычислено. Выражения из sibling
// blocks не заменяют друг друга, поскольку ни одно из них не доминирует
// другое.
//
// PHI-узлы обрабатываются отдельно:
//   * сравниваются только PHI одного basic block;
//   * сравниваются пары (incoming block, класс incoming value);
//   * порядок записей PHI не важен;
//   * эквивалентные PHI одного блока можно заменить друг другом, хотя API
//     DominatorTree не считает одну PHI доминирующей над другой PHI в том же
//     блоке: все PHI концептуально вычисляются одновременно на входе в блок.
//
// Поддерживаются только безопасно удаляемые инструкции без чтения и записи
// памяти: BinaryOperator, CmpInst, CastInst, SelectInst, GetElementPtrInst и
// PHINode. Проход не выполняет memory value numbering, PRE или code motion.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/MyGVN.h"

#include <utility>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

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

class MyGVNImpl {
  using CongruencePackageID = unsigned;

  struct CongruencePackage {
    CongruencePackageID ID;
    SmallVector<Value *, 4> Members;
  };

  CongruencePackageID CurrentID = 0;
  Function &F;
  DominatorTree &DT;
  DenseMap<const Value *, CongruencePackageID> ValueToPackage;
  SmallVector<CongruencePackage, 16> Partition;

public:
  MyGVNImpl(Function &F, DominatorTree &DT) : F(F), DT(DT) {} 

  bool run() {
    buildInitialPartition();
    refinePartition();
    return eliminateRedundancies();
  }

private:
  void buildInitialPartition() {
    // начальные пакеты
    // пакеты по опкоду, типу, дополнительной семантике, phi по одному бб 

    // ищем по опкоду, потом внутри уже по другим критериям
    DenseMap<unsigned, SmallVector<CongruencePackage>> PackagesByOpcode;
    for (auto&& BB : F) {
      for (auto&& FunctionInst : BB) {
        if (!isGVNCandidate(FunctionInst)) {
          continue;
        }

        CongruencePackage* FindPackage = nullptr;
        for (auto&& Package : PackagesByOpcode[FunctionInst.getOpcode()]) {
          if (auto* Inst = dyn_cast<Instruction>(Package.Members.front())) {  
            auto* PhiInst = dyn_cast<PHINode>(Inst);
            auto* PhiFunctionInst = dyn_cast<PHINode>(&FunctionInst);
            if (Inst->isSameOperationAs(&FunctionInst) &&
                Inst->hasSameSubclassOptionalData(&FunctionInst) &&
              (!PhiFunctionInst || PhiInst->getParent() == PhiFunctionInst->getParent())) 
            {
              FindPackage = &Package;
              break;
            }
          } 
        }

        if (FindPackage != nullptr) {
          FindPackage->Members.push_back(&FunctionInst);
          ValueToPackage[&FunctionInst] = FindPackage->ID;
        } else {
          CongruencePackage NewPackage = {CurrentID++};
          NewPackage.Members.push_back(&FunctionInst);
          ValueToPackage[&FunctionInst] = NewPackage.ID;
          PackagesByOpcode[FunctionInst.getOpcode()].push_back(std::move(NewPackage));
        }
      }
    }

    // вставить пакеты для аргументов
    for (auto&& arg : F.args()) {
      CongruencePackage ArgPackage = {CurrentID++};
      auto* ArgVal = dyn_cast<Value>(&arg);
      ArgPackage.Members.push_back(ArgVal);
      ValueToPackage[ArgVal] = ArgPackage.ID;
      Partition.push_back(std::move(ArgPackage));
    }

    // переместить пакеты из временного хранилища 
    for (auto&& [_, Package] : PackagesByOpcode) {
      Partition.insert(Partition.end(), Package.begin(), Package.end());
    }
  }

  void refinePartition() {
  // разбивать пакеты, пока возможно
    bool Changed = true;
    while (Changed) {
      Changed = false;
      SmallVector<CongruencePackage, 16> NewPartition;
      for (auto&& Package : Partition) {
        auto [MatchingMembers, RemainingMembers] = splitPackage(Package);
        if (!MatchingMembers.Members.empty()) {
          Changed = true;
          // обновить отображение значения на пакет
          auto&& refreshValueToPackage = [&](auto&& NewPackage) { 
            for (auto* Val : NewPackage.Members) {
              ValueToPackage[Val] = NewPackage.ID;
            }
            NewPartition.push_back(std::move(NewPackage));
          };
          refreshValueToPackage(MatchingMembers);
          refreshValueToPackage(RemainingMembers);
        } 
      }
      Partition.swap(NewPartition);
    } 
  }

  std::pair<CongruencePackage, CongruencePackage> 
  splitPackage(CongruencePackage& Package) {
    if (Package.Members.size() <= 1) {
      return std::pair<
        CongruencePackage, CongruencePackage>(Package, {});
    }

    // бить пакет на I и неI
    CongruencePackage MatchingMembers = {CurrentID++};
    CongruencePackage RemainingMembers = {CurrentID++}; 
    auto I = Package.Members.front();

    auto* IInst = dyn_cast<Instruction>(I);
    assert(IInst && "At this stage of the program, the package should contain only instructions");
    for (auto&& J : Package.Members) {
      auto* JInst = dyn_cast<Instruction>(J);
      assert(JInst && "At this stage of the program, the package should contain only instructions");
      if (isCongruent(IInst, JInst)) {
        MatchingMembers.Members.push_back(J);
      } else {
        RemainingMembers.Members.push_back(J);
      }
    }

    return std::make_pair(
      std::move(MatchingMembers), std::move(RemainingMembers));
  }

  bool eliminateRedundancies() {
    bool Changed = false;
    SmallVector<Instruction*, 8> ToErase;

    for (auto&& Package : Partition) {
      if (Package.Members.size() <= 1) {
        continue;
      }

      // заменить юзы каждого значения из пакета
      // соответствующим доминирующим значением
      auto&& Members = Package.Members;

      SmallPtrSet<Value*, 8> ValOpt;
      for (int I = 0; I < Members.size() - 1; ++I) {
        for (int J = I + 1; J < Members.size(); ++J) {

          auto&& opt = [&] (int X, int Y) {
            if (ValOpt.contains(Members[X]) || ValOpt.contains(Members[Y])) {
              return;
            } 
            auto* XInst = dyn_cast<Instruction>(Members[X]);
            auto* YInst = dyn_cast<Instruction>(Members[Y]);
            assert(XInst && YInst && 
              "At this stage of the program, the package should contain only instructions");

            // особенность api llvm
            // один phi может не доминировать над другим в одном бб, 
            // даже если объявлен раньше
            bool CanReplace = DT.dominates(XInst, YInst);
            if (auto* XPhi = dyn_cast<PHINode>(XInst)) {
              auto* YPhi = cast<PHINode>(YInst);
              // поэтому эта проверка
              // т.к. phi в начале бб вычисляются одновременно
              CanReplace = XPhi->getParent() == YPhi->getParent();
            }

            if (CanReplace) {
              YInst->replaceAllUsesWith(XInst);
              ++GVNRedundancyRemove;
              ToErase.push_back(YInst);
              ValOpt.insert(YInst);
              Changed = true;
            }
          };

          opt(I, J);
          opt(J, I);
        }
      }
    }

    for (auto* Inst : ToErase) {
      Inst->eraseFromParent();
      ++GVNInstRemove;
    }

    return Changed;
  }

  bool areInSamePackage(const Value *LHS, const Value *RHS) const {
    if (LHS == RHS) {
      return true;
    }

    auto LHSIt = ValueToPackage.find(LHS);
    auto RHSIt = ValueToPackage.find(RHS);

    return LHSIt != ValueToPackage.end() &&
           RHSIt != ValueToPackage.end() &&
           LHSIt->second == RHSIt->second;
  }

  bool isCongruent(const Instruction *LHS, 
                   const Instruction *RHS) const {
    if (LHS->getNumOperands() != RHS->getNumOperands()) {
        return false;
    } 

    int OpNum = LHS->getNumOperands();
    // llvm не считает phi коммутатинвной операцией
    bool unorderedOperands = LHS->isCommutative() || isa<PHINode>(LHS);

    if (!unorderedOperands) { 
      for (int i = 0; i < OpNum; ++i) {
        if (!areInSamePackage(LHS->getOperand(i), RHS->getOperand(i))) {
          return false;
        }
      }
    } else { 
      // для каждого операнда из i1 должен найтись операнд i2 
      // такой, что sameclass(i, j) и sameinbb(i, j) если phi
      // при этом все пары {i, j} должны содержать уникальные операнды
      auto* PHILHS = dyn_cast<PHINode>(LHS);
      auto* PHIRHS = dyn_cast<PHINode>(RHS);
      SmallBitVector HasSameClassInRHS(OpNum, false);
      for (int i = 0; i < OpNum; ++i) {
        bool FindSameClass = false;
        for (int j = 0; j < OpNum; ++j) {
          if (HasSameClassInRHS[j]) {
            continue;
          }
          bool IsSameClass = areInSamePackage(
            LHS->getOperand(i), RHS->getOperand(j));
          bool IsSameInBB = PHILHS || 
            PHILHS->getIncomingBlock(i) == PHIRHS->getIncomingBlock(j);
          HasSameClassInRHS[j] = IsSameClass && IsSameInBB;
          FindSameClass = IsSameClass && IsSameInBB;
          if (FindSameClass) {
            break;
          }
        }
        if (FindSameClass) {
          return false;
        }
      }
    }

    return true;
  }
};

} // namespace

// namespace analyse {

// bool sameClass(
//   Value* a, 
//   Value* b, 
//   const std::map<Value*, std::set<Value*>*>& valToP)
// {
//   if (a == b) { // если константы
//     return true;
//   }
  
//   auto aIt = valToP.find(a);
//   auto bIt = valToP.find(b);
  
//   return aIt != valToP.end() && 
//          bIt != valToP.end() && 
//          aIt->second == bIt->second;
// }

// bool match( 
//   const std::map<Value*, std::set<Value*>*>& valToP,
//   const Instruction* i1, 
//   const Instruction* i2)
// {
//   if (i1->getNumOperands() != i2->getNumOperands()) {
//       return false;
//   } 

//   int opNum = i1->getNumOperands();
//   // llvm не считает phi коммутатинвной операцией
//   bool unorderedOperands = i1->isCommutative() || isa<PHINode>(i1);

//   if (!unorderedOperands) { 
//     for (int i = 0; i < opNum; ++i) {
//       if (!sameClass(i1->getOperand(i), i2->getOperand(i), valToP)) {
//         return false;
//       }
//     }
//   } else { 
//     // для каждого операнда из i1 должен найтись операнд i2 
//     // такой, что sameclass(i, j) и sameinbb(i, j) если phi
//     // при этом все пары {i, j} должны содержать уникальные операнды
//     auto* phi1 = dyn_cast<PHINode>(i1);
//     auto* phi2 = dyn_cast<PHINode>(i2);
//     std::vector<bool> hasSameClassIn2(opNum, false);
//     for (int i = 0; i < opNum; ++i) {
//       bool findSameClass = false;
//       for (int j = 0; j < opNum; ++j) {
//         if (hasSameClassIn2[j]) {
//           continue;
//         }
//         bool isSameClass = sameClass(
//           i1->getOperand(i), i2->getOperand(j), valToP);
//         bool isSameInBB = !phi1 || 
//           phi1->getIncomingBlock(i) == phi2->getIncomingBlock(j);
//         hasSameClassIn2[j] = isSameClass && isSameInBB;
//         findSameClass = isSameClass && isSameInBB;
//         if (findSameClass) {
//           break;
//         }
//       }
//       if (!findSameClass) {
//         return false;
//       }
//     }
//   }

//   return true;
// }

// std::pair<std::set<Value*>, std::set<Value*>> 
// split(
//     const std::set<Value*>& partition, 
//     const std::map<Value*, std::set<Value*>*>& valToP) 
// {
//   if (partition.size() <= 1) {
//     return std::pair<std::set<Value*>, std::set<Value*>>(partition, {});
//   }

//   auto* I = *partition.begin();
//   std::set<Value*> packetI, packetNonI; // бить пакет на I и неI
//   packetI.insert(I);

//   auto* IInst = dyn_cast<Instruction>(I);
//   assert(IInst && "????");
//   for (auto&& J : partition) {
//     auto* JInst = dyn_cast<Instruction>(J);
//     assert(JInst && "????");
//     if (match(valToP, IInst, JInst)) {
//       packetI.insert(J);
//     } else {
//       packetNonI.insert(J);
//     }
//   }

//   return std::make_pair(packetI, packetNonI);
// }

// // лист пакетов с конгруэнтными значениями
// // все значения из одного пакета имеют соответствующие операнды,
// // тоже находящиеся в одном пакете 
// std::list<std::set<Value*>> partition(Function &F) {
//   std::list<std::set<Value*>> partitions;
//   std::map<Value*, std::set<Value*>*> valToP;

//   // начальные пакеты
//   // пакеты по опкоду, типу, дополнительной семантике

//   for (auto&& bb : F) {
//     for (auto&& i : bb) {
//       if (!isGVNCandidate(i)) {
//         continue;
//       }

//       std::set<Value*>* curP = nullptr;
//       // найти класс для инстукции
//       for (auto&& p : partitions) {
//         if (auto inst = dyn_cast<Instruction>(*p.begin())) {
//           auto* instPhi = dyn_cast<PHINode>(inst);
//           auto* iPhi = dyn_cast<PHINode>(&i);
//           // один синтаксис, один тип, один опкод, один бб у phi
//           if (inst->isSameOperationAs(&i) &&
//               inst->hasSameSubclassOptionalData(&i) &&
//             (!iPhi || instPhi->getParent() == iPhi->getParent())) 
//           {
//             curP = &p;
//             break;
//           }
//         } 
//       }

//       if (curP != nullptr) {
//         curP->insert(&i);
//         valToP[&i] = curP;
//       } else {
//         partitions.emplace_front();
//         partitions.front().insert(&i);
//         valToP[&i] = &partitions.front();
//       }
//     }
//   }

//   // вставить пакеты для аргументов
//   for (auto&& arg : F.args()) {
//     partitions.emplace_front();
//     partitions.front().insert(dyn_cast<Value>(&arg));
//     valToP[&arg] = &partitions.front();
//   }

//   // разбивать пакеты, пока возможно
//   bool change = true;
//   while (change) {
//     change = false;
//     for (auto it = partitions.begin(); it != partitions.end();) {
//       auto [packetI, packetNonI] = split(*it, valToP);
//       if (!packetNonI.empty()) {
//         change = true;
//         // обновить отображение инструкции на пакет
//         auto&& refreshValToP = [&](auto&& p) { 
//           partitions.push_front(std::move(p));
//           for (auto* i : partitions.front()) {
//             valToP[i] = &partitions.front();
//           }
//         };
//         refreshValToP(packetI);
//         refreshValToP(packetNonI);
//         it = partitions.erase(it);
//       } else {
//         ++it;
//       }
//     }
//   } 

//   return partitions;
// } 

// } // namespace analyse

// PreservedAnalyses MyGVNPath::run(Function &F,
//                                  FunctionAnalysisManager &AM) {
//   auto partitions = analyse::partition(F);

//   auto&& DT = AM.getResult<DominatorTreeAnalysis>(F);

//   bool change = false;
//   std::vector<Instruction*> toErase;

//   for (auto&& p : partitions) {
//     if (p.size() <= 1) {
//       continue;
//     }

//     // заменить юзы каждого значения из пакета
//     // соответствующим доминирующим значением
//     std::map<Value*, bool> valOpt;
//     for (auto* i : p) {
//       valOpt[i] = false;
//     }
//     std::vector pVector(p.begin(), p.end());
//     for (int i = 0; i < pVector.size() - 1; ++i) {
//       for (int j = i + 1; j < pVector.size(); ++j) {

//         auto&& opt = [&] (int x, int y) {
//           if (valOpt[pVector[x]] || valOpt[pVector[y]]) {
//             return;
//           } 
//           auto* xInst = dyn_cast<Instruction>(pVector[x]);
//           auto* yInst = dyn_cast<Instruction>(pVector[y]);
//           assert(xInst && yInst && "????");

//           // особенность api llvm
//           // один phi может не доминировать над другим в одном бб, 
//           // даже если объявлен раньше
//           bool canReplace = DT.dominates(xInst, yInst);
//           if (auto* xPhi = dyn_cast<PHINode>(xInst)) {
//             auto* yPhi = cast<PHINode>(yInst);
//             // поэтому эта проверка
//             // т.к. phi в начале бб вычисляются одновременно
//             canReplace = xPhi->getParent() == yPhi->getParent();
//           }

//           if (canReplace) {
//             yInst->replaceAllUsesWith(xInst);
//             ++GVNRedundancyRemove;
//             toErase.push_back(yInst);
//             valOpt[yInst] = true;
//             change = true;
//           }
//         };

//         opt(i, j);
//         opt(j, i);
//       }
//     }
//   }

//   for (auto* i : toErase) {
//     i->eraseFromParent();
//     ++GVNInstRemove;
//   }

//   if (!change) {
//     return PreservedAnalyses::all();
//   }

//   PreservedAnalyses PA;
//   PA.preserveSet<CFGAnalyses>();
//   return PA;
// }

PreservedAnalyses MyGVNPath::run(Function &F,
                                 FunctionAnalysisManager &AM) {
  auto&& DT = AM.getResult<DominatorTreeAnalysis>(F);
  MyGVNImpl GVN(F, DT);
  bool Res = GVN.run();
  if (Res) {
    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    return PA;
  }
  return PreservedAnalyses::all();
}