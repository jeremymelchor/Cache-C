/*!
 * \file cache.c
 *
 * \brief Interface du cache.
 * 
 * \note Ce fichier (et donc l'utilisateur du cache), n'a pas besoin de
 * connaitre les détails de l'implémentation du cache (struct Cache) : c'est un
 * exemple de type \b opaque. 
 *
 * \author Nicolas
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
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
		tmp[i].flags &= ~MODIF & ~VALID & ~R_FLAG;
	}

	return tmp;
}

struct Cache *Cache_Create(const char *fic, unsigned nblocks, unsigned nrecords,
                           size_t recordsz, unsigned nderef) {

	struct Cache *cache = malloc(sizeof(struct Cache));

	FILE *file;
	if( (file = fopen(fic, "r+b")) == NULL)
		file = fopen(fic, "w+b");

	char buffer[100000];
	strcpy(buffer,fic);

	cache->file = buffer;
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
		if (write(fd, pcache->headers[i].data, sizeof(pcache->headers[i].data))<0 && ( (flag & MODIF && flag & VALID) ||
			flag & MODIF || (flag & MODIF && flag & R_FLAG && flag & VALID) || (flag & MODIF && flag && R_FLAG) ) ) {
			c_err = CACHE_KO;
			return c_err;
		}
		//On suprime la modification M
		pcache->headers[i].flags &= ~MODIF;
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
	//On parcourt la liste de Cache_Block_Header
	for(int i = 0; i < pcache->nblocks; i++) {
		//Si le bit V vaut 1 on le remet à 0
		cur_header->flags &= ~VALID;
	}
	//On retourne le Cache_Error
	c_err = CACHE_OK;
	return c_err;
}

//! Synchronisation d'une donnée
Cache_Error Cache_Sync_Header(struct Cache *pcache, struct Cache_Block_Header *header) {
	//Création du Cache_Error
	Cache_Error c_err;
	//On parcourt la liste de Cache_Block_Header
	for(int i = 0; i < pcache->nblocks; i++) {
		int flag = pcache->headers[i].flags;
		if (((pcache->headers[i]).ibfile == header->ibfile) && ( (flag & MODIF && flag & VALID) ||
			flag & MODIF || (flag & MODIF && flag & R_FLAG && flag & VALID) || (flag & MODIF && flag && R_FLAG) )) {
			int fd = open(pcache->file, O_WRONLY);
			if (write(fd, pcache->headers[i].data, sizeof(pcache->headers[i].data))<0) {
				c_err = CACHE_KO;
				return c_err;
			}
			//On suprime la modification M
			pcache->headers[i].flags &= ~MODIF;
		}
		
	}
	//On retourne le Cache_Error
	pcache->instrument.n_syncs++;
	c_err = CACHE_OK;
	return c_err;
}

struct Cache_Block_Header * Cache_Find_Block(struct Cache * pcache, int irfile, const void * precord) {
	struct Cache_Block_Header *header = NULL;
	// On cherche l'enregistrement
	int nrecords = pcache->nrecords;
	int ibfile = irfile/nrecords;
	for(int i = 0; i < pcache->nblocks; i++){
		//on ne vérifie que les headers valides 
		if(pcache->headers[i].flags & VALID) {
			//On ne compare les ibfile avec celle recherché
			if(ibfile == pcache->headers[i].ibfile)
				header = &pcache->headers[i];
		}
	}

	// Si le header n'as pas été trouvé
	if(header == NULL){
		// on réccupère un bloc
		header = Strategy_Replace_Block(pcache);
		// On synchronise la donnée
		Cache_Sync_Header(pcache, header);
		// Synchronisation des infos
		header->ibfile = irfile/pcache->nrecords;
		header->flags = header->flags|VALID;
		// Placement du pointeur sur la donnée recherché
		fseek(pcache->fp, irfile * pcache->recordsz, SEEK_SET);
	} else {
		// On incrémente le nombre de hits (si l'enregistrement est déjà dans le cache)
		pcache->instrument.n_hits++;
	}
	return header;
}

//!Synchronisation si nécéssaire :
static Cache_Error Do_Sync_If_Needed(struct Cache *pcache) {
    static int sync_freq = NSYNC;

    //vérifie que la frequence de synchronisation soit diffèrente de 0
    if (--sync_freq == 0) {
    	sync_freq = NSYNC;
    	return Cache_Sync(pcache);
    }
    //Retourne CACHE_OK s'il n'y a pas de problèmes de synchronisation
    else return CACHE_OK;
}

//! Lecture  (à travers le cache).
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord){
	//On cherche tout d'abord un block
	struct Cache_Block_Header * header;
	header = Cache_Find_Block(pcache, irfile, precord);

	// On copie l'enregistrement du cache vers le buffer precord
	memcpy(precord, &header, pcache->recordsz);
	
	// On appelle la fonction read de la stratégie
	Strategy_Read(pcache,header);

	// On incrémente le nombre de lectures
	pcache->instrument.n_reads++;

	return CACHE_OK;
}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord){
	//création d'un header temporaire
	struct Cache_Block_Header *header;

	//Incrémentation du nombre d'écritures
    pcache->instrument.n_writes++;

    //Recherche du Header
    if ((header = Cache_Find_Block(pcache, irfile, precord)) == NULL){
    	return CACHE_KO;
    }
    //copie des données
    memcpy(ADDR(pcache, irfile, header), precord, pcache->recordsz);

    //Modification du flag
    header->flags |= MODIF;

    //Appel à l'écriture de la stratégie
    Strategy_Write(pcache, header);

    //Retour du Cache_Error
    return Do_Sync_If_Needed(pcache);
}

//! Résultat de l'instrumentation.
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache) {
	//copie de Cache_Instrument
	static struct Cache_Instrument copy;
    copy = pcache->instrument;

    //réinitialisations
    pcache->instrument.n_reads = pcache->instrument.n_writes = 0;
    pcache->instrument.n_hits = pcache->instrument.n_syncs = 0;
    pcache->instrument.n_deref = 0;

    return &copy;
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
