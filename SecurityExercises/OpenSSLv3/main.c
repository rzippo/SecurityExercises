#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "tcpSetup.h"
#include "DHKeyAgreement.h"
#include "DESTransfer.h"
#include "HMACTransfer.h"

void printHelp();

int clientMain(short serverPort, char* inputFilename);
int serverMain(short listeningPort, char* outputFilename);

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		printHelp();
		return 0;
	}

	if (!strcmp(argv[1], "c"))
	{
		short serverPort = (short) atoi(argv[2]);
		return clientMain(serverPort, argv[3]);
	}
	else if (!strcmp(argv[1], "s"))
	{
		short listeningPort = (short) atoi(argv[2]);
		return serverMain(listeningPort, argv[3]);
	}
	else
	{
		printHelp();
		return 0;
	}
	
	return 0;
}

void printHelp()
{
	printf("Required arguments:\n");
	printf("Letter c or s to denote the mode between client, sending the file, or server, receiving it.\n");
	printf("Port number. Clients connect to it, server listens on it. Communication is always on localhost for simplicity.\n");
	printf("File name. Client reads it, server writes to it.\n");
}

static void errorCheck(int ret)
{
	if (ret == -1)
	{
		perror(NULL);
		exit(1);
	}
}

static char* relativeToAbsolutePath(char* relative)
{
	char* buf = malloc(sizeof(char) * 500);
	getcwd(buf, 500);
	strcat(buf, "/");
	strcat(buf, relative);
	return buf;
}

void printKey(unsigned char* sharedKey)
{
	int fd = open(relativeToAbsolutePath("sharedKey"), O_WRONLY | O_CREAT, 0666);
	write(fd, sharedKey, 512/8);
	close(fd);
}

int clientMain(short serverPort, char* inputFilename)
{
	int communicationSocket = connectToServer(serverPort);

	unsigned char* sharedKey = clientDHKeyAgreement(communicationSocket);
	printKey(sharedKey);
	
	unsigned char* encryptionKey = sharedKey;
	unsigned char* iv = sharedKey + 128/8;
	unsigned char* hmacKey = sharedKey + 256/8;

	int inputFile = open(relativeToAbsolutePath(inputFilename), O_RDONLY);
	errorCheck(inputFile);
	
	//Encrypt and write the cyphertext to a local file
	int outEncryptedFile = open(relativeToAbsolutePath("outEncryptedFile"), O_RDWR | O_CREAT, 0666);
	errorCheck(outEncryptedFile);
	
	struct stat st;
	fstat(inputFile, &st);
	unsigned inputSize = (unsigned) st.st_size;

	encrypt(inputFile, outEncryptedFile, inputSize, 4, encryptionKey, iv);
	lseek(outEncryptedFile, 0, SEEK_SET);

	//Send to the server the number of blocks to be received
	fstat(outEncryptedFile, &st);
	unsigned encryptedSize = (unsigned) st.st_size;
	uint32_t ciphertextBlockCount = encryptedSize / (128 / 8);
	ciphertextBlockCount = htonl(ciphertextBlockCount);

	write(communicationSocket, (void*)&ciphertextBlockCount, sizeof(ciphertextBlockCount));
	
	//Send the encrypted message and then the hmac of it 
	hmacSend(outEncryptedFile, communicationSocket, encryptedSize, 4, hmacKey, 128/8);
	
	//Cleanup
	close(inputFile);
	close(outEncryptedFile);
	return 0;
}

int serverMain(short listeningPort, char* outputFilename)
{
	int communicationSocket = waitClientConnection(listeningPort);

	unsigned char* sharedKey = serverDHKeyAgreement(communicationSocket);
	printKey(sharedKey);

	unsigned char* encryptionKey = sharedKey;
	unsigned char* iv = sharedKey + 128/8;
	unsigned char* hmacKey = sharedKey + 256/8;

	//Receive the number of blocks to be received
	uint32_t ciphertextBlockCount;
	read(communicationSocket, (void*)&ciphertextBlockCount, sizeof(ciphertextBlockCount));
	ciphertextBlockCount = ntohl(ciphertextBlockCount);

	//Receive the encrypted message, save it to a local file and check the hmac
	int inEncryptedFile = open(relativeToAbsolutePath("inEncryptedFile"), O_RDWR | O_CREAT, 0666);
	errorCheck(inEncryptedFile);

	if( hmacReceive(communicationSocket, inEncryptedFile, ciphertextBlockCount, 4, hmacKey, 128/8) )
	{
		lseek(inEncryptedFile, 0, SEEK_SET);

		int outputFile = open(relativeToAbsolutePath(outputFilename), O_WRONLY | O_CREAT, 0666);
		errorCheck(outputFile);

		decrypt(inEncryptedFile, outputFile, ciphertextBlockCount, 4, encryptionKey, iv);
		close(outputFile);
	}
	else
	{
		printf("SECURITY ERROR: hmac is wrong!\n");
		return 0;
	}

	close(inEncryptedFile);
	close(communicationSocket);
	
	return 0;
}