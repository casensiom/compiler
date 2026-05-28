#ifndef _TEST_BASE_H
#define _TEST_BASE_H

#include "../tokenizer.h"
#include <stdio.h>

#include "test_struct.h"
#include "test_numbers.h"
#include "test_strings.h"

typedef Token *(*TokenizerFn)(const char *, TokenLoc, TokenizerError *);

void
test_method(UnitTestCase *cases, size_t count, TokenizerFn fn, const char *name) {
    size_t num_cases     = count / sizeof(UnitTestCase);
    size_t total_success = 0;

    for(size_t i = 0; i < num_cases; ++i) {
        TokenizerError error = {0};
        Token         *t     = fn(cases[i].value, (TokenLoc){0}, &error);

        int success = (cases[i].is_valid && t != NULL) || (!cases[i].is_valid && t == NULL);
        total_success += success;

        const char *token_present = t ? "Y" : "N";
        token_delete(t);

        if(success)
            continue;    // hide success test results

        printf("%03lu | %-30s | expected=%d | token=%s | success=%d", i, cases[i].value, cases[i].is_valid, token_present, success);
        if(error.message) {
            printf(" | error=%s", error.message);
        } else if(t != NULL) {
            printf(" | token=%.*s", (int)t->len, t->pos);
        }
        printf("\n");
    }

    printf("Total %s tests: %lu, success: %lu, error: %lu\n", name, num_cases, total_success, num_cases - total_success);
}

void
run_tests() {
    test_method(number_cases, sizeof(number_cases), tokenize_number, "number");
    test_method(string_cases, sizeof(string_cases), tokenize_string, "string");
}

#endif