#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"
#include "cache_list.h"

#define C_LIST(cpointer) ( (struct Cache_List *)((cpointer)->pstrategy) )


void *Strategy_Create(struct Cache *pcache) 
{
	return Cache_List_Create();
}

void Strategy_Close(struct Cache *pcache)
{
	Cache_List_Delete(C_LIST(pcache));
}


void Strategy_Invalidate(struct Cache *pcache)
{
	Cache_List_Clear(C_LIST(pcache));
}


struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) 
{
    struct Cache_List *list = C_LIST(pcache);
    struct Cache_Block_Header *buffer = Get_Free_Block(pcache);
    if(buffer != NULL)
    	Cache_List_Append(list, buffer);
    else{
    	pbh = Cache_List_Remove_First(list);
    	Cache_List_Append(list, buffer);
    	}
    
    return buffer;
}



void Strategy_Read(struct Cache *pcache, struct Cache_Block_Header *pbh) 
{
	Cache_List_Move_To_End(C_LIST(pcache), pbh);
}  

void Strategy_Write(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
	 Cache_List_Move_To_End(C_LIST(pcache), pbh);
} 

char *Strategy_Name()
{
    return "LRU";
}
