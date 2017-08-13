#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<linux/nvme_ioctl.h>
#include<sys/ioctl.h>
#include<fcntl.h>
#include<time.h>
#include<string.h>
#include<linux/types.h>
#include<pthread.h>
//#define DEBUG
//#define DISPLAY_READ/*display the read-data by every thread and then close the thread*/
//#define IOCTL_FREE  /*will not send any IOCTL and the values will be the maximum througput the code can measure*/

#define BUFFER_MUL 2/*making buffer 32 MiB*/
#define CLOCK CLOCK_REALTIME
#define IDENTIFY_LEN 0x1000
#define BLOCK_MAX 65535 /*NVME provides 16 bits for "number of sectors" to be read in one go*/
#define THREADS_MAX (64*1024) /*number of threads can be anythig: if it is less than QD than number of threads is the new QD*/
#define BUFFER_SIZE 16*1024*1024*BUFFER_MUL/*buffer size is 16MiB */
#define SAMPLE_SEC 10 /*it is the granularity in which Throughput calculation is done*/

#define RANDOM 1
#define SEQ 2
#define MIX_UNLIMITED 1 
#define MIX_LIMITED 2
#define ALIGNED 1
#define UNALIGNED 2
#define MIX_ALIGNMENT 3 /*mix-alignment does not bother about LBA value: one of the most gruelling and unforgiving test*/
#define READ_OPCODE 0x02
#define WRITE_OPCODE 0x01

int** data_buffer;/*this holds the 2D array of data buffer from where we take our data to write*/
/*data_buffer is prepare before testing starts depending upon Entropy and Block-size; it has been done to minimize the delay incurred by rand()*/
/*It is quite slow too fill the buffer after every IO, using rand()*/
__u64 io;/*it is incremented after every IO by every thread*/
__u64 read_io;/*it is incremented by read-threads*/
__u64 write_io;/*it is incremented by write-threads*/
int block_size ;/*variable*/
int sector_size = 512;
int ns= 1; /*namespace id*/
__u64 seq_lba ;/*this keeps the sequential LBA*/
int alignment;/*a variable to store the type of alignment(aligned;unaligned;mix-aligned) */
int alignment_t;/*vaiable to store the type and later size of alignment*/
char* file_name;/*it holds the device file name*/
__u64 lba_max;/*to be filled by identify function*/
int end_of_test = 1,row,col; /* end-of-test must be 1 otherwise threads will end*/
int read_per,write_per;

typedef struct thread_arg {
	int thread_id;/*thread id*/
}argument;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

///function prototyping
void* th_rand_write(void*);
void* th_rand_read(void*);
void* th_seq_write(void*);
void* th_seq_read(void*);
//void* th_rand_mix(void*);
//void* th_seq_mix(void*);
int con_identify();
int ns_identify();

char* fill();


