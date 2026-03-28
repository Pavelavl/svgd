#include "auth.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Global auth configuration
auth_config_t g_auth_config = {0};

/**
 * Base64 encode using OpenSSL BIO
 * Returns malloc'd string (caller must free), or NULL on error
 */
static char* base64_encode(const unsigned char *input, int length) {
    BIO *b64 = BIO_new(BIO_f_base64());
    if (!b64) return NULL;

    BIO *mem = BIO_new(BIO_s_mem());
    if (!mem) {
        BIO_free(b64);
        return NULL;
    }

    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    int written = BIO_write(b64, input, length);
    if (written <= 0) {
        BIO_free_all(b64);
        return NULL;
    }

    BIO_flush(b64);

    BUF_MEM *buffer_ptr;
    BIO_get_mem_ptr(b64, &buffer_ptr);

    char *result = malloc(buffer_ptr->length + 1);
    if (!result) {
        BIO_free_all(b64);
        return NULL;
    }

    memcpy(result, buffer_ptr->data, buffer_ptr->length);
    result[buffer_ptr->length] = '\0';

    BIO_free_all(b64);
    return result;
}

/**
 * Base64 decode using OpenSSL BIO
 * Returns malloc'd buffer (caller must free), sets *out_len
 * Returns NULL on error
 */
static unsigned char* base64_decode(const char *input, int *out_len) {
    // Add padding if input length is not a multiple of 4
    size_t input_len = strlen(input);
    size_t padded_len = (input_len + 3) & ~3;
    char *padded = NULL;

    if (padded_len != input_len) {
        padded = malloc(padded_len + 1);
        if (!padded) return NULL;
        memcpy(padded, input, input_len);
        memset(padded + input_len, '=', padded_len - input_len);
        padded[padded_len] = '\0';
    }

    const char *decode_input = padded ? padded : input;

    BIO *b64 = BIO_new(BIO_f_base64());
    if (!b64) { free(padded); return NULL; }

    BIO *mem = BIO_new_mem_buf(decode_input, -1);
    if (!mem) {
        BIO_free(b64);
        free(padded);
        return NULL;
    }

    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    int max_len = padded_len + 1;
    unsigned char *result = malloc(max_len);
    if (!result) {
        BIO_free_all(b64);
        free(padded);
        return NULL;
    }

    int decoded_len = BIO_read(b64, result, max_len);
    free(padded);

    if (decoded_len <= 0) {
        BIO_free_all(b64);
        free(result);
        *out_len = 0;
        return NULL;
    }

    result[decoded_len] = '\0';
    *out_len = decoded_len;

    BIO_free_all(b64);
    return result;
}

/**
 * Compute HMAC-SHA256 using OpenSSL EVP
 * Returns malloc'd buffer (caller must free), sets *out_len
 * Returns NULL on error
 */
static unsigned char* hmac_sha256(const unsigned char *data, size_t data_len,
                                  const unsigned char *key, size_t key_len,
                                  unsigned int *out_len) {
    unsigned char *result = malloc(EVP_MAX_MD_SIZE);
    if (!result) return NULL;

    const EVP_MD *evp_md = EVP_sha256();
    if (!evp_md) {
        free(result);
        return NULL;
    }

    unsigned char *hmac = HMAC(evp_md, key, key_len, data, data_len, result, out_len);
    if (!hmac) {
        free(result);
        return NULL;
    }

    return result;
}

/**
 * Extract string value from JSON
 * JSON format: {"key":"value"} or {"key":"value",...}
 * Returns malloc'd string (caller must free), or NULL on error
 */
static char* extract_json_string(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;

    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;

    const char *start = strchr(colon + 1, '"');
    if (!start) return NULL;
    start++; // Skip opening quote

    const char *end = strchr(start, '"');
    if (!end) return NULL;

    size_t len = end - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    return result;
}

/**
 * Extract integer value from JSON
 * Returns 0 and sets *ok to 0 if error, returns value and sets *ok to 1 if success
 */
