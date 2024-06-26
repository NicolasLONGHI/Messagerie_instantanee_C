#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h> 

#define NB_MAX_CLIENTS 10
#define TAILLE_BUFFER 1024
#define PORT_PAR_DEFAUT 8888

typedef struct {
    int id;
    int socket;
    struct sockaddr_in address;
    char nomUtilisateur[21];
    int nbMessage;
} client_info;

client_info clients[NB_MAX_CLIENTS];
pthread_t threads[NB_MAX_CLIENTS];
int num_clients = 0;
int server_socket;
int afficherMessage = 1;
const char *nomFichierConfig = "port.txt";
int port = 8888;
pthread_mutex_t mutexClientID;

//Fonction qui est un thread executée à chaque nouvelle connexion d'un client
void *nouvelleConnexion(void *arg) {
    int client_id = *((int *)arg);
    char messageBuffer[TAILLE_BUFFER];

    while (1) {
        memset(messageBuffer, 0, TAILLE_BUFFER);
        int read_size = read(clients[client_id].socket, messageBuffer, TAILLE_BUFFER);
        if (read_size <= 0) {
            printf("Client %d déconnecté\n", client_id);
            pthread_mutex_lock(&mutexClientID);
            clients[client_id].id = -1; //supprime l'id du client qui se déconnecte pour éviter d'avoir le même id si le client se connecte dans le même terminal
            pthread_mutex_unlock(&mutexClientID);
            close(clients[client_id].socket);
            pthread_exit(NULL);
        }
        if (clients[client_id].nbMessage == 0) {
            sprintf(clients[client_id].nomUtilisateur, "%s", messageBuffer);
        }
        else {
            //concatene le message pour avoir le pseudo + l'id du client en préfixe de son message
            char messageBufferModifie[TAILLE_BUFFER + 27];
            memset(messageBufferModifie, 0, TAILLE_BUFFER + 27);
            sprintf(messageBufferModifie, "[%s | %d] : %s", clients[client_id].nomUtilisateur, client_id, messageBuffer);

            if (afficherMessage == 1) {
                printf("%s", messageBufferModifie);
            }
            
            //Envoie le message reçu à tous les autres clients
            for (int i = 0; i < num_clients; i++) {
                if (i != client_id && clients[i].id != -1) {
                    write(clients[i].socket, messageBufferModifie, strlen(messageBufferModifie));
                }
            }
        }
        clients[client_id].nbMessage++;
    }
}

//Fonction qui gère les commandes
void entrerCommande(int signum) {
    printf("\nEntrez votre commande: ");
    char commande[TAILLE_BUFFER];
    fgets(commande, TAILLE_BUFFER, stdin);
    if (strcmp(commande, "message\n\0") == 0) {
        printf("\nEntrez le message à envoyer à tous les clients : ");
        char messageBuffer[TAILLE_BUFFER];
        fgets(messageBuffer, TAILLE_BUFFER, stdin);
        char messageBufferModifie[TAILLE_BUFFER];
        sprintf(messageBufferModifie, "\033[31m[Serveur] : \033[0m%s", messageBuffer); //met le pseudo du serveur en rouge
        for (int i = 0; i < num_clients; i++) {
            write(clients[i].socket, messageBufferModifie, strlen(messageBufferModifie));
        }
    }
    else if (strcmp(commande, "show\n\0") == 0) {
        if (afficherMessage == 0) {
            afficherMessage = 1;
            printf("Les messages des clients s'afficheront\n");
        }
        else {
            afficherMessage = 0;
            printf("Les messages des clients ne s'afficheront plus\n");
        }
    }
    else if (strcmp(commande, "help\n\0") == 0) {
        printf("Les commandes disponibles :\n\tmessage : Envoyer un message à tous les clients connectés\n");
        printf("\tshow : Affiche ou n'affiche plus les messages des clients\n\thelp : Connaître toutes les commandes\n\texit : Fermer le serveur\n");
    }
    else if (strcmp(commande, "exit\n\0") == 0) {
        printf("Fermeture du serveur\n");
        close(server_socket);
        pthread_mutex_destroy(&mutexClientID);
        exit(EXIT_SUCCESS);
    }
}

//Fonction appelée à chaque démarrage du serveur afin de démarrer la socket sur le port voulu dans le fichier port.txt
//Gère toutes les erreurs pouvant provenir de la lecture du fichier (données incorrectes, accès impossible, fichier inexistant)
void lirePort() {
    int fichier = open(nomFichierConfig, O_RDWR | O_CREAT, 0666); //ouvrir
    if (fichier == -1) {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier de configuration\n");
        exit(EXIT_FAILURE);
    }

    char buffer[7];
    ssize_t checkRead = read(fichier, buffer, 6); //lecture
    if (checkRead == -1) {
        fprintf(stderr, "Erreur lors de la lecture du fichier de configuration\n");
        close(fichier);
        exit(EXIT_FAILURE);
    }
    buffer[checkRead] = '\0';


    port = atoi(buffer);
    if (port < 1 || port > 65535) {
        printf("Le numéro de port lu n'est pas valide. Utilisation du port par défaut 8888.\n");
        if (ftruncate(fichier, 0) == -1) { //supprimer le contenu du fichier
            perror("Erreur lors de la troncature du fichier de configuration");
            close(fichier);
            exit(EXIT_FAILURE);
        }
        lseek(fichier, 0, SEEK_SET);
        char portParDefaut[6];
        snprintf(portParDefaut, sizeof(portParDefaut), "%d", PORT_PAR_DEFAUT);
        int checkWrite = write(fichier, portParDefaut, strlen(portParDefaut)); //ecrire dans le fichier le port par défaut
        if (checkWrite == -1) {
            fprintf(stderr, "Erreur lors de l'écriture du fichier de configuration");
            close(fichier);
            exit(EXIT_FAILURE);
        }
        port = PORT_PAR_DEFAUT;
    }

    close(fichier);
}

//Créer la socket et écoute toutes les nouvelles connexions entrantes
int main() {
    int client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    pthread_mutex_init(&mutexClientID, NULL);

    lirePort();    

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        fprintf(stderr, "Erreur lors de la création du socket\n");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    int checkBind = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (checkBind == -1) {
        fprintf(stderr, "Erreur lors de l'attachement (bind)\n");
        fprintf(stderr, "%s\n", strerror(errno));
        return 1;
    }

    int checkListen = listen(server_socket, NB_MAX_CLIENTS);
    if (checkListen == -1) {
        fprintf(stderr, "Erreur lors de l'écoute\n");
        return 1;
    }

    printf("Attente de connexion sur le port %d ...\n", port);

    signal(SIGINT, entrerCommande);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            fprintf(stderr, "Erreur lors de l'acceptation de la connexion\n");
            continue;
        }

        clients[num_clients].id = num_clients;
        clients[num_clients].socket = client_socket;
        clients[num_clients].address = client_addr;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Connexion du client %d : %s\n", num_clients, client_ip);

        pthread_create(&threads[num_clients], NULL, nouvelleConnexion, (void *)&clients[num_clients].id);

        num_clients++;
    }

    pthread_mutex_destroy(&mutexClientID);
    close(server_socket);

    return 0;
}
