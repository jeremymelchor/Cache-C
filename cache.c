/*!
 * \file cache.c
 * 
 * \brief Implémentation du cache.
 *
 * \author Jean-Paul Rigault 
 *
 * $Id: cache.c,v 1.3 2008/03/04 16:52:49 jpr Exp $
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "cache.h"
#include "low_cache.h"
#include "strategy.h"

static struct Cache_Block_Header *Get_Block(struct Cache *pcache, int irfile);
static struct Cache_Block_Header *Find_Block(struct Cache *pcache, int irfile);
static Cache_Error Read_Block(struct Cache *pcache, struct Cache_Block_Header *pb);
static Cache_Error Write_Block(struct Cache *pcache, struct Cache_Block_Header *pb);
static Cache_Error Do_Sync_If_Needed(struct Cache *pcache);

/*!
 * \ingroup cache_interface
 *
 * Cette fonction crée et initialise le cache :
 *   - Allocation du cache lui-même
 *   - Ouverture du fichier
 *   - Initialisation des champs du cache.
 *   - Allocation des entêtes de blocs et des blocs eux-memes
 *
 * \param file nom du fichier
 * \param nblocks nombre de blocs dans le cache
 * \param nrecords nombre d'enregistrements par bloc
 * \param recordsz taille d'un enregistrement
 * \param nderef période de déréférençage (pour NUR)
 * \return un pointeur sur le nouveau cache ou le pointeur nul en cas d'erreur
 */
struct Cache *Cache_Create(const char *file, unsigned nblocks,
			   unsigned nrecords, size_t recordsz,
                           unsigned nderef)
{
    int ib;

    /* Allocation de la structure du cache */
    struct Cache *pcache = (struct Cache *)malloc(sizeof(struct Cache));

    /* Sauvegarde du nom de fichier */
    pcache->file = (char *)malloc(strlen(file) + 1);
    strcpy(pcache->file, file);

    /* Ouverture du fichier en mode "update" :
     * On verifie d'abord que le fichier existe ("r+").
     * Sinon on le cree ("w+").
     */
    if ((pcache->fp = fopen(file, "r+")) == NULL)
	if ((pcache->fp = fopen(file, "w+")) == NULL) return CACHE_KO;

    /* Initialisation des valeurs de dimensionnement */
    if (nblocks > 0) pcache->nblocks = nblocks;
    else return 0;
    if (nrecords > 0) pcache->nrecords = nrecords;
    else return 0;
    if (recordsz > 0) pcache->recordsz = recordsz;
    else return 0;
    if (recordsz > 0) pcache->nderef = nderef;
    else return 0;

    pcache->blocksz = nrecords*recordsz;

    /* Allocation des entetes de bloc, et des blocs eux-memes */
    pcache->headers = malloc(nblocks*sizeof(struct Cache_Block_Header));
    for (ib = 0; ib < nblocks; ib++)
    {	pcache->headers[ib].data = (char *)malloc(pcache->blocksz);
	pcache->headers[ib].ibcache = ib;
	pcache->headers[ib].flags = 0;
    }

    /* Initialisation du pointeur sur le premier bloc libre, cad ici le premier
     * bloc */
    pcache->pfree = pcache->headers;

    /* Mise à 0 des données d'instrumentation */
    (void)Cache_Get_Instrument(pcache);

    /* Initialisation de la stratégie */
    pcache->pstrategy = Strategy_Create(pcache);

    /* On rend ce que l'on vient de créer et d'initialiser */
    return pcache;
}

/*!
 * \ingroup cache_interface
 *
 * Cette fonction ferme et détruit le cache :
 *    - Synchronisation du cache.
 *    - Déallocation des entetes de blocs et des blocs eux-memes.
 *    - Déallocation du nom de fichier.
 *    - Déallocation du cache lui-même
 *
 * \param pcache un pointeur sur le cache à fermer
 * \return le code d'erreur
 */
Cache_Error Cache_Close(struct Cache *pcache)
{
    int ib;

    /* Synchronisation */
    Cache_Sync(pcache);

    /* Fermeture de la stratégie */
    Strategy_Close(pcache);

    /* Déallocation des blocks */ 
    for (ib = 0; ib < pcache->nblocks; ib++)
        free(pcache->headers[ib].data);   

    /* Déallocation des entêtes et du nom de fichier */
    free(pcache->headers);
    free(pcache->file);

    /* Déallocation du cache lui-même */
    free(pcache);

    return CACHE_OK;
}

/*!
 * \ingroup cache_interface
 *
 * Cette fonction synchronise le contenu du fichier avec celui du cache.
 * 
 * On parcourt tous les blocs valides (flag \c VALID) en écrivant ceux qui 
 * sont modifiés (flag \c MODIF). 
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \return le code d'erreur
 */
Cache_Error Cache_Sync(struct Cache *pcache)
{
    int ib;

    for (ib = 0; ib < pcache->nblocks; ib++)
    {
	struct Cache_Block_Header *pbh = &pcache->headers[ib];

	if (pbh->flags & (VALID | MODIF)) 
	    if (Write_Block(pcache, pbh) == CACHE_KO) return CACHE_KO;
    }    

    pcache->instrument.n_syncs++;

    return CACHE_OK;
}

