/*!
 * \file cache.h
 *
 * \brief Interface du cache.
 * 
 * \note Ce fichier (et donc l'utilisateur du cache), n'a pas besoin de
 * connaitre les détails de l'implémentation du cache (struct Cache) : c'est un
 * exemple de type \b opaque. 
 *
 * \author MEURGUES Nicolas
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cache.h"
#include "low_cache.h"
#include "strategy.h"

//! Création du cache.
struct Cache *Cache_Create(const char *file, unsigned nblocks, unsigned nrecords, size_t recordsz, unsigned nderef) {
    int tmp;

    // Allocation de la structure du cache
    struct Cache *pcache = (struct Cache *)malloc(sizeof(struct Cache));

    // Sauvegarde du nom de fichier
    pcache->file = (char *)malloc(strlen(file) + 1);
    strcpy(pcache->file, file);

    // Ouverture du fichier en mode "update"
    if ((pcache->fp = fopen(file, "r+")) == NULL)
		if ((pcache->fp = fopen(file, "w+")) == NULL)
			return CACHE_KO;

    pcache->nblocks = nblocks;
    pcache->nrecords = nrecords;
    pcache->recordsz = recordsz;
    pcache->nderef = nderef;
    pcache->blocksz = nrecords*recordsz;

    // Allocation des entetes de bloc, et des blocs eux-memes
    pcache->headers = malloc(nblocks*sizeof(struct Cache_Block_Header));
    for (tmp = 0; tmp < nblocks; tmp++) {
    	pcache->headers[tmp].data = (char *)malloc(pcache->blocksz);
		pcache->headers[tmp].ibcache = tmp;
		pcache->headers[tmp].flags = 0;
    }

    // Initialisation du pointeur sur le premier bloc ltmpre, cad ici le premier bloc
    pcache->pfree = pcache->headers;

    // Mise à 0 des données d'instrumentation
    Cache_Get_Instrument(pcache);

    // Initialisation de la stratégie
    pcache->pstrategy = Strategy_Create(pcache);

    // Retour du cache
    return pcache;
}

//! Fermeture (destruction) du cache.
Cache_Error Cache_Close(struct Cache *pcache) {
    int tmp;

    // Synchronisation et fermeture de la stratégie
    Cache_Sync(pcache);
    Strategy_Close(pcache);

    // Libération des blocs 
    for (tmp = 0; tmp < pcache->nblocks; tmp++) {
        free(pcache->headers[tmp].data);   
    }

    // Déallocation des structs
    free(pcache->headers);
    free(pcache->file);
    free(pcache);

    return CACHE_OK;
}

//! Ecriture sur le Block
static Cache_Error Write_Block(struct Cache *pcache, struct Cache_Block_Header *header) {
    // On positionne le pointeur à l'adresse du block cherché
    fseek(pcache->fp, DADDR(pcache, header->ibfile), SEEK_SET);

    // Ecriture des données du Block
    if (fwrite(header->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz)
    	return CACHE_KO;

    // On efface le bit M
    header->flags &= ~MODIF;

    return CACHE_OK;
}

//! Synchronisation du cache.
Cache_Error Cache_Sync(struct Cache *pcache) {
    int tmp;

    //visite des blocks
    for (tmp = 0; tmp < pcache->nblocks; tmp++) {
		struct Cache_Block_Header *header = &pcache->headers[tmp];

		//si le bloc a V et M à 1 :
		if (header->flags & (VALID | MODIF)) {
	 	   if (Write_Block(pcache, header) == CACHE_KO)
	 	   	return CACHE_KO;
	 	}
    }    

    //On incrémente le nombre de synchronisations
    pcache->instrument.n_syncs++;

    return CACHE_OK;
}

//! Invalidation du cache.
Cache_Error Cache_Invalidate(struct Cache *pcache) {
    int tmp;
    int c_err;

    // Synchronisation du cache
    c_err = Cache_Sync(pcache);
    if (c_err != CACHE_OK) {
    	return c_err;
    }

    struct Cache_Block_Header *header;
    // On met V à 0 dans tout les blocks
    for (tmp = 0; tmp < pcache->nblocks; tmp++) {
    	header = &pcache->headers[tmp];
    	header->flags &= ~VALID; 
    }

    // Initialisation du pointeur sur le premier bloc
    pcache->pfree = pcache->headers;

    //on appéle le Invalidate de la stratégie
    Strategy_Invalidate(pcache);

    return CACHE_OK;
}

//!lecture du Block
static Cache_Error Read_Block(struct Cache *pcache, struct Cache_Block_Header *header) {
    long loff, leof;

    // On cherche la longueur actuelle du fichier
    int value = fseek(pcache->fp, 0, SEEK_END);
    if (value < 0) {
    	return CACHE_KO;
    }

    leof = ftell(pcache->fp);

    // Si l'on est au dela de la fin de fichier, on cree un bloc de zeros.
    loff = DADDR(pcache, header->ibfile); // Adresse en octets du bloc dans le fichier

    /*Si le bloc cherché est au dela de la fin de fichier on n'effectue aucune entrée-sortie,
    se contentant de mettre le bloc à 0*/
    if (loff >= leof) {
        memset(header->data, '\0', pcache->blocksz); 
    } else {		//Si le bloc existe, on s'y rend puis on le lis
    	if (fseek(pcache->fp, loff, SEEK_SET) != 0) {
    		return CACHE_KO;
    	}
    	if (fread(header->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz) {
    		return CACHE_KO;
    	}
    } 

    // On met à 1 V
    header->flags |= VALID;

    return CACHE_OK;
}

