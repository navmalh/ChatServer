#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<fcntl.h>

#define	PORT_NUM			9876
#define	MAX_CONNECTIONS		500
#define SOCKET_BUFF_LEN		1024	// this is used in socket programming
#define MAX_USERNAME_LEN	20
#define MAX_PASSWORD_LEN	20
#define MAX_DESCRIPTION_LEN	256
#define MSGQ_BUFF_LEN		1024	// this is used in IPC using message queues

// Types of IPC messages from child process
// to parent process
#define	CHILD_ASKS_PARENT_CREATE_NEW_USER			1
#define	CHILD_ASKS_PARENT_LOGIN_USER				2
#define	CHILD_ASKS_PARENT_LOGOUT_USER				3
#define	CHILD_ASKS_PARENT_DOES_USER_EXIST			4
#define	CHILD_ASKS_PARENT_IS_USER_ONLINE			5
#define	CHILD_ASKS_PARENT_AUTHENTICATE_USER			6
#define	CHILD_ASKS_PARENT_GET_ALL_USERS				7
#define	CHILD_ASKS_PARENT_GET_ONLINE_USERS			8
#define	CHILD1_ASKS_PARENT_START_CHAT_REQUEST		9
#define	CHILD2_REPLIES_PARENT_START_CHAT_REQUEST	10
#define	CHILD_ASKS_PARENT_GET_PROFILE_INFO			11
#define	CHILD_SENDS_PARENT_CHAT_MESSAGE				12
#define	CHILD_SENDS_PARENT_BROADCAST_MESSAGE		13


// Types of IPC messages from parent process
// to child process
#define	PARENT_REPLIES_CHILD_DOES_USER_EXIST		31
#define	PARENT_REPLIES_CHILD_IS_USER_ONLINE			32
#define	PARENT_REPLIES_CHILD_AUTHENTICATE_USER		33
#define	PARENT_REPLIES_CHILD_GET_ALL_USERS			34
#define	PARENT_REPLIES_CHILD_GET_ONLINE_USERS		35
#define	PARENT_ASKS_CHILD2_START_CHAT_REQUEST		36
#define	PARENT_REPLIES_CHILD1_START_CHAT_REQUEST	37
#define	PARENT_REPLIES_CHILD_GET_PROFILE_INFO		38
#define	PARENT_SENDS_CHILD_CHAT_MESSAGE				39
#define	PARENT_SENDS_CHILD_BROADCAST_MESSAGE		40

// Forward declarations
void handleConnectedUserSteps(int, int, int);
void handleClient(int,int,int);

typedef struct
{
	char username[MAX_USERNAME_LEN];
	char password[MAX_PASSWORD_LEN];
	int  age;
	char gender;	// Use 'M' or 'F'
	char description[MAX_DESCRIPTION_LEN];
} User;

typedef struct UserNode
{
	User user;
	int pid;
	struct UserNode *next;
} UserNode;

// masterUserList contains a list of all the chat users in the system.
// parent process at chat server will keep an active record of it all
// the time.
UserNode *masterUserList = NULL;

// connUserList contains a list of users which are currently online.
// parent process at chat server will keep an active record of it all
// the time.
UserNode *connUserList = NULL;

// currentUser contains the name of the currently logged-in user for
// a given client session. This will be NULL for parent process at
// chat server but will have valid value for each of the child proce-
// sses in the chat server handling specific client sessions if there
// is a logged in user in that client session, otherwise NULL will be
// stored in it.
char currentUser[MAX_USERNAME_LEN];

// Message structure for IPC communication using message queues
typedef struct
{
	long mtype;			// this will indicate what is the type of message, so that appropriate action can be taken by destination
	int sourcepid;		// use 1 for parent process, use actual pid for child process
	int destpid;		// use 1 for parent process, use actual pid for child process
	char mtext[MSGQ_BUFF_LEN];
} Message;

// Create container for parent message queue ids.
int parentmsgqids[MAX_CONNECTIONS];

// Create container for child message queue ids.
int childmsgqids[MAX_CONNECTIONS];

typedef struct
{
	char username[MAX_USERNAME_LEN];
	int iParentMQId;
	int iChildMQId;
} ConnUserMQInfo;

// connUserMQInfo array will be useful to later retrieve
// a parent or child message queue id for a particular
// user name.
ConnUserMQInfo connUserMQInfo[MAX_CONNECTIONS];

int iNumUserMQInfo = 0;	// this will count the current number of client connections

// This function is only for debugging purposes
void printConnUserMQInfo()
{
	printf("Inside printConnUserMQInfo() function.\n");

	for (int i = 0; i < iNumUserMQInfo; i++)
	{
		printf("username = %s\tiParentMQID = %d\tiChildMQId = %d\n",
				connUserMQInfo[i].username, connUserMQInfo[i].iParentMQId,
				connUserMQInfo[i].iChildMQId);
	}
}

void initializeConnUserMQInfo()
{
	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		strcpy(connUserMQInfo[i].username, "");
		connUserMQInfo[i].iParentMQId = -1;
		connUserMQInfo[i].iChildMQId = -1;
	}
}

// doesUserMQInfoExists() function will return true
// if parent & child msg q ids are available for a
// given user, else it will return false.
int doesUserMQInfoExists(char *username)
{
	int iFound = 0;

	for (int i = 0; (i < iNumUserMQInfo) && (!iFound); i++)
	{
		// Check if we have a match with the given user
		if (strcmp(connUserMQInfo[i].username, username) == 0)
		{
			iFound = 1;
		}
	}

	return iFound;
}

// Helper function to insert message q id info for a given user
void insertUserMQInfo(char *username, int iParentMQId, int iChildMQId)
{
	// Insert the new info at the end of existing information
	// of message queue ids
	strcpy(connUserMQInfo[iNumUserMQInfo].username, username);
	connUserMQInfo[iNumUserMQInfo].iParentMQId = iParentMQId;
	connUserMQInfo[iNumUserMQInfo].iChildMQId = iChildMQId;

	// Increment the count
	iNumUserMQInfo++;
}

// Helper function to update message q id info for a given user
void updateUserMQInfo(char *username, int iParentMQId, int iChildMQId)
{
	int iFound = 0;

	for (int i = 0; (i < iNumUserMQInfo) && (!iFound); i++)
	{
		// Check if we have a match with the given user
		if (strcmp(connUserMQInfo[i].username, username) == 0)
		{
			connUserMQInfo[i].iParentMQId = iParentMQId;
			connUserMQInfo[i].iChildMQId = iChildMQId;
			iFound = 1;	// flag set to exit the loop
		}
	}
}

// getParentMQId() is a helper function which given a username
// as input, it will return the corresponding parent message
// queue id which that client session child process needs to
// send message to parent process.
int getParentMQId(char *username)
{
	int iResult = -1;
	int iFound = 0;

	for (int i = 0; (i < iNumUserMQInfo) && (!iFound); i++)
	{
		// Check if we have a match with the given user
		if (strcmp(connUserMQInfo[i].username, username) == 0)
		{
			iResult = connUserMQInfo[i].iParentMQId;
			iFound = 1;	// flag set to exit the loop
		}
	}

	return iResult;
}

// getChildMQId() is a helper function which given a username
// as input, it will return the corresponding child message
// queue id which the parent process needs to send message 
// to that client session child process.
int getChildMQId(char *username)
{
	int iResult = -1;
	int iFound = 0;

	for (int i = 0; (i < iNumUserMQInfo) && (!iFound); i++)
	{
		// Check if we have a match with the given user
		if (strcmp(connUserMQInfo[i].username, username) == 0)
		{
			iResult = connUserMQInfo[i].iChildMQId;
			iFound = 1;	// flag set to exit the loop
		}
	}

	return iResult;
}

// sendMessageToMQ() and recieveMessageFromMQ() are
// two helper functions to handle the IPC part using
// message queues.

// sendMessageToMQ() is a helper function to send
// a message to a specific message queue.
void sendMessageToMQ(int msgqid, int type, int spid, int dpid, char *text, int iWait)
{
	// Create Message structure using the
	// given parameters
	Message msg;
	msg.mtype = type;
	msg.sourcepid = spid;
	msg.destpid   = dpid;
	strcpy(msg.mtext, text);

	int result;

	// Now send this message to the message queue
	if (iWait)
		result = msgsnd(msgqid, &msg.mtype, sizeof(msg), 0);
	else
		result = msgsnd(msgqid, &msg.mtype, sizeof(msg), IPC_NOWAIT);

	if (result < 0)
		perror("msgsnd");

	return;
}

// recieveMessageFromMQ() is a helper function to recieve
// a message from a specific message queue.
Message recieveMessageFromMQ(int msgqid, int iWait)
{
	// Create Message structure
	Message msg;
	int result;

	// Recieve a message from the message queue
	if (iWait)
		result = msgrcv(msgqid, &(msg.mtype), sizeof(msg), 0, 0);
	else
		result = msgrcv(msgqid, &(msg.mtype), sizeof(msg), 0, IPC_NOWAIT);
	
	//if (result < 0)
	//	perror("msgrcv");	// comment the error traces for now

	// Return the recieved message
	return msg;
}

// findUser() is a helper function that will
// return 1 if a given user is present in the
// list, else it will return 0.
int findUser(char *username, UserNode *list)
{
	int isFound = 0;

	while (list && (!isFound))
	{
		if (strcmp(list->user.username, username) == 0)
			isFound = 1;

		list = list->next;
	}

	return isFound;
}

// getUserInfo() function is used to get the user
// profile information of a particular user.
User getUserInfo(char *username, UserNode *list)
{
	User user;
	int isFound = 0;

	while (list && (!isFound))
	{
		if (strcmp(list->user.username, username) == 0)
		{
			strcpy(user.username, list->user.username);
			strcpy(user.description, list->user.description);
			user.age = list->user.age;
			user.gender = list->user.gender;
			// Don't copy password! Typical use case
			// of this function is to get general
			// profile information of one user by
			// another user in the chat server system.
			// And password must not be revealed like
			// that!

			// Set the flag to exit the loop
			isFound = 1;
		}

		list = list->next;
	}

	return user;
}

