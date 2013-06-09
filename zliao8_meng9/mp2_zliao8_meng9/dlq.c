/** @file server.c */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h> 

#include "queue.h"
#include "./libs/libhttp.h"
#include "./libs/libdictionary.h"

// global variables
int exit_flag;
struct addrinfo *res;
int server_sock;
queue_t *clients;
queue_t *pids;



const char *HTTP_404_CONTENT = "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1>The requested resource could not be found but may be available again in the future.<div style=\"color: #eeeeee; font-size: 8pt;\">Actually, it probably won't ever be available unless this is showing up because of a bug in your program. :(</div></html>";
const char *HTTP_501_CONTENT = "<html><head><title>501 Not Implemented</title></head><body><h1>501 Not Implemented</h1>The server either does not recognise the request method, or it lacks the ability to fulfill the request.</body></html>";

const char *HTTP_200_STRING = "OK";
const char *HTTP_404_STRING = "Not Found";
const char *HTTP_501_STRING = "Not Implemented";

char* process_http_header_request(const char *request)
{
	//fprintf(stderr, "request is %s\n", request);
	// Ensure our request type is correct...
	if (strncmp(request, "GET ", 4) != 0)
		return NULL;
    
	// Ensure the function was called properly...
	assert( strstr(request, "\r") == NULL );
	assert( strstr(request, "\n") == NULL );
    
	// Find the length, minus "GET "(4) and " HTTP/1.1"(9)...
	int len = strlen(request) - 4 - 9;
    
	// Copy the filename portion to our new string...
	char *filename = malloc(len + 1);
	strncpy(filename, request + 4, len);
	filename[len] = '\0';
    
	// Prevent a directory attack...
	//  (You don't want someone to go to http://server:1234/../server.c to view your source code.)
	if (strstr(filename, ".."))
	{
		free(filename);
		return NULL;
	}
    
	return filename;
}

void handler(int sig){
    
	exit_flag = 1;
	close(server_sock);
    
	int *ptrc = NULL;
	while((ptrc = queue_dequeue(clients)) != NULL){
		if(*ptrc != -1){
			shutdown(*ptrc, SHUT_RDWR);
			//close(*ptrc);
		}
		free(ptrc);
	}
    
	pthread_t *ptrp = NULL;
	while((ptrp = queue_dequeue(pids)) != NULL){
		pthread_join(*ptrp, NULL);
		free(ptrp);
	}
    
	free(clients);
	free(pids);
	freeaddrinfo(res);
	sig = 0;
}


