
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <curl/curl.h>

/* TRAFFIC LIGHT */
char SERVER_ID[BUFSIZE] = "1";
TrafficLight* tls[NUM_OF_CLI];
TrafficLight east, west, south, north;

void init_traffic_light(TrafficLight* tl, char* name) {
    int i;

    strcpy(tl->name, name);
    tl->name_subfix = 0;
    tl->front = 0;
    tl->back = 0;
    tl->side = 0;
    tl->accident = 0;
    for (i = 0; i < NUM_OF_LED; i++)
        tl->leds[i] = 0;
    pthread_mutex_init(&tl->mutex, NULL);
}

TrafficLight* createTrafficLight(char* name, int sock) {
    int i, j;
    for (i = 0; i < NUM_OF_CLI; i++) {
        if (tls[i] == NULL) {
            if (strcmp(name, east.name) == 0)
                tls[i] = &east;
            else if (strcmp(name, west.name) == 0)
                tls[i] = &west;
            else if (strcmp(name, south.name) == 0)
                tls[i] = &south;
            else if (strcmp(name, north.name) == 0)
                tls[i] = &north;
            else
                return NULL;

            tls[i]->clientSock = sock;

            return tls[i];
        }
    }
    return NULL;
}

void destroyTrafficLight(TrafficLight* tl) {
    int i;
    for (i = 0; i < NUM_OF_CLI; i++) {
        if (tls[i] == tl) {
            tls[i] = NULL;
        }
    }
}

int getBit(int bit, int bit_id) {
    if (bit_id == 0)
        return 0;
    return bit / bit_id % 2;
}

/* SOCKET */
pthread_mutex_t conn_mutex;

void run_server(char* port) {
    int serv_sock;
    struct sockaddr_in serv_addr;
    int sock_opt;
    pthread_t listen_thread;
    char dirname[BUFSIZE];
    
    pthread_mutex_init(&conn_mutex, NULL);

    if ((serv_sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        error_handler("socket() error");
	sock_opt = 1;
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));
	memset(&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET; //주소체계 (AF_INET/AF_UNIX)
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); //32bit IP
    serv_addr.sin_port = htons(atoi(port)); //16bit PORT
    if (bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof (serv_addr)) == -1)
        error_handler("bind() error");
    if (listen(serv_sock, NUM_OF_CLI) == -1)
        error_handler("listen() error");

    init_traffic_light(&east, "east");
    init_traffic_light(&west, "west");
    init_traffic_light(&south, "south");
    init_traffic_light(&north, "north");

    sprintf(dirname, "%s/%s", FILE_DIR, SERVER_ID);
    mkdir(dirname, 0755);
    
    if (pthread_create(&listen_thread, NULL, listen_clnt, (void*) serv_sock))
        error_handler("연결 대기 쓰레드 생성 실패");

    printf("\n\n");
    printf("     _____ _________ __      ______\n");
    printf("    /  ___/__    __ /  /    /  ___/              /   \\\n");
    printf("    \\____    /  /  /  /    /  /                 /_   _\\\n");
    printf("    _____)  /  /  /  /___ /  /____     /|_____    | |    _____|\\\n");
    printf("   /_____  /__/   \\______/\\_______\\      __   \\   | |   /   __  \n");
    printf("                                       \\|  |  |   | |   |  |  |/\n");
    printf("        S  ituation - cognitive                  ________\n");
    printf("                                         _______/  ____  \\__\n");
    printf("        T  raffic                       /        _/    \\_ . \\\n");
    printf("                                        |_______/        \\__|\n");
    printf("        L  ight                         |       \\_      _/  |\n");
    printf("                                        |         \\____/    |\n");
    printf("        C  ontroller                    \\___________________/\n\n");
    printf("                            << Team KKCC >>\n\n");
    
    printf("[SERVER] STLC 시작. PORT %s\n", port);

    return;
}

void *listen_clnt(void *arg) {
    pthread_t thread;
    struct sockaddr_in clnt_addr;
    int clnt_addr_size;
    int serv_sock = (int) arg;
    int clnt_sock;

    while (1) {
        clnt_addr_size = sizeof (clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *) &clnt_addr,
                &clnt_addr_size);
        pthread_create(&thread, NULL, clnt_connection, (void*) clnt_sock);
        printf("[SERVER] IP %s와 연결을 시도합니다.\n", inet_ntoa(clnt_addr.sin_addr));
    }
}

