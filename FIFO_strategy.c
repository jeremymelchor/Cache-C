#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"

#define C_LIST(pcache) ((struct Cache_List *)((pcache)->pstrategy))

 void *Strategy_Create(struct Cache *pcache) 
 {
     return Cache_List_Create();
 }

 void Strategy_Close(struct Cache *pcache)
 {
     Cache_List_Delete(C_list(pcache));
 }

 void Strategy_Invalidate(struct Cache *pcache)
 {
     Cache_List_Clear(C_list(pcache));
 }


struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) 
{
	struct Cache_Block_Header *pbh;
    struct Cache_List *c_list = C_LIST(pcache);

	/* S'il existe un cache invalide, on va utiliser celui la */
   if ((pbh = Get_Free_Block(pcache)) != NULL)
   {
       // Comme on va l'utiliser, on le met en fin de liste
       Cache_List_Append(fifo_list, pbh);
       return pbh;
   }

   // On prend le premier de la liste que l'on va retourner
   pbh = Cache_List_Remove_First(fifo_list);

   // Comme on va l'utiliser, on le met en fin de liste
   Cache_List_Append(fifo_list, pbh);

   return pbh;
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
