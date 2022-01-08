/*
======================================================================
Author:		Gabriel Racz
Version: 	1.2.1
Start Date:	June 03 2021

Changes:
[1.1]
Features:	-Hash an input of 54 characters or less (single 512 bit block)
			-Hashing speed test
			-Finding hash with specified number of leading zeroes
			-200,000 hashrate
[1.2]
			-Option to turn off output, leads to much higher performance
			 especially on Windows where syscalls to printf are painful
			-Multi-processing implemented. Can now run multi-core benchmarks.
				~Best performance using all cores and threads available as
				 processes count
				~(Need to test fork() on Windows).
			-15,000,000 hashrate on 8 core Ryzen 7 turbo enabled.

[1.2.1]
			-xorshift128 to generate random characters instead of sprintf() and rand()
			-23,000,000 hashrate
=====================================================================
WARNING: Only produces correct results on 64-bit machines
*/

#include<stdio.h>
#include<string.h>
#include<math.h>
#include<stdint.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#define MAX_INT 4294967295

static const uint32_t k[] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t ha[] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

unsigned char g_show_output = 0;

//-------------------Compound Functions-------------------
int SHR(uint32_t num, int shift){
	return num >> shift;
}

int ROTR(uint32_t num, int shift){
	return (num >> shift) | (num << (32 - shift));
}

int sig0(int x){
	return(ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3));
}

int sig1(int x){
	return(ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10));
}

int bigsig0(int x){
	return(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22));
}

int bigsig1(int x){
	return(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25));
}

int choice(uint32_t x, uint32_t y, uint32_t z){
	return (x & y) ^ ((~x) & z);
	
	
	//The worst choice implementation possible
	/*
	char bits = 32;
	short bit;
	int result = 0;
	int n;
	for(int i = 0; i < 32; i++){
		n = bits-i-1;
		bit = (x & (1 << n)) >> n;
		if(bit == 1){
			char temp = (y & (1 << n)) >> n;
			if(temp == 1)
				result = result | (1 << n);

		}else{
			char temp = (z & (1 << n)) >> n;
			if (temp == 1)
				result = result | (1 << n);
		}
	}

	return result;
	*/
}

int majority(uint32_t x, uint32_t y, uint32_t z){
	return (x&y)^(x&z)^(y&z);
}

//-------------------Binary print functions-----------------

void printIntBinary(uint32_t num){
	int bits = sizeof(num) * 8;
	short bit;
	for(int i = 0; i < bits; i++){
		bit = (num & (1 << bits-i-1)) >> bits-i-1;
		printf("%d", bit);
	}	
	printf("\n");
}

void printIntArrayBinary(int* nums){
	int bits = 32;
	short bit;
	uint32_t num;
	for(int j = 0; j < 16; j++){
		num = nums[j];
		for(int i = 0; i < bits; i++){
			bit = (num & (1 << bits-i-1)) >> bits-i-1;
			printf("%d", bit);
		}
		printf(",");
	}
	printf("\n");
}

void printCharBinary(unsigned char num){
	int bits = sizeof(num) * 8;
	short bit;
	for(int i = 0; i < bits; i++){
		bit = (num & (1 << (bits-i-1))) >> (bits-i-1);
		printf("%d", bit);
	}	
	printf("\n");
}


void printLongBinary(unsigned long num){
	int bits = 64;
	//printf("%d\n", bits);
	short bit;
	for(int i = 0; i < bits; i++){
		bit = (num & (1 << bits-i-1)) >> bits-i-1;
		printf("%d", bit);
	}	
	printf("\n");
}


//-------------------------Core Functions----------------------------------------

void processInput(uint32_t* dest, char* input, int len){
	//1. Put the char binary data in order into the int[]
	//2. Add a 0b1000000 at the end of the message
	//3. Add the length of the message into the last 2 slots (64 bits) of the chunk.

	uint32_t curr = 0;
	uint32_t elem;
	int block = 0;
	int cycle;
	for(int i = 0; i < len; i++){
		cycle = (i+1) % 4;
		if(cycle == 0){
			elem += input[i];
			curr = elem;
			dest[block] = curr;
			block++;
		}else if(cycle == 1){
			elem = input[i] << 24;
		}else if(cycle == 2){
			elem += input[i] << 16;
		}else if(cycle == 3){
			elem += input[i] << 8;
		}
	}
	if (cycle > 0)	//cleanup when exiting out of incomplete block
		dest[block] = elem;
	
	//Add 1 between message and padding
	int seperator = 0b1000000;
	cycle = (cycle+1)%4;
	dest[block] += seperator << (33 - cycle*8); 

	//Add length to last two int blocks
	//Limit of 32 bit int is 2^32 or 4294967296
	dest[15] = len*8;
}


//Create message schedule using the 512 bits of message.	
void createMessageSchedule(int* sched, int* chunk){
	memcpy(sched, chunk, sizeof(chunk)*8);

	for(int t = 16; t < 64; t++)
		sched[t] = sig1(sched[t-2]) + sched[t-7] + sig0(sched[t-15]) + sched[t-16];

	return;
}

