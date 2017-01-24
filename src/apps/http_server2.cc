#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100
#define BACKLOG 1
#define REQUEST_END "\r\n\r\n"
#define REQUEST_END_LEN sizeof(REQUEST_END)

int handle_connection(int);
bool is_more_request(char* buf);
int writenbytes(int,char *,int);
int readnbytes(int,char *,int);

int main(int argc,char *argv[])
{
        int server_port;
        int listening_sock,new_sock;
        struct sockaddr_in sa;
        char stack;
        int rc,i;
        fd_set readlist, master_set;
        int maxfd;
        struct timeval timeout;

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

        /* Make socket */
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

        FD_ZERO(&master_set);

        FD_SET(listening_sock, &master_set);

        maxfd = listening_sock;

        /* connection handling loop */
        while(1)
                {
                        /* create read list */
                        FD_ZERO(&readlist);
                        readlist = master_set;

                        /* do a select */
                        /* null timeout will block if nothing can read */
                        if((select(maxfd + 1, &readlist, NULL, NULL, NULL)) < 0){
                                perror("select");
                                goto bad;
                        }

                        /* process sockets that are ready */
                        /* for the accept socket, add accepted connection to connections */
                        for(i = 0; i <= maxfd; i++){
                                if(!FD_ISSET(i, &readlist)) continue;

                                if (i == listening_sock)
                                        {
                                                new_sock = minet_accept(listening_sock, &sa);
                                                maxfd = (new_sock > maxfd) ? new_sock : maxfd;
                                                FD_SET(new_sock, &master_set);
                                        }
                                else /* for a connection socket, handle the connection */
                                        {
                                                FD_CLR(i, &master_set);
                                                rc = handle_connection(i);
                                        }
                        }
                }
 bad:
        minet_close(listening_sock);
        minet_deinit();
}

#define OK_LENGTH 100
int handle_connection(int c_sock){
        char filename[FILENAMESIZE+1];
        int fd;
        struct stat filestat;
        char buf[BUFSIZE+1];
        char *headers; 
        char *endheaders;
        char *bptr;
        int datalen=0;
        int status = 0;
        int bytes_sent = 0;
        int bytes_to_send = 0;
        int bytes_read = 0;
        int more_request = true;

        char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                "Content-type: text/plain\r\n"\
                "Content-length: %d \r\n\r\n";
        char ok_response[OK_LENGTH];
        char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                "Content-type: text/html\r\n\r\n"\
                "<html><body bgColor=black text=white>\n"\
                "<h2>404 FILE NOT FOUND</h2>\n"
                "</body></html>\n";
        bool ok=true;

        memset(ok_response, '\0',  OK_LENGTH);

        /* parse request to get file name */
        /* Assumption: this is a GET request and filename contains no spaces*/
        memset(ok_response, '\0',  OK_LENGTH);
        memset(buf, '\0', BUFSIZE);

        /* parse request to get file name */
        /* Assumption: this is a GET request and filename contains no spaces*/
        bptr = buf;
        while(more_request){
                if((bytes_read = (minet_read(c_sock, bptr, BUFSIZE))) < 0){
                        perror("read");
                        return -1;
                }
                bptr += bytes_read;
                more_request = is_more_request(buf);
        }


        bptr = buf;
        for(;*bptr != '/'; bptr++);

        bptr++; // point to first char of filename

        memset(filename, '\0', FILENAMESIZE + 1);
        for(int i = 0; *bptr != ' '; bptr++, i++){
                filename[i] = *bptr;
        }


        /* get file length if exists */
        ok = !stat(filename, &filestat);


        /* send response */
        if (ok){
                datalen = filestat.st_size;
                /* format response */
                snprintf(ok_response, OK_LENGTH, ok_response_f, datalen);

                /* send headers */
                minet_write(c_sock, ok_response, strlen(ok_response));
                /* send file */
                if((fd = open(filename, O_RDONLY)) < 0){
                        return -1;
                }
                // reading loop
                while(datalen > 0){
                        if((bytes_to_send = read(fd,
                                                 buf,
                                                 (datalen < BUFSIZE)?
                                                 datalen :
                                                 BUFSIZE)) < 0){
                                return -1;
                        }
                        datalen -= bytes_to_send;
                        // sending loop
                        while(bytes_to_send > 0){
                                if((bytes_sent = minet_write(c_sock, buf, strlen(buf))) < 0){
                                        return -1;
                                }
                                bytes_to_send -= bytes_sent;
                        }
                        memset(buf, '\0', BUFSIZE);
                }
        }
        else // send error response
                {
                        if(minet_write(c_sock, notok_response, strlen(buf)) < 0){
                                return -1;
                        }
                }

        /* close socket and free space */
        shutdown(c_sock, SHUT_RDWR);
        minet_close(c_sock);
        if (ok)
                return 0;
        else
                return -1;
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