int main(int argc, char* argv[]){
	int random_seq = 0;/*holds the type of test: random or sequential*/
	int size = 0;/*test size*/
	int seed = 5;/*variable to store new rand() function seed*/
	__u64 c_io =0;/* variable to hold number of IO completed in one SAMPLE_SEC*/
	double c_iops = 0;/*IOPS of c_io*/
	int temp,ret,read_th/*no of read threads*/,write_th/*no of write threads*/;
	register int loop;/*variable for looping; hence register storage class*/
	int no_of_threads,entropy;
	float iops,read_iops,write_iops;
	double latency = 0;/* keep the time*/
	double t_latency = 0;/*total duration for which test runs*/
	float throughput;
	int active_range ;/*range of LBAs to consider for test */
	__u64 required_io;/*it is calculated from size*/
	struct timespec time1,time2;//to measure the latency
	if(argc<2){
		printf("Please enter the device file name\n");
		printf("for example: %s /dev/nvme<>n< >\n",argv[0]);
		return 1;
	}else file_name = argv[1];
	
	temp = con_identify();
	if(temp > 0){ printf("Identify Failed\n"); return 1;}
	temp = ns_identify();
	if(temp > 0){ printf("Identify Failed\n"); return 1;}
	lba_max = lba_max -2;
	printf("lba max: %lld \t sector size: %d\n",lba_max,sector_size);
	
	do{
		printf("Press 1 for Random IOs; Press 2 for Sequintial IOs:  ");
		scanf("%d",&random_seq);
	}while(random_seq < 1 || random_seq > 2);
	
	do{			
		printf("Enter the Read Percentage: ");
		scanf("%d",&read_per);

	}while(read_per > 100 || read_per < 0);
	write_per = 100 - read_per;
	

	do{
		printf("Enter the Block Size i.e. No-of-sectors (in units of 512B): ");
		scanf("%d",&block_size);
	}while(block_size < 1 || block_size > BLOCK_MAX);
	if(random_seq == RANDOM){		
		printf("Alignment is required to simulate or avoid read-modify and multiple-page reads\n");/*for seq alignment must be taken care with LBA and Block_size*/	
		do{
			printf("Press 1 for Aligned IOs; Press 2 for UnAligned IOs; 3 for Mix-Alignment: ");
			scanf("%d",&alignment);

		}while(alignment < 1|| alignment > 3);

		if(alignment == 1 || alignment == 2){
			do{
				printf("Press 1 for 4K Alignment/Un-Alignment; Press 2 for 8K; and Press 3 for 16K: ");
				scanf("%d",&alignment_t);
			}while(alignment_t < 1 || alignment_t > 3);
			switch(alignment_t){
				case 1: alignment_t = (0x4);
					break;
				case 2: alignment_t = (0x8);
					break;
				case 3: alignment_t = (0x10);
					break;
				default:printf("Alignment was something weird\n");
					return 1;

			}//switch
		}
	}//if RANDOM

	printf("Active Range is the range of LBAs to be considered for the test\n");
	
	if(random_seq == RANDOM){

		do{
			printf("Enter Active Range(percentage): ");
			scanf("%d",&active_range);	
		}while(active_range<0||active_range>100);
		lba_max = (lba_max*active_range/100)-block_size ;
	}else{	
		do{
			printf("Enter start LBA: ");
			scanf("%lld",&seq_lba);
		}while(seq_lba<0||seq_lba>lba_max);

		do{/*with the use of active_range and start_lba any range can be tested*/
			printf("Enter Active Range(percentage): ");
			scanf("%d",&active_range);	
		}while(active_range<0||active_range>100);
		lba_max = (lba_max*active_range/100)-block_size;
		
		if(lba_max<seq_lba){
			printf("start LBA should be lower than Active Range\n");
			return 1;
		}
	}

	printf("Enter the Size of the test (in MiB): ");
	scanf("%d",&size);

	required_io = size*1024/sector_size*1024/block_size;
	//printf("Required IOs: %lld\n",required_io);

	do{
		printf("Enter the Number of Threads: ");
		scanf("%d",&no_of_threads);		
	}while(no_of_threads < 1 || no_of_threads > THREADS_MAX);
	
	read_th = read_per*no_of_threads/100;/*since "int" is being used here the value will be floor value(i.e lowest integer) but as reads are comparetively faster than writes a thread less for read wont screw much; also we cannot have threads in decimal we will have to favour either read or write. i am going for write since its slow */
	write_th = no_of_threads - read_th;

	argument arg[no_of_threads];
	pthread_t th[no_of_threads];
	int thread_status[no_of_threads];

	if(read_per < 100){/*performed when there are some writes*/
		row = BUFFER_SIZE/sector_size/block_size;
		col = block_size*sector_size/sizeof(int);
		data_buffer = (int**)malloc(row*sizeof(int*));

		if(data_buffer == NULL){
			printf("could not manage space for data buffer: malloc failed\n");
			return 1;
		}
		for(loop = 0; loop< row;loop++){
			data_buffer[loop]=(int*)malloc(col*sizeof(int));
			if(data_buffer[loop] == NULL){ 
				printf("memory allocation for %d row failed\n",loop);
				return 1;
			}	
		}
	
		printf("Enter the Seed for Random data: ");
		scanf("%d",&seed);//keeping the seed variable
		srand(seed);
		do{
			printf("Enter the Entropy :");
			scanf("%d",&entropy);
		}while(entropy > 100 || entropy < 10 );
		
		printf("Preparing the Data Buffer\n");
		clock_gettime(CLOCK,&time1);
		for(int i = 0;i<row;i++){
			for(loop = 0;loop< col*entropy/100;loop++){
				data_buffer[i][loop] = rand();
			}
			for(;loop<col;loop++){
				data_buffer[i][loop] = 0;
			}
		}
		clock_gettime(CLOCK,&time2);	
		latency = time2.tv_sec - time1.tv_sec + (double)((time2.tv_nsec - time1.tv_nsec)/1000000000.0) ;
		printf("Buffer Preperation took: %f sec\n",latency);
		printf("Size of Buffer %d MiB\n",sizeof(data_buffer)/1024/1024);
	}
/*Displaying all the parameters for the Test*/	
	printf("\n\nDETAILS OF TEST :\n");
	printf("READ PERCENTAGE: %d	WRITE PERCENTAGE: %d\n",read_per,write_per);
	printf("DATA TRANSFER SIZE PER IO: %d bytes\n",block_size*sector_size);
	if(alignment == 1)
		printf("IOs will be %dK Aligned\n",alignment_t);
	else if(alignment == 2)
		printf("IOs will be %dK Un-Aligned\n",alignment_t);
	else if(alignment == 3)
		printf("Mix-Alignment\n");
	printf("Test Size: %d GiB\t\tNumber Of IOs: %lld\n",size/1024,required_io);
	printf("Number of threads: %d\n",no_of_threads);
	printf("Read threads: %d\t\tWrite_threads: %d\n",read_th,write_th);	

	printf("Active Range: %d percent\n",active_range);

	if(read_per<100)	
		printf("Entropy: %d\n",entropy);

	pthread_mutex_lock(&lock);/*to avoid IO generation untill all threads are up and running*/
/*creating threads for parllel execution and queueing*/
	if(random_seq == RANDOM){	
		for (loop = 0;loop< write_th;loop++){		
			arg[loop].thread_id = loop;
#ifdef DEBUG
			printf("Creating Write Thread:TI: %d\n",arg[loop].thread_id);
#endif			
			ret = pthread_create(&th[loop],NULL,th_rand_write,(void*)&arg[loop]);
#ifdef DEBUG
			if(ret) printf("Thread creation failed:TI: %d\n",arg[loop].thread_id);
#endif
			thread_status[loop] = ret;
		}
		for (;loop< no_of_threads;loop++){		
			arg[loop].thread_id = loop;
#ifdef DEBUG
			printf("Creating Read Thread:TI: %d\n",arg[loop].thread_id);
#endif
			ret = pthread_create(&th[loop],NULL,th_rand_read,(void*)&arg[loop]);
#ifdef DEBUG
			if(ret) printf("Thread creation failed:TI: %d\n",arg[loop].thread_id);
#endif
			thread_status[loop] = ret;
		}
	}else {	
		for (loop = 0;loop< write_th;loop++){		
			arg[loop].thread_id = loop;
#ifdef DEBUG
			printf("Creating Write Thread:TI: %d\n",arg[loop].thread_id);
#endif
			ret = pthread_create(&th[loop],NULL,th_seq_write,(void*)&arg[loop]);
#ifdef DEBUG
			if(ret) printf("Thread creation failed:TI: %d\n",arg[loop].thread_id);
#endif
			thread_status[loop] = ret;
		}
		for (;loop< no_of_threads;loop++){		
			arg[loop].thread_id = loop;
#ifdef DEBUG
			printf("Creating Read Thread:TI: %d\n",arg[loop].thread_id);
#endif
			ret = pthread_create(&th[loop],NULL,th_seq_read,(void*)&arg[loop]);
#ifdef DEBUG
			if(ret) printf("thread creation failed:TI: %d\n",arg[loop].thread_id);
#endif
			thread_status[loop] = ret;
		}
	}
/***********************************sampling code********************/
	while(io < required_io){
		clock_gettime(CLOCK,&time1);
		pthread_mutex_unlock(&lock);/*start the IO generation*/
		sleep(SAMPLE_SEC);
		pthread_mutex_lock(&lock);
		clock_gettime(CLOCK,&time2);

		latency = time2.tv_sec - time1.tv_sec + (double)((time2.tv_nsec -time1.tv_nsec)/1000000000.0) ;
		t_latency += latency;
		iops = io/t_latency;
		c_iops = (io - c_io)/latency;
		read_iops = read_io/t_latency;
		write_iops = write_io/t_latency;
		throughput = iops*block_size/2048.0;
		printf("***************\nTest Completed: %0.3f %% \t Time consumed(in IOs): %lf\n",(double)io*100/required_io,t_latency);
		printf("********\nAVG IOPS: %0.3f\t\tAVG THROUGHPUT(MiB/s): %0.3f\nCURRENT IOPS: %0.3f\t\tCURRENT THROUGHPUT(MiB/s): %0.3f\nREAD IOPS: %0.3f\t\tWRITE IOPS: %0.3f\nCURRENT LATENCY: %lf ms\n",iops,throughput,c_iops,c_iops*block_size/2048.0,read_iops,write_iops,(double)latency*1000/(io-c_io));
		c_io = io;
		
		
	}
	end_of_test = 0;
/*joining threads after the completion of test*/
	for(int i=0;i<no_of_threads;i++){
		if(thread_status[i]) continue;
#ifdef DEBUG
		printf("waiting for thread:TI: %d to join\n",i);
#endif
		pthread_join(th[i],NULL);
	}

return 0;
}


