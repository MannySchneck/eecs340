#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>


#define FILENAMESIZE 100
#define BUFSIZE 1024
#define BACKLOG 1
#define REQUEST_END "\r\n\r\n"
#define REQUEST_END_LEN sizeof(REQUEST_END)

typedef enum \
        {NEW,READING_HEADERS,WRITING_RESPONSE,READING_FILE,WRITING_FILE,TO_CLOSE} states;

typedef struct connection_s connection;
typedef struct connection_list_s connection_list;

struct connection_s
{
        int sock;
        int fd;
        char filename[FILENAMESIZE+1];
        char buf[BUFSIZE+1];
        char *bptr;
        bool ok;
        long filelen;
        states state;
        int headers_read,response_written,file_read,file_written;

        connection *next;
};

struct connection_list_s
{
        connection *first,*last;
};

void add_connection(int,connection_list *);
void insert_connection(int,connection_list *);
void init_connection(connection *conn);
void clean_close(connection*);
bool is_more_request(char*);


int writenbytes(int,char *,int);
int readnbytes(int,char *,int);
void read_headers(connection *);
void write_response(connection *);
void read_file(connection *);
void write_file(connection *);
// TODO:
// * free_connections

int main(int argc,char *argv[])
{
        int server_port;
        int listening_sock,new_sock;
        struct sockaddr_in sa;
        fd_set readlist,writelist, master_read_set, master_write_set;
        connection_list connections;
        connection *conn;
        int maxfd;
        char stack;
        int flags;

        connections.first = NULL;
        connections.last = NULL;

        /* parse command line args */
        if (argc != 3)
                {
                        fprintf(stderr, "usage: http_server1 k|u port\n");
                        exit(-1);
                }
        server_port = atoi(argv[2]);
        stack = toupper(argv[1][0]);


        if (server_port < 1500)
                {
                        fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n",server_port);
                        exit(-1);
                }

        if(stack == 'K'){
                minet_init(MINET_KERNEL);
        } else if(stack == 'U'){
                minet_init(MINET_USER);
        }
        else{
                fprintf(stderr, "First argument must be k or u\n");
                exit(-1);
        }

        /* initialize and make socket */
        if((listening_sock = minet_socket(SOCK_STREAM)) < 0){
                perror("minet_socket");
                exit(-1);
        }

        if((flags = fcntl(listening_sock, F_GETFL, 0)) < 0){
                perror("fcntl get");
                exit(-1);
        }
        if(fcntl(listening_sock, F_SETFL, flags | O_NONBLOCK) < 0){
                perror("fcntl ");
                printf("%d\n", __LINE__);
                exit(-1);
        }
        /* set server address*/
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(server_port);
        sa.sin_addr.s_addr = INADDR_ANY;

        /* bind listening socket */
        if(minet_bind(listening_sock, &sa)){
                perror("bind");
                goto bad;
        }

        /* start listening */
        if((minet_listen(listening_sock, BACKLOG)) < 0){
                perror("Couldn't listen");
                goto bad;
        }

        FD_ZERO(&master_read_set);
        FD_ZERO(&master_write_set);

        FD_SET(listening_sock, &master_read_set);

        maxfd = listening_sock;

        /* connection handling loop */
        while(1)
                {
                        /* create read and write lists */
                        readlist = master_read_set;
                        writelist = master_write_set;


                        /* do a select */
                        select(maxfd + 1, &readlist, &writelist, NULL, NULL);

                        /* Special case the listening socket */
                        if (FD_ISSET(listening_sock, &readlist)) {
                                if((new_sock = minet_accept(listening_sock, &sa)) < 0){
                                        if(errno == EAGAIN){
                                                continue;
                                        }
                                        else{
                                                perror("accept");
                                                exit(-1);
                                        }
                                }
                                maxfd = (new_sock > maxfd) ? new_sock : maxfd;
                                if((flags = fcntl(new_sock, F_GETFL, 0)) < 0){
                                        perror("fcntl get");
                                        printf("%d\n", __LINE__);
                                        exit(-1);
                                }
                                if(fcntl(new_sock, F_SETFL, flags | O_NONBLOCK) < 0){
                                        perror("fcntl");
                                }
                                FD_SET(new_sock, &master_read_set);
                                FD_SET(new_sock, &master_write_set);
                                insert_connection(new_sock, &connections);

                        }
                        /* loop over connections and deal with their shit */
                        for(conn = connections.first; conn != NULL; conn = conn->next){
                                if(conn->state == TO_CLOSE){
                                        FD_CLR(conn->fd, &master_read_set);
                                        FD_CLR(conn->sock, &master_read_set);
                                        FD_CLR(conn->sock, &master_write_set);
                                        readlist = master_read_set;
                                        writelist = master_write_set;
                                        clean_close(conn);
                                }
                                if(FD_ISSET(conn->sock, &readlist) ||
                                   FD_ISSET(conn->fd, &readlist)||
                                   FD_ISSET(conn->sock, &writelist)){
                                        /* for a connection socket, handle the connection */
                                        switch(conn->state){
                                        case TO_CLOSE:
                                                ;
                                        case NEW:
                                                /* init connection and try to read headers*/
                                                init_connection(conn);
                                                break;
                                        case READING_HEADERS:
                                                /* keep reading headers, change state if done */
                                                /* !ok -> WRITING RESPONSE */
                                                /* ok -> READING FILE + add fds to list */
                                                read_headers(conn);
                                                break;
                                        case READING_FILE:
                                                /* -> WRITING FILE */
                                                FD_ISSET(conn->fd, &master_read_set);
                                                read_file(conn);
                                                break;
                                        case WRITING_RESPONSE:
                                                /* start writing, update state if done */
                                                /* -> READING FILE */
                                                FD_ISSET(conn->fd, &master_write_set);
                                                write_response(conn);
                                                break;
                                        case WRITING_FILE:
                                                /* -> TO_CLOSE */
                                                write_file(conn);
                                                break;
                                        default:
                                                fprintf(stderr, "Invalid state! %d\n", conn->state);
                                                goto bad;
                                        }
                                }
                        }                       /* process sockets that are ready */
                }

 bad:
        connection* tmp;
        for(conn = connections.first; conn != NULL; conn = tmp){
                clean_close(conn);
                tmp = conn->next;
                free(conn);
        }
        minet_deinit();
        return -1;
}