/*! 
 * \ingroup cache_interface
 *
 * Cette fonction invalide le cache, c'est-à -dire le vide de son contenu.
 * 
 * On synchronise le cache puis tous les blocs sont marqués invalides
 * (RAZ du flag \c VALID). C'est comme si le cache devenait vide. On invalide
 * aussi la stratégie.
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \return le code d'erreur
 */
Cache_Error Cache_Invalidate(struct Cache *pcache)
{
    int ib;
    int rcode;

    /* Synchronisation du cache */
    if ((rcode = Cache_Sync(pcache)) != CACHE_OK) return rcode;

    /* Tous les blocs deviennent invalides */
    for (ib = 0; ib < pcache->nblocks; ib++)
    {
	struct Cache_Block_Header *pbh = &pcache->headers[ib];

	pbh->flags &= ~VALID; 
    }

    /* Initialisation du pointeur sur le premier bloc libre, cad ici le premier
     * bloc */
    pcache->pfree = pcache->headers;

    /* La stratégie a peut-être quelque chose à faire */
    Strategy_Invalidate(pcache);

    return CACHE_OK;
}

/*!
 * \ingroup cache_interface
 *
 * Cette fonction permet de lire un enregistrement à travers le cache.
 *  
 * La fonction \c Get_Block retourne le bloc du cache contenant l'enregistrement à
 * l'index-fichier \a irfile. Il n'y a plus qu'à faire la copie physique dans la zone
 * de l'utilisateur. On doit également notifier la stratégie de ce qu'on vient
 * de faire.
 *  
 * La fonction \c Do_Sync_If_Needed effectue une synchronisation à
 * intervalle régulier.
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \param irfile index de l'enregistrement dans le fichier
 * \param precord adresse où ranger l'enregistrement dans l'espace utilisateur
 * \return le code d'erreur
 */
Cache_Error Cache_Read(struct Cache *pcache, int irfile, void *precord)
{
    struct Cache_Block_Header *pbh;

    pcache->instrument.n_reads++;

    if ((pbh = Get_Block(pcache, irfile)) == NULL) return CACHE_KO;
    (void)memcpy(precord, ADDR(pcache, irfile, pbh), pcache->recordsz);

    Strategy_Read(pcache, pbh);

    return Do_Sync_If_Needed(pcache);
}

/*!
 * \ingroup cache_interface
 *
 * Cette fonction permet d'écrire un enregistrement à travers le cache.
 *  
 * La fonction Get_Block retourne le bloc du cache contenant l'enregistrement à
 * l'indice-fichier irfile. Il n'y a plus qu'à faire la copie physique depuis la
 * zone de l'utilisateur. Le bloc est marqué modifié (flag MODIF).
 * La fonction Do_Sync_If_Needed effectue une synchronisation à intervalle
 * régulier.
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \param irfile index de l'enregistrement dans le fichier
 * \param precord adresse où lire l'enregistrement dans l'espace utilisateur
 * \return le code d'erreur 
 */
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

/*! 
 * \ingroup cache_interface
 *
 * Accès aux données d'instrucmentation.
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \return un pointeur sur els données d'instrumentation
 */
struct Cache_Instrument *Cache_Get_Instrument(struct Cache *pcache)
{
    static struct Cache_Instrument save;

    save = pcache->instrument;
    pcache->instrument.n_reads = pcache->instrument.n_writes = 0;
    pcache->instrument.n_hits = pcache->instrument.n_syncs = 0;
    pcache->instrument.n_deref = 0;

    return &save;
}

/*! 
 * \defgroup cache_internal Fonctions internes à la gestion générique du cache.
 *
 * Ces fonctions sont privées au module de gestion générique (indépendant de la
 * stratégie) c'est-à-dire à cache.c..
 */

/*! \brief Synchronisation périodique.
 * 
 *  \ingroup cache_internal
 * 
 *  Pour simplifier, elle est réalisée toutes les \c NSYNC opérations de lecture
 *  ou d'écriture. Dans une système réel, elle serait plutôt réalisée
 *  périodiquement dans le temps (toutes les \a T secondes).
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \return le code d'erreur 
 */
static Cache_Error Do_Sync_If_Needed(struct Cache *pcache)
{
    static int sync_freq = NSYNC;

    if (--sync_freq == 0)
    {
	sync_freq = NSYNC;
	return Cache_Sync(pcache);
    }
    else return CACHE_OK;
}

/*! \brief Obtention depuis le fichier du bloc contenant l'enregistrement
 * d'indice-fichier donné.
 * 
 *  \ingroup cache_internal
 *
 * Cette fonction retourne un bloc valide contenant l'enregistrement cherché.
 * On cherche d'abord l'enregistrement dans le cache grâce à Find_Block.  Si on
 * ne l'y trouve pas, on obtient un bloc libre grâce à la fonction
 * Strategy_Replace_Block (qui, comme son nom l'indique, depend de la
 * strategie). On sauve ce bloc si il est marqué modifié puis on initialise son
 * entête et on lit son contenu depuis le fichier (Read_Block).
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \param irfile indice du bloc cherché dans le fichier
 * \return un pointeur sur l'entête du bloc (ou le pointeur nul si le bloc n'est pas trouvé)  
*/
struct Cache_Block_Header *Get_Block(struct Cache *pcache, int irfile)
{
    struct Cache_Block_Header *pbh;