char* fill(){/*this function, upon calling returns a row which of size SECTOR_SIZE*block_size and offset get rollovered after 16MiB/(SECTOR_SIZE*block_size) times*/
	static int offset = -1;
	offset++;
	if(offset == row){
		offset = 0;/*rollover*/
	}
	
	return (char*)data_buffer[offset];
}


void* th_rand_read(void* par){
	argument* arg = (argument*)par;		
	int fd = 0;
	int ret;

	struct nvme_passthru_cmd nvme_cmd;
	memset(&nvme_cmd,0,sizeof(nvme_cmd));
	//unsigned char buffer[sector_size*block_size];
	//for(register int i = 0;i<sector_size*block_size;buffer[i++]=0);
	unsigned char* buffer = (char*)calloc(block_size,sector_size);	
	if(buffer == NULL){
		printf("TI: %d calloc failed\n",arg->thread_id);
		return NULL;
	}
	__u64 lba;
	
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("Device file opening failed\n");
		exit(1);
	}

	nvme_cmd.opcode = READ_OPCODE;
	nvme_cmd.addr =(int) buffer;
	nvme_cmd.nsid = ns;
	nvme_cmd.data_len = sector_size*block_size;
	nvme_cmd.cdw12 = block_size;
//	nvme_cmd.cdw13 = ;
//	nvme_cmd.cdw14 = ;
//	nvme_cmd.cdw15 = ;
	while(end_of_test){
		
		pthread_mutex_lock(&lock);

		io++;
		read_io++;

		pthread_mutex_unlock(&lock);

		lba = rand()% lba_max;/*lba_max would be percentage of active range*/
		if(alignment == ALIGNED){
			lba = lba & (~(alignment_t-1)); 
		}else if(alignment == UNALIGNED){
			lba = ((lba % alignment_t)!= 0)?lba:lba+1;
		}/*else do not care about the alignment */

		nvme_cmd.cdw10 = lba;
		nvme_cmd.cdw11 = (lba>>32);
#ifndef IOCTL_FREE		
		ret = ioctl(fd,NVME_IOCTL_IO_CMD,&nvme_cmd);
	
#ifdef DEBUG	
		if(ret==0)printf("TI: %d read successful\n",arg->thread_id);
#endif	
		if(ret!=0){
			 printf("TI: %d read failed %d\n",arg->thread_id,ret); 
			 exit(1);	
		}
#ifdef DISPLAY_READ
		printf("TI: %d buffer after the read(lba= %lld)\n",arg->thread_id,lba);
		for(register int i=0;i<sector_size*block_size;printf("%c",buffer[i++]));
		exit(1);
#endif
#endif
	}//while end of test