//Compression of message schedule into 8 registers using compound functions.
void compression(int* sched, char* output){
	uint32_t a = ha[0];
	uint32_t b = ha[1];
	uint32_t c = ha[2];
	uint32_t d = ha[3];
	uint32_t e = ha[4];
	uint32_t f = ha[5];
	uint32_t g = ha[6];
	uint32_t h = ha[7];

	for(int i=0; i<64; i++){
		uint32_t T1 = bigsig1(e) + choice(e,f,g) + h + k[i] + sched[i];
		uint32_t T2 = bigsig0(a) + majority(a,b,c);

		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;
	}

	
	//Add original hash values to compressed registers.
	//TO DO: save the final registers to the ha array.
	a += ha[0];
	b += ha[1];
	c += ha[2];
	d += ha[3];
	e += ha[4];
	f += ha[5]; 
	g += ha[6];
	h += ha[7];
	
	output[0] = (unsigned char) (a >> 24);
	output[1] = (unsigned char) (a >> 16);
	output[2] = (unsigned char) (a >> 8);
	output[3] = (unsigned char) a;
	output[4] = (unsigned char) (b >> 24);
	output[5] = (unsigned char) (b >> 16);
	output[6] = (unsigned char) (b >> 8);
	output[7] = (unsigned char) b;
	output[8] = (unsigned char) (c >> 24);
	output[9] = (unsigned char) (c >> 16);
	output[10] = (unsigned char) (c >> 8);
	output[11] = (unsigned char) c;
	output[12] = (unsigned char) (d >> 24);
	output[13] = (unsigned char) (d >> 16);
	output[14] = (unsigned char) (d >> 8);
	output[15] = (unsigned char) d;
	output[16] = (unsigned char) (e >> 24);
	output[17] = (unsigned char) (e >> 16);
	output[18] = (unsigned char) (e >> 8);
	output[19] = (unsigned char) e;
	output[20] = (unsigned char) (f >> 24);
	output[21] = (unsigned char) (f >> 16);
	output[22] = (unsigned char) (f >> 8);
	output[23] = (unsigned char) f;
	output[24] = (unsigned char) (g >> 24);
	output[25] = (unsigned char) (g >> 16);
	output[26] = (unsigned char) (g >> 8);
	output[27] = (unsigned char) g;
	output[28] = (unsigned char) (h >> 24);
	output[29] = (unsigned char) (h >> 16);
	output[30] = (unsigned char) (h >> 8);
	output[31] = (unsigned char) h;
}

//main sha256 routine
void sha256(char* message, long len, char* output){
	
	uint32_t chunk[16] = { 0 }; 
	processInput(chunk, message, len);

	uint32_t message_schedule[64] = {0};
	createMessageSchedule(message_schedule, chunk);
		
	
	compression(message_schedule, output);
	return;
}

//---------------------Use cases----------------------------------------

//https://riptutorial.com/c/example/25123/xorshift-generation
uint32_t w, x, y, z;
uint32_t xorshift128(void) 
{
    uint32_t t = x;
    t ^= t << 11U;
    t ^= t >> 8U;
    x = y; y = z; z = w;
    w ^= w >> 19U;
    w ^= t;
    return w;
}

void clearKeyboardBuffer(void){
	char ch;
	while(ch = getchar() != '\n'&& ch != EOF);
}

int speedTest(int max){
	time_t t;
	srand((unsigned) time(&t));

	char message[56] = {'\0'};
	
	//Set the xorshift registers	
	w = rand();
	x = rand();
	y = rand();
	z = rand();

	long count = 0;
	uint32_t max_hash = max  - 1;
	int len = 30;
	unsigned char output[64];
	while(count < max_hash){
		count++;
		//Randomize the string
		for(int i = 0; i < len; i++){
			message[i] = (xorshift128() % 74) + 48;
		}

		sha256(message, len, output);
		
		if(g_show_output){
			printf("[%ld] ", count);
			printf("%s   ", message);
			for(int i =0; i < 32; i++){
				printf("%02x", output[i]);
			}
			printf("\n");
		}
	}
	return 0;
}

void takeSpeed(uint32_t total_max, char num_threads){
	int worker_max = total_max / num_threads;

	if(!g_show_output){
		printf("\nRunning speed test on %d core(s)...\n", num_threads);
	}
	
	//Init timers
	struct timespec start, finish;
	clock_gettime(CLOCK_MONOTONIC, &start);

	//Spawn worker processes
	pid_t* pids = malloc(sizeof(pid_t) * num_threads);
	pid_t child_pid;
	int* status;
	for(int i = 0; i < num_threads; i++){
		child_pid = fork();
		if(child_pid == 0){
			break;
		}else{
			pids[i] = child_pid;
		}	
	}
	if(child_pid != 0){
		for(int i = 0; i < num_threads; i++){
			waitpid(pids[i], status, NULL);
		}
	}else{
		speedTest(worker_max);
		exit(0);
	}

	free(pids);
	
	//Timing and output
	clock_gettime(CLOCK_MONOTONIC, &finish);
	double delta;
	delta = (finish.tv_sec - start.tv_sec);
	delta += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
	double hashes_per_second = (total_max + 1)/delta;
	
	printf("\n");
	printf("Hashes         %ld\n", total_max);
	printf("Delta          %lfs\n", delta);
	printf("Hash/Sec       %d\n", (int)floor(hashes_per_second));
}

