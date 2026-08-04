#ifndef LLVM_TRANSFORMS_UTILS_MYGVN_H
#define LLVM_TRANSFORMS_UTILS_MYGVN_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MyGVNPath : public OptionalPassInfoMixin<MyGVNPath> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MYGVN_H
