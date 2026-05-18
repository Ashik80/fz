#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "fuzzy.h"

const char * fuzzy_match(const char *haystack, const char *needle) {
    size_t ni = 0;
    size_t hi = 0;
    size_t nlen = strlen(needle);
    while (haystack[hi] != '\0') {
        if (tolower(haystack[hi]) == tolower(needle[ni])) {
            ni++;
        }
        hi++;
    }
    return ni == nlen ? haystack : NULL;
}

int fuzzy_score(const char *haystack, const char *needle) {
    int score = 0;
    int consecutive = 0;
    size_t ni = 0;
    size_t hi = 0;
    size_t nlen = strlen(needle);
    char *last_slash = strrchr(haystack, '/');
    size_t last_slash_index = last_slash != NULL ? (size_t)(last_slash - haystack) : 0;
    size_t in_last_segment = 0;
    int depth = 0;
    // depth penalty
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        if (haystack[i] == '/')
            depth++;
    }
    score -= depth * 2;
    // last occurence reward
    while (haystack[hi] != '\0') {
        if (tolower(haystack[hi]) == tolower(needle[ni])) {
            // consecutive reward
            if (consecutive > 0) {
                score += consecutive * 3;
            }
            if (last_slash == NULL || hi > last_slash_index) in_last_segment++;
            // first letter in a segment reward
            if (hi == 0
                    || haystack[hi - 1] == ' '
                    || haystack[hi - 1] == '/'
                    || haystack[hi - 1] == '_'
                    || haystack[hi - 1] == '-'
                    || haystack[hi - 1] == '.') {
                score += 5;
            }
            score++;
            consecutive++;
            ni++;
            if (ni == nlen) break;
        } else {
            // not consecutive penalty
            if (consecutive > 0) score--;
            consecutive = 0;
        }
        hi++;
    }
    if (nlen == in_last_segment) {
        score += 20;
    }
    return ni == nlen ? score : NO_MATCH;
}
