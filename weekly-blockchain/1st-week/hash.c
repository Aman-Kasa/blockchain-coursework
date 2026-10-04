#include <stdio.h>
#include <stdlib.h>
#include <openssl/evp.h>

#define BUFFER_SIZE 4096

void hash_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return;
    }

    // 1. Initialize Cryptographic Context
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_sha256();
    EVP_DigestInit_ex(mdctx, md, NULL);

    // 2. Buffer & Stream Reading
    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        // 3. Incremental Hash Processing
        EVP_DigestUpdate(mdctx, buffer, bytes_read);
    }

    // 4. Finalize Hash Digest
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);

    // Clean up resources
    EVP_MD_CTX_free(mdctx);
    fclose(file);

    // 5. Hexadecimal Formatting Output
    printf("SHA-256 Hash: ");
    for (unsigned int i = 0; i < hash_len; i++) {
        printf("%02x", hash[i]);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file_path>\n", argv[0]);
        return 1;
    }

    hash_file(argv[1]);
    return 0;
}
