/****************************************************************************
//---------------- 서버 신호등 신호관리 및 이미지 분석 소스 ------------------------//
 * 교차로 하나를 관리하는 서버의 신호관리 관련 소스.
 * cnn 이미지 분석으로 교차로를 지나가는 차량 대수를 파악하고,
 * 신호 주기를 조절하거나 사고여부를 판단한다.
 * 신호 주기 조절 알고리즘에는 주간 모드, 야간 모드가 있다.
*****************************************************************************/

extern char SERVER_ID[BUFSIZE]; //시스템이 관측하고 있는 교차로의 고유ID
extern TrafficLight east, west, south, north; //신호등
extern TrafficLight* tls[NUM_OF_CLI]; //신호등에 대한 포인터 배열
extern pthread_mutex_t conn_mutex; //연결-연결해제에 대한 뮤텍스

/**
 * 이미지 분석결과를 TrafficLight 객체에 저장
 *
 * @param im       분석된 이미지
 * @param tl       대상 신호등
 * @param num      이미지 픽셀수
 * @param thresh   이미지 분석 정확도에 대한 threshold
 * @param boxes    이미지 분석결과를 표시하는 사각형
 * @param probs    정확도
 * @param names    분석대상의 이름 목록 (front, back, side, accident-vehicle)
 * @param alphabet cnn 분석에 사용되는 filter
 * @param classes  클래스 수
 */
void get_detections(image im, TrafficLight* tl, int num, float thresh, box *boxes, float **probs, char **names, image **alphabet, int classes) {
    int i;
    int cnt = 0;

    for (i = 0; i < num; ++i) {
        int class_id = max_index(probs[i], classes);
        float prob = probs[i][class_id];
        if (prob > thresh) {
            // 인식된 객체를 둘러싼 사각형 위치 계산 및 이미지에 그리기
            cnt++;
            int width = im.h * .012;

            int offset = class_id * 123457 % classes;
            float red = get_color(2, offset, classes);
            float green = get_color(1, offset, classes);
            float blue = get_color(0, offset, classes);
            float rgb[3];

            rgb[0] = red;
            rgb[1] = green;
            rgb[2] = blue;
            box b = boxes[i];

            int left = (b.x - b.w / 2.) * im.w;
            int right = (b.x + b.w / 2.) * im.w;
            int top = (b.y - b.h / 2.) * im.h;
            int bot = (b.y + b.h / 2.) * im.h;

            if (left < 0) left = 0;
            if (right > im.w - 1) right = im.w - 1;
            if (top < 0) top = 0;
            if (bot > im.h - 1) bot = im.h - 1;

            //printf("  %s: %.0f%% (%d, %d)\n", names[class_id], prob*100, (left+right)/2, (top+bot)/2);

            draw_box_width(im, left, top, right, bot, width, red, green, blue);
            if (alphabet) {
                image label = get_label(alphabet, names[class_id], (im.h * .03) / 10);
                draw_label(im, top + width, left, label, rgb);
            }

            // 인식된 객체의 클래스 수 카운트
            if (strcmp(names[class_id], "car-side") == 0)
                tl->side++;
            else if (strcmp(names[class_id], "car-back") == 0)
                tl->back++;
            else if (strcmp(names[class_id], "car-front") == 0)
                tl->front++;
            else
                tl->accident++;

        }
    }
    //printf("  front %d\tback %d\tside %d\n", tl->front, tl->back, tl->side);
    //printf("  total %d\n", cnt);
}


/**
 * YOLO로 이미지를 분석하여 이미지 내의 차량 대수를 센다.
 *
 * @param tl       신호등 1개
 * @param thresh   이미지 분석 정확도에 대한 threshold
 * @param names    분석대상의 이름 목록 (front, back, side, accident-vehicle)
 * @param alphabet cnn 분석에 사용되는 filter
 * @param net      weight 파일로 로드된 cnn network
 */
