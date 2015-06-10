#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"

/***********************************************************
*********** DEFINE CONSTANTES ET FONCTIONS UTILES **********
***********************************************************/

/* Flag désignant si un bloc a été référencé */
#define R_FLAG 0x4

/* Paramètre constant remettant tout les flags R à 0 */
#define NDEREF 150

/* En-tête de la fonction static utilisée plus loin */
static void Dereference_If_Needed(struct Cache *pcache);

/* Structure utilisée pour la stratégie NUR */
struct Strategie_NUR
{
	unsigned nderef; 					/* période avant remiser des R à 0 */
	unsigned compteur_dereferencement;	/* compteur pour le déréférencement */
};

/* Permet de récupérer la valeur de pstrategy
 * de la structure Cache en type structure Strategie_NUR.
 * On le cast parce que pstrategy est un void  *, cela afin
 * de pouvoir n'importe quelle stratégie pour un cache.
 */
struct Strategie_NUR* STRATEGIE_NUR(struct Cache* pointeur_cache) {
    return (struct Strategie_NUR *) pointeur_cache->pstrategy;
}

/* Donne la valeur du boolen : si b est différent de 0, 
 * alors on retourne 1 sinon on retourne 0
 */
int VALEUR_BOOLEEN(int b) {
    if (b != 0) return 1;
    else return 0;
}

/* Fait l'équation n = 2*R+M. Le décalage à gauche de 1 bits nous permet
 * de multiplier par 2. Puis le ou logique va nous permettre d'additionner
 * M à 2*R. L'utilisation d'opérateurs sur les bits est indispensable car 
 * nous avons des bits et non des entiers.
 *
 * Exemple : Si R et M valent 1, alors VALEUR_BOOLEEN(1)<<1 donnera 10 (=2).
 *           Ce qui nous donnera au final 10 | 01 = 11 (=3) 
 */
int EQUATION(int R,int M) {
    int equation = VALEUR_BOOLEEN(R)<<1 | VALEUR_BOOLEEN(M);
    return equation;
} 


/***********************************************************
************************ STRATEGIE *************************
***********************************************************/

/* Pour initialiser la stratégie, on va allouer un espace mémoire pour la
 * structure, en initialisant nderef et le compteur_dereferencement à la 
 * valeur de nderef de la structure du Cache dans low_cache.c
 */
void *Strategy_Create(struct Cache *pcache) 
{
	struct Strategie_NUR *pointeur_struct = malloc(sizeof(struct Strategie_NUR));

    //nderef vaut 0
    pointeur_struct->nderef = pcache->nderef;
    pointeur_struct->compteur_dereferencement = pcache->nderef;

    return pointeur_struct;	
}

/* Arrêter la stratégie */
void Strategy_Close(struct Cache *pcache)
{
	free(pcache->pstrategy);
}

/* Remise à zéro de tout les bits de référence R */
void Strategy_Invalidate(struct Cache *pcache)
{
	struct Strategie_NUR *pointeur_struct = STRATEGIE_NUR(pcache);

    if (pointeur_struct->nderef != 0) 
    {
        pointeur_struct->compteur_dereferencement = 1;
        Dereference_If_Needed(pcache);
    } 
}

/* Permet de remplacer un bloc déjà utilisé */
struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) 
{
    int ib;
    int rm;
    struct Cache_Block_Header *pbh_save = NULL;
    struct Cache_Block_Header *pbh;

   /* On cherche d'abord un bloc invalide */
    if ((pbh = Get_Free_Block(pcache)) != NULL) return pbh;

    /* Ici, min vaut 4. On va parcourir tout les blocs, en partant
     * de l'indice 0, jusqu'au nombre max de blocks dans le cache.
     */
    for (int min = EQUATION(1,1) + 1, ib = 0; ib < pcache->nblocks; ib++) {
		pbh = &pcache->headers[ib];
	
		/* On construit un nombre binaire rm avec le bit de modification
         * et le bit de référence, et on cherche le bloc avec la valeur de rm
         * minimale */
        printf("pbh->flags %d\n",pbh->flags);
        printf("R_FLAG %d\n",R_FLAG);
        printf("pbh->flags & R_FLAG %d\n",pbh->flags & R_FLAG);
        printf("MODIF %d\n",MODIF);
		rm = EQUATION(pbh->flags & R_FLAG, pbh->flags & MODIF);
		if (rm == 0) return pbh; /* pas la peine de cherche plus loin */
		else if (rm < min) {
	    min = rm;
	    pbh_save = pbh;
		}	
    }

    return pbh_save;
}



void Strategy_Read(struct Cache *pcache, struct Cache_Block_Header *pbh) 
{
    Dereference_If_Needed(pcache);
    pbh->flags |= R_FLAG;
}  

void Strategy_Write(struct Cache *pcache, struct Cache_Block_Header *pbh)
{
    Dereference_If_Needed(pcache);
    pbh->flags |= R_FLAG;
} 

char *Strategy_Name()
{
    return "NUR";
}




static void Dereference_If_Needed(struct Cache *pcache)
{
    struct Strategie_NUR *pstrat = STRATEGIE_NUR(pcache);
    int ib;

    /* On déréférence tous les deref accès ; si deref est 0 on ne déréférence jamais */
    if (pstrat->nderef == 0 || --pstrat->compteur_dereferencement > 0) return;

    /* C'est le moment : on remet à 0 tous les bits de déférence */
    for (ib = 0; ib < pcache->nblocks; ib++) pcache->headers[ib].flags &= ~R_FLAG;

    /* On réarme le compteur et on met à jour l'instrumentation */
    pstrat->compteur_dereferencement = pstrat->nderef;
    ++pcache->instrument.n_deref;
}