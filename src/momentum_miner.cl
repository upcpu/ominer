//#define BITMAP_SIZE 536870912
//#define BITMAP_INDEX_TYPE uint64_t

#define BITMAP_SIZE_IN_BITS (BITMAP_SIZE*8lu)
#define BITMAP_INDEX_MASK(i) ((i)&(BITMAP_SIZE_IN_BITS-1lu)) 

#define UNKNOWN                 0
#define CPU                     1
#define GPU                     2
#define ACCELERATOR             4
#define AMD                     64
#define NVIDIA                  128
#define INTEL                   256
#define AMD_GCN                 1024
#define AMD_VLIW4               2048
#define AMD_VLIW5               4096
#define NO_BYTE_ADDRESSABLE     8192

#define cpu(n)                  ((n & CPU) == (CPU))
#define gpu(n)                  ((n & GPU) == (GPU))
#define gpu_amd(n)              ((n & AMD) && gpu(n))
#define gpu_amd_64(n)           (0)
#define gpu_nvidia(n)           ((n & NVIDIA) && gpu(n))
#define gpu_intel(n)            ((n & INTEL) && gpu(n))
#define cpu_amd(n)              ((n & AMD) && cpu(n))
#define amd_gcn(n)              ((n & AMD_GCN) && gpu_amd(n))
#define amd_vliw4(n)            ((n & AMD_VLIW4) && gpu_amd(n))
#define amd_vliw5(n)            ((n & AMD_VLIW5) && gpu_amd(n))
#define no_byte_addressable(n)  (n & NO_BYTE_ADDRESSABLE)

//Type names definition.
#define uint8_t  unsigned char
#define uint16_t unsigned short
#define uint32_t unsigned int
#define uint64_t unsigned long  //Tip: unsigned long long int failed on compile (AMD).

//Functions.
#define MAX(x,y)                ((x) > (y) ? (x) : (y))
#define MIN(x,y)                ((x) < (y) ? (x) : (y))


//Constants.
#define ROUNDS_DEFAULT          5000
#define ROUNDS_MIN              1000
#define ROUNDS_MAX              999999999

#define SALT_LENGTH             16
#define PLAINTEXT_LENGTH        16
#define SALT_ARRAY              (SALT_LENGTH / 8)
#define PLAINTEXT_ARRAY         (PLAINTEXT_LENGTH / 8)
#define BINARY_SIZE             (3+16+86)       //TODO: Magic number?
#define SALT_SIZE               (3+7+9+16)      //TODO: Magic number?
#define STEP                    512

#define KEYS_PER_CORE_CPU       128
#define KEYS_PER_CORE_GPU       512
#define MIN_KEYS_PER_CRYPT      128
#define MAX_KEYS_PER_CRYPT      2048*1024

//Macros.
#define SWAP(n) \
            (((n) << 56)                      \
          | (((n) & 0xff00) << 40)            \
          | (((n) & 0xff0000) << 24)          \
          | (((n) & 0xff000000) << 8)         \
          | (((n) >> 8) & 0xff000000)         \
          | (((n) >> 24) & 0xff0000)          \
          | (((n) >> 40) & 0xff00)            \
          | ((n) >> 56))

#define SWAP64_V(n)     SWAP(n)

//radeon 5870
#define DEVICE_INFO (GPU|AMD)

#if gpu_amd_64(DEVICE_INFO)
        #pragma OPENCL EXTENSION cl_amd_media_ops : enable
        #define ror(x, n)       amd_bitalign(x, x, (uint64_t) n)
        #define Ch(x, y, z)     amd_bytealign(x, y, z)
        #define Maj(x, y, z)    amd_bytealign(z ^ x, y, x )
        #define SWAP64(n)       (as_ulong(as_uchar8(n).s76543210))
#elif gpu_amd(DEVICE_INFO)
        #define Ch(x,y,z)       bitselect(z, y, x)
        #define Maj(x,y,z)      bitselect(x, y, z ^ x)
        #define ror(x, n)       rotate(x, (uint64_t) 64-n)
        #define SWAP64(n)       (as_ulong(as_uchar8(n).s76543210))
