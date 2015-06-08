#include "cache_list.h"

/*! Création d'une liste de blocs */
struct Cache_List *Cache_List_Create()
{
	Cache_List c = malloc(sizeof(Cache_List));
	c->next = c;
	c->prev = c;
}
/*! Destruction d'une liste de blocs */
void Cache_List_Delete(struct Cache_List *list)
{
	for(Cache_List *count=list->next; count!=list; count=count->next)
			Cache_List_Remove_First(list)
	free(list);
}

/*! Insertion d'un élément à la fin */
void Cache_List_Append(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	Cache_List *counter = list->next;
	Cache_List *new = malloc(sizeof(Cache_List));
	for(counter=list->next; counter !=list; counter->next){}
	
	new->pheader = pbh;
	new->next=counter;
	new->prev=counter->prev;
	counter->prev->next=new;
	counter->prev=new;
	
}
/*! Insertion d'un élément au début */
void Cache_List_Prepend(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	Cache_List *counter = list->next;
	Cache_List *new = malloc(sizeof(Cache_List));
	
	new->pheader = pbh;
	new->next = counter;
	new->prev = counter->prev;
	counter->prev->next = new;
	counter->prev = new;
}

/*! Retrait du premier élément */
struct Cache_Block_Header *Cache_List_Remove_First(struct Cache_List *list)
{
	Cache_List *slave = list->next;
	slave->prev->next=slave->next;
	slave->next->prev=slave->prev;
	free(slave);
}
/*! Retrait du dernier élément */
struct Cache_Block_Header *Cache_List_Remove_Last(struct Cache_List *list)
{
	Cache_List *slave;
	for(slave=list->next; slave !=list; slave->next){}
	
	slave->prev->next=slave->next;
	slave->next->prev=slave->prev;
	free(slave);
}
/*! Retrait d'un élément quelconque */
struct Cache_Block_Header *Cache_List_Remove(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	Cache_List *slave;
	for(slave=list->next; slave !=list && slave->pheader != pbh; slave->next){}
	
	slave->prev->next=slave->next;
	slave->next->prev=slave->prev;
	free(slave);
	
}

/*! Remise en l'état de liste vide */
void Cache_List_Clear(struct Cache_List *list)
{
  //TODO: 
}

/*! Test de liste vide */
bool Cache_List_Is_Empty(struct Cache_List *list)
{
	return list->next == list->prev;
}

/*! Transférer un élément à la fin */
void Cache_List_Move_To_End(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	Cache_List *counter = list->next;
	for(counter=list->next; counter !=list && counter->pheader !=pbh; counter->next){}
	
	counter->prev->next=counter->next;
	counter->next->prev=counter->prev;
	
	counter->prev = list->prev;
	counter->next = list->next;
	list->prev->next = counter;
	list->next->prev = counter;
}
/*! Transférer un élément  au début */
void Cache_List_Move_To_Begin(struct Cache_List *list, struct Cache_Block_Header *pbh)
{
	Cache_List *counter = list->next;
	for(counter=list->next; counter !=list && counter->pheader !=pbh; counter->next){}
	
	counter->prev->next=counter->next;
	counter->next->prev=counter->prev;
	
	counter->prev = list->next;
	counter->next = list->next->next;
	list->next->next->prev = counter;
	list->next->next = counter;
}