void get_detect_result(TrafficLight* tl, float thresh, char** names,
        image** alphabet, network net) {
    int j;
    clock_t time;
    char buff[256];
    char *input = buff;
    float nms = .4;

    // 이미지 이름 생성
    sprintf(input, "%s/%s/%s%d.jpg", FILE_DIR, SERVER_ID, tl->name,
            tl->name_subfix);
    if (input[strlen(input) - 1] == 0x0d)
        input[strlen(input) - 1] = 0;

    // 이미지 전처리(색, 크기조절)
    image im = load_image_color(input, 0, 0);
    image sized = resize_image(im, net.w, net.h);
    layer l = net.layers[net.n - 1];

    // 이미지 분석
    box *boxes = calloc(l.w * l.h * l.n, sizeof (box));
    float **probs = calloc(l.w * l.h * l.n, sizeof (float *));
    for (j = 0; j < l.w * l.h * l.n; ++j)
        probs[j] = calloc(l.classes, sizeof (float *));

    float *X = sized.data;
    time = clock();
    network_predict(net, X);
    printf("[DETECT] image \'%s\' predicted in %f seconds.\n", tl->name,
            sec(clock() - time));

    // 분석 결과를 TrafficLight 인스턴스에 저장
    get_region_boxes(l, 1, 1, thresh, probs, boxes, 0, 0);
    if (nms)
        do_nms_sort(boxes, probs, l.w * l.h * l.n, l.classes, nms);
    get_detections(im, tl, l.w * l.h * l.n, thresh, boxes, probs, names,
            alphabet, l.classes);

    sprintf(input, "%s/%s/%s%d_result", FILE_DIR, SERVER_ID, tl->name,
            tl->name_subfix);
    save_image(im, input);

    free_image(im);
    free_image(sized);
    free(boxes);
    free_ptrs((void **) probs, l.w * l.h * l.n);
}

/**
* 이미지 분석결과를 웹서버로 전송한다.
*
* @param tl     신호등 1개
* @return       성공(0), 실패(-1)
*/
int send_analyzed_image_to_web_server(TrafficLight* tl) {
    FILE* fp = NULL;
    char filename[BUFSIZE];
    char content[BUFSIZE];

    // 웹서버로 분석된 이미지 전송
    sprintf(filename, "%s/%s/%s%d_result.jpg", FILE_DIR, SERVER_ID, tl->name,
            tl->name_subfix);
    if (isImage(filename) == 0) {
        printf("[%s] Can not load image\n", tl->name);
        return -1;
    }
    if (upload_file(filename) == -1)
        return -1;

	// 웹서버로 분석 결과 txt 전송
    sprintf(filename, "%s/%s/%s.txt", FILE_DIR, SERVER_ID, tl->name);
    if ((fp = fopen(filename, "w")) == NULL) {
        printf("[%s] file open error\n", tl->name);
        return -1;
    }

    sprintf(content, "%d %d %d %d %d %d %d %d", tl->name_subfix, tl->front,
            tl->back, tl->side, tl->leds[0], tl->leds[1], tl->leds[2],
            tl->leds[3]);
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);

    if (upload_file(filename) == -1)
        return -1;

    return 0;
}

/**
* 신호등 알고리즘 결과를 웹서버로 전송한다.
*
* @param accident     사고난 경우 1, 아닌 경우 0
* @param remain_time  다음 신호까지 남은 시간(seconds)
* @param total_time   최근 신호에서 다음 신호까지 유지되는 시간(seconds)
* @return             성공(0), 실패(-1)
*/
int send_decision_results_to_web_server(int accident, int remain_time, int total_time) {
    FILE* fp = NULL;
    char filename[BUFSIZE];
    char content[BUFSIZE];

	// 웹서버로 알고리즘 결과 txt 전송
    sprintf(filename, "%s/%s/global.txt", FILE_DIR, SERVER_ID);
    if ((fp = fopen(filename, "w")) == NULL) {
        printf("[DETECT] global file open error\n");
        return -1;
    }

    sprintf(content, "%d %d %d", accident, remain_time, total_time);
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);

    if (upload_file(filename) == -1)
        return -1;

    return 0;
}

int isImage(char* filename) { //정상 이미지인지 확인
    int flag = -1;
    IplImage* src = 0;
    src = cvLoadImage(filename, flag);
    return src != 0;
}

time_t traffic_time = -1; //최근에 신호가 바뀐 시점의 system milliseconds
time_t orange_time = -1; //노란불의 남은 시간
int cur_light = 0; //신호등 주기상태 (0 ~ 4)
int next_light = 0; //신호등 이전 주기상태 (0 ~ 4)
int calc_time = 0; //신호등 알고리즘으로 계산된 추가/감소 시간 (-5 ~ +10)