// addUser() is a helper function which
// will add a given user to the list.
void addUser(User user, int pid, UserNode **list)
{
	// Create a new node
	UserNode* node = (UserNode *) malloc(sizeof(UserNode));
	strcpy(node->user.username, user.username);
	strcpy(node->user.password, user.password);
	node->user.age = user.age;
	node->user.gender = user.gender;
	strcpy(node->user.description, user.description);
	node->pid = pid;
	node->next = NULL;

	if (*list == NULL)
	{
		// Currently no users in the given list.
		// Just point list to this new node.
		*list = node;
	}
	else
	{
		// Go to the end of the list and add the
		// new node there.
		UserNode *p = *list;

		while (p->next)
			p = p->next;

		p->next = node;
	}
}

// removeUser() is a helper function which
// will remove a given user from the list.
void removeUser(User user, UserNode **list)
{
	// If list is empty, nothing to be done.
	if (*list == NULL)
		return;

	// If this user is not in the chat server
	// system, nothing to be done.
	if (!findUser(user.username, *list))
		return;

	if (strcmp((*list)->user.username, user.username) == 0)
	{
		// User is present in the first node of the
		// list itself. Modify list to point to the
		// second node of the list and then delete
		// this node.
		UserNode *node = *list;
		*list = (*list)->next;
		free(node);
		node = NULL;
	}
	else
	{
		// We need to search for the user in the
		// list, delete that node and do the plumbing
		// between previous and next nodes.
		UserNode *p = *list;
		UserNode *q = (*list)->next;

		while (strcmp(q->user.username, user.username) != 0)
		{
			p = q;
			q = q->next;
		}

		// Now at this points q points to the node
		// which needs to be deleted.
		p->next = q->next;	// Do the plumbing
		free(q);
		q = NULL;
	}
}
		
int isUserOnline(char *);	// forward declaration

// getAllUsers() function is used by the parent
// process to get a list of all the users with
// their online/offline status and populate the
// "buff" parameter with this information.
void getAllUsers(char *buff)
{
	UserNode *p = masterUserList;

	// If there are 3 users viz. "abc", "def" and
	// "ghi" and let's assume "def" is online and
	// other two users are offline, the user information
	// will be copied in "buff" as "abc-0|def-1|ghi-0"
	// where 0 and 1 indicate offline & online resp-
	// ectively.
	while (p)
	{
		strcat(buff, p->user.username);
		strcat(buff, "-");

		// Now add the Online/Offline status
		// of this user.
		if (isUserOnline(p->user.username))
			strcat(buff, "1");
		else
			strcat(buff, "0");
		
		p = p->next;

		if (p)
			strcat(buff, "|");
	}
}

// getOnlineUsers() function is used by the parent
// process to get a list of the online users and
// populate the "buff" parameter with this information.
void getOnlineUsers(char *buff)
{
	UserNode *p = connUserList;

	// If there are 3 online users viz. "abc", "def"
	// and "ghi", the user information will be copied
	// in "buff" as "abc|def|ghi".
	while (p)
	{
		strcat(buff, p->user.username);

		p = p->next;

		if (p)
			strcat(buff, "|");
	}
}

// getOnlineUserPID() is used by the parent process
// to get the PID of the child process for a given
// online user. It takes the username of the online
// user as input and returns the pid of the child
// process that is handling the current session of
// that user.
int getOnlineUserPID(char *username)
{
	int pid = -1;
	UserNode* p = connUserList;
	int iFound = 0;

	while (p && (!iFound))
	{
		if (strcmp(p->user.username, username) == 0)
		{
			// We found the user, so get it's pid
			// from the store.
			pid = p->pid;

			// Set the flag to exit the loop
			iFound = 1;
		}

		p = p->next;
	}

	return pid;
}

// getOnlineUserName() is used by the parent process
// to get the username of the client session whose
// child process child process id is passed as input.
char* getOnlineUserName(int pid)
{
	char *username = malloc(sizeof(char) * MAX_USERNAME_LEN);
	strcpy(username, "");

	UserNode* p = connUserList;
	int iFound = 0;

	while (p && (!iFound))
	{
		if (p->pid == pid)
		{
			// We found the user, so get it's username
			strcpy(username, p->user.username);

			// Set the flag to exit the loop
			iFound = 1;
		}

		p = p->next;
	}

	return username;
}

// Forward declarations
int doesUserExist(char *);
int isUserOnline(char *);

// createNewUser() function will create a new user
// in the chat server system.
void createNewUser(User user)
{
	// User must not already exist!
	if (!doesUserExist(user.username))
	{
		// We simply add this user to masterUserList.
		// pid is not relevant for masterUserList,
		// so pass that as -1.
		addUser(user, -1, &masterUserList);
	}
}

// deleteUser() function will delete a user
// from the chat server system.
void deleteUser(char *username)
{
	// User must exist first!
	if (doesUserExist(username))
	{
		// We simply remove this user from
		// masterUserList.
		User user;
		strcpy(user.username, username);	// password not important for delete user
		removeUser(user, &masterUserList);
	}
}

// login() function will allow a user to login
// to the chat server system.
void login(User user, int pid, int iParentMQId, int iChildMQId)
{
	// Check if user is not online already
	if (!isUserOnline(user.username))
	{
		// Simply add this user to connUserList
		addUser(user, pid, &connUserList);

		// Insert/Update message queue id info
		// for later retrieval and use
		if (doesUserMQInfoExists(user.username))
			updateUserMQInfo(user.username, iParentMQId, iChildMQId);
		else
			insertUserMQInfo(user.username, iParentMQId, iChildMQId);
	}
}

// logout() function will allow a user to logout
// from the chat server system.
void logout(char *username)
{
	// Check if the user is in connUserList or not
	if (isUserOnline(username))
	{
		// Simply remove user from connUserList
		User user;
		strcpy(user.username, username);
		removeUser(user, &connUserList);
	}
}

// doesUserExist() function checks if a given
// user exists or not. This function can be
// used in the parent process only as parent
// process maintains masterUserList.
int doesUserExist(char *username)
{
	// We simply check if this user is present
	// in masterUserList or not.
	return findUser(username, masterUserList);
}

// isUserOnline() function checks if a given
// user is online or not. This function can be
// used in the parent process only as parent
// process maintains connUserList.
int isUserOnline(char *username)
{
	// We simply check if this user is present
	// in connUserList or not.
	return findUser(username, connUserList);
}

// authenticateUser() function checks if the
// credentials are correct or not to allow this
// user to login. This function can only be
// used in the parent process.
int authenticateUser(char *username, char *password)
{
	int isValid = 0;

	// First check if this user exists or not
	if (!doesUserExist(username))
		return isValid;

	// We need to check the entire masterUserList
	// and try to match the user credentials
	UserNode* list = masterUserList;

	while (list && (!isValid))
	{
		// Check if username and password match
		// with this user.
		if (strcmp(list->user.username, username) == 0 &&
		    strcmp(list->user.password, password) == 0)
		{
			// User is authenticated
			isValid = 1;
		}

		list = list->next;
	}

	return isValid;
}

// This function is used by the child process
// to ask the parent process if a specific
// user actually exists. It uses message queues
// to communicate with the parent process (IPC).
// It will return 1 if user actually exists, else
// it will return 0.
int childAsksDoesUserExist(char *username, int parentmsgqid, int childmsgqid)
{
	int result = 0;

	// Ask parent process by sending a message to
	// parent message queue.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_DOES_USER_EXIST,
					getpid(), 1, username, 0);
	
	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg = recieveMessageFromMQ(childmsgqid, 1);
	
	if (msg.mtype == PARENT_REPLIES_CHILD_DOES_USER_EXIST &&
		msg.destpid == getpid())
	{
		result = atoi(msg.mtext);
	}

	return result;
}

// This function is used by the child process
// to ask the parent process if a specific user
// is currently online. It uses message queues
// to communicate with the parent process (IPC).
// It will return 1 if user is actually online,
// else it will return 0.
int childAsksIsUserOnline(char *username, int parentmsgqid, int childmsgqid)
{
	int result = 0;

	// Ask parent process by sending a message to
	// parent message queue.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_IS_USER_ONLINE,
					getpid(), 1, username, 0);

	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg = recieveMessageFromMQ(childmsgqid, 1);

	if (msg.mtype == PARENT_REPLIES_CHILD_IS_USER_ONLINE &&
		msg.destpid == getpid())
	{
		result = atoi(msg.mtext);
	}

	return result;
}

// This function is used by the child process
// to request the parent process for authentication
// of a specific user using username/password.
// It uses message queues to communicate with the
// parent process (IPC). It will return 1 if the
// authentication is passed by the parent process,
// else it will return 0.
int childAsksAuthenticateUser(char *username, char *password, int parentmsgqid, int childmsgqid)
{
	int result = 0;
	char text[MSGQ_BUFF_LEN];
	strcpy(text, username);
	strcat(text, "-");
	strcat(text, password);

	// Ask parent process by sending a message to
	// parent message queue.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_AUTHENTICATE_USER,
					getpid(), 1, text, 0);

	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg = recieveMessageFromMQ(childmsgqid, 1);

	if (msg.mtype == PARENT_REPLIES_CHILD_AUTHENTICATE_USER &&
		msg.destpid == getpid())
	{
		result = atoi(msg.mtext);
	}

	return result;
}

// This function is used by the child process to
// request the parent process for getting information
// about all the users along with their online/offline
// status. This information is returned as a char buffer.
// It uses message queues to communicate with the parent
// process (IPC).
void childAsksGetAllUsers(int parentmsgqid, int childmsgqid, char *buff)
{
	// Ask parent process by sending a message to
	// parent message queue.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_GET_ALL_USERS,
					getpid(), 1, "", 0);

	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg = recieveMessageFromMQ(childmsgqid, 1);

	if (msg.mtype == PARENT_REPLIES_CHILD_GET_ALL_USERS &&
		msg.destpid == getpid())
	{
		strcpy(buff, msg.mtext);
	}
}