int hashInput(void){
	char message[56];	
	unsigned char output[64];

	fgets(message, 56, stdin);
	long len = strlen(message);
	if(message[len-1] != '\n')
		clearKeyboardBuffer();
	message[len-1] = '\0';
	len--;

	//Exit case
	if(message[0] == '!' && message[1] == '!')
		return 1;

	sha256(message, len, output);
  
  	printf("%s", message);
    printf("   ");
	for(int i =0; i < 32; i++){
		printf("%02x", output[i]);
	}
	printf("\n");
	return 0;
}

void findHash(int zeroes){
	time_t t;
	srand((unsigned)time(&t));
	clock_t begin_time = clock();
	long count = 0;
	char message[56];
	unsigned char output[64];
	while(1){
		count++;
		sprintf(message, "%08x%08x%08x%08x", rand(), rand(), rand(), rand());
		int len = strlen(message);
		sha256(message, len, output);

	
		if(g_show_output){
			printf("[%ld] %32s       ", count,  message);
			for(int i =0; i < 32; i++){
			printf("%02x", output[i]);
			}
			printf("\n");
		}
		
		for(int i = 0; i < zeroes; i++){
			if(output[i] == 0){
				if (i == zeroes - 1){
					printf("\n\nHash found!\n");
					printf("[%ld] %32s       ", count,  message);

					for(int i =0; i < 32; i++){
						printf("%02x", output[i]);
					}
					printf("\n");
					
					float delta = (float)(clock() - begin_time)/CLOCKS_PER_SEC;
					printf("Delta     %lf\n", delta);
					long int idelta = round(delta);	
					FILE* fptr = fopen(".treasure", "a");
					float seconds = idelta % 60 + (delta - idelta);
					int minutes = (idelta - seconds)/60;
					int hours = (idelta - (idelta % 3600))/60;

					fprintf(fptr, "  (%d)[%ld]      %s      %.3lf\n", zeroes, count, message, delta);
					fclose(fptr);	
					return;
				}	
			}else{
				break;
			}
		}
	}
}


//-----------------------User Input---------------------------------

int main(int argc, char* argv[]){
	int exit = 0;
	while(exit != 1){
		printf("\n  SHA-256\n");
		printf("1. hash\n");
		printf("2. single-core benchmark\n");
		printf("3. multi-core benchmark\n");
		printf("4. mine\n");
		printf("\n$");
		int cho = 0;
		int exit_hash = 0;
		scanf("%d", &cho);
		getchar();
		switch(cho){
			case 1:
				while(exit_hash != 1){
					printf("\n$");
					exit_hash = hashInput();
				}
				break;
			case 2:
				printf("\nduration(1-4): \n");
				printf("$");
				int cho_2;
				scanf("%d", &cho_2);
				clearKeyboardBuffer();
				
				printf("\nshow output? (y/n):\n");
				printf("$");
				char ch;
				ch = getchar();
				if(ch == 'y' || ch == 'Y'){
					g_show_output = 1;
				}else {
					g_show_output = 0;
				}
	
				switch(cho_2){
					case 1:
						takeSpeed(100000, 1);
						break;
					case 2:
						takeSpeed(300000, 1);
						break;
					case 3:
						takeSpeed(1000000, 1);
						break;
					case 4:
						takeSpeed(10000000, 1);
						break;
					case 5:
						takeSpeed(MAX_INT, 1);
						break;
					default:
						break;
				}
				cho = 0;
				cho_2 = 0;
				break;
			case 3:
				printf("\nprocesses:\n");
				printf("$");
				int procs;
				scanf("%d", &procs);
				clearKeyboardBuffer();
		
				printf("\nduration(1-4): \n");
				printf("$");
				scanf("%d", &cho_2);
				clearKeyboardBuffer();
				g_show_output = 0;	
				switch(cho_2){
					case 1:
						takeSpeed(5000000, procs);
						break;
					case 2:
						takeSpeed(50000000, procs);
						break;
					case 3:
						takeSpeed(100000000, procs);
						break;
					case 4:
						takeSpeed(1000000000, procs);
						break;
					case 5:
						takeSpeed(MAX_INT, procs);
						break;
					default:
						break;
				}
				cho = 0;
				break;
			case 4:
				printf("\nleading zeroes:\n");
				printf("$");\
				int zeroes;
				scanf("%d", &zeroes);
				clearKeyboardBuffer();
				
				printf("\nshow output? (y/n):\n");
				printf("$");
				char chr;
				chr = getchar();
				if(chr == 'y' || chr == 'Y'){
					g_show_output = 1;
				}else {
					g_show_output = 0;
				}
				
				findHash(zeroes);
				break;
			default:
				exit = 1;
				break;
		}
	}
	return 0;
}
