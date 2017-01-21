#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>
#include "my_error.h"

#define BUFSIZE 1024
#define LENGTH_KEY "Content-Length: "
#define LENGTH_KEY_LEN sizeof(LENGTH_KEY)
#define END_HEADERS "\r\n\r\n"
#define END_HEADERS_LEN sizeof(END_HEADERS)
#define END_LENGTH "\r\n"
#define END_LENGTH_LEN sizeof(END_LENGTH)
#define READ_MORE_DATA -1
#define LENGTH_NOT_FOUND -2
#define HTTP_PROTOCOL "HTTP/1.0"

int write_n_bytes(int fd, char * buf, int count);
int read_n_bytes(int fd, char * buf, int count);
int check_status_line(char* buf);
void build_get_request(char *buf, char *server_path);
int parse_headers(char *buf, int *buf_pos, int datalen);
int read_length(char* buf, int* bodylen_acc, int* buf_pos, int datalen);
void flush_buffer(FILE* wheretoprint, char * buf, int* buf_pos, int* datalen);

/* TODOS:
 * Do I need to fix the byte order on what I get back from read?
 * Surely not... How can I know where the data boundaries are?
 */

int main(int argc, char * argv[]) {
        char * server_name = NULL;
        int server_port = 0;
        char * server_path = NULL;

        int single_read_bytes = 0;
        int body_bytes_read = 0;
        int sock = 0;
        int datalen = 0;
        int bodylen = 0;
        int bodylen_acc = 0;
        int length_pos = 0;
        int print_off = 0;
        int found_response_end = 0;
        struct sockaddr_in sa;
        struct hostent *site;
        FILE * wheretoprint = stdout;
        int status = -1;
        int rc = 0;
        int buf_pos = 0;
        struct timeval timeout;
        fd_set set;

        char buf[BUFSIZE + 1];
        buf[BUFSIZE] = NULL;

        /*parse args */
        if (argc != 5) {
                fprintf(stderr, "usage: http_client k|u server port path\n");
                exit(-1);}

        server_name = argv[2];
        server_port = atoi(argv[3]);
        server_path = argv[4];


        /* initialize minet */
        if(toupper(*(argv[1])) == 'K') {
                minet_init(MINET_KERNEL);
        } else if (toupper(*(argv[1])) == 'U'){
                minet_init(MINET_USER);
        } else{
                fprintf(stderr, "First argument must be k or u\n");
                exit(-1);
        }
        if((sock = minet_socket(SOCK_STREAM)) < 0){
                my_error_at_line(sock, 0, "minet_socket", __FILE__, __LINE__);
                goto bad;
        }


        // Do DNS lookup
        /* Hint: use gethostbyname() */
        if((site = gethostbyname(server_name)) == 0){
                status = errno;
                my_error_at_line(status, 1, "gethostbyname", __FILE__, __LINE__);
                goto bad;
        }

        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(server_port);
        sa.sin_addr.s_addr = * ((unsigned long *) site->h_addr_list[0]);


        /* connect socket */
        if((status = minet_connect(sock, &sa)) < 0){
                status = errno;
                my_error_at_line(status, 1, "minet_connect", __FILE__, __LINE__);
                exit(-1); // no need to close socket, never allocated.
        }

        // Wait for socket to be writable
        FD_ZERO(&set);
        FD_SET(sock, &set);

        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        do {
                status = minet_select(sock + 1, NULL, &set, NULL, &timeout);
                if(status < 0){
                        my_error_at_line(status, 1, "minet_select", __FILE__, __LINE__);
                        goto bad;
                }
                if(status == 0){
                        printf("Select timed out at line %d\n", __LINE__);
                        goto bad;
                }
        } while(!FD_ISSET(sock, &set));


        // Build and send request
        build_get_request(buf, server_path);
        if((minet_write(sock, buf, strlen(buf))) < 0){
                my_error_at_line(status, 1, "minet_write", __FILE__, __LINE__);
                goto bad;}

        /* wait till socket can be read */
        /* Hint: use select(), and ignore timeout for now. */
        FD_ZERO(&set);
        FD_SET(sock, &set);


        do {
                status = minet_select(sock + 1, &set, NULL, NULL, &timeout);
                if(status < 0){
                        my_error_at_line(status, 1, "minet_write", __FILE__, __LINE__);
                        goto bad;
                }
                if(status == 0){
                        printf("Select timed out at line: %d\n", __LINE__ - 1);
                        goto bad;
                }
        } while(!FD_ISSET(sock, &set));


        // get to headers
        buf_pos = 1; // offset for headers
        datalen = 0;
        found_response_end = 0;
        while(1 && !found_response_end){
                datalen += read_n_bytes(sock, buf, BUFSIZE - datalen);

                while(datalen >= 2 && buf_pos <  datalen && !found_response_end){
                        if((buf[buf_pos - 1] == '\r' &&
                            buf[buf_pos]     == '\n')){
                                found_response_end = 1;
                        }
                        buf_pos++;
                }
        }
        if(check_status_line(buf) < 0){
                wheretoprint = stderr;
                rc = -1;
        }

        // move buf_pos to beginning of first header line
        buf_pos++;
        length_pos = READ_MORE_DATA;
        // parse headers
        while(1 &&
              length_pos == READ_MORE_DATA){

                datalen += read_n_bytes(sock, buf + datalen, BUFSIZE - datalen);

                if(datalen >= (int) LENGTH_KEY_LEN){
                        length_pos = parse_headers(buf, &buf_pos, datalen);
                }

                if(datalen > BUFSIZE && length_pos < 0){
                        flush_buffer(wheretoprint, buf, &buf_pos, &datalen);
                }
        }

        if(length_pos == LENGTH_NOT_FOUND){
                my_error_at_line(length_pos, 0, "Didn't find length", __FILE__, __LINE__);
                goto bad; // XXX: What should I do here?
        }
        // parse in length field
        else{
                bodylen_acc = 0;
                bodylen = READ_MORE_DATA;
                buf_pos = length_pos + 1;
                while(1 &&
                      bodylen == READ_MORE_DATA){
                        datalen += read_n_bytes(sock, buf + datalen, BUFSIZE - datalen);

                        bodylen = read_length(buf, &bodylen_acc, &buf_pos, datalen);

                        if(datalen >= BUFSIZE && buf_pos == datalen){
                                flush_buffer(wheretoprint, buf, &buf_pos, &datalen);
                        }
                }
        }

        for(;strncmp(END_HEADERS, buf + buf_pos, END_HEADERS_LEN - 1); buf_pos++);
        buf_pos += END_HEADERS_LEN - 1;

        bodylen -= (datalen - buf_pos);

        print_off = (rc < 0) ? 0 : buf_pos;

        flush_buffer(wheretoprint, buf + print_off, &buf_pos, &datalen);
        memset(buf, 0, BUFSIZE);

        while(bodylen > 0){
                single_read_bytes = read_n_bytes(sock,
                                                 buf + datalen,
                                                 (bodylen >= BUFSIZE) ?
                                                 BUFSIZE - datalen:
                                                 bodylen - datalen);
                bodylen -= single_read_bytes;
                datalen += single_read_bytes;
                if(datalen >= BUFSIZE){
                        flush_buffer(wheretoprint, buf, &buf_pos, &datalen);
                        memset(buf, 0, BUFSIZE);
                }
        }
        flush_buffer(wheretoprint, buf, &buf_pos, &datalen);


        /*close socket and deinitialize */
        minet_close(sock);

	minet_deinit();

        return rc;

 bad:
        minet_close(sock);
	minet_deinit();
        return -1;
}

