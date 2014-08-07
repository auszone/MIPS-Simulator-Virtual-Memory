#include "CMP.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#define IIMAGE_PATH "iimage.bin"
#define DIMAGE_PATH "dimage.bin"
#define SNAPSHOT_PATH "snapshot.rpt"
#define REPORT_PATH "report.rpt"
bool cycle(unsigned int);
int cycleCount;
unsigned int iDisk[256], dDisk[256], regs[32], cPC, iSP,iNum, dNum;
bool halt;
FILE *snapshot, *error, *rpt, *debug;
/////////////////////////////////////////////////////////////////////
int iMemSize = 64, dMemSize = 32, iPageSize = 8, dPageSize = 16, iCacheSize = 16, iBlockSize = 4, iCacheAss = 4, dCacheSize = 16, dBlockSize = 4, dCacheAss = 1;

Cache *ICache;//cacheSize, blockSize, associativity
Cache *DCache;
TLB *ITLB;//PTEEntryNum = diskSize / pageSize
TLB *DTLB;
PTE *IPTE;//diskSize, pageSize
PTE *DPTE;
Memory *IMem, *DMem;//memSize, pageSize
///////////////////////////////////////////////////////////////////////
int ilog2 (int x)
{
    int result = 0;
    
    while (x != 0) {
        result++;
        x = x >> 1;
    }
    return result;
}
///////////////////////////////////////////////////////////////////////
void prSnapshot(FILE *fPtr, int cycleCount)
{
    fprintf(fPtr, "cycle %d\n", cycleCount);
    for(int i = 0; i < 32; i++){
        fprintf(fPtr, "$%02d: 0x%08X\n", i, regs[i]);
    }
    fprintf(fPtr, "PC: 0x%08X\n\n\n", cPC);
}
void prReport(FILE *fptr_report)
{
    fprintf( fptr_report, "ICache :\n");
    fprintf( fptr_report, "# hits: %u\n", ICache->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", ICache->missNum);
    fprintf( fptr_report, "DCache :\n");
    fprintf( fptr_report, "# hits: %u\n", DCache->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", DCache->missNum);
    fprintf( fptr_report, "ITLB :\n");
    fprintf( fptr_report, "# hits: %u\n", ITLB->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", ITLB->missNum);
    fprintf( fptr_report, "DTLB :\n");
    fprintf( fptr_report, "# hits: %u\n", DTLB->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", DTLB->missNum);
    fprintf( fptr_report, "IPageTable :\n");
    fprintf( fptr_report, "# hits: %u\n", IPTE->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", IPTE->missNum);
    fprintf( fptr_report, "DPageTable :\n");
    fprintf( fptr_report, "# hits: %u\n", DPTE->hitNum);
    fprintf( fptr_report, "# misses: %u\n\n", DPTE->missNum);
}
void offset(int* wordOffset, int* byteOffset)
{
    if(*byteOffset < 0){
        int x = *byteOffset/4;
        *wordOffset += (x - 1);
        *byteOffset += -4*(x - 1);
    }else return;
}
//error detecting
bool writeReg0(int numReg)
{
    if (numReg == 0) {
        printf("Write $0 error in cycle: %d\n", cycleCount);
        return true;
    }else return false;
}
bool numOverflow(int a, int b)
{
    bool flag = false;
    if(a > 0 && b > 0){
        if((a + b) <= 0){
            printf("Number overflow in cycle: %d\n", cycleCount);
            flag = true;
            return flag;
        }
    }
    if(a < 0 && b < 0){
        if((a + b) >= 0){
            printf("Number overflow in cycle: %d\n", cycleCount);
            flag = true;
            return flag;
        }
    }
    return flag;
}
bool subNumOverflow(int a, int b)
{
    bool flag = false;
    
    if ( b != 0 && b == -b){
        printf("Number overflow in cycle: %d\n", cycleCount);
        flag = true;
        return flag;
    }
    if(a > 0 && b > 0){
        if((a + b) <= 0){
            printf("Number overflow in cycle: %d\n", cycleCount);
            flag = true;
            return flag;
        }
    }
    if(a < 0 && b < 0){
        if((a + b) >= 0){
            printf("Number overflow in cycle: %d\n", cycleCount);
            flag = true;
            return flag;
        }
    }
    return flag;
}
bool addOverflow(int address)
{
    if(address > 1023 || address < 0){
        printf("Address overflow in cycle: %d\n", cycleCount);
        halt = true;
        return true;
    }
    else
        return false;
}
bool misAlign(int c, int base)
{
    if(c%base != 0){
        printf("Misalignment error in cycle: %d\n", cycleCount);
        halt = true;
        return true;
    }
    return false;
}
//error detecting end
//Memory Translation//

unsigned int ITLBCal(unsigned int iVirtualMemory)
{
    int physicalPageNum = -1;
    int physicalAddress = -1;
    int tag = iVirtualMemory / iPageSize;
    for (vector<TLBEntry>::iterator it = ITLB->entries.begin(); it!=ITLB->entries.end(); it++) {
        if (it->tag == tag && it->validBit) {
            //TLB HIT
            ITLB->hitNum++;
            TLBEntry temp = *it;
            ITLB->entries.erase(it);
            ITLB->entries.push_back(temp);
            physicalPageNum = temp.physicalPageNum;
            physicalAddress = physicalPageNum * iPageSize + (iVirtualMemory & ~(0xFFFFFFFF * iPageSize));
            return physicalAddress;
        }
    }
    //IF TLB MISS DO THE FOLLOWING...
    ITLB->missNum++;
    if (IPTE->entries.at(tag).validBit) {
        //PTE HIT
        IPTE->hitNum++;
    } else {
        //PAGE FAULT
        IPTE->missNum++;
        page newPage;
        int PPN = IMem->replace(tag, newPage);
        for (int i = 0 ; i < IPTE->entries.size(); i++) {
            if (IPTE->entries.at(i).physicalPageNum == PPN) {
                IPTE->entries.at(i).validBit = false;
                for (vector<block>::iterator it = ICache->blocks.begin(); it != ICache->blocks.end(); it++) {
                    if(it->validBit) {
                        int blockTemp = (it->tag * (ICache->blockNum / ICache->associativity) + it->set) * iBlockSize;
                        int pageTemp = PPN * dPageSize;
                        if (pageTemp <= blockTemp && blockTemp < (pageTemp+iPageSize)) {
                            it->validBit = false;
                        }
                    }
                }
            }
        }
        IPTE->entries.at(tag).validBit = true;
        IPTE->entries.at(tag).physicalPageNum = PPN;
    }
    ITLB->replace(tag, IPTE->entries.at(tag).physicalPageNum);
    physicalPageNum = IPTE->entries.at(tag).physicalPageNum;
    physicalAddress = physicalPageNum * iPageSize + (iVirtualMemory & ~(0xFFFFFFFF * iPageSize));
    return physicalAddress;

}
void ICacheCal(unsigned int physicalAddress)
{
    int tag = (physicalAddress / iBlockSize) / (ICache->blockNum/ICache->associativity);
    int set = (physicalAddress / iBlockSize) % (ICache->blockNum/ICache->associativity);
    vector<block>::iterator temp;

    for (vector<block>::iterator it = ICache->blocks.begin(); it != ICache->blocks.end(); it++) {
        if (it->set == set) {
            if(it->tag == tag && it->validBit){
                //Cache hit
                ICache->hitNum++;
                block temp = *it;
                ICache->replace(temp, it);
                return;
            }
        }
        
    }
    for (vector<block>::iterator it = ICache->blocks.begin(); it != ICache->blocks.end(); it++) {
        if (it->set == set) {
            //Cache miss
            ICache->missNum++;
            block newBlock;
            newBlock.validBit = true;
            newBlock.tag = tag;
            newBlock.set = set;
            ICache->replace(newBlock, it);
            return;
        }
    }
}

unsigned int DTLBCal(unsigned int dVirtualMemory)
{
    int physicalPageNum = -1;
    int physicalAddress = -1;
    int tag = dVirtualMemory / dPageSize;
    for (vector<TLBEntry>::iterator it = DTLB->entries.begin(); it!=DTLB->entries.end(); it++) {
        if (it->tag == tag && it->validBit) {
            //TLB HIT
            DTLB->hitNum++;
            TLBEntry temp = *it;
            DTLB->entries.erase(it);
            DTLB->entries.push_back(temp);
            physicalPageNum = temp.physicalPageNum;
            physicalAddress = physicalPageNum * dPageSize + (dVirtualMemory & ~(0xFFFFFFFF * dPageSize));
            return physicalAddress;
        }
    }
    //IF TLB MISS DO THE FOLLOWING...
    DTLB->missNum++;
    if (DPTE->entries.at(tag).validBit) {
        //PTE HIT
        DPTE->hitNum++;
    } else {
        //PAGE FAULT
        DPTE->missNum++;
        page newPage;
        int PPN = DMem->replace(tag, newPage);
        for (int i = 0 ; i < DPTE->entries.size(); i++) {
            if (DPTE->entries.at(i).physicalPageNum == PPN) {
                DPTE->entries.at(i).validBit = false;
                //update cache
                for (vector<block>::iterator it = DCache->blocks.begin(); it != DCache->blocks.end(); it++) {
                    if(it->validBit) {
                        int blockTemp = (it->tag * (DCache->blockNum / DCache->associativity) + it->set) * dBlockSize;
                        int pageTemp = PPN * dPageSize;
                        if (pageTemp <= blockTemp && blockTemp < (pageTemp+dPageSize)) {
                            it->validBit = false;
                        }
                    }
                }
            }
        }
        DPTE->entries.at(tag).validBit = true;
        DPTE->entries.at(tag).physicalPageNum = PPN;
        
    }
    DTLB->replace(tag, DPTE->entries.at(tag).physicalPageNum);
    physicalPageNum = DPTE->entries.at(tag).physicalPageNum;
    physicalAddress = physicalPageNum * dPageSize + (dVirtualMemory & ~(0xFFFFFFFF * dPageSize));
    return physicalAddress;
}
void DCacheCal(unsigned int physicalAddress)
{
    int tag = (physicalAddress / dBlockSize) / (DCache->blockNum/DCache->associativity);
    int set = (physicalAddress / dBlockSize) % (DCache->blockNum/DCache->associativity);
    vector<block>::iterator temp;
    for (vector<block>::iterator it = DCache->blocks.begin(); it != DCache->blocks.end(); it++) {
        if (it->set == set) {
            if(it->tag == tag && it->validBit){
                //Cache hit
                DCache->hitNum++;
                block temp = *it;
                DCache->replace(temp, it);
                return;
            }
        }
    }
    for (vector<block>::iterator it = DCache->blocks.begin(); it != DCache->blocks.end(); it++) {
        if (it->set == set) {
            //Cache miss
            DCache->missNum++;
            block newBlock;
            newBlock.validBit = true;
            newBlock.tag = tag;
            newBlock.set = set;
            DCache->replace(newBlock, it);
            return;
        }
    }
}
void configureTheSize(int argc, char const *argv[])
{
    if (argc == 11) {
        iMemSize = atoi(argv[1]);
        dMemSize = atoi(argv[2]);
        iPageSize = atoi(argv[3]);
        dPageSize = atoi(argv[4]);
        iCacheSize = atoi(argv[5]);
        iBlockSize = atoi(argv[6]);
        iCacheAss = atoi(argv[7]);
        dCacheSize = atoi(argv[8]);
        dBlockSize = atoi(argv[9]);
        dCacheAss = atoi(argv[10]);
        
    }
    ICache = new Cache(iCacheSize, iBlockSize, iCacheAss);
    DCache = new Cache(dCacheSize, dBlockSize, dCacheAss);
    ITLB = new TLB(1024/iPageSize);
    DTLB = new TLB(1024/dPageSize);
    IPTE = new PTE(1024, iPageSize);
    DPTE = new PTE(1024, dPageSize);
    IMem = new Memory(iMemSize, iPageSize);
    DMem = new Memory(dMemSize, dPageSize);
    
}
//////////////////////
int main(int argc, char const *argv[])
{
    configureTheSize(argc, argv);
    FILE *iImage = fopen(IIMAGE_PATH, "rb");
    FILE *dImage = fopen(DIMAGE_PATH, "rb");
    snapshot = fopen(SNAPSHOT_PATH, "w");
    //error = fopen(ERROR_PATH, "w");
    rpt = fopen(REPORT_PATH, "w");
    //debug = fopen(DEBUG_PATH, "w");
    fread(&cPC, sizeof(int), 1, iImage);
    fread(&iNum, sizeof(int), 1, iImage);
    for (int i = 0; i < 32;  i++) {
        regs[i] = 0;
    }
    //read iImage to iMemory
    for (int i = 0; i < 128;  i++) {
        iDisk[i] = 0;
    }
    fread(&iDisk[0+cPC/4], sizeof(int), iNum, iImage);
    //read dImage to dMemory
    fread(&regs[29], sizeof(int), 1, dImage);
    fread(&dNum, sizeof(int), 1, dImage);
    for (int i = 0; i < 128;  i++) {
        dDisk[i] = 0;
    }
    iSP = regs[29];
    fread(&dDisk, sizeof(int), dNum, dImage);
    //execution starts here
    /* printf("IMeomory\n");
     for (int i = 0; i<iNum;i++) {
     printf("%d: %08x\n", i, iMemory[i]);
     }
     printf("DMeomory\n");
     for (int i = 0; i<dNum;i++) {
     printf("%d: %08x\n", i, dMemory[i]);
     }*/
    cycleCount = 0;
    halt = false;
    while(1){
        prSnapshot(snapshot, cycleCount);
        cycleCount++;
        //printf("Cycle:%2d\n", cycleCount);
        if(addOverflow(cPC)) break;
        if(misAlign(cPC, 4)) break;
        ICacheCal(ITLBCal(cPC));
        if(cycle(iDisk[cPC/4])) break;

    }
    prReport(rpt);
    fclose(iImage);
    fclose(dImage);
    fclose(snapshot);
    fclose(rpt);
    //fclose(error);
    return 0;
}
bool cycle(unsigned int instr)
{
    int opCode = instr>>26;
    //printf("OpCode = %02x\n", opCode);
    if(opCode == 0){
        unsigned int funct = (instr<<26)>>26;
        unsigned int rs = (instr<<6)>>27;
        unsigned int rt = (instr<<11)>>27;
        unsigned int rd = (instr<<16)>>27;
        unsigned int shamt = (instr<<21)>>27;
        //printf("Funct= %02x\n", funct);
        switch(funct){
            case 0x20:
                writeReg0(rd);
                numOverflow(regs[rs], regs[rt]);
                if(rd == 0) break;
                regs[rd] = regs[rs] + regs[rt];
                break;
            case 0x22:
                writeReg0(rd);
                subNumOverflow(regs[rs], -regs[rt]);
                
                if(rd == 0) break;
                regs[rd] = regs[rs] - regs[rt];
                break;
            case 0x24:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = regs[rs] & regs[rt];
                break;
            case 0x25:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = regs[rs] | regs[rt];
                break;
            case 0x26:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = regs[rs] ^ regs[rt];
                break;
            case 0x27:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = ~(regs[rs] | regs[rt]);
                break;
            case 0x28:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = ~(regs[rs] & regs[rt]);
                break;
            case 0x2A:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = ((signed int)regs[rs] < (signed int)regs[rt]);
                break;
            case 0x00:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = regs[rt] << shamt;
                break;
            case 0x02:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = regs[rt] >> shamt;
                break;
            case 0x03:
                writeReg0(rd);
                if(rd == 0) break;
                regs[rd] = (signed int)regs[rt] >> shamt;
                break;
            case 0x08:
                cPC = regs[rs];
                return false;
                break;
        }
        cPC += 4;
    }else{
        //halt
        if(opCode == 0x3F) return true;
        //J-type
        unsigned int C = instr & 0x03FFFFFF;
        if(opCode == 0x02){
            cPC += 4;
            cPC = ((cPC>>26)<<26)+(4*C);
        }else if(opCode == 0x03){
            regs[31] = cPC+4;
            cPC = (((cPC+4)>>26)<<26)|(4*C);
        }else{
            //I-type
            unsigned int rs = (instr<<6)>>27;
            unsigned int rt = (instr<<11)>>27;
            C = (short)(instr & 0x0000FFFF);
            int Ctemp;
            int wordOffset, byteOffset;
            switch(opCode){
                case 0x08:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    if(rt == 0) break;
                    regs[rt] = regs[rs] + (signed short)C;
                    break;
                case 0x23:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    addOverflow(Ctemp);
                    misAlign(Ctemp, 4);
                    if(halt)return true;
                    if(rt == 0) break;
                    regs[rt] = dDisk[(regs[rs]+(signed short)C)/4];
                    DCacheCal(DTLBCal(regs[rs]+(signed short)C));
                    break;
                case 0x21:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    //half word . 2bytes
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    addOverflow(Ctemp);
                    misAlign(Ctemp, 2);
                    if(halt)return true;
                    if(rt == 0) break;
                    regs[rt] = ((signed short)((dDisk[wordOffset] & (0x0000FFFF << (8*byteOffset)))>>(8*byteOffset)));
                    DCacheCal(DTLBCal(wordOffset*4));
                    break;
                case 0x25:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    addOverflow(Ctemp);
                    misAlign(Ctemp, 2);
                    if(halt)return true;
                    if(rt == 0) break;
                    regs[rt] = ((unsigned short)((dDisk[wordOffset] & (0x0000FFFF << (8*byteOffset)))>>(8*byteOffset)));
                    DCacheCal(DTLBCal(wordOffset*4));
                    break;
                case 0x20:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    //byte
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    addOverflow(Ctemp);
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    if(halt)return true;
                    if(rt == 0) break;
                    regs[rt] = ((int8_t)((dDisk[wordOffset] & (0x000000FF << (8*byteOffset)))>>(8*byteOffset)));
                    DCacheCal(DTLBCal(wordOffset*4));
                    break;
                case 0x24:
                    writeReg0(rt);
                    numOverflow(regs[rs], C);
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    addOverflow(Ctemp);
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    if(halt)return true;
                    if(rt == 0) break;
                    regs[rt] = ((uint8_t)((dDisk[wordOffset] & (0x000000FF << (8*byteOffset)))>>(8*byteOffset)));
                    DCacheCal(DTLBCal(wordOffset*4));
                    break;
                case 0x2B:
                    // word
                    numOverflow(regs[rs], C);
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    addOverflow(Ctemp);
                    misAlign(Ctemp, 4);
                    if(halt)return true;
                    dDisk[Ctemp/4] = regs[rt];
                    DCacheCal(DTLBCal(Ctemp));
                    break;
                case 0x29:
                    numOverflow(regs[rs], C);
                    //half word . 2bytes
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    addOverflow(Ctemp);
                    misAlign(Ctemp, 2);
                    if(halt)return true;
                    dDisk[wordOffset] &= ~(0x0000FFFF << byteOffset*8);
                    dDisk[wordOffset] |= ((regs[rt] & 0x0000FFFF)<<byteOffset*8);
                    DCacheCal(DTLBCal(wordOffset*4));
                    break;
                case 0x28:
                    numOverflow(regs[rs], C);
                    //1 byte
                    Ctemp = (signed int)regs[rs] + (signed short)C;
                    wordOffset = Ctemp/4;
                    byteOffset = Ctemp%4;
                    addOverflow(Ctemp);
                    if(halt)return true;
                    dDisk[wordOffset] &= ~(0x000000FF << byteOffset*8);
                    dDisk[wordOffset] |= ((regs[rt] & 0x000000FF)<<byteOffset*8);
                    DCacheCal(DTLBCal(wordOffset*4));

                    break;
                case 0x0F:
                    if(writeReg0(rt)) break;
                    regs[rt] = C << 16;
                    break;
                case 0x0C:
                    if(writeReg0(rt)) break;
                    regs[rt] = regs[rs] & (unsigned short)C;
                    break;
                case 0x0D:
                    if(writeReg0(rt)) break;
                    regs[rt] = regs[rs] | (unsigned short)C;
                    break;
                case 0x0E:
                    if(writeReg0(rt)) break;
                    regs[rt] = ~(regs[rs] | (unsigned short)C);
                    break;
                case 0x0A:
                    if(writeReg0(rt)) break;
                    regs[rt] = ((signed int)regs[rs] < (signed short) C);
                    break;
                case 0x04:
                    if(regs[rs] == regs[rt]){
                        numOverflow(cPC, (4*(int)C));
                        cPC = cPC + (signed short)(4*C);
                    }
                    break;
                case 0x05:
                    if(regs[rs] != regs[rt]){
                        numOverflow(cPC, (4*(int)C));
                        cPC = cPC + (signed short)(4*C);
                    }
                    break;
            }
            cPC += 4;
        }
    }
    return false;
}
