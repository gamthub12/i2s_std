#pragma once
#include <stdbool.h>
#include <stddef.h>

void firebase_client_start(void);
bool firebase_client_pairing(char *device_id, size_t id_size, char *code, size_t code_size);
/* HTTP status; validates the phone's Firebase token before assigning ownership. */
int firebase_client_pair_request(const char *json);
/* Local diagnostics: counters since boot; does not contact Firebase. */
void firebase_client_metrics(char *output, size_t capacity);
