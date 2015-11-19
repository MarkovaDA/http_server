
#include <sys/mman.h>
#include <semaphore.h>  /* for sem_int ...*/
#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), bind(), and connect() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <time.h>       /* for time() */
#include <netdb.h>      /* for gethostbyname() */
#include <signal.h>     /* for signal() */
#include <sys/stat.h>   /* for stat() */
#include <dirent.h>
#include <sys/wait.h>

#define MAXPENDING 5    /* максимальное количество запросов */

#define DISK_IO_BUF_SIZE 4096

static void die(const char *message)
{
    perror(message);
    exit(1); 
}


static void signal_handler_1(){
    pid_t pid;
    for(;;){
        if((pid=waitpid(-1,NULL,WNOHANG)) == -1){
           break;
    }
		//fprintf(stderr,"%d\n",pid);
	}
}

/*
 * Создаём сокет и привязываем его к порту
 */
static int createServerSocket(unsigned short port)
{
    int servSock;
    struct sockaddr_in servAddr;

    /*Создаём ТСP-сокет для входящих соединений */
    if ((servSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        die("socket() failed");
      
    /*Заполняем структуру необходимыми данными */
    memset(&servAddr, 0, sizeof(servAddr));       /*обнуляем */
    servAddr.sin_family = AF_INET;                /*семейство интернет-адресов*/
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY); /*сокет будет связан со всеми локальными интерефейсами*/
    servAddr.sin_port = htons(port);              /*порт*/

    /* Привязка сокета */
    if (bind(servSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
        die("bind() failed");

    /* Прослушивание входящих соединений */
    if (listen(servSock, MAXPENDING) < 0)
        die("listen() failed");

    return servSock;
}

/*
 * посылаем сообщение клиенту
 */
ssize_t Send(int sock, const char *buf)
{
    size_t len = strlen(buf);
    ssize_t res = send(sock, buf, len, 0);
    if (res != len) {
        perror("send() failed");
        return -1;
    }
    else 
        return res;
}

/*
 * HTTP/1.0 коды и соответствующее описание ошибки
 */

static struct {
    int status;
    char *reason;
} HTTP_StatusCodes[] = {
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 204, "No Content" },
    { 301, "Moved Permanently" },
    { 302, "Moved Temporarily" },
    { 304, "Not Modified" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 0, NULL } // конец списка
};
/*получаем по коду ошибки её описание*/
static inline const char *getReasonPhrase(int statusCode)
{
    int i = 0;
    while (HTTP_StatusCodes[i].status > 0) {
        if (HTTP_StatusCodes[i].status == statusCode)
            return HTTP_StatusCodes[i].reason;
        i++;
    }
    return "Unknown Status Code";
}


/*
 * формируем статус запроса и посылаем браузеру
 */
static void sendStatusLine(int clntSock, int statusCode)
{
    char buf[1000];
    const char *reasonPhrase = getReasonPhrase(statusCode);

    //Заносим в буфер статус запроса
    sprintf(buf, "HTTP/1.0 %d ", statusCode);
    strcat(buf, reasonPhrase);
    strcat(buf, "\r\n");
    strcat(buf, "\r\n");

    //Переводим в html-формат
    if (statusCode != 200) {
        char body[1000];
        sprintf(body, 
                "<html><body>\n"
                "<h1>%d %s</h1>\n"
                "</body></html>\n",
                statusCode, reasonPhrase);
        strcat(buf, body);
    }
    //Передаем браузеру
    Send(clntSock, buf);
}

/*
 * Handle static file requests.
 * Returns the HTTP status code that was sent to the browser.
 */
static int handleFileRequest(const char *webRoot, const char *requestURI, int clntSock)
{
    int statusCode;
    FILE *fp = NULL;

    //Путь к файлу формируется из  webRoot и requestURI.
    //Если запрос оканчивается  '/', то добавляем  "index.html".
    
    char *file = (char *)malloc(strlen(webRoot) + strlen(requestURI) + 100);
    if (file == NULL)
        die("malloc failed");
    strcpy(file, webRoot);
    strcat(file, requestURI);
    if (file[strlen(file)-1] == '/') {
        strcat(file, "index.html");
    }
    //Сервер не поддерживает чтение директория
    struct stat st;

    if (stat(file, &st) == 0 && S_ISDIR(st.st_mode)) {
        //fprintf(stderr,"enter into the dir handler\n");

        /*
        statusCode = 403; // "Forbidden"
        */
        // Part4 opendir, readdir
        
        DIR *dp;
        struct dirent *dirp;

        if((dp = opendir(file)) == NULL)
            die("cant open dir");
        while((dirp = readdir(dp)) != NULL) {
            strcat(dirp->d_name, "\n");
            Send(clntSock, dirp->d_name);
        }
        statusCode = 200; // "OK"
        goto func_end;
    }

    //Если не можем открыть файл, посылаем "404 Not Found".

    fp = fopen(file, "rb");
    if (fp == NULL) {
        statusCode = 404; 
        sendStatusLine(clntSock, statusCode);
        goto func_end;
    }

    //иначе, послылаем "200 OK" 

    statusCode = 200; 
    sendStatusLine(clntSock, statusCode);

    //читаем содержимое файла
    size_t n;
    char buf[DISK_IO_BUF_SIZE];
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send(clntSock, buf, n, 0) != n) {
            perror("\nsend() failed");
            break;
        }
    }
    if (ferror(fp))
        perror("fread failed");

func_end:
    free(file);
    if (fp)
        fclose(fp);
    return statusCode;
}


