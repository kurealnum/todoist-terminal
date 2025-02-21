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
#include <unistd.h>

#define baseUrl "https://api.todoist.com/rest/v2/"

// Structs
struct memory {
  char *response;
  size_t size;
};

struct menuTask {
  char *content;
};

// Url args can simply be added to the url itself
struct curlArgs {
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

void displayError(char *message) {
  clear();
  printw("%s", message);
  refresh();
  getch();
  clear();
}

// Helper function for making a request. Return value needs to be free()-ed
cJSON *makeRequest(struct curlArgs curlArgs) {
  struct memory requestData;
  cJSON *requestsJson;

  requestData.response = malloc(1);
  requestData.size = 0;

  CURL *curl = curlArgs.curl;

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteHelper);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlArgs.headers);
  curl_easy_setopt(curl, CURLOPT_URL, curlArgs.url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&requestData);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, curlArgs.method);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    displayError("curl_easy_perform() failed.");
  } else {
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode == 204) {
      cJSON *blank = cJSON_CreateArray();
      return blank;
    }

    requestsJson = cJSON_Parse(requestData.response);

    if (requestsJson == NULL) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
        clear();
        printw("%s", error_ptr);
        refresh();
        getch();
        clear();
      } else {
        displayError("Failed to parse JSON. Error could not be shown.");
      }
    } else {
      return requestsJson;
    }
  }

  return requestsJson;
}

// Renders a menu. Must be free()-ed when finished using
MENU *renderMenuFromJson(cJSON *json, char *query) {
  cJSON *currentTask = NULL;
  int itemsLength = cJSON_GetArraySize(json);
  ITEM **items;

  items = (ITEM **)malloc((itemsLength + 1) * sizeof(struct ITEM *));
  for (int i = 0; i < itemsLength; i++) {
    cJSON *curItem = cJSON_GetArrayItem(json, i);
    if (curItem == NULL) {
      return NULL;
    }
    cJSON *curContent = cJSON_GetObjectItemCaseSensitive(curItem, query);
    if (curContent == NULL) {
      return NULL;
    }
    items[i] = new_item(curContent->valuestring, "");
  }
  items[itemsLength] = (ITEM *)NULL;
  MENU *menu = new_menu(items);
  return menu;
}

cJSON *getCurrentItemJson(MENU *menu, cJSON *json) {
  ITEM *currentItem = current_item(menu);
  int currentItemIndex = item_index(currentItem);
  cJSON *currentItemJson = cJSON_GetArrayItem(json, currentItemIndex);
  return currentItemJson;
}

// Function for rendering a certain project's tasks. Check out Todoist itself
// for a little bit more insight on how this is set up.
void projectPanel(struct curlArgs curlArgs, int row, int col) {
  PANEL *projectPanel;
  WINDOW *projectWindow;

  // Query for list of currently open tasks
  char *tasksUrl = combineString(baseUrl, "tasks");
  cJSON *tasksJson = makeRequest(curlArgs);
  int tasksLength = cJSON_GetArraySize(tasksJson);

  // Get menu
  MENU *tasksMenu = renderMenuFromJson(tasksJson, "content");
  int menuCol = 1;
  int *menuRow = &tasksLength;
  int res = set_menu_format(tasksMenu, *menuRow, menuCol);

  // Render
  projectWindow = newwin(row, col, 0, 0);
  projectPanel = new_panel(projectWindow);
  update_panels();
  post_menu(tasksMenu);
  wrefresh(projectWindow);
  refresh();

  // Event loop (ish?)
  int getchChar;
  while ((getchChar = getch()) != 'q') {
    if (getchChar == KEY_DOWN || getchChar == 'j') {
      menu_driver(tasksMenu, REQ_DOWN_ITEM);
    } else if (getchChar == KEY_UP || getchChar == 'k') {
      menu_driver(tasksMenu, REQ_UP_ITEM);
    } else if (getchChar == 'h') {
      // Go "up", back to the projects menu
      break;
    } else if (getchChar == 'p') {
      // Mark item as completed
      cJSON *currentItemJson = getCurrentItemJson(tasksMenu, tasksJson);
      ITEM *currentItem = current_item(tasksMenu);
      if (currentItemJson == NULL) {
        displayError("Something went wrong when closing the task. Press any "
                     "key to return to the projects menu.");
        return;
      }
      cJSON *currentItemIdJson =
          cJSON_GetObjectItemCaseSensitive(currentItemJson, "id");
      if (currentItemIdJson == NULL) {
        displayError("Something went wrong when closing the task. Press any "
                     "key to return to the projects menu.");
        return;
      }
      char *currentItemId = currentItemIdJson->valuestring;
      if (currentItemId == NULL) {
        displayError("Something went wrong when closing the task. Press any "
                     "key to return to the projects menu.");
        return;
      }
      char *closeTaskUrl = (char *)malloc(100);
      strcpy(closeTaskUrl, baseUrl);
      strcat(closeTaskUrl, "tasks/");
      strcat(closeTaskUrl, currentItemId);
      strcat(closeTaskUrl, "/close");

      struct curlArgs markCompleteArgs = {curlArgs.curl, curlArgs.headers,
                                          "POST", closeTaskUrl};
      cJSON *markCompleteJson = makeRequest(markCompleteArgs);
      free(closeTaskUrl);
      if (markCompleteJson == NULL) {
        displayError("Something went wrong when making an API request to close "
                     "the task. Press any key to return to the projects menu.");
        return;
      }

      // If it isn't null, the request was successfull, so we can update the
      // menu.
      // Free old items first
      ITEM **taskItems = menu_items(tasksMenu);
      for (int i = 0; i < tasksLength; i++) {
        free_item(taskItems[i]);
      }

      unpost_menu(tasksMenu);

      // Delete completed task
      int completedTaskIndex = item_index(currentItem);
      cJSON_DeleteItemFromArray(tasksJson, completedTaskIndex);

      // Create new items (will need to be free()-ed)
      int newItemsLength = cJSON_GetArraySize(tasksJson);
      ITEM **newItems =
          (ITEM **)malloc((newItemsLength + 1) * sizeof(struct ITEM *));
      for (int i = 0; i < newItemsLength; i++) {
        cJSON *curItem = cJSON_GetArrayItem(tasksJson, i);
        if (curItem == NULL) {
          displayError("Something went wrong when updating the Ncurses menu."
                       "Press any key to return to the projects menu. (Err 1)");
          return;
        }
        cJSON *curContent =
            cJSON_GetObjectItemCaseSensitive(curItem, "content");
        if (curContent == NULL) {
          displayError("Something went wrong when updating the Ncurses menu."
                       "Press any key to return to the projects menu. (Err 2)");
          return;
        }
        newItems[i] = new_item(curContent->valuestring, "");
      }
      newItems[newItemsLength] = (ITEM *)NULL;

      // Re-render menu
      set_menu_items(tasksMenu, newItems);
      post_menu(tasksMenu);
      refresh();
    }
  }

  del_panel(projectPanel);
  unpost_menu(tasksMenu);
  update_panels();
  refresh();

  // Free variables and whatnot
  ITEM **taskItems = menu_items(tasksMenu);
  free(tasksUrl);
  free(tasksJson);
  for (int i = 0; i < tasksLength; i++) {
    free_item(taskItems[i]);
  }
  free_menu(tasksMenu);
}

