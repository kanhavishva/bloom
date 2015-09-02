/*******************************************************************************
***
***	 Author: Tyler Barrus
***	 email:  barrust@gmail.com
***
***	 Version: 1.0.0
***
***	 License: MIT 2015
***
*******************************************************************************/
#include "bloom.h"

#define set_bit(A,k)	 (A[((k) / 8)] |=  (1 << ((k) % 8)))
#define clear_bit(A,k)   (A[((k) / 8)] &= ~(1 << ((k) % 8))) /* not currently used */
#define check_bit(A,k)   (A[((k) / 8)] &   (1 << ((k) % 8)))


#define CHAR_LEN 8

static const double LOG_TWO_SQUARED = 0.4804530139182;

/*******************************************************************************
***		PRIVATE FUNCTIONS
*******************************************************************************/
static uint64_t* md5_hash_default(int num_hashes, uint64_t num_bits, char *str);
static void calculate_optimal_hashes(BloomFilter *bf);

/*******************************************************************************
***		testing functions
*******************************************************************************/
void print_bits(char ch) {
	int i;
	for (i = 0; i < CHAR_LEN; i++) {
		printf("%c", (ch & (1 << i)) ? '1' : '0');
	}
	printf("\n");
}
/* END TESTING */

int bloom_filter_init(BloomFilter *bf, uint64_t estimated_elements, float false_positive_rate, HashFunction hash_function) {
	if(estimated_elements <= 0 || estimated_elements > UINT64_MAX) {
		return BLOOM_FAILURE;
	}
	if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0 ) {
		return BLOOM_FAILURE;
	}
	bf->estimated_elements = estimated_elements;
	bf->false_positive_probability = false_positive_rate;
	calculate_optimal_hashes(bf);
	bf->bloom = calloc(bf->bloom_length, sizeof(char));
	bf->elements_added = 0;
	bloom_filter_set_hash_function(bf, hash_function);
	return BLOOM_SUCCESS;
}

void bloom_filter_set_hash_function(BloomFilter *bf, HashFunction hf) {
	if (hf == NULL) {
		bf->hash_function = &md5_hash_default;
	} else {
		bf->hash_function = hf;
	}
}

int bloom_filter_destroy(BloomFilter *bf) {
	free(bf->bloom);
	bf->bloom = NULL;
	bf->elements_added = 0;
	bf->estimated_elements = 0;
	bf->false_positive_probability = 0;
	bf->number_hashes = 0;
	bf->number_bits = 0;
	bf->hash_function = NULL;
	return BLOOM_SUCCESS;
}

void bloom_filter_stats(BloomFilter *bf) {
	printf("BloomFilter\n\
	bits: %" PRIu64 "\n\
	estimated elements: %" PRIu64 "\n\
	number hashes: %d\n\
	max false positive rate: %f\n\
	bloom length (8 bits): %ld\n\
	elements added: %" PRIu64 "\n\
	current false positive rate: %f\n",
	bf->number_bits, bf->estimated_elements, bf->number_hashes,
	bf->false_positive_probability, bf->bloom_length, bf->elements_added,
	bloom_filter_current_false_positive_rate(bf));
}

int bloom_filter_add_string(BloomFilter *bf, char *str) {
	uint64_t *hashes = bf->hash_function(bf->number_hashes, bf->number_bits, str);
	int i;
	for (i = 0; i < bf->number_hashes; i++) {
		set_bit(bf->bloom, hashes[i]);
	}
	free(hashes);
	bf->elements_added++;
	return BLOOM_SUCCESS;
}


int bloom_filter_check_string(BloomFilter *bf, char *str) {
	uint64_t *hashes = bf->hash_function(bf->number_hashes, bf->number_bits, str);
	int r = BLOOM_SUCCESS;
	int i;
	for (i = 0; i < bf->number_hashes; i++) {
		int t = check_bit(bf->bloom, hashes[i]);
		if (check_bit(bf->bloom, hashes[i]) == 0) {
			r = BLOOM_FAILURE;
			break; // no need to continue checking
		}
	}
	free(hashes);
	return r;
}

float bloom_filter_current_false_positive_rate(BloomFilter *bf) {
	int num = (bf->number_hashes * -1 * bf->elements_added);
	double d = (num * 1.0) / (bf->number_bits * 1.0);
	double e = exp(d);
	return pow((1 - e), bf->number_hashes);
}

int bloom_filter_export(BloomFilter *bf, char *filepath) {
	FILE *fp;
	fp = fopen(filepath, "wb");
	if (fp == NULL) {
		fprintf(stderr, "Can't open file %s!\n", filepath);
		return BLOOM_FAILURE;
	}
	fwrite(&bf->estimated_elements, sizeof(uint64_t), 1, fp);
	fwrite(&bf->elements_added, sizeof(uint64_t), 1, fp);
	fwrite(&bf->false_positive_probability, sizeof(float), 1, fp);
	fwrite(bf->bloom, bf->bloom_length, 1, fp);
	fclose(fp);
	return BLOOM_SUCCESS;
}

int bloom_filter_import(BloomFilter *bf, char *filepath, HashFunction hash_function) {
	FILE *fp;
	fp = fopen(filepath, "r+b");
	if (fp == NULL) {
		fprintf(stderr, "Can't open file %s!\n", filepath);
		return BLOOM_FAILURE;
	}
	fread(&bf->estimated_elements, sizeof(uint64_t), 1, fp);
	fread(&bf->elements_added, sizeof(uint64_t), 1, fp);
	fread(&bf->false_positive_probability, sizeof(float), 1, fp);
	calculate_optimal_hashes(bf);
	bf->bloom = calloc(bf->bloom_length, sizeof(char));
	fread(bf->bloom, sizeof(char), bf->bloom_length, fp);
	bloom_filter_set_hash_function(bf, hash_function);
	return BLOOM_SUCCESS;
}

/*******************************************************************************
*	PRIVATE FUNCTIONS
*******************************************************************************/
static void calculate_optimal_hashes(BloomFilter *bf) {
	// calc optimized values
	long n = bf->estimated_elements;
	float p = bf->false_positive_probability;
	uint64_t m = ceil((-n * log(p)) / LOG_TWO_SQUARED);  // AKA pow(log(2), 2);
	unsigned int k = round(log(2.0) * m / n);
	// set paramenters
	bf->number_hashes = k; // should check to make sure it is at least 1...
	bf->number_bits = m;
	long num_pos = ceil(m / (CHAR_LEN * 1.0));
	bf->bloom_length = num_pos;
}

/* NOTE: The caller will free the results */
static uint64_t* md5_hash_default(int num_hashes, uint64_t num_bits, char *str) {
	uint64_t *results = calloc(num_hashes, sizeof(uint64_t));
	unsigned char digest[MD5_DIGEST_LENGTH];
	int i;
	for (i = 0; i < num_hashes; i++) {
		MD5_CTX md5_ctx;
		MD5_Init(&(md5_ctx));
		if (i == 0) {
			MD5_Update(&(md5_ctx), str, strlen(str));
		} else {
			MD5_Update(&(md5_ctx), digest, MD5_DIGEST_LENGTH);
		}
		MD5_Final(digest, &(md5_ctx));
		results[i] = (uint64_t) *(uint64_t *)digest % num_bits;
	}
	return results;
}