static long long extract_json_int(const char *json, const char *key, int *ok) {
    *ok = 0;

    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *key_pos = strstr(json, search_key);
    if (!key_pos) return 0;

    const char *colon = strchr(key_pos, ':');
    if (!colon) return 0;

    char *endptr;
    long long value = strtoll(colon + 1, &endptr, 10);

    if (endptr == colon + 1) {
        return 0; // No conversion
    }

    *ok = 1;
    return value;
}

/**
 * Load auth.json configuration
 * Returns 0 on success, -1 on error
 */
int auth_load_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    // Read entire file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        return -1;
    }

    size_t read_size = fread(content, 1, file_size, fp);
    if (read_size != (size_t)file_size) {
        free(content);
        fclose(fp);
        return -1;
    }
    content[read_size] = '\0';
    fclose(fp);

    // Parse JSON
    char *password = extract_json_string(content, "password");
    char *secret = extract_json_string(content, "jwt_secret");

    int ok;
    long long expiry_days = extract_json_int(content, "token_expiry_days", &ok);
    if (!ok) expiry_days = 7; // Default
    if (expiry_days < 1 || expiry_days > 365) expiry_days = 7;

    if (!password || !secret) {
        free(content);
        if (password) free(password);
        if (secret) free(secret);
        return -1;
    }

    // Copy to config
    strncpy(g_auth_config.password, password, MAX_PASSWORD_LEN - 1);
    g_auth_config.password[MAX_PASSWORD_LEN - 1] = '\0';

    strncpy(g_auth_config.jwt_secret, secret, MAX_JWT_SECRET_LEN - 1);
    g_auth_config.jwt_secret[MAX_JWT_SECRET_LEN - 1] = '\0';

    g_auth_config.token_expiry_days = (int)expiry_days;
    g_auth_config.loaded = 1;

    free(content);
    free(password);
    free(secret);

    return 0;
}

/**
 * Verify password against stored password
 * Returns 1 if matches, 0 if doesn't match
 */
int auth_verify_password(const char *password) {
    if (!g_auth_config.loaded || !password) {
        return 0;
    }

    size_t len = strlen(g_auth_config.password);
    if (strlen(password) != len) {
        return 0;
    }

    volatile unsigned char result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= password[i] ^ g_auth_config.password[i];
    }
    return result == 0;
}

/**
 * Create new JWT-like token
 * Format: base64(json_payload).base64(hmac_signature)
 * Returns malloc'd string (caller must free), or NULL on error
 */
char* auth_create_token(void) {
    if (!g_auth_config.loaded) {
        return NULL;
    }

    // Create payload
    time_t now = time(NULL);
    time_t exp = now + (g_auth_config.token_expiry_days * 24 * 60 * 60);

    char payload[256];
    snprintf(payload, sizeof(payload), "{\"exp\":%ld,\"iat\":%ld}", (long)exp, (long)now);

    // Encode payload to base64 ( trailing padding stripped
    char *payload_b64 = base64_encode((unsigned char *)payload, strlen(payload));
    if (!payload_b64) {
        return NULL;
    }
    // Strip trailing '=' from payload_b64
    int plen = strlen(payload_b64);
    while (plen > 0 && payload_b64[plen - 1] == '=') plen--;
    payload_b64[plen] = '\0';
    while (plen > 0 && payload_b64[plen - 1] == '=') payload_b64[--plen] = '\0';
    // Compute HMAC signature
    unsigned int sig_len;
    unsigned char *sig = hmac_sha256(
        (unsigned char *)payload_b64, strlen(payload_b64),
        (unsigned char *)g_auth_config.jwt_secret, strlen(g_auth_config.jwt_secret),
        &sig_len
    );

    if (!sig) {
        free(payload_b64);
        return NULL;
    }

    // Encode signature to base64
    char *sig_b64 = base64_encode(sig, sig_len);
    free(sig);

    if (!sig_b64) {
        free(payload_b64);
        return NULL;
    }

    // Combine: payload.signature
    size_t token_len = strlen(payload_b64) + strlen(sig_b64) + 2;
    if (token_len > MAX_TOKEN_SIZE) {
        free(payload_b64);
        free(sig_b64);
        return NULL;
    }
    char *token = malloc(token_len);
    if (!token) {
        free(payload_b64);
        free(sig_b64);
        return NULL;
    }

    strcpy(token, payload_b64);
    strcat(token, ".");
    strcat(token, sig_b64);

    free(payload_b64);
    free(sig_b64);

    return token;
}

