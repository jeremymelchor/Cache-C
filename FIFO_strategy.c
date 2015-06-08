#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"

#define FIFO_LIST(pcache)

 void *Strategy_Create(struct Cache *pcache) 
 {
     return Cache_List_Create();
 }

 void Strategy_Close(struct Cache *pcache)
 {
     Cache_List_Delete(FIFO_LIST(pcache));
 }

 void Strategy_Invalidate(struct Cache *pcache)
 {
     Cache_List_Clear(FIFO_LIST(pcache));
 }


struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) 
{
    return ;
}



void Strategy_Read(struct Cache *pcache, struct Cache_Block_Header *pbh) 
{
}  

void Strategy_Write(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
} 

char *Strategy_Name()
{
    return "FIFO";
}
