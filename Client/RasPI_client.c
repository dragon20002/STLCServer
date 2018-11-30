#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
//#include <wiringPi.h>

#define NUM_OF_LED 4
#define LED1 4 //GPIO.23
#define LED2 5 //GPIO.24
#define LED3 28 //GPIO.20
#define LED4 29 //GPIO.21

int leds[NUM_OF_LED] = {LED1, LED2, LED3, LED4};

#define BUFSIZE 513 //메세지 버퍼크기
#define MTUSIZE 512 //메세지 전송단위

int sock;
char name[BUFSIZE];
char message[BUFSIZE];
int sleep_time;

void connection(int sock);
void send_message(int sock, char* message, int msg_size);
int recv_message(int sock, char* message);
int send_image(int sock, char* message);
void* sig_handler(int signo);
void error_handler(char * message);

int main(int argc, char **argv) {
    int i;
    struct sockaddr_in serv_addr;

    if (argc != 4) {
        printf("Usage : %s <ip> <port> <name>\n", argv[0]);
        exit(1);
    }

    signal(SIGINT, (void*) sig_handler);

    // init Connection
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
		error_handler("socket() error");
	memset(&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof (serv_addr)) == -1)
        error_handler("connect() error!");

#ifdef PI
    // Set GPIO.x led output
    if (wiringPiSetup() == -1)
		return 0;

    for (i = 0; i < NUM_OF_LED; i++)
        pinMode(leds[i], OUTPUT);
#endif //PI

    // Set picture name
    strcpy(name, argv[3]);
	send_message(sock, name, strlen(name));

	if (recv_message(sock, message) == 0)
		return 0;
	sscanf(message, "%d", &sleep_time);

	// process message
	connection(sock);

    close(sock);
	printf("server connection off\n");

    return 0;
}

void connection(int sock) {
	int i;
	clock_t time;
	int isOn[NUM_OF_LED] = { 0, };

	while (1) {
		float milisec;

		time = clock();
		
#ifdef PI
		// capture
		sprintf(message, "raspistill -o %s.jpg -t 1 -w 416 -h 416 -rot 180", name);
		if (popen(message, "r") == NULL) {
			fprintf(stderr, "fail to popen raspistill\n");
			break;
		}
#endif //PI
		
		// send image
		send_message(sock, "/send_image", 11); //trans seq:1 (start)
		if (recv_message(sock, message) == 0)
			break;
		if (strstr(message, "NOK") != NULL) //trans seq:2
			break;
		send_image(sock, message);
		
		// request led setting
		send_message(sock, "/get_led", 8);
		if (recv_message(sock, message) == 0)
			break;
		sscanf(message, "%d %d %d %d", &isOn[0], &isOn[1], &isOn[2], &isOn[3]);
		for(i = 0; i < NUM_OF_LED; i++) {
#ifdef PI
			digitalWrite(leds[i], isOn[i]);
#else
			printf("%d is %d\n", leds[i], isOn[i]);
#endif //PI
		}

		//milisec = (1 / sleep_time) - (clock() - time) / CLOCKS_PER_SEC;
		//if (milisec > 0)
		//	usleep(milisec * 1000); //usec
		sleep(1);
	}
	/*
	while ((msg_size = recv_message(sock, message)) != 0) {
		if (strstr(message, "/recv_image") != NULL) { //trans seq:1 (start)
			// capture
			sprintf(message, "raspistill -o %s.jpg -t 1 -w 416 -h 416 -vf", name);

#ifdef PI
			if (popen(message, "r") == NULL) {
				send_message(sock, "NOK", 3); //trans seq:2 NOK
				break;
			}
#endif //PI

			// upload image
			if (send_image(sock, message) == -1)
				break;
		}
		else if (strstr(message, "/set_led") != NULL) {
			int i;
			char cmd_line[BUFSIZE];
			int isOn[NUM_OF_LED] = { 0, };
			sscanf(message, "%s %d %d %d %d",
				   	cmd_line, &isOn[0], &isOn[1], &isOn[2], &isOn[3]);
			for(i = 0; i < NUM_OF_LED; i++) {
#ifdef PI
				digitalWrite(leds[i], isOn[i]);
#else
				printf("%d is %d\n", leds[i], isOn[i]);
#endif //PI
			}
			send_message(sock, "OK", 2);
		}

	} //WHILE (msg_size!=0)
	*/
}

void send_message(int sock, char* message, int msg_size) {
	send(sock, message, msg_size, 0);
}

int recv_message(int sock, char* message) {
	int msg_size = recv(sock, message, MTUSIZE, 0);
	message[msg_size] = 0;
	return msg_size;
}

int send_image(int sock, char* message) {
    FILE *fp;
    char filename[BUFSIZE];
	char print_line[BUFSIZE] = "";
    int sendsum = 0;
    int filesize = 0;

	// read the file
    sprintf(filename, "%s.jpg", name);
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("%s File open error\n", filename);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    rewind(fp);

    // send the file info
    sprintf(message, "OK %d", filesize);
    send_message(sock, message, strlen(message)); //trans seq:3

	// recv ok sign
	recv_message(sock, message); //trans seq:4
	if (strstr(message, "NOK")) {
		fclose(fp);
		return -1;
	}

    // send the file fragments
	printf("upload %s ...", filename);
    while (sendsum < filesize) {
		int msg_size = 0, send_size = MTUSIZE;
		int left_size = filesize - sendsum;
		int print_line_len = strlen(print_line);

		if (left_size < MTUSIZE)
			send_size = left_size;

		// read from fp
        msg_size = fread(message, 1, send_size, fp);

		// send to server
		while(1) {
			send_message(sock, message, send_size); //trans seq:5
			if ((msg_size = recv_message(sock, message)) == 0) { //trans seq:6
				fclose(fp);
				return -1;
			}
			else if (strstr(message, "NOK") == NULL) //OK
				break;
		}
		
		sendsum += send_size;
		while (print_line_len--)
			printf("\b");
		sprintf(print_line, "%5d/%5d bytes", sendsum, filesize);
		printf("%s", print_line);
    }
    fclose(fp);
    printf("\n");

	return 0;
}

void* sig_handler(int signo) {
    int i;
    switch (signo) {
        case SIGINT:
            for (i = 0; i < NUM_OF_LED; i++) {
#ifdef PI
				digitalWrite(leds[i], 0);
#else
				printf("%d is %d\n", leds[i], 0);
#endif //PI
			}

#ifdef PI
				digitalWrite(leds[3], 1);
#else
				printf("%d is %d\n", leds[3], 1);
#endif //PI

			send_message(sock, "/exit", 5);
			if (recv_message(sock, message) == 0 || strstr(message, "OK")) {
				close(sock);
				printf("\nconnection off\n");
				exit(0);
			}
        default:
            fprintf(stderr, "%d is unhandled signal...", signo);
    }
}

void error_handler(char * message) {
    perror(message);
	exit(0);
}
