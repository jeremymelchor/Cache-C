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
#include <unistd.h>
#include <stdio.h>
#include "low_cache.h"
#include "cache.h"

struct Cache *Cache_Create(const char *fic, unsigned nblocks, unsigned nrecords,
                           size_t recordsz, unsigned nderef) {

	struct Cache *cache = malloc(sizeof(Cache));

	fopen(fic, NULL);
	cache->file = fic;
	cache->nblocks = nblocks;
	cache->nrecords = nrecords;
	cache->recordsz = recordsz;
	cache->nderef = nderef;

	struct Cache_Instrument instrument;
	instrument.n_reads = 0;
	instrument.n_writes = 0;
	instrument.n_hits = 0;
	instrument.n_syncs = 0;
	instrument.n_deref = 0;

	struct Cache_Block_Header *header = malloc(sizeof(struct Cache_Block_Header));

	cache->instrument = instrument;
	cache->pfree = header;
	cache->headers = header;

	return cache;
}

//! Fermeture (destruction) du cache.
Cache_Error Cache_Close(struct Cache *pcache) {
	Cache_Error c_err;
	Cache_Sync(pcache);
	pcache->instrument.n_syncs++;
	if (fclose(pcache->file) != 0) {
		c_err = CACHE_KO;
		return c_err;
	}
	free(&pcache->instrument);
	free(&pcache->pfree);
	free(&pcache->headers);
	c_err = CACHE_OK;

	return c_err;
}

//! Synchronisation du cache.
Cache_Error Cache_Sync(struct Cache *pcache) {
	//Création du Cache_Error
	Cache_Error c_err;
	//Réccupèration du premier Cache_Block_Header
	struct Cache_Block_Header *cur_header = pcache->headers;
	int tmp = 0;
	//On parcourt la liste de Cache_Block_Header
	while( tmp < pcache->nblocks) {
		//Si on a effectué un multiple de NSYNC accès au Cache, on lance Cache_Sync()
		if (tmp%(NSYNC) == 0) {
			Cache_Sync(pcache);
			pcache->instrument.n_syncs++;
		}
		//Si le bit M vaut 1, on écrit le bloc dans le fichier, puis on remet M à 0
		if (cur_header->flags == 1 || cur_header->flags == 3 || cur_header->flags == 5 || cur_header->flags == 7) {
			//Ecriture dans le fichier
			if (write(pcache->file, cur_header->data, sizeof(cur_header->data))<0) {
				c_err = CACHE_KO;
				return c_err;
			}
			//On diminue le flags de 1 pour supprimer le bit M
			cur_header->flags -= 1;
		}
		//On change de Cache_Block_Header puis on incrémente le nombre de Cache_Block_Header visité
		&cur_header += sizeof(struct Cache_Block_Header);
		tmp++;
	}
	//On retourne le Cache_Error
	c_err = CACHE_OK;
	return c_err;
}

//! Invalidation du cache.
Cache_Error Cache_Invalidate(struct Cache *pcache){
	//Création du Cache_Error
	Cache_Error c_err;
	//Réccupèration du premier Cache_Block_Header
	struct Cache_Block_Header *cur_header = pcache->headers;
	int tmp = 0;
	//On parcourt la liste de Cache_Block_Header
	while( tmp < pcache->nblocks) {
		//Si le bit V vaut 1 on le remet à 0
		if (cur_header->flags >= 4 && cur_header->flags <= 7) {
			cur_header->flags -= 4;
		}
		//On change de Cache_Block_Header puis on incrémente le nombre de Cache_Block_Header visité
		&cur_header += sizeof(struct Cache_Block_Header);
		tmp++;
	}
	//On retourne le Cache_Error
	c_err = CACHE_OK;
	return c_err;
}

//! Lecture  (à travers le cache).
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord){

}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord){

}
//! Résultat de l'instrumentation.
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache) {
	return &pcache->instrument;
}