void clean_close(connection *conn){
        shutdown(conn->sock, SHUT_RDWR);
        minet_close(conn->sock);
}

void read_headers(connection *conn){
        /* first read loop -- get request and headers*/
        struct stat statbuf;
        int bytes_read = 0;
        int c_sock = conn->sock;
        int flags;

        while(!(conn->headers_read = !is_more_request(conn->buf))){
                if((bytes_read = (minet_read(c_sock, conn->bptr, BUFSIZE))) < 0){
                        if(errno == EAGAIN){
                                return;
                        }
                        perror("read");
                        printf("%d\n", __LINE__);
                        exit(-1); // XXX not cleaning up =(
                }
                conn->bptr += bytes_read;
        }

        /* parse request to get file name */
        for(conn->bptr = conn->buf; (*conn->bptr && *conn->bptr != '/'); conn->bptr++);

        if(!(*conn->bptr)){
                fprintf(stderr, "bad request. You forgot a \"/\" =(\n");
        }

        /* Assumption: this is a GET request and filename contains no spaces*/
        memset(conn->filename, '\0', FILENAMESIZE);
        conn->bptr++;
        for(int i = 0; *(conn->bptr + i) != ' '; i++){
                conn->filename[i] = *(conn->bptr + i);
        }

        /* get file name and size, set to non-blocking */
        conn->ok = !stat(conn->filename, &statbuf);

        conn->state = WRITING_RESPONSE;
        if(conn->ok){
                conn->filelen = statbuf.st_size;
                if((conn->fd = open(conn->filename, O_RDONLY)) < 0){
                        perror("open");
                        exit(-1);
                }
                if((flags = fcntl(conn->fd, F_GETFL, 0)) < 0){
                        perror("fcntl get");
                        exit(-1);
                }
                if(fcntl(conn->fd, F_SETFL, O_NONBLOCK | flags) < 0){
                        perror("fcntl");
                        exit(-1);
                }
                return;
        }
}


