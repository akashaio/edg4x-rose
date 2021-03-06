#include "sage3basic.h"

#include "BinaryDebugger.h"
#include "BinaryLoader.h"
#include "DisassemblerM68k.h"
#include "DisassemblerX86.h"
#include "SRecord.h"
#include <Partitioner2/Engine.h>
#include <Partitioner2/ModulesElf.h>
#include <Partitioner2/ModulesM68k.h>
#include <Partitioner2/ModulesPe.h>
#include <Partitioner2/ModulesX86.h>
#include <Partitioner2/Utility.h>

using namespace rose::Diagnostics;

namespace rose {
namespace BinaryAnalysis {
namespace Partitioner2 {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Basic steps
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SgAsmInterpretation*
Engine::parse(const std::vector<std::string> &fileNames) {
    interp_ = NULL;
    map_.clear();

    // Parse the binary container files with single call to frontend()
    std::vector<std::string> frontendNames;
    BOOST_FOREACH (const std::string &fileName, fileNames) {
        if (boost::starts_with(fileName, "map:") || boost::starts_with(fileName, "proc:")) {
            // these are processed by the load() method only
        } else if (boost::starts_with(fileName, "run:")) {
            // these are processed here and in load()
            frontendNames.push_back(fileName.substr(4));
        } else if (boost::ends_with(fileName, ".srec")) {
            // Motorola S-Records are handled in load()
        } else {
            frontendNames.push_back(fileName);
        }
    }
    if (!frontendNames.empty()) {
        std::vector<std::string> frontendArgs;
        frontendArgs.push_back("/proc/self/exe");       // I don't think frontend actually uses this
        frontendArgs.push_back("-rose:binary");
        frontendArgs.push_back("-rose:read_executable_file_format_only");
        frontendArgs.insert(frontendArgs.end(), frontendNames.begin(), frontendNames.end());
        SgProject *project = frontend(frontendArgs);
        std::vector<SgAsmInterpretation*> interps = SageInterface::querySubTree<SgAsmInterpretation>(project);
        if (interps.empty())
            throw std::runtime_error("a binary specimen container must have at least one SgAsmInterpretation");
        interp_ = interps.back();    // windows PE is always after DOS
    }
    return interp_;
}

MemoryMap
Engine::load(const std::vector<std::string> &fileNames) {
    map_.clear();

    // Parse the binary containers
    if (!interp_)
        parse(fileNames);

    // Load the interpretation if it hasn't been already
    if (interp_ && (!interp_->get_map() || interp_->get_map()->isEmpty())) {
        if (!obtainLoader())
            throw std::runtime_error("unable to find an appropriate loader for the binary interpretation");
        loader_->load(interp_);
    }

    // Get a map from the now-loaded interpretation, or use an empty map if the interp isn't mapped
    if (interp_ && interp_->get_map())
        map_ = *interp_->get_map();
    
    // Load raw binary files into map
    BOOST_FOREACH (const std::string &fileName, fileNames) {
        if (boost::starts_with(fileName, "map:")) {
            std::string resource = fileName.substr(3);  // remove "map", leaving colon and rest of string
            map_.insertFile(resource);
        } else if (boost::starts_with(fileName, "proc:")) {
            std::string resource = fileName.substr(4);  // remove "proc", leaving colon and the rest of the string
            map_.insertProcess(resource);
        } else if (boost::starts_with(fileName, "run:")) {
            std::string exeName = fileName.substr(4);
            BinaryDebugger debugger(exeName);
            BOOST_FOREACH (const MemoryMap::Node &node, map_.nodes()) {
                if (0 != (node.value().accessibility() & MemoryMap::EXECUTABLE))
                    debugger.setBreakpoint(node.key());
            }
            debugger.runToBreakpoint();
            if (debugger.isTerminated())
                throw std::runtime_error(exeName + " " + debugger.howTerminated() + " without reaching a breakpoint");
            map_.insertProcess(":noattach:" + StringUtility::numberToString(debugger.isAttached()));
            debugger.terminate();
        } else if (boost::ends_with(fileName, ".srec")) {
            if (fileName.size()!=strlen(fileName.c_str())) {
                throw std::runtime_error("file name contains internal NUL characters: \"" +
                                         StringUtility::cEscape(fileName) + "\"");
            }
            std::ifstream input(fileName.c_str());
            if (!input.good()) {
                throw std::runtime_error("cannot open Motorola S-Record file: \"" +
                                         StringUtility::cEscape(fileName) + "\"");
            }
            std::vector<SRecord> srecs = SRecord::parse(input);
            for (size_t i=0; i<srecs.size(); ++i) {
                if (!srecs[i].error().empty())
                    mlog[ERROR] <<fileName <<":" <<(i+1) <<": S-Record: " <<srecs[i].error() <<"\n";
            }
            SRecord::load(srecs, map_, true /*create*/, MemoryMap::READABLE|MemoryMap::EXECUTABLE);
        }
    }

    return map_;
}

Partitioner
Engine::partition(const std::vector<std::string> &fileNames) {
    if (map_.isEmpty())
        load(fileNames);
    return partition(interp_);
}

Partitioner
Engine::partition(SgAsmInterpretation *interp) {
    if (interp && interp!=interp_) {
        interp_ = interp;
        if (map_.isEmpty())
            load(std::vector<std::string>());
    }
    if (!obtainDisassembler())
        throw std::runtime_error("no disassembler available for partitioning");
    Partitioner partitioner = createTunedPartitioner();
    runPartitioner(partitioner, interp_);
    return partitioner;
}
    

    
    

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Partitioner-making functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void
Engine::checkCreatePartitionerPrerequisites() const {
    if (NULL==disassembler_)
        throw std::runtime_error("Engine::createBarePartitioner needs a prior disassembler");
    if (map_.isEmpty())
        mlog[WARN] <<"Engine::createBarePartitioner: using an empty memory map\n";
}

Partitioner
Engine::createBarePartitioner() {
    checkCreatePartitionerPrerequisites();
    Partitioner p(disassembler_, map_);
    return p;
}

Partitioner
Engine::createGenericPartitioner() {
    checkCreatePartitionerPrerequisites();
    Partitioner p = createBarePartitioner();
    p.functionPrologueMatchers().push_back(ModulesX86::MatchHotPatchPrologue::instance());
    p.functionPrologueMatchers().push_back(ModulesX86::MatchStandardPrologue::instance());
    p.functionPrologueMatchers().push_back(ModulesX86::MatchAbbreviatedPrologue::instance());
    p.functionPrologueMatchers().push_back(ModulesX86::MatchEnterPrologue::instance());
    p.functionPrologueMatchers().push_back(ModulesM68k::MatchLink::instance());
    p.basicBlockCallbacks().append(ModulesX86::FunctionReturnDetector::instance());
    p.basicBlockCallbacks().append(ModulesM68k::SwitchSuccessors::instance());
    p.basicBlockCallbacks().append(ModulesX86::SwitchSuccessors::instance());
    return p;
}

Partitioner
Engine::createTunedPartitioner() {
    if (dynamic_cast<DisassemblerM68k*>(disassembler_)) {
        checkCreatePartitionerPrerequisites();
        Partitioner p = createBarePartitioner();
        p.functionPrologueMatchers().push_back(ModulesM68k::MatchLink::instance());
        p.basicBlockCallbacks().append(ModulesM68k::SwitchSuccessors::instance());
        return p;
    }
    
    if (dynamic_cast<DisassemblerX86*>(disassembler_)) {
        checkCreatePartitionerPrerequisites();
        Partitioner p = createBarePartitioner();
        p.functionPrologueMatchers().push_back(ModulesX86::MatchHotPatchPrologue::instance());
        p.functionPrologueMatchers().push_back(ModulesX86::MatchStandardPrologue::instance());
        p.functionPrologueMatchers().push_back(ModulesX86::MatchEnterPrologue::instance());
        p.basicBlockCallbacks().append(ModulesX86::FunctionReturnDetector::instance());
        p.basicBlockCallbacks().append(ModulesX86::SwitchSuccessors::instance());
        return p;
    }

    return createGenericPartitioner();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Engine utilities
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string
Engine::specimenNameDocumentation() {
    return ("The following names are recognized for binary specimens:"

            "@bullet{If the name does not match any of the following patterns then it is assumed to be the name of a "
            "file containing a specimen that is a binary container format such as ELF or PE.}"

            "@bullet{If the name begins with the string \"map:\" then it is treated as a memory map resource string that "
            "adjusts a memory map by inserting part of a file. " + MemoryMap::insertFileDocumentation() + "}"

            "@bullet{If the name begins with the string \"proc:\" then it is treated as a process resource string that "
            "adjusts a memory map by reading the process' memory. " + MemoryMap::insertProcessDocumentation() + "}"

            "@bullet{If the name begins with the string \"run:\" then it is first treated like a normal file by ROSE's "
            "\"fontend\" function, and then during a second pass it will be loaded natively under a debugger, run until "
            "a mapped executable address is reached, and then its memory is copied into ROSE's memory map possibly "
            "overwriting existing parts of the map.  This can be useful when the user wants accurate information about "
            "how that native loader links in shared objects since ROSE's linker doesn't always have identical behavior.}"

            "@bullet{If the name ends with \".srec\" and doesn't match the previous list of prefixes then it is assumed "
            "to be a text file containing Motorola S-Records and will be parsed as such and loaded into the memory map "
            "with read and execute permissions.}"

            "When more than one mechanism is used to load a single coherent specimen, the normal names are processed first "
            "by passing them all to ROSE's \"frontend\" function, which results in an initial memory map.  The other names "
            "are then processed in the order they appear, possibly overwriting parts of the map.");
}

Disassembler*
Engine::obtainDisassembler(const std::string &isaName) {
    if (!isaName.empty()) {
        if ((disassembler_ = Disassembler::lookup(isaName)))
            disassembler_ = disassembler_->clone();
    } else {
        obtainDisassembler(disassembler_);
    }
    return disassembler_;
}

BinaryLoader*
Engine::obtainLoader(BinaryLoader *loader) {
    if (loader) {
        loader_ = loader;
    } else if (loader_) {
        // already have one
    } else if (interp_) {
        if ((loader_ = BinaryLoader::lookup(interp_))) {
            loader_ = loader_->clone();
            loader_->set_perform_remap(true);
            loader_->set_perform_dynamic_linking(false);
            loader_->set_perform_relocations(false);
        }
    } else {
        // can't get one
    }
    return loader_;
}

Disassembler*
Engine::obtainDisassembler(Disassembler *disassembler) {
    if (disassembler) {
        disassembler_ = disassembler;
    } else if (disassembler_) {
        // already have one
    } else if (interp_) {
        if ((disassembler_ = Disassembler::lookup(interp_)))
            disassembler_ = disassembler_->clone();
    } else {
        // can't get one
    }
    return disassembler_;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      High-level partitioning actions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Engine&
Engine::runPartitioner(Partitioner &partitioner, SgAsmInterpretation *interp) {
    makeContainerFunctions(partitioner, interp);
    discoverFunctions(partitioner);
    attachDeadCodeToFunctions(partitioner);
    attachPaddingToFunctions(partitioner);
    attachSurroundedDataToFunctions(partitioner);
    attachBlocksToFunctions(partitioner, true);         // to emit warnings about CFG problems
    postPartitionFixups(partitioner, interp);
    return *this;
}

void
Engine::discoverBasicBlocks(Partitioner &partitioner) {
    while (makeNextBasicBlock(partitioner)) /*void*/;
}

std::vector<Function::Ptr>
Engine::discoverFunctions(Partitioner &partitioner) {
    rose_addr_t nextPrologueVa = 0;                     // where to search for function prologues

    while (1) {
        // Find as many basic blocks as possible by recursively following the CFG as we build it.
        discoverBasicBlocks(partitioner);

        // No pending basic blocks, so look for a function prologue. This creates a pending basic block for the function's
        // entry block, so go back and look for more basic blocks again.
        if (Function::Ptr function = makeNextPrologueFunction(partitioner, nextPrologueVa)) {
            nextPrologueVa = function->address();       // avoid "+1" because it may overflow
            continue;
        }

        // Nothing more to do
        break;
    }

    // Try to attach basic blocks to functions and return the list of failures.
    makeCalledFunctions(partitioner);
    return attachBlocksToFunctions(partitioner);
}

void
Engine::postPartitionFixups(Partitioner &partitioner, SgAsmInterpretation *interp) {
    if (interp)
        ModulesPe::nameImportThunks(partitioner, interp);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Function-making functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Make functions from ELF/PE containers
std::vector<Function::Ptr>
Engine::makeContainerFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;

    BOOST_FOREACH (const Function::Ptr &function, makeEntryFunctions(partitioner, interp))
        insertUnique(retval, function, sortFunctionsByAddress);
    BOOST_FOREACH (const Function::Ptr &function, makeErrorHandlingFunctions(partitioner, interp))
        insertUnique(retval, function, sortFunctionsByAddress);
    BOOST_FOREACH (const Function::Ptr &function, makeImportFunctions(partitioner, interp))
        insertUnique(retval, function, sortFunctionsByAddress);
    BOOST_FOREACH (const Function::Ptr &function, makeExportFunctions(partitioner, interp))
        insertUnique(retval, function, sortFunctionsByAddress);
    BOOST_FOREACH (const Function::Ptr &function, makeSymbolFunctions(partitioner, interp))
        insertUnique(retval, function, sortFunctionsByAddress);
    return retval;
}

// Make functions at specimen entry addresses
std::vector<Function::Ptr>
Engine::makeEntryFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;
    if (interp) {
        BOOST_FOREACH (SgAsmGenericHeader *fileHeader, interp->get_headers()->get_headers()) {
            BOOST_FOREACH (const rose_rva_t &rva, fileHeader->get_entry_rvas()) {
                rose_addr_t va = rva.get_rva() + fileHeader->get_base_va();
                Function::Ptr function = Function::instance(va, SgAsmFunction::FUNC_ENTRY_POINT);
                insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
            }
        }
    }
    return retval;
}

// Make functions at error handling addresses
std::vector<Function::Ptr>
Engine::makeErrorHandlingFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;
    if (interp) {
        BOOST_FOREACH (const Function::Ptr &function, ModulesElf::findErrorHandlingFunctions(interp))
            insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    }
    return retval;
}

// Make functions at import trampolines
std::vector<Function::Ptr>
Engine::makeImportFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;
    if (interp) {
        // Windows PE imports
        ModulesPe::rebaseImportAddressTables(partitioner, ModulesPe::getImportIndex(partitioner, interp));
        BOOST_FOREACH (const Function::Ptr &function, ModulesPe::findImportFunctions(partitioner, interp))
            insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    
        // ELF imports
        BOOST_FOREACH (const Function::Ptr &function, ModulesElf::findPltFunctions(partitioner, interp))
            insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    }
    return retval;
}

// Make functions at export addresses
std::vector<Function::Ptr>
Engine::makeExportFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;
    if (interp) {
        BOOST_FOREACH (const Function::Ptr &function, ModulesPe::findExportFunctions(partitioner, interp))
            insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    }
    return retval;
}

// Make functions that have symbols
std::vector<Function::Ptr>
Engine::makeSymbolFunctions(Partitioner &partitioner, SgAsmInterpretation *interp) {
    std::vector<Function::Ptr> retval;
    if (interp) {
        BOOST_FOREACH (const Function::Ptr &function, Modules::findSymbolFunctions(partitioner, interp))
            insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    }
    return retval;
}

// Make functions according to CFG
std::vector<Function::Ptr>
Engine::makeCalledFunctions(Partitioner &partitioner) {
    std::vector<Function::Ptr> retval;
    BOOST_FOREACH (const Function::Ptr &function, partitioner.discoverCalledFunctions())
        insertUnique(retval, partitioner.attachOrMergeFunction(function), sortFunctionsByAddress);
    return retval;
}

// Looks for a function prologue at or above the specified starting address and makes a function there.
Function::Ptr
Engine::makeNextPrologueFunction(Partitioner &partitioner, rose_addr_t startVa) {
    if (Function::Ptr function = partitioner.nextFunctionPrologue(startVa)) {
        partitioner.attachFunction(function);
        return function;
    }
    return Function::Ptr();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Basic-blocks
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// make a new basic block for an arbitrary placeholder
BasicBlock::Ptr
Engine::makeNextBasicBlock(Partitioner &partitioner) {
    ControlFlowGraph::VertexNodeIterator worklist = partitioner.undiscoveredVertex();
    if (worklist->nInEdges() > 0) {
        ControlFlowGraph::VertexNodeIterator placeholder = worklist->inEdges().begin()->source();
        BasicBlock::Ptr bb = partitioner.discoverBasicBlock(placeholder);
        partitioner.attachBasicBlock(placeholder, bb);
        return bb;
    }
    return BasicBlock::Ptr();
}

// sophomoric attempt to assign basic blocks to functions.
std::vector<Function::Ptr>
Engine::attachBlocksToFunctions(Partitioner &partitioner, bool emitWarnings) {
    std::vector<Function::Ptr> retval;
    BOOST_FOREACH (const Function::Ptr &function, partitioner.functions()) {
        partitioner.detachFunction(function);           // must be detached in order to modify block ownership
        EdgeList inwardConflictEdges, outwardConflictEdges;
        size_t nFailures = partitioner.discoverFunctionBasicBlocks(function, &inwardConflictEdges, &outwardConflictEdges);
        if (nFailures > 0) {
            insertUnique(retval, function, sortFunctionsByAddress);
            if (mlog[WARN]) {
                mlog[WARN] <<"discovery for " <<partitioner.functionName(function)
                               <<" had " <<StringUtility::plural(inwardConflictEdges.size(), "inward conflicts")
                               <<" and " <<StringUtility::plural(outwardConflictEdges.size(), "outward conflicts") <<"\n";
                BOOST_FOREACH (const ControlFlowGraph::EdgeNodeIterator &edge, inwardConflictEdges) {
                    mlog[WARN] <<"  inward conflict " <<*edge
                               <<" from " <<partitioner.functionName(edge->source()->value().function()) <<"\n";
                }
                BOOST_FOREACH (const ControlFlowGraph::EdgeNodeIterator &edge, outwardConflictEdges) {
                    mlog[WARN] <<"  outward conflict " <<*edge
                               <<" to " <<partitioner.functionName(edge->target()->value().function()) <<"\n";
                }
            }
        }
        partitioner.attachFunction(function);           // reattach even if we didn't modify it due to failure
    }
    return retval;
}

std::set<rose_addr_t>
Engine::attachDeadCodeToFunction(Partitioner &partitioner, const Function::Ptr &function, size_t maxIterations) {
    ASSERT_not_null(function);
    std::set<rose_addr_t> retval;

    for (size_t i=0; i<maxIterations; ++i) {
        // Find ghost edges
        std::set<rose_addr_t> ghosts = partitioner.functionGhostSuccessors(function);
        if (ghosts.empty())
            break;

        // Insert placeholders for all ghost edge targets
        partitioner.detachFunction(function);           // so we can modify its basic block ownership list
        BOOST_FOREACH (rose_addr_t ghost, ghosts) {
            if (retval.find(ghost)==retval.end()) {
                partitioner.insertPlaceholder(ghost);   // ensure a basic block gets created here
                function->insertBasicBlock(ghost);      // the function will own this basic block
                retval.insert(ghost);
            }
        }
        
        // If we're about to do more iterations then we should recursively discover instructions for pending basic blocks. Once
        // we've done that we should traverse the function's CFG to see if some of those new basic blocks are reachable and
        // should also be attached to the function.
        if (i+1 < maxIterations) {
            while (makeNextBasicBlock(partitioner)) /*void*/;
            size_t nFailures = partitioner.discoverFunctionBasicBlocks(function, NULL, NULL);
            if (nFailures > 0)
                break;
        }
    }

    partitioner.attachFunction(function);               // no-op if still attached
    return retval;
}

// Finds dead code and adds it to the function to which it seems to belong.
std::set<rose_addr_t>
Engine::attachDeadCodeToFunctions(Partitioner &partitioner, size_t maxIterations) {
    std::set<rose_addr_t> retval;
    BOOST_FOREACH (const Function::Ptr &function, partitioner.functions()) {
        std::set<rose_addr_t> deadVas = attachDeadCodeToFunction(partitioner, function, maxIterations);
        retval.insert(deadVas.begin(), deadVas.end());
    }
    return retval;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Data block functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DataBlock::Ptr
Engine::attachPaddingToFunction(Partitioner &partitioner, const Function::Ptr &function) {
    ASSERT_not_null(function);
    if (DataBlock::Ptr padding = partitioner.matchFunctionPadding(function)) {
        partitioner.attachFunctionDataBlock(function, padding);
        return padding;
    }
    return DataBlock::Ptr();
}

std::vector<DataBlock::Ptr>
Engine::attachPaddingToFunctions(Partitioner &partitioner) {
    std::vector<DataBlock::Ptr> retval;
    BOOST_FOREACH (const Function::Ptr &function, partitioner.functions()) {
        if (DataBlock::Ptr padding = attachPaddingToFunction(partitioner, function))
            insertUnique(retval, padding, sortDataBlocks);
    }
    return retval;
}

std::vector<DataBlock::Ptr>
Engine::attachSurroundedDataToFunctions(Partitioner &partitioner) {
    // Find executable addresses that are not yet used in the CFG/AUM
    AddressIntervalSet executableSpace;
    BOOST_FOREACH (const MemoryMap::Node &node, partitioner.memoryMap().nodes()) {
        if ((node.value().accessibility() & MemoryMap::EXECUTABLE) != 0)
            executableSpace.insert(node.key());
    }
    AddressIntervalSet unused = partitioner.aum().unusedExtent(executableSpace);

    // Iterate over the larged unused address intervals and find their surrounding functions
    std::vector<DataBlock::Ptr> retval;
    BOOST_FOREACH (const AddressInterval &interval, unused.intervals()) {
        if (interval.least()<=executableSpace.least() || interval.greatest()>=executableSpace.greatest())
            continue;
        typedef std::vector<Function::Ptr> Functions;
        Functions beforeFuncs = partitioner.functionsOverlapping(interval.least()-1);
        Functions afterFuncs = partitioner.functionsOverlapping(interval.greatest()+1);

        // What functions are in both sets?
        Functions enclosingFuncs(beforeFuncs.size());
        Functions::iterator final = std::set_intersection(beforeFuncs.begin(), beforeFuncs.end(),
                                                          afterFuncs.begin(), afterFuncs.end(), enclosingFuncs.begin());
        enclosingFuncs.resize(final-enclosingFuncs.begin());

        // Add the data block to all enclosing functions
        if (!enclosingFuncs.empty()) {
            BOOST_FOREACH (const Function::Ptr &function, enclosingFuncs) {
                DataBlock::Ptr dblock = partitioner.attachFunctionDataBlock(function, interval.least(), interval.size());
                insertUnique(retval, dblock, sortDataBlocks);
            }
        }
    }
    return retval;
}

} // namespace
} // namespace
} // namespace