#else
        #if gpu_nvidia(DEVICE_INFO)
            #pragma OPENCL EXTENSION cl_nv_pragma_unroll : enable
        #endif
        #define Ch(x,y,z)       ((x & y) ^ ( (~x) & z))
        #define Maj(x,y,z)      ((x & y) ^ (x & z) ^ (y & z))
        #define ror(x, n)       ((x >> n) | (x << (64-n)))
        #define SWAP64(n)       SWAP(n)
#endif
#define Sigma0(x)               ((ror(x,28)) ^ (ror(x,34)) ^ (ror(x,39)))
#define Sigma1(x)               ((ror(x,14)) ^ (ror(x,18)) ^ (ror(x,41)))
#define sigma0(x)               ((ror(x,1))  ^ (ror(x,8))  ^ (x>>7))
#define sigma1(x)               ((ror(x,19)) ^ (ror(x,61)) ^ (x>>6))

#define MAX_MOMENTUM_NONCE  (1<<26)
#define SEARCH_SPACE_BITS 50
#define BIRTHDAYS_PER_HASH 8

//definitions end

__constant uint64_t k[] = {
        0x428a2f98d728ae22UL, 0x7137449123ef65cdUL, 0xb5c0fbcfec4d3b2fUL,
            0xe9b5dba58189dbbcUL,
        0x3956c25bf348b538UL, 0x59f111f1b605d019UL, 0x923f82a4af194f9bUL,
            0xab1c5ed5da6d8118UL,
        0xd807aa98a3030242UL, 0x12835b0145706fbeUL, 0x243185be4ee4b28cUL,
            0x550c7dc3d5ffb4e2UL,
        0x72be5d74f27b896fUL, 0x80deb1fe3b1696b1UL, 0x9bdc06a725c71235UL,
            0xc19bf174cf692694UL,
        0xe49b69c19ef14ad2UL, 0xefbe4786384f25e3UL, 0x0fc19dc68b8cd5b5UL,
            0x240ca1cc77ac9c65UL,
        0x2de92c6f592b0275UL, 0x4a7484aa6ea6e483UL, 0x5cb0a9dcbd41fbd4UL,
            0x76f988da831153b5UL,
        0x983e5152ee66dfabUL, 0xa831c66d2db43210UL, 0xb00327c898fb213fUL,
            0xbf597fc7beef0ee4UL,
        0xc6e00bf33da88fc2UL, 0xd5a79147930aa725UL, 0x06ca6351e003826fUL,
            0x142929670a0e6e70UL,
        0x27b70a8546d22ffcUL, 0x2e1b21385c26c926UL, 0x4d2c6dfc5ac42aedUL,
            0x53380d139d95b3dfUL,
        0x650a73548baf63deUL, 0x766a0abb3c77b2a8UL, 0x81c2c92e47edaee6UL,
            0x92722c851482353bUL,
        0xa2bfe8a14cf10364UL, 0xa81a664bbc423001UL, 0xc24b8b70d0f89791UL,
            0xc76c51a30654be30UL,
        0xd192e819d6ef5218UL, 0xd69906245565a910UL, 0xf40e35855771202aUL,
            0x106aa07032bbd1b8UL,
        0x19a4c116b8d2d0c8UL, 0x1e376c085141ab53UL, 0x2748774cdf8eeb99UL,
            0x34b0bcb5e19b48a8UL,
        0x391c0cb3c5c95a63UL, 0x4ed8aa4ae3418acbUL, 0x5b9cca4f7763e373UL,
            0x682e6ff3d6b2b8a3UL,
        0x748f82ee5defb2fcUL, 0x78a5636f43172f60UL, 0x84c87814a1f0ab72UL,
            0x8cc702081a6439ecUL,
        0x90befffa23631e28UL, 0xa4506cebde82bde9UL, 0xbef9a3f7b2c67915UL,
            0xc67178f2e372532bUL,
        0xca273eceea26619cUL, 0xd186b8c721c0c207UL, 0xeada7dd6cde0eb1eUL,
            0xf57d4f7fee6ed178UL,
        0x06f067aa72176fbaUL, 0x0a637dc5a2c898a6UL, 0x113f9804bef90daeUL,
            0x1b710b35131c471bUL,
        0x28db77f523047d84UL, 0x32caab7b40c72493UL, 0x3c9ebe0a15c9bebcUL,
            0x431d67c49c100d4cUL,
        0x4cc5d4becb3e42b6UL, 0x597f299cfc657e2aUL, 0x5fcb6fab3ad6faecUL,
            0x6c44198c4a475817UL,
};

