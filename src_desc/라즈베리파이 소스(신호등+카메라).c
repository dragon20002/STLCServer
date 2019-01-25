/****************************************************************************
//---------------- 신호등(Raspberry Pi Client) 소스 ------------------------//
 * 신호등 역할을 하는 Raspberry Pi에서 동작하는 클라이언트 프로그램 소스
 * Raspberry Pi에서 캡쳐한 이미지를 서버로 전송하거나
 * 서버로부터 정보를 받아 LED를 점등/점멸한다.
*****************************************************************************/

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

int leds[NUM_OF_LED] = {LED1, LED2, LED3, LED4}; //LED에 할당된 GPIO 번호

#define BUFSIZE 513 //메세지 버퍼크기
#define MTUSIZE 512 //메세지 전송단위

int sock; //소켓ID
char name[BUFSIZE]; //신호등이름 (EAST, WEST, SOUTH, NORTH)
char message[BUFSIZE]; //서버로 보낼 메시지 내용
int sleep_time; //신호등 동작 delay time

void connection(int sock);
void send_message(int sock, char* message, int msg_size);
int recv_message(int sock, char* message);
int send_image(int sock, char* message);
void* sig_handler(int signo);
void error_handler(char * message);
void set_red();

int main(int argc, char **argv) {
    int i;
    struct sockaddr_in serv_addr;

    if (argc != 4) {
        printf("Usage : %s <ip> <port> <name>\n", argv[0]);
        exit(1);
    }

	// init Connection
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
        pinMode(leds[i], 1);
#endif //PI

    // Set picture name
    strcpy(name, argv[3]);
	send_message(sock, name, strlen(name));

	if (recv_message(sock, message) == 0)
		return 0;
	sscanf(message, "%d", &sleep_time);

	// 소켓 통신 (루프)
	connection(sock);

    close(sock);
	printf("server connection off\n");
	set_red();

    return 0;
}

/**
* 신호등 소켓 통신
*
* @param sock  서버 소켓ID
*/
void connection(int sock) {
	int i;
	clock_t time;
	int isOn[NUM_OF_LED] = { 0, };

	while (1) {
		float milisec;
		FILE* fp;

		time = clock();
		
#ifdef PI
		// capture
		sprintf(message, "raspistill -o %s.jpg -t 1 -w 416 -h 416 -rot 180 -q 10", name);
		fp = popen(message, "r");
		if (fp == NULL) {
			fprintf(stderr, "fail to popen raspistill\n");
			break;
		}
#endif //PI
		
		// 이미지를 서버로 전송
		send_message(sock, "/send_image", 11); //trans seq:1 (start)
		if (recv_message(sock, message) == 0)
			break;
		if (strstr(message, "NOK") != NULL) //trans seq:2
			break;
		send_image(sock, message);
		
		// LED 상태정보를 서버에 요청
		send_message(sock, "/get_led", 8);
		if (recv_message(sock, message) == 0)
			break;
		sscanf(message, "%d %d %d %d", &isOn[0], &isOn[1], &isOn[2], &isOn[3]);

		// LED 점등/점멸 업데이트
		for(i = 0; i < NUM_OF_LED; i++) {
#ifdef PI
			digitalWrite(leds[i], isOn[i]);
#else
			printf("%d is %d\n", leds[i], isOn[i]);
#endif //PI
		}

		pclose(fp);

		sleep(3); //send image every 3 seconds
	}
}

/**
 * 종료 시그널 처리 handler
 * 서버에게 연결 종료 메시지를 보낸다.
 *
 * @param 신호종류
 */
void* sig_handler(int signo) {
    switch (signo) {
        case SIGINT:
			send_message(sock, "/exit", 5);
			if (recv_message(sock, message) == 0 || strstr(message, "OK")) {
				close(sock);

				printf("\nconnection off\n");
				set_red();
				exit(0);
			}
        default:
            fprintf(stderr, "%d is unhandled signal...", signo);
    }
}


//---------------- 신호등 동작, 서버로 메시지, 파일 송신 등의 API -------------------//

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

void error_handler(char * message) {
    perror(message);
	set_red();
	exit(0);
}

void set_red() {
	int i;
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
}

//---------------- ------------------------------------------- -------------------//