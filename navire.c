#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h> /* fcntl */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> /* errno */
#include <string.h>

#include <bateau.h>
#include <mer.h>

void detruireBateau(int fdMer, int * nbBateaux, bateau_t * bateau) { // Un bateau a coulé
	mer_nb_bateaux_ecrire(fdMer, --(*nbBateaux));
	bateau_destroy(&bateau);
}

int bouclierBateauVerrou(const int fdMer, bateau_t * bateau, int verrouiller) {
	// Verrouille les cases appartenant au bateau
	int i;
	coords_t * bateauCellules = bateau_corps_get(bateau);
	coord_t * bateauCellule = bateauCellules->coords;
	
	struct flock verrou;
	
	verrou.l_whence = 0;
	verrou.l_len = MER_TAILLE_CASE;
	
	if(verrouiller) {
		verrou.l_type = F_WRLCK;
	} else {
		verrou.l_type = F_UNLCK;
	}
	
	for(i = 0; i < bateauCellules->nb; i++) {
		verrou.l_start = bateauCellule[i].pos;
	
		if(fcntl(fdMer, F_SETLKW, &verrou) == ERREUR) {
			if(verrouiller) {
				printf("Erreur lors de la pose du verrou bouclier\n");
			} else {
				printf("Erreur lors de la levee du verrou bouclier\n");
			}
			
			return ERREUR;
		}
	}

	return CORRECT;
}

int headerVerrou(const int fdMer, bateau_t * bateau, int verrouiller) { // Bloque le header du fichier mer pour empecher vision de le lire
	struct flock verrou;
	
	verrou.l_whence = 0;
	verrou.l_start = 0;
	verrou.l_len = MER_TAILLE_ENTETE;
	
	if(verrouiller) {
		verrou.l_type = F_WRLCK;
	} else {
		verrou.l_type = F_UNLCK;
	}
	
	if(fcntl(fdMer, F_SETLKW, &verrou) == -1) { // Si un verrou est deja pose, on ne peut pas mettre celui-ci par dessus
		return ERREUR;
	}
	return CORRECT;
}

int voisinsVerrou(const int fdMer, bateau_t * bateau, const coords_t listeVoisins, int verrouiller) {
	int i;
	case_t merCase;
	struct flock verrou;
	
	verrou.l_whence = 0;
	verrou.l_len = 0;
	
	if(verrouiller) {
		verrou.l_type = F_WRLCK;
	} else {
		verrou.l_type = F_UNLCK;
	}
	
	for(i = 0; i < listeVoisins.nb; i++) {
		mer_case_lire(fdMer, listeVoisins.coords[i], &merCase);
		if(merCase == MER_CASE_LIBRE) { // Si la case est libre, on la verrouille
			verrou.l_start = listeVoisins.coords[i].pos;			
			if(fcntl(fdMer, F_SETLK, &verrou) == -1) {
				return ERREUR;
			}
		}
	}
	
	return CORRECT;
}

int caseVerrou(const int fdMer, bateau_t * bateau, coord_t cible, int verrouiller) {  // Pose un verrou sur une case en particulier
	struct flock verrouCase;
	
	verrouCase.l_whence = 0;
	verrouCase.l_start = cible.pos;
	verrouCase.l_len = MER_TAILLE_CASE;
	
	if(verrouiller) {
		verrouCase.l_type = F_WRLCK;
	} else {
		verrouCase.l_type = F_UNLCK;
	}
	
	if(fcntl(fdMer, F_SETLK, &verrouCase)) {
		if(verrouiller) {
			printf("Erreur lors de la pose du verrou\n");
		} else {
			printf("Erreur lors de la levee du verrou\n");
		}
		
		return ERREUR;
	}
	
	return CORRECT;
}

/* 
 *	Programme principal 
 */

