// Ncurses work is at the very least partial courtesy of Pradeep Padala:
// https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/index.html

#include <cdk/dialog.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curses.h>
#include <menu.h>
#include <ncurses.h>
#include <panel.h>
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

struct menuTask {
  char *content;
};

struct curlInstructions {
  CURL *curl;
  struct curl_slist *headers;
  char *method;
  char *url;
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
    return 0;
  }

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

// Helper function for making a request. Return value needs to be free()-ed
cJSON *makeRequest(struct curlInstructions curlInstructions) {
  struct memory requestData;
  cJSON *requestsJson;

  requestData.response = malloc(1);
  requestData.size = 0;

  CURL *curl = curlInstructions.curl;

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteHelper);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlInstructions.headers);
  curl_easy_setopt(curl, CURLOPT_URL, curlInstructions.url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&requestData);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, curlInstructions.method);

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

void projectPanel(struct curlInstructions curlInstructions, int row, int col) {
  PANEL *projectPanel;
  WINDOW *projectWindow;

  // Query for list of currently open tasks
  struct memory tasks;
  tasks.response = malloc(1);
  tasks.size = 0;
  char *tasksUrl = combineString(baseUrl, "tasks");
  cJSON *tasksJson = makeRequest(curlInstructions);

  // Show results to user
  cJSON *currentTask = NULL;
  int tasksLength = cJSON_GetArraySize(tasksJson);
  ITEM **menuTasks;

  menuTasks = (ITEM **)calloc(tasksLength + 1, sizeof(struct menuTask));
  for (int i = 0; i < tasksLength; i++) {
    cJSON *curItem = cJSON_GetArrayItem(tasksJson, i);
    cJSON *curContent = cJSON_GetObjectItemCaseSensitive(curItem, "content");
    menuTasks[i] = new_item(curContent->valuestring, curContent->valuestring);
  }
  menuTasks[tasksLength] = (ITEM *)NULL;

  // Render
  projectWindow = newwin(row, col, 0, 0);
  projectPanel = new_panel(projectWindow);
  update_panels();
  doupdate();

  MENU *tasksMenu = new_menu((ITEM **)menuTasks);
  post_menu(tasksMenu);
  refresh();

  // Event loop (of sorts)
  int getchChar;
  while ((getchChar = getch()) != 'q') {
    if (getchChar == KEY_DOWN || getchChar == 'j') {
      menu_driver(tasksMenu, REQ_DOWN_ITEM);
    } else if (getchChar == KEY_UP || getchChar == 'k') {
      menu_driver(tasksMenu, REQ_UP_ITEM);
    } else if (getchChar == 'h') {
      // Go "up"
      ITEM *currentItem = current_item(tasksMenu);
      hide_panel(projectPanel);
    }
  }

  // Free variables and whatnot
  free(tasksUrl);
  free(tasks.response);
  free(tasksJson);
  for (int i = 0; i < tasksLength; i++) {
    free_item(menuTasks[i]);
  }
  free_menu(tasksMenu);
}

int main(void) {

  // ncurses
  initscr();
  keypad(stdscr, TRUE);
  raw();
  noecho();
  printw("Loading current tasks. Press q to exit.\n");
  refresh();
  int row, col;
  getmaxyx(stdscr, row, col);

  CURL *curl = curl_easy_init();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  if (!curl) {
    printf("curl didn't initalize correctly.\n");
    return 1;
  } else {
    // Get auth token from environment
    char *authToken = getenv("TODOIST_AUTH_TOKEN");

    if (authToken == NULL) {
      printw(
          "Unable to find auth token. Press any button to end the program.\n");
      refresh();
      getch();
      curl_easy_cleanup(curl);
      curl_global_cleanup();
      endwin();
      return 1;
    }

    char *authHeader = combineString("Authorization: Bearer ", authToken);
    struct curl_slist *baseList = NULL;
    baseList = curl_slist_append(baseList, authHeader);

    // Get list of projects (TODO)

    // Let user choose what project to look at (TODO -- this will involve the
    // projectPanel function)

    // Cleanup and free variables
    curl_easy_cleanup(curl);
    free(authHeader);
    endwin();
  }
  curl_global_cleanup();
}
