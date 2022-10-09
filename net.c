#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;


/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int bytesRead = 0;
  
  //keep track of how many bytes youve read
  while(bytesRead < len){

    int tempRead = read(fd, ((char *)buf + bytesRead), (len - bytesRead));//read returns how many bytes were read
    if(tempRead == -1){
      return false;
    }
    bytesRead += tempRead;
    
    if(bytesRead == 8){
      return true;
    }
  }
  return true;
}


/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int bytesWritten = 0;
  
  //keep track of how many bytes youve sent
  while(bytesWritten < len){
    
    int tempWritten = write(fd, ((char *)buf + bytesWritten), (len - bytesWritten));//write returns how many bytes were sent
    if(tempWritten == -1){
      return false;
    }
    bytesWritten += tempWritten;
    
    if(bytesWritten == 8){
      return true;
    }
  }
  return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block){

  //packet buffer
  uint8_t packet[264];

  //recieve packet from server
  if(nread(sd, 264, packet) == false){
    return false;
  }

  //copy into packetLength from header buffer, convert to host
  uint16_t packetLength = HEADER_LEN;
  memcpy(&packetLength, packet, 2);
  packetLength = ntohs(packetLength);
  
  //copy returncode and block from packet
  uint32_t recievedOp = 0;
  memcpy(&recievedOp, ((char *)packet + 2), 4);
  *op = ntohl(recievedOp);

  uint16_t recievedRet = 0;
  memcpy(&recievedRet, ((char *)packet + 6), 2);
  *ret = ntohs(recievedRet);
  
  //if there is a block, JBOD_READ_BLOCK was called
  if(packetLength != HEADER_LEN){
    //fill block from packet
    memcpy(block, (char *)packet + 8, 256);
  }
  
  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {

  //create buf for packet
  uint8_t packet[264];
  uint16_t len = HEADER_LEN;
  uint16_t server_len;
  
  //increase the len by block len if op is JBOD_WRITE
  if(block != NULL){
    len += 256;
  }
  
  //create variables for other header besides returncode
  uint32_t sendOp = op;
  
  server_len = htons(len);
  sendOp = htonl(sendOp);

  //memcpy len & op into packet
  memcpy(packet, &server_len, 2);
  memcpy(((char *)packet + 2), &sendOp, 4);

  //copy block if it exists
  if(block != NULL){
    memcpy(((char *)packet + 8), block, 256);
  }

  //send the packet to server
  if(nwrite(sd, len, packet) == false){
    return false;
  }
  
  return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in caddr;
  
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if(inet_aton(ip, &caddr.sin_addr) == 0){
    return false;
  }
  
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if(cli_sd == -1){
    return false;
  }

  if(connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){
    return false;
  }

  return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  
  close(cli_sd);
  cli_sd = -1;
  
}

/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/

int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t returncode = 0;
  
  if(send_packet(cli_sd, op, block) == false){
    return -1;
  }
  
  if(recv_packet(cli_sd, &op, &returncode, block) == false) {
    return -1;
  }

  if(returncode == -1){
    return -1;
  }
  
  return 0;
}