int main( int nb_arg , char * tab_arg[] ) {

	char fich_mer[128] ;
	case_t marque = MER_CASE_LIBRE ;
	char nomprog[128] ;
	float energie = 0.0 ;

	strcpy( nomprog , tab_arg[0] ) ;
	strcpy( fich_mer , tab_arg[1]);
	marque = tab_arg[2][0] ;
	sscanf( tab_arg[3] , "%f" , &energie );
	
	/* Initialisation de la generation des nombres pseudo-aleatoires */
	srandom((unsigned int)getpid());


	printf( "\n\n%s : ----- Debut du bateau %c (%d) -----\n\n ", nomprog , marque , getpid() );
	
	// INITIALISATION DE LA MER

	bateau_t * bateau = BATEAU_NULL ;
	int nbBateaux = 0 ; 
	int fdMer;								// Descripteur du fichier mer
	
	fdMer = open( fich_mer , O_RDWR | O_CREAT , 0666); // Ouverture du fichier mer
	
	bateau	= bateau_new(NULL, marque, energie);

	// Initialisation du bateau dans la mer	
	if(mer_bateau_initialiser(fdMer, bateau)) { // Si return 1, ya une erreur
		printf("Erreur dans l'initialisation de la mer\n");  // Probleme ici...
	
		exit(0);
	}
	
	mer_nb_bateaux_lire(fdMer, &nbBateaux);
	mer_nb_bateaux_ecrire(fdMer, ++nbBateaux);
	
	// LE BOUCLIER
	// On verifie si on a assez d'energie, si oui on pose un verrou pour empecher les tirs
	// Si non on l'enleve
	(energie >= BATEAU_SEUIL_BOUCLIER) ? bouclierBateauVerrou(fdMer, bateau, 1) : bouclierBateauVerrou(fdMer, bateau, 0);
	
	// BOUCLE PRINCIPALE

	booleen_t acquisition;
	booleen_t coule = FAUX;
	booleen_t deplacementReussi = VRAI;
	coords_t * listeVoisins = NULL;
	coord_t cible;

	while(1) { // Boucle de jeu
		sleep(3);
		
		// DEBUT DU TOUR
		printf("Action en cours avec le Bateau %c, energie restante : %f \n", bateau->marque, energie);
		
		mer_nb_bateaux_lire(fdMer, &nbBateaux);
		
		if(coule && energie < BATEAU_SEUIL_BOUCLIER) { // Destruction du bateau si touche par un autre
			printf("\nBateau %c : Destruction \n", bateau->marque);
			mer_nb_bateaux_ecrire(fdMer, --nbBateaux);
			mer_bateau_couler(fdMer, bateau);
			bateau_destroy(&bateau);
			close(fdMer);
			
			exit(0);
		}

		if(nbBateaux == 1) {
			printf("\nBateau %c : Victorieux\n", bateau->marque);
			sleep(5);
			close(fdMer);
			
			exit(0);
		}
		
		listeVoisins = coords_new();
		mer_voisins_rechercher(fdMer, bateau, &listeVoisins); // Ici qu'on recupere liste des voisins
		

		// LES DEPLACEMENTS

		// Deplacement aleatoire du bateau
		if(energie > 0) {
			if(voisinsVerrou(fdMer, bateau, *listeVoisins, 1) == ERREUR) {// On verifie que le bateau peut se deplacer et que les cases autour sont libres
				printf("Erreur lors du deplacement du bateau %c\n", marque);
				deplacementReussi = FAUX;
			}

			// On empeche la lecture du fichier pendant le deplacement du bateau
			headerVerrou(fdMer, bateau, 1);

			if((mer_bateau_deplacer(fdMer, bateau, listeVoisins, &deplacementReussi))) {
				// On deplace enfin le bateau
				printf("Erreur dans le deplacement du bateau sur la mer\n");
				detruireBateau(fdMer,&nbBateaux, bateau);

				exit(0);
			} else {
				energie -= 25; // Un deplacement coute 25 d'energie
			}

			// On leve le verrou
			headerVerrou(fdMer, bateau, 0);
		}else{
			deplacementReussi = FAUX;
		}

		
		voisinsVerrou(fdMer, bateau, *listeVoisins, 0); // Verrou sur les voisins pour le deplacement
		
		if(deplacementReussi) {
			printf("Deplacement reussi\n");
		}
		else {
			printf("Deplacement rate\n");
		}

		coords_destroy(&listeVoisins);

		// LE COMBAT

		// Tir sur les bateaux
		if((mer_bateau_cible_acquerir(fdMer, bateau, &acquisition, &cible))) {
			printf("Erreur lors de l'acquisition d'un bateau");
			detruireBateau(fdMer,&nbBateaux, bateau);

			exit(0);
		}

		if(acquisition) { // Si on peut tirer sur la cible
			// On regarde si ya un verrou, donc si la cible a son bouclier
			if(caseVerrou(fdMer, bateau, cible, 1) != ERREUR) { // On peut tirer
				if((mer_bateau_cible_tirer(fdMer, cible))) { // On tire
					printf("Erreur lors du tir du bateau %c\n", marque);
					detruireBateau(fdMer,&nbBateaux, bateau);

					exit(0);
				}

				caseVerrou(fdMer, bateau, cible, 0); // On enleve le verrou

			} else {
				printf("Erreur lors du verouillage de la cible en [%i, %i]\n", cible.l, cible.c);
			}
		} else {
			printf("Erreur lors de l'acquisition d'une cible pour le bateau %c\n", marque);
		}
		
		printf("\n\n");
	}
	
	

	printf( "\n\n%s : ----- Fin du jeu %c (%d) -----\n\n ", 
		nomprog , marque , getpid() );

	exit(0);
}
