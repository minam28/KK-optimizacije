#include "OurCFG.h"

OurCFG::OurCFG(llvm::Function &F)
{
  FunctionName = F.getName().str();
  CreateCFG(F);
}

void OurCFG::CreateCFG(Function &F)
{
  for (BasicBlock &BB : F) {
    for (Instruction &Instr : BB) {
      if (BranchInst *BranchInstr = dyn_cast<BranchInst>(&Instr)) {
        AdjacencyList[&BB].push_back(BranchInstr->getSuccessor(0));
        if (BranchInstr->isConditional()) {
          AdjacencyList[&BB].push_back(BranchInstr->getSuccessor(1));
        }
      }
    }
  }
}

void OurCFG::DFS(llvm::BasicBlock *Current)
{
  Visited.insert(Current);

  for (BasicBlock *Successor : AdjacencyList[Current]) {
    if (Visited.find(Successor) == Visited.end()) {
      DFS(Successor);
    }
  }
}

bool OurCFG::isReachable(llvm::BasicBlock *BB)
{
  return Visited.find(BB) != Visited.end();
}
