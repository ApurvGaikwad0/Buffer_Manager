#include "buffer_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include "dberror.h"
#include "storage_mgr.h"

/*
 * initBufferPool
 * --------------
 * Initializes the buffer pool for a given page file.
 * - Opens the page file (must already exist).
 * - Sets the basic fields in the BM_BufferPool structure.
 * - Allocates and initializes an array of BM_PageHandle structures.
 *
 * Returns RC_OK on success or an appropriate error code.
 */
RC initBufferPool(BM_BufferPool *const pool, const char *const fileName,
                  const int numFrames, ReplacementStrategy strat, void *stratData) 
{
    // Open the file in read+write mode to verify its existence.
    FILE *filePtr = fopen(fileName, "r+");
    int counter = 0;
    
    if (filePtr != NULL) {
        fclose(filePtr);
        // Initialize buffer pool fields.
        pool->pageFile = (char *)fileName;
        pool->numPages = numFrames;
        pool->strategy = strat;
        pool->numberReadIO = 0;
        pool->numberWriteIO = 0;
        pool->buffertimer = 0;
        
        // Allocate memory for the array of page handles (frames).
        pool->mgmtData = (BM_PageHandle *) calloc(numFrames, sizeof(BM_PageHandle));
        if (pool->mgmtData == NULL)
            return RC_MEMORY_ALLOCATION_ERROR;
        
        // Initialize each frame in the pool.
        counter = 0;
        while (counter < numFrames) {
            pool->mgmtData[counter].dirty = false;             // Not dirty initially.
            pool->mgmtData[counter].fixCounts = 0;               // No clients using the frame.
            pool->mgmtData[counter].data = NULL;                 // No page loaded.
            pool->mgmtData[counter].pageNum = NO_PAGE;           // Mark as empty.
            pool->mgmtData[counter].strategyAttribute = NULL;    // No strategy attribute yet.
            counter++;
        }
        return RC_OK;
    } else {
        // File could not be opened; it does not exist.
        return RC_FILE_NOT_FOUND;
    }
}

/*
 * shutdownBufferPool
 * -------------------
 * Shuts down the buffer pool by:
 * - Ensuring no pages are pinned (fix count must be 0).
 * - Flushing all dirty pages to disk.
 * - Freeing all allocated memory for page frames.
 *
 * Returns RC_OK on success or an error code if shutdown fails.
 */
RC shutdownBufferPool(BM_BufferPool *const pool) 
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    
    int counter = 0;
    // Check that no frame is still in use.
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].fixCounts != 0)
            return RC_SHUTDOWN_POOL_FAILED;
        counter++;
    }
    
    // Flush all dirty pages.
    RC retVal = forceFlushPool(pool);
    if (retVal != RC_OK)
        return retVal;
    
    // Free all allocated page data and attributes.
    freePagesForBuffer(pool);
    // Free the array of page handles.
    free(pool->mgmtData);
    pool->mgmtData = NULL;
    return RC_OK;
}

/*
 * forceFlushPool
 * --------------
 * Writes back all dirty pages with fixCounts == 0 to disk.
 * Iterates through each frame and calls forcePage if the frame is dirty.
 *
 * Returns RC_OK on success.
 */
RC forceFlushPool(BM_BufferPool *const pool) 
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    
    int counter = 0;
    // Process each frame in the pool.
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].dirty && pool->mgmtData[counter].fixCounts == 0) {
            RC ret = forcePage(pool, &pool->mgmtData[counter]);
            if (ret != RC_OK)
                return ret;
            // After writing, mark the page as clean.
            pool->mgmtData[counter].dirty = false;
        }
        counter++;
    }
    return RC_OK;
}

/*
 * markDirty
 * ---------
 * Marks the specified page in the pool as dirty.
 *
 * Returns RC_OK if the page is found and marked, otherwise RC_PAGE_NOT_FOUND.
 */
RC markDirty(BM_BufferPool *const pool, BM_PageHandle *const page)
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    
    int counter = 0;
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].pageNum == page->pageNum) {
            pool->mgmtData[counter].dirty = true;
            return RC_OK;
        }
        counter++;
    }
    return RC_PAGE_NOT_FOUND;
}

/*
 * unpinPage
 * ---------
 * Decreases the fix count of the specified page in the pool.
 * If the fix count is already zero, returns an error.
 *
 * Returns RC_OK on success.
 */
RC unpinPage(BM_BufferPool *const pool, BM_PageHandle *const page)
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    
    int counter = 0;
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].pageNum == page->pageNum) {
            if (pool->mgmtData[counter].fixCounts > 0)
                pool->mgmtData[counter].fixCounts--;
            else
                return RC_PAGE_NOT_PINNED;
            return RC_OK;
        }
        counter++;
    }
    return RC_PAGE_NOT_FOUND;
}

