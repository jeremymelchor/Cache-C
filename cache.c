/*!
 * \file cache.c
 *
 * \brief Interface du cache.
 * 
 * \note Ce fichier (et donc l'utilisateur du cache), n'a pas besoin de
 * connaitre les détails de l'implémentation du cache (struct Cache) : c'est un
 * exemple de type \b opaque. 
 *
 * \author Jean-Paul Rigault 
 */

#include <stdlib.h>

struct Cache *Cache_Create(const char *fic, unsigned nblocks, unsigned nrecords,
                           size_t recordsz, unsigned nderef) {

	Cache *cache = malloc(sizeof(Cache));

	fopen(fic);
	cache.file = fic;
	cache.nblocks = nblocks;
	cache.nrecords = nrecords;
	cache.recordsz = recordsz;
	cache.nderef = nderef;

	cache.instrument = malloc(sizeof(struct Cache_Instrument));
	cache.pfree = malloc(sizeof(Cache_Block_header));
	cache.headers = malloc(sizeof(Cache_Block_header));
}

//! Fermeture (destruction) du cache.
Cache_Error Cache_Close(struct Cache *pcache){
	Cache_sync();
	fclose(fic);
	cache.instrument = free();
	cache.pfree = free();
	cache.headers = free();
}

//! Synchronisation du cache.
Cache_Error Cache_Sync(struct Cache *pcache){

}

//! Invalidation du cache.
Cache_Error Cache_Invalidate(struct Cache *pcache){

}

//! Lecture  (à travers le cache).
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord){

}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord){

}
//! Résultat de l'instrumentation.
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache) {

}