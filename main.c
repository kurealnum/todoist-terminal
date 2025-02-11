#include <curl/curl.h>
#include <curl/easy.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define baseUrl "https://api.todoist.com/rest/v2/"

int main(void) {
  printf("Current tasks\n");
  CURL *curl = curl_easy_init();
  if (curl) {
    // setup
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      return 1;
    }

    char *authHeader = malloc(
        (strlen("Authorization: Bearer ") + strlen(authToken)) * sizeof(char));
    *authHeader = '\0';
    strcat(authHeader, "Authorization: Bearer ");
    strcat(authHeader, authToken);
    printf("%s\n", authHeader);

    /*char projectsUrl[100];*/
    /*strcat(projectsUrl, strcat(baseUrl, projectsUrl));*/
    /*if (authToken != NULL) {*/
    /*  curl_easy_setopt(curl, CURLOPT_HEADER, authHeader);*/
    /*} else {*/
    /*  return 1;*/
    /*}*/
    /*curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");*/
    /*curl_easy_setopt(curl, CURLOPT_URL, projectsUrl);*/
    /**/
    /*CURLcode currentTasks = curl_easy_perform(curl);*/
    /*curl_easy_cleanup(curl);*/
    free(authHeader);
  }
  curl_global_cleanup();
}
