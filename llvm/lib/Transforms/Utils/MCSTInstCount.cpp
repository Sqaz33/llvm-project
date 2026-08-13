#include "llvm/Transforms/Utils/MCSTInstCount.h"

#include <map>
#include <string>

#include "llvm/IR/Function.h"
  
using namespace llvm;

PreservedAnalyses MCSTInstCountPath::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  if (F.empty()) {
    return PreservedAnalyses::all();
  }
	
  outs() << "Function " << F.getName() << "()\n";

  std::map<std::string, int> count;
	int maxLen = 0;
  for (auto&& bb : F) {
    for (auto&& i : bb) {
			std::string op = i.getOpcodeName();
    	count[op]++;
			maxLen = op.length() > maxLen ? op.length() : maxLen;
    }
  }
	++maxLen;

	for (auto&& [op, count] : count) {
		outs() << "\t" << op << ':';
		outs() << std::string(maxLen-op.length(), ' ');
		outs() << count << '\n';
	}

  return PreservedAnalyses::all();
}
