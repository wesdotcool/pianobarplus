

#include "utils.h"

void replaceChars (char* str, char bad, char good){
  int i = 0;
  while (str[i] != 0) {
    if (str[i] == bad)
      str[i] = good;
    i += 1;
  }

}
