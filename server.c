#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>

#define RCVPORT 38199
// Структура для передачи задания серверу
typedef struct {
    int *array;  // Массив для сортировки
    int left;    // Левый индекс подмассива
    int right;   // Правый индекс подмассива
} task_data_t;
// Аргумент функции треда вычислителя
typedef struct {
    int *array;  // Массив для сортировки
    int left;    // Левый индекс подмассива
    int right;   // Правый индекс подмассива
} thread_args_t;
void merge(int arr[], int l, int m, int r) {
    int i, j, k;
    int n1 = m - l + 1;
    int n2 = r - m;
    printf("left: %d\nright: %d\n", l, r);
    printf("array: \n");
    printf("arr size: %d\n", sizeof(arr) / sizeof(int));

    int L[n1], R[n2];
    printf("11\n");

    for (i = 0; i < n1; i++) L[i] = arr[l + i];
    for (j = 0; j < n2; j++) R[j] = arr[m + 1 + j];

    i = 0;
    j = 0;
    k = l;
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            arr[k] = L[i];
            i++;
        } else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }
    for (int i = l; i <= r; i++) printf("%d ", arr[i]);
}
// Функция треда вычислителя
void *calculate(void *arg) {
    // Ожидаем в качестве аргумента указатель на структуру thread_args_t
    thread_args_t *tinfo = (thread_args_t *)arg;
    int *array = tinfo->array;
    int left = tinfo->left;
    int right = tinfo->right;
  
    if (left < right) {
        int m = left + (right - left) / 2;

        // Создаем аргументы для левого и правого тредов
        thread_args_t left_args = {array, left, m};
        thread_args_t right_args = {array, m + 1, right};

        // Создаем левый и правый треды
        pthread_t left_thread, right_thread;
        pthread_create(&left_thread, NULL, calculate, &left_args);
        pthread_create(&right_thread, NULL, calculate, &right_args);
        

        // Ждем завершения левого и правого тредов
        pthread_join(left_thread, NULL);
        pthread_join(right_thread, NULL);
        
        printf("left: %d\nright: %d\n", left, right);
        printf("array: \n");
        // printf("arr size: %d\n", sizeof(array) / sizeof(int));
        for (int i = left; i < right; i++) printf("%d ", array[i]);
        printf("\n");
        // Сливаем отсортированные левый и правый подмассивы
        merge(array, left, m, right);
        //   printf("10\n");
    }
}
// Аргумент для проверяющего клиента треда
typedef struct {
    int sock;  // Сокет с клиентом
    pthread_t *calcthreads;  // Треды которые в случае чего надо убить
    int threadnum;  // Количество этих тредов
} checker_args_t;

// Функция которая будет выполнена тредом получившим сигнал SIGUSR1
void thread_cancel(int signo) { pthread_exit(PTHREAD_CANCELED); }

