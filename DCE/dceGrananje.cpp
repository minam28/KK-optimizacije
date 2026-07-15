#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "OurCFG.h"
#include <unordered_map>
#include <vector>

using namespace llvm;

namespace
{
    struct OurDeadCodeElimination : public PassInfoMixin<OurDeadCodeElimination>
    {
        std::unordered_map<Value *, bool> Variables;
        std::unordered_map<Value *, Value *> VariablesMap;
        std::vector<Instruction *> InstructionsToRemove;

        bool InstructionEliminated;

        void handleOperand(Value *Operand)
        {
            Variables[Operand] = true;
            Variables[VariablesMap[Operand]] = true;
        }

        bool instrunctionsIdentical(Instruction &I1, Instruction &I2, const std::unordered_map<Instruction *, Instruction *> &InstructionMap)
        {
            if (I1.getOpcode() != I2.getOpcode())
                return false;

            if (I1.getNumOperands() != I2.getNumOperands())
                return false;

            if (I1.getType() != I2.getType())
                return false;

            if (isa<LoadInst>(&I1) && isa<LoadInst>(&I2)) {
                Value *Src1 = I1.getOperand(0);
                Value *Src2 = I2.getOperand(0);
                return Src1 == Src2;
            }

            for (unsigned i = 0; i < I1.getNumOperands(); ++i)
            {
                Value *Op1 = I1.getOperand(i);
                Value *Op2 = I2.getOperand(i);

                if (Op1 == Op2)
                    continue;

                if (isa<Constant>(Op1) && isa<Constant>(Op2)) {
                    if (Op1 != Op2) return false;
                    continue;
                }

                Instruction *OpInst1 = dyn_cast<Instruction>(Op1);
                Instruction *OpInst2 = dyn_cast<Instruction>(Op2);
                if (OpInst1 && OpInst2) {
                    auto It = InstructionMap.find(OpInst1);
                    if (It != InstructionMap.end() && It->second == OpInst2) {
                        continue; 
                    }
                    
                   /* auto VarIt1 = VariablesMap.find(OpInst1);
                    auto VarIt2 = VariablesMap.find(OpInst2);
                    if (VarIt1 != VariablesMap.end() && VarIt2 != VariablesMap.end()) {
                        if (VarIt1->second == VarIt2->second) {
                            continue; 
                        }
                    }*/
                }
                return false;
            }
            return true;
        }

        bool blocksIdentical(BasicBlock *BB1, BasicBlock *BB2)
        {
            if (BB1 == BB2)
                return true;

            std::unordered_map<Instruction *, Instruction *> InstructionMap;

            auto InstIt1 = BB1->begin();
            auto InstIt2 = BB2->begin();

            while (InstIt1 != BB1->end() && InstIt2 != BB2->end())
            {
                InstructionMap[&*InstIt1] = &*InstIt2;
                ++InstIt1;
                ++InstIt2;
            }

            if (InstIt1 != BB1->end() || InstIt2 != BB2->end())
                return false;

            InstIt1 = BB1->begin();
            InstIt2 = BB2->begin();

            while (InstIt1 != BB1->end())
            {
                if (!instrunctionsIdentical(*InstIt1, *InstIt2, InstructionMap))
                    return false;

                ++InstIt1;
                ++InstIt2;
            }

            return true;
        }

        void eliminateIdenticalBranches(Function &F)
        {
            for (BasicBlock &BB : F)
            {
                Instruction *Term = BB.getTerminator();
                if (!Term) continue;

                if (BranchInst *BI = dyn_cast<BranchInst>(Term))
                {
                    if (BI->isConditional())
                    {
                        BasicBlock *TrueDest = BI->getSuccessor(0);
                        BasicBlock *FalseDest = BI->getSuccessor(1);

                        if (blocksIdentical(TrueDest, FalseDest))
                        {
                            IRBuilder<> Builder(BI);
                            Builder.CreateBr(TrueDest);
                            
                            BI->eraseFromParent();
                            InstructionEliminated = true;
                            break; 
                        }
                    }
                }
            }
        }

        void eliminateDeadInstructions(Function &F)
        {
            InstructionsToRemove.clear();

            for (BasicBlock &BB : F)
            {
                for (Instruction &I : BB)
                {
                    if (I.getType()->getTypeID() != Type::VoidTyID && !isa<CallInst>(&I))
                    {
                        Variables[&I] = false;
                    }

                    if (isa<StoreInst>(&I))
                    {
                        if (Variables.find(I.getOperand(0)) != Variables.end())
                        {
                            handleOperand(I.getOperand(0)); 
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < I.getNumOperands(); i++)
                        {
                            if (Variables.find(I.getOperand(i)) != Variables.end()) 
                            {
                                handleOperand(I.getOperand(i)); 
                            }
                        }
                    }
                }
            }

            for (BasicBlock &BB : F)
            {
                for (Instruction &I : BB)
                {
                    if (isa<StoreInst>(&I))
                    {
                        if (Variables.find(I.getOperand(1)) != Variables.end() && !Variables[I.getOperand(1)])
                        {
                            InstructionsToRemove.push_back(&I);
                        }
                    }
                    else if (Variables.find(&I) != Variables.end() && !Variables[&I]) 
                    {
                        InstructionsToRemove.push_back(&I);
                    }
                }
            }

            if (InstructionsToRemove.size() > 0)
            {
                InstructionEliminated = true;
            }

            for (Instruction *Instr : InstructionsToRemove)
            {
                Instr->eraseFromParent();
            }
        }

        void eliminateUnreachableInstructions(Function &F)
        {
            std::vector<BasicBlock *> UnreachableBlocks;
            OurCFG *CFG = new OurCFG(F);
            CFG->DFS(&F.front());

            for (BasicBlock &BB : F)
            {
                if (!CFG->isReachable(&BB))
                {
                    UnreachableBlocks.push_back(&BB);
                }
            }

            if (UnreachableBlocks.size() > 0)
            {
                InstructionEliminated = true;
            }
            
            for (BasicBlock *UnreachableBlock : UnreachableBlocks)
            {
                UnreachableBlock->eraseFromParent();
            }
            delete CFG;
        }

        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM)
        {
            VariablesMap.clear();
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (isa<LoadInst>(&I)) {
                        VariablesMap[&I] = I.getOperand(0);
                    }
                }
            }

            do
            {
                InstructionEliminated = false;
                eliminateIdenticalBranches(F);
                eliminateDeadInstructions(F);
                eliminateUnreachableInstructions(F);
            } while (InstructionEliminated);

            return PreservedAnalyses::none();
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, 
        "OurDeadCodeElimination", 
        LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "our-dce") {
                        FPM.addPass(OurDeadCodeElimination());
                        return true;
                    }
                    return false;
                });
        }
    };
}
