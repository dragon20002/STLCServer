#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include "traffic.h"

#define BUFSIZE 513 //메세지 버퍼크기
#define MTUSIZE 512 //메세지 전송단위
#define NUM_OF_CLI 4
//#define WEB_URL 
#define PORT "50000"
#define WEB_URL "http://13.125.115.128:8080/STLC/upload"
//#define WEB_URL "http://localhost:8080/STLC/upload"
#define FILE_DIR "files"   //"/var/lib/tomcat8/webapps/STLC/resources/files"
#define STREAM_FPS 1

/* TRAFFIC LIGHT */
typedef struct __TrafficLight {
    int clientSock;
    char name[BUFSIZE];
    time_t name_subfix;
    int front, back, side, accident;
    int leds[NUM_OF_LED];
    pthread_mutex_t mutex;
} TrafficLight;

void init_traffic_light(TrafficLight* tl, char* name);
TrafficLight* createTrafficLight(char* name, int sock);
void destroyTrafficLight(TrafficLight* tl);
int getBit(int bit, int bit_id);

/* SOCKET */
void run_server(char* port);
void* listen_clnt(void *arg);
void* clnt_connection(void * arg);
void send_message(int clnt_sock, char * message, int msg_size);
int recv_message(int clnt_sock, char* message);
void broadcast_message(char* message);
int recv_image(TrafficLight* tl, char* message);
void error_handler(char * message);

/* HTTP using CURL */
int upload_file(char* filename);

#endif /* SERVER_H */
