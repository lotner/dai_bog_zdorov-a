#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>

// Структура для отправки задания серверу
typedef struct {
    int *array;
    int left;
    int right;
} task_data_t;
int array_size_global;
// Аргумент для треда работающего с сервером
typedef struct {
    int left;   // Левая граница
    int right;  // Правая граница
    int *array;
    struct sockaddr_in
        *server;  // Структура с информацией для подключения к серверу
                  // Куда записать результат
} thread_args_t;
// Функция треда работающего с сервером
void *send_thread(void *arg) {
    thread_args_t *task_data = (thread_args_t *)arg;
    int servsock = socket(PF_INET, SOCK_STREAM, 0);
    if (servsock < 0) {
        perror("Create new socket to server");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in listenaddr;
    listenaddr.sin_family = AF_INET;
    listenaddr.sin_addr.s_addr = INADDR_ANY;
    listenaddr.sin_port = 0;

    if (bind(servsock, (struct sockaddr *)&listenaddr, sizeof(listenaddr)) <
        0) {
        perror("Can't create listen socket");
        exit(EXIT_FAILURE);
    }
    socklen_t servaddrlen = sizeof(struct sockaddr_in);
    if (connect(servsock, (struct sockaddr *)task_data->server, servaddrlen) <
        0) {
        perror("Connect to server failed!");
        exit(EXIT_FAILURE);
    }
    if (send(servsock, &array_size_global, sizeof(int), 0) < 0) {
        perror("Sending array size to server failed");
        exit(EXIT_FAILURE);
    }
    if (send(servsock, &task_data->left, sizeof(int), 0) < 0) {
        perror("Sending left to server failed");
        exit(EXIT_FAILURE);
    }
    if (send(servsock, &task_data->right, sizeof(int), 0) < 0) {
        perror("Sending right to server failed");
        exit(EXIT_FAILURE);
    }
    task_data_t senddata;
    sleep(1);
    senddata.array = task_data->array;
    printf("sizeof(senddata.array) = %d\n", sizeof(senddata.array));
    for (int i = 0; i < array_size_global; ++i) {
        printf("%d\n", senddata.array[i]);
    }
    int sent_bytes =
        send(servsock, senddata.array, sizeof(int) * array_size_global, 0);
    printf("sent_bytes = %d\n", sent_bytes);
    sleep(0.5);
    int *buf = malloc(sizeof(int) * array_size_global);
    int recv_byte = recv(servsock, buf, sizeof(int) * array_size_global, 0);
    printf("recv_byte = %d\n", recv_byte);
    if (recv_byte == 0) {
        fprintf(stderr, "Server %s on port %d die!\nCancel calculate, on all",
                inet_ntoa(task_data->server->sin_addr),
                ntohs(task_data->server->sin_port));
        exit(EXIT_FAILURE);
    }
    printf("sorted_array:\n");
    for (int i = 0; i < array_size_global; ++i) {
        printf("%d ", buf[i]);
    }
    fprintf(stdout, "Server %s on port %d finish!\n",
            inet_ntoa(task_data->server->sin_addr),
            ntohs(task_data->server->sin_port));
    return NULL;
}

int main(int argc, char **argv) {
    int maxservu;
    int array_size;

    printf("Enter array size: ");
    scanf("%d", &array_size);
    int *array = malloc(sizeof(int) * array_size);
    printf("Enter array: ");
    for (int i = 0; i < array_size; i++) {
        scanf("%d", &array[i]);
    }
    array_size_global = array_size;
    printf("Enter max servers: ");
    scanf("%d", &maxservu);
    // Создаем сокет для работы с broadcast
    int sockbrcast = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockbrcast == -1) {
        perror("Create broadcast socket failed");
        exit(EXIT_FAILURE);
    }

    // Создаем структуру для приема ответов на broadcast
    int port_rcv = 0;
    struct sockaddr_in addrbrcast_rcv;
    bzero(&addrbrcast_rcv, sizeof(addrbrcast_rcv));
    addrbrcast_rcv.sin_family = AF_INET;
    addrbrcast_rcv.sin_addr.s_addr = htonl(INADDR_ANY);
    addrbrcast_rcv.sin_port = 0;  //htons(port_rcv);
    // Биндим её
    if (bind(sockbrcast, (struct sockaddr *)&addrbrcast_rcv,
             sizeof(addrbrcast_rcv)) < 0) {
        perror("Bind broadcast socket failed");
        close(sockbrcast);
        exit(EXIT_FAILURE);
    }

    // Структура для отправки broadcast
    int port_snd = 38199;
    struct sockaddr_in addrbrcast_snd;
    bzero(&addrbrcast_snd, sizeof(addrbrcast_snd));
    addrbrcast_snd.sin_family = AF_INET;
    addrbrcast_snd.sin_port = htons(port_snd);
    addrbrcast_snd.sin_addr.s_addr = htonl(0xffffffff);

    // Разрешаем broadcast на сокете
    int access = 1;
    if (setsockopt(sockbrcast, SOL_SOCKET, SO_BROADCAST, (const void *)&access,
                   sizeof(access)) < 0) {
        perror("Can't accept broadcast option at socket to send");
        close(sockbrcast);
        exit(EXIT_FAILURE);
    }
    int msgsize = sizeof(char) * 18;
    void *hellomesg = malloc(msgsize);
    bzero(hellomesg, msgsize);
    strcpy(hellomesg, "Hello Integral");
    // Посылаем broadcast
    if (sendto(sockbrcast, hellomesg, msgsize, 0,
               (struct sockaddr *)&addrbrcast_snd,
               sizeof(addrbrcast_snd)) < 0) {
        perror("Sending broadcast");
        close(sockbrcast);
        exit(EXIT_FAILURE);
    }

    // Делаем прослушивание сокета broadcast'ов неблокирующим
    fcntl(sockbrcast, F_SETFL, O_NONBLOCK);

    // Создаем множество прослушивания
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(sockbrcast, &readset);

    // Таймаут
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    struct sockaddr_in *servers =
        (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    bzero(servers, sizeof(struct sockaddr_in));
    int servcount = 0;
    int maxserv = 1;
    socklen_t servaddrlen = sizeof(struct sockaddr_in);
    // Создаем список серверов servers
    while (select(sockbrcast + 1, &readset, NULL, &readset, &timeout) > 0) {
        int rdbyte =
            recvfrom(sockbrcast, (void *)hellomesg, msgsize, MSG_TRUNC,
                     (struct sockaddr *)&servers[servcount], &servaddrlen);
        if (rdbyte == msgsize && strcmp(hellomesg, "Hello Client") == 0) {
            servcount++;

            if (servcount >= maxserv) {
                servers = realloc(servers,
                                  sizeof(struct sockaddr_in) * (maxserv + 1));
                if (servers == NULL) {
                    perror("Realloc failed");
                    close(sockbrcast);
                    exit(EXIT_FAILURE);
                }
                bzero(&servers[servcount], servaddrlen);
                maxserv++;
            }
            FD_ZERO(&readset);
            FD_SET(sockbrcast, &readset);
        }
    }
    int i;
    if (servcount < 1) {
        fprintf(stderr, "No servers found!\n");
        exit(EXIT_FAILURE);
    }
    if (argc > 3 && maxservu <= servcount) servcount = maxservu;
    for (i = 0; i < servcount; ++i) {
        printf("Server answer from %s on port %d\n",
               inet_ntoa(servers[i].sin_addr), ntohs(servers[i].sin_port));
    }
    printf("\n");
    free(hellomesg);

    // Создаем массив для результатов
    int *results = (int *)malloc(sizeof(int) * sizeof(array_size));
    // Создаем треды для работы с серверами
    pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t) * servcount);
    int subarray_size = array_size / servcount;
    int remaining_elements = array_size % servcount;

    for (i = 0; i < servcount; ++i) {
        thread_args_t *args = (thread_args_t *)malloc(sizeof(thread_args_t));
        args->left = i * subarray_size;
        args->right = args->left + subarray_size - 1;

        // Adjust the subarray size for the last server
        if (i == servcount - 1) {
            args->right += remaining_elements;
        }
        args->array =
            (int *)malloc(sizeof(int) * (args->right - args->left + 1));
        memcpy(args->array, &array[args->left],
               sizeof(int) * (args->right - args->left + 1));
        args->server = &servers[i];
        printf("args->left = %d, args->right = %d\n", args->left, args->right);
        for (int j = args->left; j <= args->right; ++j) {
            printf("%d\n", args->array[j]);
        }
        if (pthread_create(&tid[i], NULL, send_thread, args) != 0) {
            perror("Create send thread failed");
            exit(EXIT_FAILURE);
        }
    }

    // Copy remaining elements from the last server
    // memcpy(&sorted_array[servcount * subarray_size], &results[servcount * subarray_size], sizeof(int) * remaining_elements);

    // Sort the merged subarray

    free(servers);

    return (EXIT_SUCCESS);
}