void write_response(connection *conn){
        int rc = 0;
        char* response;
        char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                "Content-type: text/plain\r\n"\
                "Content-length: %d \r\n\r\n";
        char ok_response[100];
        char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                "Content-type: text/html\r\n\r\n"\
                "<html><body bgColor=black text=white>\n"\
                "<h2>404 FILE NOT FOUND</h2>\n"\
                "</body></html>\n";
        /* send response */
        if (conn->ok)
                {
                        /* send headers */
                        snprintf(ok_response, strlen(ok_response_f), ok_response_f, conn->filelen);
                        response = ok_response;
                }
        else
                {
                        response = notok_response;
                }
        while(conn->response_written < strlen(response)){
                if((rc = minet_write(conn->sock,
                                     response + conn->response_written,
                                     strlen(response))) < 0){
                        if(errno == EAGAIN){
                                return;
                        }
                        else{
                                perror("write");
                                exit(-1); // XXX: better cleanup
                        }
                }
                if(rc == 0){
                        conn->state = TO_CLOSE;
                        return;
                }

                conn->response_written += rc;
        }
        /* move state machine forward */
        if(conn->ok){
                conn->state = READING_FILE;
        } else{
                conn->state = TO_CLOSE;
        }
}

void read_file(connection *conn) {
        int rc;
        int flags;

        /* send file */
        if((flags = fcntl(conn->fd, F_GETFL, 0)) < 0){
                perror("fcntl get");
                exit(-1);
        }

        if(fcntl(conn->fd, F_SETFL, flags | O_NONBLOCK) < 0){
                perror("fcntl");
                exit(-1);
        }
        conn->bptr = conn->buf + (conn->file_read % BUFSIZE);
        while(conn->bptr < conn->buf + BUFSIZE && conn->file_read < conn->filelen){
                rc = read(conn->fd, conn->bptr,BUFSIZE);
                if (rc < 0){
                        if (errno == EAGAIN)
                                return;
                        fprintf(stderr,"error reading requested file %s\n",conn->filename);
                        return;
                }
                else if (rc > 0){
                        conn->file_read += rc;
                        conn->bptr += rc;
                }
                else {
                        conn->state = TO_CLOSE;
                }
        }
        conn->state = WRITING_FILE;
        write_file(conn);
}



void write_file(connection *conn){
        int written = 0;
        while(conn->buf + written < conn->bptr){
                int rc = minet_write(conn->sock, conn->buf + written, conn->bptr - conn->buf);
                if (rc < 0){
                        if (errno == EAGAIN)
                                return;
                        minet_perror("error writing response ");
                        conn->state = TO_CLOSE;
                        minet_close(conn->sock);
                        return;
                }
                if (rc > 0){
                        conn->file_written += rc;
                        written += rc;
                }
                if(conn->file_written >= conn->filelen){
                        conn->state = TO_CLOSE;
                        return;
                }
        }
        conn->state = READING_FILE;
}

int readnbytes(int fd,char *buf,int size)
{
        int rc = 0;
        int totalread = 0;
        while ((rc = minet_read(fd,buf+totalread,size-totalread)) > 0)
                totalread += rc;

        if (rc < 0)
                {
                        return -1;
                }
        else
                return totalread;
}

int writenbytes(int fd,char *str,int size)
{
        int rc = 0;
        int totalwritten =0;
        while ((rc = minet_write(fd,str+totalwritten,size-totalwritten)) > 0)
                totalwritten += rc;

        if (rc < 0)
                return -1;
        else
                return totalwritten;
}


// inserts a connection in place of a closed connection
// if there are no closed connections, appends the connection
// to the end of the list

void insert_connection(int sock,connection_list *con_list)
{
        connection *i;
        for (i = con_list->first; i != NULL; i = i->next)
                {
                        if (i->state == TO_CLOSE)
                                {
                                        i->sock = sock;
                                        i->state = NEW;
                                        return;
                                }
                }
        add_connection(sock,con_list);
}

void add_connection(int sock,connection_list *con_list){
        connection *conn = (connection *) malloc(sizeof(connection));
        conn->next = NULL;
        conn->state = NEW;
        conn->sock = sock;
        if (con_list->first == NULL)
                con_list->first = conn;
        if (con_list->last != NULL)
                {
                        con_list->last->next = conn;
                        con_list->last = conn;
                }
        else
                con_list->last = conn;
}

void init_connection(connection *conn){
        conn->fd = -1;
        conn->headers_read = 0;
        conn->response_written = 0;
        conn->file_read = 0;
        conn->file_written = 0;

        conn->state = READING_HEADERS;
        memset(conn->buf, '\0', BUFSIZE + 1);
        conn->bptr = conn->buf;
}

bool is_more_request(char* buf){
        for(int i = 0; buf[i]; i++){
                if(strncmp(buf + i, REQUEST_END, REQUEST_END_LEN - 1)){
                        continue;
                }
                return false;
        }
        return true;
}
