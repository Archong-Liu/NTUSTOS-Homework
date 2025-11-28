// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -n -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "main.h"
#include "addrspace.h"
#include "machine.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

int AddrSpace::frameQueue[NumPhysPages];
int AddrSpace::frameQueueHead = 0;
int AddrSpace::frameQueueTail = 0;

ReplaceAlgo AddrSpace::replaceAlgo = FIFO_Algo;

bool AddrSpace::usedPhyPage[NumPhysPages] = {0};

static void 
SwapHeader (NoffHeader *noffH)
{
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//----------------------------------------------------------------------


AddrSpace::AddrSpace()
{
    pageTable = NULL;
    swapTable = NULL;
    execFileName = NULL;

    // PageSize and SectorSize are predefined constants
    sectorsPerPage = PageSize / SectorSize;
    
    // zero out the entire address space
//    bzero(kernel->machine->mainMemory, MemorySize);

    frameQueueHead = frameQueueTail = 0; // initialize FIFO queue

}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
    if (pageTable != NULL)
        delete [] pageTable;

    if (swapTable != NULL)
        delete [] swapTable;

    if (execFileName != NULL)
        delete [] execFileName;
}


//----------------------------------------------------------------------
// AddrSpace::Load
// 	Load a user program into memory from a file.
//
//	Assumes that the page table has been initialized, and that
//	the object code file is in NOFF format.
//
//	"fileName" is the file containing the object code to load into memory
//----------------------------------------------------------------------

bool 
AddrSpace::Load(char *fileName) 
{
    OpenFile *executable = kernel->fileSystem->Open(fileName);
    NoffHeader noffH;
    unsigned int size;

    if (executable == NULL) {
	cerr << "Unable to open file " << fileName << "\n";
	return FALSE;
    }
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numPages = divRoundUp(size, PageSize);
//	cout << "number of pages of " << fileName<< " is "<<numPages<<endl;

    // save executable fileName for page fault handler
    if (execFileName != NULL)
        delete [] execFileName;
    execFileName = new char[strlen(fileName) + 1];
    strcpy(execFileName, fileName);

    

    // allocate pageTable (all invalid initially)
    pageTable = new TranslationEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = -1;
        pageTable[i].valid = false;
        pageTable[i].use = false;
        pageTable[i].dirty = false;
        pageTable[i].readOnly = false;
        pageTable[i].lastUsedTime = 0;
    }

    // allocate swap table
    swapTable = new SwapEntry[numPages];
    for (unsigned int i = 0; i < numPages; i++) {
        swapTable[i].used = true;          // page has initial data in swap
        swapTable[i].sector = i * sectorsPerPage;
    }

     // write initial content of each virtual page into SynchDisk
    char pageBuffer[PageSize];
    SynchDisk *disk = kernel->synchDisk;

    for (unsigned int vpn = 0; vpn < numPages; vpn++) {

        memset(pageBuffer, 0, PageSize);

        int virtAddr = vpn * PageSize;

        //
        // 1. copy code segment
        //
        if (noffH.code.size > 0) {
            int codeStart = noffH.code.virtualAddr;
            int codeEnd   = codeStart + noffH.code.size;

            int pageStart = virtAddr;
            int pageEnd   = pageStart + PageSize;

            int overlapStart = max(codeStart, pageStart);
            int overlapEnd   = min(codeEnd, pageEnd);

            if (overlapStart < overlapEnd) {
                int inFilePos = noffH.code.inFileAddr + (overlapStart - codeStart);
                int count     = overlapEnd - overlapStart;
                executable->ReadAt(pageBuffer + (overlapStart - pageStart),
                                   count, inFilePos);
            }
        }    
        // 2. copy initData segment
        //
        if (noffH.initData.size > 0) {
            int dataStart = noffH.initData.virtualAddr;
            int dataEnd   = dataStart + noffH.initData.size;

            int pageStart = virtAddr;
            int pageEnd   = pageStart + PageSize;

            int overlapStart = max(dataStart, pageStart);
            int overlapEnd   = min(dataEnd, pageEnd);

            if (overlapStart < overlapEnd) {
                int inFilePos = noffH.initData.inFileAddr + (overlapStart - dataStart);
                int count     = overlapEnd - overlapStart;
                executable->ReadAt(pageBuffer + (overlapStart - pageStart),
                                   count, inFilePos);
            }
        }
    //
        // 3. write this page to backing store
        //
        int startSector = swapTable[vpn].sector;

        for (int s = 0; s < sectorsPerPage; s++) {
            disk->WriteSector(startSector + s,
                              pageBuffer + SectorSize * s);
        }
    }

    delete executable;			// close file
    return TRUE;			// success
}