    if ((pbh = Find_Block(pcache, irfile)) == NULL)
    {
        /* L'enregistrement n'est pas dans le cache */
        
        /* On demande un block à Strategy_Replace_Block */
	pbh = Strategy_Replace_Block(pcache);
	if (pbh == NULL) return NULL;

	/* Si le bloc libéré est valide et modifié, on le sauve sur le fichier */
	if ((pbh->flags & VALID) && (pbh->flags & MODIF)
            && (Write_Block(pcache, pbh) != CACHE_OK))
	    return NULL; 
	
        /* On remplit le bloc libre et son entête avec l'information de
         * l'enregistrement d'indice-fichier irfile
         */
	pbh->flags = 0;
	pbh->ibfile = irfile / pcache->nrecords; /* indice du bloc dans le fichier */
	if (Read_Block(pcache, pbh) != CACHE_OK) return NULL;
    }

    /* Soit l'enregistrement était déjà dans le cache, soit on vient de l'y mettre */
    return pbh;
}

/*! \brief Recherche d'un enregistrement d'indice-fichier donné dans le cache.
 * 
 *  \ingroup cache_internal
 *
 * On parcourt les blocs du cache pour trouver celui qui contient
 * l'enregistrement cherché. Iici le parcours est linéaire, on devrait pouvoir
 * faire mieux !
 *
 * \param pcache un pointeur sur le cache à synchroniser
 * \param irfile indice du bloc cherché dans le fichier
 * \return un pointeur sur l'entête du bloc (ou le pointeur nul si le bloc n'est pas trouvé) 
 */
static struct Cache_Block_Header *Find_Block(struct Cache *pcache, int irfile)
{
    int ib;
    int ibfile = irfile / pcache->nrecords; /* indice du bloc dans le fichier */

    for (ib = 0; ib < pcache->nblocks; ib++)
    {
	struct Cache_Block_Header *pbh = &pcache->headers[ib];

	if (pbh->flags & VALID && pbh->ibfile == ibfile) 
        {
	    pcache->instrument.n_hits++;
	    return pbh;
	}
    }

    return NULL;
}

/*! \brief Écriture physique d'un bloc dans le fichier.
 * 
 *  \ingroup cache_internal
 *
 * On écrit le bloc puis on efface le flag de modification.
 * 
 * \param pcache un pointeur sur le cache
 * \param un pointeur sur le bloc du cache à écrire
 * \return un code d'erreur
 */
static Cache_Error Write_Block(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
    /* On positionne le pointeur d'E/S à l'adresse du bloc dans le fichier */
    fseek(pcache->fp, DADDR(pcache, pbh->ibfile), SEEK_SET);

    /* On écrit les données du bloc */
    if (fwrite(pbh->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz)
	return CACHE_KO;

    /* On efface la marque de modification */  
    pbh->flags &= ~MODIF;

    return CACHE_OK;
}

/*! \brief Lecture physique d'un bloc depuis le fichier.
 *
 *  \ingroup cache_internal
 *
 * Si le bloc correspond à des enregistrements existants actuellement sur le
 * fichier, on écrase juste les données du fichier par celles du bloc. S'il
 * est dela de la fin de fichier, on cree un bloc de zéros. Dans tous les cas
 * on valide le bloc.
 * 
 * \param pcache un pointeur sur le cache
 * \param un pointeur sur le bloc du cache à écrire
 * \return un code d'erreur
 */
static Cache_Error Read_Block(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
    long loff, leof;

    /* On cherche la longueur courante du fichier */
    if (fseek(pcache->fp, 0, SEEK_END) < 0) return CACHE_KO;
    leof = ftell(pcache->fp);

    /* Si l'on est au dela de la fin de fichier, on cree un bloc de zeros.
       Sinon, on lit le bloc depuis le fichier */

    loff = DADDR(pcache, pbh->ibfile); /* Adresse en octets du bloc dans le fichier */

    if (loff >= leof)
    {
        /* Le bloc cherché est au dela de la fin de fichier. */

        /* On n'effectue aucune entrée-sortie, se contentant de mettre le bloc à 0. */
        memset(pbh->data, '\0', pcache->blocksz); 
    }
    else 
    { 
        /* Le bloc existe dans le fichier */

        /* On y va ! */
	if (fseek(pcache->fp, loff, SEEK_SET) != 0) return CACHE_KO; 

        /* Et on le lit */
	if (fread(pbh->data, 1, pcache->blocksz, pcache->fp) != pcache->blocksz)
	    return CACHE_KO; 
    } 

    /* Le bloc est maintenant valide */
    pbh->flags |= VALID;

    return CACHE_OK;
}




