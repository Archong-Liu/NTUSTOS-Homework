// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include <string.h>

#define UserStackSize		1024 	// increase this as necessary!

class AddrSpace;

struct SwapEntry {
    bool used;          // if this swap slot is used
    int sector;            // corresponding virtual page number
};

struct FrameInfo {
    AddrSpace* space;    // 這個 frame 屬於哪個 AddrSpace
    int vpn;             // 這個 frame 裝的是哪個 virtual page
    unsigned int lastUsed; // LRU timestamp
};

extern FrameInfo frameInfo[NumPhysPages];

enum ReplaceAlgo { FIFO_Algo, LRU_Algo };


class AddrSpace {
  public:
    AddrSpace();			// Create an address space.
    ~AddrSpace();			// De-allocate an address space
    static ReplaceAlgo replaceAlgo; // Current Algorithm
    static bool usedPhyPage[NumPhysPages];
    static int frameQueue[NumPhysPages];
    static int frameQueueHead;
    static int frameQueueTail;


    void Execute(char *fileName);	// Run the the program
					// stored in the file "executable"

    void SaveState();			// Save/restore address space-specific
    void RestoreState();		// info on a context switch 

    SwapEntry *swapTable;   // Set the size of table to numPages, the number of pages in the virtual address space
    int sectorsPerPage;

    TranslationEntry *pageTable;	// Assume linear page table translation
					// for now!
    unsigned int numPages;		// Number of pages in the virtual 
					// address space

    bool Load(char *fileName);		// Load the program into memory
					// return false if not found

    void InitRegisters();		// Initialize user-level CPU registers,
					// before jumping to user code

    void PageFaultHandler(int badVAddr);
    int FindFreeFrame();
    int SelectVictimFrame();   // FIFO

  private:
    char *execFileName;
  
};

#endif // ADDRSPACE_H
