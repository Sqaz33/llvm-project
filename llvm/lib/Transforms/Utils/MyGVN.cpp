//===----------------------------------------------------------------------===//
// MyGVN — устранение полной избыточности выражений
//===----------------------------------------------------------------------===//
// LLVM version 23.0.0
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
        if (!RemainingMembers.Members.empty()) {
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
        } else {
          NewPartition.push_back(std::move(Package));
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
    auto* ICMPLHS = dyn_cast<ICmpInst>(LHS);
    bool unorderedOperands = LHS->isCommutative() || 
                             isa<PHINode>(LHS) || 
                             // проверка для icmp с предикатами eq и ne
                             (ICMPLHS && ICMPLHS->isCommutative());

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
        bool FoundSameClass = false;
        for (int j = 0; j < OpNum; ++j) {
          if (HasSameClassInRHS[j]) {
            continue;
          }
          bool IsSameClass = areInSamePackage(
            LHS->getOperand(i), RHS->getOperand(j));
          bool IsSameInBB = !PHILHS || 
            PHILHS->getIncomingBlock(i) == PHIRHS->getIncomingBlock(j);
          HasSameClassInRHS[j] = IsSameClass && IsSameInBB;
          FoundSameClass = IsSameClass && IsSameInBB;
          if (FoundSameClass) {
            break;
          }
        }
        if (!FoundSameClass) {
          return false;
        }
      }
    }

    return true;
  }
};

} // namespace

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