#ifndef LLVM_TRANSFORMS_UTILS_MYDCE_H
#define LLVM_TRANSFORMS_UTILS_MYDCE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MyDCEPath : public OptionalPassInfoMixin<MyDCEPath> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MYDCE_H
