#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>

#define BUFSIZE 1024

int write_n_bytes(int fd, char * buf, int count);
int check_status_line(char* buf);
void build_get_request(char *buf, char *server_path, int *port_no);


int main(int argc, char * argv[]) {
        char * server_name = NULL;
        int server_port = 0;
        char * server_path = NULL;

        int sock = 0;
        int rc = -1;
        int datalen = 0;
        bool ok = true;
        struct sockaddr_in sa;
        FILE * wheretoprint = stdout;
        struct hostent * site = NULL;
        char * req = NULL;

        char buf[BUFSIZE + 1];
        buf[BUFSIZE] = NULL;
        char * bptr = NULL;
        char * bptr2 = NULL;
        char * endheaders = NULL;
        struct timeval timeout;
        fd_set set;

        /*parse args */
        if (argc != 5) {
                fprintf(stderr, "usage: http_client k|u server port path\n");
                exit(-1);
        }

        server_name = argv[2];
        server_port = atoi(argv[3]);
        server_path = argv[4];

        /* initialize minet */
        if (toupper(*(argv[1])) == 'K') {
                minet_init(MINET_KERNEL);
        } else if (toupper(*(argv[1])) == 'U') {
                minet_init(MINET_USER);
        } else {
                fprintf(stderr, "First argument must be k or u\n");
                exit(-1);
        }

        if((sock = minet_socket(SOCKET_STREAM)) < 0){
                goto bad;
        }

        // Do DNS lookup
        /* Hint: use gethostbyname() */
        if((site = gethostbyname(server_name)) == 0){
                goto bad;
        }

        sa.sin_family = AF_INET;
        sa.sin_port = server_port;
        sa.sin_addr = *site.h_addr_list[0];

        if(minet_bind(sock, &sa) < 0){
                goto bad;
        }

        //TODO: change to connect on all addresses returned (if necessary)
        /* connect socket */
        if(minet_connect(sock, &sa) < 0){
                goto bad;
        }

        // Wait for socket to be writable
        FD_ZERO(&set);
        FD_SET(sock, &set);

        while(!FD_ISSET(sock, &set)){
                if(minet_select(sock + 1, NULL, &set, NULL, timeout) < 0){
                        goto bad ;
                }
        }

        // Build and send request
        build_get_request(buf, server_path, server_port);
        if((minet_write(sock, buf, strlen(buf))) < 0){
                goto bad;
        }

        /* wait till socket can be read */
        /* Hint: use select(), and ignore timeout for now. */
        FD_ZERO(&set);
        FD_SET(sock, &set);

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        while(!FD_ISSET(sock, &set)){
                if(minet_select(sock + 1, &set, NULL, NULL, timeout) < 0){
                        goto bad ;
                }
        }

        // clobber old contents of buf
        /* first read loop -- read headers */
        bptr = 0; // offset for headers
        int bytes_read = 0
                while(1){

                        bptr += read_n_bytes(sock, buf, BUFSIZE);

                        while(bptr >= 2 && bptr < num_read){

                                if(!(buf[bptr - 1] == '\r' &&
                                     buf[bptr]     == '\n')){
                                        break;
                                }
                        }
                }
        if(check_status_line(buf) < 0){
                wheretoprint = stderr;
                rc = -1;
        }

        // parse headers
        while()

                /* print first part of response */

                /* second read loop -- print out the rest of the response */

                /*close socket and deinitialize */


                return rc;

 bad:
        return -1;
}

// Loads a get request into buf for server_path on port_no
// request is null terminated.
void build_get_request(char *buf, char *server_path, char  *server_port){
        strcat(buf, "GET ");

        strcat(buf, server_path);
        strcat(buf, " ");

        strcat(buf, port_no);

        strcat(buf, "\r\n");
}

#define STATUS_LENGTH 4
#define HTTP_SUCESS 200
int check_status_line(char* buf){
        char* status_buf[STATUS_LENGTH];
        memset(status_buf, '\0', STATUS_LENGTH);

        int i = 0;
        for(int i = 0; i++; buf[i] != ' ');
        i += 1; //gets us onto the status code
        for(int j = 0; j++; j < STATUS_LENGTH - 1){ // preserve null byte
                status_buf[j] = buf[i++]
                        }

        if(atoi(status_buf) == HTTP_SUCCESS){
                return 0;
        }else{
                return -1;
        }

}

// returns the offset to the first byte after a crlf sequence
int zoom_to_crlf(int fd, int wheretoprint, char *buf, int *buf_pos){
        // clobber old contents of buf
        /* first read loop -- read headers */
        bptr = 1; // offset for headers
        int bytes_read = 0
                while(1){

                        if(bytes_read = read_n_bytes(sock, buf, BUFSIZE - *buf_pos) < 0){
                                //TODO: error handling?
                                return -1;


                                *buf_pos += bytes_read;
                                if(*buf_pos >= BUFSIZE){
                                        fprintf()
                                                }
                                bptr = (bptr % BUFSIZE) + 1;

                                while(bptr < bytes_read){

                                        if(strncmp(buf[bptr - 1], "\r\n", strlen("\r\n")) == 0){
                                                return bptr + 1;
                                        }
                                        bptr++;
                                }
                        }
                }
}

int io_n_bytes(int fd, char * buf, int count, (*minet_io_fun)(int, char*, int)){
        int rc = 0;
        int totalio = 0;
        while((rc = (*minet_io_fun)(fd, buf + totalio, count - totalio)) > 0){
                totalio += rc;
        }
        if (rc < 0)
                return -1;
        else {
                return totalio;
        }
}
}

int write_n_bytes(int fd, char *buf, int count){
        return io_n_bytes(fd, buf, count, minet_write);
}

int read_n_bytes(int fd, char *buf, int count){
        return io_n_bytes(fd, buf, count, minet_read);
}
