#ifndef AUTH_H
#define AUTH_H

#include <time.h>

// Token expiration time (7 days in seconds)
#define TOKEN_EXPIRY_SECONDS (7 * 24 * 60 * 60)

// Maximum sizes
#define MAX_PASSWORD_LEN 256
#define MAX_JWT_SECRET_LEN 256
#define MAX_TOKEN_SIZE 1024

// Auth configuration
typedef struct {
    char password[MAX_PASSWORD_LEN];
    char jwt_secret[MAX_JWT_SECRET_LEN];
    int token_expiry_days;
    int loaded;
} auth_config_t;

// Global auth configuration
extern auth_config_t g_auth_config;

// Load auth.json configuration
// Returns 0 on success, -1 on error
int auth_load_config(const char *path);

// Validate JWT-like token
// Returns 0 if valid, -1 if invalid
int auth_validate_token(const char *token);

// Create new JWT-like token
// Returns malloc'd string (caller must free), or NULL on error
char* auth_create_token(void);

// Verify password against stored password
// Returns 1 if matches, 0 if doesn't match
int auth_verify_password(const char *password);

// Check if auth is configured
// Returns 1 if configured, 0 if not
int auth_is_configured(void);

// Free allocated resources
void auth_cleanup(void);

#endif // AUTH_H