int main(void) {
  // ncurses
  initscr();
  keypad(stdscr, TRUE);
  raw();
  noecho();
  printw("Loading current projects. Press q to exit.\n");
  refresh();
  int row, col;
  getmaxyx(stdscr, row, col);

  // curl
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
    struct curl_slist *baseHeaders = NULL;
    baseHeaders = curl_slist_append(baseHeaders, authHeader);

    // Get list of projects
    struct memory projects;
    projects.response = malloc(1);
    projects.size = 0;
    char *allProjectsUrl = combineString(baseUrl, "projects");
    struct curlArgs allProjectsCurlArgs = {curl, baseHeaders, "GET",
                                           allProjectsUrl};
    cJSON *projectsJson = makeRequest(allProjectsCurlArgs);
    int numOfProjects = cJSON_GetArraySize(projectsJson);

    // Adding a cJSON object so the user can also view active tasks (AKA the
    // today section)
    cJSON *today = cJSON_CreateObject();
    if (today == NULL) {
      goto end;
    }
    if (cJSON_AddStringToObject(today, "name", "Today") == NULL) {
      goto end;
    }

    // Adding a "fake" ID field to help with managing menu (see event loop ~20
    // lines down)
    if (cJSON_AddStringToObject(today, "id", "999") == NULL) {
      goto end;
    }
    cJSON_AddItemToArray(projectsJson, today);

    MENU *projectsMenu = renderMenuFromJson(projectsJson, "name");
    if (projectsMenu == NULL) {
      goto end;
    }

    // Render
    clear();
    post_menu(projectsMenu);
    refresh();

    // For free()-ing
    ITEM **projectsItems = menu_items(projectsMenu);

    int getchChar;
    while ((getchChar = getch()) != 'q') {
      if (getchChar == KEY_DOWN || getchChar == 'j') {
        menu_driver(projectsMenu, REQ_DOWN_ITEM);
      } else if (getchChar == KEY_UP || getchChar == 'k') {
        menu_driver(projectsMenu, REQ_UP_ITEM);
      } else if (getchChar == 'q') {
        break;
      } else if (getchChar == 'l') {
        // Find project ID, and call projectPanel with that project ID in a
        // curlArgs struct
        cJSON *currentItemJson = getCurrentItemJson(projectsMenu, projectsJson);
        if (currentItemJson == NULL) {
          displayError("Something went wrong when trying to access the current "
                       "item of the Ncurses menu. Press any key to quit.");
          goto end;
        }
        cJSON *projectIDJson =
            cJSON_GetObjectItemCaseSensitive(currentItemJson, "id");
        if (projectIDJson == NULL) {
          displayError("JSON for project ID is null. Press any key to quit.");
          goto end;
        }

        char *projectID = projectIDJson->valuestring;
        char *tasksUrl;
        if (strcmp(projectID, "999") == 0) {
          tasksUrl = combineString(baseUrl, "tasks/?filter=today");
        } else {
          tasksUrl = combineString(baseUrl, "tasks/?project_id=");
          tasksUrl = combineString(tasksUrl, projectID);
        }

        struct curlArgs projectPanelCurlArgs = {curl, baseHeaders, "GET",
                                                tasksUrl};
        projectPanel(projectPanelCurlArgs, row, col);

        // Scuffed fix for projects list not loading after exiting from project
        // panel
        menu_driver(projectsMenu, REQ_NEXT_ITEM);
        menu_driver(projectsMenu, REQ_PREV_ITEM);

        // Once projectPanel returns, the user has exited the panel.
        free(tasksUrl);
      }
    }

  end:
    // Cleanup and free variables
    curl_easy_cleanup(curl);
    free(authHeader);
    free(projectsMenu);
    free(projectsJson);
    free(projects.response);
    for (int i = 0; i < numOfProjects; i++) {
      free(projectsItems[i]);
    }
    endwin();
  }
  curl_global_cleanup();
}
