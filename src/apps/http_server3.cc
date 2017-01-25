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


int writenbytes(int,char *,int);
int readnbytes(int,char *,int);
void read_headers(connection *);
void write_response(connection *);
void read_file(connection *);
void write_file(connection *);

int main(int argc,char *argv[])
{
        int server_port;
        int sock,sock2;
        struct sockaddr_in sa,sa2;
        int rc;
        fd_set readlist,writelist, master_read_set, master_write_set;
        connection_list connections;
        connection *conn;
        int maxfd;

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
        listening_sock = minet_socket(SOCK_STREAM);
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
                        select(maxfd, + 1, &readlist, &writelist, NULL, NULL);

                        /* Special case the listening socket */
                        if (FD_ISSET(listening_sock, &readlist)) {
                                new_sock = minet_accept(listening_sock, &sa);
                                maxfd = (new_sock > maxfd) ? new_sock : maxfd;
                                fcntl(new_sock, O_NONBLOCK);
                                FD_SET(new_sock, &master_read_set);
                                FD_SET(new_sock, master_write_set);
                                insert_connection(new_sock, &connections);

                        }
                        /* loop over connections and deal with their shit */
                        for(conn = connection_list.start; conn != NULL; conn = conn->next){
                                if(conn->state == TO_CLOSE){
                                        shutdown(conn->sock, SHUT_RDWR);
                                        minet_close(conn->sock);
                                }
                                if(FD_ISSET(conn->sock, &readlist) || (conn->fd, &readlist)){
                                        /* for a connection socket, handle the connection */
                                        switch(connection_state){
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
                                        default:
                                                fprintf(stderr, "Invalid state! %d\n", connection_state);
                                                goto bad;
                                        }
                                }
                                if(FD_ISSET(conn->sock, &writelist)){
                                        switch (conn->state){
                                        case WRITING_RESPONSE:
                                                /* start writing, update state if done */
                                                /* -> READING FILE */
                                                write_response(conn);
                                                break;
                                        case WRITING_FILE:
                                                /* -> TO_CLOSE */
                                                write_file(conn);
                                                break;
                                        }
                                }
                        }
                }                       /* process sockets that are ready */
 bad:
        // TODO:
        // close all sockets
        // shutdown all connections
        // free all memory
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

        while((conn->headers_read = !is_more_request(conn->buf))){
                if((bytes_read = (minet_read(c_sock, conn->bptr, BUFSIZE))) < 0){
                        if(errno == EAGAIN){
                                return;
                        }
                        perror("read");
                        exit(-1); // XXX not cleaning up =(
                }
                conn->bptr += bytes_read;
        }

        /* parse request to get file name */
        for(bptr = buf; (*bptr && *bptr == '/'); bptr++);

        if(!bptr){
                fprintf(stderr, "bad request. You forgot a \"/\" =(\n");
        }

        /* Assumption: this is a GET request and filename contains no spaces*/
        memset(conn->filename, '\0', FILENAMESIZE);
        bptr++;
        strcpy(conn->filename, buf);

        /* get file name and size, set to non-blocking */
        conn->ok = !stat(filename, &statbuf);

        conn->state = WRITING_RESPONSE;
        if(conn->ok){
                conn->filelen = statbuf.st_size;
                conn->fd = open(conn->filename, O_RDONLY);
                fnctl(conn->fd, O_NONBLOCK);
                conn->state = READING_FILE;
                return;
        }
}


void write_response(connection *conn){
        int sock2 = conn->sock;
        int rc;
        int written = conn->response_written;
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
                        snprintf(ok_response, OK_LENGTH, ok_response_f, conn->filelen);
                        response = ok_resoponse;
                }
        else
                {
                        response = notok_response;
                }
        while(conn->response_written < strlen(response)){
                if((rc = minet_write(conn->sock,
                                     response + conn->response_written,
                                     strlen(response)) < 0)){
                        if(errno == EAGAIN){
                                return;
                        }
                        else{
                                perror("write");
                                exit(-1); // XXX: better cleanup
                        }
                }
                if(rc == 0){
                        fprintf(stderr, "connection on socket %d was closed", conn->sock);
                        return;
                }

                conn->response_written += rc;
        }
        /* move state machine forward */
        if(ok){
                conn->state = READING_FILE;
        } else{
                conn->state = TO_CLOSE;
        }
}

void read_file(connection *conn) {
        int rc;

        /* send file */
        conn->bptr = file_read % BUFSIZE;
        while(conn->bptr < BUFSIZE && conn->file_read < conn->file_len){
                rc = read(conn->fd,conn->buf + bptr,BUFSIZE);
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
        while(conn->file_written % BUFSIZE < conn->bptr){
                int rc = minet_write(conn->sock, conn->buf+written, conn->file_read - conn->file_written);
                if (rc < 0){
                        if (errno == EAGAIN)
                                return;
                        minet_perror("error writing response ");
                        conn->state = TO_CLOSE;
                        minet_close(conn->sock);
                        return;
                }
                else if (rc > 0){
                        conn->file_written += rc;
                        written += rc;
                
        }
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

void init_connection(connection *conn)
{
        conn->fd = -1;
        conn->headers_read = 0;
        conn->response_written = 0;
        conn->file_read = 0;
        conn->file_written = 0;

        conn->state = READING_HEADERS;
        memset(conn->buf, '\0', BUFSIZE + 1);
        conn->bptr = buf;
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
