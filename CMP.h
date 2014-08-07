#include <vector>
using namespace std;
class block
{
public:
    block() {
        set = 0;
        tag = 0;
        validBit = false;
        dirtyBit = false;
    };
    bool validBit;
    bool dirtyBit;
    unsigned int tag;
    unsigned int set;
    
};
class Cache
{
public:
	Cache(int cacheSize, int blockSize, int associativity) {
        hitNum = 0;
        missNum = 0;
        size = cacheSize;
        this->blockSize = blockSize;
        blockNum = cacheSize / blockSize;
        this->associativity = associativity;
        blocks.assign(blockNum, block());
        if(blockNum / associativity > 1){
            for (int i = 0 ; i < blocks.size(); i++) {
                blocks.at(i).set = i/associativity;
            }
        }
        
    }
	//~Cache();
	int hitNum;
	int missNum;
	int blockNum;
    int associativity;
    int size;
    int blockSize;
	vector<block> blocks;
    void replace(block, vector<block>::iterator);
};
void Cache::replace(block newBlock, vector<block>::iterator it)
{
    
    blocks.erase(it);
    blocks.push_back(newBlock);
}
class PTEEntry {
public:
    PTEEntry(){
        validBit = false;
        physicalPageNum = 0;
    }
    bool validBit;
    unsigned int physicalPageNum;
};
class PTE
{
public:
	PTE(int diskSize, int pageSize) {
        hitNum = missNum = 0;
        entryNum = diskSize / pageSize;
        entries.resize(entryNum);
    };
	//~PTE();
	int hitNum;
	int missNum;
    int entryNum;
    vector<PTEEntry> entries;
};
class TLBEntry {
public:
    TLBEntry(){
        validBit = false;
        dirtyBit = false;
        tag = -1;
        physicalPageNum = -1;
    }
    bool validBit;
    bool dirtyBit;
    unsigned int tag;
    unsigned int physicalPageNum;
};
class TLB
{
public:
	TLB(int PTEEntryNum){
        entryNum = PTEEntryNum / 4;
        entries.resize(entryNum);
        associativity = entryNum; //fully-Associative
        hitNum = missNum = 0;
    };
	//~TLB();
	int hitNum;
	int missNum;
    int associativity;
    int entryNum;
    vector<TLBEntry> entries;
    void replace(int tag, int physicalPageNum);
};
void TLB::replace(int tag, int physicalPageNum)
{
    for (int i = 0; i < entries.size(); i++) {
        if (entries[i].physicalPageNum == physicalPageNum) entries[i].validBit = false;
    }
    TLBEntry newEntry;
    newEntry.validBit = true;
    newEntry.dirtyBit = 0;
    newEntry.tag = tag;
    newEntry.physicalPageNum = physicalPageNum;
    entries.erase(entries.begin());
    entries.push_back(newEntry);
}
class page{
public:
    page(){
        pageNum = -1;
        tag = -1;
        dirtyBit = false;
    };
    page(int pageSize){
        pageNum = 0;
        dirtyBit = false;
    }
    int pageNum;
    int tag;
    bool dirtyBit;
    
};
class  Memory {
public:
    Memory(int size, int pageSize){
        this->pageNum = size / pageSize;
        this->pages = vector<page> (pageNum);
        this->size = size;
        this->pages.resize(pageNum);
        for (int i = 0; i < pages.size(); i++) {
            this->pages[i].pageNum = i;
        }
    }
    int size;
    int pageNum;
    vector<page> pages;
    int replace(int, page);
};
int Memory::replace(int tag, page newPage)
{
    newPage.pageNum = pages.begin()->pageNum;
    newPage.tag = tag;
    this->pages.erase(pages.begin());
    this->pages.push_back(newPage);
    return newPage.pageNum;
}