// This function is used by the child process to
// request the parent process for getting information
// about all the online users. This information is
// returned as a char buffer. It uses message queues
// to communicate with the parent process (IPC).
void childAsksGetOnlineUsers(int parentmsgqid, int childmsgqid, char *buff)
{
	// Ask parent process by sending a message to
	// parent message queue.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_GET_ONLINE_USERS,
					getpid(), 1, "", 0);

	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg = recieveMessageFromMQ(childmsgqid, 1);

	if (msg.mtype == PARENT_REPLIES_CHILD_GET_ONLINE_USERS &&
		msg.destpid == getpid())
	{
		strcpy(buff, msg.mtext);
	}
}

void childAsksGetProfileInfo(int parentmsgqid, int childmsgqid,
								 char *username, char *buff)
{
	// Ask parent process by sending a message to
	// parent message queue. We need to pass username
	// as the input to the parent process.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_GET_PROFILE_INFO,
					getpid(), 1, username, 0);

	// Get reply from parent process by recieving a
	// message from the child message queue.
	Message msg2 = recieveMessageFromMQ(childmsgqid, 1);

	if (msg2.mtype == PARENT_REPLIES_CHILD_GET_PROFILE_INFO &&
		msg2.destpid == getpid())
	{
		strcpy(buff, msg2.mtext);
	}
}

// handleCreateNewUser() is a helper function that will
// communicate with client using a connected tcp socket
// to get credentials for new user being created and
// then finally add the new user to the chat server system.
// It will return 1 in case of success, else will return 0.
int handleCreateNewUser(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	char username[MAX_USERNAME_LEN];
	char password1[MAX_PASSWORD_LEN];
	char password2[MAX_PASSWORD_LEN];
	char age[4];
	char gender[2];
	char description[MAX_DESCRIPTION_LEN];
	int n = 0;

	// Ask client to enter new username
	// he wants to create
	strcpy(buff, "> Choose username: ");
	send(connfd, buff, strlen(buff), 0);

	// Now get the client's reply message (desired username)
	n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
	buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
	strcpy(username, buff);

	// Check if a user already exists with this username.
	// This is only known to the parent process, so let's
	// ask the parent process about it.
	if (childAsksDoesUserExist(username, parentmsgqid, childmsgqid))
	{
		sprintf(buff, "> Error: The user '%s' already exists.\n", username);
		send(connfd, buff, strlen(buff), 0);
		return 0;
	}
	else
	{
		// Ask client to enter password
		strcpy(buff, "> Enter password: ");
		send(connfd, buff, strlen(buff), 0);
	
		// Now get the client's reply message (desired password)
		n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		strcpy(password1, buff);

		// Ask client to retype password
		strcpy(buff, "> Retype password: ");
		send(connfd, buff, strlen(buff), 0);
		
		// Now get the client's reply message (retyped password)
		n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		strcpy(password2, buff);

		if (strcmp(password1, password2) != 0)
		{
			// Send the error message to client that the
			// two passwords don't match.
			strcpy(buff, "> Error: The two passwords don't match!\n");
			send(connfd, buff, strlen(buff), 0);
			return 0;
		}

		// Ask client to enter age
		strcpy(buff, "> Enter age: ");
		send(connfd, buff, strlen(buff), 0);
	
		// Now get the client's reply message (age)
		n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		strcpy(age, buff);

		// Ask client to enter gender
		strcpy(buff, "> Enter gender (M/F): ");
		send(connfd, buff, strlen(buff), 0);
	
		// Now get the client's reply message (gender)
		n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		strcpy(gender, buff);
		
		// Ask client to enter profile description
		strcpy(buff, "> Enter profile description (max 200 characters): ");
		send(connfd, buff, strlen(buff), 0);
	
		// Now get the client's reply message (profile description)
		n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		strcpy(description, buff);
	}

	// We have the valid credentials, now create the
	// new user and add it to the chat server system.
	// But since this needs to be done by the parent
	// process of the chat server system, so child
	// needs to send message to parent process using
	// parent message queue.
	char text[MSGQ_BUFF_LEN];
	strcpy(text, username);
	strcat(text, "-");
	strcat(text, password1);
	strcat(text, "-");
	strcat(text, age);
	strcat(text, "-");
	strcat(text, gender);
	strcat(text, "-");
	strcat(text, description);

	// source pid is pid of this child process, destination
	// pid is 1 (1 indicates parent process) and we are
	// sending concatenated username/password of user to
	// be created as text.
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_CREATE_NEW_USER, getpid(), 1, text, 0);

	// Inform client about success in creating user.
	sprintf(buff, "> \n> User '%s' has been successfully created.\n", username);
	send(connfd, buff, strlen(buff), 0);

	return 1;
}

// handleUserLogin() is a helper function that will
// communicate with client using a connected tcp socket
// to get credentials for the user who is trying to
// login to the chat server system. If authenticated,
// this user will be added to list of online users.
// It will return 1 in case of success, else it will
// return 0.
int handleUserLogin(int connfd, int parentmsgqid, int childmsgqid)
{
	int n = 0;
	char buff[SOCKET_BUFF_LEN];
	char username[MAX_USERNAME_LEN];
	char password[MAX_PASSWORD_LEN];

	// Ask client to enter the username
	// he wants to login with
	strcpy(buff, "> Enter username: ");
	send(connfd, buff, strlen(buff), 0);

	// Now get the client's reply message (username)
	n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
	buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
	strcpy(username, buff);

	// Ask client to enter password
	strcpy(buff, "> Enter password: ");
	send(connfd, buff, strlen(buff), 0);
	
	// Now get the client's reply message (password)
	n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
	buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
	strcpy(password, buff);

	// Check if this user exists or not. This is only known to
	// parent process, so let's ask the parent process.
	if (!childAsksDoesUserExist(username, parentmsgqid, childmsgqid))
	{
		sprintf(buff, "> Error: The user '%s' does not exists.\n", username);
		send(connfd, buff, strlen(buff), 0);
		return 0;
	}
	else if (childAsksIsUserOnline(username, parentmsgqid, childmsgqid))
	{
		sprintf(buff, "> Error: The user '%s' is already online through another session.\n", username);
		send(connfd, buff, strlen(buff), 0);
		return 0;
	}
	else if (!childAsksAuthenticateUser(username, password, parentmsgqid, childmsgqid))
	{
		// Check if the credentials are indeed correct
		strcpy(buff, "> Error: Invalid password.\n");
		send(connfd, buff, strlen(buff), 0);
		return 0;
	}

	// We have the valid credentials, now login the
	// new user in the chat server system. But since
	// this needs to be done by the parent process
	// process of the chat server system, so child
	// needs to send message to parent process using
	// parent message queue.
	char text[MSGQ_BUFF_LEN];
	strcpy(text, username);
	strcat(text, "-");
	strcat(text, password);

	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_LOGIN_USER, getpid(), 1, text, 0);

	// Keep a note of currently logged-in user in the child
	// process which is handling this client session
	strcpy(currentUser, username);

	// Inform client about success in user login.
	sprintf(buff, "> \n%s> User '%s' has successfully logged in.\n",
			currentUser, currentUser);
	send(connfd, buff, strlen(buff), 0);

	return 1;
}

// handleUserLogout() is a helper function that will
// communicate with client using a connected tcp socket
// to logout the currently logged in user. It will
// return 1 in case of success, else it will return 0.
int handleUserLogout(int connfd, int parentmsgqid, int childmsgqid)
{
	int n = 0;
	char buff[SOCKET_BUFF_LEN];

	// Logout this user from the chat server system.
	// But since this needs to be done by the parent
	// process of the chat server system, so child
	// needs to send message to parent process using
	// parent message queue (IPC).
	char username[MAX_USERNAME_LEN];
	strcpy(username, currentUser);
	sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_LOGOUT_USER, getpid(), 1, username, 0);

	// From here on, this child process should know that there
	// is no currently logged in user in the client session
	strcpy(currentUser, "");

	// Send a message to client about successful logout of user
	sprintf(buff, ">\n> The user '%s' has been logged out.\n>\n", username);
	send(connfd, buff, strlen(buff), 0);

	return 1;
}

// handleInitialSteps() function will ask the client
// to either create a new user or do user login etc.
// This function will communicate with the client
// using a connected tcp socket. It will return
// 1 in case of success, else it will return 0.
int handleInitialSteps(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	int iLoggedIn = 0;		// flag to indicate if user is now logged in
	int iTerminated = 0;	// flag to indicate if the session has been terminated

	// Show a welcome message to the client
	strcpy(buff, "> Welcome! You are now connected to the Chat Server System.\n");
	send(connfd, buff, strlen(buff), 0);
	
	// Keep asking initial choices to client again and again
	// until the client session has been terminated or the
	// user has successfully logged in to the chat server system.
	while ((!iTerminated) && (!iLoggedIn))
	{
		// First ask the client to either create
		// new user or login as existing user or
		// terminate the session.
		strcpy(buff, "> \n");
		strcat(buff, "> Create new user:     1\n");
		strcat(buff, "> Login existing user: 2\n");
		strcat(buff, "> Terminate session:   3\n");
		strcat(buff, "> \n");
		strcat(buff, "> Choose any of the above options and press Enter: ");
		send(connfd, buff, strlen(buff), 0);

		// Now get the client's reply message
		int n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
		int iChoice = atoi(buff);

		switch (iChoice)
		{
			case 1:
			{
				// Keep trying to create new user in
				// an infinite loop.
				while (!handleCreateNewUser(connfd, parentmsgqid, childmsgqid));

				// New user has been successfully created.
				break;
			}
			case 2:
			{
				int iNumChances = 3;

				// Give 3 chances to user to login, if
				// it doesn't work, we will go back to
				// the previous menu options of choice
				// 1 or choice 2.
				while (iNumChances > 0 && (!iLoggedIn))
				{
					if (handleUserLogin(connfd, parentmsgqid, childmsgqid))
						iLoggedIn = 1;	// user has successfully logged in
					else
						iNumChances--;
				}

				break;
			}
			case 3:
			{
				// Client has requested for terminating session
				strcpy(buff, "> Your session is being terminated.\n");
				send(connfd, buff, strlen(buff), 0);

				// Close this session
				close(connfd);
				iTerminated = 1;

				// TODO - we definitely need more work to do here
				// e.g. need to send message to parent process to
				// stop watching the two message queues related to
				// this child process
				break;
			}
			default:
			{
				// This is not a valid choice, show the
				// error message to client.
				strcpy(buff, "> Error: Invalid option entered!\n");
				send(connfd, buff, strlen(buff), 0);
			}
		}
	}

	return 1;
}

