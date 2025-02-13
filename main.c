#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define baseUrl "https://api.todoist.com/rest/v2/"

struct memory {
  char *response;
  size_t size;
};

char *combineString(char *str1, char *str2) {
  char *newString = malloc((strlen(str1) + strlen(str2) + 1) * sizeof(char));
  if (newString == NULL) {
    return NULL;
  }
  strcpy(newString, str1);
  strcat(newString, str2);
  return newString;
}

static size_t curlWriteHelper(char *data, size_t size, size_t nmemb,
                              void *clientp) {
  size_t realsize = size * nmemb;
  struct memory *mem = (struct memory *)clientp;

  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if (!ptr) {
    printf("Realloc ran out of memory\n");
    return 0; /* out of memory */
  }

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

int main(void) {
  printf("Current tasks\n");
  CURL *curl = curl_easy_init();
  CURLcode res;
  struct memory tasks;
  struct curl_slist *list = NULL;

  tasks.response = malloc(1);
  tasks.size = 0;

  curl_global_init(CURL_GLOBAL_DEFAULT);

  if (curl) {
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      printf("Unable to find auth token.\n");
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      return 1;
    }

    char *authHeader = combineString("Authorization: Bearer ", authToken);
    char *tasksUrl = combineString(baseUrl, "tasks");

    list = curl_slist_append(list, authHeader);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteHelper);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_URL, tasksUrl);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&tasks);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    } else {
      cJSON *tasksJson = cJSON_Parse(tasks.response);

      if (tasksJson == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
          fprintf(stderr, "Error before: %s\n", error_ptr);
        } else {

          printf("Failed to parse JSON. Error could not be shown.\n");
        }
      } else {
        char *string = cJSON_Print(tasksJson);
        printf("%s\n", string);
      }
    }

    // cleanup and free variables
    curl_easy_cleanup(curl);
    free(authHeader);
    free(tasksUrl);
    free(tasks.response);
  } else {
    printf("curl didn't initalize correctly.\n");
  }
  curl_global_cleanup();
}