/**
* 주간모드의 신호등 알고리즘
* 남북 직진 -> 노란불 -> 남북 좌회전 -> 노란불 -> 동서 좌회전 -> 노란불 -> 동서 직진 신호 -> 노란불
*/
void traffic_normal_mode(int default_time, int default_orange) {
    int i;
    int accident, remain_time, total_time;
    
    // 신호 남은 시간이 0이 되면 다음 신호를 계산
    if ((time(NULL) - traffic_time > default_time + calc_time) || (traffic_time == -1)) {
        int EW_Max; //동-서로 이동하는 차량 수
        int NS_Max; //남-북으로 이동하는 차량 수

        // 차량 유동량 계산
        EW_Max = east.front + west.front;
        NS_Max = north.front + south.front;

		// 신호등 정보 저장
        for (i = 0; i < NUM_OF_CLI; i++)
            if (tls[i] != NULL)
                send_analyzed_image_to_web_server(tls[i]);

		// 다음 신호 결정
        if ((time(NULL) - orange_time > default_orange)
                || (orange_time == -1)) {

            switch (cur_light) {
                case 0:
                    //North South Green
                    //East West Red
                    calc_time = 3 * (NS_Max - EW_Max);

                    green_light(&north);
                    green_light(&south);
                    red_light(&east);
                    red_light(&west);

					// 다음 신호를 저장하고 노란불로 전환
                    // 노란불 신호 시간이 지나면 저장되어 있던 다음 신호로 바뀐다.
                    cur_light++;
                    next_light = cur_light;
                    cur_light = 4;
                    break;

                case 1:
                    //North South Left Green
                    //East West Red
                    calc_time = 3 * (NS_Max - EW_Max);

                    green_left_light(&north);
                    green_left_light(&south);
                    red_light(&east);
                    red_light(&west);

                    cur_light++;
                    next_light = cur_light;
                    cur_light = 4;
                    break;

                case 2:
                    //North South Red
                    //East West Green
                    calc_time = 3 * (EW_Max - NS_Max);

                    red_light(&north);
                    red_light(&south);
                    green_light(&east);
                    green_light(&west);

                    cur_light++;
                    next_light = cur_light;
                    cur_light = 4;
                    break;
                case 3:
                    //North South Red
                    //East West Left Green
                    calc_time = 3 * (EW_Max - NS_Max);

                    red_light(&north);
                    red_light(&south);
                    green_left_light(&east);
                    green_left_light(&west);

                    cur_light = 0;
                    next_light = cur_light;
                    cur_light = 4;
                    break;
                case 4:
                    // orange
                    orange_light(&east);
                    orange_light(&west);
                    orange_light(&south);
                    orange_light(&north);

					// 노란불 시간 초기화 및 다음 신호로 전환
                    orange_time = time(NULL);
                    cur_light = next_light;
                    break;
                default:
                    break;
            }
        }

		// 신호 최소/최댓값 제한
        if (calc_time < -5)
            calc_time = -5;
        else if (calc_time > 10)
            calc_time = 10;

        // 신호등 LED 점등여부를 포함한 정보 저장
        for (i = 0; i < NUM_OF_CLI; i++)
            if (tls[i] != NULL)
                send_analyzed_image_to_web_server(tls[i]);

        // 사고 여부 결정
        accident = 0;
        for (i = 0; i < NUM_OF_CLI; i++) {
            if (tls[i] != NULL && tls[i]->accident == 1) {
                accident = 1;
                break;
            }
        }

		// 현재 노란불이면 현재 시점(다음 신호 시작 시간)을 갱신
        if (cur_light == 4)
            traffic_time = time(NULL);
    }

	// 다음 신호 유지 시간 결정
    if (cur_light == 4) {
        remain_time = default_time + calc_time - time(NULL) + traffic_time;
        total_time = default_time + calc_time;
    } else {
        remain_time = default_orange - (time(NULL) - orange_time);
        total_time = default_orange;
    }

    // 알고리즘 결과 저장
    send_decision_results_to_web_server(accident, remain_time, total_time);
    printf("[DETECT] 다음 신호까지 %d/%d 초 남았습니다.\n", remain_time, total_time);
}