struct stat_report{
    sem_t semaphore;
    int request_counter;
    int status_2xx;
    int status_3xx;
    int status_4xx;
    int status_5xx;
};

static struct stat_report *area;   
 

static int handleStatRequest(int clntSock){
    char buff[1000];
    int statusCode=200;
    sendStatusLine(clntSock,statusCode);
    sprintf(buff,"<html><body><p>Server Statistics</p><p>Requests = %d </p><p>2xx = %d </p><p>3xx = %d </p><p>4xx = %d </p><p>5xx = %d </p></body></html>\n",
                  area->request_counter, 
                  area->status_2xx, 
                  area->status_3xx, 
                  area->status_4xx, 
                  area->status_5xx);
    Send(clntSock,buff);
    return statusCode;
}


int main(int argc, char *argv[])
{   
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) /*игнорирование сигнала SIGPIPE*/
        die("signal() failed");

    if (argc != 3) {
        fprintf(stderr, "usage: %s <server_port> <web_root>\n", argv[0]);
        exit(1);
    }
    
    unsigned short servPort = atoi(argv[1]);
    const char *webRoot = argv[2];
		//создаём сокет
    int servSock = createServerSocket(servPort);
    pid_t pid;
    char line[1000];
    char requestLine[1000];
    int statusCode;
    struct sockaddr_in clntAddr;
    if(signal(SIGCHLD,signal_handler_1) == SIG_ERR){
        die("signal error");       
    }
    area = mmap(NULL,sizeof(struct stat_report),PROT_READ | PROT_WRITE,MAP_ANONYMOUS|MAP_SHARED,-1,0);
		/*семафор, разделяемый процессами*/
    if(sem_init(&area->semaphore,1,1) != 0){
        die("sem_init error");
    }
    

    for (;;) {
         fprintf(stderr,"start of for(;;)\n");
        /*
         * ожидаем соединения клиента
         */
        //инициализируем параметры
        unsigned int clntLen = sizeof(clntAddr);
				//приниаем соединение 
        int clntSock = accept(servSock, (struct sockaddr *)&clntAddr, &clntLen);
        if (clntSock < 0)
            die("accept() failed");
        if((pid = fork())==-1){
            die("fork error");
        }else if(pid > 0){
						//родительский процесс
            close(clntSock);
            continue;
        }
				//дочерний процесс
        close(servSock);
        FILE *clntFp = fdopen(clntSock, "r"); //преобразуем дескриптор сокета к файловому
        if (clntFp == NULL)
            die("fdopen failed");
        /*
         *Разбираем запрос: парсим метод, url, версию протикола
         */
        char *method      = "";
        char *requestURI  = "";
        char *httpVersion = "";
				/*считываем запрос клиента*/
        if (fgets(requestLine, sizeof(requestLine), clntFp) == NULL) {
            statusCode = 400;
            goto loop_end;
        }
	
        char *token_separators = "\t \r\n"; 
        method = strtok(requestLine, token_separators);
        requestURI = strtok(NULL, token_separators);
        httpVersion = strtok(NULL, token_separators);
        char *extraThingsOnRequestLine = strtok(NULL, token_separators);

        //Проверяем, чтобы только 3 параметра в запросе были использованы
        if (!method || !requestURI || !httpVersion || 
                extraThingsOnRequestLine) {
            statusCode = 501; // "Not Implemented"
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        //поддерживаем только GET method 
        if (strcmp(method, "GET") != 0) {
            statusCode = 501; // "Not Implemented"
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        //поддерживаем только HTTP/1.0 и HTTP/1.1
        if (strcmp(httpVersion, "HTTP/1.0") != 0 && 
            strcmp(httpVersion, "HTTP/1.1") != 0) {
            statusCode = 501; // "Not Implemented"
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }
        
        // requestURI должен начинаться с "/"
        if (!requestURI || *requestURI != '/') {
            statusCode = 400; // "Bad Request"
            sendStatusLine(clntSock, statusCode);
            goto loop_end;
        }

        // убеждаемся, что requestURI не содержит "/../" и
        // не оканчивается с "/..", что может быть большим изъяном в безопасности
        int len = strlen(requestURI);
        if (len >= 3) {
            char *tail = requestURI + (len - 3);
            if (strcmp(tail, "/..") == 0 || 
                    strstr(requestURI, "/../") != NULL)
           {
                statusCode = 400; // "Bad Request"
                sendStatusLine(clntSock, statusCode);
                goto loop_end;
            }
        }
        while (1) {
            if (fgets(line, sizeof(line), clntFp) == NULL) {
                statusCode = 400; // "Bad Request"
                goto loop_end;
            }
            if (strcmp("\r\n", line) == 0 || strcmp("\n", line) == 0) {
                break;
            }
        }
				//можно посмотреть статистику по запросам
        if(strcmp(requestURI,"/statistics") == 0){
            if(sem_wait(&area->semaphore)==-1){
                die("sem_wait error");
            }
            area->request_counter++;
            area->status_2xx++;
            if(sem_post(&area->semaphore)==-1){
                die("sem_post error");
            } /*обрабатываем запрос на статистику*/
            statusCode = handleStatRequest(clntSock);   
        }
				else{ /*предоставляем ресурс*/
            statusCode = handleFileRequest(webRoot, requestURI, clntSock);
        }
loop_end:

        /*
         * учитываем статистику запроса
         */
         if (strcmp(requestURI,"/statistics") != 0) {
            if(sem_wait(&area->semaphore)==-1){
                die("sem_wait error");
            }
            area->request_counter++;
            //get the status number
            switch(statusCode / 100) {
                case 2: area->status_2xx++;    break;
                case 3: area->status_3xx++;    break;
                case 4: area->status_4xx++;    break;
                case 5: area->status_5xx++;    break;
                default: break;
            }

            if(sem_post(&area->semaphore)==-1){
                die("sem_post error");
            }
        }
        fprintf(stderr, "%s \"%s %s %s\" %d %s (handled by child process #%d)\n",
                inet_ntoa(clntAddr.sin_addr),
                method,
                requestURI,
                httpVersion,
                statusCode,
                getReasonPhrase(statusCode),
                getpid());
        fclose(clntFp);
        exit(0);
    } 
    sem_destroy(&area->semaphore);
    munmap(area,sizeof(struct stat_report));
    return 0;
}