pthread_exit(NULL);
}


void* th_seq_read(void* par){
	argument* arg = (argument*)par;		
	int fd = 0;
	int ret;

	struct nvme_passthru_cmd nvme_cmd;
	memset(&nvme_cmd,0,sizeof(nvme_cmd));
	unsigned char* buffer = (char*)calloc(block_size,sector_size);
	if(buffer == NULL){
		printf("TI: %d calloc failed\n",arg->thread_id);
		return NULL;
	}
	
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("Device file opening failed\n");
		exit(1);
	}

	nvme_cmd.opcode = READ_OPCODE;
	nvme_cmd.addr =(int) buffer;
	nvme_cmd.nsid = ns;
	nvme_cmd.data_len = sector_size*block_size;
	nvme_cmd.cdw12 = block_size;
//	nvme_cmd.cdw13 = ;
//	nvme_cmd.cdw14 = ;
//	nvme_cmd.cdw15 = ;
	while(end_of_test){
		
		pthread_mutex_lock(&lock);
		io++;
		read_io++;

		seq_lba = (seq_lba + block_size)% lba_max;/*lba_max would be percentage of active range*/

		nvme_cmd.cdw10 = seq_lba;
		nvme_cmd.cdw11 = (seq_lba>>32);
		
		pthread_mutex_unlock(&lock);
#ifndef IOCTL_FREE		
		ret = ioctl(fd,NVME_IOCTL_IO_CMD,&nvme_cmd);
	
#ifdef DEBUG	
		if(ret==0)printf("TI: %d read successful\n",arg->thread_id);
#endif	
		if(ret!=0){
			 printf("TI: %d read failed %d\n",arg->thread_id,ret); 
			 exit(1);	
		}
#ifdef DISPLAY_READ
		printf("TI: %d buffer after the read(lba= %lld)\n",arg->thread_id,lba);
		for(register int i=0;i<sector_size*block_size;printf("%c",buffer[i++]));
		exit(1);
#endif
#endif
	}//while end of test



