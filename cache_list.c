#include <stdlib.h>
#include <stdio.h>
#include "cache_list.h"
#include "low_cache.h"

/*! Création d'une liste de blocs */
struct Cache_List *Cache_List_Create()
{
	struct Cache_List *c = malloc(sizeof(struct Cache_List));

	c->pheader = NULL;
	c->next = c;
	c->prev = c;
	return c;
}
/*! Destruction d'une liste de blocs */
void Cache_List_Delete(struct Cache_List *list)
{
	struct Cache_List *count;
	for(count=list->next; count!=list; count=count->next)
			Cache_List_Remove_First(list);
	free(list);
}

/*! Insertion d'un élément à la fin */
void Cache_List_Append(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	struct Cache_List *counter = list->prev;
	struct Cache_List *new = malloc(sizeof(struct Cache_List));
	for(counter = counter->next; counter !=list && counter->pheader !=pbh; counter=counter->next){}
	
	new->pheader = pbh;
	new->next=counter;
	new->prev=counter->prev;
	counter->prev->next=new;
	counter->prev=new;
	
}
/*! Insertion d'un élément au début */
void Cache_List_Prepend(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	struct Cache_List *counter = list->next;
	struct Cache_List *new = malloc(sizeof(struct Cache_List));
	
	new->pheader = pbh;
	new->next=counter;
	new->prev=counter->prev;
	counter->prev->next=new;
	counter->prev=new;
	list = new;
}

/*! Retrait du premier élément */
struct Cache_Block_Header *Cache_List_Remove_First(struct Cache_List *list)
{
	struct Cache_List *slave = list->next;
	slave->prev->next=slave->next;
	slave->next->prev=slave->prev;
	struct Cache_Block_Header *pbh = malloc(sizeof(struct Cache_Block_Header));
	pbh = slave->pheader;
	free(slave);
	return pbh;
}
/*! Retrait du dernier élément */
struct Cache_Block_Header *Cache_List_Remove_Last(struct Cache_List *list)
{
	struct Cache_List *slave;
	for(slave=list->next; slave !=list; slave=slave->next){}
	
	slave->prev->next=slave->next;
	slave->next->prev=slave->prev;
	struct Cache_Block_Header *pbh = malloc(sizeof(struct Cache_Block_Header));
	pbh = slave->pheader;
	free(slave);
	return pbh;
}

/*! Retrait d'un élément quelconque */
struct Cache_Block_Header *Cache_List_Remove(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	struct Cache_List *slave;

	for (slave=list->next; slave != list; slave=slave->next) {
		if (slave->pheader == pbh) {
			slave->prev->next = slave->next;
			slave->next->prev = slave->prev;
		}
	}
	return pbh;
	// struct Cache_List *slave;
	// for(slave=list->next; slave !=list && slave->pheader != pbh; slave=slave->next){}
	// int i;
	// struct Cache_Block_Header *hhh = malloc(sizeof(struct Cache_Block_Header));
	// //printf(":(\n");
	// hhh = slave->pheader;
	// //printf("OUIIII\n");
	// i = hhh->ibcache;
	// printf("hhh %d\n", i);
	// //printf("%d == \n", pbh->ibcache);
	// if(slave->pheader == pbh){
	// 	//printf("TROUVE\n");
	// 	slave->prev->next = slave->next; 
	// 	slave->next->prev = slave->prev; 
	// }
	// else
	// 	//printf("PAS TROUVE\n");
	// return slave->pheader; 
}

/*! Remise en l'état de liste vide */
void Cache_List_Clear(struct Cache_List *list)
{
  struct Cache_List *count;
  for(count=list->next; count!=list; count=count->next)
			Cache_List_Remove_First(list); 
}

/*! Test de liste vide */
bool Cache_List_Is_Empty(struct Cache_List *list)
{
	return list->next == list->prev || list == NULL;
}

/*! Transférer un élément à la fin */
void Cache_List_Move_To_End(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	struct Cache_Block_Header *header = Cache_List_Remove(list, pbh);
	if(header!=NULL)
		Cache_List_Append(list,header); 
	else
		Cache_List_Append(list,pbh); 
}
/*! Transférer un élément  au début */
void Cache_List_Move_To_Begin(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	//On trouve l'element
	struct Cache_List *counter = list->next;
	for(counter=list->next; counter !=list && counter->pheader !=pbh; counter=counter->next){}
	//On enlève l'element de son emplacement courant en recollant ses voisins entre eux.
	counter->prev->next=counter->next;
	counter->next->prev=counter->prev;
	
	//On colle l'element au bon endroit
	list->prev->next = counter;
	list->prev = counter;
	counter->prev = list->prev;
	counter->next = list;
	//On passe le pointeur sur le nouveau premier element
	list = counter;
}

void print(struct Cache_List *list){
	struct Cache_List *counter;
	int i  = 0;
	for(counter = list->next; counter !=list; counter = counter->next){
		i++;
		printf("%d, %d, %d\n",counter->pheader->ibfile, counter->pheader->ibcache, i);
	}
}
