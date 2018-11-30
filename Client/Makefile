# PI 사용시 PI 1로 설정하고 RasPI_client.c에서 #include <wiringPi.h> 주석해제
# PI 비사용 시 PI 0으로 설정하고 #include <wiringPi.h> 주석처리
PI=1

ifeq ($(PI), 1)
PI_OPT=-lwiringPi -w -D PI
endif

all: RasPI_client

RasPI_client: RasPI_client.c
	gcc -o RasPI_client RasPI_client.c $(PI_OPT)

clean:
	rm RasPI_client
