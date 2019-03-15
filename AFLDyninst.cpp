#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <sstream>
#include <climits>
#include <cstring>
#include <fstream>
using namespace std;

/* Command line parsing */
#include <getopt.h>

/* DyninstAPI includes */
#include "BPatch.h"
#include "BPatch_binaryEdit.h"
#include "BPatch_flowGraph.h"
#include "BPatch_function.h"
#include "BPatch_point.h"

using namespace Dyninst;

char *originalBinary;
char *outputBinary;

char *analyzedBListPath = NULL;
char *instrumentedBListPath = NULL;
char *skipListPath = NULL;
char *tracePath = NULL;

bool skipMainModule = false;
bool includeSharedLib = false;
bool verbose = false;
bool initInstrumented = false;
bool useBlkTracing = false;
bool useEdgeTracing = false;
bool useOriginalEdgeTracing = false;

int numBlksToSkip = 0;
int dynfix = 0;
int numInEdges = 0;
int numOutEdges = 0;
int numBlks = 0;
int perfBoostOpt = 0;

unsigned int minBlkSize = 1;

Dyninst::Address entryPoint;

set < string > instrumentLibraries;
set < string > runtimeLibraries;
set < string > skipLibraries;
set < unsigned long > skipAddresses;

BPatch_function *forkServer;
BPatch_function *saveRdi;
BPatch_function *restoreRdi;
BPatch_function *traceBlocks;
BPatch_function *traceEdges;
BPatch_function *traceEdgesOrig;

void initSkipLibraries ()
{
	/* List of shared libraries to skip instrumenting. */
	skipLibraries.insert ("libAflDyninst.so");		
	skipLibraries.insert ("libc.so.6");
	skipLibraries.insert ("libc.so.7");
	skipLibraries.insert ("ld-2.5.so");
	skipLibraries.insert ("ld-linux.so.2");
	skipLibraries.insert ("ld-lsb.so.3");
	skipLibraries.insert ("ld-linux-x86-64.so.2");
	skipLibraries.insert ("ld-lsb-x86-64.so");
	skipLibraries.insert ("ld-elf.so.1");
	skipLibraries.insert ("ld-elf32.so.1");
	skipLibraries.insert ("libstdc++.so.6");
	return;
}

const char *instLibrary = "libAFLDyninst.so";

static const char *OPT_STR = "M:N:X:Ll:r:A:O:F:BEeDI:Vx";
static const char *USAGE = " [input_binary] [analysis_options] [inst_options]\n \
	Analysis options:\n \
		-M: minimum block size (default: 1)\n \
		-N: number of initial blocks to skip (default: 0)\n \
		-X: input list of block addresses to skip\n \
		-L: don't analyze binary, only libraries\n \
		-l: linked libraries to analyze\n \
		-r: runtime libraries to analyze\n \
		-A: output list for all blocks analyzed\n \
	Instrumentation options:\n \
		-O: output instrumented binary\n \
		-F: forkserver address (required for stripped binaries)\n \
		-B: trace blocks\n \
		-E: trace edges\n \
		-D: attempt fixing Dyninst register bug\n \
		-I: output list for all blocks instrumented\n \
	Additional options:\n \
		-V: verbose mode\n";

bool parseOptions(int argc, char **argv){
	originalBinary = argv[1];
	int c;
	while ((c = getopt(argc, argv, OPT_STR)) != -1){
		switch ((char) c) {
			case 'x':
				perfBoostOpt++;
				if (perfBoostOpt > 2){
					fprintf(stderr, "Warning: maximum performance level is 2\n");
        			perfBoostOpt = 2;
        		}
        		break;
			case 'M':	
				minBlkSize = atoi(optarg);
				break;
			case 'N':
				numBlksToSkip = atoi(optarg);
				break;			
			case 'X':
            	skipListPath = optarg;
            	break;
			case 'L':
				skipMainModule = true;
				break;
			case 'l':
				instrumentLibraries.insert(optarg);
				break;
			case 'r':
				runtimeLibraries.insert(optarg);
				break;
			case 'A':
	            analyzedBListPath = optarg;
	            break; 
			case 'O':
				outputBinary = optarg;
				break;	
			case 'F':	
				entryPoint = strtoul(optarg, NULL, 16);;
				break;
			case 'B':
				useBlkTracing = true;
				break;
			case 'E':
				useEdgeTracing = true;
				break;
			case 'e':
				useOriginalEdgeTracing = true;
				break;				
			case 'D':
				dynfix = 1;
				break;			
			case 'I':
            	instrumentedBListPath = optarg;
            	break; 	
			case 'V':
				verbose = true;
				break;
			default:
				cerr << "Usage: " << argv[0] << USAGE;
				return false;
		}
	}

	if (originalBinary == NULL) {
		cerr << "Input binary is required!\n" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;
	}

	if (skipMainModule && instrumentLibraries.empty()) {
		cerr << "If using option -L , option -l is required.\n" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;
	}

	if (analyzedBListPath)
		remove(analyzedBListPath);
	if (instrumentedBListPath)
		remove(instrumentedBListPath);

	return true;
}

