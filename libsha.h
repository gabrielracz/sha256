#include<stdio.h>
#include<string.h>
#include<stdint.h>
#include<stdlib.h>

#define MAX_INT 4294967295

/*first 32 bits of fractional part of the cube root of first 64 primes*/
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

/*first 32 bits of fractional part of the square roots of first 8 primes*/
static uint32_t hash[] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

	/*_________operations________ */
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

int sum0(int x){
	return(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22));
}

int sum1(int x){
	return(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25));
}

int choice(uint32_t x, uint32_t y, uint32_t z){
	return (x & y) ^ ((~x) & z);
}

int majority(uint32_t x, uint32_t y, uint32_t z){
	return (x&y)^(x&z)^(y&z);
}

		/*_____Core_____*/

void process_input(uint8_t* buffer, size_t bufbitlen, uint32_t* message, size_t msglen){
	// Pack the char binary data into 32bit word array
	// append 0b1 to end of data.
	// bit length of message in last 2 words.

	uint32_t elem = 0;
	uint32_t cword = 0;
	uint32_t cycle;
	for(size_t i = 0; i < bufbitlen; i++){
		cycle = (i+1) % 4;
		switch(cycle) {
			case 0:
				elem += buffer[i];
				message[cword++] = elem;
				break;
			case 1:
				elem = buffer[i] << 24;
				break;
			case 2:
				elem += buffer[i] << 16;
				break;
			case 3:
				elem += buffer[i] << 8;
				break;
		}
	}
	if (cycle > 0)	//cleanup when exiting out of incomplete block
		message[cword] = elem;

	uint32_t seperator = 0b10000000;
	message[cword] |= seperator << (24 - cycle*8);


	//Add length to last two int blocks
	//Limit of 32 bit int is 2^32 or 4294967296
	if(bufbitlen*8 > MAX_INT){
		message[msglen - 2] = (bufbitlen*8) >> 32;			//upper 32 bits
		message[msglen - 1] = (uint32_t)(bufbitlen*8);		//lower 32 bits
	}else{
		message[msglen - 1] = bufbitlen*8;
	}
}


//Create message schedule using the 512 bits of message.	
void create_message_schedule(uint32_t* sched, uint32_t* chunk){
	memcpy(sched, chunk, 16 * 4);

	for(int t = 16; t < 64; t++)
		sched[t] = sig1(sched[t-2]) + sched[t-7] + sig0(sched[t-15]) + sched[t-16];

	return;
}

//Compression of message schedule into 8 registers using compound functions.
void compression(uint32_t* sched){
	uint32_t a = hash[0];
	uint32_t b = hash[1];
	uint32_t c = hash[2];
	uint32_t d = hash[3];
	uint32_t e = hash[4];
	uint32_t f = hash[5];
	uint32_t g = hash[6];
	uint32_t h = hash[7];

	for(int i=0; i<64; i++){
		uint32_t T1 = sum1(e) + choice(e,f,g) + h + k[i] + sched[i];
		uint32_t T2 = sum0(a) + majority(a,b,c);

		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;
	}

	/*Add original hash values to compressed registers.*/
	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
	hash[4] += e;
	hash[5] += f;
	hash[6] += g;
	hash[7] += h;
}

void produce_digest(char* out) {
	/*unwrap 32bit words into array of bytes*/
	uint8_t output[32];
	int x;
	for(int i = 0; i < 8; i++){
		x = i*4;
		output[x]   = (uint8_t) (hash[i] >> 24);
		output[x+1] = (uint8_t) (hash[i] >> 16);
		output[x+2] = (uint8_t) (hash[i] >> 8);
		output[x+3] = (uint8_t) (hash[i]);
	}

	/*convert raw data to hexadecimal string*/
	for(int i =0; i < 32; i++){
		sprintf(out + i*2, "%02x", output[i]);
	}
	out[64] = '\0';
}

void sha256(uint8_t* buffer, size_t buflen, char* output){
	
	size_t l = buflen*8 + 1 + 64;						//message bits + 64 bit length encoding + 0b1 seperator
	size_t msgbits = l + (512 - (l % 512));			//nearest multiple of 512 to our message length
	size_t num_blocks = msgbits / 512;

	uint32_t* message = (uint32_t*) calloc(msgbits/sizeof(uint32_t), sizeof(uint32_t));
	process_input(buffer, buflen, message, msgbits/32);

	uint32_t message_schedule[64];
	for(size_t i = 0; i < num_blocks; i++){
		create_message_schedule(message_schedule, message+i*512/32);
		compression(message_schedule);
	}
	produce_digest(output);
	free(message);
}

