#include <vector>
#include <set>
#include <utility>
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/CFG.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"


using namespace llvm;
using namespace std;

namespace{
	class InOutInit{
		public:
			BitVector in,out;
			InOutInit(){}
			InOutInit(BitVector input, BitVector output){
				in = input;
				out = output;
			}
	};

	class FlowValues{
		public:
			DenseMap<BasicBlock*, InOutInit> flowvals;
			FlowValues(){}
			FlowValues(DenseMap<BasicBlock*, InOutInit> f){
				flowvals = f;
			}
	};

    class LiveVar : public FunctionPass{
	public:
		static char ID;
		vector<Value*> domainvals;
		BitVector InitialBBCondition,BoundaryCondition; // Initial condition and Boundary condition
		DenseMap<StringRef,int> insnametoindex;	// For mapping instruction name to an index
		LiveVar(): FunctionPass(ID){}
	    	FlowValues run(Function &F){
			BasicBlock* firstBasicBlock = &F.front(); //First Basic Block
			BitVector boundary(domainvals.size(), false); // For finding the boundary condition
			int index=0;
			for(auto dom : domainvals){
				if(isa<Argument>(dom)==true){
					boundary[index] = 1;
				}
				index++;
			}
			BoundaryCondition = boundary;
			DenseMap<BasicBlock*, InOutInit> initialvals;

			for(auto bbiterator = F.begin(); bbiterator != F.end(); bbiterator++){
				if(&*bbiterator != &*firstBasicBlock){
					InOutInit temp(InitialBBCondition, InitialBBCondition);
					initialvals[&*bbiterator] = temp;
				}else{
					InOutInit temp(BoundaryCondition, InitialBBCondition);
					initialvals[&*bbiterator] = temp;
				}
			}
			// Below we find use-defs for a basic block.
			//https://www.youtube.com/watch?v=XTqokII5pVw for future reference
			DenseMap<BasicBlock*,BitVector> use,def;
			int sizeofset = domainvals.size();
			for(auto bbiterator = F.begin(); bbiterator != F.end(); bbiterator++){
				BitVector tmpuse(sizeofset,false);
				BitVector tmpdef(sizeofset,false);
				for(auto &i : *bbiterator){
					if(&*bbiterator == &*firstBasicBlock){
						for(auto arg_iterator=F.arg_begin(); arg_iterator!=F.arg_end();arg_iterator++){
							tmpdef.set(insnametoindex[(*arg_iterator).getName()]);
						}
					}
					for(auto j=0; j<i.getNumOperands();j++){
						if(i.getOperand(j)->hasName() && insnametoindex.find(i.getOperand(j)->getName())!=insnametoindex.end()){
							if(!tmpdef[insnametoindex[i.getOperand(j)->getName()]])
								tmpuse.set(insnametoindex[i.getOperand(j)->getName()]);
						}
					}
					if(i.hasName()==true)
						tmpdef.set(insnametoindex[i.getName()]);
				}
				use[&*bbiterator] = tmpuse;
				def[&*bbiterator] = tmpdef;
			}
			// Below code deals with phi nodes. Note that each operand of a phi instruction is live only along the edge to its predecessor.
			bool flag = false; // to assess convergence. If flag==0, not converged yet.
			int j = 0;
			while(flag==false){
				j++;
				flag = true;
				auto &listofBB = F.getBasicBlockList();
				for(auto bbiterator=listofBB.rbegin(); bbiterator!=listofBB.rend();bbiterator++){
					BitVector i(sizeofset,false);
					BitVector o(sizeofset,false);
					i = initialvals[&*bbiterator].in;
					o = initialvals[&*bbiterator].out;
					BitVector temp_out(sizeofset,false);
					for(auto iterator = succ_begin(&*bbiterator); iterator!=succ_end(&*bbiterator);iterator++){
						BitVector tin = initialvals[&**iterator].in;
						for(auto inst=(*iterator)->begin(); inst!=(*iterator)->end();inst++){

							if((*inst).getOpcode() == Instruction::PHI){
								for(int oprand=0; oprand<(*inst).getNumOperands();oprand++){
									if((*inst).getOperand(oprand)->hasName()){
										auto phi = dyn_cast<PHINode>(inst);
										if((*bbiterator).getName().compare(phi->getIncomingBlock(oprand)->getName())){
											tin[insnametoindex[(*inst).getOperand(oprand)->getName()]] = 0;
										}
									}
								}
							}
						}
						temp_out |=tin;
					}
					BitVector bv(def[&*bbiterator]);
					bv.flip();			
					bv &= temp_out;			
					bv |= use[&*bbiterator];	
					BitVector temp_in(bv);	
					initialvals[&*bbiterator].in = temp_in;
					initialvals[&*bbiterator].out = temp_out;
					if(flag && (i != temp_in || o != temp_out))
						flag = false; // not converged yet
				}
			}
			FlowValues flowvals(initialvals);
			return flowvals;
		}
	    DenseMap<int,StringRef> indextoinsname;	// For mapping indexes to instruction name
	    virtual bool runOnFunction(Function &F){
			// First output:
			domainvals.clear();
			for (auto argument = F.arg_begin();argument != F.arg_end();argument++){
				domainvals.push_back(argument);
			}
			for (inst_iterator i = inst_begin(F); i!=inst_end(F);i++){
				Value* val = &*i;
				if((*i).hasName()==true)
					domainvals.push_back(val);
			}
			int i = 0,index=0;
			int sizeOfSet = domainvals.size();

			BitVector ibc(sizeOfSet,false);
			InitialBBCondition = ibc;
			for(i=0;i<sizeOfSet;i++){
				insnametoindex[domainvals[i]->getName()] = i;
				indextoinsname[i] = domainvals[i]->getName();
			}

			FlowValues result = run(F);

			errs() << "1. Live values out of each Basic Block :\n"<< "--------------------------------------\n"<< "Basic Block : Live Values\n"<< "------------------------------------\n";
			for(auto ind = result.flowvals.begin(); ind!=result.flowvals.end();ind++){
				errs() << (*(*ind).first).getName() << "\t    :\t";
				BitVector sec = (*ind).second.out;
				for(int ind=0;ind<sizeOfSet;ind++){
					if(sec[ind]==true){
						errs() << indextoinsname[ind] << ','; //prints index converted to instruction name
					}
				}
				errs() <<"\b \n";
			}
			//Computations for second output
			DenseMap<BasicBlock*, vector<pair<int,set<StringRef>>>> live_at_ppoint; // To find liveness at each program point
			for(auto i = result.flowvals.begin();i!=result.flowvals.end();i++){

				int ppoints = (*i).first->size()+1;

				set<StringRef> live_locally;
				BitVector temp = (*i).second.out;
				for(int j=0; j<sizeOfSet;j++){
					if(temp[j]==true){
						live_locally.insert(indextoinsname[j]);
						}
				}
				ppoints=ppoints-1;
				pair<int,set<StringRef>> p(ppoints,live_locally);
				live_at_ppoint[(*i).first].push_back(p);
				for(auto insiterator=(*i).first->rbegin(); insiterator!=(*i).first->rend();insiterator++){
					if((*insiterator).getOpcode() != Instruction::PHI){
						if((*insiterator).hasName() && live_locally.find((*insiterator).getName())!=live_locally.end())
							live_locally.erase((*insiterator).getName());
						for(int oprand=0; oprand<(*insiterator).getNumOperands(); oprand++){
							if((*insiterator).getOperand(oprand)->hasName()&& insnametoindex.find((*insiterator).getOperand(oprand)->getName())!=insnametoindex.end())
							live_locally.insert((*insiterator).getOperand(oprand)->getName());
					}
					ppoints--;
					p.first = ppoints;
					p.second = live_locally;
					live_at_ppoint[(*i).first].push_back(p);
					}
					else{
						set<StringRef> tempset;
						ppoints--;
						p.first = ppoints;
						p.second = tempset;
						live_at_ppoint[(*i).first].push_back(p);
					}
				}
			}
			// Printing second output:
			map<int,int> hist; // int-to-int map for histogram
			errs() << "------------------------------------------------------------------\n";
			errs() << "Live values at each program point in each Basic Block : \n";
			for(auto iterate=live_at_ppoint.begin(); iterate!=live_at_ppoint.end(); iterate++){
				errs() << "------------------------------------------------------------------\n";
				errs() << (*iterate).first->getName() << " :\n";
				errs() << "\tprogram_point : live values\n";
				errs() << "\t----------------------------\n";
				for(auto i1 : (*iterate).second){
					errs() << "\t\t" << i1.first << "     : ";
					hist[i1.second.size()]++;
					for(auto i2 : i1.second){
						errs() << i2 << ',';
					}
					errs() << "\b \n";
				}
			}
			// Printing 3rd output
			errs() << "\n------------------------------------------------------------------\n";
			errs() << "Histogram : \n";
			errs() << "------------------------------------------------------------------\n";
			errs() << "#live_values\t: #program_points\n";
			errs() << "---------------------------------\n";
			for(auto hist_iterate=hist.begin(); hist_iterate!=hist.end(); hist_iterate++){
				errs() << "\t" << (*hist_iterate).first << "\t:\t" << (*hist_iterate).second << '\n';
			}
			return false;
	    }	
    };
}
char LiveVar::ID = 0;


static RegisterPass<LiveVar> X("LVA","Live Var Analysis in LLVM");
