#include <assert.h>

#include "strategy.h"
#include "low_cache.h"
#include "random.h"
#include "time.h"

/***********************************************************
*********** DEFINE CONSTANTES ET FONCTIONS UTILES **********
***********************************************************/

/* Flag désignant si un bloc a été référencé */
//#define R_FLAG 0x4

/* Paramètre constant remettant tout les flags R à 0 */
#define NDEREF 150

static void reset_flag_R(struct Cache *pcache);
static int EQUATION(struct Cache_Block_Header *block);

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
// int VALEUR_BOOLEEN(int b) {
//     if (b != 0) return 1;
//     else return 0;
// }

//  Fait l'équation n = 2*R+M. Le décalage à gauche de 1 bits nous permet
//  * de multiplier par 2. Puis le ou logique va nous permettre d'additionner
//  * M à 2*R. L'utilisation d'opérateurs sur les bits est indispensable car 
//  * nous avons des bits et non des entiers.
//  *
//  * Exemple : Si R et M valent 1, alors VALEUR_BOOLEEN(1)<<1 donnera 10 (=2).
//  *           Ce qui nous donnera au final 10 | 01 = 11 (=3) 
 
// int EQUATION(int R,int M) {
//     int equation = VALEUR_BOOLEEN(R)<<1 | VALEUR_BOOLEEN(M);
//     return equation;
// } 


/***********************************************************
************************ STRATEGIE *************************
***********************************************************/

/* Pour initialiser la stratégie, on va allouer un espace mémoire pour la
 * structure, en initialisant nderef et le compteur_dereferencement à la 
 * valeur de nderef de la structure du Cache dans low_cache.c
 */
void *Strategy_Create(struct Cache *pcache) {
	struct Strategie_NUR *pointeur_struct = malloc(sizeof(struct Strategie_NUR));

    pointeur_struct->nderef = pcache->nderef;
    pointeur_struct->compteur_dereferencement = pcache->nderef;

    return pointeur_struct;	
}

/* Arrêter la stratégie */
void Strategy_Close(struct Cache *pcache) {
    // On vide le pointeur sur la stratégie afin qu'il puisse
    // en excuter une autre quand il veut.
	free(pcache->pstrategy);
}

/* Remise à zéro de tout les bits de référence R */
void Strategy_Invalidate(struct Cache *pcache) {
    // Réinitialisation des R_FLAG et des compteur de chaque blocs
    reset_flag_R(pcache);
}

/* Permet de remplacer un bloc déjà utilisé */
struct Cache_Block_Header *Strategy_Replace_Block(struct Cache *pcache) {

    int index_block;
    int equation_max = 4;
    struct Cache_Block_Header *cbh_final = NULL;
    struct Cache_Block_Header *cbh;

   /* On cherche d'abord un bloc libre, si on a NULL : 
    * ça veut dire qu'on a un bloc de libre 
    */
    if ((cbh = Get_Free_Block(pcache)) != NULL) return cbh;

    /* Si on a pas trouvé de bloc libre, on va parcourir tout les blocs,
     * en partant de l'indice 0, jusqu'au nombre max de blocks dans le cache.
     */
    for (index_block = 0; index_block < pcache->nblocks; index_block++) {
		cbh = &pcache->headers[index_block];
	
		/* On va faire l'équation 2*r + m afin de trouver le meilleur
         * block à remplacer 
         */
		int equation = EQUATION(cbh);
        
        // Si on trouve un block non modifié et jamais utilisé, alors on prend celui là
        if (equation == 0) return cbh;

        // Sinon, on va chercher le bloc qui a l'équation rm la plus petite
		else if (equation < equation_max) {
	       equation_max = equation;
	       cbh_final = cbh;
		}	
    }

    return cbh_final;
}

/* Méthode qui est utilisée si on veut lire */
void Strategy_Read(struct Cache *pcache, struct Cache_Block_Header *cbh) {

    struct Strategie_NUR *strategy = (struct Strategie_NUR*)(pcache)->pstrategy;

    // Si le compteur de déréférencements est égal à nderef, on réinitialise
    if((++strategy->compteur_dereferencement) == strategy->nderef)
        reset_flag_R(pcache);
    // On met le flag REFER à 1 (car accès en lecture)
    cbh->flags |= R_FLAG;
}  

/* Méthode qui est utilisée si on veut écrire */
void Strategy_Write(struct Cache *pcache, struct Cache_Block_Header *cbh) {

    struct Strategie_NUR *strategy = (struct Strategie_NUR*)(pcache)->pstrategy;

    // Si le compteur de déréférencements est égal à nderef, on réinitialise
    if((++strategy->compteur_dereferencement) == strategy->nderef)
        reset_flag_R(pcache);
    // On met le flag REFER à 1 (car accès en écriture)
    cbh->flags |= R_FLAG;
} 

/* Le nom de la stratégie */
char *Strategy_Name() {
    return "NUR";
}


/***********************************************************
******************** FONCTIONS STATIC **********************
***********************************************************/

/* Fonction qui permet de réinitialiser tout les R_FLAG
 * et le compteur de chaque block
 */
static void reset_flag_R(struct Cache *pcache){
        // On récupère la stratégie en cours
        struct Strategie_NUR *strat = (struct Strategie_NUR*) pcache->pstrategy;

    // Si on peut déréférencer
    if(strat->nderef != 0){
        // On va parcourir tout les blocks du cache et remettre le R_FLAG à 0
        for (int i = 0 ; i < pcache->nblocks ; i++)
            pcache->headers[i].flags &= ~R_FLAG;
        // On remet à 0 le compteur de déréférencement
        strat->compteur_dereferencement = 0;
        // Vu qu'on a déréférencé, on va incrémenter le n_deref de instrument pour les statistiques
            pcache->instrument.n_deref++;
    }
}

/* Fait l'équation n = 2*r + m. */
static int EQUATION(struct Cache_Block_Header *block) {
    int rm_equation = 0;

    if ( (block->flags & R_FLAG) > 0 ) rm_equation += 2;
    if ( (block->flags & MODIF) > 0 ) rm_equation += 1;
    
    return rm_equation;
}