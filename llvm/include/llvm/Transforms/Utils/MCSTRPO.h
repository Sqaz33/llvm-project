#ifndef LLVM_TRANSFORMS_UTILS_MCSTRPO_H
#define LLVM_TRANSFORMS_UTILS_MCSTRPO_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MCSTRPOPath : public OptionalPassInfoMixin<MCSTRPOPath> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MCSTRPO_H