pthread_exit(NULL);
}

void* th_rand_write(void* par){
	argument* arg = (argument*)par;
	int fd = 0;

	int ret;
	struct nvme_passthru_cmd nvme_cmd;
	memset(&nvme_cmd,0,sizeof(nvme_cmd));
	//unsigned char buffer[sector_size*block_size];
	__u64 lba;

	
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("Device file opening failed\n");
		exit(1);
	}

	nvme_cmd.opcode = WRITE_OPCODE;
	//nvme_cmd.addr =(unsigned long long*) buffer;
	nvme_cmd.nsid = ns;
	nvme_cmd.data_len = sector_size*block_size;
	nvme_cmd.cdw12 = block_size;
	
	while(end_of_test){
		
		pthread_mutex_lock(&lock);
		nvme_cmd.addr = (int)fill();
		io++;
		write_io++;

		pthread_mutex_unlock(&lock);

		lba = rand()% lba_max;/*lba_max would be percentage of active range*/
		if(alignment == ALIGNED){
			lba = lba & (~(alignment_t-1)); 
		}else if(alignment == UNALIGNED){
			lba = ((lba % alignment_t)!= 0)?lba:lba+1;
		}/*else do not care about the alignment */

		nvme_cmd.cdw10 = lba;
		nvme_cmd.cdw11 = (lba>>32);
#ifndef IOCTL_FREE		
		ret = ioctl(fd,NVME_IOCTL_IO_CMD,&nvme_cmd);
	
#ifdef DEBUG	
		if(ret==0)printf("TI: %d write successful\n",arg->thread_id);
#endif	
		if(ret!=0){
			 printf("TI: %d write failed %d\n",arg->thread_id,ret); 
			 exit(1);	
		}
#endif
	}//while end of test
	
pthread_exit(NULL);

}

void* th_seq_write(void* par){
	argument* arg = (argument*)par;
	int fd = 0;
	int ret;
	struct nvme_passthru_cmd nvme_cmd;
	memset(&nvme_cmd,0,sizeof(nvme_cmd));
//	unsigned char buffer[sector_size*block_size];

	
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("Device file opening failed\n");
		exit(1);
	}

	nvme_cmd.opcode = WRITE_OPCODE;
	nvme_cmd.nsid = ns;
	nvme_cmd.data_len = sector_size*block_size;
	nvme_cmd.cdw12 = block_size;
	
	while(end_of_test){
		
		pthread_mutex_lock(&lock);

		nvme_cmd.addr = (int) fill();
		io++;
		write_io++;


		seq_lba = (seq_lba+block_size)% lba_max;/*lba_max would be percentage of active range*/

		nvme_cmd.cdw10 = seq_lba;
		nvme_cmd.cdw11 = (seq_lba>>32);
		
		pthread_mutex_unlock(&lock);
#ifndef IOCTL_FREE
		ret = ioctl(fd,NVME_IOCTL_IO_CMD,&nvme_cmd);
	
#ifdef DEBUG	
		if(ret==0)printf("TI: %d write successful\n",arg->thread_id);
#endif	
		if(ret!=0){
			 printf("TI: %d write failed %d\n",arg->thread_id,ret); 
			 exit(1);	
		}
#endif
	}//whilen end of test
	