// handleShowAllUsers() function will show a list of
// all the users to the connected tcp client.
void handleShowAllUsers(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	// Add a header line for tabular data of all users
	strcat(buff, currentUser);
	strcat(buff, "> User\tStatus\n");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	strcat(buff, currentUser);
	strcat(buff, "> ");

	// We can get information of all the users from
	// the "masterUserList" and store it in "buff".
	// But this masterUserList is only with the
	// parent process, so let's ask the parent process
	// about it.
	char allUserInfo[512];
	childAsksGetAllUsers(parentmsgqid, childmsgqid, allUserInfo);

	int iLen = strlen(allUserInfo);

	// allUserInfo is in the format like "abc-0|def-1|ghi-0"
	// which means there are a total of 3 users in the chat
	// server system viz. abc, def, ghi and currently only
	// user def is online, other two users are offline. We
	// need to display this information to the client session
	// in an appropriate way. The below for loop does exactly
	// that.
	for (int i = 0; i < iLen; i++)
	{
		if (allUserInfo[i] == '-')
		{
			strcat(buff, "\t");

			// The next character will be Online/Offline
			// status, we can append it now itself.
			i++;
			if (allUserInfo[i] == '1')
				strcat(buff, "Online");
			else
				strcat(buff, "Offline");
		}
		else if (allUserInfo[i] == '|')
		{
			strcat(buff, "\n");
			strcat(buff, currentUser);
			strcat(buff, "> ");
		}
		else
		{
			// Append the character to buff
			char text[2];
			text[0] = allUserInfo[i];
			text[1] = '\0';
			strcat(buff, text);
		}
	}

	strcat(buff, "\n");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");
	
	// Now simply show this all-user data to the client
	// using connected socket to client
	send(connfd, buff, strlen(buff), 0);
}

// handleShowOnlineUsers() function will show a list of
// all the users to the connected tcp client.
void handleShowOnlineUsers(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	// Add a header line for tabular data of online users.
	// Note that "Status" column will always have the
	// value "Online" for online users but we still show
	// it for the sake of consistency in display w.r.t.
	// "Show All Users" command.
	strcat(buff, currentUser);
	strcat(buff, "> User\tStatus\n");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	strcat(buff, currentUser);
	strcat(buff, "> ");

	// We can get information of online users from
	// the "connUserList" and store it in "buff".
	// But this connUserList is only with the
	// parent process, so let's ask the parent process
	// about it.
	char onlineUserInfo[512];
	childAsksGetOnlineUsers(parentmsgqid, childmsgqid, onlineUserInfo);

	int iLen = strlen(onlineUserInfo);

	// onlineUserInfo is in the format like "abc|def|ghi"
	// which means there are a total of 3 online users in the
	// chat server system viz. abc, def, ghi. We need to
	// display this information to the client session in an
	// appropriate way. The below for loop does exactly that.
	for (int i = 0; i < iLen; i++)
	{
		if (onlineUserInfo[i] != '|')
		{
			// Append the character to buff
			char text[2];
			text[0] = onlineUserInfo[i];
			text[1] = '\0';
			strcat(buff, text);

			// Check the next character
			if (onlineUserInfo[i+1] == '\0' || onlineUserInfo[i+1] == '|')
			{
				// Append the status as Online. This is obvious
				// information but necessary for sake of consistency
				// of display w.r.t. Show All Users command.
				strcat(buff, "\tOnline\n");

				if (onlineUserInfo[i+1] == '|')
				{
					strcat(buff, currentUser);
					strcat(buff, "> ");
				}
			}
		}
	}

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");
	
	// Now simply show this online-user data to the client
	// using connected socket to client
	send(connfd, buff, strlen(buff), 0);
}

// handleShowUserProfileInfo() function will show the profile
// details of a particular user name as asked by the client.
void handleShowUserProfileInfo(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	// Add a header line for tabular data of online users.
	// Note that "Status" column will always have the
	// value "Online" for online users but we still show
	// it for the sake of consistency in display w.r.t.
	// "Show All Users" command.
	strcat(buff, currentUser);
	strcat(buff, "> Enter username: ");
	send(connfd, buff, strlen(buff), 0);
	
	// Get the client's reply message (username)
	int n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
	buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
	
	char username[MAX_USERNAME_LEN];
	strcpy(username, buff);

	// First check if the user does exist
	if (!childAsksDoesUserExist(username, parentmsgqid, childmsgqid))
	{
		sprintf(buff, "%s> Error: The user '%s' does not exists.\n",
				currentUser, username);
		send(connfd, buff, strlen(buff), 0);
		return;
	}

	// We can get profile information of this user from
	// the "masterUserList" and store it in "buff".
	// But masterUserList is only with the parent process,
	// so let's ask the parent process about it.
	char profileInfo[512];
	childAsksGetProfileInfo(parentmsgqid, childmsgqid, username, profileInfo);

	strcpy(buff, currentUser);
	strcat(buff, ">\n");
	strcat(buff, currentUser);
	strcat(buff, "> Age = ");

	char *delimiter = "-";
	char *token = strtok(profileInfo, delimiter);
	strcat(buff, token);
	strcat(buff, "\n");
	strcat(buff, currentUser);
	strcat(buff, "> ");

	strcat(buff, "Gender = ");
	token = strtok(NULL, delimiter);
	
	if (strcasecmp(token, "M") == 0)
		strcat(buff, "Male");
	else if (strcasecmp(token, "F") == 0)
		strcat(buff, "Female");
	
	strcat(buff, "\n");
	strcat(buff, currentUser);
	strcat(buff, "> ");

	strcat(buff, "Description = ");
	token = strtok(NULL, delimiter);
	strcat(buff, token);
	strcat(buff, "\n");
	
	// Now simply show this online-user data to the client
	// using connected socket to client
	send(connfd, buff, strlen(buff), 0);
}

void handleChatting(int, char *, int, int);

