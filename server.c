#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>

#define MAX_BULLETS 5
#define MAX_PLAYERS 3
#define BOARD_WIDTH 800
#define BOARD_HEIGTH 600

//ENUMS
typedef enum{
   BLUE = 'b',
   RED = 'r',
   GREEN = 'g',
   YELLOW = 'y',
   PINK = 'p'
} Colour;


//STRUCTS
typedef struct{
    float x;
    float y;
} Point;

typedef struct{
    Point position;
    short isExpired;
}Bullet;

typedef struct{
    Point position;
    int turnover_deg;
    Point bullets[5];
    Colour colour;
    short isAlive;
} Tank;

typedef struct{
    sem_t game_semaphore;
    Tank tanks[MAX_PLAYERS];
    float board_width;
    float board_height;
} Game;

typedef struct
{
    int client_id;
    Game* game;
} Incoming_client_info;

typedef struct{
    int socket_fd;
    struct sockaddr_in client_address;
    short is_active;
} Client;

//HELPER FUNCTIONS
void init_tank(Tank* tank, Colour color,float x, float y);
void append_player_tank(int player_id, Game* game, Colour color,float x, float y);
Point random_position();
void delete_player_tank(int palyer_id, Game* game);

void init_game(Game* game){
    if(sem_init(&game->game_semaphore,0,1) != 0){
        perror("Game sempahore initialization failed\n");
        exit(EXIT_FAILURE);
    }
    game->board_height = (float)BOARD_WIDTH;
    game->board_width = (float)BOARD_HEIGTH;
    
    for(int i = 0; i < MAX_PLAYERS; i++){
        game->tanks[i].isAlive = 0;
    }
}

Point random_position(){
    Point point;
    point.x = (float)rand() / ((float)RAND_MAX / ((float)BOARD_WIDTH));
    point.y = (float)rand() / ((float)RAND_MAX / ((float)BOARD_HEIGTH));
    return point;
}

void init_tank(Tank* tank, Colour color,float x, float y){
    tank->turnover_deg = 0;
    tank->position.x = x;
    tank->position.y = y;
    tank->colour = color;
    tank->isAlive = 1;
}

void append_player_tank(int player_id, Game* game, Colour color,float x, float y){
    sem_wait(&game->game_semaphore);
    init_tank(&game->tanks[player_id], color, x, y);
    sem_post(&game->game_semaphore);
}

void delete_player_tank(int palyer_id, Game* game){
    sem_wait(&game->game_semaphore);
    game->tanks[palyer_id].isAlive = 0;
    sem_post(&game->game_semaphore);
}


//GLOBALS
sem_t client_semaphore;
Client clients[MAX_PLAYERS];
int active_players = 0;




//MAIN LOGIC FUNCTIONS
void client_handler(void* args){
    Incoming_client_info client_info = *((Incoming_client_info*) args);
    int client_id = client_info.client_id;
    Game* game = client_info.game;
    Client client;
    char* buffer[1];
    Point position = random_position();

    sem_wait(&client_semaphore);
    client = clients[client_id];
    sem_post(&client_semaphore);
    
    recv(client.socket_fd, buffer, 1, 0);
    append_player_tank(client_id, game, buffer[0],position.x, position.y);
    pritnf("New player add with colour of %c", buffer[0]);
    memset(buffer, 0, 1);

    float data[2] = {position.x, position.y};
    send(client.socket_fd, data, sizeof(data), 0);
    while(1){
        int bytes_recived = recv(client.socket_fd, buffer, 1, 0);
        if(bytes_recived <= 0){
            delete_player_tank(client_id, game);
            sem_wait(&client_semaphore);
            close(client.socket_fd);
            clients[client_id].is_active = 0;
            active_players--;
            sem_post(&client_semaphore);
            pthread_exit(NULL);
        }

    }

}

void game_handler(void* args){

}



int main(){
    srand(time(NULL));

    //Semaphore initialization
    if(sem_init(&client_semaphore, 0, 1) != 0){
        perror("Semaphore initialization failed\n");
        exit(EXIT_FAILURE);
    }

    //Server socket initialization
    int server_scoket_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if(server_scoket_fd == -1){
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(5001);

    if (bind(server_scoket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("Error binding address to socket");
        exit(EXIT_FAILURE);
    }

    //game initialization
    Game game;
    init_game(&game);
    for(int i = 0; i < MAX_PLAYERS; i++){
        clients[i].is_active = 0;    
    }



    if (listen(server_scoket_fd, MAX_PLAYERS) == -1) {
        perror("Error listening on socket");
        exit(EXIT_FAILURE);
    }

    printf("Server running at address: %s:%d\n", inet_ntoa(server_address.sin_addr), ntohs(server_address.sin_port));

    //MAIN-THREAD - new clients listener
    while(1){
        struct sockaddr_in client_address;
        socklen_t client_addr_len = sizeof(client_address);
        int incoming_socket_fd = accept(server_scoket_fd, (struct sockaddr*)&client_address, &client_addr_len);
        
        if (incoming_socket_fd == -1) {
            perror("Error accepting connection");
            continue;
        }
            
        if(active_players < MAX_PLAYERS){
            sem_wait(&client_semaphore);
            int client_id;
            for(int i = 0; i < MAX_PLAYERS; i++){
                if(clients[i].is_active == 0){
                    clients[i].socket_fd = incoming_socket_fd;
                    clients[i].client_address = client_address;
                    clients[i].is_active = 1;
                    client_id = i;
                    break;  
                }
            }
            Incoming_client_info* args = malloc(sizeof(Incoming_client_info));
            args->client_id = client_id;
            args->game = &game;
            pthread_t client_thread;

            if(pthread_create(&client_thread, NULL, client_handler, args) != 0){
                perror("Error while creating a client thread");
                close(incoming_socket_fd);
            }

            printf("New client connected, address: %s and id: %d \n", inet_ntoa(client_address.sin_addr), client_id);
            active_players++;
            sem_post(&client_semaphore);
        }else{
            char* connection_refused_info = "Connection refused - maxiimum number of active players reached";
            //send(incoming_socket_fd, connection_refused_info, strlen(connection_refused_info), 0);
            printf("%s", connection_refused_info);
            close(incoming_socket_fd);
        }
    }

}