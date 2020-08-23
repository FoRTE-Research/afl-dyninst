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

bool includeSharedLib = false;
bool verbose = false;

bool skipUnrecogFuncs = true;
bool useBlkTracing = false;
bool useEdgeTracing = false;
bool useDynFix = 0;

int numBlksToSkip = 0;
int numInEdges = 0;
int numOutEdges = 0;
int numBlks = 0;
int numBlksInst = 0;
int perfBoostOpt = 0;

bool fsrvInstrumented = false;
unsigned int minBlkSize = 1;

Dyninst::Address customFsrvAddr;

set < string > modulesToInstrument;
set < string > skipLibraries;
set < unsigned long > skipAddresses;

BPatch_function *forkServer;
BPatch_function *saveRdi;
BPatch_function *restoreRdi;
BPatch_function *traceBlocks;
BPatch_function *traceEdges;

void initSkipLibraries ()
{
	/* List of shared libraries to skip instrumenting. */
	
	skipLibraries.insert ("libAflDyninst.cpp");	
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

const char *instLibrary = "libAflDyninst.so";

static const char *OPT_STR = "m:n:uo:f:bedvx";
static const char *USAGE = " [input_binary] [analysis_options] [inst_options]\n \
	Analysis options:\n \
		-m int  - minimum block size (default: 1)\n \
		-n int  - number of initial blocks to skip (default: 0)\n \
		-u      - do not skip over unrecognizable functions (recommended\n \
	              only for closed-src or stripped binaries; default: skip)\n \
	Instrumentation options:\n \
		-o file - output instrumented binary\n \
		-f addr - custom forkserver address (required for stripped binaries)\n \
		-b      - trace blocks\n \
		-e      - trace edges\n \
		-d      - attempt fixing Dyninst register bug\n \
		-x      - optimization level 1 (graph optimizations)\n \
		-xx     - optimization level 2 (Dyninst settings)\n \
	Additional options:\n \
		-v      - verbose mode\n";

bool parseOptions(int argc, char **argv){
	originalBinary = argv[1];
	if (!originalBinary){
		cerr << "ERROR: Missing input binary!" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;
	}

	cout << "Analyzing binary: " << originalBinary << endl;

	modulesToInstrument.insert(originalBinary);
	int c;
	while ((c = getopt(argc, argv, OPT_STR)) != -1){
		switch ((char) c) {
			case 'm':	
				minBlkSize = atoi(optarg);
				break;
			case 'n':
				numBlksToSkip = atoi(optarg);
				break;			
			case 'u': 
				skipUnrecogFuncs = false;
				break;
			case 'o':
				outputBinary = optarg;
				break;	
			case 'f':	
				customFsrvAddr = strtoul(optarg, NULL, 16);;
				break;
			case 'b':
				useBlkTracing = true;
				break;
			case 'e':
				useEdgeTracing = true;
				break;			
			case 'd':
				useDynFix = true;
				break;			
			case 'x':
				perfBoostOpt++;
				if (perfBoostOpt > 2){
					cerr << "WARNING: Maximum optimization level is 2." << endl;
        			perfBoostOpt = 2;
        		}
        		break;

			case 'v':
				verbose = true;
				break;
			default:
				cerr << "Usage: " << argv[0] << USAGE;
				return false;
		}
	}

	if (!outputBinary && (useEdgeTracing || useBlkTracing)){
		cerr << "WARNING: Output binary missing - instrumentation will not be applied!\n" << endl;	
	}	

	if (outputBinary && (!useEdgeTracing && !useBlkTracing)){
		cerr << "ERROR: Output binary specified but missing instrumentation mode!\n" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;	
	}	

	if (outputBinary && (useEdgeTracing && useBlkTracing)){
		cerr << "ERROR: Cannot apply both block AND edge tracing - must select only one!\n" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;		
	}

	if (originalBinary == NULL) {
		cerr << "ERROR: Input binary is required!\n" << endl;
		cerr << "Usage: " << argv[0] << USAGE;
		return false;
	}

	return true;
}

/* Extracts function based on input name. Useful 
 * for getting instrumentation library callbacks. */

BPatch_function *findFuncByName(BPatch_image * appImage, char *curFuncName) {
	BPatch_Vector < BPatch_function * >funcs;

	if (NULL == appImage->findFunction(curFuncName, funcs)
		|| !funcs.size() || NULL == funcs[0]) {
		
		cerr << "Failed to find " << curFuncName << " function." << endl;
		return NULL;
	}

	return funcs[0];
}

/* Insert callback to initialization function 
 * in the instrumentation library. */

int insertCallToFsrv(BPatch_binaryEdit *appBin, char *curFuncName, BPatch_function *instIncFunc, \
	BPatch_point *curBlk, unsigned long curBlkAddr, unsigned int curBlkSize){	
	
	BPatch_Vector < BPatch_snippet * >instArgs;	 
	BPatch_funcCallExpr instExprTrace(*instIncFunc, instArgs);

	/* Insert the snippet at function entry */
	
	BPatchSnippetHandle *handle = appBin->insertSnippet(instExprTrace, *curBlk, BPatch_callBefore, BPatch_lastSnippet);

	if (!handle) {
		cerr << "ERROR: Failed to insert fork server callback!" << endl;
		return EXIT_FAILURE;
	}

	/* Print some useful info, if requested. */
	
	cout << "SUCCESS: Inserted fork server callback at 0x" << hex << curBlkAddr << " of function "
	<< curFuncName << " of size " << dec << curBlkSize << endl;
	
	return 0;
}

int insertTraceCallback(BPatch_binaryEdit *appBin, char *curFuncName, BPatch_function *instBlksIncFunc, \
	BPatch_point *curBlk, unsigned long curBlkAddr, unsigned int curBlkSize, unsigned int curBlkID){

	/* Verify curBlk is instrumentable. */
	
	if (curBlk == NULL) {
		cerr << "ERROR: Failed to find entry at 0x" << hex << curBlkAddr << endl;
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
	
	if (useDynFix) 
		handle = appBin->insertSnippet(instExprSaveRdi, *curBlk, BPatch_callBefore, BPatch_lastSnippet);
	
	/* Instruments the basic block. */
	
	handle = appBin->insertSnippet(instExprTrace, *curBlk, BPatch_callBefore, BPatch_lastSnippet);
	
	/* Wrap up RDI fix handling. */
	
	if (useDynFix) 
		handle = appBin->insertSnippet(instExprRestRdi, *curBlk, BPatch_callBefore, BPatch_lastSnippet);

	/* Verify instrumenting worked. If all good, 
	 * advance blkIndex and return. */
	
	if (!handle)
		cerr << "WARNING: Failed to insert trace callback at 0x" << hex << curBlkAddr << endl;

	if (handle)
		numBlksInst++;

	/* Print some useful info, if requested. */
	
	if (verbose)
		cout << "SUCCESS: Inserted trace callback at 0x" << hex << curBlkAddr << " of function " 
		<< curFuncName << " of size " << dec << curBlkSize << endl;

	return 0;
}


void iterateBlocks(BPatch_binaryEdit *appBin, vector < BPatch_function * >::iterator funcIter, int *blkIndex) {

	/* Extract the function's name, and its pointer 
	 * from the parent function vector. */
	
	BPatch_function *curFunc = *funcIter;
	char curFuncName[1024];
	curFunc->getName(curFuncName, 1024); 
 
	/* Extract the function's CFG. */
	
	BPatch_flowGraph *curFuncCFG = curFunc->getCFG();
	if (!curFuncCFG) {
		cerr << "WARNING: Failed to find CFG for function " << curFuncName << endl;
		return;
	}

	/* Extract the CFG's basic blocks and verify 
	 * the number of blocks isn't 0. */
	
	BPatch_Set < BPatch_basicBlock * >curFuncBlks;
	if (!curFuncCFG->getAllBasicBlocks(curFuncBlks)) {
		cerr << "WARNING: Failed to find basic blocks for function " << curFuncName << endl;
		return;
	} 
	
	if (curFuncBlks.size() == 0) {
		cerr << "WARNING: No basic blocks for function " << curFuncName << endl;
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
		//curBlkAddr = curBlkAddr - (long) 0x400000;
		
		/* Set curBlkID as index in basic block list. */
		
		unsigned int curBlkID = *blkIndex;

		/* If using forkserver, instrument the first 
		 * block in function <main> with the callback. 
		 * If user specifies an address, insert on the 
		 * block whose address matches the provided address. */

		if (outputBinary){
			if (customFsrvAddr){			
				if (curBlkAddr == customFsrvAddr && !fsrvInstrumented){
		    		cerr << "WARNING: Using custom forkserver address: 0x" << hex << customFsrvAddr << endl;
		    		insertCallToFsrv(appBin, curFuncName, forkServer, curBlk, curBlkAddr, curBlkSize);
		    		fsrvInstrumented = true;
		    		continue;
		    	}			
			}

			else {
				if (string(curFuncName) == string("main") && !fsrvInstrumented) {
		    		insertCallToFsrv(appBin, curFuncName, forkServer, curBlk, curBlkAddr, curBlkSize);
		    		fsrvInstrumented = true;
		    		continue;
		    	}		
		    }
		}

		/* Blacklisted functions that we don't care about. */
		
		if ((string(curFuncName) == string("init") ||
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
			string(curFuncName) == string("argp_parse")) || (

			/* Handle skipping of unrecognizable blocks 
			 * (e.g., 'targ9'), if desired. */ 

			skipUnrecogFuncs == true
				&& (string(curFuncName).substr(0,4) == string("targ"))
				&& isdigit(string(curFuncName)[5])
			)) {	

			continue;
		}

        /* If the address is in the list of addresses to skip, skip it. */
		
		if (skipAddresses.find(curBlkAddr) != skipAddresses.end()) {
        	(*blkIndex)++;
            continue;
        }

		/* If we're not in forkserver-only mode, check the block's indx 
		 * and size and skip if necessary. */
		
		if (*blkIndex < numBlksToSkip || curBlkSize < minBlkSize) {		
			(*blkIndex)++;
			continue;
		}

		/* AFL-Dyninst V2 performance boost level 1.
		 * Cull those blocks that are the only children
		 * of their parent blocks. */
		
		if (perfBoostOpt >= 1){
			if ((*blksIter)->isEntryBlock() == false) {
				bool onlyChild = true;
				BPatch_Vector <BPatch_basicBlock *> blkInBlocks;
				(*blksIter)->getSources(blkInBlocks);
				
				for (unsigned int i = 0; i < blkInBlocks.size() 
					&& onlyChild == true; i++) {

					BPatch_Vector <BPatch_basicBlock *> blkOutBlocks;
					blkInBlocks[i]->getTargets(blkOutBlocks);
					
					if (blkOutBlocks.size() > 1)
						onlyChild = false;
				}
				if (onlyChild == true)
					continue;
	      	}
      	}

      	/* Add desired tracing callback. */ 

        if (useBlkTracing)
        	insertTraceCallback(appBin, curFuncName, traceBlocks, \
        		curBlk, curBlkAddr, curBlkSize, curBlkID);

        if (useEdgeTracing)
        	insertTraceCallback(appBin, curFuncName, traceEdges, \
        		curBlk, curBlkAddr, curBlkSize, curBlkID);

    	/* Increment number of blocks. */
		
		(*blkIndex)++;
		numBlks++;

		continue;
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

	/* Set up Dyninst BPatch object. */
	
	BPatch bpatch;
	
	/* Performance-related options. */ 
	
	bpatch.setDelayedParsing(true); 	/* ??? 							 */
	bpatch.setLivenessAnalysis(false); 	/* ++speed on true (but crashes) */
	bpatch.setMergeTramp(true); 		/* ++speed on true 				 */
	bpatch.setInstrStackFrames(false);  /* ++speed on false 			 */
	bpatch.setTrampRecursive(true);		/* ++speed on true 				 */
 
	/* AFL-Dyninst V2 performance boost no.2 */
	
	if (perfBoostOpt >= 2) {
		bpatch.setSaveFPR(false); 		/* ++speed on false 			 */
	}

	/* Verify existence of original binary. */
	
	BPatch_binaryEdit *appBin = bpatch.openBinary(originalBinary, modulesToInstrument.size() != 1);
	if (appBin == NULL) {
		cerr << "ERROR: Failed to open input binary!" << endl;
		return EXIT_FAILURE;
	}

	/* Extract BPatch image of original binary. */
	
	BPatch_image *appImage = appBin->getImage();
	if (!appBin->loadLibrary(instLibrary)) {
		cerr << "ERROR: Failed to open instrumentation library!" << instLibrary << endl;
		return EXIT_FAILURE;
	}

	/* Verify code coverage functions in the instrumentation library. */
	
	forkServer	 	= findFuncByName(appImage, (char *) "forkServer");
	traceBlocks		= findFuncByName(appImage, (char *) "traceBlocks");
	traceEdges		= findFuncByName(appImage, (char *) "traceEdges");
	saveRdi			= findFuncByName(appImage, (char *) "saveRdi");
	restoreRdi		= findFuncByName(appImage, (char *) "restoreRdi");

	if (!forkServer || !saveRdi || !restoreRdi || !traceBlocks || !traceEdges ) {
		cerr << "ERROR: Instrumentation library lacks callbacks!" << endl;
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
		if ((*moduleIter)->isSharedLib()) {
			if (((modulesToInstrument.find(curModuleName) == modulesToInstrument.end()) 
				&& (string(curModuleName).find(".so") != string::npos)) 
				|| (string(curModuleName).find("libAflDyninst") != string::npos)) {	
				
				cerr << "WARNING: Skipping shared module: " << curModuleName << endl;				
				continue;
			}
		}

		/* Report where we're at in analyzing the modules. */

		if ((*moduleIter)->isSharedLib())
			cout << "LOGGING: Analyzing shared module: " << curModuleName << endl;		
		else {
			if (verbose)
				cerr << "LOGGING: Analyzing module: " << curModuleName << endl;		
		}

		/* Extract the module's functions and iterate 
		 * through its basic blocks. */
		
		vector < BPatch_function * >*funcsInModule = (*moduleIter)->getProcedures();
		vector < BPatch_function * >::iterator funcIter;
		
		for (funcIter = funcsInModule->begin(); funcIter != funcsInModule->end(); ++funcIter) { 

			/* Go through each function's basic blocks 
			 * and insert callbacks accordingly. */
			
			iterateBlocks(appBin, funcIter, &blkIndex);
		}
	}

	/* Report analyzed / instrumented blocks. */

	cout << "LOGGING: " << numBlks << " blocks analyzed" << endl;
	cout << "LOGGING: " << numBlksInst << " blocks instrumented" << endl;
	
	/* If specified, save the instrumented 
	 * binary and verify success. */
	
	if (outputBinary){
		cout << "LOGGING: Saving output binary to " << outputBinary << " ..." << endl;
		if (!appBin->writeFile(outputBinary)) {
			cerr << "ERROR: Failed to save output binary: " << outputBinary << endl;
			return EXIT_FAILURE;
		}
	}

	cout << "SUCCESS: All done!" << endl;

	return EXIT_SUCCESS;
}