int read_length(char* buf, int* bodylen_acc, int* buf_pos, int datalen){
        while(1){
                if(*buf_pos >= datalen){
                        return READ_MORE_DATA;
                }

                *bodylen_acc = (*bodylen_acc) * 10 + ((int) (buf[*buf_pos] - '0'));
                (*buf_pos)++;

                if(!strncmp(END_LENGTH, buf + *buf_pos, END_LENGTH_LEN - 1)){
                        return *bodylen_acc;
                }
        }
}

void flush_buffer(FILE* wheretoprint, char * buf, int* buf_pos, int* datalen){
        fprintf(wheretoprint, "%s", buf);
        *buf_pos = 0;
        *datalen = 0;
}

// Loads a get request into buf for server_path on port_no
// request is null terminated.
void build_get_request(char *buf, char *server_path){
        memset(buf, '\0', BUFSIZE + 1);
        strcat(buf, "GET ");

        strcat(buf, server_path);
        strcat(buf, " ");

        strcat(buf, HTTP_PROTOCOL);

        strcat(buf, "\r\n\r\n");
}


#define STATUS_LENGTH 4
#define HTTP_SUCCESS 200
int check_status_line(char* buf){
        char status_buf[STATUS_LENGTH];
        memset(status_buf, '\0', STATUS_LENGTH);

        int i = 0;
        for(; buf[i] != ' '; i++);
        i += 1; //gets us onto the status code
        for(int j = 0; j < STATUS_LENGTH - 1; j++){ // preserve null byte
                status_buf[j] = buf[i++];
        }

        if(atoi(status_buf) == HTTP_SUCCESS){
                return 0;
        }else{
                return -1;
        }

}

// returns the offset to the first byte after a crlf sequence
int parse_headers(char *buf, int *buf_pos, int datalen){
        *buf_pos = (*buf_pos < (int) LENGTH_KEY_LEN) ?
                LENGTH_KEY_LEN :
                *buf_pos;

        while(*buf_pos < datalen){

                if(!strncmp(END_HEADERS, buf + *buf_pos - (END_HEADERS_LEN - 2), END_HEADERS_LEN - 1)){
                        return LENGTH_NOT_FOUND;
                }
                (*buf_pos)++;
                if(!strncmp(LENGTH_KEY, buf + *buf_pos - (sizeof(LENGTH_KEY) - 2), sizeof(LENGTH_KEY) - 1)){
                        return *buf_pos;
                }

        }

        return READ_MORE_DATA;
}

int io_n_bytes(int fd, char * buf, int count, int (*minet_io_fun)(int, char*, int)){
        int rc = 0;
        int totalio = 0;
        while((rc = (*minet_io_fun)(fd, buf + totalio, count - totalio)) > 0){
                totalio += rc;
        }
        if(rc < 0)
                return -1;
        else {
                return totalio;
        }
}


int write_n_bytes(int fd, char *buf, int count){
        return io_n_bytes(fd, buf, count, minet_write);
}

int read_n_bytes(int fd, char *buf, int count){
        return io_n_bytes(fd, buf, count, minet_read);
}