pthread_exit(NULL);

}


int con_identify(){
int fd;
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("could not open device file\n");
		return 1;
	}
#ifdef DEBUG
	else printf("device file opened successfully\n");
#endif
	char data[IDENTIFY_LEN];
	for(register int i=0; i<IDENTIFY_LEN;data[i++]=0);
	struct nvme_admin_cmd cmd = {
		.opcode = 0x06,
		.nsid = 0,
		.addr = (int)data,
		.data_len = IDENTIFY_LEN,
		.cdw10 = 1,

	};
	

	int ret;

	ret= ioctl(fd,NVME_IOCTL_ADMIN_CMD,&cmd);
#ifdef DEBUG
	if(ret==0) printf("namespace identify successful \n");
#endif
	if(ret != 0) printf("namespace identify failed %d\n",ret);


	printf("CONTROLLER IDENTIFY DETAILS:\n\n");
//	for(register int i=0;i<IDENTIFY_LEN;printf("%c",data[i++]));
	printf("IDENTIFY DETAILS\n");
//	printf("PCI Vendor ID: %c%c\n", data[0],data[1]);
//	printf("PCI subsystem vendor ID: %c%c\n",data[2],data[3]);
	printf("Serial Number: ");
	for(int i =0; i<20; printf("%c",data[(i++)+4]));
	printf("\n");
	printf("Model Number: ");
	for(int i=0; i<40; printf("%c",data[(i++)+24]) );
	printf("\n");
	printf("Firmware Revision: ");
	for(int i = 0;i<8; printf("%c",data[(i++)+64]));
	printf("\n");
	printf("Maximum data transfer size 2^(in units of CAP.MPSMIN): %d\n", (int)data[77] );
	printf("Submission Queue Entry Size 2^:\n");
	printf("--maximum: %d\t", (int)(data[512]>>4));
	printf("--required: %d\n",(int)(data[512]&0x0f));
	printf("Completion Queue Entry Size 2^:\n");
	printf("--maximum: %d\t", (int)(data[513]>>4));
	printf("--required: %d\n",(int)(data[513]&0x0f));
	printf("Number of Namespaces: %d\n", *( (int* )(data+516)));
	if(data[525]&0x1)
		printf("Volatile Write Cache present\n");	
	printf("Number of Power States supported: %d\n",(int)data[263]);
	
return 0;
}

int ns_identify(){
int fd;
	fd = open(file_name,O_RDWR);
	if(fd == 0){
		printf("could not open device file\n");
		return 1;
	}
#ifdef DEBUG
	else printf("device file opened successfully\n");
#endif
	char data[IDENTIFY_LEN];
	for(register int i=0; i<IDENTIFY_LEN;data[i++]=0);
	struct nvme_admin_cmd cmd = {
		.opcode = 0x06,
		.nsid = 1,
		.addr = (int)data,
		.data_len = IDENTIFY_LEN,
		.cdw10 = 0,
	};
	

	int ret;

	ret= ioctl(fd,NVME_IOCTL_ADMIN_CMD,&cmd);
#ifdef DEBUG
	if(ret==0) printf("successful \n");
#endif	
	if(ret != 0) printf("failed %d\n",ret);

	printf("NAMESPACE SIZE(MAX LBA): %lld\n",*((__u64*)data));
	printf("NAMESPACE CAPACITY(ALLOWED LBA - THIN OVERPROVISIONING): %lld\n",*((__u64*)(data+8)));
	printf("NAMESPACE UTILIZATION: %lld\n",*((__u64*)(data+16)));
	if(data[24]&0x1)
		printf("Namespace supports Thin OverProvisioning(NAMESPZE CAPACITY reported may be less than SIZE)\n");
	else printf("Thin Overprovisioning not supported\n");
	printf("NUMBER OF LBA FORMATS: %d\n",(__u8)data[25]);
	printf("FORMATED LBA SIZE: %d\n",(__u8)data[26]);
	printf("LBA FORMATE 0: METADAT SIZE: %d\n", *((__u16*)(data+128)));
	printf("LBA FORMATE 0: LBA DATA SIZE(2^n): %d\n", *((__u8*)(data+130)));

	sector_size = *((__u8*)(data+130));
	sector_size = (1<<sector_size);
	lba_max = *((__u64*)data);

return 0;
}