//----------------------------------------------------------------------
// AddrSpace::Execute
// 	Run a user program.  Load the executable into memory, then
//	(for now) use our own thread to run it.
//
//	"fileName" is the file containing the object code to load into memory
//----------------------------------------------------------------------

void 
AddrSpace::Execute(char *fileName) 
{
    if (!Load(fileName)) {
	cout << "inside !Load(FileName)" << endl;
	return;				// executable not found
    }

    //kernel->currentThread->space = this;
    this->InitRegisters();		// set the initial register values
    this->RestoreState();		// load page table register

    kernel->machine->Run();		// jump to the user progam

    ASSERTNOTREACHED();			// machine->Run never returns;
					// the address space exits
					// by doing the syscall "exit"
}


//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    Machine *machine = kernel->machine;
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG(dbgAddr, "Initializing stack pointer: " << numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, don't need to save anything!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    kernel->machine->pageTable = pageTable;
    kernel->machine->pageTableSize = numPages;
}


void AddrSpace::PageFaultHandler(int badVAddr)
{
    int vpn = (unsigned)badVAddr / PageSize;
    ASSERT(vpn >= 0 && vpn < (int)numPages);
    pageTable[vpn].lastUsedTime = kernel->stats->totalTicks;
    // Step 1: try to find free physical frame
    int frame = FindFreeFrame();

    // Step 2: need replacement?
    if (frame < 0) {
        frame = SelectVictimFrame();

        // evict victim page
        for (unsigned int i = 0; i < numPages; i++) {
            if (pageTable[i].physicalPage == frame) {
                printf("page %d swapped\n", i);
                // write back if dirty
                if (pageTable[i].dirty) {
                    int sector = swapTable[i].sector;
                    for (int s = 0; s < sectorsPerPage; s++) {
                        kernel->synchDisk->WriteSector(
                            sector + s,
                            kernel->machine->mainMemory +
                            frame * PageSize + s * SectorSize
                        );
                    }
                }

                // remove victim
                pageTable[i].physicalPage = -1;
                pageTable[i].valid = false;
                break;
            }
        }
    }

    // Step 3: load faulted page from SynchDisk
    int sector = swapTable[vpn].sector;

    for (int s = 0; s < sectorsPerPage; s++) {
        kernel->synchDisk->ReadSector(
            sector + s,
            kernel->machine->mainMemory + frame * PageSize + s * SectorSize
        );
    }

    // Step 4: update pageTable
    pageTable[vpn].physicalPage = frame;
    pageTable[vpn].valid = true;
    pageTable[vpn].use = false;
    pageTable[vpn].dirty = false;

    // Step 5: enqueue frame for FIFO
    frameQueue[frameQueueTail] = frame;
    frameQueueTail = (frameQueueTail + 1) % NumPhysPages;

    // done, retry instruction automatically
}

int AddrSpace::FindFreeFrame()
{
    for (int i = 0; i < NumPhysPages; i++) {
        if (!usedPhyPage[i]) {
            usedPhyPage[i] = true;
            return i;
        }
    }
    return -1;
}

int AddrSpace::SelectVictimFrame()
{
    if (replaceAlgo == FIFO_Algo) {

        int frame = frameQueue[frameQueueHead];
        frameQueueHead = (frameQueueHead + 1) % NumPhysPages;
        return frame;

    } else { // LRU_Algo

        int victimFrame = -1;
        unsigned int oldestTime = 0xFFFFFFFF;

        for (int frame = 0; frame < NumPhysPages; frame++) {
            if (!usedPhyPage[frame]) continue;

            for (unsigned int vpn = 0; vpn < numPages; vpn++) {
                if (pageTable[vpn].physicalPage == frame) {
                    if (pageTable[vpn].lastUsedTime < oldestTime) {
                        oldestTime = pageTable[vpn].lastUsedTime;
                        victimFrame = frame;
                    }
                    break;
                }
            }
        }

        ASSERT(victimFrame >= 0);
        return victimFrame;
    }
}