/*
 * forcePage
 * ---------
 * Writes the content of the specified page from the pool back to disk.
 *
 * Returns RC_OK if successful, or an appropriate error code.
 */
RC forcePage(BM_BufferPool *const pool, BM_PageHandle *const page)
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    
    int counter = 0;
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].pageNum == page->pageNum) {
            // Open the file in read+write binary mode.
            FILE *filePtr = fopen(pool->pageFile, "rb+");
            if (filePtr == NULL)
                return RC_FILE_NOT_FOUND;
            // Move to the correct offset in the file.
            fseek(filePtr, page->pageNum * PAGE_SIZE, SEEK_SET);
            // Write the page data to disk.
            fwrite(pool->mgmtData[counter].data, PAGE_SIZE, 1, filePtr);
            fclose(filePtr);
            pool->numberWriteIO++;
            // Mark the page as clean.
            pool->mgmtData[counter].dirty = false;
            return RC_OK;
        }
        counter++;
    }
    return RC_PAGE_NOT_FOUND;
}

/*
 * pinPage
 * -------
 * Pins a page into the buffer pool.
 * - If the page is already in the pool, increments its fix count.
 * - Otherwise, locates an empty frame or selects a victim frame using the replacement strategy.
 * - Loads the requested page from disk into the chosen frame.
 *
 * Returns RC_OK on success.
 */
RC pinPage(BM_BufferPool *const pool, BM_PageHandle *const page, const PageNumber pNum)
{
    if (pool->mgmtData == NULL)
        return RC_POOL_NOT_INIT;
    if (pNum < 0)
        return RC_NEGATIVE_PAGE_NUM;
    
    int counter = 0;
    int frameIndex = -1;
    
    // Check if the page is already loaded in the pool.
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].pageNum == pNum) {
            frameIndex = counter;
            // For LRU or LRU_K, update the strategy attribute.
            if (pool->strategy == RS_LRU || pool->strategy == RS_LRU_K)
                updateBfrAtrbt(pool, &pool->mgmtData[counter]);
            // Increment fix count as a client is now using the page.
            pool->mgmtData[counter].fixCounts++;
            *page = pool->mgmtData[counter];
            return RC_OK;
        }
        counter++;
    }
    
    counter = 0;
    // Look for an empty frame in the pool.
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].pageNum == NO_PAGE) {
            frameIndex = counter;
            break;
        }
        counter++;
    }
    
    // If no empty frame is found, select a victim using the replacement strategy.
    if (frameIndex == -1) {
        frameIndex = strategyForFIFOandLRU(pool);
        if (frameIndex == -1)
            return RC_NO_AVAILABLE_FRAME;
        // If the victim frame is dirty, force its contents to disk.
        if (pool->mgmtData[frameIndex].dirty == true) {
            BM_PageHandle tempHandle = pool->mgmtData[frameIndex];
            RC retVal = forcePage(pool, &tempHandle);
            if (retVal != RC_OK)
                return retVal;
        }
    }
    
    // Load the requested page from disk into the selected frame.
    if (pool->mgmtData[frameIndex].data == NULL) {
        pool->mgmtData[frameIndex].data = (char*) calloc(PAGE_SIZE, sizeof(char));
        if (pool->mgmtData[frameIndex].data == NULL)
            return RC_MEMORY_ALLOCATION_ERROR;
    }
    
    FILE *filePtr = fopen(pool->pageFile, "r");
    if (filePtr == NULL)
        return RC_FILE_NOT_FOUND;
    // Seek to the location of the page in the file.
    fseek(filePtr, pNum * PAGE_SIZE, SEEK_SET);
    // Read the page into memory.
    fread(pool->mgmtData[frameIndex].data, sizeof(char), PAGE_SIZE, filePtr);
    fclose(filePtr);
    pool->numberReadIO++;
    
    // Update the frame with the new page's information.
    pool->mgmtData[frameIndex].pageNum = pNum;
    pool->mgmtData[frameIndex].fixCounts = 1;  // One client is using this page.
    pool->mgmtData[frameIndex].dirty = false;
    updateBfrAtrbt(pool, &pool->mgmtData[frameIndex]);
    
    *page = pool->mgmtData[frameIndex];
    return RC_OK;
}

/*
 * getFrameContents
 * ----------------
 * Returns a dynamically allocated array of PageNumbers representing the page in each frame.
 * If a frame is empty, the value is NO_PAGE.
 */
PageNumber *getFrameContents(BM_BufferPool *const pool)
{
    if (pool->mgmtData == NULL)
        return NULL;
    PageNumber *frameArr = (PageNumber *) malloc(pool->numPages * sizeof(PageNumber));
    int counter = 0;
    while (counter < pool->numPages) {
        // If the frame has data, return its page number; otherwise, NO_PAGE.
        frameArr[counter] = (pool->mgmtData[counter].data != NULL) ? pool->mgmtData[counter].pageNum : NO_PAGE;
        counter++;
    }
    return frameArr;
}

