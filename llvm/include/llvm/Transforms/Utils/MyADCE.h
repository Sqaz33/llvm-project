#ifndef LLVM_TRANSFORMS_UTILS_MYADCE_H
#define LLVM_TRANSFORMS_UTILS_MYADCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MyADCEPath : public OptionalPassInfoMixin<MyADCEPath> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MYADCE_H
