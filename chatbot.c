/*
 * echoserverts.c - A concurrent echo server using threads
 * and a message buffer.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAXUSERS 100
#define MAXROOMS 100

 /* Simplifies calls to bind(), connect(), and accept() */
typedef struct sockaddr SA;

/* Max text line length */
#define MAXLINE 8192

/* Second argument to listen() */
#define LISTENQ 1024

// We will use this as a simple circular buffer of incoming messages.
char message_buf[20][50];

// This is an index into the message buffer.
int msgi = 0;

// A lock for the message buffer.
pthread_mutex_t lock;

//Room list
struct Room* roomList[MAXROOMS];

//roomlist index
int roomi = 0;

struct User {
	char nickname[30];
	int sockfd;
};

struct Room {
	char name[30];
	struct User * user_list[MAXUSERS];
	int users; 
};

/*struct RoomList {
	struct Room * room_list[MAXROOMS];
	int roomi;
};*/

struct Room *create_room(char *name) {
	struct Room *newRoom = malloc(sizeof(struct Room));
	strcpy(newRoom->name, name);
	newRoom->users = 0;
	roomList[roomi++] = newRoom;
	return newRoom;
}

struct User *create_user(char *name, int connfd, struct Room* room) {
	struct User *newUser = malloc(sizeof(struct User));
	strcpy(newUser->nickname, name);
	newUser->sockfd = connfd;
	room->user_list[room->users] = newUser;
	room->users++;
	return newUser;
}

int find_room_index(int connfd) {
	for(int i = 0; i < roomi; i++){
		for(int j = 0; j < roomList[i]->users; j++){
			if(roomList[i]->user_list[j]->sockfd == connfd){
				return i;
			}
		}
	} return -1;
}

int contains_user(int connfd, struct Room *room, char* name) {
	for(int i = 0; i < roomi; i++) {
		for (int i = 0; i < room->users; i++) {
			if (room->user_list[i]->sockfd == connfd) {
				return 1;
			}
		}
	}
	return -1;
}

//returns 1 on successful delete, -1 otherwise
int delete_user(int connfd, struct Room *room) {
	for (int i = 0; i < room->users; i++) {
		if (room->user_list[i]->sockfd == connfd) {
			room->user_list[i] = room->user_list[room->users - 1];
			room->users--;
			return 1;
		}
	}
	return -1;
}

// Initialize the message buffer to empty strings.
void init_message_buf() {
	int i;
	for (i = 0; i < 20; i++) {
		strcpy(message_buf[i], "");
	}
}

// This function adds a message that was received to the message buffer.
// Notice the lock around the message buffer.
void add_message(char *buf) {
	pthread_mutex_lock(&lock);
	strncpy(message_buf[msgi % 20], buf, 50);
	int len = strlen(message_buf[msgi % 20]);
	message_buf[msgi % 20][len] = '\0';
	msgi++;
	pthread_mutex_unlock(&lock);
}

// Destructively modify string to be upper case
void upper_case(char *s) {
	while (*s) {
		*s = toupper(*s);
		s++;
	}
}

// A wrapper around recv to simplify calls.
int receive_message(int connfd, char *message) {
	return recv(connfd, message, MAXLINE, 0);
}

// A wrapper around send to simplify calls.
int send_message(int connfd, char *message) {
	return send(connfd, message, strlen(message), 0);
}

// A predicate function to test incoming message.
int is_list_message(char *message) { return strncmp(message, "-", 1) == 0; }

int send_list_message(int connfd) {
	char message[20 * 50] = "";
	for (int i = 0; i < 20; i++) {
		if (strcmp(message_buf[i], "") == 0) break;
		strcat(message, message_buf[i]);
		strcat(message, ",");
	}

	// End the message with a newline and empty. This will ensure that the
	// bytes are sent out on the wire. Otherwise it will wait for further
	// output bytes.
	strcat(message, "\n\0");
	printf("Sending: %s", message);

	return send_message(connfd, message);
}

int send_echo_message(int connfd, char *message) {
	upper_case(message);
	add_message(message);
	return send_message(connfd, message);
}