__constant uint64_t H[] = {
    0x6a09e667f3bcc908UL,
    0xbb67ae8584caa73bUL,
    0x3c6ef372fe94f82bUL,
    0xa54ff53a5f1d36f1UL,
    0x510e527fade682d1UL,
    0x9b05688c2b3e6c1fUL,
    0x1f83d9abfb41bd6bUL,
    0x5be0cd19137e2179UL,
};

#ifdef AMD_OPTIM
inline 
#endif
void sha512_block(ulong *w) {
    ulong a = H[0];
    ulong b = H[1];
    ulong c = H[2];
    ulong d = H[3];
    ulong e = H[4];
    ulong f = H[5];
    ulong g = H[6];
    ulong h = H[7];
    ulong t1, t2;

    #pragma unroll
    for (int i = 0; i < 80; i++) {

        if (i > 15) {
            w[i & 15] = sigma1(w[(i - 2) & 15]) + sigma0(w[(i - 15) & 15]) + w[(i - 16) & 15] + w[(i - 7) & 15];
        }
        t1 = k[i] + w[i & 15] + h + Sigma1(e) + Ch(e, f, g);
        t2 = Maj(a, b, c) + Sigma0(a);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

	w[0] = SWAP(H[0]+a); //SWAP only for final result, not for block!
    w[1] = SWAP(H[1]+b);
    w[2] = SWAP(H[2]+c);
    w[3] = SWAP(H[3]+d);
    w[4] = SWAP(H[4]+e);
    w[5] = SWAP(H[5]+f);
    w[6] = SWAP(H[6]+g);
    w[7] = SWAP(H[7]+h);
}
/* structure to hash:
struct {
uint32_t nonce; //1 to MAX_MOMENTUM_NONCE in increments of BIRTHDAYS_PER_HASH
uint64_t midHash[4]; //uint256_t
//size: 9 dword, 36 bytes
};
*/

#ifdef AMD_OPTIM
inline 
#endif
void sha512_block2(ulong2 *w) {
    ulong2 a = H[0];
    ulong2 b = H[1];
    ulong2 c = H[2];
    ulong2 d = H[3];
    ulong2 e = H[4];
    ulong2 f = H[5];
    ulong2 g = H[6];
    ulong2 h = H[7];
    ulong2 t1, t2;

    #pragma unroll
    for (int i = 0; i < 80; i++) {

        if (i > 15) {
            w[i & 15] = sigma1(w[(i - 2) & 15]) + sigma0(w[(i - 15) & 15]) + w[(i - 16) & 15] + w[(i - 7) & 15];
        }
        t1 = k[i] + w[i & 15] + h + Sigma1(e) + Ch(e, f, g);
        t2 = Maj(a, b, c) + Sigma0(a);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

	w[0] = SWAP(H[0]+a); //SWAP only for final result, not for block!
    w[1] = SWAP(H[1]+b);
    w[2] = SWAP(H[2]+c);
    w[3] = SWAP(H[3]+d);
    w[4] = SWAP(H[4]+e);
    w[5] = SWAP(H[5]+f);
    w[6] = SWAP(H[6]+g);
    w[7] = SWAP(H[7]+h);
}
/* structure to hash:
struct {
uint32_t nonce; //1 to MAX_MOMENTUM_NONCE in increments of BIRTHDAYS_PER_HASH
uint64_t midHash[4]; //uint256_t
//size: 9 dword, 36 bytes
};
*/

#define bswap32(x) ( ((x) << 24) | (((x) << 8) & 0x00ff0000) | (((x) >> 8) & 0x0000ff00) | ((x) >> 24) )

//value is a 50 bit number
inline uint32_t _50BitRor(uint64_t value, uint32_t n) {
	return (uint32_t) ((value>>n) | (value << (50-n)));
}


// -------------------------------------
// ONE BUFFER VERSION
// -------------------------------------

/*
Phase 1 computes all hashes and sets them in bit hash table.
Expects the whole bitmap and the first dword of collisionList to be zero
*/
#define S2(n) (ulong2)(as_ulong(as_uchar8(n.x).s76543210), as_ulong(as_uchar8(n.y).s76543210))
kernel void birthdayPhase1(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *collisionList)
{
	ulong2 w[16];
	uint32_t _x = get_global_id(0);
	uint32_t t = 0; //*id_offset ;
	uint32_t ot = _x * 16; //hashes per call;
	
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

    ulong2 tem;
	tem = (2*_x + (ulong2){0, 1}) * BIRTHDAYS_PER_HASH;

	w[0] = _w[0]| S2(tem);
	sha512_block2(w);

	#pragma unroll
	for(int i = 0; i<BIRTHDAYS_PER_HASH; i++){
		uint32_t hy = ((i + t + ot) << 6) | ((w[i].x>>32) & 0x3f);
		uint32_t oy = bitmap[w[i].x  & LOOK_UP_MASK];
		if(oy){
			bool cond = ((oy & 0x3f) == (hy & 0x3f )) ;
		   	if(cond){
			   	int rx = collisionList[0]++;
			   	collisionList[2*rx] = oy >> 6;
			   	collisionList[2*rx + 1] = hy >> 6;
		   	}
		}
		else{
		   bitmap[w[i].x  & LOOK_UP_MASK] = hy;
		}

	}

	#pragma unroll
	for(int i = 0; i<BIRTHDAYS_PER_HASH; i++){
		uint32_t hy = ((i + t + ot + BIRTHDAYS_PER_HASH) << 6) | ((w[i].y>>32) & 0x3f);
		uint32_t oy = bitmap[w[i].y  & LOOK_UP_MASK];
		if(oy){
			bool cond = ((oy & 0x3f) == (hy & 0x3f )) ;
		   	if(cond){
			   	int rx = collisionList[0]++;
			   	collisionList[2*rx] = oy >> 6;
			   	collisionList[2*rx + 1] = hy >> 6;
		   	}
		}
		else{
		   bitmap[w[i].y  & LOOK_UP_MASK] = hy;
		}
	}
}

kernel void zeroBitmap(global float8 *bitmap) {
	bitmap[get_global_id(0)] = 0;
}

/*
Phase 2 fills the bitmap with collisions from phase1.
Expects the whole bitmap to be zero
*/
kernel void birthdayPhase2(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *collisionList)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = collisionList[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);

	for(int i = 0; i < 8; i++) {
		w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		const BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(w[i]);
		const uint32_t dwordIndex = bitIndex/32;
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		atomic_or(&bitmap[dwordIndex], mask); //set bit for our hash
	}
}

/*
Phase 3 computes all hashes, but outputs list only with hashes previously in bitmap.
Expects the first dword of collisionList to be zero
*/
kernel void birthdayPhase3(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *collisionList)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(num*BIRTHDAYS_PER_HASH);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		const BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(w[i]);
		const uint32_t dwordIndex = bitIndex/32;
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = bitmap[dwordIndex]&mask; //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = num*BIRTHDAYS_PER_HASH; //collisionList is going to have all nonces with collision
		}
	}
}