void *clnt_connection(void *arg) {
    char message[BUFSIZE];
    int msg_size;
    TrafficLight* tl;

    // 쓰레드 이름 설정
    if (recv_message((int) arg, message) == 0)
        return 0;

    pthread_mutex_lock(&conn_mutex);
    tl = createTrafficLight(message, (int) arg);
    pthread_mutex_unlock(&conn_mutex);

    if (tl == NULL) {
        close((int) arg);
        printf("[SERVER] 연결을 실패했습니다.\n");
        return;
    }

    printf("[SERVER] \"%s\" 신호등이 연결되었습니다.\n", tl->name);

    // PI sleep time 설정
    sprintf(message, "%d", STREAM_FPS);
    send_message(tl->clientSock, message, strlen(message));

    // 쓰레드 동작
    while ((msg_size = recv_message(tl->clientSock, message)) != 0) {
        if (strstr(message, "/send_image") != NULL) { //trans seq:1 (start)
            send_message(tl->clientSock, "OK", 2); //trans seq:2
            if (recv_image(tl, message) == -1) {
                fprintf(stderr, "recv image err\n");
                break;
            }
        } else if (strstr(message, "/get_led") != NULL) {
            sprintf(message, "%d %d %d %d",
                    tl->leds[0], tl->leds[1], tl->leds[2], tl->leds[3]);
            send_message(tl->clientSock, message, 16);
        } else if (strstr(message, "/exit") != NULL) {
            send_message(tl->clientSock, "OK", 2);
            break;
        }
    }

    pthread_mutex_lock(&conn_mutex);
    close(tl->clientSock);
    printf("[SERVER] \"%s\" 신호등의 연결이 끊어졌습니다.\n", tl->name);
    destroyTrafficLight(tl);
    pthread_mutex_unlock(&conn_mutex);

    return 0;
}

void send_message(int clnt_sock, char * message, int msg_size) {
    send(clnt_sock, message, msg_size, 0);
}

int recv_message(int clnt_sock, char* message) {
    int msg_size;
    msg_size = recv(clnt_sock, message, MTUSIZE, 0);
    message[msg_size] = 0;

    return msg_size;
}

void broadcast_message(char * message) {
    int i;
    for (i = 0; i < NUM_OF_CLI; i++)
        if (tls[i] != NULL)
            send_message(tls[i]->clientSock, message, strlen(message));
}

int recv_image(TrafficLight* tl, char* message) {
    FILE *fp;
    char cmd_line[BUFSIZE];
    int recvsum = 0;
    char filename[BUFSIZE];
    int filesize = 0;

    // recv the file info
    if (recv_message(tl->clientSock, message) == 0) //trans seq:3
        return -1;
    sscanf(message, "%s %d", cmd_line, &filesize);
    if (strstr(cmd_line, "NOK")) {
        printf("[%s] 이미지 전송 실패\n", tl->name);
        return -1;
    }

    // create a file
    pthread_mutex_lock(&tl->mutex);
    tl->name_subfix = time(NULL);
    sprintf(filename, "%s/%s/%s%d.jpg", FILE_DIR, SERVER_ID, tl->name, tl->name_subfix);
    if ((fp = fopen(filename, "wb")) == NULL) {
        pthread_mutex_unlock(&tl->mutex);
        printf("[%s] 파일 쓰기 실패\n", tl->name);
        return -1;
    }

    // send ok sign
    send_message(tl->clientSock, "OK", 2); //trans seq:4

    // recv the file fragments
    while (recvsum < filesize) {
        int msg_size = 0, recv_size = MTUSIZE;
        int left_size = filesize - recvsum;

        if (left_size < MTUSIZE)
            recv_size = left_size;

        // recv from client
        while (1) {
            msg_size = recv_message(tl->clientSock, message); //trans seq:5
            if (msg_size == 0) {
                fclose(fp);
                pthread_mutex_unlock(&tl->mutex);
                return -1;
            }
            if (msg_size == recv_size) {
                send_message(tl->clientSock, "OK", 2); //trans seq:6 OK
                break;
            }
            send_message(tl->clientSock, "NOK", 3); //trans seq:6 NOK
        }

        // write to fp
        msg_size = fwrite(message, 1, msg_size, fp);

        recvsum += recv_size;
    }
    fclose(fp);
    printf("[%s] %s.jpg 다운로드 완료 (%5d/%5d bytes)\n", tl->name, tl->name, recvsum, filesize);
    pthread_mutex_unlock(&tl->mutex);

    return 0;
}

void error_handler(char * message) {
    perror(message);
    exit(0);
}

/* HTTP using CURL */
int upload_file(char* filename) {
    CURL* curl;
    CURLcode res;
    struct curl_httppost* formpost = NULL;
    struct curl_httppost* lastptr = NULL;
    struct curl_slist* headerList = NULL;
    static const char buf[] = "Expect:";
    char url[BUFSIZE];
    
    curl_global_init(CURL_GLOBAL_ALL);

    // post body
    curl_formadd(&formpost, &lastptr,
            CURLFORM_COPYNAME, "file",
            CURLFORM_FILE, filename,
            CURLFORM_END);
    
    if (!(curl = curl_easy_init()))
        return -1;

    // add header
    headerList = curl_slist_append(headerList, buf);
    
    // setopt
    sprintf(url, "%s/%s", WEB_URL, SERVER_ID);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[DETECTOR] 파일 업로드 실패 (%s)\n", curl_easy_strerror(res));
        return -1;
    }
    curl_easy_cleanup(curl);
    curl_formfree(formpost);
    curl_slist_free_all(headerList);
    
    return 0;
}