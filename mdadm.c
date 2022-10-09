#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "mdadm.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"

int mounted = -1;
uint32_t blockID = 0;
uint32_t diskID = 0;
uint32_t currentAddress = 0;

//mounts the disk
int mdadm_mount(void) {
  if(mounted == -1){
    jbod_cmd_t command = JBOD_MOUNT;
    command = command << 26;
    uint32_t op = command;
    
    if(jbod_client_operation(op, NULL) == 0){
      mounted = 1;
      return 1;
    }
  }
  return -1;
}

//unmounts the disk
int mdadm_unmount(void) {
  if(mounted == 1){
    jbod_cmd_t command = JBOD_UNMOUNT;
    command = command << 26;
    uint32_t op = command;
    
    if(jbod_client_operation(op, NULL) == 0){
      mounted = -1;
      return 1;
    }
  }
  return -1;
}

//given the address and command, returns the ORd bits
uint32_t address_and_command_to_op(uint32_t address, uint32_t command){
  return (address | command);
}

//given the current address, calls jbod_operation to seek to the correct disk
void address_to_disk(uint32_t a){
  jbod_cmd_t command = JBOD_SEEK_TO_DISK;
  command = command << 26;
  diskID = a/65536;
  
  uint32_t diskIDFormatted = diskID << 22;
  uint32_t op = address_and_command_to_op(diskIDFormatted, command);

  jbod_client_operation(op, NULL);
}

//given the current address, calls jbod_operation to seek to the correct block
void address_to_block(uint32_t a){
  jbod_cmd_t command = JBOD_SEEK_TO_BLOCK;
  command = command << 26;
  blockID = (a - (diskID)*65536)/256;
  uint32_t op = address_and_command_to_op(blockID, command);

  jbod_client_operation(op, NULL);
}

//given the length left to read and the bit position in a block, returns the number of bytes to read in that block
int bits_left_to(uint32_t len, uint32_t a){
  if((a + len) > 256){
    return (256 - a);
  }
  return len;
}

//given the address, length, and a buffer, this function fills the buffer with the information read in 
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  if((buf == NULL)){
    if(len == 0){
      return 0;
    }
      return -1;
   }
  
  //boundary checks
  if((mounted == 1) && ((len+addr)<=1048576) && (len<=1024)){

    currentAddress = addr;
    address_to_disk(currentAddress);
    address_to_block(currentAddress);
    uint32_t lenLeft = len;
    uint8_t tempBuf[256];
    int bitPositionInBlock = (currentAddress - (diskID*65536) - (blockID*256));//contains the position in the block where read starts off

    
    
    while(lenLeft > 0){
      
      int leftToRead = bits_left_to(lenLeft, bitPositionInBlock);//the amount of bits that left to be read within the current block
      
      if((cache_enabled() == true)){//Cache is enabled
	if((cache_lookup(diskID, blockID, tempBuf)==1)){//cache contains block, buf is filled
	  memcpy(((char *)buf + (len - lenLeft)), &tempBuf[bitPositionInBlock], leftToRead);
	  currentAddress += leftToRead;
	  address_to_block(currentAddress);//manually increment block
	  
	}else{//cache does not contain block
	    
	    uint32_t op = (JBOD_READ_BLOCK << 26);
	    jbod_client_operation(op, tempBuf);
	    memcpy(((char *)buf + (len - lenLeft)), &tempBuf[bitPositionInBlock], leftToRead);
	    currentAddress += leftToRead;
	    
	    if(cache_insert(diskID, blockID, tempBuf) == 1){}else{
	      cache_update(diskID, blockID, tempBuf);
	    }
	  }
      }else{//Cache disabled, read through jbod
	    
	uint32_t op = (JBOD_READ_BLOCK << 26);
	jbod_client_operation(op, tempBuf);
	memcpy(((char *)buf + (len - lenLeft)), &tempBuf[bitPositionInBlock], leftToRead);
	currentAddress += leftToRead;
	
      }
      
      lenLeft -= leftToRead;
      bitPositionInBlock = 0;
      
      if((currentAddress - (diskID*65536)) > 65536){//switches to the next disk because the end of the last block has been reached
	address_to_disk(currentAddress);
      }
     }
    
    return len;
  }
  
  return -1;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if((buf == NULL)){// empty buffer
    if(len == 0){
      return 0;
    }
      return -1;
  }
  
  if((mounted != 1) || ((len+addr)>1048576) || (len>1024)){//Not mounted or bad linear address and size
    return -1;
  }

  currentAddress = addr;
  address_to_disk(currentAddress);
  address_to_block(currentAddress);
  uint32_t lenLeft = len;//variable to hold the remaining bytes left to be read
  uint8_t tempBuf[256];
  int bitPositionInBlock = (currentAddress - (diskID*65536) - (blockID*256));//Where the addr falls in a block (0-255)

  while(lenLeft > 0){
    
      uint32_t op2 = (JBOD_WRITE_BLOCK << 26);
      int leftToWrite = bits_left_to(lenLeft, bitPositionInBlock);//The bytes left inside the block that need written to

      int priorAddr = currentAddress;
      mdadm_read((currentAddress - (currentAddress%256)), 256, tempBuf);//Fills tempBuf with whats currently inside of the block
      currentAddress = priorAddr;//reset currentAddress to the address it was at before being incremented
      
      address_to_disk(currentAddress);//seeks to the correct block after being auto-incremented
      address_to_block(currentAddress);
      
      memcpy(&tempBuf[bitPositionInBlock], &buf[(len - lenLeft)], leftToWrite);//Copies what needs to be written into tempBuf

      if((cache_enabled() == true)){//Cache is enabled
	if(cache_insert(diskID, blockID, tempBuf) == 1){}//Inserted into cache
	else{//already in cache
	  cache_update(diskID, blockID, tempBuf);
	}
      }

      jbod_client_operation(op2, tempBuf);
      currentAddress += leftToWrite;
      lenLeft -= leftToWrite;
      bitPositionInBlock = 0;
      
      if((currentAddress - (diskID*65536)) >= 65536){//Switches to the next disk because the end of the last block has been reached
	address_to_disk(currentAddress);
      }
  }
  
  return len;
}
