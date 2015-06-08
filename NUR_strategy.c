#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"

/*!
 * RAND : pas grand chose à faire ici. 
 *
 * En fait, nous initialisons le germe
 * (seed) du générateur aléatoire à quelque chose d'éminemment variable, pour
 * éviter d'avoir la même séquence à chque exécution...
 */
void *Strategy_Create(struct Cache *pcache) {
}

void Strategy_Close(struct Cache *pcache)
{
}


void Strategy_Invalidate(struct Cache *pcache)
{
}


struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) 
{
    return &pcache->headers[ib];
}



void Strategy_Read(struct Cache *pcache, struct Cache_Block_Header *pbh) 
{
}  

void Strategy_Write(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
} 

char *Strategy_Name()
{
    return "NRU";
}
