#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define baseUrl "https://api.todoist.com/rest/v2/"

// Structs
struct memory {
  char *response;
  size_t size;
};

// Helper function for combining two strings. Needs to be free()-ed.
char *combineString(char *str1, char *str2) {
  char *newString = malloc((strlen(str1) + strlen(str2) + 1) * sizeof(char));
  if (newString == NULL) {
    return NULL;
  }
  strcpy(newString, str1);
  strcat(newString, str2);
  return newString;
}

// Helper function for writing with curl
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

// Helper function for making a request. Return value needs to be free()-ed
cJSON *makeRequest(CURL *curl, struct curl_slist *headers, char *url) {
  struct memory requestData;
  cJSON *requestsJson;

  requestData.response = malloc(1);
  requestData.size = 0;

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteHelper);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&requestData);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  } else {
    requestsJson = cJSON_Parse(requestData.response);

    if (requestsJson == NULL) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
        fprintf(stderr, "Error before: %s\n", error_ptr);
      } else {

        printf("Failed to parse JSON. Error could not be shown.\n");
      }
    } else {
      return requestsJson;
    }
  }

  return requestsJson;
}

int main(void) {
  printf("Current tasks\n");
  CURL *curl = curl_easy_init();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  if (!curl) {
    printf("curl didn't initalize correctly.\n");
    return 1;
  } else {
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      printf("Unable to find auth token.\n");
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      return 1;
    }

    struct memory tasks;
    tasks.response = malloc(1);
    tasks.size = 0;

    char *authHeader = combineString("Authorization: Bearer ", authToken);
    struct curl_slist *list = curl_slist_append(list, authHeader);
    char *tasksUrl = combineString(baseUrl, "tasks");
    cJSON *tasksJson = makeRequest(curl, list, tasksUrl);
    char *x = cJSON_Print(tasksJson);
    printf("%s\n", x);

    // cleanup and free variables
    curl_easy_cleanup(curl);
    free(authHeader);
    free(tasksUrl);
    free(tasks.response);
  }
  curl_global_cleanup();
}
