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

static struct Cache_Block_Header *Find_Block(struct Cache *pcache, int irfile) {
    int ib;
    //indice du bloc
    int ibfile = irfile / pcache->nrecords;

    //recherche du block
    for (ib = 0; ib < pcache->nblocks; ib++) {
		struct Cache_Block_Header *header = &pcache->headers[ib];

		if (header->flags & VALID && header->ibfile == ibfile) {
	    	pcache->instrument.n_hits++;
	    	return header;
		}
    }

    return NULL;
}

static Cache_Error Read_Block(struct Cache *pcache, struct Cache_Block_Header *header) {
    long loff, leof;

    // On cherche la longueur courante du fichier
    if (fseek(pcache->fp, 0, SEEK_END) < 0)
    	return CACHE_KO;

    leof = ftell(pcache->fp);

    //Si l'on est au dela de la fin de fichier, on cree un bloc de zeros
    loff = DADDR(pcache, header->ibfile);

    // Le bloc cherché est au dela de la fin de fichier
    if (loff >= leof) {
        // On n'effectue aucune entrée-sortie, se contentant de mettre le bloc à 0
        memset(header->data, '\0', pcache->blocksz); 
    } else {
    	// Le bloc existe dans le fichier, donc on s'y rend
        if (fseek(pcache->fp, loff, SEEK_SET) != 0)
        	return CACHE_KO;
        if (fread(header->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz)
        	return CACHE_KO; 
    } 

    // On Valide le block
    header->flags |= VALID;

    return CACHE_OK;
}

static Cache_Error Write_Block(struct Cache *pcache, struct Cache_Block_Header *header) {
    // On positionne le pointeur à l'adresse du bloc dans le fichier
    fseek(pcache->fp, DADDR(pcache, header->ibfile), SEEK_SET);

    /* On écrit les données du bloc */
    if (fwrite(header->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz)
	return CACHE_KO;

    /* On efface la marque de modification */  
    header->flags &= ~MODIF;

    return CACHE_OK;
}

struct Cache_Block_Header *Get_Block(struct Cache *pcache, int irfile)
{
    struct Cache_Block_Header *header;

    //Si l'enregistrement n'est pas dans le bloc
    if ((header = Find_Block(pcache, irfile)) == NULL)
    {
        // On demande un block à Strategy_Replace_Block
	header = Strategy_Replace_Block(pcache);
	if (header == NULL) return NULL;

	// Si le bloc libéré est valide et modifié, on le sauve sur le fichier
	if ((header->flags & VALID) && (header->flags & MODIF)
            && (Write_Block(pcache, header) != CACHE_OK))
	    return NULL; 
	
    // On remplit le bloc libre et son entête avec l'information irfile
	header->flags = 0;
	// indice du bloc dans le fichier
	header->ibfile = irfile / pcache->nrecords;
	if (Read_Block(pcache, header) != CACHE_OK) return NULL;
    }

    // Soit l'enregistrement était déjà dans le cache, soit on vient de l'y mettre
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
	struct Cache_Block_Header *header;
    pcache->instrument.n_reads++;

    //Si le block n'existe pas :
    header = Get_Block(pcache, irfile);
    if (header == NULL)
    	return CACHE_KO;
    
    //On copie la mémoire
    memcpy(precord, ADDR(pcache, irfile, header), pcache->recordsz);

    //Appel à la lecture de la strategie
    Strategy_Read(pcache, header);

    //Retour du Cache_Error
    return Do_Sync_If_Needed(pcache);
}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord)
{
    struct Cache_Block_Header *pbh;

    pcache->instrument.n_writes++;

    if ((pbh = Get_Block(pcache, irfile)) == NULL) return CACHE_KO;
    (void)memcpy(ADDR(pcache, irfile, pbh), precord, pcache->recordsz);

    pbh->flags |= MODIF;

    Strategy_Write(pcache, pbh);

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