/*
Phase 4 fills the bitmap with domain from phase3 using different hash (just like phase1), lists collisions in collisionList
Expects the whole bitmap and first dword of collisionList to be zero
*/
kernel void birthdayPhase4(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *domain, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = domain[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		//w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		//(uint32_t)(w[i]>>(28-(64-SEARCH_SPACE_BITS)));
		const BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN)); 
		const uint32_t dwordIndex = bitIndex/32;
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = atomic_or(&bitmap[dwordIndex], mask); //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = nonce; //collisionList is going to have all nonces with collision
		}
	}
}

/*
Phase 5 fills the bitmap with collisions found in phase 4 (like phase2)
Expects the bitmap to be zero 
*/
kernel void birthdayPhase5(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = collisionList[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);
#ifdef AMD_OPTIM2
	#pragma unroll
	for(int i = 0; i < 8; i++) {
#else
	for(int i = 0; i < 8; i++) {
		const BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN)); 
		const uint32_t dwordIndex = bitIndex/32;
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		atomic_or(&bitmap[dwordIndex], mask); //set bit for our hash
	}
#endif
}

/*
Phase 6 computes all hashes from domain, but outputs list only with hashes previously in bitmap.
Expects the first dword of collisionList to be zero
*/
kernel void birthdayPhase6(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *domain, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = domain[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		const BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN)); 
		const uint32_t dwordIndex = bitIndex/32;
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = bitmap[dwordIndex]&mask; //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = nonce; //collisionList is going to have all nonces with collision
		}
	}
}