/*
 * getDirtyFlags
 * -------------
 * Returns an array of boolean flags indicating if each frame is dirty.
 */
bool *getDirtyFlags(BM_BufferPool *const pool)
{
    if (pool->mgmtData == NULL)
        return NULL;
    bool *flagArr = (bool *) malloc(pool->numPages * sizeof(bool));
    int counter = 0;
    while (counter < pool->numPages) {
        flagArr[counter] = pool->mgmtData[counter].dirty;
        counter++;
    }
    return flagArr;
}

/*
 * getFixCounts
 * ------------
 * Returns an array of integers representing the fix count of each frame.
 */
int *getFixCounts(BM_BufferPool *const pool)
{
    if (pool->mgmtData == NULL)
        return NULL;
    int *countArr = (int *) malloc(pool->numPages * sizeof(int));
    int counter = 0;
    while (counter < pool->numPages) {
        countArr[counter] = pool->mgmtData[counter].fixCounts;
        counter++;
    }
    return countArr;
}

/*
 * getNumReadIO and getNumWriteIO
 * -------------------------------
 * Return the total number of read and write I/O operations performed.
 */
int getNumReadIO(BM_BufferPool *const pool)
{
    return pool->numberReadIO;
}

int getNumWriteIO(BM_BufferPool *const pool)
{
    return pool->numberWriteIO;
}

/*
 * strategyForFIFOandLRU
 * ----------------------
 * Implements the replacement strategy for FIFO and LRU.
 * It selects a victim frame with fixCounts == 0 that has the smallest strategyAttribute.
 * If a frame has no attribute set, it is immediately selected.
 * Also, normalizes the buffertimer if it exceeds a threshold.
 */
int strategyForFIFOandLRU(BM_BufferPool *pool)
{
    int victimIndex = -1;
    int counter = 0;
    int minValue = pool->buffertimer; // Initial comparison value
    
    while (counter < pool->numPages) {
        // Only consider frames that are not currently pinned.
        if (pool->mgmtData[counter].fixCounts != 0) {
            counter++;
            continue;
        }
        // If no strategy attribute exists, choose this frame.
        if (pool->mgmtData[counter].strategyAttribute == NULL) {
            victimIndex = counter;
            break;
        }
        int currentAttr = *(pool->mgmtData[counter].strategyAttribute);
        if (currentAttr <= minValue) {
            minValue = currentAttr;
            victimIndex = counter;
        }
        counter++;
    }
    
    // Normalize buffertimer to prevent overflow.
    if (pool->buffertimer > 32000) {
        pool->buffertimer = 0;
        counter = 0;
        while (counter < pool->numPages) {
            if (pool->mgmtData[counter].strategyAttribute != NULL)
                *(pool->mgmtData[counter].strategyAttribute) = 0;
            counter++;
        }
    }
    return victimIndex;
}

/*
 * getAttributionArray
 * -------------------
 * Returns an array of the current strategy attributes for all frames.
 */
int *getAttributionArray(BM_BufferPool *pool)
{
    if (pool->mgmtData == NULL)
        return NULL;
    int *attribArray = (int *) malloc(pool->numPages * sizeof(int));
    int counter = 0;
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].strategyAttribute != NULL)
            attribArray[counter] = *(pool->mgmtData[counter].strategyAttribute);
        else
            attribArray[counter] = 0;
        counter++;
    }
    return attribArray;
}

/*
 * freePagesForBuffer
 * ------------------
 * Frees the allocated memory for each frame's data and strategy attribute.
 */
void freePagesForBuffer(BM_BufferPool *pool)
{
    if (pool->mgmtData == NULL)
        return;
    int counter = 0;
    while (counter < pool->numPages) {
        if (pool->mgmtData[counter].data != NULL)
            free(pool->mgmtData[counter].data);
        if (pool->mgmtData[counter].strategyAttribute != NULL)
            free(pool->mgmtData[counter].strategyAttribute);
        counter++;
    }
}

/*
 * updateBfrAtrbt
 * --------------
 * Updates the strategy attribute (e.g., timestamp) for a given page handle.
 * Allocates memory if the attribute is not already set.
 */
RC updateBfrAtrbt(BM_BufferPool *pool, BM_PageHandle *pageHandle)
{
    if (pageHandle->strategyAttribute == NULL) {
        pageHandle->strategyAttribute = (int *) calloc(1, sizeof(int));
        if (pageHandle->strategyAttribute == NULL)
            return RC_MEMORY_ALLOCATION_ERROR;
    }
    // Set the attribute to the current buffertimer value.
    *(pageHandle->strategyAttribute) = pool->buffertimer;
    pool->buffertimer++;
    return RC_OK;
}