void *worker(void *ptr){
    
	/*
 	 *  Reading the HTTP Header
 	 *
 	 */
	int *socket = (int*)ptr;
	fd_set master;
	fd_set slave;
	FD_ZERO(&master);
	FD_SET(*socket, &master);
    
	while(1){
		slave = master;
		select(*socket+1, &slave, NULL, NULL, NULL);
        
		if(exit_flag == 1) break;
        
		http_t *new = malloc(sizeof(http_t));
        
		if(http_read(new, *socket) <= 0){
			printf("No HTTP request could be processed... \n");
			http_free(new);
			free(new);
			break;
		}
        
		char *fptr = process_http_header_request(http_get_status(new));
		int response_code;
		char *response_header = malloc(1024);;
		char *content_type = malloc(32);
		char *content_length = malloc(256);
		char *connection = malloc(64);
		int con_flag = 0;
        
		if(fptr == NULL){
            
			// 501 response
			response_code = 501;
			// header
			sprintf(response_header, "HTTP/1.1 %d %s\r\n", response_code, (char*)HTTP_501_STRING);
			// content_type
			sprintf(content_type, "Content-Type: text/html\r\n");
			strcat(response_header, content_type);
			// content_length
			sprintf(content_length, "Content-Length: %d\r\n", (int)strlen((char*)HTTP_501_CONTENT));
			strcat(response_header, content_length);
			// connection
			const char* con = http_get_header(new, "Connection");
			if(strcasecmp(con, "Keep-Alive") == 0){
				sprintf(connection, "Connection: Keep-Alive\r\n");
			}else{
				sprintf(connection, "Connection: close\r\n");
				con_flag = 1;
			}
			strcat(response_header, connection);
			strcat(response_header, "\r\n");
            
			// communicating over sockets
			// send(socket descriptor you want to send data to,
			// 	a pointer to the data you want to send,
			// 	length of that data in bytes,
			// 	just set flags to 0)
			// return number of bytes actually sent out
			//fprintf(stderr, "\n\n%s\n", response_header);
			send(*socket, (const void*)response_header, strlen(response_header), 0);
			send(*socket, HTTP_501_CONTENT, strlen(HTTP_501_CONTENT), 0);
			
			
		}else{
			// get correct path
			char *fdir = malloc(256);
			strcpy(fdir, "web/");
			if(strcmp(fptr, "/") == 0){
				// process as /index.html
				strcat(fdir, "index.html");
			}else{
				strcat(fdir, fptr);
			}
            
			// fopen call under the web directory
			// return 404 response if not exist
			// if exist return entire contents of the file (200 response)
            
			//if(f == NULL){
			struct stat FileAttrib;
			FILE *f = fopen(fdir, "r");
			if(f == NULL){
				// 404 response code
				response_code = 404;
				// header
				sprintf(response_header, "HTTP/1.1 %d %s\r\n", response_code, (char*)HTTP_404_STRING);
				// content_type
				sprintf(content_type, "Content-Type: text/html\r\n");
				strcat(response_header, content_type);
				// content_length
				sprintf(content_length, "Content-Length: %d\r\n", (int)strlen((char*)HTTP_404_CONTENT));
				strcat(response_header, content_length);
				// connection
				const char* con = http_get_header(new, "Connection");
				if(strcasecmp(con, "Keep-Alive") == 0){
					sprintf(connection, "Connection: Keep-Alive\r\n");
				}else{
					sprintf(connection, "Connection: close\r\n");
					con_flag = 1;
				}
				strcat(response_header, connection);
				strcat(response_header, "\r\n");
                
				// send
				//fprintf(stderr, "\n\n%s\n", response_header);
				send(*socket, (const void*)response_header, strlen(response_header), 0);
				send(*socket, HTTP_404_CONTENT, strlen(HTTP_404_CONTENT), 0);
                
			}else{
				// 200 response
				response_code = 200;
				stat(fdir, &FileAttrib);
				
				// header
				sprintf(response_header, "HTTP/1.1 %d %s\r\n", response_code, (char*)HTTP_200_STRING);
				// content_typerver_sock, res->ai_addr, res->ai_addrlen) == -1){
				//                 per
				get_content_type(content_type, fdir);
				strcat(response_header, content_type);
				// content_length
				//sprintf(content_length, "Content-Length: %d\r\n", (int)strlen((char*)HTTP_200_STRING));
				sprintf(content_length, "Content-Length: %jd\r\n", (intmax_t)FileAttrib.st_size);
				strcat(response_header, content_length);
				// connection
				const char* con = http_get_header(new, "Connection");
				if(strcasecmp(con, "Keep-Alive") == 0){
					sprintf(connection, "Connection: Keep-Alive\r\n");
				}else{
					sprintf(connection, "Connection: close\r\n");
					con_flag = 1;
				}
				strcat(response_header, connection);
				strcat(response_header, "\r\n");
                
				// send files
				send(*socket, (const void*)response_header, strlen(response_header), 0);
                
				size_t body_size = (intmax_t)FileAttrib.st_size;
				//fprintf(stderr, "\n\n%s\n", response_header);
				char *response_body = malloc(body_size);
				response_body[0] = '\0';
                
				fread(response_body, body_size, 1, f);
                
				send(*socket, (const void*)response_body, body_size, 0);
				free(response_body);
				fclose(f);
			}
			free(fdir);
            
		}
		
		free(response_header);
		free(content_length);
		free(connection);
		free(content_type);
		free(fptr);
		http_free(new);
		free(new);
		if(con_flag){
			break;
		}
        
	}
    
	return NULL;
    
}


void *server(void *ptr){
    
    char *port = (char*)ptr;
    
    struct addrinfo hints;
    
    /*
     clients = malloc(sizeof(queue_t));
     pids = malloc(sizeof(queue_t));
     queue_init(clients);
     queue_init(pids);
     exit_flag = 0;
     */
    
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
    
	/* getaddrinfo(host name or IP address,
     port number or the name of a particular service(http, ftp, telnet, or smtp),
     already filled out struct addrinfo,
     struct addrinfo gonna to be filled out) */
	if(getaddrinfo(NULL, port, &hints, &res)){
		perror("getaddrinfo");
		return 0;
	}
    
	/* get the file descriptor:
     socket(IPv4 or IPv6, stream or datagram, TCP or UDP) */
	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(server_sock < 0){
		perror("socket");
		return 0;
	}
    
	/* bind socket to the port number:
     bind(socket file descriptor, pointer to the port and IP address, length of that address); */
	if(bind(server_sock, res->ai_addr, res->ai_addrlen) == -1){
		perror("bind");
		return 0;
	}
    
	/* wait for incoming connections
     listen(socket file descriptor from socket(), number of connections allowed on the incoming queue) */
	if(listen(server_sock, 20) == -1){
		perror("listen");
		return 0;
	}
    
	signal(SIGINT, handler);
	fd_set master;
	fd_set slave;
	FD_ZERO(&master);
	FD_SET(server_sock, &master);
    
    while(1){
		slave = master;
		if(select(server_sock+1, &slave, NULL, NULL, NULL) < 0){
			perror("select");
			break;
		}
		if (exit_flag == 1) break;
        
		int *client_socket = malloc(sizeof(int));
		*client_socket = 0;
		queue_enqueue(clients, client_socket);
        
		/* return a brand new socket file descriptor to use
         accept(listening socket descriptor,
         pointer to a local struct sockaddr_storage which stores the information about the incoming connection,
         local integer variable that set to sizeof(struct sockaddr_storage))*/
		if( (*client_socket = accept(server_sock, NULL, NULL)) < 0){
			perror("accept");
			return 0;
		}else{
            pthread_t *p = malloc(sizeof(pthread_t));
			queue_enqueue(pids, p);
			int rc = pthread_create(p, NULL, worker, (void *)client_socket);
			if (rc){
				fprintf(stderr, "---ERROR; pthread_create failed, return code is %d\n", rc);
				exit(-1);
			}
		}
	}
    
}


