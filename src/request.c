#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)
#define BUFFERSIZE 50

pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_empty = PTHREAD_COND_INITIALIZER;;
pthread_cond_t buffer_full = PTHREAD_COND_INITIALIZER;;

// below default values are defined in 'request.h'
int num_threads = DEFAULT_THREADS;
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int scheduling_algo = DEFAULT_SCHED_ALGO;	

//
//	TODO: add code to create and manage the shared global buffer of requests
//	HINT: You will need synchronization primitives.
//		pthread_mutuex_t lock_var is a viable option.
// need a struct
// need array of requests

// creates struct for each request
typedef struct request_t {
    int fd;
    char filename[MAXBUF];
    int buffersize;
} request_t;

// array of requests
typedef struct {
    request_t buffer[BUFFERSIZE];
    int first;
    int last;
    int count;
} request_buffer_t;

// initializing buffer values
request_buffer_t buffer = {
    .first = 0,
    .last = 0,
    .count = 0
};



//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>CYB-3053 WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
	readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
	strcpy(filetype, "image/jpeg");
    else 
	strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread logic)
//
void* thread_request_serve_static(void* arg)
{
    // TODO: write code to actualy respond to HTTP requests
    // Pull from global buffer of requests

    
    while(1) {

        pthread_mutex_lock(&buffer_lock);
        
        // wait if empty
        while (buffer.count == 0) {
            pthread_cond_wait(&buffer_empty, &buffer_lock);
    }

    // store value of request tht will get chosen (starts in FIFO)
    int req_num = buffer.first;

    // SFF
    if (scheduling_algo == 1) {
        int smallest = buffer.first;
        for (int i = 1; i < buffer.count; i++) {
            int pos = (buffer.first + i) % BUFFERSIZE;
            if (buffer.buffer[pos].buffersize < buffer.buffer[smallest].buffersize) {
                smallest = pos;
            }
        }
        req_num = smallest;
    }

    // RANDOM
    if (scheduling_algo == 2) {
        int offset = rand() % buffer.count;
        req_num = (buffer.first + offset) % BUFFERSIZE;
    }

    request_t req = buffer.buffer[req_num];

    // fills gaps in buffer to stop it from breking
    if (req_num == buffer.first) {
        buffer.first = (buffer.first + 1) % BUFFERSIZE;
    } else {
        int i = req_num;
        while (i != buffer.last) {
            int next = (i + 1) % BUFFERSIZE;
            buffer.buffer[i] = buffer.buffer[next];
            i = next;
        }
        // put inside so it doesn't double decrement
        buffer.last = (buffer.last - 1 + BUFFERSIZE) % BUFFERSIZE;
    }

    // update buffer
    buffer.count--;

    // signal thread and unlock
    pthread_cond_signal(&buffer_full);
    pthread_mutex_unlock(&buffer_lock);


    struct stat sbuf;
    if (stat(req.filename, &sbuf) >= 0) {
        request_serve_static(req.fd, req.filename, sbuf.st_size);
    }

    close_or_die(req.fd);
}

}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
    // get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

    // verify if the request type is GET or not
    if (strcasecmp(method, "GET")) {
	request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
	return;
    }
    request_read_headers(fd);
    
    // check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
    // get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
	request_error(fd, filename, "404", "Not found", "server could not find this file");
	return;
    }
    
    // verify if requested content is static
    if (is_static) {
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
		request_error(fd, filename, "403", "Forbidden", "server could not read this file");
		return;
	}
    
	// TODO: directory traversal mitigation	

    // checks if ../ appears anywhere in request
    if (strstr(filename, "../")) {
            request_error(fd, filename, "403", "Forbidden", "Permission Denied");
            return;
    }

	// TODO: write code to add HTTP requests in the buffer

    pthread_mutex_lock(&buffer_lock);

    // wait if full
    while (buffer.count == 50) {
        pthread_cond_wait(&buffer_full, &buffer_lock);
    }

    request_t *req = &buffer.buffer[buffer.last];

    // adds to buffer
    req -> fd = fd;
    strncpy(req -> filename, filename, MAXBUF);
    req -> buffersize = sbuf.st_size;
    
    // update buffer
    buffer.last = (buffer.last + 1) % BUFFERSIZE;
    buffer.count++;

    // signal thread and unlock
    pthread_cond_signal(&buffer_empty);
    pthread_mutex_unlock(&buffer_lock);

    } else {
	request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
