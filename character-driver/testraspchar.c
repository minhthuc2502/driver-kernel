/**
 * @file    testraspchar.c
 * @author  PHAM Minh Thuc
 * @date    7 April 2020
 * @version 0.1
 * @brief   A simple example in user space to test the character driver in kernel space
*/
#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<sys/ioctl.h>

typedef struct {                                                                                                        
  unsigned char read_count_h;                                                                                           
  unsigned char read_count_l;                                                                                           
  unsigned char write_count_h;                                                                                          
  unsigned char write_count_l;                                                                                          
  unsigned char device_status;                                                                                          
} status_t; 

#define MAGICAL_NUMBER 240
#define RCHAR_CLR_DATA_REGS _IO(MAGICAL_NUMBER, 0)
#define RCHAR_GET_STS_REGS  _IOR(MAGICAL_NUMBER, 1, status_t *)
#define RCHAR_RD_DATA_REGS  _IOR(MAGICAL_NUMBER, 2, unsigned char *)
#define RCHAR_WR_DATA_REGS  _IOW(MAGICAL_NUMBER, 3, unsigned char *)
#define BUFFER_LENGTH 256               ///< The buffer length (crude but fine)
#define NODE_DEVICE  "/dev/raspberrychar"

static char receive[BUFFER_LENGTH];     ///< The receive buffer from the LKM

int open_raspdev() {
   int fd = open(NODE_DEVICE, O_RDWR);
   if (fd < 0) {
      perror("Failed to open the device...");
      return errno;
   }
   return fd;
}

void close_raspdev(int fd) {
   close(fd);
}

void clear_data_raspdev() {
   int fd;
   fd  = open_raspdev();
   int ret = ioctl(fd,RCHAR_CLR_DATA_REGS);   
   close(fd);
   printf("%s data register in char device.\n", (ret < 0)?"Couldn't clear":"Clear");
}

void get_status_raspdev() {
   status_t status;
   unsigned int read_cnt, write_cnt;   
   int fd = open_raspdev();
   ioctl(fd, RCHAR_GET_STS_REGS, (status_t *)&status);
   close(fd);
   read_cnt = status.read_count_h << 8 | status.read_count_l;
   write_cnt = status.write_count_h << 8 | status.write_count_l;
   printf("Static: number of reading (%u) times, number of writing (%u) times\n", read_cnt, write_cnt); 
}

void control_read_raspchar() {
   unsigned char isReadable;
   status_t status;
   char c = 'n';
   printf("Do you want to enable reading on data registers? (y/n)");
   scanf("%[^\n]%*c",&c);
   if (c == 'y')
      isReadable = 1;
   else if (c == 'n')
      isReadable = 0;
   else return;
   int fd = open_raspdev();
   ioctl(fd, RCHAR_RD_DATA_REGS, (unsigned char *)&isReadable);
   ioctl(fd, RCHAR_GET_STS_REGS, (status_t *)&status);
   close(fd);
   if (status.device_status & 0x01)
      printf("Enabled to read data from data registers\n");
   else
      printf("Disable to read data from data registers\n");    
}

void control_write_raspchar() {                                                                                          
   unsigned char isWritable;                                                                                            
   status_t status;                                                                                                     
   char c = 'n';                                                                                                        
   printf("Do you want to enable writing on data registers? (y/n)");                                                   
   scanf("%[^\n]%*c",&c);                                                                                                      
   if(c == 'y')                                                                                                         
      isWritable = 1;                                                                                                    
   else if (c == 'n')                                                                                                   
      isWritable = 0;                                                                                                    
   else return;                                                                                                         
   int fd = open_raspdev();                                                                                             
   ioctl(fd, RCHAR_WR_DATA_REGS, (unsigned char *)&isWritable);                                                          
   ioctl(fd, RCHAR_GET_STS_REGS, (status_t *)&status);                                                                  
   close(fd);                                                                                                           
   if (status.device_status & 0x02)                                                                                      
      printf("Enabled to write data from data registers\n");                                                               
   else                                                                                                                 
      printf("Disable to write data from data registers\n");                                                               
}  
  
int main(){
   int ret, fd;
   char stringToSend[BUFFER_LENGTH];
   printf("Starting device test code example...\n");
   fd = open_raspdev();
   printf("Type in a short string to send to the kernel module:\n");
   scanf("%[^\n]%*c", stringToSend);                // Read in a string (with spaces)
   printf("Writing message to the device [%s].\n", stringToSend);
   ret = write(fd, stringToSend, strlen(stringToSend)); // Send the string to the LKM
   if (ret < 0){
      perror("Failed to write the message to the device.");
      return errno;
   }
   close_raspdev(fd);
   // Open device file to read data register
   fd = open_raspdev();
   printf("Press ENTER to read back from the device...\n");
   getchar();
 
   printf("Reading from the device...\n");
   ret = read(fd, receive, BUFFER_LENGTH);        // Read the response from the LKM
   if (ret < 0){
      perror("Failed to read the message from the device.");
      return errno;
   }
   printf("The received message is: [%s]\n", receive);
   close_raspdev(fd);
   clear_data_raspdev();
   get_status_raspdev();
   control_read_raspchar();
   control_write_raspchar();      
   printf("End of the program\n");
   return 0;
}