int start_server(char *port){
    
    pthread_t *p = malloc(sizeof(pthread_t));
    int rc = pthread_create(p, NULL, server, (void *)port);
    if (rc){
        fprintf(stderr, "---ERROR; pthread_create failed, return code is %d\n", rc);
        exit(-1);
    }
    
    return 1;
}


int main(int argc, char **argv)
{
    
    
    /*
     *  User Interface
     *
     *  1. Displaying a menu for user to start running the program
     *
     */
    fprintf(stderr, "\n|***************************************************|\n");
    fprintf(stderr, "|******        Distributed Log Querier        ******|\n");
    fprintf(stderr, "|---------------------------------------------------|\n");
    fprintf(stderr, "|===          Zihan Liao & Qinglei Meng          ===|\n");
    fprintf(stderr, "|===                06/08/2013                   ===|\n");
    fprintf(stderr, "|===          CS425 Distributed Systems          ===|\n");
    fprintf(stderr, "|===                   MP2                       ===|\n");
    fprintf(stderr, "|===  University of Illinois at Urbana-Champaign ===|\n");
    fprintf(stderr, "|***************************************************|\n\n");
    fprintf(stderr, "|***************************************************|\n");
    fprintf(stderr, "|-------------------    Menu   ---------------------|\n");
    fprintf(stderr, "|-------  1. Mannual                        --------|\n");
    fprintf(stderr, "|-------  2. Start local server             --------|\n");
    fprintf(stderr, "|-------  3. Generating local log file      --------|\n");
    fprintf(stderr, "|-------  4. Grep                           --------|\n");
    fprintf(stderr, "|-------  5. Exit                           --------|\n");
    fprintf(stderr, "|***************************************************|\n");
    
    while (1) {
        fprintf(stderr, "\n$ Please choose (1-5) from the menu: ");
        char in[64];
        fgets(in, 64, stdin);
        int choice = atoi(in);
        
        if (choice == 1) {
            fprintf(stderr, "your choice is 1\n");
        }else if(choice == 2){
            fprintf(stderr, "your choice is 2\n");
            fprintf(stderr, "\n-- select a port number for the server: ");
            char *port_in = malloc(32);
            fgets(port_in, 32, stdin);
            int port = atoi(port_in);
            if(port <= 0 || port >= 65536){
                fprintf(stderr, "-- Illegal port number.\n");
            }else{
                if(start_server(port_in)){
                    fprintf(stderr, "-- Congratulations! The server has been started!\n");
                }else{
                    fprintf(stderr, "-- Sorry! Starting server failed, please try again!\n");
                }
            }
        }else if(choice == 3){
            fprintf(stderr, "your choice is 3\n");
        }else if(choice == 4){
            fprintf(stderr, "your choice is 4\n");
        }else if(choice == 5){
            fprintf(stderr, "your choice is 5\n");
            exit(0);
        }else{
            fprintf(stderr, "-- error, not in the choice range\n");
        }
        
    }
    
    /*
     *  Incoming message handler
     *
     *  1. To start the program, run ./mp2 [port number]
     *  2. The program will create a new thread to run a server and
     *      listen to incoming HTTP requests
     *  3. The server will create a worker thread to handle each
     *      incoming requests, run the commands and send back data
     *
     */
    
    if(argc != 2){
		fprintf(stderr, "Usage: %s [port number]\n", argv[0]);
		return 1;
	}
    
	int port = atoi(argv[1]);
	if(port <= 0 || port >= 65536){
		fprintf(stderr, "Illegal port number.\n");
		return 1;
	}
    
    /*pthread_t *p = malloc(sizeof(pthread_t));
    int rc = pthread_create(p, NULL, server, (void *)&port);
    if (rc){
        fprintf(stderr, "---ERROR; pthread_create failed, return code is %d\n", rc);
        exit(-1);
    }*/
    


    
    
    /*
     *  Terminal monitor
     *
     *  1. Monitoring new commands on the terminal
     *  2. Giving corresponding responses or starting to run the Querier
     *
     */
    
    
    /*
     *  Querier
     *
     *  1. It will handle the new commands and send requests to other
     *      distributed machines
     *  2. It waits for data from other machines
     *
     */
    
    /*
     *  Output processor
     *
     *  1. After successfully getting messages from other distributed 
     *      machines, it processes data and prints it out
     *
     */




	return 0;
}



