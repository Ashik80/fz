#ifndef FUZZY_H
#define FUZZY_H

#define NO_MATCH -200

const char * fuzzy_match(const char *haystack, const char *needle);
int fuzzy_score(const char *haystack, const char *needle);

#endif