/* Extracts function based on input name. Useful for getting instrumentation library callbacks. */
BPatch_function *findFuncByName(BPatch_image * appImage, char *curFuncName) {
	BPatch_Vector < BPatch_function * >funcs;

	if (NULL == appImage->findFunction(curFuncName, funcs) || !funcs.size()
			|| NULL == funcs[0]) {
		cerr << "Failed to find " << curFuncName << " function." << endl;
		return NULL;
	}

	return funcs[0];
}

/* Insert callback to initialization function in the instrumentation library. */
int insertCallToInit(BPatch_binaryEdit *appBin, char *curFuncName, BPatch_function *instIncFunc, BPatch_point *curBlk, unsigned long curBlkAddr, unsigned int curBlkSize){
	/* init has no args */
	BPatch_Vector < BPatch_snippet * >instArgs;	 
	BPatch_funcCallExpr instExprTrace(*instIncFunc, instArgs);

	/* Insert the snippet at function entry */
	BPatchSnippetHandle *handle = appBin->insertSnippet(instExprTrace, *curBlk, BPatch_callBefore, BPatch_lastSnippet);

	if (!handle) {
		cerr << "Failed to insert fork server callback." << endl;
		return EXIT_FAILURE;
	}

	/* Print some useful info, if requested. */
	if (verbose)
		cout << "Inserted fork server callback at 0x" << hex << curBlkAddr << " of " << curFuncName << " of size " << dec << curBlkSize << endl;
	
	return 0;
}

int insertTraceCallback(BPatch_binaryEdit *appBin, char *curFuncName, BPatch_function *instBlksIncFunc, BPatch_point *curBlk, unsigned long curBlkAddr, unsigned int curBlkSize, unsigned int curBlkID){

	/* Verify curBlk is instrumentable. */
	if (curBlk == NULL) {
		cerr << "Failed to find entry at 0x" << hex << curBlkAddr << endl;
		return EXIT_FAILURE;
	}	

	BPatch_Vector < BPatch_snippet * >instArgsDynfix;
	BPatch_Vector < BPatch_snippet * >instArgs;
	
	BPatch_constExpr argCurBlkID(curBlkID);
	instArgs.push_back(&argCurBlkID);

	BPatch_funcCallExpr instExprSaveRdi(*saveRdi, instArgsDynfix);
	BPatch_funcCallExpr instExprRestRdi(*restoreRdi, instArgsDynfix);
	BPatch_funcCallExpr instExprTrace(*instBlksIncFunc, instArgs);
	BPatchSnippetHandle *handle;
	
	/* RDI fix handling. */
	if (dynfix) 
		handle = appBin->insertSnippet(instExprSaveRdi, *curBlk, BPatch_callBefore, BPatch_lastSnippet);
	
	/* Instruments the basic block. */
	handle = appBin->insertSnippet(instExprTrace, *curBlk, BPatch_callBefore, BPatch_lastSnippet);
	
	/* Wrap up RDI fix handling. */
	if (dynfix) 
		handle = appBin->insertSnippet(instExprRestRdi, *curBlk, BPatch_callBefore, BPatch_lastSnippet);

	/* Verify instrumenting worked. If all good, advance blkIndex and return. */
	if (!handle) {
		cerr << "Failed to insert trace callback at 0x" << hex << curBlkAddr << endl;
	}

	if (handle){
		/* If path to output instrumented bb addrs list set, save the addresses of each basic block instrumented to that file. */
        if (instrumentedBListPath){
            FILE *blksListFile = fopen(instrumentedBListPath, "a"); 
            fprintf(blksListFile, "%lu,%u\n", curBlkAddr, curBlkID); 
            fclose(blksListFile); 
        } 
	}

	/* Print some useful info, if requested. */
	if (verbose)
		cout << "Inserted trace callback at 0x" << hex << curBlkAddr << " of " << curFuncName << " of size " << dec << curBlkSize << endl;

	return 0;
}


