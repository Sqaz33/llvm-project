#ifndef LLVM_TRANSFORMS_UTILS_MCSTInstCount_H
#define LLVM_TRANSFORMS_UTILS_MCSTInstCount_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MCSTInstCountPath : public OptionalPassInfoMixin<MCSTInstCountPath> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MCSTInstCount_H