// Тред проверяющий состояние клиента
void *client_check(void *arg) {
    // Нам должен быть передан аргумент типа checker_args_t
    checker_args_t *args = (checker_args_t *)arg;
    char a[10];
    recv(args->sock, &a, 10,
         0);  // Так как мы используем TCP, если клиент умрет или что либо
    // скажет, то recv тут же разблокирует тред и вернёт -1
    int st;
    for (int i = 0; i < args->threadnum; ++i)
        st = pthread_kill(args->calcthreads[i],
                          SIGUSR1);  // Шлем всем вычислителям SIGUSR1
    return NULL;
}
void *listen_broadcast(void *arg) {
    int *isbusy = arg;
    // Создаем сокет для работы с broadcast
    int sockbrcast = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockbrcast == -1) {
        perror("Create broadcast socket failed");
        exit(EXIT_FAILURE);
    }

    // Создаем структуру для приема ответов на broadcast
    int port_rcv = RCVPORT;
    struct sockaddr_in addrbrcast_rcv;
    bzero(&addrbrcast_rcv, sizeof(addrbrcast_rcv));
    addrbrcast_rcv.sin_family = AF_INET;
    addrbrcast_rcv.sin_addr.s_addr = htonl(INADDR_ANY);
    addrbrcast_rcv.sin_port = htons(port_rcv);
    // Биндим её
    if (bind(sockbrcast, (struct sockaddr *)&addrbrcast_rcv,
             sizeof(addrbrcast_rcv)) < 0) {
        perror("Bind broadcast socket failed");
        close(sockbrcast);
        exit(EXIT_FAILURE);
    }

    int msgsize = sizeof(char) * 18;
    char hellomesg[18];
    bzero(hellomesg, msgsize);
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

    struct sockaddr_in client;
    ;
    bzero(&client, sizeof(client));
    socklen_t servaddrlen = sizeof(struct sockaddr_in);
    char helloanswer[18];
    bzero(helloanswer, msgsize);
    strcpy(helloanswer, "Hello Client");
    int sockst = 1;
    while (sockst > 0) {
        sockst = select(sockbrcast + 1, &readset, NULL, &readset, NULL);
        if (sockst == -1) {
            perror("Broblems on broadcast socket");
            exit(EXIT_FAILURE);
        }
        int rdbyte = recvfrom(sockbrcast, (void *)hellomesg, msgsize, MSG_TRUNC,
                              (struct sockaddr *)&client, &servaddrlen);
        if (rdbyte == msgsize && strcmp(hellomesg, "Hello Integral") == 0 &&
            *isbusy == 0) {
            if (sendto(sockbrcast, helloanswer, msgsize, 0,
                       (struct sockaddr *)&client,
                       sizeof(struct sockaddr_in)) < 0) {
                perror("Sending answer");
                close(sockbrcast);
                exit(EXIT_FAILURE);
            }
        }
        FD_ZERO(&readset);
        FD_SET(sockbrcast, &readset);
    }
    return NULL;
}
int main(int argc, char **argv) {
    // Аргумент может быть только один - это кол-во тредов
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [numofcpus]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int numofthread;
    printf("1\n");
    if (argc == 2) {
        numofthread = atoi(argv[1]);
        if (numofthread < 1) {
            fprintf(stderr, "Incorrect num of threads!\n");
            exit(EXIT_FAILURE);
        }
        fprintf(stdout, "Num of threads forced to %d\n", numofthread);
    } else {
        // Если аргументов нет, то определяем кол-во процессоров автоматически
        numofthread = sysconf(_SC_NPROCESSORS_ONLN);
        if (numofthread < 1) {
            fprintf(stderr,
                    "Can't detect num of processors\n"
                    "Continue in two threads\n");
            numofthread = 2;
        }
        fprintf(stdout, "Num of threads detected automatically it's %d\n\n",
                numofthread);
    }
    printf("2\n");

    struct sigaction cancel_act;
    memset(&cancel_act, 0, sizeof(cancel_act));
    cancel_act.sa_handler = thread_cancel;
    sigfillset(&cancel_act.sa_mask);
    sigaction(SIGUSR1, &cancel_act, NULL);

    // Создаем тред слушающий broadcast'ы
    pthread_t broadcast_thread;
    int isbusy = 1;  //(int*) malloc(sizeof(int));
    // Переменная которая сообщает треду следует ли отвечать на broadcast
    // 0 - отвечать, 1- нет
    isbusy = 1;
    if (pthread_create(&broadcast_thread, NULL, listen_broadcast, &isbusy)) {
        fprintf(stderr, "Can't create broadcast listen thread");
        perror("Detail:");
        exit(EXIT_FAILURE);
    }
    int listener;
    struct sockaddr_in addr;
    listener = socket(PF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("Can't create listen socket");
        exit(EXIT_FAILURE);
    }
    printf("3\n");

    addr.sin_family = AF_INET;
    addr.sin_port = htons(RCVPORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    int a = 1;
    // Добавляем опцию SO_REUSEADDR для случаев когда мы перезапускам сервер
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &a, sizeof(a)) < 0) {
        perror("Set listener socket options");
        exit(EXIT_FAILURE);
    }

    // Биндим сокет
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Can't bind listen socket");
        exit(EXIT_FAILURE);
    }

    // Начинаем ждать соединения от клиентов
    if (listen(listener, 1) < 0) {
        perror("Eror listen socket");
        exit(EXIT_FAILURE);
    }
    printf("4\n");

    // Ожидаем соединений
    int needexit = 0;
    while (needexit == 0) {
        fprintf(stdout, "\nWait new connection...\n\n");
        int client;
        isbusy = 0;  // Разрешаем отвечать клиентам на запросы
        struct sockaddr_in addrclient;
        socklen_t addrclientsize = sizeof(addrclient);
        client =
            accept(listener, (struct sockaddr *)&addrclient, &addrclientsize);
        if (client < 0) {
            perror("Client accepting");
        }
        isbusy = 1;  // Запрещаем отвечать на запросы

        int array_size = 0;
        recv(client, &array_size, sizeof(int), 0);
        int left, right, total_received = 0;
        recv(client, &left, sizeof(int), 0);
        printf("left: %d\n", left);
        recv(client, &right, sizeof(int), 0);
        printf("right: %d\n", right);
        printf("array_size: %d\n", array_size);
        int *buf = (int *)malloc(sizeof(int) * array_size);

        int received = recv(client, buf, sizeof(int) * array_size, 0);
        if (received < 0) {
            perror("Receiving data from client");
            exit(EXIT_FAILURE);
        }

        printf("bytes_received: %d\n", received);

        printf("received array: ");
        for (int i = 0; i < array_size; i++) {
            printf("%d ", buf[i]);
        }
        printf("\n");

        printf("total received: %d\n", total_received);
        if (received != array_size * sizeof(int) || left < 0 || right < 0) {
            fprintf(stderr, "Invalid data from %s on port %d, reset peer\n",
                    inet_ntoa(addrclient.sin_addr), ntohs(addrclient.sin_port));
            close(client);
            isbusy = 0;
        } else {
            fprintf(stdout, "Calculate and send to %s on port %d\n",
                    inet_ntoa(addrclient.sin_addr), ntohs(addrclient.sin_port));
            thread_args_t *tinfo;
            printf("6\n");
            task_data_t data;
            data.array = buf;
            data.left = left;
            data.right = right;
            calculate(&data);

            int sent = send(client, buf, sizeof(int) * array_size, 0);
            printf("sent: %d\n", sent);

            close(client);

            free(tinfo);
            isbusy = 0;
            fprintf(stdout, "Calculate and send finish!\n");
        }

        return (EXIT_SUCCESS);
    };
}