//! Recherche d'un Block
static struct Cache_Block_Header *Find_Block(struct Cache *pcache, int irfile) {
    int tmp;
    int ibfile = irfile / pcache->nrecords;

    //On parcourt les block
    for (tmp = 0; tmp < pcache->nblocks; tmp++) {
		struct Cache_Block_Header *header = &pcache->headers[tmp];

		//S'ils sont valides et qu'ils on le même ibfile que celui cherché, on le retourne
		if (header->flags & VALID && header->ibfile == ibfile) {
		    pcache->instrument.n_hits++;
		    return header;
		}
    }

    return NULL;
}

//! Réccupère un Block grace à son irfile
struct Cache_Block_Header *Get_Block(struct Cache *pcache, int irfile) {
    struct Cache_Block_Header *header;

    //Si le Block est nul l'enregistrement n'est pas dans le cache
    header = Find_Block(pcache, irfile);
    if (header == NULL) {
        
        // On fait appel à Strategy_Replace_Block et retourne NULL si ce dernier n'existe pas
		header = Strategy_Replace_Block(pcache);
		if (header == NULL) {
			return NULL;
		}
		// Si V et M sont à 1, on le sauve sur le fichier
		Cache_Error c_err = Write_Block(pcache, header);
		if ((header->flags & VALID) && (header->flags & MODIF) && (c_err != CACHE_OK)) {
	    	return NULL;
		}
	
        //On rempli header
        header->flags = 0;
        header->ibfile = irfile / pcache->nrecords; /* indice du bloc dans le fichier */
        if (Read_Block(pcache, header) != CACHE_OK) {
        	return NULL;
        }
    }

    //On retourne le header
    return header;
}

//! Vérification de la nécessité de synchroniser
static Cache_Error Verify_Sync_Need(struct Cache *pcache) {
    static int sync_freq = NSYNC;

    if (--sync_freq == 0) {
		sync_freq = NSYNC;
		return Cache_Sync(pcache);
    }
    
    else return CACHE_OK;
}

//! Lecture  (à travers le cache).
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord) {
	struct Cache_Block_Header *header;

	//On incrémente le nombre de lectures
    pcache->instrument.n_reads++;

    //Si le header est NULL on retourne CACHE_KO
    header = Get_Block(pcache, irfile);
    if (header == NULL) {
    	return CACHE_KO;
    }

    //On copie la mémoire
    memcpy(precord, ADDR(pcache, irfile, header), pcache->recordsz);

    //La stratégie lis
    Strategy_Read(pcache, header);

    //on vérifie s'il est nécéssaire de synchroniser
    return Verify_Sync_Need(pcache);
}

//! Écriture (à travers le cache).
Cache_Error Cache_Write(struct Cache *pcache, int irfile, const void *precord) {
    struct Cache_Block_Header *header;

    //On incrémente le nombre d'écritures
    pcache->instrument.n_writes++;

    //Si le bloc n'existe pas, on retourne CACHE_KO
    header = Get_Block(pcache, irfile);
    if (header == NULL)
    	return CACHE_KO;
    
    //On copie les données du buffer dans le cache
    memcpy(ADDR(pcache, irfile, header), precord, pcache->recordsz);

    //On ajoute M aux flags
    header->flags |= MODIF;

    //On fait appel au Write de la stratégie
    Strategy_Write(pcache, header);

    //On vérifie s'il faut synchroniser
    return Verify_Sync_Need(pcache);
}

//! Résultat de l'instrumentation.
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache) {
    //Copie du Cache_Instrument
    struct Cache_Instrument copy = pcache->instrument;

    //On réinitialise le Cache_Instrument
    pcache->instrument.n_reads = pcache->instrument.n_writes = 0;
    pcache->instrument.n_hits = pcache->instrument.n_syncs = 0;
    pcache->instrument.n_deref = 0;

    //On retourne la copie
    return &copy;
}