/**
 * Validate JWT-like token
 * Returns 0 if valid, -1 if invalid
 */
int auth_validate_token(const char *token) {
    if (!g_auth_config.loaded || !token) {
        fprintf(stderr, "[AUTH] validate: not loaded or null token\n");
        return -1;
    }

    fprintf(stderr, "[AUTH] validate: token='%s'\n", token);

    // Split token into payload and signature
    const char *dot = strchr(token, '.');
    if (!dot) {
        fprintf(stderr, "[AUTH] validate: no dot in token\n");
        return -1;
    }

    // Extract payload
    size_t payload_len = dot - token;
    char *payload_b64 = malloc(payload_len + 1);
    if (!payload_b64) {
        return -1;
    }
    memcpy(payload_b64, token, payload_len);
    payload_b64[payload_len] = '\0';

    // Extract signature
    char *sig_b64 = strdup(dot + 1);
    if (!sig_b64) {
        free(payload_b64);
        return -1;
    }

    fprintf(stderr, "[AUTH] payload_b64='%s' sig_b64='%s'\n", payload_b64, sig_b64);

    // Decode signature from base64
    int sig_decoded_len;
    unsigned char *sig_decoded = base64_decode(sig_b64, &sig_decoded_len);
    if (!sig_decoded) {
        fprintf(stderr, "[AUTH] validate: sig base64_decode FAILED\n");
        free(payload_b64);
        free(sig_b64);
        return -1;
    }
    int sig_len = sig_decoded_len;
    fprintf(stderr, "[AUTH] sig decoded ok, len=%d\n", sig_len);

    // Compute expected HMAC
    unsigned int expected_sig_len;
    unsigned char *expected_sig = hmac_sha256(
        (unsigned char *)payload_b64, strlen(payload_b64),
        (unsigned char *)g_auth_config.jwt_secret, strlen(g_auth_config.jwt_secret),
        &expected_sig_len
    );

    if (!expected_sig) {
        fprintf(stderr, "[AUTH] validate: hmac_sha256 FAILED\n");
        free(payload_b64);
        free(sig_b64);
        free(sig_decoded);
        return -1;
    }

    fprintf(stderr, "[AUTH] expected_sig_len=%u sig_len=%d\n", expected_sig_len, sig_len);

    // Compare signatures (constant-time to prevent timing attacks)
    if (sig_len != (int)expected_sig_len) {
        fprintf(stderr, "[AUTH] validate: sig length mismatch\n");
        free(payload_b64);
        free(sig_b64);
        free(sig_decoded);
        free(expected_sig);
        return -1;
    }
    volatile unsigned char result = 0;
    for (int i = 0; i < sig_len; i++) {
        result |= sig_decoded[i] ^ expected_sig[i];
    }
    free(sig_decoded);
    free(expected_sig);

    if (result != 0) {
        free(payload_b64);
        free(sig_b64);
        return -1;
    }

    // Decode payload and check expiration
    int payload_decoded_len;
    unsigned char *payload_decoded = base64_decode(payload_b64, &payload_decoded_len);
    free(payload_b64);
    free(sig_b64);

    if (!payload_decoded) {
        return -1;
    }

    // Check expiration
    int ok;
    long long exp = extract_json_int((char *)payload_decoded, "exp", &ok);
    free(payload_decoded);

    if (!ok) {
        return -1;
    }

    time_t now = time(NULL);
    if (exp < now) {
        return -1; // Token expired
    }

    return 0; // Valid token
}

/**
 * Check if auth is configured
 * Returns 1 if configured, 0 if not
 */
int auth_is_configured(void) {
    return g_auth_config.loaded;
}

/**
 * Free allocated resources
 */
void auth_cleanup(void) {
    memset(&g_auth_config, 0, sizeof(auth_config_t));
}
