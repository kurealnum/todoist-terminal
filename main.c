#include <curl/curl.h>
#include <curl/easy.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define baseUrl "https://api.todoist.com/rest/v2/"

char *combineString(char *str1, char *str2);

int main(void) {
  printf("Current tasks\n");
  CURL *curl = curl_easy_init();
  if (curl) {
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      printf("Unable to find auth token.\n");
      return 1;
    }

    char *authHeader = combineString("Authorization: Bearer ", authToken);
    char *projectsUrl = combineString(baseUrl, "projects/");

    curl_easy_setopt(curl, CURLOPT_HEADER, authHeader);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, projectsUrl);

    CURLcode currentTasks = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    // free variables
    free(authHeader);
  } else {
    printf("curl didn't initalize correctly.\n");
  }
  curl_global_cleanup();
}

char *combineString(char *str1, char *str2) {
  char *newString = malloc((strlen(str1) + strlen(str2) + 1) * sizeof(char));
  if (newString == NULL) {
    return NULL;
  }
  strcpy(newString, str1);
  strcat(newString, str2);
  return newString;
}