// -------------------------------------
// TWO BUFFER VERSION
// -------------------------------------

/*
Phase 1 computes all hashes and sets them in bit hash table.
Expects the whole bitmap and the first dword of collisionList to be zero
*/
kernel void _2buf_birthdayPhase1(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *collisionList)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(num*BIRTHDAYS_PER_HASH);

	sha512_block(w);

	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(w[i]);
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = (bitIndex%32);
		const uint32_t mask = 1<<bitInDword;
		uint32_t last = atomic_or(b, mask); //set bit for our hash

		//don't move this out of the loop as this will cluster atomic_inc
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = num*BIRTHDAYS_PER_HASH; //collisionList is going to have all nonces with collision
		}
	}
}

kernel void _2buf_zeroBitmap(global float8 *bitmap, global float8 *bitmap2) {
	const uint32_t i = get_global_id(0);
	bitmap[i] = 0;
	bitmap2[i] = 0;
}

/*
Phase 2 fills the bitmap with collisions from phase1.
Expects the whole bitmap to be zero
*/
kernel void _2buf_birthdayPhase2(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *collisionList)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = collisionList[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);

	for(int i = 0; i < 8; i++) {
		w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(w[i]);
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		atomic_or(b, mask); //set bit for our hash
	}
}

/*
Phase 3 computes all hashes, but outputs list only with hashes previously in bitmap.
Expects the first dword of collisionList to be zero
*/
kernel void _2buf_birthdayPhase3(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *collisionList)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(num*BIRTHDAYS_PER_HASH);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(w[i]);
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = *b&mask; //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = num*BIRTHDAYS_PER_HASH; //collisionList is going to have all nonces with collision
		}
	}
}

/*
Phase 4 fills the bitmap with domain from phase3 using different hash (just like phase1), lists collisions in collisionList
Expects the whole bitmap and first dword of collisionList to be zero
*/
kernel void _2buf_birthdayPhase4(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *domain, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = domain[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		//w[i] = w[i] >> (64 - SEARCH_SPACE_BITS);
		//(uint32_t)(w[i]>>(28-(64-SEARCH_SPACE_BITS)));
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN));
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = atomic_or(b, mask); //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = nonce; //collisionList is going to have all nonces with collision
		}
	}
}

/*
Phase 5 fills the bitmap with collisions found in phase 4 (like phase2)
Expects the bitmap to be zero 
*/
kernel void _2buf_birthdayPhase5(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = collisionList[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);

	for(int i = 0; i < 8; i++) {
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN));
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		atomic_or(b, mask); //set bit for our hash
	}
}

/*
Phase 6 computes all hashes from domain, but outputs list only with hashes previously in bitmap.
Expects the first dword of collisionList to be zero
*/
kernel void _2buf_birthdayPhase6(constant uint64_t *_w, global uint32_t *bitmap, global uint32_t *bitmap2, global uint32_t *domain, global uint32_t *collisionList, uint32_t rotateN)
{
	uint64_t w[16];
	const uint32_t num = get_global_id(0);
	const uint32_t nonce = domain[num+1]; //first dword is a counter
    #pragma unroll
	for(int i = 0; i < 16; i++)
		w[i] = _w[i];

	((uint32_t*)w)[1] = bswap32(nonce);

	sha512_block(w);
	uint32_t alreadyWritten = 0;
	for(int i = 0; i < 8; i++) {
		BITMAP_INDEX_TYPE bitIndex = BITMAP_INDEX_MASK(_50BitRor(w[i] >> (64 - SEARCH_SPACE_BITS), rotateN));
		const uint32_t dwordIndex = bitIndex/32;
		global uint32_t *b = (dwordIndex >= BITMAP_SIZE/8) ? &bitmap2[dwordIndex-BITMAP_SIZE/8] : &bitmap[dwordIndex]; //I hope it's just a cmov
		const uint32_t bitInDword = bitIndex%32;
		const uint32_t mask = 1<<bitInDword;
		const uint32_t last = *b&mask; //set bit for our hash
		if(last&mask && alreadyWritten == 0) { //we have a collision
			alreadyWritten = 1; //don't write the same nonce several times
			const uint32_t listIndex = atomic_inc(collisionList)+1;
			collisionList[listIndex] = nonce; //collisionList is going to have all nonces with collision
		}
	}
}