void iterateBlocks(BPatch_binaryEdit *appBin, vector < BPatch_function * >::iterator funcIter, int *blkIndex) {

	/* Extract the function's name, and its pointer from the parent function vector. */
	BPatch_function *curFunc = *funcIter;
	char curFuncName[1024];
	curFunc->getName(curFuncName, 1024); 
 
	/* Extract the function's CFG. */
	BPatch_flowGraph *curFuncCFG = curFunc->getCFG();
	if (!curFuncCFG) {
		cerr << "Failed to find CFG for function " << curFuncName << endl;
		return;
	}

	/* Extract the CFG's basic blocks and verify the number of blocks isn't 0. */
	BPatch_Set < BPatch_basicBlock * >curFuncBlks;
	if (!curFuncCFG->getAllBasicBlocks(curFuncBlks)) {
		cerr << "Failed to find basic blocks for function " << curFuncName << endl;
		return;
	} 
	if (curFuncBlks.size() == 0) {
		cerr << "No basic blocks for function " << curFuncName << endl;
		return;
	}

	/* Set up this function's basic block iterator and start iterating. */
	BPatch_Set < BPatch_basicBlock * >::iterator blksIter;

	for (blksIter = curFuncBlks.begin(); blksIter != curFuncBlks.end(); blksIter++) {

		/* Get the current basic block, and its size and address. */
		BPatch_point *curBlk = (*blksIter)->findEntryPoint();
		unsigned int curBlkSize = (*blksIter)->size();	 
		/* Compute the basic block's adjusted address.	*/
		unsigned long curBlkAddr = (*blksIter)->getStartAddress();
		/* Non-PIE binary address correction. */ 
		curBlkAddr = curBlkAddr - (long) 0x400000;
		/* Set curBlkID as index in basic block list. */
		unsigned int curBlkID = *blkIndex;

		/* If we're not instrumenting, jump to basic block/edge counting. */
		if (!outputBinary) goto save_binary_statistics;

		/* If using forkserver, instrument the first block in function <main> with the forkserver callback. */
        if (string(curFuncName) == string("main")){

        	if (!initInstrumented){
        		insertCallToInit(appBin, curFuncName, forkServer, curBlk, curBlkAddr, curBlkSize);
        		initInstrumented = true;
        	}

        	else continue;
        }

		/* Other basic blocks to ignore. */
		if (string(curFuncName) == string("init") ||
			string(curFuncName) == string("_init") ||
			string(curFuncName) == string("fini") ||
			string(curFuncName) == string("_fini") ||
			string(curFuncName) == string("register_tm_clones") ||
			string(curFuncName) == string("deregister_tm_clones") ||
			string(curFuncName) == string("frame_dummy") ||
			string(curFuncName) == string("__do_global_ctors_aux") ||
			string(curFuncName) == string("__do_global_dtors_aux") ||
			string(curFuncName) == string("__libc_csu_init") ||
			string(curFuncName) == string("__libc_csu_fini") ||
			string(curFuncName) == string("start") ||
			string(curFuncName) == string("_start") ||
			string(curFuncName) == string("__libc_start_main") ||
			string(curFuncName) == string("__gmon_start__") ||
			string(curFuncName) == string("__cxa_atexit") ||
			string(curFuncName) == string("__cxa_finalize") ||
			string(curFuncName) == string("__assert_fail") ||
			string(curFuncName) == string("free") ||
			string(curFuncName) == string("fnmatch") ||
			string(curFuncName) == string("readlinkat") ||
			string(curFuncName) == string("malloc") ||
			string(curFuncName) == string("calloc") ||
			string(curFuncName) == string("realloc") ||
			string(curFuncName) == string("argp_failure") ||
			string(curFuncName) == string("argp_help") ||
			string(curFuncName) == string("argp_state_help") ||
			string(curFuncName) == string("argp_error") ||
			string(curFuncName) == string("argp_parse") ||
			string(curFuncName) == string("__afl_maybe_log") ||
			string(curFuncName) == string("__fsrvonly_store") ||
			string(curFuncName) == string("__fsrvonly_return") ||
			string(curFuncName) == string("__fsrvonly_setup") ||
			string(curFuncName) == string("__fsrvonly_setup_first") ||
			string(curFuncName) == string("__fsrvonly_forkserver") ||
			string(curFuncName) == string("__fsrvonly_fork_wait_loop") ||
			string(curFuncName) == string("__fsrvonly_fork_resume") ||
			string(curFuncName) == string("__fsrvonly_die") ||
			string(curFuncName) == string("__fsrvonly_setup_abort") ||
			string(curFuncName) == string(".AFL_SHM_ENV") || 
			(string(curFuncName).substr(0,4) == string("targ") \
				&& isdigit(string(curFuncName)[5]))) {
			continue;
		}

        /* If the address is in the list of addresses to skip, skip it. */
		if (skipAddresses.find(curBlkAddr) != skipAddresses.end()) {
        	(*blkIndex)++;
            continue;
        }

		/* If we're not in forkserver-only mode, check the block's indx/size and skip if necessary. */
		if (*blkIndex < numBlksToSkip || curBlkSize < minBlkSize) {		
			(*blkIndex)++;
			continue;
		}

		/* AFL-Dyninst V2 performance boost no.1 */
		if (perfBoostOpt >= 1){
			
			if ((*blksIter)->isEntryBlock() == false) {
				bool good = false;

			BPatch_Vector <BPatch_basicBlock *> blkInBlocks;
			(*blksIter)->getSources(blkInBlocks);
			
			for (unsigned int i = 0; i < blkInBlocks.size() && good == false; i++) {
				BPatch_Vector <BPatch_basicBlock *> blkOutBlocks;
				blkInBlocks[i]->getTargets(blkOutBlocks);
				
				if (blkOutBlocks.size() > 1)
					good = true;
			}
			if (good == false)
				continue;
	      	}
      	}

		/* If path to output analyzed bb addrs list set, save the addresses of each basic block visited. */
        if (analyzedBListPath){
            FILE *blksListFile = fopen(analyzedBListPath, "a"); 
            fprintf(blksListFile, "%lu\n", curBlkAddr); 
            fclose(blksListFile); 
        } 

        if (useBlkTracing)
        	insertTraceCallback(appBin, curFuncName, traceBlocks, curBlk, curBlkAddr, curBlkSize, curBlkID);

        if (useEdgeTracing)
        	insertTraceCallback(appBin, curFuncName, traceEdges, curBlk, curBlkAddr, curBlkSize, curBlkID);

        if (useOriginalEdgeTracing)
        	insertTraceCallback(appBin, curFuncName, traceEdgesOrig, curBlk, curBlkAddr, curBlkSize, curBlkID);        

        save_binary_statistics:

       	/* Extract and increment outgoing and incoming edge counts. */
		BPatch_Vector<BPatch_edge*> blkOutEdges;
    	(*blksIter)->getOutgoingEdges(blkOutEdges);	
    	numOutEdges += blkOutEdges.size();

    	BPatch_Vector<BPatch_edge*> blkInEdges;
    	(*blksIter)->getIncomingEdges(blkInEdges);	
    	numInEdges += blkInEdges.size();

    	/* Increment number of blocks. */
		(*blkIndex)++;
		numBlks++;

		continue;
	}

	return;
}