int process_message(int connfd, char *message) {
	if (strncmp(message, "\\", 1) == 0) {
		if (strncmp(message, "\\JOIN", 5) == 0) {
			char *nickname = strtok(message + 5, " ");
			char *room_name = strtok(NULL, " ");
			int room_exists = 0;
			for(int i = 0; i < roomi; i++) {
				if (roomList[i]->name == room_name) {
					create_user(nickname, connfd, roomList[i]);
					room_exists = 1;
				}
			}
			if (!room_exists) {
				struct Room *newRoom = create_room(room_name);
				create_user(nickname, connfd, newRoom);
			}
			send_message(connfd, room_name);
		}
		else if (strncmp(message, "\\ROOMS", 6) == 0) {
			char room_list[MAXROOMS * 32];
			for (int i = 0; i < roomi; i++) {
				strcat(room_list, roomList[i]->name);
			}
			send_message(connfd, room_list);
		}
		else if (strncmp(message, "\\LEAVE", 6) == 0) {
			for (int i = 0; i < roomi; i++) {
				if (delete_user(connfd, roomList[i]) == 1) {
					send_message(connfd, "GOODBYE");
				}
			}
			send_message(connfd, "You are not currently in any room");
		}
		else if (strncmp(message, "\\WHO", 4) == 0) {
			char user_list[MAXUSERS * 32];
			int index = find_room_index(connfd);
			for (int i = 0; i < roomList[index]->users; i++) {
				strcat(user_list, roomList[index]->user_list[i]->nickname);
			}
			send_message(connfd, user_list);
		}
		else if (strncmp(message, "\\HELP", 5) == 0) {
			//sorry this is an abortion idk how to do string line spacing
			char* ret;
			strcat(ret, "Possible commands are:\n"); 
			strcat(ret, "\\JOIN nickname room: When the server receives this command it will add the user to the\n list of users associated with a particular room. If the room does not exist it will create a\n new room and add the user to the list of users associated with the new room. The server must\n respond to the client with the name of the room.\n");
			strcat(ret, "\\ROOMS: When the server receives this command it will respond with a list of the available rooms.\n");
			strcat(ret, "\\LEAVE: When the server receives this command it will remove the user from the room and, send the single message GOODBYE, and disconnect the client.\n"); 
			strcat(ret, "\\WHO: When the server receives this command it will send a list the users in the room the user is currently in.\n");
			strcat(ret, "\\nickname message: When the server receives this command it will send the message to the user specified by nickname.\n");
			send_message(connfd, ret);
		}
		else {
			char* ret; 
			strcat(ret, message);
			strcat(ret, " command not recognized.");
			send_message(connfd, ret);
		}
	}
	else {

	}

	if (is_list_message(message)) {
		printf("Server responding with list response.\n");
		return send_list_message(connfd);
	}
	else {
		printf("Server responding with echo response.\n");
		return send_echo_message(connfd, message);
	}
}

// The main function that each thread will execute.
void echo(int connfd) {
	size_t n;

	// Holds the received message.
	char message[MAXLINE];

	while ((n = receive_message(connfd, message)) > 0) {
		message[n] = '\0';  // null terminate message (for string operations)
		printf("Server received message %s (%d bytes)\n", message, (int)n);
		n = process_message(connfd, message);
	}
}

// Helper function to establish an open listening socket on given port.
int open_listenfd(int port) {
	int listenfd;    // the listening file descriptor.
	int optval = 1;  //
	struct sockaddr_in serveraddr;

	/* Create a socket descriptor */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;

	/* Eliminates "Address already in use" error from bind */
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
		sizeof(int)) < 0)
		return -1;

	/* Listenfd will be an endpoint for all requests to port
	   on any IP address for this host */
	bzero((char *)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);
	if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) return -1;

	/* Make it a listening socket ready to accept connection requests */
	if (listen(listenfd, LISTENQ) < 0) return -1;
	return listenfd;
}

// thread function prototype as we have a forward reference in main.
void *thread(void *vargp);

int main(int argc, char **argv) {
	// Check the program arguments and print usage if necessary.
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	// initialize message buffer.
	init_message_buf();

	// Initialize the message buffer lock.
	pthread_mutex_init(&lock, NULL);

	// The port number for this server.
	int port = atoi(argv[1]);

	// The listening file descriptor.
	int listenfd = open_listenfd(port);

	// The main server loop - runs forever...
	while (1) {
		// The connection file descriptor.
		int *connfdp = malloc(sizeof(int));

		// The client's IP address information.
		struct sockaddr_in clientaddr;

		// Wait for incoming connections.
		socklen_t clientlen = sizeof(struct sockaddr_in);
		*connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);

		/* determine the domain name and IP address of the client */
		struct hostent *hp =
			gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
				sizeof(clientaddr.sin_addr.s_addr), AF_INET);

		// The server IP address information.
		char *haddrp = inet_ntoa(clientaddr.sin_addr);

		// The client's port number.
		unsigned short client_port = ntohs(clientaddr.sin_port);

		printf("server connected to %s (%s), port %u\n", hp->h_name, haddrp,
			client_port);

		// Create a new thread to handle the connection.
		pthread_t tid;
		pthread_create(&tid, NULL, thread, connfdp);
	}
}

/* thread routine */
void *thread(void *vargp) {
	// Grab the connection file descriptor.
	int connfd = *((int *)vargp);
	// Detach the thread to self reap.
	pthread_detach(pthread_self());
	// Free the incoming argument - allocated in the main thread.
	free(vargp);
	// Handle the echo client requests.
	echo(connfd);
	printf("client disconnected.\n");
	// Don't forget to close the connection!
	close(connfd);
	return NULL;
}