// handleStartChat() function will initiate the steps
// to start chat for the connected tcp client.
void handleStartChat(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Add a blank line for space and clarity
	strcat(buff, currentUser);
	strcat(buff, "> \n");

	// First step is to ask the username with
	// which user wants to start chat
	strcat(buff, currentUser);
	strcat(buff, "> Enter user name to chat with or wait for incoming chat requests: ");
	
	// Send this "buff" data to the client using
	// connected socket to that client
	send(connfd, buff, strlen(buff), 0);

	// We need an infinite loop as we check for any user input
	// in client session (via socket) in a non-blocking way
	// and also check for any incoming chat request (via child
	// message queue) again in a non-blocking way.	
	while (1)
	{
		strcpy(buff, "");
		int n = 0;

		// Now get the client's reply message. Make it non-blocking
		// as we also want to check for incoming chat requests if
		// user is not typing any user name to chat with.
		n = recv(connfd, buff, SOCKET_BUFF_LEN, MSG_DONTWAIT);

		if (n != -1)
		{
			buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
			char username[MAX_USERNAME_LEN];
			strcpy(username, buff);

			// Check if the user has given it's own username!
			if (strcmp(username, currentUser) == 0)
			{
				sprintf(buff, "%s> Error: You are not allowed to chat with yourself.\n",
						currentUser);
				send(connfd, buff, strlen(buff), 0);

				// Give a fresh chance to start the chat by
				// making a recursive call to this function.
				handleStartChat(connfd, parentmsgqid, childmsgqid);
			}	
			// Check if the user does exist
			else if (!childAsksDoesUserExist(username, parentmsgqid, childmsgqid))
			{
				sprintf(buff, "%s> Error: The user '%s' does not exists.\n",
						currentUser, username);
				send(connfd, buff, strlen(buff), 0);
				
				// Give a fresh chance to start the chat by
				// making a recursive call to this function.
				handleStartChat(connfd, parentmsgqid, childmsgqid);
			}
			// Check if the user is online - TODO - we need to remove this restriction!
			else if (!childAsksIsUserOnline(username, parentmsgqid, childmsgqid))
			{
				sprintf(buff, "%s> Error: The user '%s' is current not online. TODO - Chat with offline user should be allowed, need to work on this later!\n", currentUser, username);
				send(connfd, buff, strlen(buff), 0);
				
				// Give a fresh chance to start the chat by
				// making a recursive call to this function.
				handleStartChat(connfd, parentmsgqid, childmsgqid);
			}

			// For start chat request, the flow of IPC messages is
			// as follows. This child process will send a
			// CHILD1_ASKS_PARENT_START_CHAT_REQUEST message to the
			// parent process. The parent process will send the
			// PARENT_ASKS_CHILD2_START_CHAT_REQUEST message to the
			// child process of the target recipient user. That
			// target child process will reply back with a Y/N
			// (i.e. Yes/No) CHILD2_REPLIES_PARENT_START_CHAT_REQUEST
			// message back to the parent process. Further, the
			// parent process will now send the PARENT_REPLIES_CHILD1_START_CHAT_REQUEST
			// back to this original child process which started
			// the request in the first place. So the flow of messages
			// is like this:
			//
			// (1) CHILD1_ASKS_PARENT_START_CHAT_REQUEST message from source child to parent
			// (2) PARENT_ASKS_CHILD2_START_CHAT_REQUEST message from parent to target child
			// (3) CHILD2_REPLIES_PARENT_START_CHAT_REQUEST message from target child to parent
			// (4) PARENT_REPLIES_CHILD1_START_CHAT_REQUEST message from parent to source child


			// We need to send chat request to other child
			// process (other user session) but this has to
			// be done via the parent process. So let's send
			// the chat request to parent process which will
			// further ask the other child process (other
			// user session). Send the username to chat with
			// as message text to the parent process.
			//sendMessageToMQ(parentmsgqid, CHILD_ASKS_PARENT_START_CHAT_REQUEST,
			//				getpid(), 1, username, 0);

			// Don't use wrapper function until things work!
			// Use msgsnd directly.
			Message msg;
			msg.mtype = CHILD1_ASKS_PARENT_START_CHAT_REQUEST;
			msg.sourcepid = getpid();
			msg.destpid   = 1;
			strcpy(msg.mtext, username);
			
			int result = msgsnd(parentmsgqid, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
			if (result < 0)
				perror("msgsnd");

			// Now get the final reply from the parent process
			// which has already done the communication with
			// the second child process (second user).
			Message msg2;
			result = msgrcv(childmsgqid, &(msg2.mtype), sizeof(msg2), 0, 0);	// blocking
			
			if (result >= 0)
			{
				if (msg2.mtype == PARENT_REPLIES_CHILD1_START_CHAT_REQUEST && msg2.destpid == getpid())
				{
					char reply[2];
					strcpy(reply, msg2.mtext);

					int iChatAccepted = 0;
					if (strcasecmp(reply, "Y") == 0)
						iChatAccepted = 1;

					if (iChatAccepted)
					{
						// Show a success message of chat acceptance to this user
						sprintf(buff, "%s>\n%s> Congratulations! User '%s' has accepted your chat request.\n%s> ",
								currentUser, currentUser, username, currentUser);
						strcat(buff, "You can now start chatting.\n");
						strcat(buff, currentUser);
						strcat(buff, "> ");
						send(connfd, buff, strlen(buff), 0);

						// Chat request has now been accepted, call the
						// handleChatting() function to manage further
						// chatting between these two users.
						handleChatting(connfd, username, parentmsgqid, childmsgqid);
					}
					else
					{
						sprintf(buff, "%s>\n%s> Sorry! User '%s' has declined your chat request.\n",
								currentUser, currentUser, username);
						send(connfd, buff, strlen(buff), 0);

						// Chat request has been rejected, call this function
						// recursively to give a fresh chance to start the chat.
						handleStartChat(connfd, parentmsgqid, childmsgqid);
					}
				}
			}
		}
		else
		{
			// Check for incoming chat request from other client
			// session also. This should be a non-blocking check.
			// This child process will recieve it from the
			// parent process but it will arrive on child msg q.
			//Message msg = recieveMessageFromMQ(childmsgqid, 0);
			
			Message msg;
			int result = msgrcv(childmsgqid, &(msg.mtype), sizeof(msg), 0, IPC_NOWAIT);	// non-blocking

			if (result >= 0)
			{			
				if ((msg.mtype == PARENT_ASKS_CHILD2_START_CHAT_REQUEST) && (msg.destpid == getpid()))
				{
					// There is indeed a chat request from another user.
					// Ask this user if he/she can accept it or not.
					char sourceUser[MAX_USERNAME_LEN];
					strcpy(sourceUser, msg.mtext);
					sprintf(buff, "\n%s>\n%s> User '%s' is requesting for chat. Press Y/N: ",
							currentUser, currentUser, sourceUser);
					send(connfd, buff, strlen(buff), 0);
				
					// Get the Y/N reply from this user	
					n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
					buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character

					int iChatAccepted = 0;
					if (strcasecmp(buff, "Y") == 0)
						iChatAccepted = 1;

					// Add the name of the sourceUser
					strcat(buff, "-");
					strcat(buff, sourceUser);

					// Inform the parent process with the Y/N reply of this user.
					// Also, parent process should be told about the original
					// sourceUser of the chat request as this will now be the
					// user to which parent process should send it's final reply.
					Message msg;
					msg.mtype = CHILD2_REPLIES_PARENT_START_CHAT_REQUEST;
					msg.sourcepid = getpid();
					msg.destpid   = 1;
					strcpy(msg.mtext, buff);
					int result = msgsnd(parentmsgqid, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
					if (result >= 0)
					{
						if (iChatAccepted)
						{
							// Show a message of chat acceptance to this user
							sprintf(buff, "%s>\n%s> You have accepted the chat request of user '%s'.\n%s> ",
									currentUser, currentUser, sourceUser, currentUser);
							strcat(buff, "You can now start chatting.\n");
							strcat(buff, currentUser);
							strcat(buff, "> ");
							send(connfd, buff, strlen(buff), 0);

							// Chat request has now been accepted, call the
							// handleChatting() function to manage further
							// chatting between these two users.
							handleChatting(connfd, sourceUser, parentmsgqid, childmsgqid);
						}
						else
						{
							sprintf(buff, "%s>\n%s> You have declined chat request of user '%s'.\n",
									currentUser, currentUser, sourceUser);
							send(connfd, buff, strlen(buff), 0);
						
							// Chat request has been rejected, call this function
							// recursively to give another chance to start chat.
							handleStartChat(connfd, parentmsgqid, childmsgqid);
						}
					}
					else
					{
						perror("msgsnd");
					}
				}	// end if

				if ((msg.mtype == PARENT_SENDS_CHILD_BROADCAST_MESSAGE) && (msg.destpid == getpid()))
				{
					// Show the user prompt once again
					sprintf(buff, "\n%s> ", currentUser);
					send(connfd, buff, strlen(buff), 0);

					char broadcastMessage[256];
					strcpy(broadcastMessage, msg.mtext);

					// Let's split broadcastMessage into source
					// user name and actual message content.
					char sourceUser[MAX_USERNAME_LEN];
					char message[200];
					char *delimiter = "-";

					char *token = strtok(broadcastMessage, delimiter);
					strcpy(sourceUser, token);

					token = strtok(NULL, delimiter);
					strcpy(message, token);

					// broadcastMessage has source username followed
					// by a hyphen and then message. We need to replace
					// hyphen with ": " while displaying to this user.
					strcpy(broadcastMessage, sourceUser);
					strcat(broadcastMessage, ": ");
					strcat(broadcastMessage, message);

					// Now show the broadcast message on this
					// user session's terminal window. This
					// will require connfd socket. Add prefix
					// too. Prefix is simply the name of the
					// user who is sending you the message.
					strcpy(buff, broadcastMessage);
					strcat(buff, "\n");
					send(connfd, buff, strlen(buff), 0);
				}
			}	// end if
		}
	}	// end while

	return;
}


// handleChatting() function is used by the child process
// to exchange chat messages with the other user session.
// At this point, the initial chat request has already been
// raised and accepted between both the users.
void handleChatting(int connfd, char *otherUser, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Either side user can stop this chat by sending
	// a "stop" (case-insensitive) message.
	int iStopped = 0;

	// Continuously check for any outgoing or incoming
	// messages in a loop, unless a stop chat message
	// is sent from either user by sending a "stop" message.
	while (!iStopped)
	{
		strcpy(buff, "");
		int n = 0;

		// Check if we have recieved some message from this
		// current user session window. This will come via
		// the socket, make it non-blocking as we may not
		// have something and we want to check for incoming
		// messages from other user too.
		n = recv(connfd, buff, SOCKET_BUFF_LEN, MSG_DONTWAIT);
		
		if (n != -1)
		{
			// Indeed this user has typed something, we need
			// to send it to the other user via the parent
			// process (IPC).
			buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character

			// Just show the proper prompt to user first
			char prompt[50];
			sprintf(prompt, "%s> ", currentUser);
			send(connfd, prompt, strlen(prompt), 0);

			// This user (currentUser) has asked to stop
			// chat.
			if (strcasecmp(buff, "stop") == 0)
				iStopped = 1;	// flag set to exit the while loop

			// We need to concatenate the other user name and
			// the message text before sending. This will help
			// the parent process to decide where to redirect
			// this chat message.
			char outgoingMessage[256];
			strcpy(outgoingMessage, otherUser);
			strcat(outgoingMessage, "-");
			strcat(outgoingMessage, buff);

			Message outMsg;
			outMsg.mtype = CHILD_SENDS_PARENT_CHAT_MESSAGE;
			outMsg.sourcepid = getpid();
			outMsg.destpid   = 1;
			strcpy(outMsg.mtext, outgoingMessage);
			
			// Send this message immediately to parent process (no waiting)
			// via the message queue (IPC)
			int result = msgsnd(parentmsgqid, &outMsg.mtype, sizeof(outMsg), IPC_NOWAIT);
			
			if (result < 0)
				perror("msgsnd");
		}

		// Now it's time to check any incoming message from other user.
		// This will come via the message queue (IPC). Again, make it
		// non-blocking as we are doing these checks in an infinite loop.
		Message inMsg;
		int result = msgrcv(childmsgqid, &(inMsg.mtype), sizeof(inMsg), 0, IPC_NOWAIT);

		// Check if we have a valid incoming message
		if (result >= 0)
		{
			// Check if it is the type of message we are expecting and
			// we are the intended recipient of it
			if (inMsg.mtype == PARENT_SENDS_CHILD_CHAT_MESSAGE && inMsg.destpid == getpid())
			{
				char incomingMessage[256];
				strcpy(incomingMessage, inMsg.mtext);

				// Check if otherUser has asked to stop chatting
				// i.e. if we have recieved a "stop" message.
				if (strcasecmp(incomingMessage, "stop") == 0)
					iStopped = 1;	// flag set to exit the while loop

				// Now show the incoming message on this
				// user session's terminal window. This
				// will require connfd socket.

				if (iStopped)
				{
					// stop message recieved from other user.
					// Replace "stop" message with a more
					// informative message for this user.
					sprintf(incomingMessage, "User '%s' has stopped this chat.", otherUser);
					strcpy(buff, incomingMessage);
				}
				else
				{
					// Normal chat message, add prefix too.
					// Prefix is simply the name of the user
					// who is sending you the message.
					char prefix[32];
					strcpy(prefix, otherUser);
					strcat(prefix, ": ");

					strcpy(buff, prefix);
					strcat(buff, incomingMessage);
				}

				strcat(buff, "\n");
				strcat(buff, currentUser);
				strcat(buff, "> ");
				send(connfd, buff, strlen(buff), 0);
			}

			if ((inMsg.mtype == PARENT_SENDS_CHILD_BROADCAST_MESSAGE) && (inMsg.destpid == getpid()))
			{
				// Show the user prompt once again
				sprintf(buff, "\n%s> ", currentUser);
				send(connfd, buff, strlen(buff), 0);

				char broadcastMessage[256];
				strcpy(broadcastMessage, inMsg.mtext);

				// Let's split broadcastMessage into source
				// user name and actual message content.
				char sourceUser[MAX_USERNAME_LEN];
				char message[200];
				char *delimiter = "-";

				char *token = strtok(broadcastMessage, delimiter);
				strcpy(sourceUser, token);

				token = strtok(NULL, delimiter);
				strcpy(message, token);

				// broadcastMessage has source username followed
				// by a hyphen and then message. We need to replace
				// hyphen with ": " while displaying to this user.
				strcpy(broadcastMessage, sourceUser);
				strcat(broadcastMessage, ": ");
				strcat(broadcastMessage, message);

				// Now show the broadcast message on this
				// user session's terminal window. This
				// will require connfd socket. Add prefix
				// too. Prefix is simply the name of the
				// user who is sending you the message.
				strcpy(buff, broadcastMessage);
				strcat(buff, "\n");
				strcat(buff, currentUser);
				strcat(buff, "> ");
				send(connfd, buff, strlen(buff), 0);
			}
		}
	}

	// Chat is stopped now, let's go back to previous level
	// menu options.
	handleConnectedUserSteps(connfd, parentmsgqid, childmsgqid);
}

// handleBroadcastMessage() function will send a message
// to all the online user sessions by sending the message
// to the parent process. The parent process will further
// broadcast it to all the other online users (except the
// sender one). Note that this sender only sends message
// and never recieves any message from other users.
void handleBroadcastMessage(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	strcpy(buff, "");

	// Just show the proper prompt to user first
	char prompt[50];
	sprintf(prompt, "%s> ", currentUser);
	send(connfd, prompt, strlen(prompt), 0);

	// This side sender user can stop this broadcast
	// by typing a "stop" (case-insensitive) message.
	int iStopped = 0;

	// Keep sending broadcast message to all other
	// online users unless this user has typed "stop"
	// message.
	while (!iStopped)
	{
		strcpy(buff, "");

		// Check if we have recieved some message from this
		// current user session window. This will come via
		// the socket.
		int n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
		
		if (n != -1)
		{
			// Indeed this user has typed something, we need
			// to send it to the other user via the parent
			// process (IPC).
			buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character

			// Just show the proper prompt to user first
			sprintf(prompt, "%s> ", currentUser);
			send(connfd, prompt, strlen(prompt), 0);

			// This user (currentUser) has asked to stop
			// chat.
			if (strcasecmp(buff, "stop") == 0)
			{
				iStopped = 1;	// flag set to exit the while loop
			}
			else
			{
				// We need to concatenate the current user name and
				// the message text before sending. This will help
				// the parent process to redirect this broadcast
				// message to all users except this source user!
				char outgoingMessage[256];
				strcpy(outgoingMessage, currentUser);
				strcat(outgoingMessage, "-");
				strcat(outgoingMessage, buff);

				Message outMsg;
				outMsg.mtype = CHILD_SENDS_PARENT_BROADCAST_MESSAGE;
				outMsg.sourcepid = getpid();
				outMsg.destpid   = 1;
				strcpy(outMsg.mtext, outgoingMessage);
			
				// Send this message immediately to parent process (no waiting)
				// via the message queue (IPC)
				int result = msgsnd(parentmsgqid, &outMsg.mtype, sizeof(outMsg), IPC_NOWAIT);
			
				if (result < 0)
					perror("msgsnd");
			}
		}
	}

	// Broadcast is stopped now, let's go back to previous level
	// menu options.
	handleConnectedUserSteps(connfd, parentmsgqid, childmsgqid);
}

// handleConnectedUserSteps() function is used to
// give the command options to a logged in user and
// then takes necessary steps as per user choice.
void handleConnectedUserSteps(int connfd, int parentmsgqid, int childmsgqid)
{
	char buff[SOCKET_BUFF_LEN];
	int iLoggedOut = 0;
	int iChoice = 0;
	int iShowMenuOptions = 1;

	while (!iLoggedOut)
	{
		if (iShowMenuOptions)
		{
			// First give the client all the options
			// that user should see after login into
			// chat server system. As the user is
			// logged in, show him the "username> "
			// prompt every time.
			strcpy(buff, currentUser);
			strcat(buff, "> \n");
			strcat(buff, currentUser);
			strcat(buff, "> Show all users:           1\n");
			strcat(buff, currentUser);
			strcat(buff, "> Show online users:        2\n");
			strcat(buff, currentUser);
			strcat(buff, "> Start chat:               3\n");
			strcat(buff, currentUser);
			strcat(buff, "> Broadcast message:        4\n");
			strcat(buff, currentUser);
			strcat(buff, "> Show user profile info:   5\n");
			strcat(buff, currentUser);
			strcat(buff, "> Logout:                   6\n");
			strcat(buff, currentUser);
			strcat(buff, "> \n");
			strcat(buff, currentUser);
			strcat(buff, "> Choose any one of the above options and press Enter: ");
			send(connfd, buff, strlen(buff), 0);

			iShowMenuOptions = 0;	// turn it off for now to avoid repeated menu prompts
		}

		// Now get the client's reply message using
		// connected socket. Make it non-blocking because
		// we also want to check for any incoming broadcast
		// messages from other users via the parent process.
		int n = recv(connfd, buff, SOCKET_BUFF_LEN, MSG_DONTWAIT);

		if (n != -1)
		{
			buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character
			iChoice = atoi(buff);
			iShowMenuOptions = 1;	// turn it on again, we want to show menu options next time

			switch (iChoice)
			{
				case 1:
					handleShowAllUsers(connfd, parentmsgqid, childmsgqid);
					break;
				case 2:
					handleShowOnlineUsers(connfd, parentmsgqid, childmsgqid);
					break;
				case 3:
					handleStartChat(connfd, parentmsgqid, childmsgqid);
					break;
				case 4:
					handleBroadcastMessage(connfd, parentmsgqid, childmsgqid);
					break;
				case 5:
					handleShowUserProfileInfo(connfd, parentmsgqid, childmsgqid);
					break;
				case 6:
					handleUserLogout(connfd, parentmsgqid, childmsgqid);
					iLoggedOut = 1;		// flag set to exit the while loop
					break;
				default:
					// This is not a valid choice, show the
					// error message to client.
					sprintf(buff, "%s> Error: Invalid option entered!\n%s> ",
							currentUser, currentUser);
					send(connfd, buff, strlen(buff), 0);
					break;
			}	// end switch
		}	// end if
		else
		{
			// Check for incoming chat request & incoming broadcast
			// messages from other client session also. This should
			// be a non-blocking check. This child process will recieve
			// it from the parent process but it will arrive on child msg q.
			Message msg;
			int result = msgrcv(childmsgqid, &(msg.mtype), sizeof(msg), 0, IPC_NOWAIT);	// non-blocking
			
			if (result >= 0)
			{
				if ((msg.mtype == PARENT_ASKS_CHILD2_START_CHAT_REQUEST) && (msg.destpid == getpid()))
				{
					iShowMenuOptions = 1;	// turn it on again, we want to show menu options next time

					// There is indeed a chat request from another user.
					// Ask this user if he/she can accept it or not.
					char sourceUser[MAX_USERNAME_LEN];
					strcpy(sourceUser, msg.mtext);
					sprintf(buff, "\n%s>\n%s> User '%s' is requesting for chat. Press Y/N: ",
							currentUser, currentUser, sourceUser);
					send(connfd, buff, strlen(buff), 0);
				
					// Get the Y/N reply from this user	
					n = recv(connfd, buff, SOCKET_BUFF_LEN, 0);
					buff[n-2] = '\0';  // Replace last '\n' character entered by user to '\0' character

					int iChatAccepted = 0;
					if (strcasecmp(buff, "Y") == 0)
						iChatAccepted = 1;

					// Add the name of the sourceUser
					strcat(buff, "-");
					strcat(buff, sourceUser);

					// Inform the parent process with the Y/N reply of this user.
					// Also, parent process should be told about the original
					// sourceUser of the chat request as this will now be the
					// user to which parent process should send it's final reply.
					Message msg;
					msg.mtype = CHILD2_REPLIES_PARENT_START_CHAT_REQUEST;
					msg.sourcepid = getpid();
					msg.destpid   = 1;
					strcpy(msg.mtext, buff);
					int result = msgsnd(parentmsgqid, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
					if (result >= 0)
					{
						if (iChatAccepted)
						{
							// Show a message of chat acceptance to this user
							sprintf(buff, "%s>\n%s> You have accepted the chat request of user '%s'.\n%s> ",
									currentUser, currentUser, sourceUser, currentUser);
							strcat(buff, "You can now start chatting.\n");
							strcat(buff, currentUser);
							strcat(buff, "> ");
							send(connfd, buff, strlen(buff), 0);

							// Chat request has now been accepted, call the
							// handleChatting() function to manage further
							// chatting between these two users.
							handleChatting(connfd, sourceUser, parentmsgqid, childmsgqid);
						}
						else
						{
							sprintf(buff, "%s>\n%s> You have declined chat request of user '%s'.\n",
									currentUser, currentUser, sourceUser);
							send(connfd, buff, strlen(buff), 0);
						
							// Chat request has been rejected, call this function
							// recursively to give previous menu options.
							handleConnectedUserSteps(connfd, parentmsgqid, childmsgqid);
						}
					}
					else
					{
						perror("msgsnd");
					}
				}
				
				if ((msg.mtype == PARENT_SENDS_CHILD_BROADCAST_MESSAGE) && (msg.destpid == getpid()))
				{
					iShowMenuOptions = 1;	// turn it on again, we want to show menu options next time
					
					// Show the user prompt once again
					sprintf(buff, "\n%s> ", currentUser);
					send(connfd, buff, strlen(buff), 0);

					char broadcastMessage[256];
					strcpy(broadcastMessage, msg.mtext);

					// Let's split broadcastMessage into source
					// user name and actual message content.
					char sourceUser[MAX_USERNAME_LEN];
					char message[200];
					char *delimiter = "-";

					char *token = strtok(broadcastMessage, delimiter);
					strcpy(sourceUser, token);

					token = strtok(NULL, delimiter);
					strcpy(message, token);

					// broadcastMessage has source username followed
					// by a hyphen and then message. We need to replace
					// hyphen with ": " while displaying to this user.
					strcpy(broadcastMessage, sourceUser);
					strcat(broadcastMessage, ": ");
					strcat(broadcastMessage, message);

					// Now show the broadcast message on this
					// user session's terminal window. This
					// will require connfd socket. Add prefix
					// too. Prefix is simply the name of the
					// user who is sending you the message.
					strcpy(buff, broadcastMessage);
					strcat(buff, "\n");
					send(connfd, buff, strlen(buff), 0);
				}
			}
		}		
	}	// end while

	// User has logged out, so let's go back to
	// the initial menu options by calling the
	// handleClient() function.
	handleClient(connfd, parentmsgqid, childmsgqid);
}

// handleClient() function is executed by a child
// process of the server to handle this connection.
void handleClient(int connfd, int parentmsgqid, int childmsgqid)
{
	// Call handleInitialSteps() function to present the
	// initial options to client to create a new user or
	// to do user login etc, take user choice as input
	// and then do necessary steps as chat server.
	if (!handleInitialSteps(connfd, parentmsgqid, childmsgqid))
	{
		// Client session has been terminated,
		// no point in going any further.
		return;
	}

	// User is now logged in to chat server

	// Call handleConnectedUserSteps() function to present
	// all options to a logged-in user and take necessary
	// actions as per user input.	
	handleConnectedUserSteps(connfd, parentmsgqid, childmsgqid);

/*
	char ip[16];
	inet_ntop(AF_INET, &(pCliAddr->sin_addr), ip, INET_ADDRSTRLEN);
	printf("Connected Client details:\t");
	printf("IP = %s\tPort = %d\n", ip, pCliAddr->sin_port);
	printf("Data sent by client = %s\n", buff);

	// send() system call is used to send message
	// on a socket.
	send(connfd, buff, strlen(buff), 0);

	// Close the tcp connection (send FIN packet
	// to server program)
	close(connfd);
*/
}

// splitUsernamePassword() is a helper function that will
// split a piece of input text containing hyphen separated
// username and password into two separate username and
// password strings.
void splitUsernamePassword(char *text, char *username, char *password)
{
	char *ptr = strchr(text, '-');
	int iPos = ptr - text;
	int iLen = strlen(text);
	int i = 0, j = 0;

	while (i < iPos)
		username[i++] = text[j++];

	username[i] = '\0';

	i = 0;
	j = iPos+1;

	while (j < iLen)
		password[i++] = text[j++];

	password[i] = '\0';
}

// This function is used by the parent process
// to recieve and handle a message from the
// child process.
void handleMessageFromChild(int parentmsgqid, int childmsgqid)
{
	//Message msg = recieveMessageFromMQ(parentmsgqid, 0);
	// Use msgrcv directly for now instead of call to
	// wrapper function.
	Message msg;
	int result = msgrcv(parentmsgqid, &(msg.mtype), sizeof(msg), 0, IPC_NOWAIT);

	if (result < 0)
		return;		// don't process this further

	// Check if the destination pid corresponds to
	// parent process indeed. Note that we use 1 to
	// indicate parent process (instead of actual pid
	// of parent process).
	if (msg.destpid == 1)
	{
		switch (msg.mtype)
		{
		case CHILD_ASKS_PARENT_CREATE_NEW_USER:
		{
			char username[MAX_USERNAME_LEN];
			char password[MAX_PASSWORD_LEN];
			char age[4];
			char gender[2];
			char description[MAX_DESCRIPTION_LEN];
			char *delimiter = "-";

			// msg.mtext contains all the information
			// related to the user separated by '-'
			// character. Let's split all that info
			// into separate variables using strtok().
			char *token = strtok(msg.mtext, delimiter);
			strcpy(username, token);

			token = strtok(NULL, delimiter);
			strcpy(password, token);

			token = strtok(NULL, delimiter);
			strcpy(age, token);

			token = strtok(NULL, delimiter);
			strcpy(gender, token);

			token = strtok(NULL, delimiter);
			strcpy(description, token);

			// Create the User object with all
			// the information we have now
			User user;
			strcpy(user.username, username);
			strcpy(user.password, password);
			user.age = atoi(age);
			
			if (strcasecmp(gender, "M") == 0)
				user.gender = 'M';
			else if (strcasecmp(gender, "F") == 0)
				user.gender = 'F';
			else
				user.gender = '-';	// not known

			strcpy(user.description, description);

			// Create user now
			createNewUser(user);

			// All done, currently don't see the need to
			// inform success back to client in this case

			break;
		}
		case CHILD_ASKS_PARENT_LOGIN_USER:
		{
			User *user = (User *) malloc(sizeof(User));

			// Split msg.mtext into username and password
			splitUsernamePassword(msg.mtext, user->username, user->password);

			// Get the pid of the child process
			int childpid = msg.sourcepid;

			// Do the login now
			login(*user, childpid, parentmsgqid, childmsgqid);

			// All done, currently don't see the need to
			// inform success back to client in this case.
			// Client will show success message anyway.

			break;
		}
		case CHILD_ASKS_PARENT_LOGOUT_USER:
		{
			char username[MAX_USERNAME_LEN];
			strcpy(username, msg.mtext);

			// Do the logout now
			logout(username);

			// All done, currently don't see the need to
			// inform success back to client in this case
			// Client will show success message anyway.
			
			break;
		}
		case CHILD_ASKS_PARENT_DOES_USER_EXIST:
		{
			char username[MAX_USERNAME_LEN];
			strcpy(username, msg.mtext);

			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;

			// Check if this user actually exists or not
			int iUserExists = doesUserExist(username);

			char sUserExists[2];
			sprintf(sUserExists, "%d", iUserExists);

			// Now inform the child process about the result
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_DOES_USER_EXIST,
							1, childpid, sUserExists, 0);
			break;
		}
		case CHILD_ASKS_PARENT_IS_USER_ONLINE:
		{
			char username[MAX_USERNAME_LEN];
			strcpy(username, msg.mtext);

			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;

			// Check if this user is online or not
			int iUserOnline = isUserOnline(username);

			char sUserOnline[2];
			sprintf(sUserOnline, "%d", iUserOnline);

			// Now inform the child process about the result
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_IS_USER_ONLINE,
							1, childpid, sUserOnline, 0);
			break;
		}
		case CHILD_ASKS_PARENT_AUTHENTICATE_USER:
		{
			char username[MAX_USERNAME_LEN];
			char password[MAX_PASSWORD_LEN];
			
			// Split msg.mtext into username and password
			splitUsernamePassword(msg.mtext, username, password);

			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;

			// Check if this user actually exists or not
			int iIsAuthenticated = authenticateUser(username, password);

			char sIsAuthenticated[2];
			sprintf(sIsAuthenticated, "%d", iIsAuthenticated);

			// Now inform the child process about the result
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_AUTHENTICATE_USER,
							1, childpid, sIsAuthenticated, 0);
			break;
		}
		case CHILD_ASKS_PARENT_GET_ALL_USERS:
		{
			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;

			// Get information about all the users and
			// their online/offline status.
			char sAllUsers[MSGQ_BUFF_LEN];
			strcpy(sAllUsers, "");
			getAllUsers(sAllUsers);

			// Now inform the child process about the result
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_GET_ALL_USERS,
							1, childpid, sAllUsers, 0);
			break;
		}
		case CHILD_ASKS_PARENT_GET_ONLINE_USERS:
		{
			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;

			// Get information about the online users
			char sOnlineUsers[MSGQ_BUFF_LEN];
			strcpy(sOnlineUsers, "");
			getOnlineUsers(sOnlineUsers);

			// Now inform the child process about the result
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_GET_ONLINE_USERS,
							1, childpid, sOnlineUsers, 0);
			break;
		}
		case CHILD_ASKS_PARENT_GET_PROFILE_INFO:
		{
			// Get the pid of the child who is asking
			int childpid = msg.sourcepid;
			
			// Get the username for which profile info
			// is being asked
			char username[MAX_USERNAME_LEN];
			strcpy(username, msg.mtext);

			// Get the profile information from masterUserList
			User user = getUserInfo(username, masterUserList);

			// Store the user profile info in char array
			// and send the same to the child process
			char profileInfo[512];
			sprintf(profileInfo, "%d-%c-%s", user.age, user.gender, user.description);
			sendMessageToMQ(childmsgqid, PARENT_REPLIES_CHILD_GET_PROFILE_INFO,
							1, childpid, profileInfo, 0);

			break;
		}
		case CHILD1_ASKS_PARENT_START_CHAT_REQUEST:
		{
			// sourceChild process is the initiator of the
			// Start Chat request
			int sourceChild = msg.sourcepid;

			// Get the source user name who has initiated
			// this chat request
			char *sourceUser = getOnlineUserName(sourceChild);
			
			// targetUser process is the intended target
			// user of the chat request
			char targetUser[MAX_USERNAME_LEN];
			strcpy(targetUser, msg.mtext);

			// Get the pid of the targetUser using connUserList
			int targetChild = getOnlineUserPID(targetUser);

			// Get the message queue id of the target child
			// process to which we need to write the message
			int iTargetChildMQId = getChildMQId(targetUser);

			// Now ask the target child process about start
			// chat request
			//sendMessageToMQ(iTargetChildMQId, PARENT_ASKS_CHILD_START_CHAT_REQUEST,
			//				1, targetChild, sourceUser, 0);

			// Don't use wrapper function until things work!
			// Use msgsnd directly.
			Message msg;
			msg.mtype = PARENT_ASKS_CHILD2_START_CHAT_REQUEST;
			msg.sourcepid = 1;
			msg.destpid   = targetChild;
			strcpy(msg.mtext, sourceUser);
			int result = msgsnd(iTargetChildMQId, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
			if (result < 0)
				perror("msgsnd");

			break;
		}
		case CHILD2_REPLIES_PARENT_START_CHAT_REQUEST:
		{
			// msg.mtext contains Y/N reply followed by hyphen
			// followed by the original user who initiated the
			// chat request. Let's use strtok() to get both.
			char *token;
			char *delimiter = "-";

			char reply[2];
			token = strtok(msg.mtext, delimiter);
			strcpy(reply, token);

			if (strcasecmp(reply, "Y") == 0)
				strcpy(reply, "Y");
			else
				strcpy(reply, "N");		// anything else is taken as a "No"

			// Original user is now the target user for parent's
			// reply message.
			char targetUser[MAX_USERNAME_LEN];
			token = strtok(NULL, delimiter);
			strcpy(targetUser, token);
			
			// Get the pid of the targetUser using connUserList
			int targetChild = getOnlineUserPID(targetUser);

			// Get the message queue id of the target child
			// process to which we need to write the message
			int iTargetChildMQId = getChildMQId(targetUser);

			// Send the Y/N reply message to the child 1 (originator
			// of chat request)
			Message msg;
			msg.mtype = PARENT_REPLIES_CHILD1_START_CHAT_REQUEST;
			msg.sourcepid = 1;
			msg.destpid   = targetChild;
			strcpy(msg.mtext, reply);
			int result = msgsnd(iTargetChildMQId, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
			if (result < 0)
				perror("msgsnd");
			break;
		}
		case CHILD_SENDS_PARENT_CHAT_MESSAGE:
		{
			// msg.mtext contains the target user name
			// followed by hyphen followed by the actual
			// chat message to be sent to that user. Let's
			// use strtok() to get both.
			char *token;
			char *delimiter = "-";

			char targetUser[MAX_USERNAME_LEN];
			token = strtok(msg.mtext, delimiter);
			strcpy(targetUser, token);

			// Original user is now the target user for parent's
			// reply message.
			char message[256];
			token = strtok(NULL, delimiter);
			strcpy(message, token);
			
			// Get the pid of the targetUser using connUserList
			int targetChild = getOnlineUserPID(targetUser);

			// Get the message queue id of the target child
			// process to which we need to write the message
			int iTargetChildMQId = getChildMQId(targetUser);
			
			// Send the chat message to the target child process
			Message msg;
			msg.mtype = PARENT_SENDS_CHILD_CHAT_MESSAGE;
			msg.sourcepid = 1;
			msg.destpid   = targetChild;
			strcpy(msg.mtext, message);
			int result = msgsnd(iTargetChildMQId, &msg.mtype, sizeof(msg), IPC_NOWAIT);
			
			if (result < 0)
				perror("msgsnd");

			break;
		}
		case CHILD_SENDS_PARENT_BROADCAST_MESSAGE:
		{
			// msg.mtext contains the source user name
			// followed by hyphen followed by the chat 
			// message to be sent to all online users
			// except the source user himself.
			char *token;
			char *delimiter = "-";

			// Message to be broadcasted is exactly same
			// as recieved i.e. source user name followed
			// by hyphen followed by broadcast message.
			char message[256];
			strcpy(message, msg.mtext);

			// Get the source user name
			char sourceUser[MAX_USERNAME_LEN];
			token = strtok(msg.mtext, delimiter);
			strcpy(sourceUser, token);

			// We need to broadcast this message to all the
			// online users except the source user himself.
			// Let's loop through all the online users.
			UserNode *user = connUserList;

			while (user)
			{
				// Get the name of the online user
				char targetUser[MAX_USERNAME_LEN];
				strcpy(targetUser, user->user.username);

				// Don't send broadcast message back to
				// the source user. So check if the target
				// user is not same as the source user.
				if (strcmp(targetUser, sourceUser) != 0)
				{
					// Get the pid of the targetUser
					int targetChild = getOnlineUserPID(targetUser);

					// Get the message queue id of the target child
					// process to which we need to write the message
					int iTargetChildMQId = getChildMQId(targetUser);
			
					// Send the chat message to the target child process
					Message broadcast;
					broadcast.mtype = PARENT_SENDS_CHILD_BROADCAST_MESSAGE;
					broadcast.sourcepid = 1;
					broadcast.destpid   = targetChild;
					strcpy(broadcast.mtext, message);
					int result = msgsnd(iTargetChildMQId, &broadcast.mtype, sizeof(broadcast), IPC_NOWAIT);
			
					if (result < 0)
						perror("msgsnd");
				}	// end if (strcmp())

				// Move to the next user in connUserList
				user = user->next;
			}	// end while

			break;
		}
		default:
		{
			// Error case
			break;
		}
		}
	}
}

