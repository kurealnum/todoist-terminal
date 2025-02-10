#include <curl/curl.h>
#include <curl/easy.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  printf("Current tasks\n");
  CURL *curl = curl_easy_init();
  if (curl) {
    // setup
    char baseUrl[] = "https://api.todoist.com/rest/v2/";
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    char authHeader[100] = "Authorization: Bearer";
    strcat(authHeader, authToken);

    char projectsUrl[100];
    strcat(projectsUrl, strcat(baseUrl, projectsUrl));
    if (authToken != NULL) {
      curl_easy_setopt(curl, CURLOPT_HEADER, authHeader);
    } else {
      return 1;
    }
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, projectsUrl);

    CURLcode currentTasks = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
  }
  curl_global_cleanup();
}