/**
* 야간모드의 신호등 알고리즘
* 남북 직진 -> (동서 차량 감지 시) -> 노란불 -> 동서 직진 -> 노란불 -> 남북 직진
*/
void traffic_night_mode(int default_time, int default_orange) {
    int i;
    int accident, remain_time, total_time;

	// 신호 남은 시간이 0이 되면 다음 신호를 계산    
    if ((time(NULL) - traffic_time > default_time) || (traffic_time == -1)) {
        int EW_Max, NS_Max;
    
		// 차량 유동량 계산
        EW_Max = east.front + west.front;
        NS_Max = north.front + south.front;

		// 신호등 정보 저장
        for (i = 0; i < NUM_OF_CLI; i++)
            if (tls[i] != NULL)
                send_analyzed_image_to_web_server(tls[i]);

		// 다음 신호 결정
        if ((time(NULL) - orange_time > default_orange) || (orange_time == -1)) {

            switch (cur_light) {
                case 0:
                    //North South Green
                    //East West Red

                    green_light(&north);
                    green_light(&south);
                    red_light(&east);
                    red_light(&west);

					// 동서로 이동하는 차량이 많아지면
					// 다음 신호를 저장하고 노란불로 전환
                    if (EW_Max >= 2) {
                        cur_light++;
                        next_light = cur_light;
                        cur_light = 2;
                    }
                    break;

                case 1:
                    //North South Red
                    //East West Green
                    //없는쪽 신호시간
                    default_time = 10;

                    red_light(&north);
                    red_light(&south);
                    green_light(&east);
                    green_light(&west);

					// 남북으로 이동하는 신호로 복귀
                    cur_light = 0;
                    next_light = cur_light;
                    cur_light = 2;

                    break;

                case 2:
                    // orange
                    orange_light(&east);
                    orange_light(&west);
                    orange_light(&south);
                    orange_light(&north);

					// 노란불 시간 초기화 및 다음 신호로 전환
                    orange_time = time(NULL);
                    cur_light = next_light;

                    if (default_time > 0)
                        default_time = 0;
                    break;
                default:
                    break;
            }
        }

        // 신호 최소/최댓값 제한
        if (calc_time < 0)
            calc_time = 0;
        else if (calc_time > 40)
            calc_time = 40;

		// 신호등 LED 점등여부를 포함한 정보 저장
        for (i = 0; i < NUM_OF_CLI; i++)
            if (tls[i] != NULL)
                send_analyzed_image_to_web_server(tls[i]);

        // 사고 여부 결정
        accident = 0;
        for (i = 0; i < NUM_OF_CLI; i++) {
            if (tls[i] != NULL && tls[i]->accident == 1) {
                accident = 1;
                break;
            }
        }

		// 현재 시점(다음 신호 시작 시간)을 갱신
        traffic_time = time(NULL);
    }

	// 다음 신호 유지 시간 결정
    if (default_time - (time(NULL) - traffic_time) < 0) {
        remain_time = 0;
        total_time = default_time;
    } else {
        remain_time = default_time - time(NULL) + traffic_time;
        total_time = default_time;
    }

	// 알고리즘 결과 저장
    printf("[DETECT] 다음 신호까지 %d/%d 초 남았습니다.\n", remain_time, total_time);
    send_decision_results_to_web_server(accident, remain_time, total_time);
}

/**
* 서버를 실행하고 주/야간 모드 실행을 수행하는 Main 코드
*
* @param datacfg     데이터 설정파일
* @param cfgfile     이미지 분석 설정파일
* @param weightfile  cnn 이미지 분석 weight 파일
* @param thresh      이미지 분석 정확도에 대한 threshold
*/
void test_detector(char *datacfg, char *cfgfile, char *weightfile, float thresh) {
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    char **names = get_labels(name_list);
    image **alphabet = load_alphabet();
    network net = parse_network_cfg_custom(cfgfile, 1);

    // weight 파일로부터 이미지 객체인식을 위한 cnn 네트워크 로드
    if (weightfile) {
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    srand(2222222);

    int traffic_mode = 0; //0: 주간모드, 1: 야간모드
    
    // 50000번 포트로 서버 실행
    run_server(PORT);

    while (1) {
        int i;
        time_t current_time;

		// 키 입력이 있으면 주야간 모드 전환
        if (kbhit()) {
            if (getchar() == '1') {
                traffic_mode = !traffic_mode;
                if (traffic_mode == 0) {
                    printf("주간모드로 변경되었습니다.\n");
                } else {
                    printf("야간모드로 변경되었습니다.\n");
                }

                // 모드 변경 시 초기화
                traffic_time = -1;
                orange_time = -1;
                cur_light = 0;
                next_light = 0;
                calc_time = 0;
                
                sleep(2);
            }
        }

        // 1. 클라이언트로부터 받은 이미지 분석
        current_time = time(NULL);
        for (i = 0; i < NUM_OF_CLI; i++) {
            
            // 신호등과 서버 간에 연결/연결해제 코드와 상호배제
            pthread_mutex_lock(&conn_mutex);

            // 신호등 상태 초기화
            if (tls[i]) {
                tls[i]->front = 0;
                tls[i]->back = 0;
                tls[i]->side = 0;
                tls[i]->accident = 0;

                // 클라이언트와 소켓통신 코드와 상호배제
                pthread_mutex_lock(&tls[i]->mutex);

                // 신호등으로부터 받은 이미지 분석
                get_detect_result(tls[i], thresh, names, alphabet, net);

                // 분석 결과 저장
                send_analyzed_image_to_web_server(tls[i]);

                pthread_mutex_unlock(&tls[i]->mutex);
            }
            pthread_mutex_unlock(&conn_mutex);
        }
        printf("[DETECT] 이미지 분석 완료 (%.2f 초)\n", time(NULL) - current_time);

        // 2. 분석결과를 토대로 Traffic Algorithm 실행
        current_time = time(NULL);
        if (traffic_mode == 0) //주간
            traffic_normal_mode(20, 5);
        else //야간
            traffic_night_mode(0, 3);

        printf("[DETECT] 신호등 신호 전달 완료 (%.2f 초)\n", time(NULL) - current_time);
        printf("------------------------------------------------\n");

        usleep(300);
    }
}