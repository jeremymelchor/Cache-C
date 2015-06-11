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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "low_cache.h"
#include "cache_list.h"
#include "random.h"
#include "strategy.h"
#include "cache.h"


struct Cache_Block_Header* createHeaders(struct Cache* pcache) {
	struct Cache_Block_Header* tmp = (struct Cache_Block_Header*)malloc( sizeof(struct Cache_Block_Header) * pcache->nblocks );
	
	//Si l'allocation de mémoire ne fonctionne pas :
	if(!tmp) {
		return NULL;
	}

	//Sinon :
	int i;
	for(i = 0; i < pcache->nblocks; i++) {
		tmp[i].ibcache = i;
		tmp[i].data = (char*)malloc( pcache->recordsz * pcache->nrecords );
		tmp[i].ibfile = -1;
	}

	return tmp;
}

struct Cache *Cache_Create(const char *fic, unsigned nblocks, unsigned nrecords,
                           size_t recordsz, unsigned nderef) {

	struct Cache *cache = malloc(sizeof(struct Cache));

	FILE *file;
	if( (file = fopen(fic, "r+b")) == NULL)
		file = fopen(fic, "w+b");

	cache->file = fic;
	cache->fp = file;
	cache->nblocks = nblocks;
	cache->nrecords = nrecords;
	cache->recordsz = recordsz;
	cache->nderef = nderef;

	struct Cache_Instrument instrument = (struct Cache_Instrument) {0, 0, 0, 0, 0};
	instrument.n_reads = 0;
	instrument.n_writes = 0;
	instrument.n_hits = 0;
	instrument.n_syncs = 0;
	instrument.n_deref = 0;

	cache->instrument = instrument;
	cache->pfree = &cache->headers[0];
	cache->headers = createHeaders(cache);

	return cache;
}

//! Fermeture (destruction) du cache.
Cache_Error Cache_Close(struct Cache *pcache) {
	Cache_Error c_err;

	//Synchronise le cache
	Cache_Sync(pcache);

	//vérifie si la fermeture s'est bien passé:
	if (fclose(pcache->fp) != 0) {
		c_err = CACHE_KO;
		return c_err;
	}

	//Free des structs
	free(&pcache->instrument);
	free(pcache);
	
	pcache->headers = NULL;
	pcache = NULL;

	c_err = CACHE_OK;

	return c_err;
}

//! Synchronisation du cache.
Cache_Error Cache_Sync(struct Cache *pcache) {
	//Création du Cache_Error
	Cache_Error c_err;
	//On parcourt la liste de Cache_Block_Header
	for(int i = 0; i < pcache->nblocks; i++) {
		//Si on a effectué un multiple de NSYNC accès au Cache, on lance Cache_Sync()
		if (i%(NSYNC) == 0) {
			Cache_Sync(pcache);
			pcache->instrument.n_syncs++;
		}
		//Si le bit M vaut 1, on écrit le bloc dans le fichier, puis on remet M à 0
		//Ecriture dans le fichier
		int flag = pcache->headers[i].flags;
		int fd = open(pcache->file, O_WRONLY);
		if (write(fd, pcache->headers[i].data, sizeof(pcache->headers[i].data))<0 && ( flag == MODIF+VALID || flag == MODIF ||
			flag == MODIF+R_FLAG+VALID || flag == MODIF+R_FLAG ) ) {
			c_err = CACHE_KO;
			return c_err;
		}
		//On suprime la modification M
		pcache->headers[i].flags &= ~MODIF;s
	}
	//On retourne le Cache_Error
	pcache->instrument.n_syncs++;
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
	int address = &cur_header;
	//On parcourt la liste de Cache_Block_Header
	while( tmp < pcache->nblocks) {
		struct Cache_Block_Header *ad = (struct Cache_Block_Header *)address;
		//Si le bit V vaut 1 on le remet à 0
		cur_header->flags &= ~VALID;
		//On change de Cache_Block_Header puis on incrémente le nombre de Cache_Block_Header visité
		address = address + sizeof(struct Cache_Block_Header);
		tmp++;
	}
	//On retourne le Cache_Error
	c_err = CACHE_OK;
	return c_err;
}

//! Lecture  (à travers le cache).
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord){
	int fd = open(pcache->file, O_RDONLY);
	Cache_Error c_err;
	fseek(pcache->fp, 0, irfile);
	if (read(fd, &precord, sizeof(struct Cache_Block_Header))<0) {
		c_err = CACHE_KO;
		return c_err;
	}
	c_err = CACHE_OK;
	return c_err;
}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord){
	Cache_Error c_err;
	fseek(pcache->fp, 0, irfile);
	if (write(pcache->headers, &precord, sizeof(struct Cache_Block_Header))<0) {
		c_err = CACHE_KO;
		return c_err;
	}
	c_err = CACHE_OK;
	return c_err;
}
//! Résultat de l'instrumentation.
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache) {
	return &pcache->instrument;
} 

/* Permet de rechercher un bloc libre dans le cache.
 * Si il n'y en a pas, on return NULL.
 */
struct Cache_Block_Header *Get_Free_Block(struct Cache *pcache){
	struct Cache_Block_Header *cache_block_header;
	
	// on parcourt tout les blocks du cache
	int i;
	for (i = 0; i < pcache->nblocks; i++) {
		cache_block_header =  &(pcache->headers[i]);

		// Si on trouve un block libre
		if ( (cache_block_header->flags & VALID) == 0 ) return cache_block_header;
	}

	// On a pas trouvé de blocs vides
	return NULL;
}