void initSkipAddresses(){
    
    if (skipListPath != NULL && access(skipListPath, R_OK) == 0){

    	char line[256];

        FILE *skipListFile = fopen(skipListPath, "r"); 

        while (fgets(line, sizeof(line), skipListFile)){
        	unsigned long addr = atoi(line);
            skipAddresses.insert(addr);
        }

        fclose(skipListFile);

    }
    return;
}

int main(int argc, char **argv) {
	
	/* Parse arguments. */
	if (!parseOptions(argc, argv)) {
		return EXIT_FAILURE;
	}

	/* Initialize libraries and addresses to skip. */
	initSkipLibraries();
    initSkipAddresses();

	/* Set up Dyninst BPatch object. */
	BPatch bpatch;
	
	/* Performance-related options. */ 
	bpatch.setDelayedParsing(true); 	/* ??? 							*/
	bpatch.setLivenessAnalysis(false); 	/* ++speed on true (but crashes)*/
	bpatch.setMergeTramp(true); 		/* ++speed on true 				*/
	bpatch.setInstrStackFrames(false);  /* ++speed on false 			*/
 
	/* AFL-Dyninst V2 performance boost no.2 */
	if (perfBoostOpt >= 2) {
		bpatch.setSaveFPR(false); 		/* ++speed on false 			*/
		bpatch.setTrampRecursive(true);	/* ++speed on true 				*/
	}

	/* Verify existence of original binary. */
	BPatch_binaryEdit *appBin = bpatch.openBinary(originalBinary, instrumentLibraries.size() != 1);
	if (appBin == NULL) {
		cerr << "Failed to open binary" << endl;
		return EXIT_FAILURE;
	}

	/* Extract BPatch image of original binary. */
	BPatch_image *appImage = appBin->getImage();
	if (!appBin->loadLibrary(instLibrary)) {
		cerr << "Failed to open instrumentation library " << instLibrary << endl;
		cerr << "It needs to be located in the curent working directory." << endl;
		return EXIT_FAILURE;
	}

	/* Verify code coverage functions in the instrumentation library. */
	forkServer	 	= findFuncByName(appImage, (char *) "forkServer");
	traceBlocks		= findFuncByName(appImage, (char *) "traceBlocks");
	traceEdges		= findFuncByName(appImage, (char *) "traceEdges");
	traceEdgesOrig	= findFuncByName(appImage, (char *) "traceEdgesOrig");	
	saveRdi			= findFuncByName(appImage, (char *) "saveRdi");
	restoreRdi		= findFuncByName(appImage, (char *) "restoreRdi");

	if (!forkServer || !saveRdi || !restoreRdi || !traceBlocks || !traceEdges || ! traceEdgesOrig) {
		cerr << "Instrumentation library lacks callbacks!" << endl;
		return EXIT_FAILURE;
	}

	/* Set up modules iterator and other necessary variables. */
	vector < BPatch_module * >*modules = appImage->getModules();
	vector < BPatch_module * >::iterator moduleIter;
	int blkIndex = 0;
	
	/* For each module, iterate through its functions. */
	for (moduleIter = modules->begin(); moduleIter != modules->end(); ++moduleIter) {
		
		/* Extract module name and verify whether it should be skipped based. */
		char curModuleName[1024];
		(*moduleIter)->getName(curModuleName, 1024);
		if ((*moduleIter)->isSharedLib ()) {
			if (!includeSharedLib || skipLibraries.find (curModuleName) != skipLibraries.end ()) {
				if (verbose) {
					cout << "Skipping library: " << curModuleName << endl;
				}
				continue;
			}
		}

		/* Extract the module's functions and iterate through its basic blocks. */
		vector < BPatch_function * >*funcsInModule = (*moduleIter)->getProcedures();
		vector < BPatch_function * >::iterator funcIter;
		
		for (funcIter = funcsInModule->begin(); funcIter != funcsInModule->end(); ++funcIter) { 
			/* Go through each function's basic blocks and insert callbacks accordingly. */
			iterateBlocks(appBin, funcIter, &blkIndex);
		}
	}

	/* If specified, save the instrumented binary and verify success. */
	if (outputBinary){
		if (verbose)
			cout << "Saving the instrumented binary to " << outputBinary << " ..." << endl;
		if (!appBin->writeFile(outputBinary)) {
			cerr << "Failed to write output file: " << outputBinary << endl;
			return EXIT_FAILURE;
		}
	}
	if (verbose)
		cout << "All done!" << endl;
		cout << numBlks << " total blocks." << endl;
		cout << numOutEdges << " total outgoing edges." << endl;
		cout << numInEdges << " total incoming edges." << endl;

	return EXIT_SUCCESS;
}