// This function is used by child process to
// handle a message from the parent process
void handleMessageFromParent(Message msg)
{
	// Check if the destination pid corresponds to
	// this child parent process indeed.
	if (msg.destpid == getpid())
	{
		switch (msg.mtype)
		{
		case 1:
			break;
		case 2:
			break;
		case 3:
			break;
		default:
			break;
		}
	}
}

int main()
{
	// socket() system call creates an endpoint for
	// communication and returns a file descriptor
	// that refers to that endpoint.
	int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Set the SO_REUSEADDR option to avoid bind
	// address in use error on repeated runs of
	// this program
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

	// Make the "sockfd" listening socket as non-blocking.
	// Otherwise, it will get stuck in accept() call when
	// no new client is there and handleMessageFromChild()
	// won't be invoked again. That will prevent any messages
	// from previous child processes (clients) to be received
	// and acted upon by the parent process.
	int flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

	// Check if socket() call worked well
	if (sockfd < 0)
	{
		perror("socket");
		exit(1);
	}

	// sockaddr_in structure allows you to bind a socket
	// with the desired address so that a server can listen
	// to the clients' connection requests.
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT_NUM);

	// Currently "sockfd" socket has no address assigned to it,
	// so let's assign "servaddr" address to "sockfd" socket.
	// The bind() system call assigns the address specified by
	// the second parameter to the socket file descriptor spec-
	// ified in the first parameter.
	if (bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
	{
		perror("bind");
		exit(1);
	}

	// listen() system call is used to mark a socket as a passive
	// socket i.e. this socket will now be used to accept incoming
	// connection requests using accept() system call. This socket
	// will deal in terms of connections and not in terms of data.
	if (listen(sockfd, MAX_CONNECTIONS) < 0)
	{
		perror("listen");
		exit(1);
	}

	// sockfd is now a passive (listening) socket, it will be used
	// to accept incoming connection requests from clients.

	int connfd;
	struct sockaddr_in cliaddr;
	socklen_t clilen;

	int iNumConnections = 0;	// total number of client connections

	// Server processes client requests in an infinite loop.
	while (1)
	{
		clilen = sizeof(cliaddr);

		// accept() system call extracts the first connection
		// request in the queue of pending connections for the
		// listening socket ("sockfd" here), creates a new
		// connected socket and returns file descriptor of the
		// new connected socket.
		connfd = accept(sockfd, (struct sockaddr *) &cliaddr, &clilen);

		if (connfd > 0)
		{
			// Create parent message queue. Parent will read from this
			// and child will write into it.
			int iParentMsgQId = msgget(IPC_PRIVATE, 0644 | IPC_CREAT);
			//printf("New message queue created: iParentMsgQId = %d\n", iParentMsgQId);

			// Create child message queue. Child will read from this
			// and parent will write into it.
			int iChildMsgQId = msgget(IPC_PRIVATE, 0644 | IPC_CREAT);
			//printf("New message queue created: iChildMsgQId = %d\n", iChildMsgQId);

			// Store iParentMsgQId and iChildMsgQId in their
			// respective containers for later use.
			parentmsgqids[iNumConnections] = iParentMsgQId;
			childmsgqids[iNumConnections]  = iChildMsgQId;

			// Increment the number of client connections
			iNumConnections++;

			// Create child process to handle this client session
			if (fork() == 0)
			{
				// Child process
				//printf("New child process created: PID = %d\n", getpid());
				close(sockfd);					// We don't need listening socket in child process, so just close it here.
				handleClient(connfd, iParentMsgQId, iChildMsgQId);	// Just handle the client request in child process
				exit(0);						// and exit child process.
			}
		}

		// Recieve and handle messages from all child processes.
		for (int i = 0; i < iNumConnections; i++)
			handleMessageFromChild(parentmsgqids[i], childmsgqids[i]);

		//close(connfd);	// We should only close if child requests Logout & Exit etc.
	}

	return 0;
}

